// SPDX-License-Identifier: GPL-2.0-only
#include "loading/crash_recovery.h"
#include "loading/crash_dialog_state.h"
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

namespace rb_blitz::loading {
namespace {
platform::song_load::Session session;
std::atomic<bool> configured{false};
CrashDialogState presentation;
bool inject = false;
}
void ConfigureCrashRecovery(rex::Runtime* runtime) {
  presentation.recovered = REXCVAR_GET(song_load_recovered);
  inject = REXCVAR_GET(song_load_test_crash);
  if (!REXCVAR_GET(song_load_crash_recovery)) return;
  auto args = platform::CommandLineArguments();
  if (!args.empty()) args.erase(args.begin());
  const auto monitor = rex::filesystem::GetExecutableFolder() / "rb_blitz_load_monitor.exe";
  const auto log = runtime->cache_root() / "song-load-recovery.log";
  std::string error;
  if (session.Start(monitor, args, log, &error)) {
    configured.store(true, std::memory_order_release);
    REXLOG_INFO("load_recovery: external monitor ready pid={} recovered={}", session.monitor_pid(), presentation.recovered);
  } else REXLOG_ERROR("load_recovery: unavailable: {}", error);
}
void BeginCrashRecoveryAttempt() {
  if (!configured.load(std::memory_order_acquire)) return;
  session.SetLoading(true);
  if (inject) {
    inject = false;
    REXLOG_WARN("load_recovery: injecting one synthetic process failure before song setup");
#if defined(_WIN32)
    ExitProcess(0xC0000005);
#endif
  }
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
