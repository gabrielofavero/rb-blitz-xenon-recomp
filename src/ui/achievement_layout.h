// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include "ui/portrait_viewport.h"

namespace rb_blitz::ui::achievements {

struct BrowserLayout {
  bool portrait;
  int columns;
  float scale, padding, x, y, width, height;
};

inline BrowserLayout LayoutBrowser(float width, float height, PortraitViewport viewport = {}) {
  const bool portrait = width < height;
  float left = 0, top = 0, right = width, bottom = height;
  if (portrait && viewport.width > 0 && viewport.height > 0 &&
      viewport.safe_width > 0 && viewport.safe_height > 0) {
    left = std::clamp(viewport.safe_x / viewport.width * width, 0.f, width);
    top = std::clamp(viewport.safe_y / viewport.height * height, 0.f, height);
    right = std::clamp((viewport.safe_x + viewport.safe_width) / viewport.width * width, left, width);
    bottom = std::clamp((viewport.safe_y + viewport.safe_height) / viewport.height * height, top, height);
    if (right <= left || bottom <= top) { left = top = 0; right = width; bottom = height; }
  }
  // Portrait uses a phone-width baseline so text and controls stay readable.
  const float scale = std::min((right - left) / (portrait ? 400.f : 1280.f),
                               (bottom - top) / 720.f);
  const float padding = (portrait ? 16 : 24) * scale;
  const float panel_width = portrait ? right - left - 2 * padding :
      std::min(1140 * scale, right - left - 2 * padding);
  return {portrait, portrait ? 1 : 2, scale, padding,
          (left + right - panel_width) / 2, top + padding,
          panel_width, bottom - top - 2 * padding};
}

}  // namespace rb_blitz::ui::achievements
