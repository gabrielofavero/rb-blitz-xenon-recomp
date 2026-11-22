// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace rb_blitz::ui {

// Window units, as returned by SDL_GetWindowSize and SDL_GetWindowSafeArea.
struct PortraitViewport {
  float width = 0, height = 0;
  float safe_x = 0, safe_y = 0, safe_width = 0, safe_height = 0;
};

struct PortraitFit {
  float scale = 1, x = 0, z = 0;
  float X(float value) const { return value * scale + x; }
  float Z(float value) const { return value * scale + z; }
};

// Fit the authored 480-unit canvas without enlarging it. Device cutouts and
// a small gutter constrain the whole canvas, including animated decorations.
inline PortraitFit FitPortraitViewport(float authored_aspect, PortraitViewport viewport) {
  const auto& v = viewport;
  for (float value : {authored_aspect, v.width, v.height, v.safe_x, v.safe_y,
                      v.safe_width, v.safe_height})
    if (!std::isfinite(value)) return {};
  if (authored_aspect <= 0 || v.width <= 0 || v.height <= 0) return {};
  float left = std::clamp(v.safe_x, 0.f, v.width);
  float top = std::clamp(v.safe_y, 0.f, v.height);
  float right = std::clamp(v.safe_x + v.safe_width, left, v.width);
  float bottom = std::clamp(v.safe_y + v.safe_height, top, v.height);
  // SDL can report no safe rectangle before the native surface is ready.
  if (right <= left || bottom <= top) {
    left = top = 0; right = v.width; bottom = v.height;
  }
  const float gutter = std::min({v.width, v.height, right - left, bottom - top}) * .02f;
  left += gutter; right -= gutter; top += gutter; bottom -= gutter;
  const float scale = std::min({1.f, (bottom - top) / v.height,
                               (right - left) / (v.height * authored_aspect)});
  return {scale, ((left + right) / 2 - v.width / 2) * 480 / v.height,
                 (v.height / 2 - (top + bottom) / 2) * 480 / v.height};
}

// Local guest transform: three padded basis rows followed by translation.
// Preserve depth translation and padding; fit only the UI plane's position.
inline std::array<float, 16> FitPortraitTransform(std::array<float, 16> matrix,
                                                PortraitFit fit) {
  for (size_t row = 0; row < 3; ++row)
    for (size_t column = 0; column < 3; ++column)
      matrix[row * 4 + column] *= fit.scale;
  matrix[12] = fit.X(matrix[12]);
  matrix[14] = fit.Z(matrix[14]);
  return matrix;
}

// The UI thread publishes window facts; the guest draw thread consumes a copy.
inline std::mutex portrait_viewport_mutex;
inline PortraitViewport portrait_viewport;
inline void PublishPortraitViewport(PortraitViewport viewport) {
  std::lock_guard lock(portrait_viewport_mutex);
  portrait_viewport = viewport;
}
inline PortraitViewport CurrentPortraitViewport() {
  std::lock_guard lock(portrait_viewport_mutex);
  return portrait_viewport;
}

}  // namespace rb_blitz::ui
