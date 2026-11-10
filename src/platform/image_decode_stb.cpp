// SPDX-License-Identifier: GPL-2.0-only
#include "platform/image_decode.h"

#if defined(_WIN32)
#error "The stb image adapter is for non-Windows builds only"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#include <stb_image.h>

#include <limits>

namespace rb_blitz::platform {

bool DecodeImageRgba(const std::uint8_t* bytes, const std::size_t size,
                     const std::uint32_t max_dimension, std::vector<std::uint8_t>* rgba,
                     std::uint32_t* width, std::uint32_t* height, std::string* error) {
  if (!bytes || !rgba || !width || !height || !error || size == 0 ||
      size > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      max_dimension == 0 || max_dimension > 16384) {
    if (error) *error = "invalid image input";
    return false;
  }
  int source_width = 0;
  int source_height = 0;
  int channels = 0;
  if (!stbi_info_from_memory(bytes, static_cast<int>(size), &source_width,
                             &source_height, &channels) || source_width <= 0 ||
      source_height <= 0 || source_width > static_cast<int>(max_dimension) ||
      source_height > static_cast<int>(max_dimension)) {
    *error = "image dimensions are invalid or exceed the limit";
    return false;
  }
  stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size),
                                          &source_width, &source_height, &channels, 4);
  if (!pixels) {
    *error = "cannot decode PNG or JPEG";
    return false;
  }
  const std::size_t length = static_cast<std::size_t>(source_width) * source_height * 4;
  rgba->assign(pixels, pixels + length);
  stbi_image_free(pixels);
  *width = static_cast<std::uint32_t>(source_width);
  *height = static_cast<std::uint32_t>(source_height);
  error->clear();
  return true;
}

}  // namespace rb_blitz::platform
