// SPDX-License-Identifier: GPL-2.0-only
#include "ui/input_font_scene.h"
#include "ui/milo_object_boundaries.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <cmath>
#include <string_view>
#include <tuple>
#include "ui/portrait_menu.h"

// stb's project dependency supplies the raw DEFLATE decoder used by Milo chunks.
// Keep its definitions local: the SDK has another image decoder in a separate library.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#include "../../rexglue-sdk/thirdparty/stb/stb_image.h"

namespace rb_blitz::ui::input_font {
namespace {
// Use the C++11 shared_ptr atomic operations: Apple libc++ does not yet
// implement the C++20 atomic<shared_ptr<T>> specialization. These retain the
// same sequentially consistent publication and owned-reader lifetime.
std::shared_ptr<const std::vector<uint8_t>> g_original_bc3;
std::shared_ptr<const std::vector<uint8_t>> g_private_bc3;
std::shared_ptr<const std::vector<uint8_t>> g_original_buttons_bc3;
std::shared_ptr<const RetailPsPads> g_original_ps_pads_bc3;
std::shared_ptr<const CalibrationButtons> g_calibration_buttons;
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
void PutFloat(uint8_t* p, float value) { PutBe32(p, std::bit_cast<uint32_t>(value)); }
bool Fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}
bool ReplaceMeasured(std::vector<uint8_t>* bytes, std::string_view original,
                     std::string_view replacement, size_t expected_count) {
  if (original.size() != replacement.size()) return false;
  size_t count = 0;
  for (auto at = std::search(bytes->begin(), bytes->end(), original.begin(), original.end());
       at != bytes->end();
       at = std::search(at + original.size(), bytes->end(), original.begin(), original.end())) {
    std::copy(replacement.begin(), replacement.end(), at);
    ++count;
  }
  return count == expected_count;
}
bool ReplaceAnchoredFonts(std::vector<uint8_t>* bytes, std::string_view old_font,
                          std::string_view new_font,
                          std::initializer_list<std::string_view> anchors,
                          size_t expected, std::string* error) {
  std::vector<size_t> offsets;
  for (auto at = std::search(bytes->begin(), bytes->end(), old_font.begin(), old_font.end());
       at != bytes->end();
       at = std::search(at + old_font.size(), bytes->end(), old_font.begin(), old_font.end())) {
    const auto offset = size_t(at - bytes->begin());
    const auto from = offset > 256 ? offset - 256 : 0;
    const auto anchored = std::any_of(anchors.begin(), anchors.end(), [&](std::string_view anchor) {
      return std::search(bytes->begin() + from, at, anchor.begin(), anchor.end()) != at;
    });
    if (!anchored) continue;
    if (offset < 4 || Be32(bytes->data() + offset - 4) != old_font.size())
      return Fail(error, "action hint font string length changed");
    offsets.push_back(offset);
  }
  if (offsets.size() != expected) return Fail(error, "action hint label font anchors changed");
  for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
    const auto offset = *it;
    PutBe32(bytes->data() + offset - 4, static_cast<uint32_t>(new_font.size()));
    bytes->erase(bytes->begin() + offset, bytes->begin() + offset + old_font.size());
    bytes->insert(bytes->begin() + offset, new_font.begin(), new_font.end());
  }
  return true;
}
uint32_t Crc32(const uint8_t* bytes, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
  }
  return ~crc;
}
bool DecodeStream(const std::vector<uint8_t>& source, std::vector<uint8_t>* bytes,
                  std::string* error, uint32_t max_chunk = 2 * 1024 * 1024) {
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
      if (expanded == 0 || expanded > max_chunk)
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

bool PrepareOriginalButtons(const std::vector<uint8_t>& source, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  // Measured buttons.milo_xbox record: 23-byte header, eight zero bytes, then
  // the first 512-square BC3 mip. Xbox stores each 16-bit word byte-swapped.
  constexpr size_t kHeader = 0x5C9, kPayload = kHeader + 31;
  constexpr size_t kBaseBytes = 512u * 512u;
  constexpr std::array<uint8_t, 12> kHeaderShape{
      8, 0, 0, 0, 0x18, 0, 2, 0, 2, 0, 2, 0};
  if (bytes.size() < kPayload + kBaseBytes ||
      std::memcmp(bytes.data() + kHeader + 4, kHeaderShape.data(),
                  kHeaderShape.size()) != 0 ||
      std::any_of(bytes.begin() + kHeader + 23, bytes.begin() + kPayload,
                  [](uint8_t byte) { return byte != 0; }))
    return Fail(error, "retail button BC3 texture record changed");
  std::vector<uint8_t> original(kBaseBytes);
  for (size_t i = 0; i < kBaseBytes; i += 2) {
    original[i] = bytes[kPayload + i + 1];
    original[i + 1] = bytes[kPayload + i];
  }
  std::atomic_store(&g_original_buttons_bc3,
      std::make_shared<const std::vector<uint8_t>>(std::move(original)));
  if (error) error->clear();
  return true;
}

bool PrepareRetailPsPads(const std::vector<uint8_t>& source, std::string* error) {
  std::vector<uint8_t> bytes;
  // This scene has 2.8 MiB expanded chunks, larger than the font scenes.
  if (!DecodeStream(source, &bytes, error, 3 * 1024 * 1024)) return false;
  constexpr std::array<uint8_t, 12> kHeaderShape{
      8, 0, 0, 0, 0x18, 6, 4, 0, 4, 0, 4, 0};
  constexpr std::array<uint32_t, 4> kChainCrc{
      0x1107691A, 0xF06BDFD3, 0xEEB00FDC, 0x53DB3410};
  constexpr size_t kMipCount = 7;
  constexpr size_t kChainBytes = 1398016;
  RetailPsPads pads;
  for (size_t index = 0; index < pads.size(); ++index) {
    const std::string path = "img/ps_" + std::to_string(index) + ".png";
    const auto at = std::search(bytes.begin(), bytes.end(), path.begin(), path.end());
    if (at == bytes.end() || at == bytes.begin() ||
        *(at - 1) != path.size())
      return Fail(error, "retail PS pad path changed");
    const size_t path_end = size_t(at - bytes.begin()) + path.size();
    const size_t header = path_end + 11;
    const size_t payload = header + 31;
    if (payload + kChainBytes > bytes.size() ||
        std::memcmp(bytes.data() + header + 4, kHeaderShape.data(),
                    kHeaderShape.size()) != 0 ||
        std::any_of(bytes.begin() + header + 23, bytes.begin() + payload,
                    [](uint8_t byte) { return byte != 0; }) ||
        Crc32(bytes.data() + payload, kChainBytes) != kChainCrc[index])
      return Fail(error, "retail PS pad BC3 texture record changed");
    size_t offset = payload;
    for (size_t mip = 0; mip < kMipCount; ++mip) {
      const size_t dim = 1024u >> mip;
      const size_t size = dim * dim;
      auto& level = pads[index].emplace_back(size);
      // Xbox scene BC3 words are byte-swapped; the host override expects
      // canonical BC3 blocks, just like PrepareOriginalButtons above.
      for (size_t i = 0; i < size; i += 2) {
        level[i] = bytes[offset + i + 1];
        level[i + 1] = bytes[offset + i];
      }
      offset += size;
    }
  }
  std::atomic_store(&g_original_ps_pads_bc3, std::make_shared<const RetailPsPads>(std::move(pads)));
  if (error) error->clear();
  return true;
}

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
    PutFloat(bytes.data() + table - 12, kPrivateFontCellWidth / kAtlasSize);
    PutFloat(bytes.data() + table - 8, kPrivateFontCellHeight / kAtlasSize);
    for (uint32_t index = 0; index < kIconCount; ++index) {
      const uint32_t code = GlyphCode(index);
      uint8_t* glyph = bytes.data() + table + code * 18;
      if (glyph[0] != 0 || glyph[1] != code)
        return Fail(error, "Pentatonic glyph code changed");
      PutFloat(glyph + 2, float(PrivateIconX(index)) / kAtlasSize);
      PutFloat(glyph + 6, float(PrivateIconY(index) - kPrivateInset) / kAtlasSize);
      PutFloat(glyph + 10, float(kPrivateIconSize) / kPrivateFontCellWidth);
      PutFloat(glyph + 14, float(kPrivateIconSize) / kPrivateFontCellWidth);
    }
  }

  constexpr size_t kTextureHeader = 0x867;
  constexpr size_t kTexturePayload = kTextureHeader + 31;
  size_t retail_chain_bytes = 0;
  for (uint32_t mip = 0; mip < kMipCount; ++mip)
    retail_chain_bytes += (kRetailAtlasSize >> mip) * (kRetailAtlasSize >> mip);
  const std::array<uint8_t, 8> expected{8, 0, 0, 0, 0x18, 6, 4, 0};
  if (bytes.size() < kTexturePayload + retail_chain_bytes ||
      Be32(bytes.data() + kTextureHeader) != 0x6F93E325 ||
      std::memcmp(bytes.data() + kTextureHeader + 4, expected.data(), expected.size()) != 0)
    return Fail(error, "Pentatonic BC3 texture record changed");
  original_bc3->resize(retail_chain_bytes);
  for (size_t i = 0; i < retail_chain_bytes; i += 2) {
    (*original_bc3)[i] = bytes[kTexturePayload + i + 1];
    (*original_bc3)[i + 1] = bytes[kTexturePayload + i];
  }
  // The measured texture record has width, height and row pitch at +10.
  if (Be32(bytes.data() + kTextureHeader + 10) != 0x04000400 ||
      bytes[kTextureHeader + 14] != 4 || bytes[kTextureHeader + 15] != 0)
    return Fail(error, "Pentatonic texture dimensions changed");
  // Keep the font object names, which the game's links use. Give its texture,
  // materials, source path, and bitmap cache key a separate identity.
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
  std::vector<uint8_t> private_bc3(retail_chain_bytes);
  constexpr std::array<uint8_t, 16> marker{
      0x52, 0x42, 0x42, 0x5A, 0x49, 0x43, 0x4F, 0x4E,
      0x46, 0x4F, 0x4E, 0x54, 0x31, 0x4B, 0x00, 0x01};
  std::copy(marker.begin(), marker.end(), private_bc3.begin());
  const uint32_t bitmap_key = Crc32(private_bc3.data(), kAtlasSize * kAtlasSize);
  if (!bitmap_key || bitmap_key == 0x6F93E325)
    return Fail(error, "private bitmap cache key collided with retail");
  PutBe32(bytes.data() + kTextureHeader, bitmap_key);
  for (size_t i = 0; i < retail_chain_bytes; i += 2) {
    bytes[kTexturePayload + i] = private_bc3[i + 1];
    bytes[kTexturePayload + i + 1] = private_bc3[i];
  }
  // A single stored chunk is valid Milo and avoids a second compressor.
  if (!PackStored(bytes, scene, error)) return false;
  std::atomic_store(&g_original_bc3, std::make_shared<const std::vector<uint8_t>>(*original_bc3));
  std::atomic_store(&g_private_bc3, std::make_shared<const std::vector<uint8_t>>(std::move(private_bc3)));
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
      Be32(bytes.data() + end + 4) != 0xADDEADDE ||
      Be32(bytes.data() + 0x16296) != 0x41A23D60)
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
    const size_t metrics = added.size();
    added.insert(added.end(), bytes.begin() + 0x16296, bytes.begin() + end);
    // The icon style is independent of the label text. Give the existing
    // keycap artwork a few more screen pixels without changing its silhouette.
    PutFloat(added.data() + metrics, 22.3f);
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
            std::bit_cast<uint32_t>(kPrivateFontCellWidth / kAtlasSize) ||
        Be32(bytes.data() + table - 8) !=
            std::bit_cast<uint32_t>(kPrivateFontCellHeight / kAtlasSize))
      return Fail(error, "prepared icon font cell dimensions changed");
    for (uint32_t action = 0; action < kHintCount; ++action) {
      // The splash keeps its authored 0x80 glyph. The metronome's tap glyph
      // uses the control code freed by that alias.
      uint32_t code = GlyphCode(action);
      if (action == kSplashStartHint) code = kSplashGlyphCode;
      if (action == kCalibrationTapIcon - kHintFirst) code = kCalibrationTapGlyphCode;
      uint8_t* glyph = bytes.data() + table + code * 18;
      if (glyph[0] != 0 || glyph[1] != code)
        return Fail(error, "prepared icon font early glyph changed");
      const uint32_t index = kHintFirst + action;
      PutFloat(glyph + 2, float(PrivateIconX(index)) / kAtlasSize);
      PutFloat(glyph + 6, float(PrivateIconY(index) - kPrivateInset) / kAtlasSize);
      PutFloat(glyph + 10, float(kPrivateIconSize) / kPrivateFontCellWidth);
      PutFloat(glyph + 14, float(kPrivateIconSize) / kPrivateFontCellWidth);
    }
  }
  // Both scenes use the same texture identity and pixels; their glyph tables differ.
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
      Be32(bytes.data() + 0x3E6) != 0xADDEADDE ||
      Be32(bytes.data() + 0x3CA) != 0x4198B240)
    return Fail(error, "inline help font record changed");
  // The footer has less room beside its caption than a mapping row.
  PutFloat(bytes.data() + 0x3CA, 20.0f);
  PutBe32(bytes.data() + at, static_cast<uint32_t>(new_font.size()));
  bytes.erase(bytes.begin() + at + 4, bytes.begin() + at + 4 + old_font.size());
  bytes.insert(bytes.begin() + at + 4, new_font.begin(), new_font.end());
  return PackStored(bytes, scene, error);
}
bool PrepareLoading(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                    std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  constexpr std::string_view token = "loading_press_button";
  constexpr std::string_view old_font = "../resource/fonts/buttons.milo";
  constexpr std::string_view new_font = "../resource/fonts/inline_help_icons.milo";
  // The prompt and rotating tip are separate UILabels. Match each font from
  // its measured text token, so the other three button styles stay retail.
  for (std::string_view anchor : {token, std::string_view("loading_tip_01")}) {
    size_t matches = 0, offset = 0;
    for (auto at = std::search(bytes.begin(), bytes.end(), anchor.begin(), anchor.end());
         at != bytes.end();
         at = std::search(at + anchor.size(), bytes.end(), anchor.begin(), anchor.end())) {
      const auto limit = bytes.begin() +
          std::min(bytes.size(), size_t(at - bytes.begin()) + 256);
      const auto font = std::search(at + anchor.size(), limit,
                                    old_font.begin(), old_font.end());
      if (font == limit) continue;
      offset = size_t(font - bytes.begin());
      ++matches;
    }
    if (matches != 1) return Fail(error, "loading label button font changed");
    if (offset < 4 || Be32(bytes.data() + offset - 4) != old_font.size())
      return Fail(error, "loading label font string length changed");
    PutBe32(bytes.data() + offset - 4, static_cast<uint32_t>(new_font.size()));
    bytes.erase(bytes.begin() + offset, bytes.begin() + offset + old_font.size());
    bytes.insert(bytes.begin() + offset, new_font.begin(), new_font.end());
  }
  if (!ReplaceAnchoredFonts(&bytes, old_font, new_font, {"autoplay_desc"}, 3, error))
    return false;
  return PackStored(bytes, scene, error);
}
bool PrepareSplash(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                   std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  constexpr std::string_view token = "splash_press_start";
  constexpr std::string_view old_font = "../resource/fonts/buttons.milo";
  constexpr std::string_view new_font = "../resource/fonts/inline_help_icons.milo";
  size_t matches = 0, offset = 0;
  for (auto at = std::search(bytes.begin(), bytes.end(), token.begin(), token.end());
       at != bytes.end();
       at = std::search(at + token.size(), bytes.end(), token.begin(), token.end())) {
    const auto limit = bytes.begin() +
        std::min(bytes.size(), size_t(at - bytes.begin()) + 256);
    const auto font = std::search(at + token.size(), limit,
                                  old_font.begin(), old_font.end());
    if (font == limit) continue;
    offset = size_t(font - bytes.begin());
    ++matches;
  }
  if (matches != 1) return Fail(error, "splash start button font changed");
  if (offset < 4 || Be32(bytes.data() + offset - 4) != old_font.size())
    return Fail(error, "splash start font string length changed");
  PutBe32(bytes.data() + offset - 4, static_cast<uint32_t>(new_font.size()));
  bytes.erase(bytes.begin() + offset, bytes.begin() + offset + old_font.size());
  bytes.insert(bytes.begin() + offset, new_font.begin(), new_font.end());
  return PackStored(bytes, scene, error);
}
namespace {
struct PortraitObject { std::string type, name; std::vector<uint8_t> data; };
void SceneWord(std::vector<uint8_t>& out, uint32_t value) {
  const auto at = out.size(); out.resize(at + 4); PutBe32(out.data() + at, value);
}
void SceneString(std::vector<uint8_t>& out, std::string_view value) {
  SceneWord(out, uint32_t(value.size())); out.insert(out.end(), value.begin(), value.end());
}
void RenameSceneString(std::vector<uint8_t>& out, std::string_view from, std::string_view to) {
  for (size_t at = 4; at + from.size() <= out.size(); ++at) {
    if (Be32(out.data() + at - 4) != from.size() ||
        std::memcmp(out.data() + at, from.data(), from.size())) continue;
    PutBe32(out.data() + at - 4, uint32_t(to.size()));
    out.erase(out.begin() + at, out.begin() + at + from.size());
    out.insert(out.begin() + at, to.begin(), to.end()); at += to.size();
  }
}
void SceneColor(std::vector<uint8_t>& out, size_t at, float r, float g, float b, float a) {
  PutFloat(out.data() + at, r); PutFloat(out.data() + at + 4, g);
  PutFloat(out.data() + at + 8, b); PutFloat(out.data() + at + 12, a);
}
void SceneMatrix(std::vector<uint8_t>& out, size_t at, float sx, float sz,
                 float x = 0, float y = 0, float z = 0) {
  std::fill(out.begin() + at, out.begin() + at + 48, uint8_t(0));
  PutFloat(out.data() + at, sx); PutFloat(out.data() + at + 16, 1);
  PutFloat(out.data() + at + 32, sz); PutFloat(out.data() + at + 36, x);
  PutFloat(out.data() + at + 40, y); PutFloat(out.data() + at + 44, z);
}

// Keep the directory's embedded resources and root record intact while editing
// its own object table. Group draw membership is independent of transform parents.
struct PortraitScene {
  std::vector<uint8_t> header, directory;
  std::vector<PortraitObject> objects;
  std::string root;
  float root_z = 0;
  size_t info = 0;
  explicit PortraitScene(const std::vector<uint8_t>& bytes) {
    size_t at = 4;
    auto string = [&] {
      const auto size = Be32(bytes.data() + at); at += 4;
      std::string value(reinterpret_cast<const char*>(bytes.data() + at), size);
      at += size; return value;
    };
    string(); root = string(); info = at;
    at += 9;
    const auto count = Be32(bytes.data() + at); at += 4;
    header.assign(bytes.begin(), bytes.begin() + at);
    for (uint32_t i = 0; i < count; ++i) {
      auto type = string(), name = string();
      objects.push_back({std::move(type), std::move(name), {}});
    }
    const auto markers = detail::MiloObjectBoundaries(bytes, at, count);
    directory.assign(bytes.begin() + at, bytes.begin() + markers.front() + 4);
    for (size_t i = 0; i < count; ++i)
      objects[i].data.assign(bytes.begin() + markers[i] + 4,
                             bytes.begin() + markers[i + 1] + 4);
  }
  PortraitObject* Find(std::string_view name) {
    for (auto& object : objects) if (object.name == name) return &object;
    return nullptr;
  }
  void Before(std::string_view member, const std::vector<std::string>& names) {
    for (auto& object : objects) {
      if (object.type != "Group") continue;
      auto& data = object.data;
      // RndGroup's drawable vector follows its transform and drawable fields.
      const auto parent = Be32(data.data() + 0x8A);
      size_t at = 0x8E + parent + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 4;
      if (at + 4 > data.size()) continue;
      const auto count = Be32(data.data() + at); const auto count_at = at; at += 4;
      for (uint32_t i = 0; i < count && at + 4 <= data.size(); ++i) {
        const auto size = Be32(data.data() + at);
        if (at + 4 + size > data.size()) break;
        if (std::string_view(reinterpret_cast<const char*>(data.data() + at + 4), size) == member) {
          std::vector<uint8_t> added;
          for (const auto& name : names) SceneString(added, name);
          PutBe32(data.data() + count_at, count + uint32_t(names.size()));
          data.insert(data.begin() + at, added.begin(), added.end());
          return;
        }
        at += 4 + size;
      }
    }
  }
  void RemoveDrawMember(std::string_view member) {
    for (auto& object : objects) {
      if (object.type != "Group") continue;
      auto& data = object.data;
      const size_t count_at = 0xAB + Be32(data.data() + 0x8A);
      if (count_at + 4 > data.size()) continue;
      size_t at = count_at + 4;
      auto count = Be32(data.data() + count_at);
      for (uint32_t i = 0; i < count; ++i) {
        const auto size = Be32(data.data() + at);
        if (std::string_view(reinterpret_cast<const char*>(data.data() + at + 4), size) == member) {
          data.erase(data.begin() + at, data.begin() + at + 4 + size);
          PutBe32(data.data() + count_at, --count);
          break;
        }
        at += 4 + size;
      }
    }
  }
  std::vector<uint8_t> Build() const {
    auto result = header;
    const auto old_count = Be32(result.data() + info + 9);
    uint32_t name_bytes = 0;
    for (const auto& object : objects) {
      SceneString(result, object.type); SceneString(result, object.name);
      name_bytes += uint32_t(object.name.size() + 1);
    }
    PutBe32(result.data() + info, Be32(header.data() + info) +
             2 * (uint32_t(objects.size()) - old_count));
    PutBe32(result.data() + info + 4, name_bytes + uint32_t(root.size() + 1));
    PutBe32(result.data() + info + 9, uint32_t(objects.size()));
    result.insert(result.end(), directory.begin(), directory.end());
    for (const auto& object : objects) result.insert(result.end(), object.data.begin(), object.data.end());
    return result;
  }
};

size_t PortraitMatrix(const PortraitObject& object) {
  if (object.type == "Group") return 0x21;
  if (object.type == "UISlider") return 0x20;
  if (object.type == "UILabel" || object.type == "UIList") return 0x19;
  if (object.type == "Mesh") return 0x15;
  if (object.type == "InlineHelp") {
    for (size_t i = 9; i + 113 <= object.data.size(); ++i)
      if (Be32(object.data.data() + i) == 9) return i + 4;
  }
  return 0;
}

void PortraitParent(PortraitObject& object, std::string_view parent) {
  const auto at = PortraitMatrix(object) + 105;
  const auto size = Be32(object.data.data() + at);
  object.data.erase(object.data.begin() + at + 4, object.data.begin() + at + 4 + size);
  object.data.insert(object.data.begin() + at + 4, parent.begin(), parent.end());
  PutBe32(object.data.data() + at, uint32_t(parent.size()));
}

void PortraitPose(PortraitObject& object, float scale, float x, float z,
                  std::string_view parent = {}) {
  const auto matrix = PortraitMatrix(object);
  for (size_t at : {matrix, matrix + 48}) SceneMatrix(object.data, at, scale, scale, x, 0, z);
  PortraitParent(object, parent);
}

void PortraitText(PortraitObject& object, float size, float width,
                  uint32_t alignment = 0x22, float wrap_height = -1) {
  auto& data = object.data;
  constexpr std::string_view prefix = "../";
  const auto font = std::search(data.begin(), data.end(), prefix.begin(), prefix.end());
  if (font == data.end()) return;
  const size_t at = size_t(font - data.begin()) - 4;
  const auto end = at + 4 + Be32(data.data() + at);
  const auto variation = Be32(data.data() + end);
  PutFloat(data.data() + end + 4 + variation, size);
  if (alignment != UINT32_MAX) PutBe32(data.data() + at - 33, alignment);
  if (width >= 0) PutFloat(data.data() + at - 29, width);
  if (wrap_height >= 0) {
    PutFloat(data.data() + at - 12, wrap_height);
    PutBe32(data.data() + at - 8, 0);  // UILabel wrap mode
  }
}

bool PortraitLibraryAnimation(PortraitScene& scene, float aspect, std::string* error) {
  auto* animation = scene.Find("TransAnim.tnm");
  if (!scene.Find("Parent.trans") || !animation)
    return Fail(error, "portrait library transition objects changed");
  auto& data = animation->data;
  if (data.size() != 180 || Be32(data.data() + 49) != 6)
    return Fail(error, "portrait library transition keys changed");
  const float horizontal = ui::PortraitSplash(aspect).width / (480 * 16.f / 9);
  for (size_t at = 53; at < 53 + 6 * 16; at += 16)
    PutFloat(data.data() + at, std::bit_cast<float>(Be32(data.data() + at)) * horizontal);
  // Native transition Flows animate Parent.trans. Detached portrait content
  // must inherit that same translation, including newly authored surfaces.
  for (auto& object : scene.objects) {
    const auto matrix = PortraitMatrix(object);
    if (matrix && Be32(object.data.data() + matrix + 105) == 0)
      PortraitParent(object, "Parent.trans");
  }
  return true;
}

void PortraitTitle(PortraitScene& scene, float aspect) {
  if (auto* title = scene.Find("header.lbl")) {
    PortraitPose(*title, 1, 0, ui::kPortraitTitleCenter - scene.root_z);
    PortraitText(*title, 22, ui::PortraitSplash(aspect).width * .94f);
    // Drawable::showing follows the transform's parent and revision.
    title->data[PortraitMatrix(*title) + 113] = 1;
  }
}

void PortraitHiddenGroup(PortraitScene& scene, PortraitScene& templates,
                         std::string name, const std::vector<std::string>& members) {
  auto group = *templates.Find("options_menu.grp");
  group.name = std::move(name);
  PortraitPose(group, 1, 0, 0);
  group.data[PortraitMatrix(group) + 113] = 0;
  constexpr size_t count_at = 0xAB;
  size_t end = count_at + 4;
  for (uint32_t i = 0; i < Be32(group.data.data() + count_at); ++i)
    end += 4 + Be32(group.data.data() + end);
  std::vector<uint8_t> added;
  SceneWord(added, uint32_t(members.size()));
  for (const auto& member : members) SceneString(added, member);
  group.data.erase(group.data.begin() + count_at, group.data.begin() + end);
  group.data.insert(group.data.begin() + count_at, added.begin(), added.end());
  scene.objects.push_back(std::move(group));
}

void PortraitSurface(PortraitScene& scene, const std::vector<uint8_t>& buttons,
                     std::string name, float width, float height, float x, float z,
                     std::string_view before, bool loading = false) {
  std::vector<std::string> members;
  PortraitScene templates(buttons);
  if (!scene.Find("portrait_caption_black.color")) {
    auto color = *templates.Find("active_normal.color");
    color.name = "portrait_caption_black.color";
    SceneColor(color.data, 17, 0, 0, 0, 1);
    scene.objects.push_back(std::move(color));
  }
  for (bool border : {true, false}) {
    const auto stem = name + (border ? "_border" : "");
    auto material = templates.Find("slot.mat")->data;
    RenameSceneString(material, "options_slot.tex", "");
    PutBe32(material.data() + 0x15, 3); // Source-alpha blend, as on the menu rows.
    const auto color = loading ? ui::LoadingButtonColor(border) : ui::FooterButtonColor(border);
    SceneColor(material, 0x19, color[0], color[1], color[2], color[3]);
    material[0x29] = 0; // Unlit, untextured native surface.
    auto mesh = templates.Find("slot.mesh")->data;
    const float w = width + (border ? 1.4f : 0), h = height + (border ? 1.4f : 0);
    for (size_t matrix : {size_t(0x15), size_t(0x45)}) {
      PutFloat(mesh.data() + matrix, w / 300);
      PutFloat(mesh.data() + matrix + 20, h / 30);
      PutFloat(mesh.data() + matrix + 36, x - w / 2);
      PutFloat(mesh.data() + matrix + 40, border ? .2f : .1f);
      PutFloat(mesh.data() + matrix + 44, z + h / 2 - scene.root_z);
    }
    RenameSceneString(mesh, "options_menu.grp", "");
    RenameSceneString(mesh, "slot.mat", stem + ".mat");
    RenameSceneString(mesh, "slot.mesh", stem + ".mesh");
    members.push_back(stem + ".mesh");
    scene.objects.push_back({"Mat", stem + ".mat", std::move(material)});
    scene.objects.push_back({"Mesh", stem + ".mesh", std::move(mesh)});
  }
  if (!before.empty()) scene.Before(before, members);
  else {
    // A hidden draw group keeps these meshes out of the directory's automatic
    // traversal. The live InlineHelp draw invokes their native mesh draw only
    // for actions that are present, including after calibration state changes.
    PortraitHiddenGroup(scene, templates, name + "_surface.grp", members);
  }
}

void PortraitStoreDialogFrame(PortraitScene& scene, PortraitScene& templates,
                              const ui::PortraitStoreDialogLayout& layout) {
  // The native slot quad supplies the packed vertex format and XZ transform.
  // Render three rounded rails with a bright core and red falloff, like the
  // search keyboard. Each rail fits in one native face group.
  constexpr size_t first_vertex = 222, vertex_size = 36, points = 20;
  constexpr size_t face_group = first_vertex + 4 * vertex_size + 4 + 2 * 3 * 2;
  const auto& source = templates.Find("slot.mesh")->data;
  const auto add = [&](const std::string& name, float inset, bool fill) {
    auto mesh = source;
    mesh.resize(first_vertex);
    const auto vertex = [&](float x, float y, uint32_t color) {
      const size_t offset = mesh.size();
      mesh.insert(mesh.end(), source.begin() + first_vertex,
                  source.begin() + first_vertex + vertex_size);
      PutFloat(mesh.data() + offset, x);
      PutFloat(mesh.data() + offset + 4, y);
      PutFloat(mesh.data() + offset + 8, 0);
      PutBe32(mesh.data() + offset + 12, color);
    };
    const auto contour = [&](float edge, uint32_t color) {
      constexpr float quarter_turn = 1.570796327f;
      const float radius = std::max(1.5f, 8.f - .38f * edge);
      const float right = layout.width / 2 - edge - radius;
      const float top = layout.height / 2 - edge - radius;
      for (int corner = 0; corner < 4; ++corner) {
        const float x = (corner == 0 || corner == 3) ? right : -right;
        const float y = corner < 2 ? top : -top;
        for (int step = 0; step < 5; ++step) {
          const float angle = (corner + step / 4.f) * quarter_turn;
          vertex(x + radius * std::cos(angle), y + radius * std::sin(angle), color);
        }
      }
    };
    if (fill) {
      constexpr uint32_t background = 0xFC0A0608;
      contour(0, background);
      vertex(0, 0, background);
    } else {
      contour(inset - 2.6f, 0x00FF121C);
      contour(inset - 1.0f, 0xADFF121C);
      contour(inset, 0xFFFFB3A6);
      contour(inset + 1.0f, 0xADFF121C);
      contour(inset + 2.6f, 0x00FF121C);
    }
    PutBe32(mesh.data() + first_vertex - 13, fill ? points + 1 : points * 5);
    const auto triangle = [&](size_t a, size_t b, size_t c) {
      for (const size_t index : {a, b, c}) {
        mesh.push_back(uint8_t(index >> 8));
        mesh.push_back(uint8_t(index));
      }
    };
    const size_t faces = fill ? points : points * 8;
    SceneWord(mesh, uint32_t(faces));
    for (size_t point = 0; point < points; ++point) {
      const size_t next = (point + 1) % points;
      if (fill) triangle(points, point, next);
      else for (size_t band = 0; band < 4; ++band) {
        const size_t outer = band * points, inner = outer + points;
        triangle(outer + point, outer + next, inner + point);
        triangle(outer + next, inner + next, inner + point);
      }
    }
    const size_t footer = mesh.size();
    mesh.insert(mesh.end(), source.begin() + face_group, source.end());
    mesh[footer + 4] = uint8_t(faces);
    for (size_t matrix : {size_t(0x15), size_t(0x45)}) {
      PutFloat(mesh.data() + matrix + 36, 0);
      PutFloat(mesh.data() + matrix + 40, fill ? .2f : .1f);
      PutFloat(mesh.data() + matrix + 44, 0);
    }
    RenameSceneString(mesh, "options_menu.grp", "");
    RenameSceneString(mesh, "slot.mat", "portrait_dialog_frame.mat");
    RenameSceneString(mesh, "slot.mesh", name);
    scene.objects.push_back({"Mesh", name, std::move(mesh)});
    scene.Before("error.lbl", {name});
  };
  auto material = *templates.Find("slot.mat");
  material.name = "portrait_dialog_frame.mat";
  RenameSceneString(material.data, "options_slot.tex", "");
  PutBe32(material.data.data() + 0x15, 3);
  SceneColor(material.data, 0x19, 1, 1, 1, 1);
  material.data[0x29] = 0;
  material.data[0x2A] = 1;
  scene.objects.push_back(std::move(material));
  add("portrait_dialog_panel.mesh", 0, true);
  for (int rail = 0; rail < 3; ++rail)
    add("portrait_dialog_rail_" + std::to_string(rail) + ".mesh", 3.2f + rail * 3.8f, false);
}

void PortraitFooter(PortraitScene& scene, const std::vector<uint8_t>& buttons,
                    float aspect, bool back_only = false, bool confirm_only = false,
                    bool recommendations = false) {
  auto original = std::find_if(scene.objects.begin(), scene.objects.end(),
      [](const auto& object) { return object.type == "InlineHelp"; });
  if (original == scene.objects.end()) return;
  // Keep the original widget and its changing actions. The draw bridge places
  // each live label and draws only its corresponding native surface.
  PortraitPose(*original, 1, 0, -197 - scene.root_z);
  RenameSceneString(original->data, "../resource/inline_help/default.milo",
                      "../resource/inline_help/portrait_actions.milo");
  if (confirm_only) RenameSceneString(original->data, "select", "rbbz_loading_retry");
  if (back_only || confirm_only) {
    auto& data = original->data;
    size_t at = 13;
    for (uint32_t i = 0; i < Be32(data.data() + 9);) {
      const auto end = at + 12 + Be32(data.data() + at + 4);
      const bool back = Be32(data.data() + at) == 2;
      if (back_only ? back : !back) { at = end; ++i; }
      else {
        data.erase(data.begin() + at, data.begin() + end);
        PutBe32(data.data() + 9, Be32(data.data() + 9) - 1);
      }
    }
  }
  for (size_t i = 0; i < (recommendations ? 2u : 3u); ++i) {
    const auto slot = recommendations ? ui::PortraitRecommendationsFooter(aspect, i, 2) :
                                         ui::PortraitCalibrationFooter(aspect, i);
    PortraitSurface(scene, buttons,
        recommendations ? ui::PortraitRecommendationFooterSurface(i) :
        i == 0 ? "portrait_confirm" : i == 1 ? "portrait_back" : "portrait_retry",
        slot.width, slot.height, slot.x, slot.z, {});
  }
}

void FitPortraitPauseFrame(std::vector<uint8_t>& bytes, const ui::PortraitPauseLayout& layout) {
  constexpr size_t neon_transform = 0xA0C;
  const float scale = layout.width / 400;
  for (const auto matrix : {neon_transform, neon_transform + 48})
    SceneMatrix(bytes, matrix, scale, scale);

  // The texture's inner rail is 77 units in from the top and bottom of the
  // original quad. Stretch the center strip while preserving the outer
  // quarters at the same scale as the sides, including the glow at each corner.
  const float half_height = layout.height / (2 * scale) + 77;
  const std::array<float, 4> row_z{half_height, half_height - 100,
                                 100 - half_height, -half_height};
  constexpr std::array<uint8_t, 4> texture_v{0x00, 0x34, 0x3A, 0x3C};  // Half floats: 0, ¼, ¾, 1.
  constexpr size_t vertex_size = 36;
  constexpr size_t original_faces = 4 + 2 * 3 * 2;
  // The background and three light meshes are identical packed quads in
  // the embedded popup. Work backwards so each original offset stays valid.
  for (const size_t first_vertex : {0x2C64, 0x2AD1, 0x2880, 0x26ED}) {
    std::vector<uint8_t> geometry;
    const auto begin = bytes.begin() + first_vertex;
    const auto faces = begin + 4 * vertex_size;
    const auto face_group = faces + original_faces;
    for (size_t row = 0; row < row_z.size(); ++row) {
      for (const size_t side : {0, 2}) {
        const auto vertex = begin + side * vertex_size;
        const auto offset = geometry.size();
        geometry.insert(geometry.end(), vertex, vertex + vertex_size);
        PutFloat(geometry.data() + offset + 8, row_z[row]);
        geometry[offset + 18] = texture_v[row];
        geometry[offset + 19] = 0;
      }
    }
    SceneWord(geometry, 6);
    for (uint8_t right_top = 0; right_top < 6; right_top += 2) {
      const uint8_t left_top = right_top + 1;
      const uint8_t right_bottom = right_top + 2;
      const uint8_t left_bottom = right_top + 3;
      for (const auto index : {right_bottom, right_top, left_bottom, left_top, left_bottom, right_top}) {
        geometry.push_back(0);
        geometry.push_back(index);
      }
    }
    PutBe32(bytes.data() + first_vertex - 13, 8);
    face_group[4] = 6;  // All six triangles share the native face group.
    const auto end = bytes.erase(begin, face_group);
    bytes.insert(end, geometry.begin(), geometry.end());
  }
}
}  // namespace

bool PreparePortraitStartupLogo(const std::vector<uint8_t>& source,
                               std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  // The bitmap has already been replaced with Ultimate art. Validate the
  // unchanged material/geometry tail, independent of those image bytes.
  constexpr size_t geometry = 0x55850;
  if (bytes.size() != 351617 ||
      Crc32(bytes.data() + geometry, bytes.size() - geometry) != 0x4AA736EA)
    return Fail(error, "portrait startup logo source revision changed");
  // Startup scenes still fit the landscape image to the window width. Zoom
  // the native group uniformly so the square wordmark occupies most of it.
  for (size_t matrix : {size_t(0x55A90), size_t(0x55AC0)}) {
    SceneMatrix(bytes, matrix, 1.5f, 1.5f);
    PutFloat(bytes.data() + matrix + 16, 1.5f);
  }
  return PackStored(bytes, scene, error);
}

bool PreparePortraitCanvas(const std::vector<uint8_t>& source,
                           std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error, 16 * 1024 * 1024)) return false;
  // Fonts, nested list resources and ordinary RndDirs keep their own coordinate
  // spaces. Only the owning UI panel receives the runtime safe-area transform.
  constexpr std::string_view type = "PanelDir";
  if (bytes.size() < 8 + type.size() || Be32(bytes.data() + 4) != type.size() ||
      std::memcmp(bytes.data() + 8, type.data(), type.size())) {
    *scene = source;
    return true;
  }
  PortraitScene edited(bytes);
  for (auto& object : edited.objects) {
    // Trans records include Parent.trans, whose children already inherit the
    // directory through it. Never reparent an existing animation hierarchy.
    const auto matrix = object.type == "Trans" ? size_t(0x11) : PortraitMatrix(object);
    if (!matrix) continue;
    const auto parent = matrix + 105;
    if (parent + 4 > object.data.size() ||
        Be32(object.data.data() + parent) > object.data.size() - parent - 4)
      return Fail(error, "portrait canvas transform parent changed");
    if (Be32(object.data.data() + parent) != 0) continue;
    if (object.type == "Trans") {
      PutBe32(object.data.data() + parent, uint32_t(edited.root.size()));
      object.data.insert(object.data.begin() + parent + 4, edited.root.begin(), edited.root.end());
    } else {
      PortraitParent(object, edited.root);
    }
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitAutosave(const std::vector<uint8_t>& source, float aspect,
                            std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 532432 || Crc32(bytes.data(), bytes.size()) != 0xBDCAB5B3)
    return Fail(error, "portrait autosave source revision changed");
  // Keep native localization and wrapping. The panel's enter method positions
  // the icon and label together after measuring the wrapped text height.
  PutFloat(bytes.data() + 0x81BAB, ui::PortraitSplash(aspect).width * .84f);
  constexpr size_t text_size = 0x81BF1;
  PutFloat(bytes.data() + text_size,
           std::bit_cast<float>(Be32(bytes.data() + text_size)) * .55f);
  return PackStored(bytes, scene, error);
}

bool PreparePortraitPrompt(const std::vector<uint8_t>& source, float aspect,
                           std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  const auto crc = Crc32(bytes.data(), bytes.size());
  const bool confirmation = bytes.size() == 13645 && crc == 0x7EFF956D;
  const bool popup = bytes.size() == 2803318 && crc == 0x6790B076;
  if (!confirmation && !popup) return Fail(error, "portrait prompt source revision changed");
  const float scale = ui::PortraitPromptScale(aspect, popup);
  // Scale the content's transform parent. The confirmation's full-screen
  // Shade.mesh belongs to its draw group but has no transform parent, so it
  // continues dimming the entire portrait background.
  const size_t matrix = confirmation ? 0x2DF8 : 0x2AC307;
  for (size_t at : {matrix, matrix + 48})
    for (size_t axis : {size_t(0), size_t(16), size_t(32)})
      PutFloat(bytes.data() + at + axis, scale);
  if (confirmation) {
    // on.anim animates Group.grp::scale on every entry. Scale each vector key,
    // preserving its times and easing, so the animation cannot undo the fit.
    for (size_t key = 0; key < 3; ++key)
      for (size_t axis = 0; axis < 3; ++axis) {
        const size_t at = 0x32B7 + key * 16 + axis * 4;
        PutFloat(bytes.data() + at, std::bit_cast<float>(Be32(bytes.data() + at)) * scale);
      }
  }
  return PackStored(bytes, scene, error);
}

bool PreparePortraitSettingsPrompt(const std::vector<uint8_t>& source, float aspect,
                                   std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 13645 || Crc32(bytes.data(), bytes.size()) != 0x7EFF956D)
    return Fail(error, "portrait settings prompt source revision changed");
  PortraitScene edited(bytes);
  const float width = ui::PortraitSplash(aspect).width * .84f;
  // Retain Group.grp and its entrance animation at native scale. Message wrapping
  // and button geometry have separate fits; the full-screen shade stays untouched.
  auto& message = *edited.Find("pause.lbl");
  PortraitPose(message, 1, 0, 66, "Group.grp");
  PortraitText(message, 16, width);
  auto& list = *edited.Find("options.lst");
  PortraitPose(list, width / 364.5f, 0, -26, "Group.grp");
  RenameSceneString(list.data, "../resource/lists/list_popup.milo",
                    "../resource/lists/portrait_prompt.milo");
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitPromptList(const std::vector<uint8_t>& source,
                               const std::vector<uint8_t>& button_source,
                               std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> fitted, bytes;
  if (!PreparePortraitSplashList(source, button_source, &fitted, error) ||
      !DecodeStream(fitted, &bytes, error)) return false;
  PortraitScene edited(bytes);
  // Preserve the popup provider's field name on the shared portrait button style.
  auto* label = edited.Find("song");
  if (!label || label->type != "UIListLabel")
    return Fail(error, "portrait prompt list label widget changed");
  label->name = "list";
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitPause(const std::vector<uint8_t>& source,
                          const std::vector<uint8_t>& button_source, float aspect,
                          std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error))
    return false;
  if (bytes.size() != 364849 || Crc32(bytes.data(), bytes.size()) != 0x7A8DF8A9)
    return Fail(error, "portrait pause source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait pause button template revision changed");
  const auto layout = ui::PortraitPause(aspect);
  FitPortraitPauseFrame(bytes, layout);
  PortraitScene edited(bytes);
  // Keep these objects in a hidden draw group so the panel directory cannot
  // pick them up as independent drawables.
  const std::vector<std::string> hidden{"main_bg1.mesh", "select.ihp"};
  for (const auto& name : hidden) edited.RemoveDrawMember(name);
  PortraitScene templates(buttons);
  PortraitHiddenGroup(edited, templates, "portrait_pause_hidden.grp", hidden);
  auto& title = *edited.Find("pause.lbl");
  PortraitPose(title, 1, 0, layout.title_z, "Group.grp");
  PortraitText(title, layout.title_size, layout.width - 24);
  RenameSceneString(title.data, "../resource/fonts/pentatonic.milo",
                    "../resource/fonts/portrait_splash.milo");
  for (const char* name : {"pause.lst", "options.lst"}) {
    auto& list = *edited.Find(name);
    PortraitPose(list, layout.list_scale, 0, layout.list_z, "Group.grp");
    RenameSceneString(list.data, "../resource/lists/list_popup.milo",
                      "../resource/lists/portrait_prompt.milo");
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitSettings(const std::vector<uint8_t>& source,
                             const std::vector<uint8_t>& button_source, float aspect,
                             std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error))
    return false;
  if (bytes.size() != 17455 || Crc32(bytes.data(), bytes.size()) != 0x0E190262)
    return Fail(error, "portrait settings source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait settings button template revision changed");
  PortraitScene edited(bytes);
  PortraitTitle(edited, aspect);
  PortraitPose(*edited.Find("options.lst"), ui::PortraitSplash(aspect).list_scale, 0, 77);
  RenameSceneString(edited.Find("options.lst")->data, "../resource/lists/list_options_main.milo",
                    "../resource/lists/list_splash.milo");
  PortraitFooter(edited, buttons, aspect, true);
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitAudio(const std::vector<uint8_t>& source,
                         const std::vector<uint8_t>& button_source, float aspect,
                         std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 45314 || Crc32(bytes.data(), bytes.size()) != 0xD42E4A77)
    return Fail(error, "portrait audio source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait audio button template revision changed");
  PortraitScene edited(bytes);
  const auto layout = ui::PortraitSplash(aspect);
  PortraitPose(*edited.Find("content.grp"), 1, 0, 0);
  for (const char* name : {"sfx.grp", "track_vol.grp", "vid_options.grp"})
    PortraitPose(*edited.Find(name), 1, 0, 0, "content.grp");
  PortraitTitle(edited, aspect);
  PortraitPose(*edited.Find("checkbox.lst"), layout.list_scale, 0, 77);
  RenameSceneString(edited.Find("checkbox.lst")->data, "../resource/lists/list_options_main.milo",
                    "../resource/lists/list_splash.milo");
  const float wheel_scale = std::min(layout.width * .84f / 190, 1.f);
  for (const char* name : {"sound_effects_slider.sld", "track_sound_slider.sld"})
    PortraitPose(*edited.Find(name), wheel_scale, -88.75f * wheel_scale, -100);
  PortraitFooter(edited, buttons, aspect);
  return PackStored(edited.Build(), scene, error);
}

bool PrepareRenderScaleScene(const std::vector<uint8_t>& source,
                            const std::vector<uint8_t>& button_source, float aspect, bool portrait,
                            std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, prepared;
  if (portrait) {
    if (!PreparePortraitAudio(source, button_source, aspect, &prepared, error) ||
        !DecodeStream(prepared, &bytes, error)) return false;
  } else {
    if (!DecodeStream(source, &bytes, error)) return false;
    if (bytes.size() != 45314 || Crc32(bytes.data(), bytes.size()) != 0xD42E4A77)
      return Fail(error, "render scale scene source revision changed");
  }
  PortraitScene edited(bytes);
  // Keep the native Audio buttons and Select/Back footer. Both profiles share
  // the bottom wheel; its value label sits above it, clear of help and footer.
  const float width = portrait ? ui::PortraitSplash(aspect).width : 640.f;
  const float scale = std::min(width * .84f / 190, 1.f);
  for (const char* slider : {"sound_effects_slider.sld", "track_sound_slider.sld"})
    PortraitPose(*edited.Find(slider), scale, -88.75f * scale, -100);
  auto label = *edited.Find("header.lbl");
  label.name = "scale_value.lbl";
  PortraitPose(label, 1, 0, -70);
  PortraitText(label, 16, width * .84f);
  label.data[PortraitMatrix(label) + 113] = 1;
  edited.Before("track_sound_slider.sld", {label.name});
  edited.objects.push_back(std::move(label));
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitCredits(const std::vector<uint8_t>& source,
                           const std::vector<uint8_t>& button_source, float aspect,
                           std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error, 12 * 1024 * 1024) ||
      !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 9379309 || Crc32(bytes.data(), bytes.size()) != 0x120A5308)
    return Fail(error, "portrait credits source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait credits button template revision changed");
  PortraitScene edited(bytes);
  PortraitPose(*edited.Find("credits.lst"), 1, 0, 147.09f);
  PortraitFooter(edited, buttons, aspect);
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitCreditsList(const std::vector<uint8_t>& source, float aspect,
                               std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 358583 || Crc32(bytes.data(), bytes.size()) != 0x0A08F5BC)
    return Fail(error, "portrait credits list source revision changed");
  PortraitScene edited(bytes);
  const float width = ui::PortraitSplash(aspect).width * .90f;
  for (auto& object : edited.objects) {
    if (object.type != "UILabel") continue;
    const bool column = object.name == "slot_left.lbl" || object.name == "slot_right.lbl";
    if (column) {
      for (size_t matrix : {size_t(0x19), size_t(0x49)})
        PutFloat(object.data.data() + matrix + 36, object.name == "slot_left.lbl" ? -6.f : 6.f);
    }
    PortraitText(object, column ? 13.f : 16.f, column ? (width - 12) / 2 : width, UINT32_MAX);
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitActions(const std::vector<uint8_t>& source, float aspect,
                            std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> hinted, bytes;
  if (!PrepareInlineHelp(source, &hinted, error) || !DecodeStream(hinted, &bytes, error)) return false;
  PortraitScene edited(bytes);
  auto* text = edited.Find("text.lbl");
  PortraitText(*text, 27.5f * ui::PortraitSplash(aspect).list_scale,
               ui::PortraitFooterAction(aspect, 0, 2).width * .94f);
  constexpr std::string_view icon_font = "../fonts/inline_help_icons.milo";
  const auto icon = std::search(text->data.begin(), text->data.end(), icon_font.begin(), icon_font.end());
  if (icon == text->data.end()) return Fail(error, "portrait action icon style changed");
  const auto color = size_t(icon - text->data.begin()) + icon_font.size();
  PutFloat(text->data.data() + color + 4 + Be32(text->data.data() + color),
           20 * ui::PortraitSplash(aspect).list_scale / .55f);
  RenameSceneString(text->data, "../fonts/pentatonic_regularcond.milo", "../fonts/portrait_splash.milo");
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitControls(const std::vector<uint8_t>& source,
                             const std::vector<uint8_t>& button_source,
                             const std::vector<uint8_t>& welcome_source, float aspect,
                             std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error, 3 * 1024 * 1024) ||
      !DecodeStream(button_source, &buttons, error)) return false;
  const auto crc = Crc32(bytes.data(), bytes.size());
  if (crc != 0xDB1E96A0 && crc != 0x375728BA && crc != 0xDBAD7872 &&
      crc != 0x61CF95FD && crc != 0x06D6D651 && crc != 0x75394D35 &&
      crc != 0xF522F89D)
    return Fail(error, "portrait controls source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait controls button template revision changed");
  const float width = ui::PortraitSplash(aspect).width;
  const bool presets = crc == 0xF522F89D;
  const bool welcome = crc == 0xDB1E96A0;
  PortraitScene edited(bytes);
  // Body transforms remain separate from the fixed title and footer. Root
  // scaling is only the fallback for native calibration art, not menu controls.
  const float scale = std::min(width * (presets ? .94f / 540.f : .90f / 600.f), 1.f);
  // Panel loading can reset the directory transform. Author content against an
  // identity root so its loaded and runtime poses agree.
  if (welcome)
    for (size_t offset : {size_t(0x214), size_t(0x244)})
      PutFloat(edited.directory.data() + offset, 0);
  for (auto& object : edited.objects) {
    const auto matrix = PortraitMatrix(object);
    if (!matrix || object.type == "InlineHelp" || object.name == "header.lbl") continue;
    const auto parent_at = matrix + 105;
    const auto parent_size = Be32(object.data.data() + parent_at);
    if (parent_at + 4 + parent_size > object.data.size())
      return Fail(error, "portrait controls transform parent changed");
    const std::string_view parent(reinterpret_cast<const char*>(object.data.data() + parent_at + 4), parent_size);
    if (parent.empty() || parent == edited.root) {
      for (size_t m : {matrix, matrix + 48})
        for (size_t field : {size_t(0), size_t(4), size_t(8), size_t(24), size_t(28),
                             size_t(32), size_t(36), size_t(44)})
          PutFloat(object.data.data() + m + field,
                   std::bit_cast<float>(Be32(object.data.data() + m + field)) * scale);
      for (size_t m : {matrix, matrix + 48})
        PutFloat(object.data.data() + m + 44,
                 std::bit_cast<float>(Be32(object.data.data() + m + 44)) + edited.root_z * (scale - 1));
    }
  }
  if (crc == 0x61CF95FD || crc == 0x75394D35) {
    std::vector<uint8_t> common;
    if (!DecodeStream(welcome_source, &common, error) ||
        Crc32(common.data(), common.size()) != 0xDB1E96A0)
      return Fail(error, "portrait calibration chrome source revision changed");
    PortraitScene chrome(common);
    if (!edited.Find("header.lbl")) edited.objects.push_back(*chrome.Find("header.lbl"));
    if (std::none_of(edited.objects.begin(), edited.objects.end(),
        [](const auto& object) { return object.type == "InlineHelp"; }))
      edited.objects.push_back(*chrome.Find("InlineHelp.ihp"));
  }
  if (crc == 0x375728BA) {
    // The native audio/video groups already select the localized stage title.
    // Keep that draw membership when moving the titles to the marquee.
    auto& header = *edited.Find("header.lbl");
    PortraitParent(header, "");
    header.data[PortraitMatrix(header) + 113] = 0;
    edited.RemoveDrawMember(header.name);
    for (const char* name : {"audio_title.lbl", "video_title.lbl"}) {
      auto& title = *edited.Find(name);
      PortraitPose(title, 1, 0, ui::kPortraitTitleCenter);
      PortraitText(title, 22, 0);
    }
    for (const char* name : {"cal_audio_instructions.lbl", "cal_video_instructions.lbl"}) {
      auto& description = *edited.Find(name);
      PortraitPose(description, 1, 0, ui::kPortraitCalibrationInstructionsZ);
      PortraitText(description, 11.5f, width * .84f);
    }
    struct Label { const char* name; float z, size; };
    for (const auto& label : {Label{"cal_countdown.lbl", ui::kPortraitCalibrationSpeakerZ, 40},
                              {"cal_instructions.lbl", ui::kPortraitCalibrationStatusZ, 10.5f},
                              {"cal_reps.lbl", ui::kPortraitCalibrationHitCountZ, 10},
                              {"cal_setting.lbl", ui::kPortraitCalibrationOffsetZ, 11}}) {
      auto* object = edited.Find(label.name);
      if (!object) continue;
      PortraitPose(*object, 1, 0, label.z);
      PortraitText(*object, label.size, width * .84f);
    }
    // Fit each visual independently, preserving the metronome's XY-to-XZ
    // rotation and the progress bar's bone/texture animations.
    const auto art = [&](const char* name, float primary_scale, float x, float z) {
      auto& object = *edited.Find(name);
      const auto matrix = PortraitMatrix(object);
      const float factor = primary_scale / std::bit_cast<float>(Be32(object.data.data() + matrix));
      for (size_t m : {matrix, matrix + 48}) {
        for (size_t field = 0; field < 36; field += 4)
          PutFloat(object.data.data() + m + field,
                   std::bit_cast<float>(Be32(object.data.data() + m + field)) * factor);
        PutFloat(object.data.data() + m + 36, x);
        PutFloat(object.data.data() + m + 44, z);
      }
      PortraitParent(object, "");
    };
    const float fit = width / 270;
    // Constrain the video art and progress bar on tablets so they fit between
    // the description and the hit count without overlapping the circle.
    const float art_width = std::min(width, 270.f);
    // Resting speaker alpha bounds (4,65)-(139,205) mapped through the quad's
    // UVs; sound waves expand to its right without moving the speaker body.
    art("audio_anim.grp", .65f * fit, -223.3656f * .65f * fit,
        ui::kPortraitCalibrationSpeakerZ - 39.78075f * .65f * fit);
    const float progress = 1.18f * art_width / 270;
    art("prog_bar.grp", progress, -.3215f * progress,
        ui::kPortraitCalibrationProgressZ + 65.4355f * progress);
    const float metro = art_width * .64f / 599.163f;
    art("cal_metronome.grp", metro, -1.3685f * metro, ui::kPortraitCalibrationMeterZ + 73.7405f * metro);
    const float background = art_width * .64f / 345.003f;
    art("cal_bg.grp", background, .8785f * background, ui::kPortraitCalibrationMeterZ + 84.6205f * background);
    const float visual = art_width * .65f / 511.003f;
    art("visuals_anim.grp", visual, -176.5365f * visual, ui::kPortraitCalibrationMeterZ - 296.4875f * visual);
    // Give the pressed/released prompt its own centered row above the meter.
    for (const char* name : {"button_press_xbox.mesh", "button_press_ps3.mesh"}) {
      art(name, 1.5573348f * visual * .5f, 0, ui::kPortraitCalibrationPromptZ);
      auto& object = *edited.Find(name);
      const auto matrix = PortraitMatrix(object);
      std::copy_n(object.data.data() + matrix, 48, object.data.data() + matrix + 48);
    }
    for (const char* name : {"cal_hardware_audio_illustration.grp", "cal_hardware_video_illustration.grp"})
      art(name, .75f * fit, 0, 30);
  } else PortraitTitle(edited, aspect);
  if (crc == 0xDBAD7872) {
    PortraitPose(*edited.Find("labels.grp"), .8f * width / 270, 0, -10);
    for (const char* name : {"audio.lbl", "video.lbl"}) {
      PortraitPose(*edited.Find(name), 1, 0, 117);
      PortraitText(*edited.Find(name), 16, width * .84f);
    }
    PortraitPose(*edited.Find("current_offset.lbl"), 1, 0, -105);
    PortraitText(*edited.Find("current_offset.lbl"), 22, width * .84f);
  }
  if (crc == 0x06D6D651) {
    for (const auto& [name, z] : {std::pair{"audio_offset.lbl", 45.f}, {"video_offset.lbl", -25.f}}) {
      PortraitPose(*edited.Find(name), 1, 0, z);
      PortraitText(*edited.Find(name), 16, width * .84f);
    }
  }
  if (auto* failed = edited.Find("failed.lbl")) {
    PortraitPose(*failed, 1, 0, -112);
    PortraitText(*failed, 12, width * .84f);
  }
  if (welcome) {
    auto* list = edited.Find("cal_mode.lst");
    PortraitPose(*list, ui::PortraitSplash(aspect).list_scale, 0, 77 - edited.root_z);
    RenameSceneString(list->data, "../resource/lists/list_calibration_mode.milo",
                      "../resource/lists/portrait_calibration.milo");
    // The secondary title duplicates the marquee's localized heading.
    auto* secondary = edited.Find("title.lbl");
    PortraitParent(*secondary, "");
    secondary->data[PortraitMatrix(*secondary) + 113] = 0;
  }
  if (presets) {
    // Fit the complete controller diagram and its preset-specific callouts as
    // one unit. Their leader lines must continue identifying the right controls.
    for (auto& object : edited.objects) {
      if (object.type != "UILabel" || object.name == "header.lbl") continue;
      if (object.name == "IndexNum.lbl") {
        PortraitPose(object, 1, 0, 110);
        PortraitText(object, 16, width * .50f);
      } else {
        PortraitText(object, 18, -1, UINT32_MAX);
      }
    }
    const float x = width * .36f;
    for (const char* name : {"arrow_lt.mesh", "arrow_lt_glow.mesh", "arrow_rt.mesh", "arrow_rt_glow.mesh"}) {
      auto* arrow = edited.Find(name);
      const float side = std::string_view(name).find("_lt") != std::string_view::npos ? -1 : 1;
      // The authored arrow quad is 64 units tall, including its transparent
      // margins. Fit the complete quad inside the 38-unit touch target.
      PortraitPose(*arrow, .45f, side * x, 110);
    }
    PortraitSurface(edited, buttons, "portrait_previous", 38, 38, -x, 110, {});
    PortraitSurface(edited, buttons, "portrait_next", 38, 38, x, 110, {});
    const std::vector<std::string> arrows{
        "arrow_lt.mesh", "arrow_lt_glow.mesh", "arrow_rt.mesh", "arrow_rt_glow.mesh"};
    for (const auto& arrow : arrows) edited.RemoveDrawMember(arrow);
    PortraitScene templates(buttons);
    // Draw these native meshes beside the live preset label so hover colors
    // apply to the same frame as their background, without a second traversal.
    PortraitHiddenGroup(edited, templates, "portrait_preset_arrows.grp", arrows);
  }
  if (crc == 0x375728BA) {
    // Calibration replaces nav.ihp's actions during countdown, measurement and
    // retry. Keep this single live widget; its draw bridge reflows its labels.
    auto& nav = *edited.Find("nav.ihp");
    PortraitPose(nav, 1, 0, -197);
    RenameSceneString(nav.data, "../resource/inline_help/default.milo",
                      "../resource/inline_help/portrait_actions.milo");
    for (size_t i = 0; i < 3; ++i) {
      const auto slot = ui::PortraitCalibrationFooter(aspect, i);
      PortraitSurface(edited, buttons,
          i == 0 ? "portrait_confirm" : i == 1 ? "portrait_back" : "portrait_retry",
          slot.width, slot.height, slot.x, slot.z, {});
    }
  } else {
    PortraitFooter(edited, buttons, aspect, crc == 0x61CF95FD, crc == 0x75394D35);
  }
  if (!presets) {
    auto marker = *edited.Find("portrait_caption_black.color");
    marker.name = "portrait_calibration_footer.color";
    edited.objects.push_back(std::move(marker));
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitCalibrationList(const std::vector<uint8_t>& source,
                                    const std::vector<uint8_t>& button_source,
                                    std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> fitted, bytes;
  if (!PreparePortraitSplashList(source, button_source, &fitted, error) ||
      !DecodeStream(fitted, &bytes, error)) return false;
  PortraitScene edited(bytes);
  auto* label = edited.Find("song");
  if (!label || label->type != "UIListLabel") return Fail(error, "portrait list label widget changed");
  // CalibrationModesProvider fills the widget called name, rather than song.
  label->name = "name";
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitSongLoading(const std::vector<uint8_t>& source, float aspect,
                                std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 9828070 || Crc32(bytes.data(), bytes.size()) != 0x3DCC8337)
    return Fail(error, "portrait song loading source revision changed");
  PortraitScene edited(bytes);
  const auto layout = ui::PortraitSongLoading(aspect);
  const float scale = layout.scene_scale;
  constexpr uint32_t top_center = 0x12, middle_center = 0x22;
  const auto place = [&](const char* name, float x, float z, std::string_view parent) {
    PortraitPose(*edited.Find(name), 1, x / scale, z / scale, parent);
  };
  const auto style = [&](const char* name, float size, float width, uint32_t alignment,
                         float height) {
    PortraitText(*edited.Find(name), size / scale, width / scale, alignment, height / scale);
  };
  const auto font_style = [](PortraitObject& object, std::string_view font) {
    auto& data = object.data;
    const auto end = size_t(std::search(data.begin(), data.end(), font.begin(), font.end()) -
                            data.begin()) + font.size();
    return data.data() + end + 4 + Be32(data.data() + end);
  };
  constexpr std::string_view buttons = "../resource/fonts/buttons.milo";
  auto& tip = *edited.Find("tip.lbl");
  place("tip.lbl", 0, layout.tip_z, edited.root);
  style("tip.lbl", layout.tip_size, layout.tip_width, middle_center, layout.tip_height);
  PutFloat(font_style(tip, buttons), layout.tip_size * 1.1f / scale);
  // Power-up Flow selects one of two height keys for the tip. Both states
  // share the same wrapping area in portrait.
  auto& height_keys = edited.Find("tip_size.anim")->data;
  for (size_t at : {0x5A, 0x62})
    PutFloat(height_keys.data() + at, layout.tip_height / scale);

  place("Powerups.grp", 0, layout.powerup_z, "all.grp");
  struct PowerupSlot {
    const char* group;
    const char* icon;
    const char* label;
    int column;
  };
  for (const auto& slot : {PowerupSlot{"active.grp", "active_pup.mesh", "name_active_powerup.lbl", -1},
                         {"Passive_a.grp", "passive_a_pup.mesh", "name_passive_powerup_a.lbl", 0},
                         {"Passive_b.grp", "passive_b_pup.mesh", "name_passive_powerup_b.lbl", 1}}) {
    place(slot.group, slot.column * layout.powerup_step, 0, "Powerups.grp");
    // The icon meshes have a 120-unit quad before their local transform.
    PortraitPose(*edited.Find(slot.icon), layout.powerup_radius / (60 * scale), 0, 0, slot.group);
    place(slot.label, 0, -layout.powerup_radius - 9, slot.group);
    style(slot.label, layout.powerup_name_size, layout.powerup_name_width, top_center,
          layout.powerup_name_height);
  }

  place("splash.grp", 0, 0, "all.grp");
  place("load_vert.grp", 0, layout.spinner_z, "splash.grp");
  place("wait.lbl", 0, layout.status_z - layout.spinner_z, "load_vert.grp");
  style("wait.lbl", layout.status_size, layout.status_width, middle_center, layout.status_height);
  auto& wait = *edited.Find("wait.lbl");
  // The native style stores opacity four floats after its text size.
  constexpr size_t style_opacity_offset = 4 * sizeof(float);
  PutFloat(font_style(wait, "../resource/fonts/eagle_book_blitzlabel.milo") +
               style_opacity_offset, .9f);
  // Keep the native spinner and caption pulse, with a readable opacity floor.
  auto& pulse = edited.Find("PropAnim.anim")->data;
  constexpr size_t wait_alpha_min_key = 0xE6;
  PutFloat(pulse.data() + wait_alpha_min_key, .72f);

  place("loading.lbl", 0, layout.status_z, "splash.grp");
  style("loading.lbl", layout.status_size, layout.status_width, middle_center, layout.status_height);
  auto& prompt = *edited.Find("loading.lbl");
  PutFloat(font_style(prompt, buttons), layout.status_size * 1.05f / scale);
  RenameSceneString(prompt.data, "../resource/fonts/pentatonic.milo",
                    "../resource/fonts/pentatonic_regularcond.milo");
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitConnection(const std::vector<uint8_t>& source,
                               const std::vector<uint8_t>& button_source, float aspect,
                               std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error))
    return false;
  if (bytes.size() != 148693 || Crc32(bytes.data(), bytes.size()) != 0x61948B05)
    return Fail(error, "portrait connection source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait connection button template revision changed");
  const auto layout = ui::PortraitLoading(aspect);
  PortraitScene edited(bytes);
  auto color = *edited.Find("UIColor.color");
  color.name = "portrait_action.color";
  SceneColor(color.data, 17, .97f, .97f, .99f, 1);
  edited.objects.push_back(std::move(color));
  auto& status = *edited.Find("status.lbl");
  PortraitText(status, layout.text_size, layout.text_width, 0x42);
  RenameSceneString(status.data, "../resource/fonts/pentatonic_regularcond.milo",
                    "../resource/fonts/portrait_splash.milo");
  for (const char* action : {"continue", "back", "skip"}) {
    const auto hint_name = std::string(action) + ".ihp";
    auto* hint = edited.Find(hint_name);
    RenameSceneString(hint->data, "UIColor.color", "portrait_action.color");
    RenameSceneString(hint->data, "../resource/inline_help/default.milo",
                      "../resource/inline_help/portrait_actions.milo");
    const auto stem = std::string("portrait_") + action;
    PortraitSurface(edited, buttons, stem, layout.button_width / layout.button_scale,
                    30 / layout.button_scale, 0, 0, {}, true);
    for (const char* suffix : {".mesh", "_border.mesh"})
      PortraitParent(*edited.Find(stem + suffix), hint_name);
  }
  // The native pulse animates label scale after entry. Keep its scale stable so
  // measured text height and the progress bar use the same coordinate system.
  auto& pulse = edited.Find("status_pulse.anim")->data;
  for (size_t at = 0; at + 4 <= pulse.size(); ++at)
    if (Be32(pulse.data() + at) == std::bit_cast<uint32_t>(1.25f))
      PutFloat(pulse.data() + at, 1.f);
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitSplash(const std::vector<uint8_t>& source,
                           const std::vector<uint8_t>& button_source, float aspect,
                           std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 10448706 || Crc32(bytes.data(), bytes.size()) != 0x6234458E)
    return Fail(error, "portrait splash art source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait splash button template revision changed");
  auto copy = [&](size_t from, size_t to) {
    return std::vector<uint8_t>(bytes.begin() + from, bytes.begin() + to);
  };
  // Show the authored backdrop's full horizontal color range in portrait.
  // Cropping its bright center discarded the landscape's saturated red edges.
  // Preserve all original material colors, including the city silhouette.
  const float backdrop_x = ui::PortraitSplash(aspect).width / (480 * 16.f / 9);
  for (size_t matrix : {size_t(0x9F63C5 + 0x15), size_t(0x9F63C5 + 0x45)}) {
    PutFloat(bytes.data() + matrix, backdrop_x);
    PutFloat(bytes.data() + matrix + 36, 7.1868896f * backdrop_x);
  }
  // Keep the retail text-only Start prompt, including the alternate icon
  // style's size and baseline relative to the primary text. Shrinking only
  // the text made the A glyph oversized and visibly displaced in portrait.

  auto shade_material = copy(10406977, 10407257);
  RenameSceneString(shade_material, "splash_vig1.tex", "");
  SceneColor(shade_material, 0x19, 0, 0, 0, 1);
  shade_material[0x2A] = 1; // pre_lit enables the packed vertex color/alpha.
  auto shade_mesh = copy(0x9F6B5F, 0x9F6CF9);
  shade_mesh[149] = 1;
  // Preserve the native packed vertex format, normals, UVs, two-sided material
  // and face-group trailer. Eight cells per axis provide a smooth dark edge.
  constexpr size_t vertices = 235, vertex_count = 222;
  const auto vertex_template = std::vector<uint8_t>(shade_mesh.begin() + vertices,
                                                   shade_mesh.begin() + vertices + 36);
  const auto tail = std::vector<uint8_t>(shade_mesh.begin() + 395, shade_mesh.end());
  shade_mesh.resize(vertices);
  PutBe32(shade_mesh.data() + vertex_count, 81);
  const float half_width = ui::PortraitSplash(aspect).width * .5f;
  const auto smooth = [](float v) { return v * v * (3 - 2 * v); };
  for (int z = 0; z <= 8; ++z) for (int x = 0; x <= 8; ++x) {
    const float nx = (x - 4) / 4.f, nz = (z - 4) / 4.f;
    const auto edge = [&](float v) { return smooth(std::max(0.f, (std::abs(v) - .5f) * 2)); };
    const float alpha = .22f * edge(nx) + .07f * edge(nz);
    auto vertex = vertex_template;
    PutFloat(vertex.data(), nx * half_width * 1.08f); PutFloat(vertex.data() + 4, 0);
    PutFloat(vertex.data() + 8, nz * 260);
    PutBe32(vertex.data() + 12, (uint32_t(std::lround(alpha * 255)) << 24) | 0xFFFFFF);
    shade_mesh.insert(shade_mesh.end(), vertex.begin(), vertex.end());
  }
  SceneWord(shade_mesh, 128);
  const auto index = [&](uint16_t v) { shade_mesh.push_back(uint8_t(v >> 8)); shade_mesh.push_back(uint8_t(v)); };
  for (int z = 0; z < 8; ++z) for (int x = 0; x < 8; ++x) {
    const uint16_t a = uint16_t(z * 9 + x), b = uint16_t(a + 9);
    for (auto v : {uint16_t(a + 1), uint16_t(b + 1), a, b, a, uint16_t(b + 1)}) index(v);
  }
  shade_mesh.insert(shade_mesh.end(), tail.begin(), tail.end());
  // The single native face group contains all 128 triangles.
  shade_mesh[shade_mesh.size() - tail.size() + 4] = 128;
  RenameSceneString(shade_mesh, "start_panel.grp", "splash");
  RenameSceneString(shade_mesh, "start_panel.mat", "portrait_shade.mat");
  RenameSceneString(shade_mesh, "start_panel.mesh", "portrait_shade.mesh");
  for (size_t matrix : {size_t(0x15), size_t(0x45)})
    SceneMatrix(shade_mesh, matrix, 1, 1, 0, 45, 0);

  auto group = copy(0x9ED20A, 0x9ED303);
  constexpr std::string_view city = "City_Splash_Elements.grp";
  auto found = std::search(group.begin(), group.end(), city.begin(), city.end());
  const size_t member = size_t(found - group.begin());
  if (found == group.end() || member < 8 || Be32(group.data() + member - 8) != 3)
    return Fail(error, "portrait splash draw group changed");
  PutBe32(group.data() + member - 8, 4);
  std::vector<uint8_t> added_member; SceneString(added_member, "portrait_shade.mesh");
  group.insert(group.begin() + member + city.size(), added_member.begin(), added_member.end());
  bytes.erase(bytes.begin() + 0x9ED20A, bytes.begin() + 0x9ED303);
  bytes.insert(bytes.begin() + 0x9ED20A, group.begin(), group.end());
  const std::array<PortraitObject, 2> added{{
      {"Mat", "portrait_shade.mat", std::move(shade_material)},
      {"Mesh", "portrait_shade.mesh", std::move(shade_mesh)}}};
  std::vector<uint8_t> declarations;
  uint32_t names = 0;
  for (const auto& object : added) {
    SceneString(declarations, object.type); SceneString(declarations, object.name);
    names += uint32_t(object.name.size() + 1);
    bytes.insert(bytes.end(), object.data.begin(), object.data.end());
  }
  PutBe32(bytes.data() + 0x1A, 152);
  PutBe32(bytes.data() + 0x1E, 1103 + names);
  PutBe32(bytes.data() + 0x23, 75);
  bytes.insert(bytes.begin() + 0x7B3, declarations.begin(), declarations.end());
  PortraitScene edited(bytes);
  const auto layout = ui::PortraitLoading(aspect);
  for (const char* action : {"continue", "back"}) {
    const auto hint_name = std::string(action) + ".ihp";
    auto& hint = *edited.Find(hint_name);
    PortraitPose(hint, 1, std::string_view(action) == "continue" ? -layout.button_x : layout.button_x, -197);
    RenameSceneString(hint.data, "../resource/inline_help/default.milo",
                      "../resource/inline_help/portrait_actions.milo");
    const auto stem = std::string("portrait_") + action;
    PortraitSurface(edited, buttons, stem, layout.button_width, 30, 0, 0, {});
    for (const char* suffix : {".mesh", "_border.mesh"})
      PortraitParent(*edited.Find(stem + suffix), hint_name);
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitSplashList(const std::vector<uint8_t>& source,
                               const std::vector<uint8_t>& button_source, std::vector<uint8_t>* scene,
                               std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 4423 || Crc32(bytes.data(), bytes.size()) != 0x4B1EEE24)
    return Fail(error, "portrait splash list source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait button template revision changed");
  PutFloat(bytes.data() + 0x54A, ui::kPortraitMenuRowStep);
  // UILabel's explicit normal/selected color references, rather than the
  // directory's unused fallback color names, determine the visible text.
  SceneColor(bytes, 0xA45, .96f, .96f, .98f, 1);
  SceneColor(bytes, 0xA6A, .09f, .065f, .025f, 1);
  SceneColor(bytes, 0x102C, .09f, .065f, .025f, 1);
  PutFloat(bytes.data() + 0xBFB, 27.5f);

  std::vector<size_t> markers;
  for (size_t at = 0; at + 4 <= bytes.size(); ++at)
    if (Be32(bytes.data() + at) == 0xADDEADDE) markers.push_back(at);
  if (markers.size() != 24) return Fail(error, "portrait list object boundaries changed");
  std::vector<PortraitObject> objects;
  size_t at = 0x29;
  auto string = [&] {
    const auto size = Be32(bytes.data() + at); at += 4;
    std::string value(reinterpret_cast<const char*>(bytes.data() + at), size); at += size; return value;
  };
  for (size_t i = 0; i < 23; ++i) {
    auto type = string(), name = string();
    // UIListHighlight reshapes its mesh into an underline during Draw. Use
    // the options list's ordinary Mesh slots for both button states instead.
    if (type == "UIListHighlight") continue;
    objects.push_back({std::move(type), std::move(name),
        {bytes.begin() + markers[i] + 4, bytes.begin() + markers[i + 1] + 4}});
  }
  for (auto& object : objects)
    if (object.name == "button.lbl")
      RenameSceneString(object.data, "../fonts/pentatonic.milo", "../fonts/portrait_splash.milo");
  auto original = [&](std::string_view name) {
    return std::find_if(objects.begin(), objects.end(), [&](const auto& o) { return o.name == name; })->data;
  };
  const auto prototype_material = original("highlight.mat");
  const auto prototype_mesh = original("highlight.mesh");
  auto button = [&](std::string name, bool selected, bool border) {
    auto material = prototype_material;
    const auto color = border ? ui::kMenuButtonBorder :
        selected ? ui::kMenuButtonHover : ui::kMenuButtonNormal;
    SceneColor(material, 0x19, color[0], color[1], color[2], color[3]);
    auto mesh = prototype_mesh;
    const float width = border ? 367.f : 364.5f, height = border ? 55.25f : 52.75f;
    for (size_t matrix : {size_t(0x15), size_t(0x45)})
      SceneMatrix(mesh, matrix, width / 405.f, height / 26.f, -192.5f * width / 405.f);
    RenameSceneString(mesh, "highlight.mat", name + ".mat");
    RenameSceneString(mesh, "highlight.mesh", name + ".mesh");
    std::vector<uint8_t> widget(buttons.begin() + 0x163E0, buttons.begin() + 0x16466);
    PutBe32(widget.data() + 101, selected ? 1 : 2);
    // UIListWidget::draw_order precedes the color references. Labels draw at
    // order 1; paint the button surfaces first, leaving their text unobscured.
    PutFloat(widget.data() + 25, border ? -2.f : -1.f);
    RenameSceneString(widget, "slot.mesh", name + ".mesh");
    RenameSceneString(widget, "slot.mat", name + ".mat");
    objects.push_back({"Mat", name + ".mat", std::move(material)});
    objects.push_back({"Mesh", name + ".mesh", std::move(mesh)});
    objects.push_back({"UIListMesh", name, std::move(widget)});
  };
  button("portrait_border", false, true);
  button("portrait_button", false, false);
  button("portrait_selected", true, false);
  std::vector<uint8_t> result(bytes.begin(), bytes.begin() + 0x29);
  uint32_t name_bytes = 0;
  for (const auto& object : objects) {
    SceneString(result, object.type); SceneString(result, object.name);
    name_bytes += uint32_t(object.name.size() + 1);
  }
  PutBe32(result.data() + 0x1C, uint32_t(objects.size()) * 2 + 2);
  PutBe32(result.data() + 0x20, name_bytes + 8); // includes the root's "default" name
  PutBe32(result.data() + 0x25, uint32_t(objects.size()));
  result.insert(result.end(), bytes.begin() + 0x304, bytes.begin() + 0x567);
  for (const auto& object : objects) result.insert(result.end(), object.data.begin(), object.data.end());
  return PackStored(result, scene, error);
}

bool PreparePortraitSplashFont(const std::vector<uint8_t>& source,
                               std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 1424134 || Crc32(bytes.data(), bytes.size()) != 0x6AA20AD1)
    return Fail(error, "portrait button font source revision changed");
  // Use the same 1024-square regular atlas as Help & Options. Keep every
  // variation unlit in this private copy so background lights do not tint
  // the button labels; the shared font retains its authored materials.
  for (size_t material : {size_t(0x155D8A), size_t(0x155EB2), size_t(0x155FDA), size_t(0x156102)})
    bytes[material + 0x29] = 0; // Mat::use_environ
  return PackStored(bytes, scene, error);
}

namespace {
bool BuildPortraitSongList(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& buttons,
                           bool grouped, std::string_view store,
                           std::vector<uint8_t>* scene, std::string* error, float aspect = 0);
}

bool PreparePortraitSongList(const std::vector<uint8_t>& source,
                             const std::vector<uint8_t>& button_source, bool grouped,
                             std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != (grouped ? 357193u : 3288u) ||
      Crc32(bytes.data(), bytes.size()) != (grouped ? 0xBB51126C : 0x3AA51362))
    return Fail(error, "portrait song list source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait song button template revision changed");
  return BuildPortraitSongList(bytes, buttons, grouped, {}, scene, error);
}

bool PreparePortraitStoreList(const std::vector<uint8_t>& source,
                              const std::vector<uint8_t>& button_source, std::string_view kind,
                              std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  const auto crc = Crc32(bytes.data(), bytes.size());
  if (!((kind == "menu" && bytes.size() == 25668 && crc == 0xEE741658) ||
        (kind == "browser" && bytes.size() == 77557 && crc == 0xA2078F90) ||
        (kind == "info" && bytes.size() == 91304 && crc == 0x1C5441E7)))
    return Fail(error, "portrait store list source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait store button template revision changed");
  return BuildPortraitSongList(bytes, buttons, false, kind, scene, error);
}

bool PreparePortraitMessagesList(const std::vector<uint8_t>& source,
                                 const std::vector<uint8_t>& button_source, float aspect,
                                 std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error, 4 * 1024 * 1024) ||
      !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 2462023 || Crc32(bytes.data(), bytes.size()) != 0xB9CA86DD)
    return Fail(error, "portrait message list source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait message button template revision changed");
  return BuildPortraitSongList(bytes, buttons, false, "messages", scene, error, aspect);
}

namespace {
bool BuildPortraitSongList(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& buttons,
                           bool grouped, std::string_view store,
                           std::vector<uint8_t>* scene, std::string* error, float aspect) {
  PortraitScene edited(bytes), templates(buttons);
  const bool messages = store == "messages";
  const bool store_menu = store == "menu";
  const auto inbox = ui::PortraitMessages(aspect);
  // UIListDir's tail holds orientation, preview fields and element spacing.
  PutFloat(edited.directory.data() + edited.directory.size() - 29,
           messages ? inbox.row_step : store_menu ? ui::kPortraitMenuRowStep : ui::kPortraitSongRowStep);
  std::erase_if(edited.objects, [&](const auto& object) {
    return object.type == "UIListHighlight" ||
           (object.type == "UIListMesh" && !(messages && object.name == "icon"));
  });
  for (auto& object : edited.objects) {
    if (object.type == "Group") PortraitPose(object, 1, 0, 0);
    if (object.type == "UIListCustom") PutFloat(object.data.data() + 25, 100);
    if (object.type == "UIColor") {
      const bool black = object.name == "highlight.color" || object.name == "selecting.color" ||
                         object.name == "Selected.color" || object.name == "highlight_text.color";
      SceneColor(object.data, 17, black ? .04f : 1.f, black ? .03f : 1.f, black ? .02f : 1.f, 1);
    }
    if (object.type == "UILabel") {
      PortraitPose(object, 1, -196, 0);
      PortraitText(object, object.name == "filter.lbl" ? 18.f : 22.f,
                   object.name == "filter.lbl" || !grouped ? 390.f : 208.f, 0x21);
      // Taller rows let song names wrap while retaining a separate star column.
      constexpr std::string_view font = "../fonts/";
      const auto at = std::search(object.data.begin(), object.data.end(), font.begin(), font.end());
      if (at != object.data.end()) {
        PutBe32(object.data.data() + (at - object.data.begin()) - 41, 0);
      }
    }
  }
  if (!store.empty() && !messages) {
    // Retain provider field names so grouping, paging and ownership stay native.
    std::erase_if(edited.objects, [&](const auto& object) {
      if (object.type != "UIListLabel") return false;
      if (store == "menu") return object.name != "filter" && object.name != "count";
      if (store == "info") return object.name != "list";
      return object.name != "song" && object.name != "album" && object.name != "group" &&
             object.name != "group_center" && object.name != "purchased";
    });
    for (auto& object : edited.objects) {
      // The first populated title paints the cell. Keep ownership and count
      // labels above it, regardless of the source directory's widget order.
      if (object.type == "UIListLabel")
        PutFloat(object.data.data() + 25, object.name == "count" || object.name == "purchased" ? 2.f : 1.f);
      if (object.type == "UILabel") {
        const bool badge = object.name == "purchased.lbl" || object.name == "count.lbl";
        PortraitPose(object, 1, badge ? 140.f : -196.f, 0);
        PortraitText(object, badge ? 12.f : store_menu ? 27.5f : 22.f,
                     badge ? 65.f : 325.f, 0x21);
        RenameSceneString(object.data, "../fonts/pentatonic.milo", "../fonts/portrait_splash.milo");
      }
      if (object.type == "UIColor") {
        const bool black = object.name == "slot_active_selecting.color" ||
                           object.name == "slot_selected.color" || object.name == "highlight_selected.color";
        SceneColor(object.data, 17, black ? .04f : 1.f, black ? .03f : 1.f, black ? .02f : 1.f, 1);
      }
    }
  }
  if (messages) {
    std::erase_if(edited.objects, [](const auto& object) {
      return object.type == "UIListLabel" && object.name != "subject_line" &&
             object.name != "header" && object.name != "new";
    });
    for (auto& object : edited.objects) {
      if (object.name == "header.lbl") object.name = "portrait_message_header.lbl";
      RenameSceneString(object.data, "header.lbl", "portrait_message_header.lbl");
      if (object.type == "UIListLabel")
        PutFloat(object.data.data() + 25, object.name == "new" ? 3.f : 1.f);
      if (object.type == "UIColor") SceneColor(object.data, 17, 1, 1, 1, 1);
      if (object.type != "UILabel") continue;
      const bool badge = object.name == "new.lbl";
      PortraitPose(object, 1, badge ? inbox.width / 2 - 8 : -inbox.width / 2 + inbox.text_inset, 0);
      PortraitText(object, badge ? 9.f : 14.f, badge ? 12.f : inbox.width - inbox.text_inset - 16, 0x21);
      if (!badge) for (const char* font : {"../fonts/pentatonic_regularcond.milo",
                                          "../fonts/pentatonic_bold.milo", "../fonts/pentatonic.milo"})
        RenameSceneString(object.data, font, "../fonts/portrait_splash.milo");
    }
    PortraitPose(*edited.Find("icon.mesh"), inbox.icon_size / 50,
                  -inbox.width / 2 + inbox.icon_inset, 0);
    // The label draw paints the row background. Draw its separate icon after
    // that surface so the feed keeps the provider's original icon material.
    PutFloat(edited.Find("icon")->data.data() + 25, 2.f);
  }
  if (!store.empty()) {
    auto black = *templates.Find("active_normal.color");
    black.name = "portrait_caption_black.color";
    SceneColor(black.data, 17, 0, 0, 0, 1);
    edited.objects.push_back(std::move(black));
  }
  // The bonus glyph is outside the portrait title column. The native song
  // label remains the first populated label for each selectable row.
  if (grouped) std::erase_if(edited.objects, [](const auto& object) {
    return object.type == "UIListLabel" && object.name == "bonus";
  });
  if (grouped) {
    for (int i = 0; i < 5; ++i)
      PortraitPose(*edited.Find("star" + std::to_string(i + 1) + ".mesh"), 2.f,
                   38.f + 36.f * i, 0);
    PortraitPose(*edited.Find("bonus.lbl"), .7f, -204, 0);
  }
  // Keep native text and star widgets. The draw bridge paints these rectangles
  // beneath populated labels using the provider's final row transforms.
  auto rectangle = [&](std::string name, bool selected, bool header) {
    auto material = *templates.Find("slot.mat");
    material.name = name + ".mat";
    RenameSceneString(material.data, "options_slot.tex", "");
    material.data[0x29] = 0;
    PutBe32(material.data.data() + 0x15, 3);
    const auto color = selected ? ui::kMenuButtonHover : ui::kMenuButtonNormal;
    SceneColor(material.data, 0x19, color[0], color[1], color[2], color[3]);
    auto mesh = *templates.Find("slot.mesh"); mesh.name = name + ".mesh";
    const float height = header ? 30.f : messages ? inbox.row_height : ui::kPortraitSongRowHeight;
    const float width = messages ? inbox.width : 420.f;
    for (size_t at : {size_t(0x15), size_t(0x45)}) {
      SceneMatrix(mesh.data, at, width / 300, height / 30, -width / 2, 0, height / 2);
      PutFloat(mesh.data.data() + at + 16, 0);
      PutFloat(mesh.data.data() + at + 20, height / 30);
      PutFloat(mesh.data.data() + at + 28, -1);
      PutFloat(mesh.data.data() + at + 32, 0);
    }
    RenameSceneString(mesh.data, "options_menu.grp", "");
    RenameSceneString(mesh.data, "slot.mat", material.name);
    RenameSceneString(mesh.data, "slot.mesh", mesh.name);
    if (auto* original = edited.Find(material.name)) *original = std::move(material);
    else edited.objects.push_back(std::move(material));
    if (auto* original = edited.Find(mesh.name)) *original = std::move(mesh);
    else edited.objects.push_back(std::move(mesh));
  };
  for (const bool selected : {false, true}) {
    const std::string name = selected ? "portrait_selected" : "portrait_row";
    rectangle(name, selected, false);
  }
  rectangle("portrait_header", false, true);
  std::vector<std::string> row_meshes{"portrait_row.mesh", "portrait_selected.mesh", "portrait_header.mesh"};
  if (store_menu) {
    rectangle("portrait_row_border", false, false);
    const auto& border = ui::kMenuButtonBorder;
    SceneColor(edited.Find("portrait_row_border.mat")->data, 0x19, border[0], border[1], border[2], border[3]);
    row_meshes.push_back("portrait_row_border.mesh");
  }
  if (store == "browser") SceneColor(edited.Find("portrait_row.mat")->data, 0x19, .035f, .026f, .065f, .83f);
  if (messages) for (const char* name : {"portrait_row.mat", "portrait_selected.mat"})
    SceneColor(edited.Find(name)->data, 0x19, .04f, .025f, .09f, .94f);
  SceneColor(edited.Find("portrait_header.mat")->data, 0x19, .035f, .025f, .07f, .86f);
  // Draw these native meshes with each populated label's final slot transform.
  // The song provider does not create elements for unfamiliar mesh widgets.
  PortraitHiddenGroup(edited, templates, "portrait_rows.grp", row_meshes);
  edited.RemoveDrawMember("slot.mesh");
  std::vector<std::string> original_art{"slot.mesh"};
  if (grouped) { edited.RemoveDrawMember("highlight.mesh"); original_art.push_back("highlight.mesh"); }
  if (!store.empty()) {
    original_art.clear();
    for (const auto& object : edited.objects)
      if (object.type == "Mesh" && !object.name.starts_with("portrait_") &&
          !(messages && object.name == "icon.mesh")) original_art.push_back(object.name);
    for (const auto& name : original_art) edited.RemoveDrawMember(name);
  }
  PortraitHiddenGroup(edited, templates, "portrait_old_rows.grp", original_art);
  return PackStored(edited.Build(), scene, error);
}

} // namespace

bool PreparePortraitSongs(const std::vector<uint8_t>& source,
                          const std::vector<uint8_t>& button_source, float aspect,
                          std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 1548452 || Crc32(bytes.data(), bytes.size()) != 0xF1C32A9A)
    return Fail(error, "portrait song panel source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait song button template revision changed");
  PortraitScene edited(bytes);
  const auto layout = ui::PortraitSongs(aspect);
  PortraitPose(*edited.Find("songlist.grp"), 1, 0, 0);
  PortraitPose(*edited.Find("songselect.grp"), 1, 0, 0);
  PortraitPose(*edited.Find("right_col.grp"), 1, 0, 0);
  PortraitPose(*edited.Find("songs.lst"), layout.list_scale, 0, layout.list_z, "songlist.grp");
  RenameSceneString(edited.Find("songs.lst")->data, "../resource/lists/list_mysongs.milo",
                    "../resource/lists/portrait_mysongs.milo");
  const std::vector<std::string> landscape_art{"side_panel.mesh", "side_neon.mesh", "side_neon_inner.mesh",
                           "info_panel.mesh", "mini_leaders.lst", "lb_title.lbl", "title.lbl",
                           "difficulty_header.lbl", "loading.grp", "no_board.grp", "unavailable.grp",
                           "scroller.grp", "InlineHelp_Leaderboard.ihp"};
  for (const auto& name : landscape_art)
    edited.RemoveDrawMember(name);
  PortraitScene templates(buttons);
  PortraitHiddenGroup(edited, templates, "portrait_landscape_art.grp", landscape_art);
  const float left = -layout.width / 2 + 6;
  // The album mesh's pivot cancels its vertex bounds: it is centred at zero,
  // with approximately 167-unit sides after its own baked transform.
  auto* album = edited.Find("album_art");
  if (!album || album->type != "RndDir") return Fail(error, "portrait album resource changed");
  // Its local transform is set by the panel's entry method after resource loading.
  auto title = *edited.Find("artist.lbl"); title.name = "portrait_song_title.lbl";
  RenameSceneString(title.data, "leaderboard_song_artistname", "");
  edited.objects.push_back(std::move(title));
  for (const auto& [name, z, size] : {
      std::tuple{"portrait_song_title.lbl", -141.f, 12.f},
      std::tuple{"artist.lbl", -154.f, 10.f},
      std::tuple{"album.lbl", -166.f, 9.f},
      std::tuple{"genre_year.lbl", -178.f, 9.f}}) {
    auto* label = edited.Find(name);
    PortraitPose(*label, 1, left + 48, z);
    PortraitText(*label, size, layout.width - 60, 0x21);
    RenameSceneString(label->data, "../resource/fonts/pentatonic_regularcond.milo",
                      "../resource/fonts/portrait_splash.milo");
  }
  // Private unlit copies keep the library tabs independent of the album and
  // difficulty resources' lighting. Other hub pages retain the shared headers.
  for (const auto& [name, token] : {
      std::pair{"portrait_tab_songs.lbl", "my_songs"}, std::pair{"portrait_tab_store.lbl", "store"},
      std::pair{"portrait_tab_messages.lbl", "header_inbox"}, std::pair{"portrait_tab_recs.lbl", "recs"}}) {
    auto label = *edited.Find("artist.lbl"); label.name = name;
    RenameSceneString(label.data, "leaderboard_song_artistname", token);
    PortraitPose(label, 1, 0, layout.tabs_z);
    PortraitText(label, 12, layout.width);
    edited.objects.push_back(std::move(label));
    edited.Before("sorting.lbl", {name});
  }
  for (const bool next : {false, true}) {
    const std::string stem = next ? "portrait_header_next" : "portrait_header_previous";
    const float x = (next ? 1.f : -1.f) * (layout.width / 2 - ui::kPortraitSongArrowInset);
    PortraitSurface(edited, buttons, stem, ui::kPortraitSongArrowTargetSize,
                    ui::kPortraitSongArrowTargetSize, x, layout.tabs_z, {});
  }
  // Include the new title in the native right-column traversal.
  edited.Before("artist.lbl", {"portrait_song_title.lbl"});
  PortraitSurface(edited, buttons, "portrait_song_card", layout.width, layout.card_height,
                  0, layout.card_z, "portrait_song_title.lbl");
  auto* sort = edited.Find("sorting.lbl");
  PortraitPose(*sort, 1, -layout.width / 4 - 2, layout.tools_z);
  PortraitText(*sort, 12, layout.width / 2 - 8);
  auto search = *sort; search.name = "portrait_search.lbl";
  RenameSceneString(search.data, "song_sort_str", "search");
  RenameSceneString(search.data, "../resource/fonts/buttons.milo",
                    "../resource/fonts/inline_help_icons.milo");
  PortraitPose(search, 1, layout.width / 4 + 2, layout.tools_z);
  edited.objects.push_back(std::move(search));
  edited.Before("sorting.lbl", {"portrait_search.lbl"});
  for (const char* name : {"portrait_sort", "portrait_search"})
    PortraitSurface(edited, buttons, name, layout.width / 2 - 3, 27,
                    name == std::string_view("portrait_sort") ? -layout.width / 4 - 2 : layout.width / 4 + 2,
                    layout.tools_z, {});
  PortraitFooter(edited, buttons, aspect);
  SceneColor(edited.Find("portrait_retry.mat")->data, 0x19, 1, .855f, .086f, 1);
  auto color = *edited.Find("portrait_caption_black.color"); color.name = "portrait_songs.color";
  edited.objects.push_back(std::move(color));
  // Retain the landscape popup's neon frame, list highlight and native hints.
  // Fit the complete group together so its animation and internal spacing agree.
  const float popup_scale = layout.width / 420;
  PortraitPose(*edited.Find("shortcuts.grp"), popup_scale, 0, 0);
  PortraitPose(*edited.Find("shortcuts.lst"), 1, 0, 135, "shortcuts.grp");
  RenameSceneString(edited.Find("shortcuts.lst")->data, "../resource/lists/list_popup.milo",
                    "../resource/lists/portrait_shortcuts.milo");
  PortraitPose(*edited.Find("popup_header.lbl"), 1, 0, 190, "shortcuts.grp");
  PortraitText(*edited.Find("popup_header.lbl"), 28, 350);
  PortraitPose(*edited.Find("pop_up_help.ihp"), 1.5f, -95, -185, "shortcuts.grp");
  auto& popup_animation = edited.Find("pop_up.anim")->data;
  if (popup_animation.size() != 223 || Be32(popup_animation.data() + 91) != 2)
    return Fail(error, "portrait shortcut entrance animation changed");
  for (size_t at : {size_t(95), size_t(103)}) PutFloat(popup_animation.data() + at, .5f * popup_scale);
  for (size_t at : {size_t(111), size_t(119)}) PutFloat(popup_animation.data() + at, popup_scale);
  if (!PortraitLibraryAnimation(edited, aspect, error)) return false;
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitStore(const std::vector<uint8_t>& source,
                          const std::vector<uint8_t>& button_source, std::string_view kind,
                          float aspect, std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  const auto crc = Crc32(bytes.data(), bytes.size());
  if (!((kind == "menu" && bytes.size() == 198542 && crc == 0x56F69A79) ||
        (kind == "browser" && bytes.size() == 27572 && crc == 0x02151E8F) ||
        (kind == "details" && bytes.size() == 699173 && crc == 0x3A7D0176) ||
        (kind == "info" && bytes.size() == 3170769 && crc == 0xCFC46DD6) ||
        (kind == "dialog" && bytes.size() == 2803318 && crc == 0x6790B076) ||
        (kind == "chrome" && bytes.size() == 281861 && crc == 0xE4D9DD75)))
    return Fail(error, "portrait store scene source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait store button template revision changed");
  PortraitScene edited(bytes), templates(buttons);
  const auto layout = ui::PortraitSongs(aspect);
  const auto offer = ui::PortraitStoreOffer(aspect);
  // Each independently drawn panel owns its safe-area fit, including details
  // below the browser and the screen still visible behind a modal.
  auto marker = *templates.Find("active_normal.color");
  marker.name = "portrait_store.color";
  edited.objects.push_back(std::move(marker));
  const auto hide = [&](std::vector<std::string> names) {
    for (const auto& name : names) edited.RemoveDrawMember(name);
    PortraitHiddenGroup(edited, templates, "portrait_store_hidden.grp", names);
  };
  const auto label = [&](const char* name, float x, float z, float size, float width) {
    auto* object = edited.Find(name);
    PortraitPose(*object, 1, x, z);
    PortraitText(*object, size, width, 0x21);
    for (const char* font : {"../resource/fonts/pentatonic_regularcond.milo",
                             "../resource/fonts/pentatonic_bold.milo",
                             "../resource/fonts/pentatonic.milo"})
      RenameSceneString(object->data, font, "../resource/fonts/portrait_splash.milo");
  };
  if (kind == "dialog") {
    const auto dialog = ui::PortraitStoreDialog(aspect);
    PortraitPose(*edited.Find("all.grp"), 1, 0, 0);
    hide({"error_panel"});
    label("error.lbl", 0, dialog.text_top, 16, dialog.TextWidth());
    PortraitText(*edited.Find("error.lbl"), 16, dialog.TextWidth(), 0x12, dialog.text_height);
    label("continue.lbl", dialog.ActionX(), dialog.action_z, 14, dialog.ActionWidth() - 8);
    PortraitText(*edited.Find("continue.lbl"), 14, dialog.ActionWidth() - 8);
    auto close = *edited.Find("continue.lbl");
    close.name = "portrait_dialog_close.lbl";
    PortraitPose(close, 1, -dialog.ActionX(), dialog.action_z);
    edited.objects.push_back(std::move(close));
    edited.Before("continue.lbl", {"portrait_dialog_close.lbl"});

    PortraitSurface(edited, buttons, "portrait_dialog_shade", ui::PortraitSplash(aspect).width * 2,
                    520, 0, 0, "error.lbl");
    SceneColor(edited.Find("portrait_dialog_shade.mat")->data, 0x19, 0, 0, 0, .56f);
    SceneColor(edited.Find("portrait_dialog_shade_border.mat")->data, 0x19, 0, 0, 0, 0);
    PortraitStoreDialogFrame(edited, templates, dialog);
    PortraitSurface(edited, buttons, "portrait_dialog_action", dialog.ActionWidth(), dialog.action_height,
                    dialog.ActionX(), dialog.action_z, {});
    PortraitSurface(edited, buttons, "portrait_dialog_close", dialog.ActionWidth(), dialog.action_height,
                    -dialog.ActionX(), dialog.action_z, {});
    PortraitSurface(edited, buttons, "portrait_dialog_single", dialog.SingleWidth(), dialog.action_height,
                    0, dialog.action_z, {});
    SceneColor(edited.Find("portrait_dialog_action.mat")->data, 0x19, .37f, .08f, .13f, 1);
  } else if (kind == "chrome") {
    hide({"album.lbl", "artist.lbl", "desc.lbl", "genre.lbl", "year.lbl",
          "microsoft.lbl", "ps_logo.mesh", "ps_logo_1080.mesh"});
    label("crumb.lbl", -layout.width / 2 + 6, layout.tools_z, 12, layout.width - 12);
    PortraitPose(*edited.Find("wait.grp"), .4f, layout.width / 2 - 10, layout.tools_z);
  } else if (kind == "details") {
    hide({"genre_screen.mesh", "album_info_screen.mesh", "pack_songs.lst"});
    const float left = -layout.width / 2 + 54;
    for (const auto& [name, z] : {std::pair{"artist.lbl", -139.f}, {"album.lbl", -151.f},
                                 {"genre.lbl", -163.f}, {"year.lbl", -175.f}})
      label(name, left, z, 10, z < -160 ? std::max(20.f, layout.width - 120) : layout.width - 64);
    SceneColor(edited.Find("black.color")->data, 17, 1, 1, 1, 1);
    // Album mesh geometry is baked in the landscape right column. Normalize
    // that geometry to a square rather than retaining its old pivot/offset.
    auto* album = edited.Find("album.mesh");
    constexpr size_t vertices = 219;
    if (album->data.size() != 394) return Fail(error, "portrait store album geometry changed");
    float min_x = INFINITY, max_x = -INFINITY, min_y = INFINITY, max_y = -INFINITY;
    for (uint32_t i = 0; i < 4; ++i) {
      const auto* vertex = album->data.data() + vertices + i * 36;
      const auto x = std::bit_cast<float>(Be32(vertex));
      const auto y = std::bit_cast<float>(Be32(vertex + 4));
      min_x = std::min(min_x, x); max_x = std::max(max_x, x);
      min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    if (!(max_x > min_x && max_y > min_y)) return Fail(error, "portrait store album bounds changed");
    for (uint32_t i = 0; i < 4; ++i) {
      auto* vertex = album->data.data() + vertices + i * 36;
      const auto x = std::bit_cast<float>(Be32(vertex));
      const auto y = std::bit_cast<float>(Be32(vertex + 4));
      PutFloat(vertex, 40 * ((x - min_x) / (max_x - min_x) - .5f));
      PutFloat(vertex + 4, 0);
      PutFloat(vertex + 8, 40 * (.5f - (y - min_y) / (max_y - min_y)));
    }
    PortraitPose(*album, 1, -layout.width / 2 + 27, layout.card_z);
    edited.Find("album.mat")->data[0x29] = 0;
    // The difficulty resource is placed after it is loaded by its panel DTA.
    PortraitSurface(edited, buttons, "portrait_store_card", layout.width, layout.card_height,
                    0, layout.card_z, "album.mesh");
    PortraitSurface(edited, buttons, "portrait_store_cover", offer.art_size + 4, offer.art_size + 4,
                    0, offer.ArtZ(), "album.mesh");
    SceneColor(edited.Find("portrait_store_cover.mat")->data, 0x19, .028f, .017f, .04f, 1);
    SceneColor(edited.Find("portrait_store_cover_border.mat")->data, 0x19, .94f, .71f, .46f, .46f);
    for (const char* name : {"portrait_store_cover.mesh", "portrait_store_cover_border.mesh"}) {
      auto& mesh = *edited.Find(name);
      mesh.data[PortraitMatrix(mesh) + 113] = 0;
    }
  } else {
    const bool menu = kind == "menu", info = kind == "info";
    if (menu) hide({"scroller.grp"});
    else if (info) hide({"neon_panel.mesh", "neon_light.mesh", "recommended.grp", "recommended_wait.grp"});
    else hide({"album.mesh", "scroller.grp", "shortcuts.grp"});
    const char* list_name = menu ? "filter.lst" : info ? "buttons.lst" : "song.lst";
    auto* list = edited.Find(list_name);
    const float list_z = info ? -45.f : menu ? ui::kPortraitStoreMenuRowZ : ui::kPortraitStoreBrowserRowZ;
    PortraitPose(*list, layout.list_scale, 0, list_z);
    const char* old_resource = menu ? "../resource/lists/list_store_frontmenu.milo" :
        info ? "../resource/lists/list_store_songpage.milo" : "../resource/lists/list_held_button.milo";
    const auto resource = std::string("../resource/lists/portrait_store_") + std::string(kind) + ".milo";
    RenameSceneString(list->data, old_resource, resource);
    // Private headers follow the library tab identity, without altering shared
    // background animation keys used by the landscape hub pages.
    auto heading = menu ? *edited.Find("store_unavailable.lbl") :
        info ? *edited.Find("song.lbl") : *edited.Find("popup_header.lbl");
    heading.name = "portrait_tab_store.lbl";
    PortraitPose(heading, 1, 0, layout.tabs_z);
    PortraitText(heading, 17, layout.width - 76);
    for (const char* font : {"../resource/fonts/pentatonic_regularcond.milo",
                             "../resource/fonts/pentatonic_bold.milo",
                             "../resource/fonts/pentatonic.milo"})
      RenameSceneString(heading.data, font, "../resource/fonts/portrait_splash.milo");
    // Replace the scene token; set_token_fmt also refreshes it at panel entry.
    edited.objects.push_back(std::move(heading));
    // The heading remains visible while the menu's loading state switches
    // between main.grp, loading.grp and unavailable.grp.
    edited.Before(menu ? "main.grp" : info ? "song.lbl" : "song.lst", {"portrait_tab_store.lbl"});
    if (!info) {
      for (const bool next : {false, true}) {
        const auto stem = std::string(next ? "portrait_header_next" : "portrait_header_previous");
        PortraitSurface(edited, buttons, stem, ui::kPortraitSongArrowTargetSize,
                        ui::kPortraitSongArrowTargetSize,
                        (next ? 1.f : -1.f) * (layout.width / 2 - ui::kPortraitSongArrowInset), layout.tabs_z, {});
      }
    }
    if (menu) label("store_unavailable.lbl", -layout.width * .43f, 30, 17, layout.width * .86f);
    if (info) {
      // Price and artist labels precede the title in the retail draw group.
      // Put the surface ahead of all of them so recycled price labels stay legible.
      PortraitSurface(edited, buttons, "portrait_store_info", offer.width, offer.top - offer.Bottom(),
                      0, (offer.top + offer.Bottom()) / 2, "cost.lbl");
      SceneColor(edited.Find("portrait_store_info.mat")->data, 0x19, .045f, .026f, .073f, .95f);
      for (const auto& text : ui::PortraitStoreOfferLabels(offer))
        label(text.name.data(), offer.Left(), text.top, text.size, offer.TextWidth());
    }
    PortraitFooter(edited, buttons, aspect);
    auto color = *edited.Find("portrait_caption_black.color"); color.name = "portrait_songs.color";
    edited.objects.push_back(std::move(color));
    // All native state footers use the same private action resource.
    for (auto& object : edited.objects) if (object.type == "InlineHelp")
      RenameSceneString(object.data, "../resource/inline_help/default.milo",
                        "../resource/inline_help/portrait_actions.milo");
  }
  if ((kind == "menu" || kind == "browser" || kind == "details") &&
      !PortraitLibraryAnimation(edited, aspect, error)) return false;
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitRecommendationList(const std::vector<uint8_t>& source, float aspect,
                                       std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 707715 || Crc32(bytes.data(), bytes.size()) != 0xBAC782D2)
    return Fail(error, "portrait recommendation list source revision changed");
  PortraitScene edited(bytes);
  const auto layout = ui::PortraitRecommendations(aspect);
  std::erase_if(edited.objects, [](const auto& object) {
    return object.type == "UIListHighlight" ||
           (object.type == "UIListMesh" && object.name != "album_mesh");
  });
  for (auto& object : edited.objects) {
    if (object.type == "Group") PortraitPose(object, 1, 0, 0);
    if (object.type == "Mat" && object.name.starts_with("album")) object.data[0x29] = 0;
    if (object.type == "UIColor") SceneColor(object.data, 17, 1, 1, 1, 1);
  }
  PortraitPose(*edited.Find("album_slot.mesh"), layout.art_size / 125, 0, 0);
  auto& type = *edited.Find("rec_type.lbl");
  PortraitPose(type, 1, layout.Left(), layout.TypeTop() - layout.carousel_z);
  PortraitText(type, 11, layout.TextWidth(), 0x11);
  RenameSceneString(type.data, "../fonts/pentatonic_regularcond.milo", "../fonts/portrait_splash.milo");
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitRecommendationLeaderboard(const std::vector<uint8_t>& source, float aspect,
                                              std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 93819 || Crc32(bytes.data(), bytes.size()) != 0x2560C8D4)
    return Fail(error, "portrait recommendation leaderboard source revision changed");
  PortraitScene edited(bytes);
  const auto layout = ui::PortraitRecommendations(aspect);
  PutFloat(edited.directory.data() + edited.directory.size() - 29, layout.leaderboard_step);
  std::erase_if(edited.objects, [](const auto& object) {
    return object.type == "UIListHighlight" || object.type == "UIListMesh";
  });
  for (auto& object : edited.objects) {
    if (object.type == "Group") PortraitPose(object, 1, 0, 0);
    if (object.type == "UIColor") SceneColor(object.data, 17, 1, 1, 1, 1);
    if (object.type != "UILabel") continue;
    const bool rank = object.name == "rank.lbl", score = object.name == "score.lbl";
    const float x = rank ? -layout.width / 2 + 8 : score ? layout.width / 2 - 60 : -layout.width / 2 + 36;
    PortraitPose(object, 1, x, 0);
    PortraitText(object, score || rank ? 10.f : 11.f,
                   rank ? 22.f : score ? 52.f : layout.width - 102, 0x21);
    RenameSceneString(object.data, "../fonts/pentatonic_regularcond.milo", "../fonts/portrait_splash.milo");
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitRecommendations(const std::vector<uint8_t>& source,
                                    const std::vector<uint8_t>& button_source,
                                    const std::vector<uint8_t>& background_source, float aspect,
                                    std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons, background;
  if (!DecodeStream(source, &bytes, error, 4 * 1024 * 1024) ||
      !DecodeStream(button_source, &buttons, error) ||
      !DecodeStream(background_source, &background, error)) return false;
  if (bytes.size() != 3556837 || Crc32(bytes.data(), bytes.size()) != 0xAE602183)
    return Fail(error, "portrait recommendation panel source revision changed");
  if (background.size() != 245092 || Crc32(background.data(), background.size()) != 0xE76A5615)
    return Fail(error, "portrait recommendation header arrow source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait recommendation button template revision changed");
  PortraitScene edited(bytes), templates(buttons), header(background);
  for (const char* name : {"shell_nav_arrow.tex", "shell_arrow_left.mat", "shell_arrow_rt.mat",
                           "shell_arrow_left.mesh", "shell_arrow_rt.mesh"})
    if (!header.Find(name)) return Fail(error, "portrait recommendation header arrow source changed");
  const auto layout = ui::PortraitRecommendations(aspect);
  std::vector<std::string> old_art;
  for (const auto& object : edited.objects) if (object.type == "Mesh") old_art.push_back(object.name);
  for (const auto& name : old_art) edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_recommendations_landscape.grp", old_art);
  for (const char* name : {"home.grp", "info.grp", "mini_leaders.grp", "no_board.grp", "unavailable.grp"})
    PortraitPose(*edited.Find(name), 1, 0, 0);
  auto& carousel = *edited.Find("recommendations.lst");
  PortraitPose(carousel, 1, 0, layout.carousel_z);
  RenameSceneString(carousel.data, "../resource/lists/list_recommendations_albums.milo",
                    "../resource/lists/portrait_recommendations.milo");
  auto& leaders = *edited.Find("mini_leaders.lst");
  PortraitPose(leaders, 1, 0, layout.leaderboard_z);
  RenameSceneString(leaders.data, "../resource/lists/list_home_lb.milo",
                    "../resource/lists/portrait_rec_leaders.milo");
  const auto label = [&](const char* name, float z, float size) {
    auto& object = *edited.Find(name);
    PortraitPose(object, 1, layout.Left(), z);
    PortraitText(object, size, layout.TextWidth(), 0x11);
    // The reason's alternate bold font keeps its own material and emphasis.
    for (const char* font : {"../resource/fonts/pentatonic_regularcond.milo", "../resource/fonts/pentatonic.milo"})
      RenameSceneString(object.data, font, "../resource/fonts/portrait_splash.milo");
    const auto font = std::search(object.data.begin(), object.data.end(), "../", "../" + 3);
    PutBe32(object.data.data() + (font - object.data.begin()) - 41, 0);
  };
  label("song_title.lbl", layout.TitleTop(), 18);
  label("song_artist.lbl", layout.ArtistTop(), 13);
  label("song_info.lbl", layout.InfoTop(), 11);
  label("description.lbl", layout.ReasonTop(), 13);
  label("lb_title.lbl", layout.leaderboard_title_top, 11);
  label("rank_first.lbl", -136, 12);
  label("unavailablel.lbl", -136, 12);
  auto ink = *templates.Find("active_normal.color");
  ink.name = "portrait_recommendations.color";
  SceneColor(ink.data, 17, 1, 1, 1, 1);
  edited.objects.push_back(std::move(ink));
  auto heading = *edited.Find("song_title.lbl");
  heading.name = "portrait_tab_recs.lbl";
  RenameSceneString(heading.data, "RB_Yellow.color", "portrait_recommendations.color");
  PortraitPose(heading, 1, 0, layout.tabs_z);
  PortraitText(heading, 17, layout.width - 76);
  edited.objects.push_back(std::move(heading));
  edited.Before("info.grp", {"portrait_tab_recs.lbl"});
  edited.objects.push_back(*header.Find("shell_nav_arrow.tex"));
  for (const bool next : {false, true}) {
    const auto stem = std::string(next ? "portrait_rec_next" : "portrait_rec_previous");
    const auto source_stem = std::string(next ? "shell_arrow_rt" : "shell_arrow_left");
    auto material = *header.Find(source_stem + ".mat");
    material.name = stem + "_arrow.mat";
    auto mesh = *header.Find(source_stem + ".mesh");
    mesh.name = stem + "_arrow.mesh";
    RenameSceneString(mesh.data, source_stem + ".mat", material.name);
    RenameSceneString(mesh.data, source_stem + ".mesh", mesh.name);
    // Copy only the resting header mesh and texture; no pulse/glow Flow follows it.
    PortraitPose(mesh, .42174512f * ui::kPortraitSongArrowScale,
                 (next ? 1.f : -1.f) * layout.NavigationX(), layout.carousel_z);
    edited.objects.push_back(std::move(material));
    edited.objects.push_back(std::move(mesh));
    PortraitHiddenGroup(edited, templates, stem + "_arrow.grp", {stem + "_arrow.mesh"});
    auto arrow = *edited.Find("portrait_tab_recs.lbl");
    arrow.name = stem + ".lbl";
    PortraitPose(arrow, 1, (next ? 1.f : -1.f) * layout.NavigationX(), layout.carousel_z);
    PortraitText(arrow, 24, layout.navigation_size);
    edited.objects.push_back(std::move(arrow));
    edited.Before("recommendations.lst", {stem + ".lbl"});
    PortraitSurface(edited, buttons, stem, layout.navigation_size, 42,
                    (next ? 1.f : -1.f) * layout.NavigationX(), layout.carousel_z, {});
    const auto header_stem = std::string(next ? "portrait_header_next" : "portrait_header_previous");
    PortraitSurface(edited, buttons, header_stem, ui::kPortraitSongArrowTargetSize,
                    ui::kPortraitSongArrowTargetSize,
                    (next ? 1.f : -1.f) * (layout.width / 2 - ui::kPortraitSongArrowInset), layout.tabs_z, {});
  }
  auto empty = *edited.Find("song_title.lbl");
  empty.name = "portrait_rec_empty.lbl";
  PortraitPose(empty, 1, layout.Left(), 90);
  PortraitText(empty, 16, layout.TextWidth(), 0x11);
  edited.objects.push_back(std::move(empty));
  edited.Before("info.grp", {"portrait_rec_empty.lbl"});
  auto position = *edited.Find("portrait_tab_recs.lbl");
  position.name = "portrait_rec_position.lbl";
  PortraitPose(position, 1, -layout.Left(), layout.position_top);
  PortraitText(position, 10, layout.PositionWidth(), 0x14);
  edited.objects.push_back(std::move(position));
  edited.Before("info.grp", {"portrait_rec_position.lbl"});
  const float hero_bottom = layout.DetailsTop() - 2;
  PortraitSurface(edited, buttons, "portrait_rec_hero", layout.width, layout.hero_top - hero_bottom,
                  0, (layout.hero_top + hero_bottom) / 2, "portrait_rec_position.lbl");
  PortraitSurface(edited, buttons, "portrait_rec_details", layout.width, layout.DetailsTop() - layout.DetailsBottom(),
                  0, (layout.DetailsTop() + layout.DetailsBottom()) / 2, "portrait_rec_position.lbl");
  // Reading areas separate small text from the detailed animated backdrop.
  SceneColor(edited.Find("portrait_rec_details.mat")->data, 0x19, .04f, .025f, .09f, .94f);
  const std::vector<std::string> board_members{"mini_leaders.grp", "no_board.grp", "unavailable.grp"};
  for (const auto& name : board_members) edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_rec_board_state.grp", board_members);
  edited.Before("loading_audio.grp", {"portrait_rec_board_state.grp"});
  // A background leaderboard request must not cover the playable song with
  // its landscape loading label. Category text already identifies the card.
  for (const char* name : {"loading.grp", "rec_icon"}) edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_rec_unused.grp", {"loading.grp", "rec_icon"});
  PortraitPose(*edited.Find("loading_audio.grp"), .25f, layout.width / 2 - 12, -152);
  PortraitFooter(edited, buttons, aspect, false, false, true);
  auto marker = *edited.Find("portrait_recommendations.color");
  marker.name = "portrait_songs.color";
  edited.objects.push_back(std::move(marker));
  if (!PortraitLibraryAnimation(edited, aspect, error)) return false;
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitMessages(const std::vector<uint8_t>& source,
                             const std::vector<uint8_t>& button_source, float aspect,
                             std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error, 4 * 1024 * 1024) ||
      !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 2997942 || Crc32(bytes.data(), bytes.size()) != 0xC69F406E)
    return Fail(error, "portrait messages source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait message button template revision changed");
  PortraitScene edited(bytes), templates(buttons);
  const auto layout = ui::PortraitMessages(aspect);
  std::vector<std::string> old_art;
  for (const auto& object : edited.objects)
    if (object.type == "Mesh" || object.name == "message.txt") old_art.push_back(object.name);
  for (const auto& name : old_art) edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_messages_landscape.grp", old_art);
  auto& list = *edited.Find("songs.lst");
  PortraitPose(list, 1, 0, layout.list_z);
  RenameSceneString(list.data, "../resource/lists/list_inbox.milo",
                    "../resource/lists/portrait_inbox.milo");
  const auto label = [&](const char* name, float z, float size) {
    auto& object = *edited.Find(name);
    PortraitPose(object, 1, -layout.width / 2 + 10, z);
    PortraitText(object, size, layout.width - 20, 0x11);
    // Keep the body's alternate bold font separate: sharing its material with
    // the primary font would let emphasis tint every glyph in the paragraph.
    for (const char* font : {"../resource/fonts/pentatonic_regularcond.milo",
                             "../resource/fonts/bureaugrot_wide_bold.milo",
                             "../resource/fonts/pentatonic.milo"})
      RenameSceneString(object.data, font, "../resource/fonts/portrait_splash.milo");
    const auto font = std::search(object.data.begin(), object.data.end(), "../", "../" + 3);
    PutBe32(object.data.data() + (font - object.data.begin()) - 41, 0);
  };
  label("subject.lbl", layout.subject_top, 16);
  label("message.lbl", layout.body_top, 13);
  for (const char* name : {"waiting_txt.lbl", "challenge_txt.lbl"})
    label(name, 35, 14);
  auto heading = *edited.Find("subject.lbl");
  heading.name = "portrait_tab_messages.lbl";
  auto ink = *templates.Find("active_normal.color");
  ink.name = "portrait_messages_ink.color";
  SceneColor(ink.data, 17, 1, 1, 1, 1);
  RenameSceneString(heading.data, "Yellow.color", ink.name);
  edited.objects.push_back(std::move(ink));
  PortraitPose(heading, 1, 0, layout.tabs_z);
  PortraitText(heading, 17, layout.width - 76);
  edited.objects.push_back(std::move(heading));
  edited.Before("inbox.grp", {"portrait_tab_messages.lbl"});
  for (const bool next : {false, true}) {
    const auto stem = std::string(next ? "portrait_header_next" : "portrait_header_previous");
    PortraitSurface(edited, buttons, stem, ui::kPortraitSongArrowTargetSize,
                    ui::kPortraitSongArrowTargetSize,
                    (next ? 1.f : -1.f) * (layout.width / 2 - ui::kPortraitSongArrowInset), layout.tabs_z, {});
  }
  PortraitSurface(edited, buttons, "portrait_messages_reading", layout.width,
                  layout.detail_top - layout.detail_bottom, 0,
                  (layout.detail_top + layout.detail_bottom) / 2, "message.lbl");
  SceneColor(edited.Find("portrait_messages_reading.mat")->data, 0x19, .04f, .025f, .09f, .94f);
  SceneColor(edited.Find("portrait_messages_reading_border.mat")->data, 0x19, .36f, .79f, 1, .9f);
  PortraitSurface(edited, buttons, "portrait_messages_list", layout.width, layout.row_height,
                  0, layout.list_z, "songs.lst");
  SceneColor(edited.Find("portrait_messages_list.mat")->data, 0x19, .04f, .025f, .09f, .94f);
  PortraitPose(*edited.Find("loading.grp"), .7f, 0, 70);
  PortraitFooter(edited, buttons, aspect, true);
  auto marker = *edited.Find("portrait_caption_black.color");
  marker.name = "portrait_songs.color";
  edited.objects.push_back(std::move(marker));
  if (!PortraitLibraryAnimation(edited, aspect, error)) return false;
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitPowerups(const std::vector<uint8_t>& source,
                            const std::vector<uint8_t>& button_source, float aspect,
                            std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes, buttons;
  if (!DecodeStream(source, &bytes, error) || !DecodeStream(button_source, &buttons, error)) return false;
  if (bytes.size() != 2119316 || Crc32(bytes.data(), bytes.size()) != 0x56CA45F0)
    return Fail(error, "portrait powerup panel source revision changed");
  if (buttons.size() != 91806 || Crc32(buttons.data(), buttons.size()) != 0x6AD2DE01)
    return Fail(error, "portrait powerup button template revision changed");
  PortraitScene edited(bytes), templates(buttons);
  const auto layout = ui::PortraitPowerups(aspect);
  auto ink = *templates.Find("active_normal.color");
  ink.name = "portrait_powerup_ink.color";
  SceneColor(ink.data, 17, 1, 1, 1, 1);
  edited.objects.push_back(std::move(ink));
  for (const char* name : {"powerup_panel.grp", "Lists.grp", "Selected_Song.grp",
                           "description.grp", "type_1.grp", "type_2.grp", "type_3.grp"})
    PortraitPose(*edited.Find(name), 1, 0, 0);
  // Keep every native provider and its equipped-item textures. The fourth
  // category action is presented by the footer instead of a fourth icon.
  PortraitPose(*edited.Find("powerup_select.lst"), layout.category_scale,
               -120 * layout.category_scale, layout.categories_z);
  RenameSceneString(edited.Find("powerup_select.lst")->data,
      "../resource/lists/list_powerup_main_select.milo", "../resource/lists/portrait_powerup_categories.milo");
  for (const char* name : {"active_powerups.lst", "passive_powerups_a.lst", "passive_powerups_b.lst"}) {
    PortraitPose(*edited.Find(name), layout.choices_scale, -64 * layout.choices_scale, layout.choices_z);
    RenameSceneString(edited.Find(name)->data,
        "../resource/lists/list_powerup_left_to_right_temp.milo", "../resource/lists/portrait_powerup_choices.milo");
  }
  const std::vector<std::string> hidden{
      "active_powerup.mesh", "passive_powerup.mesh", "active_title.lbl", "passive.lbl",
      "passive_powerup_text.lbl", "warning.lbl", "title_playsong.lbl", "total_cost.lbl",
      "equip_reminder.grp", "frame.mesh", "neon.mesh", "coin.lbl", "cost_text.lbl",
      "diff_panel.mesh", "description_panel.mesh"};
  for (const auto& name : hidden) edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_powerup_landscape.grp", hidden);
  PortraitPose(*edited.Find("cred_column.grp"), 1, 0, 0);
  for (const auto& [name, x] : {std::pair{"your_monies_text.lbl", -layout.width / 4},
                               std::pair{"your_cred.lbl", layout.width / 4}}) {
    auto& label = *edited.Find(name);
    PortraitPose(label, 1, x, layout.currency_z);
    PortraitText(label, 9, layout.width / 2 - 4);
    // The coin and pick share a 45/136 ink center below the glyph top;
    // the primary font's capitals use 20.5/31. Match their visible centers.
    constexpr std::string_view icons = "../resource/fonts/blitz_icons.milo";
    const auto font = std::search(label.data.begin(), label.data.end(), icons.begin(), icons.end());
    if (font == label.data.end()) return Fail(error, "portrait currency icon font changed");
    const auto end = size_t(font - label.data.begin()) + icons.size();
    PutFloat(label.data.data() + end + 4, 24);
    PutFloat(label.data.data() + end + 12, 24 * 45.f / 136 - 9 * 20.5f / 31);
  }
  PortraitSurface(edited, buttons, "portrait_currency", layout.width, layout.currency_height,
                  0, layout.currency_z, {});
  edited.Before("your_cred.lbl", {"portrait_currency_surface.grp"});
  for (const auto& [name, x] : {
      std::pair{"title_deployed.lbl", -layout.width / 3},
      std::pair{"title_seeded.lbl", 0.f}, std::pair{"title_automatic.lbl", layout.width / 3}}) {
    auto& label = *edited.Find(name);
    PortraitPose(label, 1, x, layout.CategoryCaptionZ());
    PortraitText(label, 10 * layout.width / 249, layout.width / 3 - 4);
    RenameSceneString(label.data,
        std::string_view(name) == "title_deployed.lbl" ? "powerup_sel__head_deploy" :
        std::string_view(name) == "title_seeded.lbl" ? "powerup_sel__head_seeded" : "powerup_sel__head_automatic",
        std::string_view(name) == "title_deployed.lbl" ? "rbbz_powerup_overdrive" :
        std::string_view(name) == "title_seeded.lbl" ? "rbbz_powerup_note" : "rbbz_powerup_track");
    RenameSceneString(label.data, "../resource/fonts/pentatonic_regularcond.milo",
                      "../resource/fonts/portrait_splash.milo");
  }
  auto header = *edited.Find("title_deployed.lbl");
  header.name = "header.lbl";
  RenameSceneString(header.data, "rbbz_powerup_overdrive", "powerup_selection_title");
  RenameSceneString(header.data, "RB_Yellow.color", "portrait_powerup_ink.color");
  edited.objects.push_back(std::move(header));
  PortraitPose(*edited.Find("header.lbl"), 1, 0, layout.title_z);
  PortraitText(*edited.Find("header.lbl"), 18, layout.width);
  edited.Before("title_deployed.lbl", {"header.lbl"});
  const float left = -layout.width / 2;
  auto& album = *edited.Find("album_art_panel.mesh");
  const float album_scale = 44.f / 165 * layout.width / 249;
  // The mesh extends down from its top edge, so offset by half its scaled height.
  PortraitPose(album, album_scale, left + 6, layout.card_z + 165 * album_scale / 2);
  for (const auto& [name, z, size] : {
      std::tuple{"selection_title.lbl", 149.f, 12.f},
      std::tuple{"selection_artist.lbl", 139.f, 10.f},
      std::tuple{"name.lbl", layout.name_z, 18.f},
      std::tuple{"powerup_desc.lbl", layout.description_z, 14.f}}) {
    auto& label = *edited.Find(name);
    const bool song = std::string_view(name).starts_with("selection_");
    PortraitPose(label, 1, song ? left + 58 * layout.width / 249 : 0, z);
    PortraitText(label, size * layout.width / 249,
                 song ? layout.width - 64 * layout.width / 249 : layout.width - 20,
                 song ? 0x21 : 0x22);
    if (song) RenameSceneString(label.data, "UIColor.color", "portrait_powerup_ink.color");
    RenameSceneString(label.data, "../resource/fonts/pentatonic_regularcond.milo",
                      "../resource/fonts/portrait_splash.milo");
    // Revision 29 stores fit mode before alignment/width. A description must
    // retain all its lines before its measured rectangle is fitted for drawing.
    const auto font = std::search(label.data.begin(), label.data.end(), "../", "../" + 3);
    PutBe32(label.data.data() + (font - label.data.begin()) - 41, 0);
  }
  PortraitSurface(edited, buttons, "portrait_song_card", layout.width, layout.card_height,
                  0, layout.card_z, "album_art_panel.mesh");
  PortraitSurface(edited, buttons, "portrait_powerup_choices", layout.width, 110,
                  0, -63, {});
  edited.Before("Lists.grp", {"portrait_powerup_choices_surface.grp"});
  PortraitSurface(edited, buttons, "portrait_powerup_details", layout.width, layout.details_height,
                  0, layout.details_z, {});
  // Descriptions occupy their own guest view, so reading never confirms a
  // list item. Each visible item has a separate, generous Details touch target.
  std::vector<std::string> info_controls;
  for (int i = 0; i < 3; ++i) {
    const auto stem = "portrait_powerup_info_" + std::to_string(i);
    auto label = *edited.Find("title_seeded.lbl");
    label.name = stem + ".lbl";
    RenameSceneString(label.data, "rbbz_powerup_note", "rbbz_powerup_details");
    PortraitPose(label, 1, (i - 1) * 64 * layout.choices_scale, layout.InfoZ());
    PortraitText(label, 10 * layout.width / 249, 60 * layout.choices_scale);
    edited.objects.push_back(std::move(label));
    PortraitSurface(edited, buttons, stem, 60 * layout.choices_scale, 24,
                    (i - 1) * 64 * layout.choices_scale, layout.InfoZ(), {});
    info_controls.push_back(stem + ".lbl");
  }
  for (const char* name : {"title_deployed.lbl", "title_seeded.lbl", "title_automatic.lbl"})
    edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_powerup_captions.grp",
      {"title_deployed.lbl", "title_seeded.lbl", "title_automatic.lbl"});
  edited.Find("portrait_powerup_captions.grp")->data[0x21 + 113] = 1;
  PortraitHiddenGroup(edited, templates, "portrait_powerup_controls.grp", info_controls);
  edited.Before("header.lbl", {"portrait_powerup_captions.grp", "portrait_powerup_controls.grp"});
  // The native purchase confirmation remains a modal with its original actions.
  PortraitPose(*edited.Find("confirm.grp"), ui::PortraitPromptScale(aspect), 0, 0);
  PortraitPose(*edited.Find("purchasing_text.lbl"), 1, 0, -174);
  PortraitText(*edited.Find("purchasing_text.lbl"), 11, layout.width);
  PortraitFooter(edited, buttons, aspect);
  auto& help_object = *edited.Find("help.ihp");
  PortraitPose(help_object, 1, 0, -197);
  RenameSceneString(help_object.data, "../resource/inline_help/default.milo",
                    "../resource/inline_help/portrait_actions.milo");
  auto& help = help_object.data;
  // Back and Start use the existing inline-help draw/input bridge. Start is
  // bound to the panel's native song purchase/launch path in the portrait DTA.
  for (size_t at = 13, i = 0; i < Be32(help.data() + 9); ++i) {
    const auto action = Be32(help.data() + at);
    if (action == 1) PutBe32(help.data() + at, 4);
    at += 12 + Be32(help.data() + at + 4);
  }
  RenameSceneString(help, "select", "powerup_sel__head_play");
  auto info_help = help_object;
  info_help.name = "portrait_info_help.ihp";
  // One native Cancel action, presented as Close on the dedicated details view.
  auto& data = info_help.data;
  size_t at = 13;
  while (Be32(data.data() + 9) > 1) {
    const auto end = at + 12 + Be32(data.data() + at + 4);
    if (Be32(data.data() + at) == 2) at = end;
    else {
      data.erase(data.begin() + at, data.begin() + end);
      PutBe32(data.data() + 9, Be32(data.data() + 9) - 1);
    }
  }
  RenameSceneString(data, "inline_help_back", "settings_close");
  edited.objects.push_back(std::move(info_help));
  for (const char* name : {"name.lbl", "powerup_desc.lbl"}) edited.RemoveDrawMember(name);
  PortraitHiddenGroup(edited, templates, "portrait_powerup_info.grp",
      {"portrait_powerup_details_border.mesh", "portrait_powerup_details.mesh",
       "name.lbl", "powerup_desc.lbl", "portrait_info_help.ihp"});
  // Remove duplicate hidden surface membership before adding the modal group.
  edited.objects.erase(std::remove_if(edited.objects.begin(), edited.objects.end(), [](const auto& object) {
    return object.name == "portrait_powerup_details_surface.grp";
  }), edited.objects.end());
  edited.Before("header.lbl", {"portrait_powerup_info.grp"});
  SceneColor(edited.Find("portrait_back.mat")->data, 0x19, 1, .855f, .086f, ui::kFooterOpacity);
  auto marker = *edited.Find("portrait_caption_black.color");
  marker.name = "portrait_powerups.color";
  edited.objects.push_back(std::move(marker));
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitPowerupList(const std::vector<uint8_t>& source, bool categories,
                               std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (categories ? bytes.size() != 3798863 || Crc32(bytes.data(), bytes.size()) != 0xB5D69B6F
                 : bytes.size() != 900120 || Crc32(bytes.data(), bytes.size()) != 0x6DD17D45)
    return Fail(error, "portrait powerup list source revision changed");
  PortraitScene edited(bytes);
  // Embedded flow directories follow the owning UIListDir's delimiter.
  // Its spacing belongs to that first record, before those nested resources.
  constexpr std::array<uint8_t, 4> delimiter{0xAD, 0xDE, 0xAD, 0xDE};
  const auto end = std::search(edited.directory.begin(), edited.directory.end(),
                               delimiter.begin(), delimiter.end());
  if (end == edited.directory.end() || end - edited.directory.begin() < 25)
    return Fail(error, "portrait powerup list directory changed");
  PutFloat(edited.directory.data() + (end - edited.directory.begin()) + 4 - 29,
           categories ? 120.f : 64.f);
  if (categories) {
    PortraitPose(*edited.Find("medium.grp"), 1, 0, 0);
    for (const char* name : {"activePowerup.mesh", "highlight_med.mesh", "glow_med.mesh"})
      PortraitPose(*edited.Find(name), std::string_view(name) == "activePowerup.mesh" ? 1.f : 1.2f, 0, 0);
    std::erase_if(edited.objects, [](const auto& object) { return object.type == "UIListArrow"; });
    PortraitPose(*edited.Find("empty.lbl"), 1, 0, 0);
    PortraitText(*edited.Find("empty.lbl"), 14, 90);
  } else {
    PortraitPose(*edited.Find("powerup.mesh"), 1, 0, 0);
    for (const char* name : {"highlight.mesh", "glow.mesh"})
      PortraitPose(*edited.Find(name), 50.f / 64, 0, 0);
    for (const bool right : {false, true}) {
      auto& arrow = *edited.Find(right ? "arrow_rt.mesh" : "arrow_lt.mesh");
      // Preserve the left arrow's rotation while moving its resting pivot.
      for (size_t at : {size_t(0x15), size_t(0x45)})
        PutFloat(arrow.data.data() + at + 36, right ? 46.f : -46.f);
    }
  }
  return PackStored(edited.Build(), scene, error);
}

bool PreparePortraitShortcuts(const std::vector<uint8_t>& source,
                             std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 3288 || Crc32(bytes.data(), bytes.size()) != 0x3AA51362)
    return Fail(error, "portrait shortcut list source revision changed");
  PortraitScene edited(bytes);
  PutFloat(edited.directory.data() + edited.directory.size() - 29, 48);
  PortraitText(*edited.Find("list.lbl"), 28, 350);
  // Stretch the existing native selection cell along the list's vertical axis.
  auto& slot = edited.Find("slot.mesh")->data;
  for (size_t at : {size_t(0x15), size_t(0x45)}) PutFloat(slot.data() + at + 32, 1.6f);
  return PackStored(edited.Build(), scene, error);
}

bool PrepareSongSelect(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                       std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  constexpr std::string_view old_font = "../resource/fonts/buttons.milo";
  constexpr std::string_view new_font = "../resource/fonts/inline_help_icons.milo";
  constexpr std::string_view label = "sorting.lbl";
  const auto font = std::search(bytes.begin(), bytes.end(), old_font.begin(), old_font.end());
  if (font == bytes.end() ||
      std::search(font + old_font.size(), bytes.end(), old_font.begin(), old_font.end()) !=
          bytes.end() ||
      std::search(bytes.begin(), font, label.begin(), label.end()) == font)
    return Fail(error, "song sort label font changed");
  const size_t at = size_t(font - bytes.begin());
  if (at < 4 || Be32(bytes.data() + at - 4) != old_font.size())
    return Fail(error, "song sort font string length changed");
  PutBe32(bytes.data() + at - 4, static_cast<uint32_t>(new_font.size()));
  bytes.erase(bytes.begin() + at, bytes.begin() + at + old_font.size());
  bytes.insert(bytes.begin() + at, new_font.begin(), new_font.end());
  return PackStored(bytes, scene, error);
}
bool PrepareCalibrationLayout(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                              std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 3336387 || Crc32(bytes.data(), bytes.size()) != 0x375728BA)
    return Fail(error, "calibration layout source revision changed");
  // The resting speaker occupies x=[4,139) of its 256-pixel animation canvas.
  // Center that visible artwork, preserving the space for waves to its right.
  // Both serialized matrices move together; the guest also recomputes world
  // transforms from the local matrix when the panel enters.
  constexpr size_t group = 0x325DFF; // audio_anim.grp
  constexpr float shift = 29.613494f;
  for (size_t offset : {size_t(0x45), size_t(0x75)}) {
    auto* x = bytes.data() + group + offset;
    PutFloat(x, std::bit_cast<float>(Be32(x)) + shift);
  }
  return PackStored(bytes, scene, error);
}
bool PrepareCalibration(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                        std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  constexpr std::array<std::string_view, 4> paths{
      "../image/button_a_calibrate_icon0001.bmp",
      "../image/button_a_calibrate_icon0002.bmp",
      "../image/button_press_ps30001.bmp",
      "../image/button_press_ps30002.bmp"};
  constexpr std::array<uint8_t, 12> header_shape{
      8, 0, 0, 0, 0x18, 4, 1, 0, 1, 0, 1, 0};
  CalibrationButtons originals;
  for (size_t index = 0; index < paths.size(); ++index) {
    const auto path = paths[index];
    const auto at = std::search(bytes.begin(), bytes.end(), path.begin(), path.end());
    if (at == bytes.end() || at == bytes.begin() || *(at - 1) != path.size())
      return Fail(error, "calibration button path changed");
    const size_t path_end = size_t(at - bytes.begin()) + path.size();
    size_t header = bytes.size();
    for (size_t offset = path_end; offset + 31 <= bytes.size() &&
                                   offset < path_end + 80; ++offset) {
      if (std::memcmp(bytes.data() + offset + 4, header_shape.data(),
                      header_shape.size()) == 0 &&
          std::all_of(bytes.begin() + offset + 23, bytes.begin() + offset + 31,
                      [](uint8_t byte) { return byte == 0; })) {
        header = offset;
        break;
      }
    }
    if (header == bytes.size()) return Fail(error, "calibration button header changed");
    size_t payload = header + 31;
    for (size_t mip = 0; mip < 5; ++mip) {
      const size_t dimension = 256u >> mip;
      const size_t size = dimension * dimension;
      if (payload + size > bytes.size())
        return Fail(error, "calibration button texture truncated");
      auto& level = originals[index].emplace_back(size);
      for (size_t byte = 0; byte < size; byte += 2) {
        level[byte] = bytes[payload + byte + 1];
        level[byte + 1] = bytes[payload + byte];
      }
      payload += size;
    }
  }
  if (!ReplaceAnchoredFonts(&bytes, "../resource/fonts/buttons.milo",
                            "../resource/fonts/inline_help_icons.milo",
                            {"cal_audio_desc_pad", "cal_video_desc_pad"}, 2, error)) return false;
  // The two green labels flanking the metronome are separate from the large
  // pressed/released button meshes. Give them the live Interface Confirm font.
  if (!ReplaceAnchoredFonts(&bytes, "../resource/fonts/buttons.milo",
                            "../resource/fonts/inline_help_icons.milo",
                            {"cal_metronome.grp"}, 2, error)) return false;
  std::atomic_store(&g_calibration_buttons,
      std::make_shared<const CalibrationButtons>(std::move(originals)));
  return PackStored(bytes, scene, error);
}
std::shared_ptr<const CalibrationButtons> OriginalCalibrationButtons() {
  return std::atomic_load(&g_calibration_buttons);
}
bool PrepareHudHints(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                     std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (!ReplaceAnchoredFonts(&bytes, "../ui/resource/fonts/buttons.milo",
                            "../ui/resource/fonts/inline_help_icons.milo",
                            {"remedial_help.grp", "tutorial_pane.grp"}, 4, error)) return false;
  return PackStored(bytes, scene, error);
}
bool PrepareGameplayHud(const std::vector<uint8_t>& source, bool portrait, bool touch,
                        std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  // Pinned installed HUD: the tail after the inline Flow directories contains
  // these ordinary root objects in declaration order. Refuse other revisions
  // before editing offsets, including an already generated HUD.
  if (bytes.size() != 4838050 || Crc32(bytes.data(), bytes.size()) != 0xA70E19B1)
    return Fail(error, "gameplay HUD source revision changed");
  if (portrait) {
    // when_stars_align.anim centers the stock row by moving star0 left as
    // each subsequent star advances right from its predecessor. Keep star0
    // at the pivot and reverse only the X offsets in the remaining tracks.
    constexpr std::array<size_t, 5> tracks{0x415B85, 0x415C0E, 0x415C97, 0x415D20, 0x415DA9};
    for (size_t star = 0; star < tracks.size(); ++star) {
      for (size_t frame = 0; frame < 5; ++frame) {
        auto* x = bytes.data() + tracks[star] + frame * 16;
        const float offset = std::bit_cast<float>(Be32(x));
        PutFloat(x, star == 0 || offset == 0 ? 0.f : -offset);
      }
    }
  }
  if (!touch) return PackStored(bytes, scene, error);
  auto copy = [&](size_t from, size_t to) {
    return std::vector<uint8_t>(bytes.begin() + from, bytes.begin() + to);
  };
  auto append_u32 = [](std::vector<uint8_t>& out, uint32_t value) {
    const size_t at = out.size(); out.resize(at + 4);
    PutBe32(out.data() + at, value);
  };
  auto append_string = [&](std::vector<uint8_t>& out, std::string_view value) {
    append_u32(out, uint32_t(value.size()));
    out.insert(out.end(), value.begin(), value.end());
  };
  auto rename = [&](std::vector<uint8_t>& out, std::string_view from, std::string_view to) {
    // All matches in these small measured records are length-prefixed strings.
    for (size_t at = 4; at + from.size() <= out.size(); ++at) {
      if (Be32(out.data() + at - 4) != from.size() ||
          std::memcmp(out.data() + at, from.data(), from.size()) != 0) continue;
      PutBe32(out.data() + at - 4, uint32_t(to.size()));
      out.erase(out.begin() + at, out.begin() + at + from.size());
      out.insert(out.begin() + at, to.begin(), to.end());
      at += to.size();
    }
  };
  auto members = [&](std::vector<uint8_t>& out, std::string_view first,
                     std::initializer_list<std::string_view> names) {
    const auto at = std::search(out.begin(), out.end(), first.begin(), first.end());
    const size_t count = size_t(at - out.begin()) - 8;
    // Group revision 16 ends in a five-byte LOD/environment trailer and marker.
    const auto tail = std::vector<uint8_t>(out.end() - 9, out.end());
    out.resize(count);
    append_u32(out, uint32_t(names.size()));
    for (auto name : names) append_string(out, name);
    out.insert(out.end(), tail.begin(), tail.end());
  };
  auto matrix = [](std::vector<uint8_t>& out, size_t at, float x, float y, float z,
                   float sx, float sy, float sz) {
    for (size_t i = 0; i < 12; ++i) PutFloat(out.data() + at + i * 4, 0);
    PutFloat(out.data() + at, sx);
    PutFloat(out.data() + at + 16, sy);
    PutFloat(out.data() + at + 32, sz);
    PutFloat(out.data() + at + 36, x);
    PutFloat(out.data() + at + 40, y);
    PutFloat(out.data() + at + 44, z);
  };
  struct Object { std::string type, name; std::vector<uint8_t> data; };
  std::vector<Object> objects;
  auto bg = copy(0x48265F, 0x48277C);
  rename(bg, "remedial_help_bg.tex", "help_bg_powerup.tex");
  for (size_t at : {0x19, 0x1D, 0x21}) PutFloat(bg.data() + at, .35f);
  objects.push_back({"Mat", "touch_pause_bg.mat", std::move(bg)});
  auto ink = copy(0x48265F, 0x48277C);
  PutFloat(ink.data() + 0x19, .92f);
  PutFloat(ink.data() + 0x1D, .96f);
  PutFloat(ink.data() + 0x21, 1);
  PutFloat(ink.data() + 0x25, 1);
  rename(ink, "remedial_help_bg.tex", "");
  objects.push_back({"Mat", "touch_pause_ink.mat", std::move(ink)});
  auto group = copy(0x4830A4, 0x4831A5);
  group[0x9A] = 1; // showing, independently of the optional remedial prompt
  members(group, "help_bg.mesh", {"touch_pause_bg.mesh", "touch_pause_icon.grp"});
  objects.push_back({"Group", "touch_pause.grp", std::move(group)});
  auto icon = copy(0x4830A4, 0x4831A5);
  icon[0x9A] = 1;
  matrix(icon, 0x21, 0, 0, 0, 1, 1, 1);
  matrix(icon, 0x51, 0, 0, 0, 1, 1, 1);
  members(icon, "help_bg.mesh", {"touch_pause_l.mesh", "touch_pause_r.mesh"});
  rename(icon, "PanelDir", "touch_pause.grp");
  objects.push_back({"Group", "touch_pause_icon.grp", std::move(icon)});
  for (int part = 0; part < 3; ++part) {
    const std::string_view name = part == 0 ? "touch_pause_bg.mesh" :
                                  part == 1 ? "touch_pause_l.mesh" : "touch_pause_r.mesh";
    auto mesh = copy(0x489711, 0x48996D);
    if (part) {
      const float x = part == 1 ? -10.f : 10.f;
      matrix(mesh, 0x15, x, -1, 0, .06f, 1, .4f);
      matrix(mesh, 0x45, x, -1, 0, .06f, 1, .4f);
    }
    rename(mesh, "remedial_help.grp", part ? "touch_pause_icon.grp" : "touch_pause.grp");
    rename(mesh, "help_bg.mat", part ? "touch_pause_ink.mat" : "touch_pause_bg.mat");
    rename(mesh, "help_bg.mesh", name);
    objects.push_back({"Mesh", std::string(name), std::move(mesh)});
  }
  auto anim = copy(0x489BBF, 0x489C5F);
  // Preserve the native interpolation and timing, normalized for a Group
  // instead of the UILabel whose base scale is 1.875.
  for (size_t key : {0x63, 0x73, 0x83})
    for (size_t axis = 0; axis < 3; ++axis) {
      auto* value = anim.data() + key + axis * 4;
      PutFloat(value, std::bit_cast<float>(Be32(value)) / 1.875f);
    }
  rename(anim, "remedial_help.lbl", "touch_pause_icon.grp");
  objects.push_back({"PropAnim", "touch_pause.anim", std::move(anim)});

  // Add the new group to the title's hud.grp traversal. The original remedial
  // Flow, its visibility, its arrows, label, and animations remain guest-owned.
  auto hud = copy(0x482C71, 0x482D6E);
  members(hud, "band_meter_hud", {"band_meter_hud", "blitz_text_flat.mesh",
                                   "blitz_meter.grp", "touch_pause.grp"});
  bytes.erase(bytes.begin() + 0x482C71, bytes.begin() + 0x482D6E);
  bytes.insert(bytes.begin() + 0x482C71, hud.begin(), hud.end());
  std::vector<uint8_t> declarations;
  uint32_t name_bytes = 0;
  for (const auto& object : objects) {
    append_string(declarations, object.type);
    append_string(declarations, object.name);
    name_bytes += uint32_t(object.name.size() + 1);
    bytes.insert(bytes.end(), object.data.begin(), object.data.end());
  }
  PutBe32(bytes.data() + 0x1C, 226 + uint32_t(objects.size()) * 2);
  PutBe32(bytes.data() + 0x20, 2047 + name_bytes);
  PutBe32(bytes.data() + 0x25, 112 + uint32_t(objects.size()));
  bytes.insert(bytes.begin() + 0xDA4, declarations.begin(), declarations.end());
  PortraitScene edited(bytes);
  // The bar, animated Blitz logo and points keep their native animation keys.
  // One identity parent moves them together below the device's top cutout.
  auto top = *edited.Find("touch_pause_icon.grp");
  top.name = "portrait_top_hud.grp";
  members(top.data, "touch_pause_l.mesh", {"blitz_meter.grp", "blitz_text_flat.mesh"});
  PortraitPose(top, 1, 0, 0);
  for (const char* name : {"blitz_meter.grp", "blitz_points.lbl"})
    PortraitParent(*edited.Find(name), top.name);
  RenameSceneString(edited.Find("blitz_logo_root.trans")->data, "PanelDir", top.name);
  edited.RemoveDrawMember("blitz_meter.grp");
  edited.RemoveDrawMember("blitz_text_flat.mesh");
  edited.Before("band_meter_hud", {top.name});
  edited.objects.push_back(std::move(top));
  return PackStored(edited.Build(), scene, error);
}
bool PreparePowerupSelect(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                          std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (!ReplaceAnchoredFonts(&bytes, "../resource/fonts/buttons.milo",
                            "../resource/fonts/inline_help_icons.milo",
                            {"active_description"}, 1, error)) return false;
  return PackStored(bytes, scene, error);
}
bool PrepareErrorReturn(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                        std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (!ReplaceAnchoredFonts(&bytes, "../resource/fonts/buttons.milo",
                            "../resource/fonts/inline_help_icons.milo",
                            {"error_return_button"}, 1, error)) return false;
  return PackStored(bytes, scene, error);
}
bool PrepareAccountLink(const std::vector<uint8_t>& source, std::vector<uint8_t>* scene,
                        std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (!ReplaceAnchoredFonts(&bytes, "../resource/fonts/buttons.milo",
                            "../resource/fonts/inline_help_icons.milo",
                            {"link_facebook_email_sent"}, 1, error)) return false;
  return PackStored(bytes, scene, error);
}
std::shared_ptr<const std::vector<uint8_t>> OriginalBc3() {
  return std::atomic_load(&g_original_bc3);
}
std::shared_ptr<const std::vector<uint8_t>> PrivateBc3() {
  return std::atomic_load(&g_private_bc3);
}
std::shared_ptr<const std::vector<uint8_t>> OriginalButtonsBc3() {
  return std::atomic_load(&g_original_buttons_bc3);
}
std::shared_ptr<const RetailPsPads> OriginalPsPadsBc3() {
  return std::atomic_load(&g_original_ps_pads_bc3);
}
}  // namespace rb_blitz::ui::input_font
