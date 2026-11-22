// SPDX-License-Identifier: GPL-2.0-only
#include "ui/input_font_scene.h"

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
    for (uint32_t action = 1; action < kHintCount; ++action) {
      // Slot 0 remains the header's Previous cue. The splash owns a distinct
      // image slot, while its label keeps the already-authored 0x80 glyph.
      const uint32_t code = action == kSplashStartHint ? kSplashGlyphCode : GlyphCode(action);
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
    std::vector<size_t> markers;
    for (size_t i = at; i + 4 <= bytes.size(); ++i)
      if (Be32(bytes.data() + i) == 0xADDEADDE) markers.push_back(i);
    markers.erase(markers.begin(), markers.end() - count - 1);
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

void PortraitText(PortraitObject& object, float size, float width, uint32_t alignment = 0x22) {
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

void PortraitFooter(PortraitScene& scene, const std::vector<uint8_t>& buttons,
                    float aspect, bool back_only = false, bool confirm_only = false) {
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
  for (size_t i = 0; i < 3; ++i) {
    const auto slot = ui::PortraitCalibrationFooter(aspect, i);
    PortraitSurface(scene, buttons,
        i == 0 ? "portrait_confirm" : i == 1 ? "portrait_back" : "portrait_retry",
        slot.width, slot.height, slot.x, slot.z, {});
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
  // These wheels save as they move, so the page only needs its native Back hint.
  auto& actions = edited.Find("nav.ihp")->data;
  for (size_t at = 13, index = 0; index < Be32(actions.data() + 9);) {
    const size_t end = at + 12 + Be32(actions.data() + at + 4);
    if (Be32(actions.data() + at) == 2) { at = end; ++index; }
    else {
      actions.erase(actions.begin() + at, actions.begin() + end);
      PutBe32(actions.data() + 9, Be32(actions.data() + 9) - 1);
    }
  }
  const float width = portrait ? ui::PortraitSplash(aspect).width : 640.f;
  const float scale = std::min(width * .84f / 190, 1.f);
  const auto label_template = *edited.Find("header.lbl");
  size_t index = 0;
  for (const char* slider : {"sound_effects_slider.sld", "track_sound_slider.sld"}) {
    const float top = index == 0 ? 100.f : -15.f;
    auto label = label_template;
    label.name = index == 0 ? "interface_scale.lbl" : "gameplay_scale.lbl";
    PortraitPose(label, 1, 0, top);
    PortraitText(label, 18, width * .84f);
    label.data[PortraitMatrix(label) + 113] = 1;
    edited.Before(slider, {label.name});
    edited.objects.push_back(std::move(label));
    PortraitPose(*edited.Find(slider), scale, -88.75f * scale, top - 40);
    ++index;
  }
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
  PortraitTitle(edited, aspect);
  if (crc == 0x375728BA) {
    RenameSceneString(edited.Find("header.lbl")->data, "audio_video_options", "cal_welcome_title");
    struct Label { const char* name; float z, size; };
    for (const auto& label : {Label{"audio_title.lbl", 117, 16}, {"video_title.lbl", 117, 16},
                              {"cal_audio_instructions.lbl", ui::kPortraitCalibrationInstructionsZ, 11.5f},
                              {"cal_video_instructions.lbl", ui::kPortraitCalibrationInstructionsZ, 11.5f},
                              {"cal_countdown.lbl", -16, 40}, {"cal_instructions.lbl", -128, 10.5f},
                              {"cal_reps.lbl", -105, 10}, {"cal_setting.lbl", -153, 11}}) {
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
    // Resting speaker alpha bounds (4,65)-(139,205) mapped through the quad's
    // UVs; sound waves expand to its right without moving the speaker body.
    art("audio_anim.grp", .65f * fit, -223.3656f * .65f * fit,
        ui::kPortraitCalibrationMeterZ - 39.78075f * .65f * fit);
    art("prog_bar.grp", 1.18f * fit, -.3215f * 1.18f * fit,
        -82 + 65.4355f * 1.18f * fit);
    const float metro = width * .64f / 599.163f;
    art("cal_metronome.grp", metro, -1.3685f * metro, ui::kPortraitCalibrationMeterZ + 73.7405f * metro);
    const float background = width * .64f / 345.003f;
    art("cal_bg.grp", background, .8785f * background, ui::kPortraitCalibrationMeterZ + 84.6205f * background);
    const float visual = width * .65f / 511.003f;
    art("visuals_anim.grp", visual, -176.5365f * visual, ui::kPortraitCalibrationMeterZ - 296.4875f * visual);
    // Keep the input prompt in the right margin, clear of the meter and its
    // moving needle. Both platform meshes share this independent pose.
    for (const char* name : {"button_press_xbox.mesh", "button_press_ps3.mesh"}) {
      art(name, 1.5573348f * visual * .5f, width * .42f, ui::kPortraitCalibrationMeterZ);
      auto& object = *edited.Find(name);
      const auto matrix = PortraitMatrix(object);
      std::copy_n(object.data.data() + matrix, 48, object.data.data() + matrix + 48);
    }
    for (const char* name : {"cal_hardware_audio_illustration.grp", "cal_hardware_video_illustration.grp"})
      art(name, .75f * fit, 0, 30);
  }
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

bool PreparePortraitSongLoading(const std::vector<uint8_t>& source,
                                std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 9828070 || Crc32(bytes.data(), bytes.size()) != 0x3DCC8337)
    return Fail(error, "portrait song loading source revision changed");
  PortraitScene edited(bytes);
  auto& prompt = *edited.Find("loading.lbl");
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
  PortraitScene edited(bytes), templates(buttons);
  // UIListDir's tail holds orientation, preview fields and element spacing.
  PutFloat(edited.directory.data() + edited.directory.size() - 29, ui::kPortraitSongRowStep);
  std::erase_if(edited.objects, [](const auto& object) {
    return object.type == "UIListHighlight" || object.type == "UIListMesh";
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
    const float height = header ? 30.f : ui::kPortraitSongRowHeight;
    for (size_t at : {size_t(0x15), size_t(0x45)}) {
      SceneMatrix(mesh.data, at, 420.f / 300, height / 30, -210, 0, height / 2);
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
  SceneColor(edited.Find("portrait_header.mat")->data, 0x19, .035f, .025f, .07f, .86f);
  // Draw these native meshes with each populated label's final slot transform.
  // The song provider does not create elements for unfamiliar mesh widgets.
  PortraitHiddenGroup(edited, templates, "portrait_rows.grp",
                      {"portrait_row.mesh", "portrait_selected.mesh", "portrait_header.mesh"});
  edited.RemoveDrawMember("slot.mesh");
  std::vector<std::string> original_art{"slot.mesh"};
  if (grouped) { edited.RemoveDrawMember("highlight.mesh"); original_art.push_back("highlight.mesh"); }
  PortraitHiddenGroup(edited, templates, "portrait_old_rows.grp", original_art);
  return PackStored(edited.Build(), scene, error);
}

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
bool PrepareTouchHud(const std::vector<uint8_t>& source,
                     std::vector<uint8_t>* scene,
                     std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  // Pinned installed HUD: the tail after the inline Flow directories contains
  // these ordinary root objects in declaration order. Refuse other revisions
  // before editing offsets, including an already generated HUD.
  if (bytes.size() != 4838050 || Crc32(bytes.data(), bytes.size()) != 0xA70E19B1)
    return Fail(error, "touch HUD source revision changed");
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
