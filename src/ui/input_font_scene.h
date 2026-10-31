// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rb_blitz::ui::input_font {

// Derive a private copy of the installed Pentatonic font scene. The only edits are 45
// previously empty glyph records, which point into unused atlas space. The atlas pixels are
// supplied at runtime by the texture override; no game art is stored in the project.
bool PrepareScene(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                  std::vector<uint8_t>* original_bc3, std::string* error);
bool PrepareList(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                 std::string* error);
std::shared_ptr<const std::vector<uint8_t>> OriginalBc3();

inline constexpr char kSceneRecord[] = "ui/resource/fonts/gen/pentatonic.milo_xbox";
inline constexpr char kIconSceneRecord[] = "ui/resource/fonts/gen/input_icons.milo_xbox";
inline constexpr char kListRecord[] = "ui/resource/lists/gen/list_options_main.milo_xbox";
inline constexpr uint32_t kAtlasSize = 1024;
inline constexpr uint32_t kIconFirst = 0x80;
inline constexpr uint32_t kIconCount = 45;
inline constexpr uint32_t kIconSize = 64;
inline constexpr uint32_t kIconColumns = 15;
inline constexpr uint32_t kIconColumnStride = 68;
inline constexpr uint32_t kIconInsetX = 2;
inline constexpr uint32_t kIconRowStride = 80;
inline constexpr uint32_t kIconInsetY = 8;
// RndFont samples a fixed cell height; the final glyph float is advance, not height.
inline constexpr float kFontCellWidth = 84.25f;
inline constexpr float kFontCellHeight = 79.5f;
// Align the spare band at every authored mip, including the 16-square final level.
inline constexpr uint32_t kIconTop = 768;
inline constexpr uint32_t kMipCount = 7;
inline constexpr uint32_t GlyphCode(uint32_t index) {
  // Thirty blank C1 records, followed by 15 blank Latin-1 records in the
  // dedicated icon-font copy. No visible retail glyph is replaced.
  constexpr uint32_t extra[] = {
      0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA8, 0xAF, 0xB1,
      0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8};
  if (index >= 30) return extra[index - 30];
  uint32_t code = kIconFirst + index;
  if (code >= 0x8A) ++code;
  if (code >= 0x9A) ++code;
  return code;
}

}  // namespace rb_blitz::ui::input_font
