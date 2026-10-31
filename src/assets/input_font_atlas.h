// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <vector>

#include "input/input_icons.h"
#include "controller_schemes/schemes.h"

namespace rb_blitz::assets::input_font_atlas {

// Replace only the spare glyph tiles in the player's original font mip chain. Controller
// images come from the selected package or the player's original button sheet; host keycaps
// come from P8. Untouched font BC3 blocks are copied byte for byte.
bool Compose(const std::vector<uint8_t>& original_font,
             const std::vector<uint8_t>& original_buttons,
             const controller_schemes::Scheme* scheme,
             const std::vector<input::icons::Icon>& icons,
             std::vector<std::vector<uint8_t>>* levels);

}  // namespace rb_blitz::assets::input_font_atlas
