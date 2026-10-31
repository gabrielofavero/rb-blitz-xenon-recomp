// SPDX-License-Identifier: GPL-2.0-only
#include "assets/input_font_atlas.h"

#include <algorithm>

#include "assets/texture_codec.h"
#include "ui/input_font_scene.h"

namespace rb_blitz::assets::input_font_atlas {
namespace {
using controller_schemes::Image;
using input::icons::Art;

Image CropRetail(const std::vector<uint8_t>& rgba, const std::string& slot) {
  for (const auto& entry : controller_schemes::ButtonSlots()) {
    if (slot != entry.id) continue;
    const auto& r = entry.rect;
    // Glyph UV bounds omit a few antialiased pixels at the lower edge of the
    // retail A/B circles. Read their full measured 85-square atlas cell.
    constexpr uint32_t cell = 85;
    const uint32_t x = (r.x / cell) * cell, y = (r.y / cell) * cell;
    Image image{cell, cell, std::vector<uint8_t>(cell * cell * 4)};
    for (uint32_t row = 0; row < cell; ++row)
      std::copy_n(rgba.data() + (size_t(y + row) * 512 + x) * 4,
                  size_t(cell) * 4, image.rgba.data() + size_t(row) * cell * 4);
    return image;
  }
  return {};
}
Image ImageFor(const input::icons::Icon& icon,
               const std::vector<uint8_t>& retail_rgba,
               const controller_schemes::Scheme* scheme) {
  if (icon.art == Art::kPackageImage)
    return {icon.width, icon.height, icon.rgba};
  if (icon.art == Art::kSchemeSlot) {
    if (scheme) {
      const auto found = scheme->buttons.find(icon.slot);
      if (found != scheme->buttons.end()) return found->second;
    }
    return CropRetail(retail_rgba, icon.slot);
  }
  return {icon.width, icon.height, icon.rgba};
}
}  // namespace

bool Compose(const std::vector<uint8_t>& original_font,
             const std::vector<uint8_t>& original_buttons,
             const controller_schemes::Scheme* scheme,
             const std::vector<input::icons::Icon>& icons,
             std::vector<std::vector<uint8_t>>* levels) {
  namespace font = ui::input_font;
  if (!levels || icons.empty() || icons.size() > font::kIconCount ||
      original_buttons.size() != 512u * 512u) return false;
  size_t total = 0;
  for (uint32_t mip = 0; mip < font::kMipCount; ++mip)
    total += size_t(font::kAtlasSize >> mip) * (font::kAtlasSize >> mip);
  if (original_font.size() != total) return false;

  std::vector<uint8_t> retail_rgba(512u * 512u * 4);
  assets::DecodeDxt5(original_buttons.data(), 512, 512, retail_rgba.data());
  std::vector<Image> fitted;
  fitted.reserve(icons.size());
  for (const auto& icon : icons) {
    Image image = ImageFor(icon, retail_rgba, scheme);
    if (!image.width || !image.height || image.rgba.size() != size_t(image.width) * image.height * 4)
      return false;
    fitted.push_back(controller_schemes::Fit(image, font::kIconSize, font::kIconSize));
  }

  // Downsample the private icon atlas as one image. Fractional mip coordinates
  // must follow the authored UVs, rather than rounding each icon's origin.
  constexpr uint32_t band_height = font::kAtlasSize;
  std::vector<uint8_t> band(size_t(font::kAtlasSize) * band_height * 4);
  for (size_t i = 0; i < fitted.size(); ++i) {
    const uint32_t x = uint32_t(i % font::kIconColumns) * font::kIconColumnStride +
                       font::kIconInsetX;
    const uint32_t y = uint32_t(i / font::kIconColumns) * font::kIconRowStride +
                       font::kIconInsetY;
    for (uint32_t row = 0; row < font::kIconSize; ++row)
      std::copy_n(fitted[i].rgba.data() + size_t(row) * font::kIconSize * 4,
                  size_t(font::kIconSize) * 4,
                  band.data() + (size_t(y + row) * font::kAtlasSize + x) * 4);
  }

  levels->clear();
  for (uint32_t mip = 0; mip < font::kMipCount; ++mip) {
    const uint32_t dim = font::kAtlasSize >> mip;
    std::vector<uint8_t> resized(size_t(dim) * dim * 4);
    if (mip == 0) resized = band;
    else assets::ResizeRgba(band.data(), font::kAtlasSize, band_height,
                            resized.data(), dim, dim);
    std::vector<uint8_t> level;
    assets::EncodeDxt5(resized.data(), dim, dim, &level);
    levels->push_back(std::move(level));
  }
  return true;
}
}  // namespace rb_blitz::assets::input_font_atlas
