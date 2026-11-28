// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace rb_blitz::input {

struct GlyphEdgeVertex {
  float x, u;
};

// RndText emits each quad in top-left, bottom-left, bottom-right, top-right
// order. Clip its horizontal edges and interpolate the atlas coordinate along
// each edge so the cut glyph keeps its original proportions.
inline std::array<GlyphEdgeVertex, 4> ClipMenuGlyph(
    const std::array<GlyphEdgeVertex, 4>& glyph, float shift, float left, float right) {
  auto clipped = glyph;
  for (std::size_t corner = 0; corner < glyph.size(); ++corner) {
    const auto& from = glyph[corner];
    const auto& to = glyph[3 - corner];
    const float x = from.x + shift;
    clipped[corner].x = std::clamp(x, left, right);
    if (x != clipped[corner].x && from.x != to.x) {
      const float t = std::clamp((clipped[corner].x - x) / (to.x - from.x), 0.f, 1.f);
      clipped[corner].u = std::lerp(from.u, to.u, t);
    }
  }
  return clipped;
}

}  // namespace rb_blitz::input
