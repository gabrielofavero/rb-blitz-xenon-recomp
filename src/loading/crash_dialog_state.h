// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string_view>
namespace rb_blitz::loading {
struct CrashDialogState {
  bool recovered = false, start_taken = false, popup_taken = false, menu_ready = false;
  bool Query(std::string_view path, bool* answer) {
    if (!path.starts_with("rbbz-load/recovery/")) return false;
    *answer = false;
    if (path == "rbbz-load/recovery/active") { *answer = recovered && !popup_taken; return true; }
    if (!recovered || !menu_ready) return true;
    if (path == "rbbz-load/recovery/start") *answer = !start_taken;
    else if (path == "rbbz-load/recovery/starting") { start_taken = true; *answer = true; }
    else if (path == "rbbz-load/recovery/popup" && start_taken && !popup_taken) {
      popup_taken = true;
      *answer = true;
    }
    return true;
  }
};
}
