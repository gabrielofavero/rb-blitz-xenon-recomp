// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

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
};

// The six-option guest list ends above this strip; footer hints occupy the
// bottom tenth. Never let text or its background grow outside this strip.
// Authored captions: <=96 ASCII characters, short sentences, no manual wrapping.
// Real font measurement below remains authoritative for narrow windows / DPI.
inline CaptionLayout LayoutCaption(float width, float height, std::string_view size) {
  width = std::max(1.0f, width);
  height = std::max(1.0f, height);
  const float scale = size == "biggest" ? 1.5f : size == "big" ? 1.25f : 1.0f;
  const float top = height * 0.765f;
  const float bottom = height * 0.865f;
  const float font = std::min(std::clamp(height * 0.028f, 16.0f, 64.0f) * scale,
                              (bottom - top) / 1.2f);
  const float panel_width = width * (size == "biggest" ? 0.88f : size == "big" ? 0.80f : 0.72f);
  return {font, panel_width, top, bottom,
          std::min(font * 0.35f, panel_width * 0.1f), font * 0.1f};
}

// Keep the selected size. Overflow is shortened at word boundaries with an
// ellipsis instead of shrinking the font or covering options / button hints.
// Fits uses the renderer's actual wrapped font metrics, not a character estimate.
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
