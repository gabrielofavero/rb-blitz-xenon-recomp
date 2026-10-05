// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the focus trace (launcher/src/focus_log.h, A3).

#include "focus_log.h"

#include <ios>

namespace rb_blitz::launcher {

FocusLog::FocusLog(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  out_.open(path, std::ios::out | std::ios::trunc);
}

void FocusLog::Write(std::string_view kind, std::string_view text) {
  if (!out_.is_open()) {
    return;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start_;
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  out_ << millis << "ms " << kind << ' ' << text << '\n';
  out_.flush();
}

}  // namespace rb_blitz::launcher
