// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace rb_blitz::ui::input_font {

// Derive an icon-only copy of the installed Pentatonic font scene. Its glyph
// table is private; the texture still shares the retail 1024-square identity.
// No game art is stored in the project.
bool PrepareScene(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                  std::vector<uint8_t>* original_bc3, std::string* error);
// Read the retail button sheet from the installed game scene before any font
// draw. Some screens use only the private font and never upload buttons.tex.
bool PrepareOriginalButtons(const std::vector<uint8_t>& source, std::string* error);
// Cache the four original PS pad mip chains from the installed Xbox 360 scene.
// Used only in memory when a layout selects retail_pad_family = "ps".
using RetailPsPads = std::array<std::vector<std::vector<uint8_t>>, 4>;
bool PrepareRetailPsPads(const std::vector<uint8_t>& source, std::string* error);
std::shared_ptr<const RetailPsPads> OriginalPsPadsBc3();
inline constexpr char kControllerSceneRecord[] =
    "ui/controller_config/gen/controller_config.milo_xbox";
// A second private font aliases early, proven glyph codes to the reserved
// footer image tiles without changing P9 mapping-row glyphs.
bool PrepareHintScene(const std::vector<uint8_t>& icon_scene, std::vector<uint8_t>* scene,
                      std::string* error);
bool PrepareList(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                 std::string* error);
bool PrepareInlineHelp(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                       std::string* error);
// Point the loading begin prompt, rotating tip and Power-up descriptions at
// the action-hint font, leaving unrelated label styles on their retail fonts.
bool PrepareLoading(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                    std::string* error);
// Point the splash start label at the action-hint font.
bool PrepareSplash(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                   std::string* error);
bool PrepareCalibration(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                        std::string* error);
bool PrepareHudHints(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                     std::string* error);
bool PreparePowerupSelect(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                          std::string* error);
bool PrepareErrorReturn(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                        std::string* error);
bool PrepareAccountLink(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                        std::string* error);
std::shared_ptr<const std::vector<uint8_t>> OriginalBc3();
std::shared_ptr<const std::vector<uint8_t>> PrivateBc3();
std::shared_ptr<const std::vector<uint8_t>> OriginalButtonsBc3();

inline constexpr char kSceneRecord[] = "ui/resource/fonts/gen/pentatonic.milo_xbox";
inline constexpr char kButtonSceneRecord[] = "ui/resource/fonts/gen/buttons.milo_xbox";
inline constexpr char kIconSceneRecord[] = "ui/resource/fonts/gen/input_icons.milo_xbox";
inline constexpr char kHintSceneRecord[] = "ui/resource/fonts/gen/inline_help_icons.milo_xbox";
inline constexpr char kListRecord[] = "ui/resource/lists/gen/list_options_main.milo_xbox";
inline constexpr char kInlineHelpRecord[] = "ui/resource/inline_help/gen/default.milo_xbox";
inline constexpr char kLoadingRecord[] = "ui/loading/gen/loading.milo_xbox";
inline constexpr char kSplashRecord[] = "ui/splash/gen/splash.milo_xbox";
inline constexpr char kCalibrationRecord[] = "ui/calibration/gen/cal_auto.milo_xbox";
inline constexpr char kHudRecord[] = "track/gen/hud.milo_xbox";
inline constexpr char kPowerupSelectRecord[] = "ui/powerup_select/gen/powerup_select.milo_xbox";
inline constexpr char kErrorReturnRecord[] = "ui/error/gen/error_general.milo_xbox";
inline constexpr char kAccountLinkRecord[] = "ui/rbworld_link/gen/rbworld_link.milo_xbox";
inline constexpr uint32_t kRetailAtlasSize = 1024;
inline constexpr uint32_t kAtlasSize = 1024;
static_assert(kAtlasSize == kRetailAtlasSize);
inline constexpr uint32_t kIconFirst = 0x80;
inline constexpr uint32_t kIconCount = 180;
inline constexpr uint32_t kHintCount = 15;
// Image slots 101..115 belong to action hints. The hint font aliases early
// private glyph codes to these tiles, leaving P9's row glyphs unchanged.
inline constexpr uint32_t kHintFirst = 101;
inline constexpr uint32_t kInterfaceStartHint = 4;
// The loading screen's rotating power-up tip is a gameplay action, unlike its
// begin-song prompt and the footer actions. It uses an unused hint tile.
inline constexpr uint32_t kGameplayPowerupHint = 6;
inline constexpr uint32_t kGameplayFirstNoteHint = 7;
inline constexpr uint32_t kGameplaySecondNoteHint = 8;
inline constexpr uint32_t kGameplayMoveRightHint = 9;
inline constexpr uint32_t kGameplayMoveLeftHint = 10;
inline constexpr uint32_t kGameplayPauseHint = 12;
inline constexpr uint32_t kRowIconCount = kIconCount - kHintCount;
inline constexpr uint32_t RowGlyphIndex(uint32_t index) {
  return index < kHintFirst ? index : index + kHintCount;
}
inline constexpr uint32_t kIconSize = 32;
inline constexpr uint32_t kIconColumns = 28;
inline constexpr uint32_t kIconColumnStride = 36;
inline constexpr uint32_t kIconInsetX = 2;
inline constexpr uint32_t kIconRowStride = 36;
inline constexpr uint32_t kIconInsetY = 2;
// RndFont samples the cell height; the final glyph float is advance, not height.
inline constexpr float kOriginalFontCellWidth = 84.25f;
inline constexpr float kOriginalFontCellHeight = 79.5f;
inline constexpr float kFontCellWidth = 36.0f;
inline constexpr float kFontCellHeight = 36.0f;
inline constexpr uint32_t kIconTop = 768;
inline constexpr uint32_t kMipCount = 7;
inline constexpr uint32_t kPrivateIconSize = 64;
inline constexpr uint32_t kPrivateCellSize = 68;
inline constexpr uint32_t kPrivateColumns = 15;
inline constexpr uint32_t kPrivateInset = 2;
inline constexpr float kPrivateFontCellWidth = 68.0f;
inline constexpr float kPrivateFontCellHeight = 68.0f;
inline constexpr uint32_t PrivateIconX(uint32_t index) {
  return (index % kPrivateColumns) * kPrivateCellSize + kPrivateInset;
}
inline constexpr uint32_t PrivateIconY(uint32_t index) {
  return (index / kPrivateColumns) * kPrivateCellSize + kPrivateInset;
}
static_assert(kPrivateColumns * kPrivateCellSize <= kAtlasSize);
static_assert(((kIconCount + kPrivateColumns - 1) / kPrivateColumns) * kPrivateCellSize <=
              kAtlasSize);
inline constexpr uint32_t IconSize(uint32_t) { return kIconSize; }
inline constexpr uint32_t IconX(uint32_t index) {
  return (index % kIconColumns) * kIconColumnStride + kIconInsetX;
}
inline constexpr uint32_t IconY(uint32_t index) {
  return kIconTop + (index / kIconColumns) * kIconRowStride + kIconInsetY;
}
static_assert(kIconColumns * kIconColumnStride <= kAtlasSize);
static_assert(kIconTop + ((kIconCount + kIconColumns - 1) / kIconColumns) * kIconRowStride <=
              kAtlasSize);
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
static_assert(GlyphCode(kGameplayPowerupHint) == 0x86);
static_assert(GlyphCode(kInterfaceStartHint) == 0x84);
static_assert(GlyphCode(kGameplayMoveLeftHint) == 0x8B);
static_assert(GlyphCode(kGameplayPauseHint) == 0x8D);

}  // namespace rb_blitz::ui::input_font
