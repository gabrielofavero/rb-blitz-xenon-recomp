// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <vector>

#include "input/input_icons.h"
#include "controller_schemes/schemes.h"

namespace rb_blitz::assets::input_font_atlas {

// Compose icons in the dedicated 2048-square font texture. The extra eighth
// level supplies the 1024-square retail fallback without another resample.
bool Compose(const std::vector<uint8_t>& original_font,
             const std::vector<uint8_t>& original_buttons,
             const controller_schemes::Scheme* scheme,
             const std::vector<input::icons::Icon>& icons,
             std::vector<std::vector<uint8_t>>* levels);

// Copy only the icon band into the original retail font mip chain. All retail
// text blocks remain byte-for-byte intact if the guest shares that texture.
bool ComposeRetail(const std::vector<uint8_t>& original_font,
                   const std::vector<std::vector<uint8_t>>& private_levels,
                   std::vector<std::vector<uint8_t>>* levels);

}  // namespace rb_blitz::assets::input_font_atlas
