// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "profile/remap.h"

namespace rb_blitz::input::last_device {

enum class Family { kKeyboardMouse, kController };

struct State {
  Family family = Family::kKeyboardMouse;
  std::string scheme = "retail";
  uint64_t revision = 0;
};

// Plain-data tracker. Times are monotonic milliseconds supplied by the caller.
// Only new physical activity changes family; held controls and focus changes do not.
class Tracker {
 public:
  State Current() const { return state_; }
  void SelectScheme(std::string scheme);
  void Focus(bool focused);
  bool KeyDown(uint64_t now_ms);
  bool MouseButton(uint64_t now_ms);
  bool MouseWheel(uint64_t now_ms);
  bool MouseMove(int x, int y, uint64_t now_ms);
  bool Pad(bool connected, const profile::remap::PadState& pad, uint64_t now_ms);
  // Commits a real opposite-family event deferred by the debounce window.
  bool Tick(uint64_t now_ms);

 private:
  bool Switch(Family family, uint64_t now_ms);
  State state_;
  bool focused_ = false;
  bool pad_active_ = false;
  bool have_pointer_ = false;
  int pointer_x_ = 0, pointer_y_ = 0;
  int motion_ = 0;
  uint64_t last_switch_ms_ = 0;
  bool switched_ = false;
  std::optional<Family> pending_family_;
};

// Process-wide bridge. Each observation returns a state only when the family changes.
State Current();
void SelectScheme(std::string scheme);
void Focus(bool focused);
std::optional<State> KeyDown();
std::optional<State> MouseButton();
std::optional<State> MouseWheel();
std::optional<State> MouseMove(int x, int y);
std::optional<State> Pad(bool connected, const profile::remap::PadState& pad);
std::optional<State> Tick();

}  // namespace rb_blitz::input::last_device
