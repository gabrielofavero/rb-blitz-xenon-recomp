// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rb_blitz::ui::input_font::detail {

// Flow objects serialize an inline directory after their base-object delimiter.
// Its directory and child delimiters belong to that Flow, not its containing scene.
inline std::vector<size_t> MiloObjectBoundaries(std::span<const uint8_t> bytes,
                                               size_t begin, uint32_t count) {
  const auto word = [&](size_t at) {
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 |
           uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
  };
  std::vector<size_t> markers;
  for (size_t at = begin; at + 4 <= bytes.size(); ++at)
    if (word(at) == 0xADDEADDE) markers.push_back(at);

  // Work backwards so nested Flows have already been folded into their parent.
  for (size_t i = markers.size(); i-- > 0;) {
    size_t at = markers[i] + 4;
    if (at + 12 > bytes.size() || word(at) != 32 || word(at + 4) != 4 ||
        std::string_view(reinterpret_cast<const char*>(bytes.data() + at + 8), 4) != "Flow")
      continue;
    at += 12;
    if (at + 4 > bytes.size()) return {};
    const auto name_size = word(at);
    at += 4;
    if (name_size > bytes.size() - at || bytes.size() - at - name_size < 13) return {};
    at += name_size + 9;
    const auto children = word(at);
    if (children >= markers.size() - i - 1) return {};
    markers.erase(markers.begin() + i, markers.begin() + i + children + 1);
  }
  if (markers.size() < size_t(count) + 1) return {};
  markers.erase(markers.begin(), markers.end() - count - 1);
  return markers;
}

}  // namespace rb_blitz::ui::input_font::detail
