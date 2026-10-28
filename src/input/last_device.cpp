// SPDX-License-Identifier: GPL-2.0-only
#include "input/last_device.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace rb_blitz::input::last_device {
namespace {
constexpr uint64_t kDebounceMs = 100;
constexpr int kMouseMotionPixels = 12;
constexpr uint8_t kTriggerThreshold = 30;

uint64_t NowMs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool Active(const profile::remap::PadState& pad) {
  const int threshold = profile::remap::kStickThreshold;
  return pad.buttons || pad.left_trigger > kTriggerThreshold ||
         pad.right_trigger > kTriggerThreshold ||
         std::abs(int(pad.left_x)) > threshold ||
         std::abs(int(pad.left_y)) > threshold ||
         std::abs(int(pad.right_x)) > threshold ||
         std::abs(int(pad.right_y)) > threshold;
}

std::mutex g_mutex;
Tracker g_tracker;

template <typename F>
std::optional<State> Observe(F&& observe) {
  std::lock_guard lock(g_mutex);
  if (!observe(g_tracker, NowMs())) return std::nullopt;
  return g_tracker.Current();
}
}  // namespace

void Tracker::SelectScheme(std::string scheme) {
  if (scheme.empty()) scheme = "retail";
  if (state_.scheme == scheme) return;
  state_.scheme = std::move(scheme);
  ++state_.revision;
}

void Tracker::Focus(bool focused) {
  focused_ = focused;
  have_pointer_ = false;
  motion_ = 0;
  // A background release may never arrive. The next focused pad observation can
  // establish a fresh edge, but focus loss itself never changes the family.
  if (!focused) {
    pad_active_ = false;
    pending_family_.reset();
  }
}

bool Tracker::Switch(Family family, uint64_t now_ms) {
  if (state_.family == family) {
    pending_family_.reset();
    return false;
  }
  if (switched_ && now_ms >= last_switch_ms_ &&
      now_ms - last_switch_ms_ < kDebounceMs) {
    pending_family_ = family;
    return false;
  }
  pending_family_.reset();
  state_.family = family;
  ++state_.revision;
  last_switch_ms_ = now_ms;
  switched_ = true;
  motion_ = 0;
  have_pointer_ = false;
  return true;
}

bool Tracker::Tick(uint64_t now_ms) {
  if (!pending_family_ || !focused_ || now_ms < last_switch_ms_ ||
      now_ms - last_switch_ms_ < kDebounceMs) return false;
  return Switch(*pending_family_, now_ms);
}

bool Tracker::KeyDown(uint64_t now_ms) { return focused_ && Switch(Family::kKeyboardMouse, now_ms); }
bool Tracker::MouseButton(uint64_t now_ms) { return focused_ && Switch(Family::kKeyboardMouse, now_ms); }
bool Tracker::MouseWheel(uint64_t now_ms) { return focused_ && Switch(Family::kKeyboardMouse, now_ms); }

bool Tracker::MouseMove(int x, int y, uint64_t now_ms) {
  if (!focused_) return false;
  if (!have_pointer_) {
    pointer_x_ = x; pointer_y_ = y; have_pointer_ = true;
    return false;
  }
  motion_ = std::min(kMouseMotionPixels, motion_ +
      std::abs(x - pointer_x_) + std::abs(y - pointer_y_));
  pointer_x_ = x; pointer_y_ = y;
  if (motion_ < kMouseMotionPixels) return false;
  motion_ = 0;
  return Switch(Family::kKeyboardMouse, now_ms);
}

bool Tracker::Pad(bool connected, const profile::remap::PadState& pad, uint64_t now_ms) {
  const bool active = connected && Active(pad);
  const bool edge = active && !pad_active_;
  pad_active_ = active;
  return focused_ && edge && Switch(Family::kController, now_ms);
}

State Current() { std::lock_guard lock(g_mutex); return g_tracker.Current(); }
void SelectScheme(std::string scheme) {
  std::lock_guard lock(g_mutex);
  g_tracker.SelectScheme(std::move(scheme));
}
void Focus(bool focused) { std::lock_guard lock(g_mutex); g_tracker.Focus(focused); }
std::optional<State> KeyDown() {
  return Observe([](Tracker& t, uint64_t now) { return t.KeyDown(now); });
}
std::optional<State> MouseButton() {
  return Observe([](Tracker& t, uint64_t now) { return t.MouseButton(now); });
}
std::optional<State> MouseWheel() {
  return Observe([](Tracker& t, uint64_t now) { return t.MouseWheel(now); });
}
std::optional<State> MouseMove(int x, int y) {
  return Observe([&](Tracker& t, uint64_t now) { return t.MouseMove(x, y, now); });
}
std::optional<State> Pad(bool connected, const profile::remap::PadState& pad) {
  return Observe([&](Tracker& t, uint64_t now) { return t.Pad(connected, pad, now); });
}
std::optional<State> Tick() {
  return Observe([](Tracker& t, uint64_t now) { return t.Tick(now); });
}
}  // namespace rb_blitz::input::last_device
