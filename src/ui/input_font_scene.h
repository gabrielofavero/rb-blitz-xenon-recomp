// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rb_blitz::ui::input_font {

// Derive a private icon-only copy of the installed Pentatonic font scene. Its texture has a
// distinct fingerprint so replacing the complete icon atlas cannot change the retail text font.
// Atlas pixels are supplied at runtime; no game art is stored in the project.
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
inline constexpr uint32_t kIconCount = 180;
inline constexpr uint32_t kIconSize = 64;
inline constexpr uint32_t kIconColumns = 15;
inline constexpr uint32_t kIconColumnStride = 68;
inline constexpr uint32_t kIconInsetX = 2;
inline constexpr uint32_t kIconRowStride = 80;
inline constexpr uint32_t kIconInsetY = 8;
// RndFont samples a fixed cell height; the final glyph float is advance, not height.
inline constexpr float kFontCellWidth = 84.25f;
inline constexpr float kFontCellHeight = 79.5f;
// The private icon atlas begins at its first row at every authored mip.
inline constexpr uint32_t kIconTop = 0;
inline constexpr uint32_t kMipCount = 7;
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
