// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <utility>

#include "input/touch_scroll.h"

namespace rb_blitz::ui::achievements {

enum class ScrollPhase { kDown, kMove, kUp, kCancel };
struct ScrollPoint { float x, y; };
struct ScrollViewport {
  float left = 0, top = 0, right = 0, bottom = 0;
  float display_width = 0, display_height = 0;
  float window_width = 0, window_height = 0;
  bool operator==(const ScrollViewport&) const = default;
  bool Contains(ScrollPoint p) const {
    return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
  }
};

// One unit is forty window points, independent of card height, display density
// and column count. Sharing the song-list model also shares slop and friction.
class CardScroll {
 public:
  void Reset() { *this = {}; }
  void Cancel() { motion_.Reset(); captured_ = false; tap_.reset(); }
  void Configure(ScrollViewport viewport, float current, float maximum, bool enabled) {
    if (viewport != viewport_ || !enabled) Cancel();
    // Releases may never arrive while the window is unfocused or minimized.
    if (!enabled) { contacts_.clear(); primary_.reset(); }
    viewport_ = viewport;
    enabled_ = enabled && viewport.window_width > 0 && viewport.window_height > 0 &&
        viewport.display_width > 0 && viewport.display_height > 0;
    current_ = current;
    maximum_ = std::max(0.f, maximum);
    motion_.Limit(maximum_ * UnitsPerPixel());
  }
  bool TouchOwnsView() const { return motion_.OwnsView(); }

  // Coordinates are normalized to the window. Only the first contact can own a
  // gesture; lifting it never transfers the gesture to a remaining finger.
  void Observe(uint64_t device, uint64_t finger, ScrollPhase phase,
               float x, float y, double now) {
    const auto key = std::pair{device, finger};
    if (phase == ScrollPhase::kDown) {
      if (contacts_.empty()) primary_ = key;
      contacts_.insert(key);
    }
    if (primary_ == key) {
      const ScrollPoint point{x * viewport_.display_width, y * viewport_.display_height};
      const input::TouchScrollPoint at{x * viewport_.window_width,
          y * viewport_.window_height, y * viewport_.window_height / 40};
      if (phase == ScrollPhase::kDown) {
        tap_.reset();
        captured_ = enabled_ && viewport_.Contains(point);
        if (captured_)
          motion_.Begin(at, current_ * UnitsPerPixel(), maximum_ * UnitsPerPixel(), now);
        else Cancel();
      } else if (captured_ && enabled_) {
        if (phase == ScrollPhase::kMove) motion_.Move(at, now);
        else if (phase == ScrollPhase::kUp) {
          if (motion_.End(at, now) && viewport_.Contains(point)) tap_ = point;
          captured_ = false;
        } else Cancel();
      }
    }
    if (phase == ScrollPhase::kUp || phase == ScrollPhase::kCancel) {
      contacts_.erase(key);
      if (primary_ == key) primary_.reset();
    }
  }
  std::optional<float> Advance(double now) {
    motion_.Advance(now);
    if (!enabled_ || !motion_.OwnsView()) return {};
    return motion_.Position() / UnitsPerPixel();
  }
  std::optional<ScrollPoint> TakeTap() { return std::exchange(tap_, {}); }

 private:
  float UnitsPerPixel() const {
    return viewport_.display_height > 0 ? viewport_.window_height / viewport_.display_height / 40 : 0;
  }
  ScrollViewport viewport_;
  input::TouchListScroll motion_;
  std::set<std::pair<uint64_t, uint64_t>> contacts_;
  std::optional<std::pair<uint64_t, uint64_t>> primary_;
  std::optional<ScrollPoint> tap_;
  float current_ = 0, maximum_ = 0;
  bool enabled_ = false, captured_ = false;
};
}  // namespace rb_blitz::ui::achievements
