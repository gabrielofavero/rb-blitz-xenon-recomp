// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rb_blitz::ui::boot_loading_art {

// 60 phases of the installed spinner's authored wedge mesh and plate material.
// No game artwork is distributed with this project.
inline constexpr uint32_t kFrameSize = 128;
inline constexpr size_t kAnimationFrameCount = 60;
using BootLoadingFrames = std::array<std::vector<uint8_t>, kAnimationFrameCount>;

bool LoadBootLoadingFrames(const std::filesystem::path& game_data_root,
                           BootLoadingFrames* frames, std::string* error);

}  // namespace rb_blitz::ui::boot_loading_art
