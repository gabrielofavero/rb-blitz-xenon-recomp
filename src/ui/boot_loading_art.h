// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rb_blitz::ui::boot_loading_art {

// 60 phases of a 128x128 RGBA8 circular autosave icon reconstructed from the retail
// spinner's wrapping texture strip. The player's installed ARK supplies the
// artwork; no game artwork is distributed with this project.
inline constexpr uint32_t kFrameSize = 128;
inline constexpr size_t kAnimationFrameCount = 60;
using BootLoadingFrames = std::array<std::vector<uint8_t>, kAnimationFrameCount>;

bool LoadBootLoadingFrames(const std::filesystem::path& game_data_root,
                           BootLoadingFrames* frames, std::string* error);

}  // namespace rb_blitz::ui::boot_loading_art
