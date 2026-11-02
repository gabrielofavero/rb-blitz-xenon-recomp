// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rb_blitz::ui::input_font {

// Derive an icon-only copy of the installed Pentatonic font scene with a
// dedicated 2048-square texture. No game art is stored in the project.
bool PrepareScene(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                  std::vector<uint8_t>* original_bc3, std::string* error);
// A second private font aliases early, proven glyph codes to the reserved
// footer image tiles without changing P9 mapping-row glyphs.
bool PrepareHintScene(const std::vector<uint8_t>& icon_scene, std::vector<uint8_t>* scene,
                      std::string* error);
bool PrepareList(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                 std::string* error);
bool PrepareInlineHelp(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                       std::string* error);
std::shared_ptr<const std::vector<uint8_t>> OriginalBc3();
std::shared_ptr<const std::vector<uint8_t>> PrivateBc3();

inline constexpr char kSceneRecord[] = "ui/resource/fonts/gen/pentatonic.milo_xbox";
inline constexpr char kIconSceneRecord[] = "ui/resource/fonts/gen/input_icons.milo_xbox";
inline constexpr char kHintSceneRecord[] = "ui/resource/fonts/gen/inline_help_icons.milo_xbox";
inline constexpr char kListRecord[] = "ui/resource/lists/gen/list_options_main.milo_xbox";
inline constexpr char kInlineHelpRecord[] = "ui/resource/inline_help/gen/default.milo_xbox";
inline constexpr uint32_t kRetailAtlasSize = 1024;
inline constexpr uint32_t kAtlasSize = 2048;
inline constexpr uint32_t kIconFirst = 0x80;
inline constexpr uint32_t kIconCount = 180;
inline constexpr uint32_t kHintCount = 15;
// Image slots 101..115 belong to InlineHelp. Its own font scene aliases early
// private glyph codes to these tiles, leaving P9's row glyphs unchanged.
inline constexpr uint32_t kHintFirst = 101;
inline constexpr uint32_t kRowIconCount = kIconCount - kHintCount;
inline constexpr uint32_t RowGlyphIndex(uint32_t index) {
  return index < kHintFirst ? index : index + kHintCount;
}
inline constexpr uint32_t kIconSize = 64;
inline constexpr uint32_t kIconColumns = 30;
inline constexpr uint32_t kIconColumnStride = 68;
inline constexpr uint32_t kIconInsetX = 2;
inline constexpr uint32_t kIconRowStride = 68;
inline constexpr uint32_t kIconInsetY = 2;
inline constexpr uint32_t kHintIconSize = 96;
inline constexpr uint32_t kHintCellSize = 100;
inline constexpr uint32_t kHintInset = kIconInsetY;
// Coordinates are exactly twice the proven 1024-square retail-band layout.
// If the guest aliases the private texture to retail, the same UVs still show
// the 32/48-pixel icons in the retail fallback band.
// RndFont samples the cell height; the final glyph float is advance, not height.
// RndFont samples the cell height; the final glyph float is advance, not height.
inline constexpr float kOriginalFontCellWidth = 84.25f;
inline constexpr float kOriginalFontCellHeight = 79.5f;
inline constexpr float kFontCellWidth = 68.0f;
inline constexpr float kFontCellHeight = 68.0f;
inline constexpr float kHintFontCellWidth = 100.0f;
inline constexpr float kHintFontCellHeight = 100.0f;
inline constexpr uint32_t kIconTop = 1536;
inline constexpr uint32_t kMipCount = 7;
inline constexpr bool IsHintTile(uint32_t index) {
  return index >= kHintFirst && index < kHintFirst + kHintCount;
}
inline constexpr uint32_t IconSize(uint32_t index) {
  return IsHintTile(index) ? kHintIconSize : kIconSize;
}
inline constexpr uint32_t IconX(uint32_t index) {
  if (IsHintTile(index)) return (index - kHintFirst) * kHintCellSize + kHintInset;
  const uint32_t row_index = index < kHintFirst ? index : index - kHintCount;
  return (row_index % kIconColumns) * kIconColumnStride + kIconInsetX;
}
inline constexpr uint32_t IconY(uint32_t index) {
  if (IsHintTile(index)) return kIconTop + kHintInset;
  const uint32_t row_index = index < kHintFirst ? index : index - kHintCount;
  return kIconTop + kHintCellSize +
         (row_index / kIconColumns) * kIconRowStride + kIconInsetY;
}
static_assert(kHintCount * kHintCellSize <= kAtlasSize);
static_assert(kIconColumns * kIconColumnStride <= kAtlasSize);
static_assert(kIconTop + kHintCellSize +
              ((kRowIconCount + kIconColumns - 1) / kIconColumns) * kIconRowStride <= kAtlasSize);
inline constexpr uint32_t GlyphCode(uint32_t index) {
  // Keep the accepted first 45 codes stable. The private icon font may also use
  // occupied retail glyph records because ordinary labels use the original font.
  constexpr uint32_t extra[] = {
      0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA8, 0xAF, 0xB1,
      0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8};
  if (index < 30) {
    uint32_t code = kIconFirst + index;
    if (code >= 0x8A) ++code;
    if (code >= 0x9A) ++code;
    return code;
  }
  if (index < 45) return extra[index - 30];
  uint32_t remaining = index - 45;
  for (uint32_t code = 0xB9; code < 0x100; ++code) {
    if (remaining-- == 0) return code;
  }
  constexpr char ascii[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!?";
  for (uint32_t i = 0; i < sizeof(ascii) - 1; ++i) {
    if (remaining-- == 0) return ascii[i];
  }
  return 0;
}

}  // namespace rb_blitz::ui::input_font
