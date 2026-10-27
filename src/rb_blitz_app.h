// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.
//
// Hook hygiene (docs/reference/rb3-references.md §8): this is the one file whose hooks are
// rex::ReXApp's, so each override below states what the SDK does by itself and why
// this app differs. In short:
//   * OnPreSetup - the SDK runs its configured backends and input devices; here the
//     GPU plugin defaults to xenos when none is named (the only one staged), the
//     graphics backend named by `gpu_backend` is loaded first (refusing an empty or
//     "any" value, so the SDK's own load is untouched), and the mouse driver is
//     appended to whatever input factory is configured. Both are additive; a configured
//     plugin and a configured factory are honoured.
//   * OnConfigurePaths - faithfully, every path is honoured as given; here Controller Settings
//     profile's `[settings]` rows are applied at the config rank first (D3 rank 4, so a
//     game started without Controller Settings still sees the rows it saved, and the game's own
//     rb_blitz.toml - loaded by the SDK after this hook - still outranks it), and a
//     writable root that resolves inside the read-only game tree is dropped in favour of
//     the platform user directory, so Controller Settings cannot make the game write into its own
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
//   * OnConfigureFonts - the SDK registers its own font and adds nothing else; here the
//     caption overlay's system sans joins the same atlas (player-facing text next to the
//     SDK's 10 px debug face), with the SDK's font as the fallback when none can be read.
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
#include <rex/graphics/compile_trace.h>
#include "diag/shader_trace.h"
#include "graphics/preparation_loading.h"
#include "graphics/shader_coverage.h"
#include <rex/filesystem.h>
#include <rex/logging/macros.h>
#include <rex/platform/dynlib.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/presenter.h>
#include <rex/ui/overlay/achievement_notification.h>
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
#include "hooks/local_save.h"
#include "hooks/settings_bridge_hooks.h"
#include "hooks/settings_payload.h"
#include "hooks/shell_music.h"
#include "hooks/ultimate.h"
#include "loading/load_trace.h"
#include "loading/crash_recovery.h"
#include "input/mouse_ui.h"
#include "input/remap.h"
#include "input/title_input.h"
#include "profile/profile_apply.h"
#include "settings/boot.h"
#include "platform/game_restart.h"
#include "store/store_fetch.h"
#include "store/store_catalogue.h"
#include "store/album_art_service.h"
#include "store/store_library.h"
#include "store/store_probe.h"
#include "store/store_search_service.h"
#include "ui/help_overlay.h"
#include "ui/menu_music_player.h"
#include "ui/keyboard_theme.h"
#include "util/sha256.h"

// The graphics backend the game renders with, defined in src/main.cpp beside the app it belongs
// to. It is this project's cvar rather than the SDK's because choosing a backend is the *app's*
// decision: the GPU plugin can only run the backends it was compiled with, so the load below is
// where the choice can be honoured or refused.
REXCVAR_DECLARE(std::string, gpu_backend);


class RbBlitzApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  // The SDK creates the toast once per launch. Suppressing its UI leaves the
  // achievement manager's unlock, save, and reload paths running normally.
  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    if (REXCVAR_GET(enhancements_hide_achievements)) {
      return nullptr;
    }
    return rex::ReXApp::CreateAchievementNotificationDialog();
  }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<RbBlitzApp>(new RbBlitzApp(ctx, "rb_blitz",
        PPCImageConfig));
  }

  // Keep the SDK's internal name for config/save paths, and give the window
  // its player-facing name while retaining the SDK build identification.
  void OnOpened(rex::ui::UISetupEvent& event) override {
    event.target()->SetTitle("Rock Band Blitz " REXGLUE_BUILD_TITLE);
  }

  // Runtime/backend selection. Called before Runtime::Setup(); the GPU plugin
  // is loaded immediately after this hook returns, so set the plugin name here.
  // The `rexgpu-xenos` DLL is staged next to the executable via
  // rexglue_setup_target(rb_blitz GPU_PLUGINS xenos) in CMakeLists.txt.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
      if (rex::cvar::GetFlagByName("settings_safe_graphics") == "true")
        rex::cvar::SetFlagByName("gpu_backend", "d3d12");
      SelectGpuBackend(config);
    // Load the default plugin here too: its cvars must exist before presentation and runtime
    // setup consume them. Loading creates the graphics object; SetupPresentation follows later.
    if (!config.graphics && !config.gpu_plugin.empty()) {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
    }
    // Vulkan converts B-010 fixed-point textures directly from GPU shared memory
    // (SDK patch 0022), so it no longer needs automatic CPU resolve readback.
    if (config.graphics && settings_boot_) {
      settings_boot_->BeforeGraphicsSetup(selected_backend_);
    }
    ApplyContentLicense();
    // The runtime reads input_factory the moment this hook returns, and the
    // input system is built from it, so this is the point where an extra input
    // device can still be added. See src/input/mouse_ui.h.
    //
    // P2 of the gameplay-mapping plan: the SDK installs its own default factory before this hook
    // (ReXApp::SetupPresentation), so the title's system has to replace it here - the two
    // installers below wrap whatever is here. Without this the SDK's MnK driver would ignore the
    // keyboard gate and no physical driver would be visible for a `pad:` gameplay source
    // (measured 2026-10-18: no `title_input:` line and an empty physical pad list).
    config.input_factory = REX_INPUT_BACKEND(rb_blitz::input::CreateTitleInputSystem);
    rb_blitz::input::InstallMouseUiNavigation(config);
    // Controller Settings' `[remap]` table, applied to the pad state the guest asks for. Its own
    // wrapper of input_factory, so both it and the mouse compose with whichever backend was
    // selected. See src/input/remap.h. Since P2 of the gameplay-mapping plan this is also where
    // the source gates and the gameplay tables' physical snapshot live.
    rb_blitz::input::InstallPadRemap(config);
  }

  // The graphics backend, from `gpu_backend` (controller-settings/config/settings.toml's Renderer row).
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
  //
  // Not static (it was, before P11a): a backend that really loaded is remembered in the app, so the
  // settings store can report which renderer this boot is actually using.
  void SelectGpuBackend(rex::RuntimeConfig& config) {
    if (config.graphics || config.gpu_plugin.empty()) {
      return;
    }    const std::string backend = REXCVAR_GET(gpu_backend);
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
    // Remembered only when a backend was really named and loaded: the settings boot reports it as
    // the active renderer, and an unnamed/"any" choice stays unnamed rather than being guessed at.
    if (config.graphics) {
      selected_backend_ = backend;
    }
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
  // Controller Settings that asks for that gets the platform user directory instead. Any
  // other directory is honoured as given - see rb_blitz::fs::IsSameOrInside()
  // for why the check is not a std::filesystem::relative() call.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // D3's rank 4, and the reason R4's standalone half works at all: Controller Settings' own rows,
    // applied at the config rank. This hook runs before the SDK loads the game's own
    // `<exe>.toml` (rex_app.cpp's SetupEnvironment), so the game's file still outranks the
    // profile at equal rank, and the command line and the environment - applied before any of
    // this - outrank both. Without it only a game started by Controller Settings' Launch Game button
    // saw any of Controller Settings' settings, because that is the only thing that passes them as
    // argv (docs/plans/finished/2026-09-27-launcher-plan.md §11 findings 6 and 7).
    //
    // The report is kept and logged in OnPostInitLogging, not here: logging is initialized a few
    // lines after this hook returns, so a line written now would go nowhere.
    profile_apply_ = rb_blitz::profile::ApplyProfileSettings();

    // P11a: the native settings store. It runs *after* the legacy profile's rows (which it migrates
    // and then outranks at equal rank) and *before* the SDK loads the game's own rb_blitz.toml
    // (which is a later write at the same rank and therefore still beats it). Everything it
    // decides - the identity, the path file, the migration, the target and licence, the rows the
    // game reads at init - happens inside ConfigurePaths; what comes back is the two roots the path
    // file selected, for the PathConfig below.
    settings_boot_ = std::make_unique<rb_blitz::settings::boot::SettingsBoot>();
    rb_blitz::settings::boot::SettingsBoot::SetCurrent(settings_boot_.get());
    const rb_blitz::settings::boot::PathRoots boot_roots =
        settings_boot_->ConfigurePaths(rex::filesystem::GetExecutableFolder());

    // The runtime derived these roots from the cvars *before* this hook ran, so a root a profile
    // row just supplied is not in `paths` yet. Re-read the two Controller Settings row can name, exactly
    // as the runtime reads them; an empty cvar leaves the runtime's own default in place.
    if (!REXCVAR_GET(user_data_root).empty()) {
      paths.user_data_root = REXCVAR_GET(user_data_root);
    }
    if (!REXCVAR_GET(cache_root).empty()) {
      paths.cache_root = REXCVAR_GET(cache_root);
    }

    auto redirect_if_inside_game_root = [&](std::filesystem::path& p) {
      if (rb_blitz::fs::IsSameOrInside(p, paths.game_data_root)) {
        p.clear();
      }
    };

    redirect_if_inside_game_root(paths.user_data_root);
    redirect_if_inside_game_root(paths.cache_root);
    redirect_if_inside_game_root(paths.update_data_root);

    // The path file's own roots (installer-written, hand-editable). They replace the legacy
    // profile's path fields, which is why they are applied after the profile and after the cvar
    // re-read above; an explicit environment/CLI value outranks them and was left alone in
    // ConfigurePaths (it reports that in the boot log).
    if (boot_roots.have_user_data_root) {
      paths.user_data_root = std::filesystem::path(std::u8string(boot_roots.user_data_root.begin(),
                                                              boot_roots.user_data_root.end()));
      redirect_if_inside_game_root(paths.user_data_root);
    }

    // Writable roots default outside the game tree (platform user dir). The runtime has already
    // filled user_data_root with GetUserFolder()/GetName() ("rb_blitz") before this hook, so when
    // nothing named a root explicitly it is replaced with the folder Controller Settings' *Save game
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
    // The bootstrap has already validated this handoff and waited for the parent.
    for (const auto& argument : rb_blitz::platform::CommandLineArguments()) {
      if (rb_blitz::platform::IsHandoffSwitch(argument)) settings_restart_boot_ = true;
    }
    rb_blitz::enhancements::LogToggles();
    // R5 reads its own cvars here, before the guest starts: the read hook that
    // applies the menu filter runs on guest threads and must not touch the
    // registry, so the toggle and the row list are captured once (P1's D7).
    rb_blitz::menu_filter::Configure(settings_restart_boot_);
    rb_blitz::local_save::Configure(runtime());
    rb_blitz::ultimate::Configure(runtime(), game_data_root());
    // After ultimate::Configure, which decides what the guest-visible game tree
    // looks like: DLC is resolved by the SDK content manager from a host path, so
    // the two do not interact, but the DLC log line reads better next to the boot
    // identity and the payload line than before them.
    rb_blitz::dlc::Configure(runtime(), game_data_root());
    // Capture pacing before guest threads start. Progress uses actual enumerators
    // and UI completion, independently of whether the host cache was enabled/hit.
    rb_blitz::content_progress::Configure();
    // After dlc::Configure, whose enumeration is the pool R9 draws from: with the
    // toggle on, a few loaded DLC songs are mounted read-only and the main menu's
    // shell-music loader is redirected to them (src/hooks/shell_music.cpp). With the
    // toggle off - or no library - nothing is mounted and the hook is inert.
    rb_blitz::shell_music::Configure(runtime(), game_data_root());
    // P11a: the settings store's second half, and it has to run in this order: after
    // ultimate::Configure (the provider's values are what the store reports for its rows) and
    // *before* bridge::Configure, whose seeding goes through the channel's store - so the channel
    // must already be pointed at the service.
    settings_boot_->AfterLoadXexImage(selected_backend_);
    // Publish overrides before authoring disabled menu rows, once all config sources exist.
    settings_boot_->FinishInit();
    settings_boot_->SetPresentedProbe([this] {
      auto* graphics = runtime()->graphics_system();
      auto* presenter = graphics ? graphics->presenter() : nullptr;
      rex::ui::RawImage frame;
      if (!presenter || !presenter->CaptureGuestOutput(frame) || !frame.width || !frame.height)
        return false;
      // Reject a blank startup surface; alpha alone is not evidence of a guest frame.
      for (size_t i = 0; i + 3 < frame.data.size(); i += 4)
        if (frame.data[i] || frame.data[i + 1] || frame.data[i + 2]) return true;
      return false;
    });
    if (auto* sink = settings_boot_->sink()) {
      sink->SetRestartHandler([this](uint64_t saved, uint64_t current, uint64_t id,
                                     const std::string& target, std::string* error) {
        rb_blitz::platform::LaunchContext context;
        context.executable = rex::filesystem::GetExecutablePath();
        context.game_data_root = game_data_root();
        context.working_directory = std::filesystem::current_path();
        context.profile_file = settings_boot_->paths().legacy_profile;
        context.target = target;
        context.developer_arguments = rb_blitz::platform::CommandLineArguments();
        if (!context.developer_arguments.empty()) context.developer_arguments.erase(context.developer_arguments.begin());
        rb_blitz::dlc::DrainRefreshForRestart();
        bool ready = false;
        const bool dispatched = app_context().CallInUIThreadSynchronous([&] {
          restart_coordinator_.ResetAfterFailure();
          const auto outcome = restart_coordinator_.Request(saved, current, id, context);
          *error = outcome.error;
          ready = restart_coordinator_.CloseRequested();
          if (ready) REXLOG_INFO("settings_restart: replacement child is waiting for parent exit");
          else REXLOG_ERROR("settings_restart: {}", *error);
        });
        if (!dispatched || !ready) return false;
        return app_context().CallInUIThreadDeferred([this] { window()->RequestClose(); });
      });
    }
    // P2 of the RhythmVerse store plan: the store's path probe is installed *before* the payload
    // is built, because the probe is what names the paths that build result is measured against.
    // It reads its cvar once here, on this host thread; the observer itself runs on guest threads.
    rb_blitz::store::InstallPathProbe();
    // P3 of the RhythmVerse store plan: the record serving switch. The store's own netcache
    // requests are answered from the loaded-file container (src/store/store_fetch.h), so this
    // reads the serve directory once, here, and the hook never touches the cvar registry.
    rb_blitz::store::InstallStoreFetch();
    // P4 of the RhythmVerse store plan: the search's worker and its one HTTP path. Started here,
    // on the host thread before the guest exists, because the guest's `write_file` hook only posts
    // work - the request itself never runs on a guest thread (project rule).
    rb_blitz::store::InstallSearchTransport();
    // P3's catalogue: the offers a menu page is answered with. Its worker is started here for the
    // same reason, and the serving policy reads only its snapshot (src/store/store_catalogue.h).
    rb_blitz::store::InstallStoreCatalogue();
    // P3's album art: the cover worker that fetches the API's `album_art`, encodes the
    // `.png_xbox` the retail art loader wants and publishes it for the guest's art request. Started
    // before the catalogue so a page that lands during the same boot already has its prefetch seam
    // installed (src/store/album_art_service.h).
    rb_blitz::store::InstallAlbumArt();
    // P3's Downloaded mark (§3.2): the artist/title index of the loaded DLC library. Its builder
    // thread is started here; the serving policy asks only the published snapshot.
    rb_blitz::store::InstallStoreLibrary(runtime()->user_data_root());
    // P9-10 of the RhythmVerse store plan (§9): warm Recently Added once per boot. Until this the
    // first catalogue fetch started when the store *opened* (`store_fetch.cpp`'s front-menu serve),
    // so a Recent press 1-2 s later asked for a page the site had not answered yet and was handed
    // the **empty** page, which the retail DTA turns back into the front menu - "Recent does
    // nothing" (measured 2026-10-22 in two consecutive boots: `dlc_store/europe/eng/recent/` served
    // as 46 bytes 1.1 s / 1.7 s after the front menu, page 1 landing 1.0 s / 1.9 s *after* the
    // press). Asked here, before the guest exists, page 1 is held before the title can even draw
    // its menu. It is the same worker and the same budget (`store_catalogue_pages`), and nothing is
    // drawn: the caption strip only speaks within its surface window, which starts when the store
    // is served a record. With the master off this is a no-op - the store makes no request at all
    // then (plan §1.6, and the acceptance Control leg pins that silence).
    rb_blitz::store::RequestCatalogueRefresh("the boot asked");
    graphics_loading_payload_ready_ =
        rb_blitz::settings::InstallMenuPayload(runtime(), settings_restart_boot_);
    // The typed settings channel (P1): its two guest hooks are installed at link time, so this
    // only reads their flags once on this host thread and logs what they are set to. A guest
    // thread must not touch the cvar registry, which is why the read happens here.
    rb_blitz::settings::bridge::Configure();
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
  void OnPreLaunchModule() override {
    // P11a: the last read-back before the guest thread exists. A cvar this boot wrote can have been
    // outranked (an explicit source) or overwritten by the game's own rb_blitz.toml, and a row the
    // player cannot change has to say so rather than save a value that never takes effect.
    rex::cvar::FinalizeInit();
    rb_blitz::loading::ConfigureLoadTrace();
    rb_blitz::loading::ConfigureCrashRecovery(runtime(), settings_restart_boot_);
    rb_blitz::graphics::ConfigureLoading(runtime());
    coverage_recorder_ = rb_blitz::graphics::StartDevelopmentCoverage(
        rex::filesystem::GetExecutablePath(),runtime()->cache_root());
    rb_blitz::diag::shader_trace::LogConfiguration();
  }

  bool OnWindowCloseRequested() override {
    rb_blitz::loading::EndCrashRecoveryAttempt();
    rb_blitz::shell_music::CloseQueuedPool();
    rb_blitz::graphics::CloseLoading();
    rex::graphics::compile_trace::Snapshot("window_close");
    if (settings_boot_) settings_boot_->Closing();
    return true;
  }

  // Bulk work belongs to the post-A content boundary, including when an SDK
  // diagnostic requests asynchronous startup. Pre-A draws bootstrap on demand.
  bool DeferShaderStoragePreparation() const override { return true; }
  bool CanLaunchModule() const override { return graphics_loading_payload_ready_; }
  void OnGuestThreadExit(rex::system::XThread*) override {
    rb_blitz::shell_music::CloseQueuedPool();
    rb_blitz::graphics::CloseLoading();
  }

  // Logging is up from here, which is why the profile application made in OnConfigurePaths is
  // reported now and not there: what Controller Settings' file contributed (and which of its rows this
  // build refused) is exactly the evidence a "my Controller Settings setting did not apply" report needs. The
  // settings store's own boot lines follow it, for the same reason.
  void OnPostInitLogging() override {
    rb_blitz::profile::LogProfileApplyReport(profile_apply_);
    if (settings_boot_) {
      settings_boot_->LogConfigured();
    }
  }

  // The help captions' typeface (owner request, 2026-10-14). The SDK registers its own font
  // (ProggyTiny at 10 px) and then calls this; here a system sans joins the atlas for the caption
  // overlay, which is player-facing text rather than a debug readout. This is the only moment an
  // atlas may still be added to, which is why the load lives here and not in the overlay.
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    rb_blitz::ui::help::ConfigureCaptionFont(atlas);
    rb_blitz::ui::ConfigureKeyboardFonts(atlas);
  }

  // The SDK's compact keyboard dialog is a debug-style prompt by default.
  // Restyle that same modal for both library and RhythmVerse song searches,
  // and share its presentation with the existing achievement unlock toast.
  void OnConfigureStyle(ImGuiStyle&, rex::ui::Style& style) override {
    rb_blitz::ui::ConfigureKeyboardTheme(style, [this] {
      return window() ? window()->GetNativeWindowHandle() : nullptr;
    });
  }

  // Diagnostics. The probe overlay is an ordinary SDK dialog: its constructor
  // registers it with the drawer and its destructor unregisters it, so this only
  // has to own it for as long as the app lives. Whether it draws is the
  // `probe_overlay` cvar's decision, re-read every frame, which is what makes the
  // toggle live; the cvar off draws nothing at all.
  //
  // The one thing the dialog cannot reach is the window (it lives here), so it is
  // handed over as a callback rather than a pointer - the same lazily-called shape
  // as the menu cursor's window lookup.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    mouse_cursor_ = rb_blitz::input::CreateMouseMenuCursor(drawer, [this] { return window(); });

    // The help captions (owner request, 2026-10-13): a subtitle at the bottom of the screen that
    // describes the highlighted settings row. It is fed by the guest's own caption events (the
    // generated DTA writes `rbbz-settings/help/<symbol>/-`, routed through the settings bridge)
    // and by the retail-page observer (src/hooks/help_observer.cpp); both resolve against the
    // tables in src/ui/settings_screen.cpp and src/ui/help_topics.h. Creating it registers the
    // bridge's caption sink; whether it draws is the `settings_help_overlay` cvar's decision,
    // re-read every frame, which is what makes the toggle live.
    help_overlay_ = rb_blitz::ui::help::CreateOverlay(drawer);
    music_player_ = rb_blitz::ui::music::CreatePlayer(drawer);

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
  // Controller Settings can still point a built binary at a different game/ tree - a
  // tree with a Rock Band Blitz Ultimate payload merged into it, for instance.
  // Nothing here aborts: the point is that the log says which revision (if any)
  // the running executable was actually booted against, so a bug report can be
  // matched to it.
  void LogBootIdentity() {    REXLOG_INFO("boot identity: {}", REXGLUE_BUILD_STAMP);

    // Which compiler, CMake, Ninja, MSVC toolset, Windows SDK and SDK commit built
    // this binary, with the frozen set named in the same line when it is one. It is
    // the build's half of what the game data identity below is the runtime's half
    // of: a log states the provenance of both sides before any other evidence in it
    // is read (docs/guides/toolchain.md).
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
                " docs/guides/ultimate-compat.md)",
                rb_blitz::fingerprint::vanilla::kEntrypointSize,
                rb_blitz::fingerprint::vanilla::kEntrypointSha256);
  }

  // Owned for the app's lifetime; it unregisters itself from the drawer when this
  // is destroyed, which happens before ~ReXApp() tears the drawer down.
  std::unique_ptr<rex::ui::ImGuiDialog> probe_overlay_;
  std::unique_ptr<rex::ui::ImGuiDialog> mouse_cursor_;
  bool graphics_loading_payload_ready_ = false;
  std::unique_ptr<rb_blitz::graphics::CoverageRecorder> coverage_recorder_;
  // The help captions' overlay (see OnCreateDialogs); destroying it also unregisters the settings
  // bridge's caption sink, because the sink object lives beside the dialog.
  std::unique_ptr<rex::ui::ImGuiDialog> help_overlay_;
  std::unique_ptr<rex::ui::ImGuiDialog> music_player_;

  // What Controller Settings' profile contributed at OnConfigurePaths, reported once logging exists.
  rb_blitz::profile::ProfileApplyReport profile_apply_;

  // P11a: the native settings store and its boot wiring (src/settings/boot.h). Created in
  // OnConfigurePaths - it needs the executable folder, which only exists from there on - and owned
  // for the process lifetime because the guest channel, the menu-rows seam and the restart
  // handshake all reach the same store.
  std::unique_ptr<rb_blitz::settings::boot::SettingsBoot> settings_boot_;

  // The backend this boot really loaded, when it named one (empty when the SDK chose). Reported by
  // the settings store as the active renderer.
  std::string selected_backend_;
  bool settings_restart_boot_ = false;
  std::unique_ptr<rb_blitz::platform::ChildLauncher> restart_launcher_ =
      rb_blitz::platform::CreateChildLauncher();
  rb_blitz::platform::RestartCoordinator restart_coordinator_{restart_launcher_.get()};
};
