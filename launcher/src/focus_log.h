// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The focus trace: what the ring and the input devices did, as a file (--focus-log).
//
// A1's evidence was the window title, which can say which tab the shell is on and nothing more.
// A3 (the pad moves the ring) and A2 (the bar names the focused row) both come down to *which
// row* the ring is on at a given moment, and which device put it there - a question a screenshot
// answers only by eye, on a display the reader does not have. So the shell writes the answer down
// when it changes, and the check is a diff of one small text file instead of a picture.
//
// This is a diagnostic and not a feature: it is off unless the switch names it, it writes only
// when something changes, and nothing in the launcher reads it back.

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace rb_blitz::launcher {

class FocusLog {
 public:
  FocusLog() = default;
  // An empty path leaves it disabled, which is what every ordinary run does. A path opens (and
  // truncates) the file: a trace appended to yesterday's trace would be a worse trace.
  explicit FocusLog(const std::filesystem::path& path);

  bool enabled() const { return out_.is_open(); }

  // One line, attributed to nothing but the clock: `kind` is a word the reader greps for
  // ("focus", "device", "pads") and `text` is what changed. Written and flushed as it is asked
  // for, because the file is read while the launcher is still open - a buffered tail would be
  // exactly the part a script is waiting for.
  void Write(std::string_view kind, std::string_view text);

 private:
  std::ofstream out_;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace rb_blitz::launcher
