// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <vector>

namespace rb_blitz::input {
struct InlineHelpEntry { uint32_t label, action; };

// Guest InlineHelp has parallel action records (28 bytes) and UILabel pointers.
// Keep the full label pointer from the footer; a nested DrawShowing receiver can
// be a base-class pointer instead. read32 supplies the guest big-endian reader.
template <typename Read32>
std::vector<InlineHelpEntry> ReadInlineHelpEntries(uint32_t object, Read32 read32) {
  const auto range = [](uint32_t address, uint32_t size) {
    return address >= 0x40000000 && uint64_t(address) + size <= 0x7F000000;
  };
  if (!range(object, 100)) return {};
  const uint32_t begin = read32(object + 80), end = read32(object + 84);
  const uint32_t labels = read32(object + 92), labels_end = read32(object + 96);
  if (end < begin || end - begin > 15 * 28 || (end - begin) % 28 ||
      labels_end < labels || labels_end - labels != (end - begin) / 7 ||
      !range(begin, end - begin) || !range(labels, labels_end - labels)) return {};
  std::vector<InlineHelpEntry> entries;
  for (uint32_t i = 0; i < (end - begin) / 28; ++i) {
    const uint32_t label = read32(labels + i * 4);
    if (range(label, 4)) entries.push_back({label, read32(begin + i * 28)});
  }
  return entries;
}
}  // namespace rb_blitz::input
