// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::ui::help {

struct CaptionLayout {
  float font_size;
  float width;
  float top;
  float bottom;
  float pad_x;
  float pad_y;
  float TextWidth() const { return width - 2 * pad_x; }
  float TextHeight() const { return bottom - top - 2 * pad_y; }
  size_t LineCapacity() const {
    return std::max(size_t{1}, static_cast<size_t>(TextHeight() / font_size + .0001f));
  }
  bool operator==(const CaptionLayout&) const = default;
};

// The six-option guest list ends above this strip; footer hints occupy the
// bottom tenth. Never let text or its background grow outside this strip.
// Authored captions: <=96 ASCII characters, short sentences, no manual wrapping.
// Real font measurement below remains authoritative for narrow windows / DPI.
inline CaptionLayout LayoutCaption(float width, float height, std::string_view size,
                                   bool expand_on_narrow_displays = true) {
  width = std::max(1.0f, width);
  height = std::max(1.0f, height);
  const float scale = size == "biggest" ? 1.5f : size == "big" ? 1.25f : 1.0f;
  const float top = height * 0.765f;
  const float bottom = height * 0.865f;
  const float font = std::min(std::clamp(height * 0.028f, 16.0f, 64.0f) * scale,
                              (bottom - top) / 1.2f);
  const float preferred_width = width * (size == "biggest" ? 0.88f : size == "big" ? 0.80f : 0.72f);
  // Use more of a narrow display so each line can keep more words together.
  const float panel_width = expand_on_narrow_displays
      ? std::max(preferred_width, std::min(width * 0.90f, height * 0.60f))
      : preferred_width;
  return {font, panel_width, top, bottom,
          std::min(font * 0.35f, panel_width * 0.1f), font * 0.1f};
}

struct CaptionLine {
  std::string_view text;
  float width;
};

template <typename Wrap, typename Measure>
std::vector<CaptionLine> WrapCaption(std::string_view text, Wrap wrap, Measure measure) {
  std::vector<CaptionLine> lines;
  while (!text.empty()) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const char* newline = std::find(begin, end, '\n');
    const char* line_end = begin == newline ? newline : wrap(begin, newline);
    // ImGui can return one byte for a glyph wider than the available space.
    if (line_end == begin && line_end < newline) ++line_end;
    while (line_end < newline &&
           (static_cast<unsigned char>(*line_end) & 0xC0) == 0x80) ++line_end;
    const char* trimmed = line_end;
    while (trimmed > begin && (trimmed[-1] == ' ' || trimmed[-1] == '\t')) --trimmed;
    const std::string_view line(begin, trimmed - begin);
    lines.push_back({line, measure(line)});
    begin = line_end;
    while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
    if (begin < end && *begin == '\n') ++begin;
    text.remove_prefix(begin - text.data());
  }
  return lines;
}

class CaptionScroll {
 public:
  void Reset() { line_count_ = 0; }

  // Offset in lines. Move just one line at a time so readers can carry their
  // place forward. Hold at both ends before returning to the opening lines.
  float Offset(const CaptionLayout& layout, size_t line_count, uint64_t revision, double now) {
    if (line_count != line_count_ || revision != revision_ || layout != layout_) {
      line_count_ = line_count;
      revision_ = revision;
      layout_ = layout;
      started_at_ = now;
    }
    const size_t visible = layout.LineCapacity();
    if (line_count <= visible) return 0;
    const size_t steps = line_count - visible;
    constexpr double kOpeningHold = 2.6;
    constexpr double kAdvance = .30;
    constexpr double kLineHold = 1.8;
    constexpr double kClosingHold = 2.8;
    constexpr double kReturn = .60;
    constexpr double kStep = kAdvance + kLineHold;
    const double forward = (steps - 1) * kStep + kAdvance;
    const double cycle = kOpeningHold + forward + kClosingHold + kReturn;
    const double phase = std::fmod(now - started_at_, cycle) - kOpeningHold;
    if (phase <= 0) return 0;
    const auto ease = [](double t) { return t * t * (3 - 2 * t); };
    if (phase < forward) {
      const double step = std::floor(phase / kStep);
      const double t = std::min((phase - step * kStep) / kAdvance, 1.0);
      return static_cast<float>(step + ease(t));
    }
    const double returning = std::max(0.0, phase - forward - kClosingHold);
    return static_cast<float>(steps * (1 - ease(returning / kReturn)));
  }

 private:
  CaptionLayout layout_{};
  size_t line_count_ = 0;
  uint64_t revision_ = 0;
  double started_at_ = 0;
};

// Word-boundary ellipsis for stationary status messages and notifications.
template <typename Fits>
std::string FitCaption(std::string text, Fits fits) {
  if (fits(text)) return text;
  while (!text.empty()) {
    const auto space = text.find_last_of(" \n\t");
    if (space != std::string::npos) text.resize(space);
    else {
      size_t last = text.size() - 1;
      while (last && (static_cast<unsigned char>(text[last]) & 0xC0) == 0x80) --last;
      text.resize(last);
    }
    if (fits(text + "...")) return text + "...";
  }
  return fits("...") ? "..." : "";
}

}  // namespace rb_blitz::ui::help
