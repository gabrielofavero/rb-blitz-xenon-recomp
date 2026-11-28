// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace rb_blitz::ui::music {

// Song DTA display fields can contain Latin-1 bytes, while ImGui expects UTF-8.
// Preserve valid UTF-8 sequences so newer packages and mixed-encoding fields work too.
inline std::string DisplayTextUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    size_t length = 0;
    if (lead < 0x80) length = 1;
    else if (lead >= 0xC2 && lead <= 0xDF) length = 2;
    else if (lead >= 0xE0 && lead <= 0xEF) length = 3;
    else if (lead >= 0xF0 && lead <= 0xF4) length = 4;
    if (length > 1 && i + length <= text.size()) {
      const auto second = static_cast<unsigned char>(text[i + 1]);
      if ((second & 0xC0) != 0x80 ||
          (lead == 0xE0 && second < 0xA0) ||
          (lead == 0xED && second >= 0xA0) ||
          (lead == 0xF0 && second < 0x90) ||
          (lead == 0xF4 && second >= 0x90)) length = 0;
      for (size_t j = 2; length && j < length; ++j)
        if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) length = 0;
    } else if (length > 1) {
      length = 0;
    }
    if (length) {
      out.append(text.substr(i, length));
      i += length;
    } else {
      out += static_cast<char>(0xC0 | (lead >> 6));
      out += static_cast<char>(0x80 | (lead & 0x3F));
      ++i;
    }
  }
  return out;
}

class TitleMarquee {
 public:
  void Reset() { distance_ = 0; }

  float Offset(float text_width, float width, float scale, double now) {
    const float distance = std::max(0.f, text_width - width);
    if (distance != distance_ || scale != scale_) {
      distance_ = distance;
      scale_ = scale;
      started_at_ = now;
    }
    if (distance == 0) return 0;

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
  float distance_ = 0, scale_ = 0;
  double started_at_ = 0;
};

}  // namespace rb_blitz::ui::music
