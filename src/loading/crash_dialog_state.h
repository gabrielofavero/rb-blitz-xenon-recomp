// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string_view>
#include <cstdint>
namespace rb_blitz::loading {
struct CrashDialogState {
  bool recovered = false, start_taken = false, popup_taken = false, menu_ready = false;
  bool settings_restart = false;
  bool fast_startup = false;
  uint32_t timeout_seconds = 0;
  bool Query(std::string_view path, bool* answer) {
    if (!path.starts_with("rbbz-load/recovery/")) return false;
    *answer = false;
    // Share the guest's short boot route; only crash recovery is owed an error popup.
    if (path == "rbbz-load/recovery/active") {
      *answer = (recovered && !popup_taken) || ((settings_restart || fast_startup) && !start_taken);
      return true;
    }
    if (path == "rbbz-load/recovery/timeout30") { *answer = recovered && timeout_seconds == 30; return true; }
    if (path == "rbbz-load/recovery/timeout60") { *answer = recovered && timeout_seconds == 60; return true; }
    if ((!recovered && !settings_restart && !fast_startup) || !menu_ready) return true;
    if (path == "rbbz-load/recovery/start") *answer = !start_taken;
    else if (path == "rbbz-load/recovery/starting") { start_taken = true; *answer = true; }
    else if (path == "rbbz-load/recovery/popup" && recovered && start_taken && !popup_taken) {
      popup_taken = true;
      *answer = true;
    }
    return true;
  }
};
}
