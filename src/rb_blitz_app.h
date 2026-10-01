// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.
//
// Hook hygiene (docs/backlog.md §1): this is the one file whose hooks are
// rex::ReXApp's, so each override below states what the SDK does by itself and why
// this app differs. In short:
//   * OnPreSetup - the SDK runs its configured backends and input devices; here the
//     GPU plugin defaults to xenos when none is named (the only one staged), and the
//     mouse driver is appended to whatever input factory is configured. Both are
//     additive; a configured plugin and a configured factory are honoured.
//   * OnConfigurePaths - faithfully, every path is honoured as given; here a writable
//     root that resolves inside the read-only game tree is dropped in favour of the
//     platform user directory, so a launcher cannot make the game write into its own
//     data. Any other directory is honoured as given.
//   * ApplyContentLicense - the emulated console owns no licence, which sends the
//     title down the trial path; here license_mask defaults to 1 (treated as
//     purchased), and only when nothing else - config file, REX_* environment or
//     command line - named a mask, so `license_mask = 0` restores the faithful path.
//   * OnPostLoadXexImage - the SDK starts the guest unchanged; here the Ultimate and
//     DLC layers run first, and each is a no-op without its payload/directory.
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
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging/macros.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/version.h>

#include "generated/fingerprint_expected.h"
#include "generated/toolchain_build.h"
#include "fs/path_policy.h"
#include "hooks/dlc.h"
#include "hooks/ultimate.h"
#include "input/mouse_ui.h"
#include "util/sha256.h"

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

    // Writable roots default outside the game tree (platform user dir).
    if (paths.user_data_root.empty()) {
      paths.user_data_root = rex::filesystem::GetUserFolder() / "rb_blitz";
    }
    if (paths.cache_root.empty()) {
      paths.cache_root = paths.user_data_root / "cache";
    }
  }

  // Boot identity and mod compatibility. Called once the XEX is loaded and the
  // paths are final, which is the first point where game_data_root() is known,
  // logging is up and the guest has not started yet.
  void OnPostLoadXexImage() override {
    LogBootIdentity();
    rb_blitz::ultimate::Configure(runtime(), game_data_root());
    // After ultimate::Configure, which decides what the guest-visible game tree
    // looks like: DLC is resolved by the SDK content manager from a host path, so
    // the two do not interact, but the DLC log line reads better next to the boot
    // identity and the payload line than before them.
    rb_blitz::dlc::Configure(runtime(), game_data_root());
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
  void LogBootIdentity() {
    REXLOG_INFO("boot identity: {}", REXGLUE_BUILD_STAMP);

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
};
