// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rb_blitz::platform {

// Host image decoder for untrusted PNG/JPEG response bodies. Rejects oversized
// sources before allocating RGBA output. Implemented by stb_image on Unix hosts.
bool DecodeImageRgba(const std::uint8_t* bytes, std::size_t size,
                     std::uint32_t max_dimension, std::vector<std::uint8_t>* rgba,
                     std::uint32_t* width, std::uint32_t* height, std::string* error);

}  // namespace rb_blitz::platform
