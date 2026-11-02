// SPDX-License-Identifier: GPL-2.0-only
#include "assets/input_font_atlas.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "assets/texture_codec.h"
#include "ui/input_font_scene.h"

namespace rb_blitz::assets::input_font_atlas {
namespace {
using controller_schemes::Image;
using input::icons::Art;

double Lanczos(double distance) {
  distance = std::abs(distance);
  if (distance >= 3.0) return 0.0;
  if (distance < 1e-8) return 1.0;
  const double angle = std::numbers::pi * distance;
  return 3.0 * std::sin(angle) * std::sin(angle / 3.0) / (angle * angle);
}

// The font packs detailed source PNGs into small glyphs. A box filter loses
// their thin borders and legends; a premultiplied Lanczos reduction retains
// detail without dark RGB from transparent source pixels bleeding into edges.
void ResizeIcon(const Image& source, Image* resized) {
  const double sx = double(source.width) / resized->width;
  const double sy = double(source.height) / resized->height;
  const double rx = 3.0 * std::max(1.0, sx);
  const double ry = 3.0 * std::max(1.0, sy);
  std::vector<double> horizontal(size_t(resized->width) * source.height * 4);
  for (uint32_t y = 0; y < source.height; ++y) {
    for (uint32_t x = 0; x < resized->width; ++x) {
      const double center = (x + 0.5) * sx - 0.5;
      double sum[4] = {}, weight_sum = 0;
      const int first = std::max(0, int(std::ceil(center - rx)));
      const int last = std::min(int(source.width) - 1, int(std::floor(center + rx)));
      for (int ix = first; ix <= last; ++ix) {
        const double weight = Lanczos((ix - center) / std::max(1.0, sx));
        const auto* pixel = source.rgba.data() + (size_t(y) * source.width + ix) * 4;
        const double alpha = pixel[3] / 255.0;
        for (int c = 0; c < 3; ++c) sum[c] += weight * pixel[c] * alpha;
        sum[3] += weight * alpha;
        weight_sum += weight;
      }
      auto* output = horizontal.data() + (size_t(y) * resized->width + x) * 4;
      for (int c = 0; c < 4; ++c) output[c] = sum[c] / weight_sum;
    }
  }
  for (uint32_t y = 0; y < resized->height; ++y) {
    const double center = (y + 0.5) * sy - 0.5;
    const int first = std::max(0, int(std::ceil(center - ry)));
    const int last = std::min(int(source.height) - 1, int(std::floor(center + ry)));
    for (uint32_t x = 0; x < resized->width; ++x) {
      double sum[4] = {}, weight_sum = 0;
      for (int iy = first; iy <= last; ++iy) {
        const double weight = Lanczos((iy - center) / std::max(1.0, sy));
        const auto* pixel = horizontal.data() + (size_t(iy) * resized->width + x) * 4;
        for (int c = 0; c < 4; ++c) sum[c] += weight * pixel[c];
        weight_sum += weight;
      }
      auto* output = resized->rgba.data() + (size_t(y) * resized->width + x) * 4;
      const double alpha = std::clamp(sum[3] / weight_sum, 0.0, 1.0);
      output[3] = uint8_t(std::lround(alpha * 255.0));
      for (int c = 0; c < 3; ++c)
        output[c] = alpha > 1e-6 ? uint8_t(std::lround(std::clamp(
            sum[c] / weight_sum / alpha, 0.0, 255.0))) : 0;
    }
  }
}

Image FitIcon(const Image& source, uint32_t size) {
  const double scale = std::min(double(size) / source.width, double(size) / source.height);
  const uint32_t width = std::clamp(uint32_t(source.width * scale + 0.5), 1u, size);
  const uint32_t height = std::clamp(uint32_t(source.height * scale + 0.5), 1u, size);
  Image resized{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
  ResizeIcon(source, &resized);
  Image out{size, size, std::vector<uint8_t>(size_t(size) * size * 4)};
  const uint32_t left = (size - width) / 2, top = (size - height) / 2;
  for (uint32_t row = 0; row < height; ++row)
    std::copy_n(resized.rgba.data() + size_t(row) * width * 4, size_t(width) * 4,
                out.rgba.data() + (size_t(top + row) * size + left) * 4);
  return out;
}

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
    total += size_t(font::kGuestAtlasSize >> mip) * (font::kGuestAtlasSize >> mip);
  if (original_font.size() != total) return false;

  std::vector<uint8_t> retail_rgba(512u * 512u * 4);
  assets::DecodeDxt5(original_buttons.data(), 512, 512, retail_rgba.data());
  std::vector<Image> fitted;
  fitted.reserve(icons.size());
  for (size_t index = 0; index < icons.size(); ++index) {
    const auto& icon = icons[index];
    Image image = ImageFor(icon, retail_rgba, scheme);
    if (!image.width || !image.height || image.rgba.size() != size_t(image.width) * image.height * 4)
      return false;
    fitted.push_back(FitIcon(image, font::IconSize(static_cast<uint32_t>(index))));
  }

  // The private font has its own texture, so all of its pixels are available
  // for icons. Downsample the atlas as one image to keep mip UVs aligned.
  std::vector<uint8_t> atlas(size_t(font::kAtlasSize) * font::kAtlasSize * 4);
  for (size_t i = 0; i < fitted.size(); ++i) {
    const uint32_t x = font::IconX(static_cast<uint32_t>(i));
    const uint32_t y = font::IconY(static_cast<uint32_t>(i));
    const uint32_t size = font::IconSize(static_cast<uint32_t>(i));
    for (uint32_t row = 0; row < size; ++row)
      std::copy_n(fitted[i].rgba.data() + size_t(row) * size * 4,
                  size_t(size) * 4,
                  atlas.data() + (size_t(y + row) * font::kAtlasSize + x) * 4);
  }

  levels->clear();
  for (uint32_t mip = 0; mip <= font::kMipCount; ++mip) {
    const uint32_t dim = font::kAtlasSize >> mip;
    std::vector<uint8_t> resized(size_t(dim) * dim * 4);
    if (mip == 0) resized = atlas;
    else assets::ResizeRgba(atlas.data(), font::kAtlasSize, font::kAtlasSize,
                            resized.data(), dim, dim);
    std::vector<uint8_t> level;
    assets::EncodeDxt5(resized.data(), dim, dim, &level);
    if (level.size() != size_t(dim) * dim) return false;
    levels->push_back(std::move(level));
  }
  return true;
}

bool ComposeRetail(const std::vector<uint8_t>& original_font,
                   const std::vector<std::vector<uint8_t>>& private_levels,
                   std::vector<std::vector<uint8_t>>* levels) {
  namespace font = ui::input_font;
  if (!levels || private_levels.size() <= font::kMipCount) return false;
  size_t expected_bytes = 0;
  for (uint32_t mip = 0; mip < font::kMipCount; ++mip) {
    const size_t dim = font::kRetailAtlasSize >> mip;
    expected_bytes += dim * dim;
    if (private_levels[mip + 1].size() != dim * dim) return false;
  }
  if (original_font.size() != expected_bytes) return false;
  levels->clear();
  size_t offset = 0;
  for (uint32_t mip = 0; mip < font::kMipCount; ++mip) {
    const size_t dim = font::kRetailAtlasSize >> mip;
    const size_t level_bytes = dim * dim;
    const size_t icon_start = dim * ((font::kIconTop / 2) >> mip);
    std::vector<uint8_t> level(original_font.begin() + offset,
                               original_font.begin() + offset + level_bytes);
    std::copy(private_levels[mip + 1].begin() + icon_start,
              private_levels[mip + 1].end(), level.begin() + icon_start);
    levels->push_back(std::move(level));
    offset += level_bytes;
  }
  return true;
}
}  // namespace rb_blitz::assets::input_font_atlas
