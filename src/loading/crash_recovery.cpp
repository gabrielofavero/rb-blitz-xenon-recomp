// SPDX-License-Identifier: GPL-2.0-only
#include "loading/crash_recovery.h"
#include "loading/crash_dialog_state.h"
#include "loading/load_policy.h"
#include "platform/song_load_monitor.h"
#include "platform/game_restart.h"
#include <atomic>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#if defined(_WIN32)
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(song_load_crash_recovery, true, "Runtime",
    "Restart into the main menu with a native error dialog after a fatal process exit during song loading.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(song_load_recovered, false, "Internal",
    "One-shot crash-recovery boot reason. Supplied by the external load monitor.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(song_load_test_crash, false, "Diagnostics",
    "Exit with a synthetic crash code on the first song load to test the external restart. Removed on recovery.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(song_load_guard, "off", "Runtime",
    "Restart to the main menu when a song load exceeds this deadline; applies to the next load.")
    .allowed({"off", "30s", "60s"});
REXCVAR_DEFINE_INT32(song_load_timeout, 0, "Internal",
    "One-shot timeout recovery reason, supplied by the load monitor.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(song_load_test_stall, false, "Diagnostics",
    "Block the first song job before native work, to test timeout restart. Removed on recovery.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace rb_blitz::loading {
namespace {
platform::song_load::Session session;
std::atomic<bool> configured{false};
CrashDialogState presentation;
bool inject = false;
bool inject_stall = false;
uint32_t attempt_seconds = 0;
}
void ConfigureCrashRecovery(rex::Runtime* runtime, bool settings_restart_boot) {
  presentation.recovered = REXCVAR_GET(song_load_recovered);
  presentation.settings_restart = settings_restart_boot;
  if (settings_restart_boot)
    REXLOG_INFO("settings_restart: short boot; auto-starting the local user through discovery");
  presentation.timeout_seconds = REXCVAR_GET(song_load_timeout);
  inject = REXCVAR_GET(song_load_test_crash);
  inject_stall = REXCVAR_GET(song_load_test_stall);
  PublishLoadGuardPolicy(REXCVAR_GET(song_load_guard));
  rex::cvar::RegisterChangeCallback("song_load_guard", [](std::string_view, std::string_view value) {
    PublishLoadGuardPolicy(value);
  });
  auto args = platform::CommandLineArguments();
  if (!args.empty()) args.erase(args.begin());
  const auto monitor = rex::filesystem::GetExecutableFolder() / "rb_blitz_load_monitor.exe";
  const auto log = runtime->cache_root() / "song-load-recovery.log";
  std::string error;
  if (session.Start(monitor, args, log, &error, REXCVAR_GET(song_load_crash_recovery))) {
    configured.store(true, std::memory_order_release);
    REXLOG_INFO("load_recovery: external monitor ready pid={} recovered={}", session.monitor_pid(), presentation.recovered);
  } else REXLOG_ERROR("load_recovery: unavailable: {}", error);
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
  const bool handled = presentation.Query(path, answer);
  if (handled && *answer && path == "rbbz-load/recovery/popup") {
    REXLOG_INFO("load_recovery: main menu ready; consuming native crash dialog once");
  }
  return handled;
}
}
