// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>

namespace rb_blitz::input {

// x/y are window points for gesture slop; row follows the presented list's
// actual row pitch, so dragging stays attached to the finger at any resolution.
struct TouchScrollPoint { float x = 0, y = 0, row = 0; };
enum class TouchScrollAxis { kVertical, kHorizontal };

class TouchListScroll {
 public:
  void Reset() { *this = {}; }
  bool OwnsView() const { return owns_view_; }
  bool Pressed() const { return pressed_; }
  bool Dragging() const { return dragging_; }
  float Position() const { return position_; }
  float Velocity() const { return velocity_; }

  void Limit(float maximum) {
    maximum_ = std::max(0.f, maximum);
    Clamp();
  }

  void Begin(TouchScrollPoint point, float first, float maximum, double now,
             TouchScrollAxis axis = TouchScrollAxis::kVertical) {
    Advance(now);
    const bool catching = std::abs(velocity_) > kStopSpeed;
    if (!owns_view_) position_ = first;
    owns_view_ = pressed_ = true;
    dragging_ = false;
    tap_ = !catching;
    velocity_ = 0;
    axis_ = axis;
    start_ = last_ = point;
    last_motion_ = now;
    clock_ = now;
    Limit(maximum);
  }

  void Move(TouchScrollPoint point, double now) {
    if (!pressed_ || !Finite(point)) return;
    const float dx = point.x - start_.x, dy = point.y - start_.y;
    if (std::hypot(dx, dy) > kSlop) tap_ = false;
    const bool horizontal = axis_ == TouchScrollAxis::kHorizontal;
    const float along = std::abs(horizontal ? dx : dy);
    const float across = std::abs(horizontal ? dy : dx);
    if (along > kSlop && along >= across) dragging_ = true;
    if (dragging_) {
      const float delta = last_.row - point.row;
      const float before = position_;
      position_ += delta;
      const double elapsed = now - last_motion_;
      if (elapsed > 0 && delta != 0) {
        const float sample = std::clamp(float(delta / elapsed), -90.f, 90.f);
        const float weight = float(1 - std::exp(-elapsed / .04));
        velocity_ = elapsed > .12 ? 0 : velocity_ + weight * (sample - velocity_);
      }
      Clamp();
      if (position_ == before && delta != 0) velocity_ = 0;
    }
    // A stationary release must not replace the last moving velocity sample.
    if (point.row != last_.row) last_motion_ = now;
    last_ = point;
    clock_ = now;
  }

  bool End(TouchScrollPoint point, double now) {
    if (!pressed_) return false;
    Move(point, now);
    const bool tap = tap_ && !dragging_ && Finite(point);
    pressed_ = false;
    if (!dragging_ || now - last_motion_ > .10) velocity_ = 0;
    dragging_ = false;
    return tap;
  }

  void Advance(double now) {
    const double elapsed = now - clock_;
    clock_ = now;
    if (!owns_view_ || pressed_ || elapsed <= 0) return;
    // Never replay momentum accumulated while the guest was suspended.
    if (elapsed > .25) { velocity_ = 0; return; }
    constexpr double friction = 6;
    const float decay = float(std::exp(-friction * elapsed));
    position_ += velocity_ * (1 - decay) / float(friction);
    velocity_ *= decay;
    if (std::abs(velocity_) < kStopSpeed) velocity_ = 0;
    Clamp();
  }

 private:
  static bool Finite(TouchScrollPoint p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.row);
  }
  void Clamp() {
    position_ = std::clamp(position_, 0.f, maximum_);
    if ((position_ <= 0 && velocity_ < 0) || (position_ >= maximum_ && velocity_ > 0))
      velocity_ = 0;
  }
  static constexpr float kSlop = 8, kStopSpeed = .08f;
  bool owns_view_ = false, pressed_ = false, dragging_ = false, tap_ = false;
  float position_ = 0, velocity_ = 0, maximum_ = 0;
  double clock_ = 0, last_motion_ = 0;
  TouchScrollAxis axis_ = TouchScrollAxis::kVertical;
  TouchScrollPoint start_, last_;
};
}  // namespace rb_blitz::input
