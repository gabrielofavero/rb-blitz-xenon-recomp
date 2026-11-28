// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>

namespace rb_blitz::ui {

class TextMarquee {
 public:
  void Reset() { distance_ = 0; }

  float Offset(float text_width, float width, float scale, double now) {
    const float distance = std::max(0.f, text_width - width);
    if (distance != distance_ || width != width_ || scale != scale_) {
      distance_ = distance;
      width_ = width;
      scale_ = scale;
      started_at_ = now;
    }
    if (distance == 0 || !(scale > 0)) return 0;

    constexpr double kHoldSeconds = 1.25;
    constexpr double kScrollUnitsPerSecond = 18;
    const double travel = distance / (scale * kScrollUnitsPerSecond);
    const double half_cycle = kHoldSeconds + travel;
    const double phase = std::fmod(now - started_at_, 2 * half_cycle);
    const bool returning = phase >= half_cycle;
    const double leg_time = returning ? phase - half_cycle : phase;
    const double t = std::clamp((leg_time - kHoldSeconds) / travel, 0.0, 1.0);
    const double eased = t * t * (3 - 2 * t);
    return static_cast<float>(distance * (returning ? 1 - eased : eased));
  }

 private:
  float distance_ = 0, width_ = 0, scale_ = 0;
  double started_at_ = 0;
};

}  // namespace rb_blitz::ui
