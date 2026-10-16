// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <vector>

namespace rb_blitz::input {

struct MenuPoint { float x = 0, y = 0; };
// Invert the presenter's centered aspect/overscan rectangle in physical pixels.
inline MenuPoint MenuPointer(float x, float y, int w, int h, int aspect_w, int aspect_h,
                             bool letterbox, bool overscan, int safe_x, int safe_y) {
  if (w <= 0 || h <= 0 || aspect_w <= 0 || aspect_h <= 0) return {NAN, NAN};
  auto scale = [](int value, int numerator, int denominator) {
    return int((int64_t(value) * numerator + denominator / 2) / denominator);
  };
  int pw = w, ph = h;
  if (int64_t(w) * aspect_h > int64_t(h) * aspect_w) {
    int safe = overscan && safe_y > 0 && safe_y < 100 ? safe_y : 100;
    ph = scale(w, aspect_h, aspect_w);
    if (int64_t(ph) * safe > int64_t(h) * 100) {
      ph = scale(h, 100, safe);
      if (letterbox) pw = scale(h * 100, aspect_w, aspect_h * safe);
    }
  } else {
    int safe = overscan && safe_x > 0 && safe_x < 100 ? safe_x : 100;
    pw = scale(h, aspect_w, aspect_h);
    if (int64_t(pw) * safe > int64_t(w) * 100) {
      pw = scale(w, 100, safe);
      if (letterbox) ph = scale(w * 100, aspect_h, aspect_w * safe);
    }
  }
  return {(x - (w - pw) / 2) / pw, (y - (h - ph) / 2) / ph};
}
struct MenuBounds {
  float left = 0, top = 0, right = 0, bottom = 0;
  bool Contains(MenuPoint p) const {
    return std::isfinite(p.x) && std::isfinite(p.y) &&
           p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
  }
};
struct MenuRow {
  MenuPoint center;
  int index = -1;
  bool enabled = false;
  std::optional<MenuBounds> bounds;
};

// Coordinates are normalized through the camera that actually drew the list.
// Disabled rows participate in the hit test so hovering one cannot select a neighbor.
inline int HitMenuRow(MenuPoint p, const MenuBounds& bounds,
                      const std::vector<MenuRow>& rows) {
  if (!bounds.Contains(p)) return -1;
  float distance = std::numeric_limits<float>::infinity();
  const MenuRow* closest = nullptr;
  for (const auto& row : rows) {
    if (row.bounds && !row.bounds->Contains(p)) continue;
    float dx = p.x - row.center.x, dy = p.y - row.center.y;
    float d = dx * dx + dy * dy;
    if (d < distance) { distance = d; closest = &row; }
  }
  return closest && closest->enabled ? closest->index : -1;
}

enum class MouseAction { kMove, kAccept, kBack, kUp, kDown, kLeft, kRight };
struct MenuMouseEvent { MouseAction action; MenuPoint point; };

// Guest-frame pulses, independent of how many times XInput is polled per frame.
// Both the press and release must reach a pad poll before advancing: UI frames
// can run while the guest's separate input thread is delayed.
// Every screen/focus boundary discards queued input, including partial wheel ticks.
class MenuMouseQueue {
 public:
  void Context(uint32_t screen, uint32_t focus, bool enabled) {
    if (screen != screen_ || focus != focus_ || enabled != enabled_) Reset();
    screen_ = screen; focus_ = focus; enabled_ = enabled;
  }
  void Reset() {
    events_.clear(); wheel_ = 0;
    if (pulse_) release_pending_ = true;
    pulse_ = 0; frames_ = 0; sampled_ = false;
  }
  bool enabled() const { return enabled_; }
  void Push(MouseAction action, MenuPoint point = {}) {
    if (!enabled_) return;
    if (action == MouseAction::kMove && !events_.empty() &&
        events_.back().action == MouseAction::kMove) events_.back().point = point;
    else if (events_.size() < 128) events_.push_back({action, point});
  }
  void Wheel(int delta) {
    if (!enabled_) return;
    // A direction reversal must not pay off the previous fractional detent.
    if ((delta > 0 && wheel_ < 0) || (delta < 0 && wheel_ > 0)) wheel_ = 0;
    wheel_ = std::clamp(wheel_ + delta, -120 * 64, 120 * 64);
    while (wheel_ >= 120) { Push(MouseAction::kUp); wheel_ -= 120; }
    while (wheel_ <= -120) { Push(MouseAction::kDown); wheel_ += 120; }
  }
  std::optional<MenuMouseEvent> NextFrame(bool can_scroll = true) {
    if (!enabled_) return {};
    if (frames_ > 0) { --frames_; return {}; }
    if (pulse_) {
      if (sampled_) { pulse_ = 0; sampled_ = false; release_pending_ = true; }
      return {}; // at least one complete release frame
    }
    if (release_pending_) {
      if (!sampled_) return {};
      release_pending_ = false;
    }
    if (events_.empty()) return {};
    const auto action = events_.front().action;
    if (!can_scroll && (action == MouseAction::kUp || action == MouseAction::kDown)) return {};
    auto event = events_.front(); events_.pop_front();
    return event;
  }
  void Pulse(MouseAction action) {
    pulse_ = action == MouseAction::kAccept ? 0x1000 :
             action == MouseAction::kBack ? 0x2000 :
             action == MouseAction::kUp ? 1 : action == MouseAction::kDown ? 2 :
             action == MouseAction::kLeft ? 4 : action == MouseAction::kRight ? 8 : 0;
    frames_ = pulse_ ? 1 : 0; // two guest frames down, then one frame released
    sampled_ = false;
  }
  uint16_t pulse() const { return enabled_ ? pulse_ : 0; }
  // Only the final player-one pad filter acknowledges delivery. The synthetic
  // driver's connectivity/packet queries must not consume the pulse.
  uint16_t SamplePad() { sampled_ = true; return pulse(); }
 private:
  uint32_t screen_ = 0, focus_ = 0;
  bool enabled_ = false;
  int wheel_ = 0, frames_ = 0;
  uint16_t pulse_ = 0;
  bool sampled_ = false, release_pending_ = false;
  std::deque<MenuMouseEvent> events_;
};

// Thread-safe bridge. Window events contain no guest pointers; only the UIManager
// hook reads guest objects or calls guest functions.
bool MouseSupportEnabled();
bool MouseMenuAvailable();
void MouseMenuFocus(bool focused);
void QueueMenuMouse(MouseAction action, MenuPoint point);
void QueueMenuWheel(int delta);
uint16_t MenuMousePadState();
uint16_t SampleMenuMousePadState();

// The UIManager frame state the help caption observer (src/hooks/help_observer.cpp) reads, as the
// last frame's Context() stored it. `FocusedListDrawn()` is the list the previous frame's draw
// identified as the focused component (0 when none) - the only shape the observer may read a
// selection from, because a slider has no list state at list+176. Recorded regardless of the
// mouse preference, because the captions must not depend on an input setting.
uint32_t CurrentScreen();
uint32_t CurrentFocus();
uint32_t FocusedListDrawn();
bool MenuAvailable();
} // namespace rb_blitz::input
