// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace rb_blitz::input {
struct InlineHelpEntry { uint32_t label, action; };

// SetWorldXfm writes the draw matrix at +72 and clears the dirty byte at +189.
// WorldXfm is an evaluator: calling it on a clean object rebuilds from its local
// transform and discards temporary placement assigned by InlineHelp::DrawShowing.
// Match the guest draw path by using its cached matrix while it is clean.
template <typename Read8, typename Evaluate>
uint32_t WorldTransformForDraw(uint32_t transformable, Read8 read8, Evaluate evaluate) {
  return read8(transformable + 189) ? evaluate(transformable) : transformable + 72;
}

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

// Let the guest draw each live label at a separate position without cloning the
// widget's state. Restore both vector ranges before any subsequent guest update.
template <typename Read32, typename Write32, typename Draw>
bool DrawInlineHelpLabelsSeparately(uint32_t object, Read32 read32, Write32 write32, Draw draw) {
  const auto entries = ReadInlineHelpEntries(object, read32);
  if (entries.empty()) return false;
  const auto begin = read32(object + 80), end = read32(object + 84);
  const auto labels = read32(object + 92), labels_end = read32(object + 96);
  if (entries.size() != (end - begin) / 28) return false;
  struct Restore {
    Write32& write;
    uint32_t object, begin, end, labels, labels_end;
    ~Restore() {
      write(object + 80, begin); write(object + 84, end);
      write(object + 92, labels); write(object + 96, labels_end);
    }
  } restore{write32, object, begin, end, labels, labels_end};
  for (size_t i = 0; i < entries.size(); ++i) {
    write32(object + 80, begin + uint32_t(i) * 28);
    write32(object + 84, begin + uint32_t(i + 1) * 28);
    write32(object + 92, labels + uint32_t(i) * 4);
    write32(object + 96, labels + uint32_t(i + 1) * 4);
    draw(i);
  }
  return true;
}
}  // namespace rb_blitz::input
