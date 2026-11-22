// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace rb_blitz::input {
struct InlineHelpEntry { uint32_t label, action, record = 0; };

// The source Symbol is at +4; +16 points to its localized display string.
// Match the Symbol so navigation policy does not depend on the current language.
template <typename Read32, typename Text>
std::string_view InlineHelpCaptionToken(const InlineHelpEntry& entry, Read32 read32, Text text) {
  return entry.record ? text(read32(entry.record + 4)) : std::string_view{};
}

inline constexpr std::string_view CalibrationFooterCaption(bool calibration, std::string_view caption) {
  return calibration && (caption == "select" || caption == "continue")
      ? "rbbz_calibration_next" : caption;
}

inline bool CalibrationRetryCaption(std::string_view caption) {
  return caption == "retry" || caption == "rbbz_loading_retry";
}

struct InlineHelpPlacement { size_t index, count; };

// Failure is a separate guest panel, so its single Retry label must reserve the
// upper slot even while the underlying panel still draws Back and Next.
inline InlineHelpPlacement CalibrationFooterPlacement(bool calibration, std::string_view caption,
                                                      size_t index, size_t count) {
  return calibration && CalibrationRetryCaption(caption) ? InlineHelpPlacement{2, 3}
                                                        : InlineHelpPlacement{index, count};
}

// Keep Back and Next on the lower row, followed by Retry. Shared Confirm actions
// do not make the labels interchangeable; each retains its native record.
template <typename Caption>
std::vector<InlineHelpEntry> CalibrationFooterEntries(
    const std::vector<InlineHelpEntry>& entries, Caption caption) {
  auto visible = entries;
  const auto rank = [&](const auto& entry) {
    return CalibrationRetryCaption(caption(entry)) ? 2 : entry.action == 2 ? 0 : 1;
  };
  std::stable_sort(visible.begin(), visible.end(),
      [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
  return visible;
}

// RndText batches glyph quads by font in 12-byte records at UILabel +132.
// Use each batch's live glyph count, not the mesh's allocated vertex capacity:
// touch removes the icon batch and captions can shrink when actions change.
template <typename Read32, typename ReadFloat>
std::optional<std::array<float, 4>> InlineHelpGlyphBounds(
    uint32_t label, Read32 read32, ReadFloat read_float) {
  const auto range = [](uint32_t address, uint64_t size) {
    return address >= 0x40000000 && uint64_t(address) + size <= 0x7F000000;
  };
  if (!range(label, 140)) return {};
  const auto begin = read32(label + 132), end = read32(label + 136);
  if (end < begin || end - begin > 16 * 12 || (end - begin) % 12 ||
      !range(begin, end - begin)) return {};
  const float inf = std::numeric_limits<float>::infinity();
  std::array<float, 4> bounds{inf, inf, -inf, -inf};
  for (auto batch = begin; batch < end; batch += 12) {
    const auto glyphs = read32(batch + 8);
    if (!glyphs) continue;
    const auto mesh = read32(batch + 4);
    if (glyphs > 1024 || !range(mesh, 328)) return {};
    const auto owner = read32(mesh + 324);
    if (!range(owner, 264)) return {};
    const auto vertices = read32(owner + 256), capacity = read32(owner + 260);
    const auto count = glyphs * 4;
    if (count > capacity || !range(vertices, uint64_t(count) * 96)) return {};
    for (uint32_t i = 0; i < count; i += 4) {
      std::array<float, 4> quad{inf, inf, -inf, -inf};
      for (uint32_t corner = 0; corner < 4; ++corner) {
        const auto vertex = vertices + (i + corner) * 96;
        const float x = read_float(vertex), z = read_float(vertex + 8);
        if (!std::isfinite(x) || !std::isfinite(z)) return {};
        quad[0] = std::min(quad[0], x); quad[2] = std::max(quad[2], x);
        quad[1] = std::min(quad[1], z); quad[3] = std::max(quad[3], z);
      }
      // Spaces advance the pen without drawing; RndText zeros their spare quads.
      if (!(quad[2] > quad[0]) || !(quad[3] > quad[1])) continue;
      bounds[0] = std::min(bounds[0], quad[0]); bounds[2] = std::max(bounds[2], quad[2]);
      bounds[1] = std::min(bounds[1], quad[1]); bounds[3] = std::max(bounds[3], quad[3]);
    }
  }
  if (!(bounds[2] > bounds[0]) || !(bounds[3] > bounds[1])) return {};
  return bounds;
}

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
    if (range(label, 4)) entries.push_back({label, read32(begin + i * 28), begin + i * 28});
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
