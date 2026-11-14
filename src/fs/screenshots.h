// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <cstdint>

namespace rb_blitz::screenshots {

// The normal user-data root is the Blitz folder itself. Older installations
// selected a Saved Games child instead; keep Pictures alongside that child.
std::filesystem::path PicturesDirectory(const std::filesystem::path& user_data_root);

// Presenter pixels are top-down R8 G8 B8 X8; the final row may omit stride padding.
// Writes a standard, uncompressed 24-bit BMP, choosing a new numbered filename
// exclusively so existing screenshots are never replaced, even by concurrent callers.
bool SaveBitmap(const std::filesystem::path& directory, std::string_view guest_prefix,
                uint32_t width, uint32_t height, size_t stride,
                std::span<const uint8_t> pixels, std::filesystem::path& saved_path,
                std::string& error);

}  // namespace rb_blitz::screenshots
