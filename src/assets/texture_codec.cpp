// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "assets/texture_codec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb_blitz::assets {
namespace {

std::uint16_t ReadLe16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}

std::uint32_t ReadLe32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
         (std::uint32_t(p[3]) << 24);
}

void WriteLe16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFF);
  p[1] = static_cast<std::uint8_t>(v >> 8);
}

void WriteLe32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFF);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// A 5:6:5 colour expanded the way the hardware does it (replicate the high bits).
struct Rgb8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

Rgb8 Expand565(std::uint16_t c) {
  const std::uint8_t r5 = static_cast<std::uint8_t>((c >> 11) & 0x1F);
  const std::uint8_t g6 = static_cast<std::uint8_t>((c >> 5) & 0x3F);
  const std::uint8_t b5 = static_cast<std::uint8_t>(c & 0x1F);
  return Rgb8{static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
              static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
              static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2))};
}

std::uint16_t Pack565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  const std::uint16_t r5 = static_cast<std::uint16_t>((r * 31u + 127u) / 255u);
  const std::uint16_t g6 = static_cast<std::uint16_t>((g * 63u + 127u) / 255u);
  const std::uint16_t b5 = static_cast<std::uint16_t>((b * 31u + 127u) / 255u);
  return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

// The four colours a block interpolates. `four_colour` selects the mode the DXT5 colour block
// always uses; DXT1 uses it only while c0 > c1 (c0 <= c1 means three colours and a transparent
// entry).
void ColourPalette(std::uint16_t c0, std::uint16_t c1, bool four_colour, Rgb8 out[4]) {
  out[0] = Expand565(c0);
  out[1] = Expand565(c1);
  if (four_colour || c0 > c1) {
    out[2] = Rgb8{static_cast<std::uint8_t>((2 * out[0].r + out[1].r) / 3),
                  static_cast<std::uint8_t>((2 * out[0].g + out[1].g) / 3),
                  static_cast<std::uint8_t>((2 * out[0].b + out[1].b) / 3)};
    out[3] = Rgb8{static_cast<std::uint8_t>((out[0].r + 2 * out[1].r) / 3),
                  static_cast<std::uint8_t>((out[0].g + 2 * out[1].g) / 3),
                  static_cast<std::uint8_t>((out[0].b + 2 * out[1].b) / 3)};
  } else {
    out[2] = Rgb8{static_cast<std::uint8_t>((out[0].r + out[1].r) / 2),
                  static_cast<std::uint8_t>((out[0].g + out[1].g) / 2),
                  static_cast<std::uint8_t>((out[0].b + out[1].b) / 2)};
    out[3] = Rgb8{0, 0, 0};  // transparent
  }
}

void AlphaPalette(std::uint8_t a0, std::uint8_t a1, std::uint8_t out[8]) {
  out[0] = a0;
  out[1] = a1;
  if (a0 > a1) {
    for (int i = 1; i <= 6; ++i) {
      out[1 + i] = static_cast<std::uint8_t>((a0 * (7 - i) + a1 * i) / 7);
    }
  } else {
    for (int i = 1; i <= 4; ++i) {
      out[1 + i] = static_cast<std::uint8_t>((a0 * (5 - i) + a1 * i) / 5);
    }
    out[6] = 0;
    out[7] = 255;
  }
}

int NearestColour(const Rgb8 palette[4], std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  int best = 0;
  int best_distance = 1 << 30;
  for (int i = 0; i < 4; ++i) {
    const int dr = int(palette[i].r) - int(r);
    const int dg = int(palette[i].g) - int(g);
    const int db = int(palette[i].b) - int(b);
    const int distance = dr * dr + dg * dg + db * db;
    if (distance < best_distance) {
      best_distance = distance;
      best = i;
    }
  }
  return best;
}

int NearestAlpha(const std::uint8_t palette[8], std::uint8_t a) {
  int best = 0;
  int best_distance = 1 << 30;
  for (int i = 0; i < 8; ++i) {
    const int distance = std::abs(int(palette[i]) - int(a));
    if (distance < best_distance) {
      best_distance = distance;
      best = i;
    }
  }
  return best;
}

// One 4x4 block of RGBA, edge pixels repeated so a texture whose size is not a multiple of 4 still
// encodes its last partial block from real pixels.
void GatherBlock(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height, int block_x,
                 int block_y, std::uint8_t pixels[16][4]) {
  for (int y = 0; y < 4; ++y) {
    const std::uint32_t sy = std::min<std::uint32_t>(std::uint32_t(block_y * 4 + y), height - 1);
    for (int x = 0; x < 4; ++x) {
      const std::uint32_t sx = std::min<std::uint32_t>(std::uint32_t(block_x * 4 + x), width - 1);
      std::memcpy(pixels[y * 4 + x], rgba + (std::size_t(sy) * width + sx) * 4, 4);
    }
  }
}

// The block's colour endpoints by the bounding-box fit, with the guarantee that c0 != c1 so the
// four-entry palette stays well defined.
void FitColours(const std::uint8_t pixels[16][4], bool four_colour, std::uint16_t* c0,
                std::uint16_t* c1) {
  std::uint8_t rmin = 255, gmin = 255, bmin = 255;
  std::uint8_t rmax = 0, gmax = 0, bmax = 0;
  for (int i = 0; i < 16; ++i) {
    rmin = std::min(rmin, pixels[i][0]);
    gmin = std::min(gmin, pixels[i][1]);
    bmin = std::min(bmin, pixels[i][2]);
    rmax = std::max(rmax, pixels[i][0]);
    gmax = std::max(gmax, pixels[i][1]);
    bmax = std::max(bmax, pixels[i][2]);
  }
  std::uint16_t hi = Pack565(rmax, gmax, bmax);
  std::uint16_t lo = Pack565(rmin, gmin, bmin);
  if (hi == lo) {
    if (hi < 0xFFFF) {
      ++hi;
    } else {
      --lo;
    }
  }
  if (hi < lo) {
    std::swap(hi, lo);
  }
  if (!four_colour && hi <= lo) {
    // DXT1 only reaches its four-colour mode while c0 > c1.
    if (hi < 0xFFFF) {
      ++hi;
    } else if (lo > 0) {
      --lo;
    }
  }
  *c0 = hi;
  *c1 = lo;
}

}  // namespace

std::size_t BlockBytes(TextureFormat format) { return format == TextureFormat::kDxt1 ? 8 : 16; }

std::size_t RowPitchBytes(TextureFormat format, std::uint32_t width) {
  return std::size_t((width + 3) / 4) * BlockBytes(format);
}

std::size_t MipLevelBytes(TextureFormat format, std::uint32_t width, std::uint32_t height,
                          std::uint32_t level) {
  const std::uint32_t w = std::max<std::uint32_t>(1, width >> level);
  const std::uint32_t h = std::max<std::uint32_t>(1, height >> level);
  return std::size_t((w + 3) / 4) * ((h + 3) / 4) * BlockBytes(format);
}

std::size_t MipChainBytes(TextureFormat format, std::uint32_t width, std::uint32_t height,
                          std::uint32_t levels) {
  std::size_t total = 0;
  for (std::uint32_t level = 0; level < levels; ++level) {
    total += MipLevelBytes(format, width, height, level);
  }
  return total;
}

bool ParseTextureEnvelope(const std::uint8_t* data, std::size_t size, TextureEnvelope* envelope,
                          std::string* why) {
  if (data == nullptr || envelope == nullptr) {
    if (why) *why = "no data";
    return false;
  }
  if (size < kTextureEnvelopeBytes) {
    if (why) *why = "shorter than the " + std::to_string(kTextureEnvelopeBytes) + "-byte envelope";
    return false;
  }
  const std::uint8_t version = data[0];
  if (version == 1) {
    // Three shipped files are version 1 (`gem_ulti`, `gstar1`, `logo_anim`) and their header lays
    // its dimensions out differently from the version 2 files this codec writes. The engine reads
    // them itself; nothing here produces or consumes them, so the honest answer is a refusal rather
    // than a guess that would misread a texture's size.
    if (why) *why = "envelope version 1 is not decoded by this build (only version 2 is written)";
    return false;
  }
  if (version != 2) {
    if (why) *why = "unknown envelope version " + std::to_string(version);
    return false;
  }
  const std::uint16_t format_word = ReadLe16(data + 5);
  TextureFormat format;
  switch (format_word) {
    case 0x0804:
      format = TextureFormat::kDxt1;
      break;
    case 0x1808:
      format = TextureFormat::kDxt5;
      break;
    default:
      if (why) *why = "unsupported format word " + std::to_string(format_word);
      return false;
  }
  TextureEnvelope parsed;
  parsed.version = 2;
  parsed.id = ReadLe32(data + 1);
  parsed.format = format;
  // The mip count, the dimensions and the pitch sit at the same offsets in every version 2 file.
  parsed.mip_levels = std::uint32_t(data[10]) + 1;
  parsed.width = ReadLe16(data + 11);
  parsed.height = ReadLe16(data + 13);
  parsed.pitch_dwords = ReadLe16(data + 15);
  if (parsed.width == 0 || parsed.height == 0) {
    if (why) *why = "a zero dimension";
    return false;
  }
  if (parsed.mip_levels > 32) {
    if (why) *why = "an impossible mip count";
    return false;
  }
  const std::size_t expected =
      MipChainBytes(parsed.format, parsed.width, parsed.height, parsed.mip_levels);
  if (size < kTextureEnvelopeBytes + expected) {
    if (why) {
      *why = "the header describes " + std::to_string(expected + kTextureEnvelopeBytes) +
             " byte(s), the file carries " + std::to_string(size);
    }
    return false;
  }
  *envelope = parsed;
  return true;
}

void SwapSixteenBitWords(std::uint8_t* data, std::size_t size) {
  if (data == nullptr) return;
  for (std::size_t i = 0; i + 1 < size; i += 2) {
    std::swap(data[i], data[i + 1]);
  }
}

void DecodeDxt1(const std::uint8_t* blocks, std::uint32_t width, std::uint32_t height,
                std::uint8_t* rgba) {
  if (blocks == nullptr || rgba == nullptr || width == 0 || height == 0) return;
  const std::uint32_t block_columns = (width + 3) / 4;
  const std::uint32_t block_rows = (height + 3) / 4;
  for (std::uint32_t by = 0; by < block_rows; ++by) {
    for (std::uint32_t bx = 0; bx < block_columns; ++bx) {
      const std::uint8_t* src = blocks + (std::size_t(by) * block_columns + bx) * 8;
      const std::uint16_t c0 = ReadLe16(src);
      const std::uint16_t c1 = ReadLe16(src + 2);
      const std::uint32_t bits = ReadLe32(src + 4);
      Rgb8 palette[4];
      ColourPalette(c0, c1, false, palette);
      const bool transparent_index = c0 <= c1;
      for (int y = 0; y < 4; ++y) {
        const std::uint32_t dy = by * 4 + std::uint32_t(y);
        if (dy >= height) break;
        for (int x = 0; x < 4; ++x) {
          const std::uint32_t dx = bx * 4 + std::uint32_t(x);
          if (dx >= width) break;
          const int index = int((bits >> (2 * (y * 4 + x))) & 0x3);
          std::uint8_t* out = rgba + (std::size_t(dy) * width + dx) * 4;
          const bool clear = transparent_index && index == 3;
          out[0] = palette[index].r;
          out[1] = palette[index].g;
          out[2] = palette[index].b;
          out[3] = clear ? 0 : 255;
        }
      }
    }
  }
}

void DecodeDxt5(const std::uint8_t* blocks, std::uint32_t width, std::uint32_t height,
                std::uint8_t* rgba) {
  if (blocks == nullptr || rgba == nullptr || width == 0 || height == 0) return;
  const std::uint32_t block_columns = (width + 3) / 4;
  const std::uint32_t block_rows = (height + 3) / 4;
  for (std::uint32_t by = 0; by < block_rows; ++by) {
    for (std::uint32_t bx = 0; bx < block_columns; ++bx) {
      const std::uint8_t* src = blocks + (std::size_t(by) * block_columns + bx) * 16;
      std::uint8_t alpha_palette[8];
      AlphaPalette(src[0], src[1], alpha_palette);
      std::uint64_t alpha_bits = 0;
      for (int i = 0; i < 6; ++i) {
        alpha_bits |= std::uint64_t(src[2 + i]) << (8 * i);
      }
      const std::uint16_t c0 = ReadLe16(src + 8);
      const std::uint16_t c1 = ReadLe16(src + 10);
      const std::uint32_t bits = ReadLe32(src + 12);
      // A DXT5 colour block always uses the four-colour interpolation.
      Rgb8 palette[4];
      ColourPalette(c0, c1, true, palette);
      for (int y = 0; y < 4; ++y) {
        const std::uint32_t dy = by * 4 + std::uint32_t(y);
        if (dy >= height) break;
        for (int x = 0; x < 4; ++x) {
          const std::uint32_t dx = bx * 4 + std::uint32_t(x);
          if (dx >= width) break;
          const int pixel = y * 4 + x;
          const int colour_index = int((bits >> (2 * pixel)) & 0x3);
          const int alpha_index = int((alpha_bits >> (3 * pixel)) & 0x7);
          std::uint8_t* out = rgba + (std::size_t(dy) * width + dx) * 4;
          out[0] = palette[colour_index].r;
          out[1] = palette[colour_index].g;
          out[2] = palette[colour_index].b;
          out[3] = alpha_palette[alpha_index];
        }
      }
    }
  }
}

void EncodeDxt1(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>* blocks) {
  blocks->clear();
  if (rgba == nullptr || width == 0 || height == 0) return;
  const std::uint32_t block_columns = (width + 3) / 4;
  const std::uint32_t block_rows = (height + 3) / 4;
  blocks->resize(std::size_t(block_columns) * block_rows * 8);
  std::uint8_t* out = blocks->data();
  for (std::uint32_t by = 0; by < block_rows; ++by) {
    for (std::uint32_t bx = 0; bx < block_columns; ++bx) {
      std::uint8_t pixels[16][4];
      GatherBlock(rgba, width, height, int(bx), int(by), pixels);
      std::uint16_t c0 = 0, c1 = 0;
      FitColours(pixels, false, &c0, &c1);
      Rgb8 palette[4];
      ColourPalette(c0, c1, true, palette);
      std::uint32_t bits = 0;
      for (int i = 0; i < 16; ++i) {
        bits |= std::uint32_t(NearestColour(palette, pixels[i][0], pixels[i][1], pixels[i][2]))
                << (2 * i);
      }
      std::uint8_t* dst = out + (std::size_t(by) * block_columns + bx) * 8;
      WriteLe16(dst, c0);
      WriteLe16(dst + 2, c1);
      WriteLe32(dst + 4, bits);
    }
  }
}

void EncodeDxt5(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>* blocks) {
  blocks->clear();
  if (rgba == nullptr || width == 0 || height == 0) return;
  const std::uint32_t block_columns = (width + 3) / 4;
  const std::uint32_t block_rows = (height + 3) / 4;
  blocks->resize(std::size_t(block_columns) * block_rows * 16);
  std::uint8_t* out = blocks->data();
  for (std::uint32_t by = 0; by < block_rows; ++by) {
    for (std::uint32_t bx = 0; bx < block_columns; ++bx) {
      std::uint8_t pixels[16][4];
      GatherBlock(rgba, width, height, int(bx), int(by), pixels);
      // Alpha: the eight-step mode, endpoints being the block's extremes.
      std::uint8_t amin = 255, amax = 0;
      for (int i = 0; i < 16; ++i) {
        amin = std::min(amin, pixels[i][3]);
        amax = std::max(amax, pixels[i][3]);
      }
      const std::uint8_t a0 = amax;
      const std::uint8_t a1 = amin;
      std::uint8_t alpha_palette[8];
      AlphaPalette(a0, a1, alpha_palette);
      std::uint64_t alpha_bits = 0;
      for (int i = 0; i < 16; ++i) {
        const int index = a0 == a1 ? 0 : NearestAlpha(alpha_palette, pixels[i][3]);
        alpha_bits |= std::uint64_t(index) << (3 * i);
      }
      std::uint16_t c0 = 0, c1 = 0;
      FitColours(pixels, true, &c0, &c1);
      Rgb8 palette[4];
      ColourPalette(c0, c1, true, palette);
      std::uint32_t bits = 0;
      for (int i = 0; i < 16; ++i) {
        bits |= std::uint32_t(NearestColour(palette, pixels[i][0], pixels[i][1], pixels[i][2]))
                << (2 * i);
      }
      std::uint8_t* dst = out + (std::size_t(by) * block_columns + bx) * 16;
      dst[0] = a0;
      dst[1] = a1;
      for (int i = 0; i < 6; ++i) {
        dst[2 + i] = static_cast<std::uint8_t>((alpha_bits >> (8 * i)) & 0xFF);
      }
      WriteLe16(dst + 8, c0);
      WriteLe16(dst + 10, c1);
      WriteLe32(dst + 12, bits);
    }
  }
}

void ResizeRgba(const std::uint8_t* src, std::uint32_t src_width, std::uint32_t src_height,
                std::uint8_t* dst, std::uint32_t dst_width, std::uint32_t dst_height) {
  if (src == nullptr || dst == nullptr || src_width == 0 || src_height == 0 || dst_width == 0 ||
      dst_height == 0) {
    return;
  }
  // Horizontal pass: dst_width x src_height, in float.
  std::vector<float> middle(std::size_t(dst_width) * src_height * 4, 0.0f);
  for (std::uint32_t y = 0; y < src_height; ++y) {
    const std::uint8_t* row = src + std::size_t(y) * src_width * 4;
    for (std::uint32_t x = 0; x < dst_width; ++x) {
      const double begin = double(x) * src_width / dst_width;
      const double end = double(x + 1) * src_width / dst_width;
      const double span = end - begin;
      const std::uint32_t first = std::min(src_width - 1, std::uint32_t(begin));
      const std::uint32_t last = std::min(src_width - 1, std::uint32_t(std::ceil(end)) - 1);
      float accumulators[4] = {0, 0, 0, 0};
      double weight_sum = 0.0;
      for (std::uint32_t i = first; i <= last; ++i) {
        const double overlap = std::min(end, double(i) + 1.0) - std::max(begin, double(i));
        if (overlap <= 0.0) continue;
        const std::uint8_t* p = row + std::size_t(i) * 4;
        for (int c = 0; c < 4; ++c) {
          accumulators[c] += float(p[c]) * float(overlap);
        }
        weight_sum += overlap;
      }
      float* out = middle.data() + (std::size_t(y) * dst_width + x) * 4;
      const float scale = weight_sum > 0.0 ? float(1.0 / weight_sum) : float(1.0 / span);
      for (int c = 0; c < 4; ++c) {
        out[c] = accumulators[c] * scale;
      }
    }
  }
  // Vertical pass over the middle buffer.
  for (std::uint32_t y = 0; y < dst_height; ++y) {
    const double begin = double(y) * src_height / dst_height;
    const double end = double(y + 1) * src_height / dst_height;
    const std::uint32_t first = std::min(src_height - 1, std::uint32_t(begin));
    const std::uint32_t last = std::min(src_height - 1, std::uint32_t(std::ceil(end)) - 1);
    for (std::uint32_t x = 0; x < dst_width; ++x) {
      float accumulators[4] = {0, 0, 0, 0};
      double weight_sum = 0.0;
      for (std::uint32_t i = first; i <= last; ++i) {
        const double overlap = std::min(end, double(i) + 1.0) - std::max(begin, double(i));
        if (overlap <= 0.0) continue;
        const float* p = middle.data() + (std::size_t(i) * dst_width + x) * 4;
        for (int c = 0; c < 4; ++c) {
          accumulators[c] += p[c] * float(overlap);
        }
        weight_sum += overlap;
      }
      std::uint8_t* out = dst + (std::size_t(y) * dst_width + x) * 4;
      for (int c = 0; c < 4; ++c) {
        const float value = weight_sum > 0.0 ? accumulators[c] / float(weight_sum) : 0.0f;
        out[c] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
      }
    }
  }
}

std::string BuildPlatformTexture(const std::uint8_t* rgba, std::uint32_t width,
                                 std::uint32_t height, TextureFormat format, std::uint32_t id,
                                 bool swap_words, std::uint32_t min_level_size) {
  if (rgba == nullptr || width < 4 || height < 4 || width % 4 != 0 || height % 4 != 0) {
    return {};
  }
  if (min_level_size < 4) min_level_size = 4;

  // The complete mip chain, largest first, each level from the one above it.
  std::vector<std::uint32_t> sizes;  // w and h per level
  std::vector<std::vector<std::uint8_t>> pixels;
  std::vector<std::vector<std::uint8_t>> encoded;

  std::uint32_t level_width = width;
  std::uint32_t level_height = height;
  std::vector<std::uint8_t> current(rgba, rgba + std::size_t(width) * height * 4);
  for (;;) {
    std::vector<std::uint8_t> blocks;
    if (format == TextureFormat::kDxt1) {
      EncodeDxt1(current.data(), level_width, level_height, &blocks);
    } else {
      EncodeDxt5(current.data(), level_width, level_height, &blocks);
    }
    if (blocks.empty()) return {};
    pixels.push_back(current);
    encoded.push_back(std::move(blocks));
    sizes.push_back(level_width);
    sizes.push_back(level_height);
    const std::uint32_t next_width = level_width / 2;
    const std::uint32_t next_height = level_height / 2;
    if (next_width < 4 || next_height < 4) break;
    if (std::min(next_width, next_height) < min_level_size) break;
    std::vector<std::uint8_t> smaller(std::size_t(next_width) * next_height * 4);
    ResizeRgba(current.data(), level_width, level_height, smaller.data(), next_width, next_height);
    current = std::move(smaller);
    level_width = next_width;
    level_height = next_height;
  }

  std::string file;
  const std::size_t payload = MipChainBytes(format, width, height, std::uint32_t(encoded.size()));
  file.assign(kTextureEnvelopeBytes + payload, '\0');
  auto* bytes = reinterpret_cast<std::uint8_t*>(file.data());
  // The envelope, exactly as measured on the shipped files (see the header comment).
  bytes[0] = 2;  // version
  WriteLe32(bytes + 1, id);
  WriteLe16(bytes + 5, static_cast<std::uint16_t>(format));
  bytes[7] = 0;
  bytes[8] = 0;
  bytes[9] = 0;
  bytes[10] = static_cast<std::uint8_t>(encoded.size() - 1);
  WriteLe16(bytes + 11, static_cast<std::uint16_t>(width));
  WriteLe16(bytes + 13, static_cast<std::uint16_t>(height));
  WriteLe16(bytes + 15, static_cast<std::uint16_t>(RowPitchBytes(format, width) / 4));

  std::size_t offset = kTextureEnvelopeBytes;
  for (const std::vector<std::uint8_t>& blocks : encoded) {
    std::memcpy(bytes + offset, blocks.data(), blocks.size());
    offset += blocks.size();
  }
  if (offset != file.size()) {
    return {};
  }
  if (swap_words) {
    SwapSixteenBitWords(bytes + kTextureEnvelopeBytes, payload);
  }
  return file;
}

bool DecodePlatformTexture(const std::uint8_t* data, std::size_t size, bool swapped,
                           std::vector<std::uint8_t>* rgba, TextureEnvelope* envelope,
                           std::string* why) {
  TextureEnvelope parsed;
  if (!ParseTextureEnvelope(data, size, &parsed, why)) {
    return false;
  }
  if (envelope != nullptr) *envelope = parsed;
  if (rgba == nullptr) return true;
  const std::size_t level_bytes =
      MipLevelBytes(parsed.format, parsed.width, parsed.height, 0);
  std::vector<std::uint8_t> blocks(data + kTextureEnvelopeBytes,
                                   data + kTextureEnvelopeBytes + level_bytes);
  if (swapped) {
    SwapSixteenBitWords(blocks.data(), blocks.size());
  }
  rgba->assign(std::size_t(parsed.width) * parsed.height * 4, 0);
  if (parsed.format == TextureFormat::kDxt1) {
    DecodeDxt1(blocks.data(), parsed.width, parsed.height, rgba->data());
  } else {
    DecodeDxt5(blocks.data(), parsed.width, parsed.height, rgba->data());
  }
  return true;
}

}  // namespace rb_blitz::assets
