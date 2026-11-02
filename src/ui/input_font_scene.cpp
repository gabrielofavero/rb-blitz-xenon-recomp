// SPDX-License-Identifier: GPL-2.0-only
#include "ui/input_font_scene.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <string_view>

// stb's project dependency supplies the raw DEFLATE decoder used by Milo chunks.
// Keep its definitions local: the SDK has another image decoder in a separate library.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#include "../../rexglue-sdk/thirdparty/stb/stb_image.h"

namespace rb_blitz::ui::input_font {
namespace {
std::atomic<std::shared_ptr<const std::vector<uint8_t>>> g_original_bc3;
std::atomic<std::shared_ptr<const std::vector<uint8_t>>> g_private_bc3;
uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void PutLe32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i != 4; ++i) p[i] = uint8_t(value >> (8 * i));
}
uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void PutBe32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i != 4; ++i) p[i] = uint8_t(value >> (24 - 8 * i));
}
void PutBe16(uint8_t* p, uint16_t value) {
  p[0] = uint8_t(value >> 8);
  p[1] = uint8_t(value);
}
void PutFloat(uint8_t* p, float value) { PutBe32(p, std::bit_cast<uint32_t>(value)); }
bool Fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}
bool ReplaceMeasured(std::vector<uint8_t>* bytes, std::string_view original,
                     std::string_view replacement, size_t expected_count) {
  if (original.size() != replacement.size()) return false;
  size_t count = 0;
  for (auto at = std::search(bytes->begin(), bytes->end(),
                             original.begin(), original.end());
       at != bytes->end();
       at = std::search(at + original.size(), bytes->end(),
                        original.begin(), original.end())) {
    std::copy(replacement.begin(), replacement.end(), at);
    ++count;
  }
  return count == expected_count;
}
bool DecodeStream(const std::vector<uint8_t>& source, std::vector<uint8_t>* bytes,
                  std::string* error) {
  constexpr uint32_t kHeader = 0x810;
  constexpr uint32_t kStored = 0xCABEDEAF;
  constexpr uint32_t kDeflate = 0xCDBEDEAF;
  constexpr uint32_t kStoredChunk = 0x01000000;
  constexpr uint32_t kChunkMask = 0x00FFFFFF;
  if (!bytes || source.size() < kHeader || Le32(source.data() + 4) != kHeader)
    return Fail(error, "invalid Milo chunk header");
  const uint32_t format = Le32(source.data());
  const uint32_t chunks = Le32(source.data() + 8);
  if ((format != kStored && format != kDeflate) || chunks == 0 || chunks > 512)
    return Fail(error, "unsupported Milo chunk stream");
  bytes->clear();
  size_t pos = kHeader;
  for (uint32_t i = 0; i < chunks; ++i) {
    const uint32_t word = Le32(source.data() + 16 + i * 4);
    const size_t size = word & kChunkMask;
    if (pos + size > source.size()) return Fail(error, "truncated Milo chunk");
    if (format == kStored || (word & kStoredChunk)) {
      bytes->insert(bytes->end(), source.begin() + pos, source.begin() + pos + size);
    } else {
      if (size < 5) return Fail(error, "short compressed Milo chunk");
      const uint32_t expanded = Le32(source.data() + pos);
      if (expanded == 0 || expanded > 2 * 1024 * 1024)
        return Fail(error, "implausible Milo chunk size");
      const size_t old = bytes->size();
      bytes->resize(old + expanded);
      const int got = stbi_zlib_decode_noheader_buffer(
          reinterpret_cast<char*>(bytes->data() + old), static_cast<int>(expanded),
          reinterpret_cast<const char*>(source.data() + pos + 4), static_cast<int>(size - 4));
      if (got != expanded) return Fail(error, "Milo DEFLATE failed");
    }
    pos += size;
  }
  if (pos != source.size()) return Fail(error, "Milo chunk stream has trailing bytes");
  return true;
}
bool PackStored(const std::vector<uint8_t>& bytes, std::vector<uint8_t>* scene,
                std::string* error) {
  constexpr uint32_t kHeader = 0x810, kStored = 0xCABEDEAF;
  constexpr uint32_t kStoredChunk = 0x01000000, kChunkMask = 0x00FFFFFF;
  if (!scene || bytes.empty() || bytes.size() > kChunkMask)
    return Fail(error, "Milo scene is too large");
  scene->assign(kHeader, 0);
  PutLe32(scene->data(), kStored);
  PutLe32(scene->data() + 4, kHeader);
  PutLe32(scene->data() + 8, 1);
  PutLe32(scene->data() + 12, static_cast<uint32_t>(bytes.size()));
  PutLe32(scene->data() + 16, kStoredChunk | static_cast<uint32_t>(bytes.size()));
  scene->insert(scene->end(), bytes.begin(), bytes.end());
  if (error) error->clear();
  return true;
}
}  // namespace

bool PrepareScene(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                  std::vector<uint8_t>* original_bc3, std::string* error) {
  if (!scene || !original_bc3) return Fail(error, "missing Pentatonic output");
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;

  // Four fixed-width Font Glyph arrays share one texture. Locate them by their
  // contiguous ASCII records rather than a compressed-file offset. This is a private
  // icon-only scene, so occupied text glyphs may be reassigned here.
  std::vector<size_t> tables;
  for (size_t at = 32 * 18 + 4; at + (256 - 32) * 18 <= bytes.size(); ++at) {
    if (bytes[at] != 0 || bytes[at + 1] != 0x20 ||
        Be32(bytes.data() + at - 32 * 18 - 4) != 256) continue;
    bool ascii = true;
    for (uint32_t c = 0; c < 8; ++c)
      if (bytes[at + c * 18] != 0 || bytes[at + c * 18 + 1] != 0x20 + c)
        ascii = false;
    if (ascii) tables.push_back(at - 32 * 18);
  }
  if (tables.size() != 4) return Fail(error, "Pentatonic Glyph arrays changed");
  for (const size_t table : tables) {
    if (Be32(bytes.data() + table - 12) !=
            std::bit_cast<uint32_t>(kOriginalFontCellWidth / kRetailAtlasSize) ||
        Be32(bytes.data() + table - 8) !=
            std::bit_cast<uint32_t>(kOriginalFontCellHeight / kRetailAtlasSize))
      return Fail(error, "Pentatonic cell dimensions changed");
    PutFloat(bytes.data() + table - 12, kFontCellWidth / kAtlasSize);
    PutFloat(bytes.data() + table - 8, kFontCellHeight / kAtlasSize);
    for (uint32_t index = 0; index < kIconCount; ++index) {
      const uint32_t code = GlyphCode(index);
      uint8_t* glyph = bytes.data() + table + code * 18;
      if (glyph[0] != 0 || glyph[1] != code)
        return Fail(error, "Pentatonic glyph code changed");
      PutFloat(glyph + 2, float(IconX(index)) / kAtlasSize);
      PutFloat(glyph + 6, float(IconY(index) -
                                (IsHintTile(index) ? kHintInset : kIconInsetY)) / kAtlasSize);
      PutFloat(glyph + 10, IconSize(index) / kFontCellWidth);
      PutFloat(glyph + 14, IconSize(index) / kFontCellWidth);
    }
  }

  constexpr size_t kTextureHeader = 0x867;
  constexpr size_t kTexturePayload = kTextureHeader + 31;
  size_t retail_chain_bytes = 0, private_chain_bytes = 0;
  for (uint32_t mip = 0; mip < kMipCount; ++mip)
    retail_chain_bytes += (kRetailAtlasSize >> mip) * (kRetailAtlasSize >> mip);
  for (uint32_t mip = 0; mip < kMipCount; ++mip)
    private_chain_bytes += (kAtlasSize >> mip) * (kAtlasSize >> mip);
  const std::array<uint8_t, 8> expected{8, 0, 0, 0, 0x18, 6, 4, 0};
  if (bytes.size() < kTexturePayload + retail_chain_bytes ||
      std::memcmp(bytes.data() + kTextureHeader + 4, expected.data(), expected.size()) != 0)
    return Fail(error, "Pentatonic BC3 texture record changed");
  original_bc3->resize(retail_chain_bytes);
  for (size_t i = 0; i < retail_chain_bytes; i += 2) {
    (*original_bc3)[i] = bytes[kTexturePayload + i + 1];
    (*original_bc3)[i + 1] = bytes[kTexturePayload + i];
  }
  // RndDir shares resources by their serialized names. The copied font must
  // own its Tex and Mat objects, and the texture's source path must differ too;
  // otherwise the guest keeps sampling the already loaded retail 1024 atlas.
  // Font object names stay unchanged because the title's font links use them.
  constexpr struct { std::string_view original, replacement; size_t count; } names[] = {
      {"Pentatonic_Regular_(8_89)4x.tex", "Iconatonic_Regular_(8_89)4x.tex", 5},
      {"Pentatonic_Regular_(8_89)4x.mat", "Iconatonic_Regular_(8_89)4x.mat", 2},
      {"Pentatonic_Regular_(5_00)4x_fadeout.mat",
       "Iconatonic_Regular_(5_00)4x_fadeout.mat", 3},
      {"Pentatonic_Regular_(5_00)4x_ploff.mat",
       "Iconatonic_Regular_(5_00)4x_ploff.mat", 3},
      {"Pentatonic_Regular_(5_00)4x_znormal.mat",
       "Iconatonic_Regular_(5_00)4x_znormal.mat", 3},
      {"../image/pentatonic_regular_(8_89)4x.bmp",
       "../image/iconatonic_regular_(8_89)4x.bmp", 1},
  };
  for (const auto& name : names)
    if (!ReplaceMeasured(&bytes, name.original, name.replacement, name.count))
      return Fail(error, "Pentatonic texture identity references changed");
  // The copied scene needs its own texture identity. Give its otherwise empty
  // BC3 image a marker so the guest cache cannot alias the retail font; the
  // runtime override replaces every pixel before the private font is drawn.
  std::vector<uint8_t> private_bc3(private_chain_bytes);
  constexpr std::array<uint8_t, 16> marker{
      0x52, 0x42, 0x42, 0x5A, 0x49, 0x43, 0x4F, 0x4E,
      0x46, 0x4F, 0x4E, 0x54, 0x32, 0x4B, 0x00, 0x01};
  std::copy(marker.begin(), marker.end(), private_bc3.begin());
  // The measured texture record has width, height and row pitch at +10.
  if (Be32(bytes.data() + kTextureHeader + 10) != 0x04000400 ||
      bytes[kTextureHeader + 14] != 4 || bytes[kTextureHeader + 15] != 0)
    return Fail(error, "Pentatonic texture dimensions changed");
  for (size_t field = 0; field < 3; ++field)
    PutBe16(bytes.data() + kTextureHeader + 10 + field * 2, uint16_t(kAtlasSize));
  std::vector<uint8_t> swapped(private_chain_bytes);
  for (size_t i = 0; i < private_chain_bytes; i += 2) {
    swapped[i] = private_bc3[i + 1];
    swapped[i + 1] = private_bc3[i];
  }
  bytes.erase(bytes.begin() + kTexturePayload,
              bytes.begin() + kTexturePayload + retail_chain_bytes);
  bytes.insert(bytes.begin() + kTexturePayload, swapped.begin(), swapped.end());
  // A single stored chunk is valid Milo and avoids a second compressor.
  if (!PackStored(bytes, scene, error)) return false;
  g_original_bc3.store(std::make_shared<const std::vector<uint8_t>>(*original_bc3));
  g_private_bc3.store(std::make_shared<const std::vector<uint8_t>>(std::move(private_bc3)));
  if (error) error->clear();
  return true;
}
bool PrepareList(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                 std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  // Retail UILabel revision 29: style count, resource/color references, then
  // size, kerning, z offset, italics and alpha. Preserve every existing color.
  constexpr size_t count = 0x1625F, first = 0x16263, end = 0x162AA;
  constexpr std::string_view font = "../fonts/pentatonic.milo";
  if (bytes.size() != 91806 || Be32(bytes.data() + count) != 1 ||
      Be32(bytes.data() + first) != font.size() ||
      std::memcmp(bytes.data() + first + 4, font.data(), font.size()) != 0 ||
      bytes[0x16252] != 1 || Be32(bytes.data() + end) != 0 ||
      Be32(bytes.data() + end + 4) != 0xADDEADDE)
    return Fail(error, "mapping label style record changed");
  std::vector<uint8_t> added;
  auto string = [&](std::string_view value) {
    const size_t at = added.size();
    added.resize(at + 4 + value.size());
    PutBe32(added.data() + at, static_cast<uint32_t>(value.size()));
    std::copy(value.begin(), value.end(), added.begin() + at + 4);
  };
  auto style = [&](std::string_view resource, std::string_view color) {
    string(resource);
    string(color);
    added.insert(added.end(), bytes.begin() + 0x16296, bytes.begin() + end);
  };
  // Each style must have a distinct font/material. A duplicate primary-font
  // style overwrites the primary material's hover tint in DrawShowing.
  style("../fonts/input_icons.milo", "active_focused.color");
  PutBe32(bytes.data() + count, 2);
  bytes.insert(bytes.begin() + end, added.begin(), added.end());
  // UILabel::PostLoad (824BC3E0) consumes one additional material-variation
  // string per style, after PreLoad has consumed the style array. Preserve
  // the primary style's empty string and append the icon style's empty string.
  bytes.insert(bytes.begin() + end + added.size() + 4, 4, 0);
  return PackStored(bytes, scene, error);
}
bool PrepareHintScene(const std::vector<uint8_t>& icon_scene, std::vector<uint8_t>* scene,
                      std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(icon_scene, &bytes, error)) return false;
  std::vector<size_t> tables;
  for (size_t at = 32 * 18 + 4; at + (256 - 32) * 18 <= bytes.size(); ++at) {
    if (bytes[at] != 0 || bytes[at + 1] != 0x20 ||
        Be32(bytes.data() + at - 32 * 18 - 4) != 256) continue;
    bool ascii = true;
    for (uint32_t c = 0; c < 8; ++c)
      if (bytes[at + c * 18] != 0 || bytes[at + c * 18 + 1] != 0x20 + c)
        ascii = false;
    if (ascii) tables.push_back(at - 32 * 18);
  }
  if (tables.size() != 4) return Fail(error, "prepared icon font Glyph arrays changed");
  for (const size_t table : tables) {
    if (Be32(bytes.data() + table - 12) !=
            std::bit_cast<uint32_t>(kFontCellWidth / kAtlasSize) ||
        Be32(bytes.data() + table - 8) !=
            std::bit_cast<uint32_t>(kFontCellHeight / kAtlasSize))
      return Fail(error, "prepared icon font cell dimensions changed");
    PutFloat(bytes.data() + table - 12, kHintFontCellWidth / kAtlasSize);
    PutFloat(bytes.data() + table - 8, kHintFontCellHeight / kAtlasSize);
    for (uint32_t action = 0; action < kHintCount; ++action) {
      const uint32_t code = GlyphCode(action);
      uint8_t* glyph = bytes.data() + table + code * 18;
      if (glyph[0] != 0 || glyph[1] != code)
        return Fail(error, "prepared icon font early glyph changed");
      const uint32_t index = kHintFirst + action;
      PutFloat(glyph + 2, float(IconX(index)) / kAtlasSize);
      PutFloat(glyph + 6, float(IconY(index) - kHintInset) / kAtlasSize);
      PutFloat(glyph + 10, kHintIconSize / kHintFontCellWidth);
      PutFloat(glyph + 14, kHintIconSize / kHintFontCellWidth);
    }
  }
  return PackStored(bytes, scene, error);
}
bool PrepareInlineHelp(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                       std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  constexpr size_t at = 0x3AD;
  constexpr std::string_view old_font = "../fonts/buttons.milo";
  constexpr std::string_view new_font = "../fonts/inline_help_icons.milo";
  if (bytes.size() != 1002 || Be32(bytes.data() + at) != old_font.size() ||
      std::memcmp(bytes.data() + at + 4, old_font.data(), old_font.size()) != 0 ||
      Be32(bytes.data() + 0x3E6) != 0xADDEADDE)
    return Fail(error, "inline help font record changed");
  PutBe32(bytes.data() + at, static_cast<uint32_t>(new_font.size()));
  bytes.erase(bytes.begin() + at + 4, bytes.begin() + at + 4 + old_font.size());
  bytes.insert(bytes.begin() + at + 4, new_font.begin(), new_font.end());
  return PackStored(bytes, scene, error);
}
std::shared_ptr<const std::vector<uint8_t>> OriginalBc3() {
  return g_original_bc3.load();
}
std::shared_ptr<const std::vector<uint8_t>> PrivateBc3() {
  return g_private_bc3.load();
}
}  // namespace rb_blitz::ui::input_font
