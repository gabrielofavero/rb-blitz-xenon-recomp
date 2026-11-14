// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cstdint>

namespace rb_blitz::ui::achievements {

enum Command : uint32_t {
  kLeft = 1, kRight = 2, kUp = 4, kDown = 8, kFirst = 16, kLast = 32,
  kPageUp = 64, kPageDown = 128, kBack = 256,
};

// Shared by keyboard and physical-pad input. Opening/focus changes require a
// neutral frame; held directions repeat after a delay, while Back never repeats.
class Navigation {
 public:
  int selected = 0;

  void Reset() { *this = Navigation{}; }
  uint32_t Tick(uint32_t held, bool focused, float dt) {
    if (!focused) { ready_ = false; previous_ = 0; return 0; }
    if (!ready_) { ready_ = held == 0; previous_ = held; return 0; }
    const uint32_t pressed = held & ~previous_;
    if (held != previous_) { repeat_in_ = .35f; previous_ = held; return pressed; }
    if (!held) return 0;
    repeat_in_ -= std::clamp(dt, 0.0f, .1f);
    if (repeat_in_ > 0) return 0;
    repeat_in_ = .09f;
    return held & ~kBack;
  }

  bool Move(uint32_t command, int count, int page_rows) {
    const int before = selected;
    if (count <= 0) { selected = 0; return false; }
    selected = std::clamp(selected, 0, count - 1);
    if (command & kFirst) selected = 0;
    else if (command & kLast) selected = count - 1;
    else if (command & kPageUp) selected = std::max(0, selected - 2 * page_rows);
    else if (command & kPageDown) selected = std::min(count - 1, selected + 2 * page_rows);
    else if (command & kUp) selected = std::max(selected % 2, selected - 2);
    else if ((command & kDown) && selected / 2 < (count - 1) / 2)
      selected = std::min(count - 1, selected + 2);
    else if ((command & kLeft) && selected % 2) --selected;
    else if ((command & kRight) && selected % 2 == 0 && selected + 1 < count) ++selected;
    return before != selected;
  }

 private:
  bool ready_ = false;
  uint32_t previous_ = 0;
  float repeat_in_ = .35f;
};
}  // namespace rb_blitz::ui::achievements
