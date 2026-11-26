// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "settings/runtime_settings.h"
#include "assets/texture_override.h"
#include "platform/bundle_resources.h"
#if defined(RB_BLITZ_IOS)
#include "platform/ios_runtime.h"
#endif
#include "input/touch_controls.h"
#include "ui/gameplay_presentation.h"
#include <rex/diagnostics/compile_trace.h>
#include "diag/shader_trace.h"
#include "diag/boot_heartbeat.h"
#include "diag/session_logs.h"
#include "graphics/preparation_loading.h"
#include "graphics/shader_coverage.h"
#include <rex/logging/log_category.h>
#include <rex/platform/dynlib.h>
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
#include "hooks/screenshots.h"
#include "hooks/settings_bridge_hooks.h"
#include "hooks/settings_payload.h"
#include "hooks/shell_music.h"
#include "hooks/ultimate.h"
#include "loading/load_trace.h"
#include "loading/crash_recovery.h"
#include "messages/messages_service.h"
#include "messages/messages_guest.h"
#include "messages/messages_update.h"
#include "input/mouse_ui.h"
#include "input/last_device.h"
#include "input/remap.h"
#include "input/title_input.h"
#include "profile/profile_apply.h"
#include "platform/crash_diagnostics.h"
#include "settings/boot.h"
#include "settings/paths.h"
#include "settings/display_runtime.h"
#include "settings/display_hotkey.h"
#include "settings/display_trial.h"
#include "platform/game_restart.h"
#include "platform/bootstrap_arguments.h"
#if defined(RB_BLITZ_IOS_IMPORTER)
#include "platform/ios_importer.h"
#endif
#include "platform/window_host.h"
#include "platform/exit_deadline.h"
#include "store/store_fetch.h"
#include "store/store_catalogue.h"
#include "store/album_art_service.h"
#include "store/store_library.h"
#include "store/store_probe.h"
#include "store/store_search_service.h"
#include "ui/help_overlay.h"
#include "ui/notification_overlay.h"
#include "ui/achievements_overlay.h"
#include "ui/notifications.h"
#include "ui/boot_loading_art.h"
#include "ui/boot_loading_icon.h"
#include "hooks/startup_splasher.h"
#include "ui/menu_music_player.h"
#include "ui/keyboard_theme.h"
#include "util/sha256.h"

#include <atomic>
#include <cctype>
#include <algorithm>
#include <array>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <rex/app/title_app.h>
#include <rex/path_settings.h>
#include <rex/platform/file.h>
#include <rex/kernel/title_paths.h>
#include <rex/gpu/graphics_system.h>
#include <rex/gpu/graphics_presentation.h>
#include <rex/gpu/graphics_preparation_service.h>
#include <rex/gpu/texture_override.h>
#include <rex/hid/input_system.h>
#include <rex/hid/title_input.h>
#include <rex/ui/ui_system.h>
#include <rex/ui/style.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/achievement_notifications.h>
#if defined(RB_BLITZ_IOS)
#include <rex/ui/application_activity.h>
#endif
#include <rex/ui/native_window_host.h>
#include <rex/xam/achievement_manager.h>
#include "hooks/crypto.h"
#include "settings/runtime_values.h"
#include "settings/title_defaults.h"
#include "settings/graphics_settings.h"

class RbBlitzApp final : public rex::app::TitleApp, public rex::ui::IUIEventListener {
 public:
  RbBlitzApp() : TitleApp("rb_blitz", kPPCImageConfig) {}

  int RunEntry(const rex::CLIArgList& args) {
    startup_args_ = rb_blitz::platform::NormalizeBootstrapArguments(args);
#if defined(RB_BLITZ_IOS_IMPORTER)
    // The importer owns UIKit until assets are ready, before any runtime mounts or drivers exist.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return 1;
    rex::ScopeGuard release_import_ui([] { SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS); });
    std::atomic<bool> ready{false};
    auto importer = rb_blitz::platform::StartIOSImporter([&ready] { ready.store(true); });
    if (!importer) return 1;
    for (;;) {
      while (!ready.load()) {
        SDL_PumpEvents();
        SDL_Delay(8);
      }
      ready.store(false);
      if (importer->PrepareLaunch()) break;
    }
    importer.reset();
#endif
    try {
      if (!Init(startup_args_)) {
        OnShutdown();
        return Shutdown();
      }
      if (LaunchTitle()) Run();
      return Shutdown();
    } catch (const StartupCancelled&) {
      if (!on_init_complete_) OnShutdown();
      return Shutdown();
    } catch (...) {
      if (!on_init_complete_) OnShutdown();
      Shutdown();
      throw;
    }
  }

 protected:
  rex::AppSysRequirements GetRequirements() const override {
    auto needs = rex::app::TitleApp::GetRequirements();
    // Raw UI events and the title input filter provide keyboard input even
    // when every external input driver has been disabled or fails.
    needs.input = false;
    return needs;
  }

  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    using namespace rb_blitz::settings;
    REX_TRY_VOID(RegisterTitleSettings(registry));
    REX_TRY_VOID(RegisterRuntimeSettingAliases(registry));
    // Resolve canonical environment values before selecting stored boot identity and paths.
    for (auto* setting : registry.Enumerate()) {
      std::string environment = "REX_";
      for (unsigned char c : setting->GetName())
        environment += c == '.' ? '_' : static_cast<char>(std::toupper(c));
      if (auto value = rex::platform::GetEnvironmentValue(environment))
        REX_TRY_VOID(registry.SetText(rex::SettingLayer::kEnvironment, setting->GetName(), *value));
    }
    // Boot identity and stored path choices depend on explicit options before the SDK parses them.
    for (const auto& option : startup_args_.GetOptions()) {
      if (const auto canonical = ResolveRuntimeSettingName(option.name))
        REX_TRY_VOID(registry.SetText(rex::SettingLayer::kCommandLine, *canonical,
                                     option.value.value_or("true")));
    }
    REX_TRY_VOID(SetTitleDefault(root, rex::platform::GetExecutableFolder().string()));
    auto& title_paths = GetTitleRuntime().GetTitlePaths();
    REX_TRY_VOID(SetTitleDefault(title_paths.game_root, (rex::platform::GetExecutableFolder() / "game").string()));
#if defined(_WIN32)
    REX_TRY_VOID(SetTitleDefault(REX_PATHS().user_root, (rex::platform::GetUserFolder() / "Rock Band Blitz").string()));
    REX_TRY_VOID(SetTitleDefault(REX_PATHS().cache_root, "{app.user_root}/cache"));
#else
    const auto native = NativeUserRootsFromEnvironment();
    if (!native.data.empty()) {
      REX_TRY_VOID(SetTitleDefault(REX_PATHS().user_root, native.data.string()));
      REX_TRY_VOID(SetTitleDefault(title_paths.update_root, (native.data / "updates").string()));
    }
    if (!native.cache.empty())
      REX_TRY_VOID(SetTitleDefault(REX_PATHS().cache_root, native.cache.string()));
#if defined(RB_BLITZ_IOS)
    ios_documents_root_ = rb_blitz::platform::PrepareIOSDocuments();
    REX_TRY_VOID(SetTitleDefault(REX_PATHS().config_file, (native.config / "rb_blitz.toml").string()));
#if defined(RB_BLITZ_IOS_IMPORTER)
    REX_TRY_VOID(SetTitleDefault(title_paths.game_root, rb_blitz::platform::IOSImportedGameRoot().string()));
#endif
    REX_TRY_VOID(SetTitleDefault(title::dlc_root, (ios_documents_root_ / "DLCs").string()));
#endif
#endif
    schema::SetMobilePreviewEnabled(title::mobile_preview.Get());
    profile_apply_ = rb_blitz::profile::ApplyProfileSettings();
    settings_boot_ = std::make_unique<boot::SettingsBoot>();
    boot::SettingsBoot::SetCurrent(settings_boot_.get());
    settings_boot_->ConfigurePaths(rex::platform::GetExecutableFolder());
    REX_TRY_VOID(SetTitleDefault(GetUI().GetSettings().title, "Rock Band Blitz [rexglue-v" REXGLUE_VERSION_STRING "]"));
    if (rb_blitz::platform::window_host::Active()) {
      native_host_ = std::make_shared<NativeWindowHost>();
      native_host_registration_ = rex::ui::RegisterNativeWindowHost(native_host_);
    }
    return RegisterLegacyLogCategoryConfig(registry, REX_PATHS().config_file.Get());
  }

  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry& registry) override {
    using namespace rb_blitz::settings;
    rb_blitz::input::last_device::SetMouseAsTouch(title::mouse_as_touch.Get());
    REX_TRY_VOID(ApplyLegacyLoggingInputs());
    REX_TRY_VOID(ApplyLegacyGraphicsInputs());
    auto& graphics = GetGraphics().GetSettings();
    const auto legacy_backend = InspectRuntimeSetting("gpu_backend");
    if (graphics.driver.GetLayer() < legacy_backend.staged_source ||
        graphics.driver.GetLayer() == rex::SettingLayer::kDefault)
      REX_TRY_VOID(graphics.driver.SetText(legacy_backend.staged_source, legacy_backend.active_value));
    if (title::settings_safe_graphics.Get()) {
#if defined(_WIN32)
      REX_TRY_VOID(graphics.driver.SetText(rex::SettingLayer::kRuntime, "d3d12"));
#else
      REX_TRY_VOID(graphics.driver.SetText(rex::SettingLayer::kRuntime, "vulkan"));
#endif
    }
    if (graphics.driver.Get() == "vulkan" && !VulkanLoaderIsUsable()) {
      REXLOG_WARN("graphics: Vulkan loader is unavailable; selecting the platform default");
      REX_TRY_VOID(graphics.driver.SetText(graphics.driver.GetLayer(), "any"));
    }
    selected_backend_ = graphics.driver.Get();
    REX_TRY_VOID(NormalizeInputDrivers());
    REX_TRY_VOID(EnforceWritablePaths());
    if (settings_boot_ && !settings_boot_->BeforeGraphicsSetup(selected_backend_))
      return rex::Err(rex::ErrorCategory::kConfig,
          "The render profile could not be protected or admitted safely. Relaunch to recover at 720p.");
    ApplyContentLicense();
#if defined(RB_BLITZ_IOS)
    REX_TRY_VOID(SetTitleDefault(registry, "log.flush_level", "trace"));
#endif
    if (auto file_setting = registry.Find("log.file_name");
        file_setting && CanSetTitleDefault(**file_setting) &&
        ReadSdkSetting<std::string>("log.file_name").empty() &&
        !rb_blitz::graphics::IsDevelopmentBuild(rex::platform::GetExecutablePath())) {
      diagnostics_root_ = UserRoot();
#if defined(RB_BLITZ_IOS)
      diagnostics_root_ = ios_documents_root_;
#endif
      const auto path = rb_blitz::diag::PrepareSessionLog(diagnostics_root_);
      if (!path.empty()) {
        diagnostics_log_file_ = path.string();
        REX_TRY_VOID(SetTitleDefault(registry, "log.file_name", diagnostics_log_file_));
        REX_TRY_VOID(SetTitleDefault(registry, "log.max_file_size_bytes", "2097152"));
        REX_TRY_VOID(SetTitleDefault(registry, "log.max_files", "1"));
      }
    }
    std::string art_error;
    boot_icon_art_loaded_ = rb_blitz::ui::boot_loading_art::LoadBootLoadingFrames(
        GameRoot(), &boot_loading_frames_, &art_error);
    if (!boot_icon_art_loaded_) REXLOG_WARN("boot_loading: autosave icon unavailable: {}", art_error);
    rb_blitz::dlc::PrepareBootLibraryScan(GameRoot(), CacheRoot() / "dlc-library-identities.cache");
    rb_blitz::shell_music::PrepareBootCatalog(CacheRoot() / "menu_music_catalog.cache");
    return rex::Ok();
  }

  rex::Result<void> OnInit() override {
    bool initialized = false;
    rex::ScopeGuard rollback([&] { if (!initialized) OnShutdown(); });
    selected_backend_ = GetGraphics().GetDriverName();
    if (settings_boot_) settings_boot_->BeginStartupShaderCompilation();
    rb_blitz::profile::LogProfileApplyReport(profile_apply_);
    if (settings_boot_) settings_boot_->LogConfigured();
    auto* preparation = REX_INTERFACE(rex::gpu::IGraphicsPreparation);
    if (!preparation || !preparation->SetShaderStorageDeferred(true))
      return rex::Err(rex::ErrorCategory::kConfig, "graphics preparation cannot be deferred safely");
    auto* title_input = REX_INTERFACE(rex::hid::ITitleInput);
    if (!title_input) return rex::Err(rex::ErrorCategory::kModule, "title input service is unavailable");
    rb_blitz::input::ConfigureTitleInput(title_input);
    input_filter_ = rb_blitz::input::InstallPadRemap(*title_input);
    ui_listener_ = GetUI().AddEventListener(*this);
#if defined(RB_BLITZ_IOS)
    if (!SDL_AddEventWatch(&WatchIOSActivation, this))
      return rex::Err(rex::ErrorCategory::kModule, "iOS activation event watch is unavailable");
    ios_event_watch_ = true;
#endif
    auto& atlas = GetUI().GetFonts();
    rb_blitz::ui::help::ConfigureCaptionFont(&atlas);
    rb_blitz::ui::ConfigureKeyboardFonts(&atlas);
    rb_blitz::ui::ConfigureKeyboardTheme(GetUI().GetStyle(), [this] {
      auto native = GetUI().GetNativeWindow();
      return native ? native->window : nullptr;
    });
    GetUI().GetStyle().achievements.toggle_override = [] { rb_blitz::ui::achievements::Toggle(); };
    auto* notifications = REX_INTERFACE(rex::ui::IAchievementNotifications);
    if (!notifications)
      return rex::Err(rex::ErrorCategory::kModule, "achievement notification policy is unavailable");
    notifications->SetEnabled(!rb_blitz::settings::title::enhancements_hide_achievements.Get());
    notification_policy_ = rb_blitz::settings::title::enhancements_hide_achievements.OnChange(
        [](const bool& hidden) {
          if (auto* service = REX_INTERFACE(rex::ui::IAchievementNotifications))
            service->SetEnabled(!hidden);
        });
    // The persistent host owns resizing. Changing SDL child flags after attach
    // restores its decorations and creates a second visible title bar.
    if (!rb_blitz::platform::window_host::Active()) {
      if (auto native = GetUI().GetNativeWindow(); native && native->sdl)
        SDL_SetWindowResizable(static_cast<SDL_Window*>(native->sdl),
                              rb_blitz::settings::title::window_resizable.Get());
    }
    gameplay_presentation_ = rb_blitz::ui::CreateGameplayPresentation(
        GetUI(),
        [] { return REX_INTERFACE(rex::gpu::IGraphicsPresentation); }, [this] {
          if (!rb_blitz::input::touch::PreviewEnabled() || !GetTitleRuntime().IsLoaded()) return;
          const auto entries = GetTitleRuntime().GetAchievementManager().ListAchievements();
          if (!entries.empty())
            if (auto* service = REX_INTERFACE(rex::ui::IAchievementNotifications))
              service->Preview(entries.front());
        });
    AttachDialog(gameplay_presentation_, startup_dialogs_);
    touch_controls_ = rb_blitz::input::touch::CreateControls(GetUI());
    AttachDialog(touch_controls_, startup_dialogs_);
    boot_loading_icon_ = std::make_unique<rb_blitz::ui::BootLoadingIcon>(
        [] { auto* p = REX_INTERFACE(rex::gpu::IGraphicsPresentation);
             return p ? p->GetPresentationState().guest_output_refresh : 0; },
        [] { return rb_blitz::diag::shader_trace::NativeStartupPanelReady(); },
        [] { return rb_blitz::diag::shader_trace::NativeSplashReady(); },
        [] { return rb_blitz::hooks::startup_splasher::NativeLogoLoadStarted(); });
    if (boot_icon_art_loaded_) boot_loading_icon_->SetFrames(std::move(boot_loading_frames_));
    startup_dialogs_.push_back(GetUI().AddDialog(*boot_loading_icon_));
    PumpStartupWindow();
    initialized = true;
    on_init_complete_ = true;
    return rex::Ok();
  }

  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& title) override {
    rb_blitz::crypto::ResetGuestCryptoState();
    rb_blitz::input::InvalidateGameplayInput();
    rb_blitz::assets::ConfigureTextureOverrides(REX_INTERFACE(rex::gpu::ITextureOverrides));

    PumpStartupWindow();
    const auto boot_started = std::chrono::steady_clock::now();
    auto stage_started = boot_started;
    auto log_stage = [&](const char* stage) {
      const auto now = std::chrono::steady_clock::now();
      REXLOG_INFO("boot_timing: post_xex stage={} stage_ms={} total_ms={}", stage,
                  std::chrono::duration_cast<std::chrono::milliseconds>(now - stage_started).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(now - boot_started).count());
      stage_started = now;
      PumpStartupWindow();
    };
    REXLOG_INFO("boot_timing: post_xex begin");
    LogBootIdentity();
    for (const auto& argument : rb_blitz::platform::CommandLineArguments()) {
      if (rb_blitz::platform::IsHandoffSwitch(argument)) settings_restart_boot_ = true;
    }
    rb_blitz::enhancements::LogToggles();
    rb_blitz::menu_filter::Configure(settings_restart_boot_);
    rb_blitz::local_save::Configure(&title);
#if defined(RB_BLITZ_IOS)
    rb_blitz::screenshots::Configure(&title, ios_documents_root_);
#else
    rb_blitz::screenshots::Configure(&title);
#endif
    rb_blitz::ultimate::Configure(&title, GameRoot());
    log_stage("identity_and_ultimate");
    if (!rb_blitz::dlc::Configure(&title, GameRoot()))
      return rex::Err(rex::ErrorCategory::kValidation, "content validation service is unavailable");
    log_stage("dlc_configure");
    rb_blitz::content_progress::Configure();
    rb_blitz::shell_music::Configure(&title, GameRoot());
    log_stage("shell_music_configure");
    settings_boot_->AfterLoadXexImage(selected_backend_);
    log_stage("settings_after_xex");
    if (rb_blitz::settings::title::enhancements_icon_schemes.Get()) {
      REXLOG_INFO("boot_timing: controller_schemes begin");
      rb_blitz::assets::InstallControllerSchemes(
          rb_blitz::platform::BundleResources(rex::platform::GetExecutableFolder()) / "assets" / "controller" / "schemes",
          UserRoot() / "controller_schemes",
          [this] { PumpStartupWindow(); });
      std::string selected = "retail";
      if (settings_boot_ && settings_boot_->service()) {
        auto* service = settings_boot_->service();
        service->RefreshControllerSchemes();
        const std::string requested =
            service->RequestedValue(rb_blitz::settings::SettingId::kButtonScheme).text;
        if (rb_blitz::assets::SelectControllerScheme(requested)) {
          selected = requested;
        } else {
          REXLOG_WARN("button_schemes: saved scheme {} is missing, using retail", requested);
          rb_blitz::settings::Value fallback;
          fallback.kind = rb_blitz::settings::ValueKind::kEnum;
          fallback.text = "retail";
          service->store().MarkChanged(rb_blitz::settings::SettingId::kButtonScheme,
                                       fallback, rb_blitz::settings::Origin::kAutomatic);
          service->store().Save();
        }
      }
      rb_blitz::assets::SelectControllerScheme(selected);
      if (const char* probe = std::getenv("RB_BLITZ_SCHEME")) {
        if (!rb_blitz::assets::SelectControllerScheme(probe))
          REXLOG_WARN("button_schemes: probe scheme '{}' is unavailable", probe);
      }
    }
    log_stage("controller_schemes_and_selection");
    settings_boot_->FinishInit();
    log_stage("settings_finish_init");
    settings_boot_->SetPresentedProbe([this] { return HasNonblankGuestFrame(); });
    settings_boot_->SetResolutionPresentProbe([] {
      auto* presentation = REX_INTERFACE(rex::gpu::IGraphicsPresentation);
      return presentation ? presentation->GetPresentationState().guest_output_refresh : 0;
    });
    ConfigureDisplayAndRestart();
    rb_blitz::store::InstallPathProbe();
    rb_blitz::store::InstallStoreFetch();
    rb_blitz::store::InstallSearchTransport();
    rb_blitz::store::InstallStoreCatalogue();
    rb_blitz::store::InstallAlbumArt();
    rb_blitz::messages::Install(UserRoot(),
        rb_blitz::platform::BundleResources(rex::platform::GetExecutableFolder()));
    rb_blitz::messages::InstallUpdateAction([this] {
      GetUI().PostTask([this] { RequestShutdown(); });
      return true;
    });
    rb_blitz::messages::InstallMessagesGuest();
    rb_blitz::store::InstallStoreLibrary(UserRoot());
    rb_blitz::store::RequestCatalogueRefresh("the boot asked");
    graphics_loading_payload_ready_ =
        rb_blitz::settings::InstallMenuPayload(
            &title, settings_restart_boot_, [this] { PumpStartupWindow(); });
    if (!graphics_loading_payload_ready_)
      return rex::Err(rex::ErrorCategory::kValidation, "the required startup menu payload could not be installed");
    rb_blitz::settings::bridge::Configure();
    log_stage("menu_services_and_payload");
    CreateRegularDialogs();

    const auto started = std::chrono::steady_clock::now();
    REXLOG_INFO("boot_timing: pre_launch begin");
    rb_blitz::assets::InstallP1TextureOverride();

    if (rb_blitz::platform::InstallCrashDiagnostics(CacheRoot()))
      REXLOG_INFO("crash_diagnostics: pid={} fatal native exceptions write records to {}",
                  rb_blitz::platform::ThisProcessId(),
                  (CacheRoot() / "crash-dumps").string());
    else
      REXLOG_WARN("crash_diagnostics: could not install native crash capture");
    rb_blitz::loading::ConfigureLoadTrace();
    rb_blitz::loading::ConfigureCrashRecovery(CacheRoot(), settings_restart_boot_);
    rb_blitz::graphics::ConfigureLoading(&title);
    coverage_recorder_ = rb_blitz::graphics::StartCoverage(
        rex::platform::GetExecutablePath(),CacheRoot(),UserRoot());
    rb_blitz::diag::shader_trace::LogConfiguration();
    if (!diagnostics_log_file_.empty() &&
        !rb_blitz::diag::WriteSessionInfo(diagnostics_root_, rex::GetBuildStamp(),
            RBBLITZ_TOOLCHAIN_STAMP, rex::platform::GetExecutablePath()))
      REXLOG_WARN("diagnostics: could not write session build identity");
    REXLOG_INFO("boot_timing: guest_launch_ready elapsed_ms={}",
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count());
    rb_blitz::diag::boot_heartbeat::Start();

    return rex::Ok();
  }

  void OnTitleStopping(rex::kernel::TitleRuntime&) override {
    rb_blitz::content_progress::Close();
    rb_blitz::store::ClosePathProbe();
    CloseDisplayTrial();
    RevertDisplayPreview();
    rb_blitz::diag::boot_heartbeat::Finish("title stopping");
    rb_blitz::loading::EndCrashRecoveryAttempt();
    rb_blitz::shell_music::CloseQueuedPool();
    rb_blitz::shell_music::DrainQueuedPool();
    rb_blitz::dlc::DrainRefreshForRestart();
    rb_blitz::graphics::CloseLoading();
    coverage_recorder_.reset();
    rb_blitz::input::InvalidateGameplayInput();
    ClearRegularDialogs();
    display_preview_.reset();
    if (settings_boot_) settings_boot_->Closing();
    rex::diagnostics::compile_trace::Snapshot("title_stopping");
  }

  void OnTitleExit(rex::kernel::TitleExitReason) override {
    if (!rb_blitz::platform::ArmExitDeadline()) std::_Exit(0);
    REXLOG_INFO("shutdown: guest exited; host cleanup has a 2000 ms process-exit deadline");
    RequestShutdown();
  }

  void OnShutdown() override {
    rb_blitz::loading::CloseCrashRecovery();
    CloseDisplayTrial();
#if defined(RB_BLITZ_IOS)
    if (ios_event_watch_) {
      SDL_RemoveEventWatch(&WatchIOSActivation, this);
      ios_event_watch_ = false;
    }
#endif
    rb_blitz::content_progress::Close();
    rb_blitz::store::ClosePathProbe();
    rb_blitz::shell_music::DrainQueuedPool();
    rb_blitz::dlc::DrainRefreshForRestart();
    ClearRegularDialogs();
    startup_dialogs_.clear();
    boot_loading_icon_.reset();
    gameplay_presentation_.reset();
    touch_controls_.reset();
    ui_listener_.Reset();
    notification_policy_.Reset();
    if (on_init_complete_) GetUI().GetStyle().achievements.toggle_override = {};
    input_filter_.Reset();
    rb_blitz::input::ConfigureTitleInput(nullptr);
    rb_blitz::assets::ResetTextureOverrides();
    rb_blitz::ui::notifications::SharedQueue().Clear();
    coverage_recorder_.reset();
    display_preview_.reset();
    settings_boot_.reset();
    if (on_init_complete_) REXLOG_INFO("Title shutdown completed");
  }

 private:
  void OnUIEvent(const rex::ui::UIEvent& event) override {
    if (event.GetType() == rex::ui::UIEvent::Type::kFocus ||
        event.GetType() == rex::ui::UIEvent::Type::kClosing) display_hotkey_.Reset();
    if (event.GetType() == rex::ui::UIEvent::Type::kClosing) {
      CloseDisplayTrial();
      RevertDisplayPreview();
      rb_blitz::diag::boot_heartbeat::Finish("window close");
    }
#if defined(RB_BLITZ_IOS)
    if (event.GetType() == rex::ui::UIEvent::Type::kFocus) {
      auto native = GetUI().GetNativeWindow();
      if (native && native->sdl)
        SetIOSActive((SDL_GetWindowFlags(static_cast<SDL_Window*>(native->sdl)) &
                      SDL_WINDOW_INPUT_FOCUS) != 0);
    }
#endif
  }
  void OnInputEvent(rex::InputEvent& event) override {
    const auto action = display_hotkey_.Handle(event);
    if (action == rb_blitz::settings::DisplayHotkey::Action::kNone) return;
    if (action == rb_blitz::settings::DisplayHotkey::Action::kTrial) {
      if (!display_trial_) display_trial_ =
          std::make_unique<rb_blitz::settings::DisplayTrial>(GetUI());
      std::string error;
      if (!display_trial_->Cycle(&error)) REXLOG_WARN("settings_display: {}", error);
      return;
    }
    const auto previous = rb_blitz::settings::RuntimeDisplayMode(GetUI());
    if (previous.empty()) {
      REXLOG_WARN("settings_display: current display mode is unavailable");
      return;
    }
    std::string error;
    if (!rb_blitz::settings::ApplyRuntimeDisplay(
            GetUI(), previous == "windowed" ? "borderless" : "windowed", &error))
      REXLOG_WARN("settings_display: Alt+Enter failed: {}", error);
  }
#if defined(RB_BLITZ_IOS)
  void SetIOSActive(bool active) {
    auto* activity = REX_INTERFACE(rex::ui::IApplicationActivity);
    const auto changed = activity
        ? activity->SetActive(active)
        : rex::Err(rex::ErrorCategory::kModule, "application activity service is unavailable");
    if (!changed) {
      REXLOG_ERROR("ios_lifecycle: activation transition failed: {}", changed.error().message);
      RequestShutdown();
      return;
    }
    rb_blitz::platform::SetIOSApplicationActive(active);
    if (!active) rb_blitz::input::InvalidateGameplayInput();
    else rb_blitz::platform::LogIOSPresentationState();
  }
  static bool SDLCALL WatchIOSActivation(void* context, SDL_Event* event) {
    auto& app = *static_cast<RbBlitzApp*>(context);
    switch (event->type) {
      case SDL_EVENT_WILL_ENTER_BACKGROUND:
      case SDL_EVENT_DID_ENTER_BACKGROUND:
        app.SetIOSActive(false);
        break;
      case SDL_EVENT_DID_ENTER_FOREGROUND:
        app.SetIOSActive(true);
        break;
      default: break;
    }
    return true;
  }
#endif
  std::filesystem::path GameRoot() { return GetTitleRuntime().GetTitlePaths().game_root.Get(); }
  std::filesystem::path UserRoot() { return REX_PATHS().user_root.Get(); }
  std::filesystem::path CacheRoot() { return REX_PATHS().cache_root.Get(); }
  struct StartupCancelled {};
  void PumpStartupWindow() { if (!RunFrame()) throw StartupCancelled{}; }
  void AttachDialog(std::unique_ptr<rex::ui::IDialog>& dialog,
                    std::vector<rex::ScopeGuard>& guards) {
    if (dialog) guards.push_back(GetUI().AddDialog(*dialog));
  }
  bool HasNonblankGuestFrame() {
    auto* presentation = REX_INTERFACE(rex::gpu::IGraphicsPresentation);
    rex::gpu::CaptureImage frame;
    if (!presentation || !presentation->CaptureGuestOutput(frame) || !frame.width || !frame.height)
      return false;
    if (frame.stride < size_t(frame.width) * 4 ||
        frame.height > frame.data.size() / frame.stride) return false;
    for (size_t y = 0; y < frame.height; ++y)
      for (size_t x = 0; x < frame.width; ++x) {
        const auto i = y * frame.stride + x * 4;
        if (frame.data[i] || frame.data[i + 1] || frame.data[i + 2]) return true;
      }
    return false;
  }
  void ApplyContentLicense() {
    const auto license = rb_blitz::settings::InspectRuntimeSetting("license_mask");
    if (license.staged_source == rex::SettingLayer::kDefault)
      rb_blitz::settings::WriteRuntimeSetting("license_mask", "1", rex::SettingLayer::kTitle);
    REXLOG_INFO("content licence: license_mask = {}",
        rb_blitz::settings::InspectRuntimeSetting("license_mask").active_value);
  }
  rex::Result<void> NormalizeInputDrivers() {
    auto& drivers = GetInput().GetSettings().drivers;
    std::string normalized;
    std::string value = drivers.Get();
    for (size_t first = 0; first <= value.size();) {
      size_t end = value.find(',', first);
      if (end == std::string::npos) end = value.size();
      auto token = value.substr(first, end - first);
      const auto begin = token.find_first_not_of(" \t");
      if (begin != std::string::npos) {
        token = token.substr(begin, token.find_last_not_of(" \t") - begin + 1);
        if (token == "sdl") token = "sdl3";
        if (!normalized.empty()) normalized += ',';
        normalized += token;
      }
      if (end == value.size()) break;
      first = end + 1;
    }
    if (normalized != value) REX_TRY_VOID(drivers.SetText(drivers.GetLayer(), normalized));
    return rex::Ok();
  }
  rex::Result<void> EnforceWritablePaths() {
    const auto game = GameRoot();
    const auto native = rb_blitz::settings::NativeUserRootsFromEnvironment();
    const auto safe_user = native.data.empty()
        ? rex::platform::GetUserFolder() / "Rock Band Blitz" : native.data;
    auto redirect = [&](auto& setting, const std::filesystem::path& fallback) -> rex::Result<void> {
      if (!setting.Get().empty() && rb_blitz::fs::IsSameOrInside(setting.Get(), game)) {
        REXLOG_WARN("paths: {} is inside the game root; using {}", setting.GetName(), fallback.string());
        if (rb_blitz::fs::IsSameOrInside(fallback, game))
          return rex::Err(rex::ErrorCategory::kConfig, "the writable root cannot be placed outside the game data");
        REX_TRY_VOID(setting.SetText(rex::SettingLayer::kRuntime, fallback.string()));
      }
      return rex::Ok();
    };
    REX_TRY_VOID(redirect(REX_PATHS().user_root, safe_user));
    REX_TRY_VOID(redirect(REX_PATHS().cache_root, native.cache.empty() ? safe_user / "cache" : native.cache));
    REX_TRY_VOID(redirect(GetTitleRuntime().GetTitlePaths().update_root, safe_user / "updates"));
    return rex::Ok();
  }
  static bool VulkanLoaderIsUsable() {
    rex::platform::DynamicLibrary loader;
#if defined(RB_BLITZ_IOS)
    const auto frameworks = rex::platform::GetExecutableFolder() / "Frameworks";
    if (!loader.Load(frameworks / "MoltenVK.framework" / "MoltenVK") &&
        !loader.Load(frameworks / "libMoltenVK.dylib")) return false;
#elif defined(_WIN32)
    if (!loader.Load("vulkan-1.dll")) return false;
#elif defined(__APPLE__)
    if (!loader.Load("libvulkan.1.dylib")) return false;
#else
    if (!loader.Load("libvulkan.so.1")) return false;
#endif
    return loader.GetRawSymbol("vkGetInstanceProcAddr") != nullptr;
  }
  void CloseDisplayTrial() {
    if (!display_trial_) return;
    std::string error;
    if (!display_trial_->Close(&error)) REXLOG_WARN("settings_display: {}", error);
    display_trial_.reset();
    display_hotkey_.Reset();
  }
  void RevertDisplayPreview() {
    if (display_preview_ && display_preview_->active()) {
      std::string error;
      if (!display_preview_->Revert(&error)) REXLOG_WARN("settings_display: {}", error);
    }
  }
  void ConfigureDisplayAndRestart();
  void CreateRegularDialogs();
  void ClearRegularDialogs() {
    regular_dialogs_.clear();
    probe_overlay_.reset(); mouse_cursor_.reset(); help_overlay_.reset();
    notification_overlay_.reset(); achievement_browser_.reset(); music_player_.reset();
  }
  void LogBootIdentity() {
    REXLOG_INFO("boot identity: {}", rex::GetBuildStamp());

    REXLOG_INFO("toolchain: {}", RBBLITZ_TOOLCHAIN_STAMP);

    const std::filesystem::path entrypoint =
        GameRoot() / rb_blitz::fingerprint::vanilla::kEntrypointPath;
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
  class NativeWindowHost final : public rex::ui::INativeWindowHost {
    bool Attach(void* window, std::function<void(bool)> focus) override {
      mode_.reset();
      return rb_blitz::platform::window_host::Attach(window,
          rb_blitz::settings::title::window_resizable.Get(), std::move(focus));
    }
    bool SetDisplayMode(rex::ui::NativeHostDisplayMode mode) override {
      if (!rb_blitz::platform::window_host::SetDisplayMode(static_cast<int>(mode))) return false;
      mode_ = mode;
      return true;
    }
    void SetTitle(std::string_view value) override { rb_blitz::platform::window_host::SetTitle(value); }
    std::optional<rex::ui::NativeHostDisplayMode> GetDisplayMode() const override { return mode_; }
    std::optional<rex::ui::NativeHostDisplayMode> mode_;
  };
  rex::CLIArgList startup_args_;
  std::unique_ptr<rb_blitz::settings::boot::SettingsBoot> settings_boot_;
  rb_blitz::profile::ProfileApplyReport profile_apply_;
  std::string selected_backend_;
  bool settings_restart_boot_ = false;
  bool on_init_complete_ = false;
  bool graphics_loading_payload_ready_ = false;
  bool boot_icon_art_loaded_ = false;
  rb_blitz::ui::boot_loading_art::BootLoadingFrames boot_loading_frames_;
  std::unique_ptr<rb_blitz::ui::BootLoadingIcon> boot_loading_icon_;
  std::unique_ptr<rex::ui::IDialog> probe_overlay_, mouse_cursor_, touch_controls_;
  std::unique_ptr<rex::ui::IDialog> gameplay_presentation_, help_overlay_, notification_overlay_;
  std::unique_ptr<rex::ui::IDialog> achievement_browser_, music_player_;
  std::vector<rex::ScopeGuard> startup_dialogs_, regular_dialogs_;
  rex::ScopeGuard input_filter_, ui_listener_, notification_policy_, native_host_registration_;
  std::shared_ptr<NativeWindowHost> native_host_;
  std::unique_ptr<rb_blitz::graphics::CoverageRecorder> coverage_recorder_;
  std::string diagnostics_log_file_;
  std::filesystem::path diagnostics_root_;
#if defined(RB_BLITZ_IOS)
  std::filesystem::path ios_documents_root_;
  bool ios_event_watch_ = false;
#endif
  rb_blitz::settings::DisplayHotkey display_hotkey_;
  std::unique_ptr<rb_blitz::settings::DisplayTrial> display_trial_;
  std::unique_ptr<rb_blitz::settings::DisplayPreviewTransaction> display_preview_;
  std::string display_preview_requested_mode_;
  uint64_t display_preview_present_sequence_ = 0;
  std::unique_ptr<rb_blitz::platform::ChildLauncher> restart_launcher_ =
      rb_blitz::platform::CreateChildLauncher();
  rb_blitz::platform::RestartCoordinator restart_coordinator_{restart_launcher_.get()};
};

inline void RbBlitzApp::CreateRegularDialogs() {
  ClearRegularDialogs();
  mouse_cursor_ = rb_blitz::input::CreateMouseMenuCursor(GetUI());
  help_overlay_ = rb_blitz::ui::help::CreateOverlay();
  notification_overlay_ = rb_blitz::ui::notifications::CreateOverlay(GetUI());
  achievement_browser_ = rb_blitz::ui::achievements::CreateOverlay(GetUI(), [this] {
    auto& title = GetTitleRuntime();
    return title.IsLoaded() ? &title.GetAchievementManager() : nullptr;
  });
  music_player_ = rb_blitz::ui::music::CreatePlayer(GetUI());
  probe_overlay_ = rb_blitz::diag::CreateOverlayDialog([this] {
    rb_blitz::diag::WindowState state;
    auto native = GetUI().GetNativeWindow();
    if (!native || !native->sdl) return state;
    auto* window = static_cast<SDL_Window*>(native->sdl);
    int pw = 0, ph = 0, lw = 0, lh = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    SDL_GetWindowSize(window, &lw, &lh);
    state.physical_width = pw; state.physical_height = ph;
    state.logical_width = lw; state.logical_height = lh;
    state.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (lw) state.dpi_scale = float(pw) / float(lw);
    return state;
  });
  for (auto* dialog : {&mouse_cursor_, &help_overlay_, &notification_overlay_,
                       &achievement_browser_, &music_player_, &probe_overlay_})
    AttachDialog(*dialog, regular_dialogs_);
}

inline void RbBlitzApp::ConfigureDisplayAndRestart() {
  auto* sink = settings_boot_ ? settings_boot_->sink() : nullptr;
  if (!sink) return;
  if (GetUI().HasWindow() && !settings_boot_->paths().settings_file.empty()) {
    display_preview_ = std::make_unique<rb_blitz::settings::DisplayPreviewTransaction>(
        GetUI(), settings_boot_->paths().settings_file);
    rb_blitz::settings::ServiceSink::DisplayPreviewHooks hooks;
    hooks.begin = [this](std::string_view requested, std::string* error) {
      auto* presentation = REX_INTERFACE(rex::gpu::IGraphicsPresentation);
      if (!display_preview_ || !presentation) {
        if (error) *error = "display presentation is unavailable";
        return false;
      }
      const auto previous = rb_blitz::settings::RuntimeDisplayMode(GetUI());
      if (previous.empty()) {
        if (error) *error = "the native display mode is unavailable";
        return false;
      }
      if (!display_preview_->Begin(previous, requested, error)) return false;
      display_preview_requested_mode_ = requested;
      display_preview_present_sequence_ = presentation->GetPresentationState().successful_ui_present;
      return true;
    };
    hooks.keep = [this](const rb_blitz::settings::DisplayPreviewTransaction::Persist& persist,
                        std::string* error) {
      auto* presentation = REX_INTERFACE(rex::gpu::IGraphicsPresentation);
      if (!display_preview_ || !presentation || !display_preview_->active() ||
          presentation->GetPresentationState().successful_ui_present <= display_preview_present_sequence_) {
        if (error) *error = "the new window mode has not presented yet";
        return false;
      }
      if (rb_blitz::settings::RuntimeDisplayMode(GetUI()) != display_preview_requested_mode_) {
        if (error) *error = "the native window is no longer in the previewed mode";
        return false;
      }
      return display_preview_->Keep(true, persist, error);
    };
    hooks.revert = [this](std::string* error) {
      const bool reverted = display_preview_ && display_preview_->Revert(error);
      if (reverted) display_preview_requested_mode_.clear();
      return reverted;
    };
    hooks.active = [this] { return display_preview_ && display_preview_->active(); };
    hooks.applied = [this](const rb_blitz::settings::Value& value, uint64_t revision) {
      using namespace rb_blitz::settings;
      if (!settings_boot_) return;
      const std::map<SettingId, Value> changed = {{SettingId::kDisplay, value}};
      HotApplyPlan plan; plan.live = changed;
      const auto generation = settings_boot_->NoteCommittedRows(changed, plan, revision);
      settings_boot_->NoteAppliedRows(changed,
          {{SettingId::kDisplay, HotApplyStatus::kApplied, {}}}, generation);
    };
    sink->SetDisplayPreviewHooks(std::move(hooks));
  }
  sink->SetRestartHandler([this](uint64_t saved, uint64_t current, uint64_t id,
                                const std::string& target, std::string* error) {
    rb_blitz::platform::LaunchContext context;
    context.executable = rex::platform::GetExecutablePath();
    context.game_data_root = GameRoot();
    context.working_directory = std::filesystem::current_path();
    context.profile_file = settings_boot_->paths().legacy_profile;
    context.target = target;
    context.developer_arguments = rb_blitz::platform::CommandLineArguments();
    if (!context.developer_arguments.empty()) context.developer_arguments.erase(context.developer_arguments.begin());
    rb_blitz::dlc::DrainRefreshForRestart();
    bool ready = false;
    if (!GetUI().RunSynchronous([&] {
      restart_coordinator_.ResetAfterFailure();
      const auto outcome = restart_coordinator_.Request(saved, current, id, context);
      if (error) *error = outcome.error;
      ready = restart_coordinator_.CloseRequested();
      if (ready && !rb_blitz::platform::window_host::ExpectRestart(restart_launcher_->ChildPid())) {
        ready = false;
        if (error) *error = "the native window could not accept the replacement game";
      }
      if (ready) REXLOG_INFO("settings_restart: replacement game is waiting; native window stays open");
    }) || !ready) return false;
    GetUI().PostTask([this] { RequestShutdown(); });
    return true;
  });
}
