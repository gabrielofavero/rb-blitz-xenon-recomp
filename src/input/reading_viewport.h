// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace rb_blitz::input {

struct ReadingGlyph {
  uint32_t mesh, vertices;
  std::array<float, 4> x;
};

// Clip the guest's existing font batches, including alternate bold styles.
// Whole glyphs outside the reading strip collapse during this draw; their
// original coordinates are restored before the next native layout pass.
template <class ReadWord, class ReadFloat>
std::optional<std::vector<ReadingGlyph>> ReadingGlyphsOutside(
    uint32_t label, float bottom, float top, ReadWord word, ReadFloat number) {
  const auto range = [](uint32_t at, uint64_t size) {
    return at >= 0x40000000 && uint64_t(at) + size <= 0x7F000000;
  };
  if (!std::isfinite(bottom) || !std::isfinite(top) || bottom >= top || !range(label, 140)) return {};
  const auto begin = word(label + 132), end = word(label + 136);
  if (end < begin || end - begin > 16 * 12 || (end - begin) % 12 || !range(begin, end - begin)) return {};
  std::vector<ReadingGlyph> clipped;
  for (auto batch = begin; batch < end; batch += 12) {
    const auto glyphs = word(batch + 8);
    if (!glyphs) continue;
    const auto mesh = word(batch + 4);
    if (glyphs > 1024 || !range(mesh, 328)) return {};
    const auto owner = word(mesh + 324);
    if (!range(owner, 264)) return {};
    const auto vertices = word(owner + 256), capacity = word(owner + 260);
    if (glyphs * 4 > capacity || !range(vertices, uint64_t(glyphs) * 4 * 96)) return {};
    for (uint32_t i = 0; i < glyphs; ++i) {
      ReadingGlyph glyph{owner, vertices + i * 4 * 96, {}};
      float low = INFINITY, high = -INFINITY;
      for (uint32_t corner = 0; corner < 4; ++corner) {
        const auto at = glyph.vertices + corner * 96;
        glyph.x[corner] = number(at);
        const float z = number(at + 8);
        if (!std::isfinite(glyph.x[corner]) || !std::isfinite(z)) return {};
        low = std::min(low, z); high = std::max(high, z);
      }
      if (low < bottom - .001f || high > top + .001f) clipped.push_back(glyph);
    }
  }
  return clipped;
}
}  // namespace rb_blitz::input
