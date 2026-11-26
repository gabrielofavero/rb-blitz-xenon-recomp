// SPDX-License-Identifier: GPL-2.0-only
#include "loading/crash_recovery.h"
#include "loading/crash_dialog_state.h"
#include "loading/load_policy.h"
#include "platform/song_load_monitor.h"
#include "platform/game_restart.h"
#include <atomic>
#include "settings/runtime_settings.h"
#include <rex/platform/file.h>
#include <rex/path_settings.h>
#include <rex/logging/log_category.h>
#if defined(_WIN32)
#include <windows.h>
#endif










namespace rb_blitz::loading {
namespace {
platform::song_load::Session session;
std::atomic<bool> configured{false};
CrashDialogState presentation;
rex::ScopeGuard guard_policy_change;
bool inject = false;
bool inject_stall = false;
uint32_t attempt_seconds = 0;
}
void ConfigureCrashRecovery(const std::filesystem::path& cache_root, bool settings_restart_boot) {
  presentation.recovered = rb_blitz::settings::title::song_load_recovered.Get();
  presentation.settings_restart = settings_restart_boot;
  presentation.fast_startup = rb_blitz::settings::title::startup_mode.Get() == "fast";
  if (presentation.fast_startup)
    REXLOG_INFO("startup: fast; auto-starting the local user through discovery");
  if (settings_restart_boot)
    REXLOG_INFO("settings_restart: short boot; auto-starting the local user through discovery");
  presentation.timeout_seconds = rb_blitz::settings::title::song_load_timeout.Get();
  inject = rb_blitz::settings::title::song_load_test_crash.Get();
  inject_stall = rb_blitz::settings::title::song_load_test_stall.Get();
  PublishLoadGuardPolicy(rb_blitz::settings::title::song_load_guard.Get());
  guard_policy_change = rb_blitz::settings::title::song_load_guard.OnChange([](const std::string& value) {
    PublishLoadGuardPolicy(value);
  });
  auto args = platform::CommandLineArguments();
  if (!args.empty()) args.erase(args.begin());
  const auto monitor = rex::platform::GetExecutableFolder() / "rb_blitz_load_monitor.exe";
  const auto log = cache_root / "song-load-recovery.log";
  std::string error;
  if (session.Start(monitor, args, log, &error, rb_blitz::settings::title::song_load_crash_recovery.Get())) {
    configured.store(true, std::memory_order_release);
    REXLOG_INFO("load_recovery: external monitor ready pid={} recovered={}", session.monitor_pid(), presentation.recovered);
  } else REXLOG_ERROR("load_recovery: unavailable: {}", error);
}
void CloseCrashRecovery() {
  // Settings are destroyed before some process globals. Retire the callback
  // while its setting and mutex still exist, rather than during static teardown.
  guard_policy_change.Reset();
  if (configured.exchange(false, std::memory_order_acq_rel)) session.SetLoading(false);
}
void BeginCrashRecoveryAttempt() {
  if (!configured.load(std::memory_order_acquire)) return;
  const auto seconds = CurrentLoadGuardSeconds();
  attempt_seconds = seconds;
  session.SetLoading(true, seconds * 1000);
  REXLOG_INFO("load_recovery: song attempt armed timeout_seconds={}", seconds);
  if (inject) {
    inject = false;
    REXLOG_WARN("load_recovery: injecting one synthetic process failure before song setup");
#if defined(_WIN32)
    ExitProcess(0xC0000005);
#endif
  }
}
void BeforeSongLoadJob() {
  if (!configured.load(std::memory_order_acquire) || !inject_stall) return;
  if (!attempt_seconds) {
    REXLOG_WARN("load_recovery: diagnostic stall deferred because this attempt has no timeout");
    return;
  }
  inject_stall = false;
  REXLOG_WARN("load_recovery: injecting one stalled song job before native work");
#if defined(_WIN32)
  Sleep(INFINITE);
#endif
}
void EndCrashRecoveryAttempt() {
  if (configured.load(std::memory_order_acquire)) session.SetLoading(false);
}
void ObserveRecoveryScreen(std::string_view name, bool stable) {
  presentation.menu_ready = stable && name == "splash_screen";
}
bool CrashRecoveryQuery(std::string_view path, bool* answer) {
  if (path == "rbbz-load/recovery/unlock_error_enter" ||
      path == "rbbz-load/recovery/unlock_error_match")
    REXLOG_WARN("startup: purchased short-boot error-screen probe: {}", path);
  const bool handled = presentation.Query(path, answer);
  if (handled && *answer && path == "rbbz-load/recovery/popup") {
    REXLOG_INFO("load_recovery: main menu ready; consuming native crash dialog once");
  }
  return handled;
}
}
