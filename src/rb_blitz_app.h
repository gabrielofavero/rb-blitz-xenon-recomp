// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.
//
// Hook hygiene (docs/rb3-references.md §8): this is the one file whose hooks are
// rex::ReXApp's, so each override below states what the SDK does by itself and why
// this app differs. In short:
//   * OnPreSetup - the SDK runs its configured backends and input devices; here the
//     GPU plugin defaults to xenos when none is named (the only one staged), the
//     graphics backend named by `gpu_backend` is loaded first (refusing an empty or
//     "any" value, so the SDK's own load is untouched), and the mouse driver is
//     appended to whatever input factory is configured. Both are additive; a configured
//     plugin and a configured factory are honoured.
//   * OnConfigurePaths - faithfully, every path is honoured as given; here a writable
//     root that resolves inside the read-only game tree is dropped in favour of the
//     platform user directory, so a launcher cannot make the game write into its own
//     data. Any other directory is honoured as given.
//   * ApplyContentLicense - the emulated console owns no licence, which sends the
//     title down the trial path; here license_mask defaults to 1 (treated as
//     purchased), and only when nothing else - config file, REX_* environment or
//     command line - named a mask, so `license_mask = 0` restores the faithful path.
//   * OnPostLoadXexImage - the SDK starts the guest unchanged; here the Ultimate and
//     DLC layers run first, and each is a no-op without its payload/directory. R9's
//     shell-music layer runs last and only when its toggle is on and the DLC layer
//     found a library: it mounts a few packages read-only and redirects the hook the
//     main menu's music goes through, and with the toggle off it is inert.
//   * OnPreLaunchModule - the SDK never finalizes the cvar registry, so a key in
//     rb_blitz.toml that matches no cvar stays silent; here the registry is finalized
//     last, which reports such a key and locks the kInitOnly flags.
//   * OnCreateDialogs - the SDK adds its own overlays (F3 debug, console, settings);
//     here the probe overlay joins them, so there is still one ImGui context and one
//     renderer. It draws only while probe_overlay is on; with the cvar off it changes
//     nothing (diagnostics, see src/diag/probe.h).
//   * LogBootIdentity - diagnostics only; it changes no guest behaviour.
//
//   Every early return above is either the faithful path or a refusal to deviate
//   further: a configured plugin, factory, path or mask leaves the SDK's own
//   behaviour alone, and a missing payload or directory makes the added layers
//   no-ops.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging/macros.h>
#include <rex/platform/dynlib.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/window.h>
#include <rex/version.h>

#include "generated/fingerprint_expected.h"
#include "generated/toolchain_build.h"
#include "diag/probe.h"
#include "enhancements.h"
#include "fs/path_policy.h"
#include "hooks/menu_filter.h"
#include "hooks/dlc.h"
#include "hooks/content_progress.h"
#include "hooks/shell_music.h"
#include "hooks/ultimate.h"
#include "input/mouse_ui.h"
#include "input/remap.h"
#include "util/sha256.h"

// The graphics backend the game renders with, defined in src/main.cpp beside the app it belongs
// to. It is this project's cvar rather than the SDK's because choosing a backend is the *app's*
// decision: the GPU plugin can only run the backends it was compiled with, so the load below is
// where the choice can be honoured or refused.
REXCVAR_DECLARE(std::string, gpu_backend);

class RbBlitzApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<RbBlitzApp>(new RbBlitzApp(ctx, "rb_blitz",
        PPCImageConfig));
  }

  // Runtime/backend selection. Called before Runtime::Setup(); the GPU plugin
  // is loaded immediately after this hook returns, so set the plugin name here.
  // The `rexgpu-xenos` DLL is staged next to the executable via
  // rexglue_setup_target(rb_blitz GPU_PLUGINS xenos) in CMakeLists.txt.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    SelectGpuBackend(config);
    ApplyContentLicense();
    // The runtime reads input_factory the moment this hook returns, and the
    // input system is built from it, so this is the point where an extra input
    // device can still be added. See src/input/mouse_ui.h.
    //
    // The presenter is the only handle on the guest's own frames, and it does
    // not exist yet here, so it is handed over as a lazy lookup instead of a
    // pointer. Runtime::graphics_system() is the public route to it;
    // Window::presenter() is protected.
    rb_blitz::input::InstallMouseUiNavigation(config, [this] {
      rex::Runtime* runtime = this->runtime();
      rex::system::IGraphicsSystem* graphics = runtime ? runtime->graphics_system() : nullptr;
      return graphics ? graphics->presenter() : nullptr;
    });
    // The launcher's `[remap]` table, applied to the pad state the guest asks for. Its own
    // wrapper of input_factory, so both it and the mouse compose with whichever backend was
    // selected. See src/input/remap.h.
    rb_blitz::input::InstallPadRemap(config);
  }

  // The graphics backend, from `gpu_backend` (launcher/config/settings.toml's Renderer row).
  //
  // The SDK loads the GPU plugin itself, with its own default backend - "any", whichever
  // backend the plugin was compiled with. All this does is name one instead, and only when
  // something did: "any" and an empty value both return without touching `config`, so the
  // SDK's own load runs exactly as it always did and the faithful behaviour is unchanged.
  //
  // A named backend the payload does not have fails to load, is logged with the backend's
  // name by the plugin, and leaves `config.graphics` empty - so the SDK's own load runs anyway
  // and picks the backend the build does have. A wrong choice therefore degrades to the working
  // renderer instead of refusing to boot, and never silently: the log names what could not be
  // provided and what was used instead.
  static void SelectGpuBackend(rex::RuntimeConfig& config) {
    if (config.graphics || config.gpu_plugin.empty()) {
      return;
    }
    const std::string backend = REXCVAR_GET(gpu_backend);
    if (backend.empty() || backend == "any") {
      return;
    }
    // A backend this *payload* has but this *machine* cannot run: the SDK loads the Vulkan
    // loader at run time rather than linking it, so the plugin is happy to build a Vulkan
    // graphics system on a box with no Vulkan driver, and the failure only lands later, inside
    // presentation setup - where the backend is already committed and there is nothing left to
    // fall back to. Checking the loader first is what keeps "a wrong choice degrades to the
    // renderer that works" true for the case that actually happens.
    if (backend == "vulkan" && !VulkanLoaderIsUsable()) {
      REXLOG_WARN("graphics backend 'vulkan' was asked for, but {} is not usable here; "
                  "falling back to the backend this build would have picked by itself",
                  rex::platform::lib_names::kVulkanLoader);
      return;
    }
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
  }

  // True when the Vulkan loader is present and looks like a loader. Whether it has a usable
  // *device* is deliberately left to the provider: enumerating physical devices here would mean
  // a second Vulkan initialization in the same process, and the provider has to do it anyway.
  static bool VulkanLoaderIsUsable() {
    rex::platform::DynamicLibrary loader;
    if (!loader.Load(rex::platform::lib_names::kVulkanLoader)) {
      return false;
    }
    return loader.GetRawSymbol("vkGetInstanceProcAddr") != nullptr;
  }

  // Path policy. Called before logging is initialized, so keep this silent.
  //
  // game_data_root (read-only) comes from --game_data_root / the cvar and is
  // mounted read-only by the SDK (allow_game_relative_writes defaults off).
  // Writable state (user/update/cache) must never live inside game_data_root: a
  // launcher that asks for that gets the platform user directory instead. Any
  // other directory is honoured as given - see rb_blitz::fs::IsSameOrInside()
  // for why the check is not a std::filesystem::relative() call.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    auto redirect_if_inside_game_root = [&](std::filesystem::path& p) {
      if (rb_blitz::fs::IsSameOrInside(p, paths.game_data_root)) {
        p.clear();
      }
    };

    redirect_if_inside_game_root(paths.user_data_root);
    redirect_if_inside_game_root(paths.cache_root);
    redirect_if_inside_game_root(paths.update_data_root);

    // Writable roots default outside the game tree (platform user dir). The runtime has already
    // filled user_data_root with GetUserFolder()/GetName() ("rb_blitz") before this hook, so when
    // nothing named a root explicitly it is replaced with the folder the launcher's *Save game
    // location* row shows - one spelling of the default, in both places. An explicit
    // --user_data_root, the cvar, or a hand-edited profile is left exactly as it is.
    if (REXCVAR_GET(user_data_root).empty()) {
      paths.user_data_root = rex::filesystem::GetUserFolder() / "Rock Band Blitz";
    }
    if (REXCVAR_GET(cache_root).empty()) {
      paths.cache_root = paths.user_data_root / "cache";
    }
  }

  // Boot identity and mod compatibility. Called once the XEX is loaded and the
  // paths are final, which is the first point where game_data_root() is known,
  // logging is up and the guest has not started yet.
  void OnPostLoadXexImage() override {
    LogBootIdentity();
    rb_blitz::enhancements::LogToggles();
    // R5 reads its own cvars here, before the guest starts: the read hook that
    // applies the menu filter runs on guest threads and must not touch the
    // registry, so the toggle and the row list are captured once (P1's D7).
    rb_blitz::menu_filter::Configure();
    rb_blitz::ultimate::Configure(runtime(), game_data_root());
    // After ultimate::Configure, which decides what the guest-visible game tree
    // looks like: DLC is resolved by the SDK content manager from a host path, so
    // the two do not interact, but the DLC log line reads better next to the boot
    // identity and the payload line than before them.
    rb_blitz::dlc::Configure(runtime(), game_data_root());
    // After dlc::Configure, whose package count is what the discovery screen's progress
    // bar is given: the toggle is read here, before the guest starts, and the count is
    // read from the DLC layer when the bar's own hooks run (src/hooks/content_progress.cpp).
    rb_blitz::content_progress::Configure();
    // After dlc::Configure, whose enumeration is the pool R9 draws from: with the
    // toggle on, a few loaded DLC songs are mounted read-only and the main menu's
    // shell-music loader is redirected to them (src/hooks/shell_music.cpp). With the
    // toggle off - or no library - nothing is mounted and the hook is inert.
    rb_blitz::shell_music::Configure(runtime(), game_data_root());
  }

  // Config-file hygiene. A key in rb_blitz.toml (or on the command line) that matches no
  // cvar is deferred at load time, because the cvar may register later - the GPU plugin's
  // do - and is only reported if something finalizes the registry. This is the last hook
  // before the guest thread is created and every cvar this build has is registered by now,
  // so finalizing here turns a misspelled key from a silent no-op into a log warning.
  //
  // FinalizeInit is the SDK's own end-of-initialization point: it also stops any further
  // change to kInitOnly flags, which is correct here - those are device and logging
  // selections that the config, environment and command line have already fixed.
  void OnPreLaunchModule() override { rex::cvar::FinalizeInit(); }

  // Diagnostics. The probe overlay is an ordinary SDK dialog: its constructor
  // registers it with the drawer and its destructor unregisters it, so this only
  // has to own it for as long as the app lives. Whether it draws is the
  // `probe_overlay` cvar's decision, re-read every frame, which is what makes the
  // toggle live; the cvar off draws nothing at all.
  //
  // The one thing the dialog cannot reach is the window (it lives here), so it is
  // handed over as a callback rather than a pointer - the same lazily-called shape
  // as the mouse driver's presenter lookup in OnPreSetup.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    probe_overlay_ = rb_blitz::diag::CreateOverlayDialog(drawer, [this] {
      rb_blitz::diag::WindowState state;
      const rex::ui::Window* w = window();
      if (w == nullptr) {
        return state;
      }
      state.physical_width = w->GetActualPhysicalWidth();
      state.physical_height = w->GetActualPhysicalHeight();
      state.logical_width = w->GetActualLogicalWidth();
      state.logical_height = w->GetActualLogicalHeight();
      state.fullscreen = w->IsFullscreen();
      // The window reports both sizes; the ratio is the only reason to want a
      // scale, and a logical size of zero (window not yet realised) means one.
      if (state.logical_width != 0) {
        state.dpi_scale = static_cast<float>(state.physical_width) /
                          static_cast<float>(state.logical_width);
      }
      return state;
    });
  }

 private:
  // Content licence state, the emulated console's answer to "does this profile
  // own this title?". XamContentGetLicenseMask hands the guest the
  // `license_mask` kernel cvar verbatim, and this is the XBLA build of Rock Band
  // Blitz: it asks for that mask early in boot and takes the trial path whenever
  // the mask is zero, which is the mode that does not keep scores. ReXGlue
  // defaults the mask to zero, i.e. "a
  // console that owns no licence", and no rb_blitz.toml is shipped - by design,
  // see installer/tools/make_payload.ps1 - so an installed build boots the full
  // game as a trial. Playing the user's own dump is not that situation, and the
  // reference baseline boots with a licence enabled.
  //
  // Called after the config file, the REX_* environment and the command line
  // have been applied, and only writes when none of them named a mask, so
  // `license_mask = 0` in rb_blitz.toml or --license_mask=0 still selects the
  // trial path - that is how a trial-only bug stays reproducible.
  void ApplyContentLicense() {
    if (rex::cvar::GetFlagSource("license_mask") != rex::cvar::Source::kDefault) {
      REXLOG_INFO("content licence: license_mask = {} (configured)",
                  rex::cvar::GetFlagByName("license_mask"));
      return;
    }

    // The cvar's storage lives in rexruntime.dll, so the write goes through the
    // declaration in <rex/system/flags.h> rather than the registry.
    REXCVAR_SET(license_mask, 1);
    REXLOG_INFO("content licence: license_mask = 1 (default; the title is treated as purchased)");
  }

  // The build gate in CMakeLists.txt has already refused to recompile against a
  // dump other than the one config/game_fingerprints.toml describes, but a
  // launcher can still point a built binary at a different game/ tree - a
  // tree with a Rock Band Blitz Ultimate payload merged into it, for instance.
  // Nothing here aborts: the point is that the log says which revision (if any)
  // the running executable was actually booted against, so a bug report can be
  // matched to it.
  void LogBootIdentity() {    REXLOG_INFO("boot identity: {}", REXGLUE_BUILD_STAMP);

    // Which compiler, CMake, Ninja, MSVC toolset, Windows SDK and SDK commit built
    // this binary, with the frozen set named in the same line when it is one. It is
    // the build's half of what the game data identity below is the runtime's half
    // of: a log states the provenance of both sides before any other evidence in it
    // is read (docs/toolchain.md).
    REXLOG_INFO("toolchain: {}", RBBLITZ_TOOLCHAIN_STAMP);

    const std::filesystem::path entrypoint =
        game_data_root() / rb_blitz::fingerprint::vanilla::kEntrypointPath;
    std::error_code ec;
    std::uint64_t size = 0;
    const std::string digest = rb_blitz::util::HashFileHex(entrypoint, &size, ec);
    if (digest.empty()) {
      REXLOG_WARN("game data identity: cannot read {} ({})", entrypoint.string(),
                  ec.message());
      return;
    }

    if (size == rb_blitz::fingerprint::vanilla::kEntrypointSize &&
        digest == rb_blitz::fingerprint::vanilla::kEntrypointSha256) {
      REXLOG_INFO("game data identity: {} {} ({} bytes, sha256 {})",
                  rb_blitz::fingerprint::vanilla::kGameName,
                  rb_blitz::fingerprint::vanilla::kXexVersion, size, digest);
      return;
    }

    REXLOG_WARN("game data identity: MODIFIED - {} is {} bytes, sha256 {}",
                entrypoint.string(), size, digest);
    REXLOG_WARN("  expected: {} bytes, sha256 {} (config/game_fingerprints.toml, see"
                " docs/ultimate-compat.md)",
                rb_blitz::fingerprint::vanilla::kEntrypointSize,
                rb_blitz::fingerprint::vanilla::kEntrypointSha256);
  }

  // Owned for the app's lifetime; it unregisters itself from the drawer when this
  // is destroyed, which happens before ~ReXApp() tears the drawer down.
  std::unique_ptr<rex::ui::ImGuiDialog> probe_overlay_;
};
