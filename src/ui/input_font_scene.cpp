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
           std::bit_cast<float>(Be32(bytes.data() + text_size)) * .7f);
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
  const auto layout = ui::PortraitSettings(aspect);
  // Keep the cropped marquee/backdrop at its authored size. Uniformly fit
  // only the title and six-row list, retaining the original vertical anchors.
  for (size_t matrix : {size_t(0x41EC), size_t(0x421C), size_t(0x432C), size_t(0x435C)})
    for (size_t axis : {size_t(0), size_t(16), size_t(32)})
      PutFloat(bytes.data() + matrix + axis, layout.content_scale);

  // InlineHelp's first entry is Select; its second is the native Cancel/Back
  // action. Keep Back alone and center it inside the bottom button. The normal
  // input hint formatter still supplies the current device's icon and locale.
  for (size_t matrix : {size_t(0x3F6C), size_t(0x3F9C)})
    SceneMatrix(bytes, matrix, 1, 1, 0, 0, -190);
  PutBe32(bytes.data() + 0x3EF1, 1);

  // Derive the button from the installed options slot, including its packed
  // vertex format and valid face trailer. Its material already lives in this
  // scene; the list's own materials and selection behavior remain independent.
  std::vector<uint8_t> mesh(buttons.begin() + 91238, buttons.begin() + 91635);
  for (size_t matrix : {size_t(0x15), size_t(0x45)}) {
    PutFloat(mesh.data() + matrix, layout.back_width / 300.f);
    PutFloat(mesh.data() + matrix + 36, -layout.back_width / 2);
    PutFloat(mesh.data() + matrix + 44, layout.back_top);
  }
  RenameSceneString(mesh, "options_menu.grp", "options.grp");
  RenameSceneString(mesh, "slot.mesh", "portrait_back.mesh");
  SceneColor(bytes, 0x3A57 + 0x19, .063f, .051f, .11f, .8f);
  bytes.insert(bytes.end(), mesh.begin(), mesh.end());

  // Apply variable-length edits from the end so every measured source offset
  // above remains valid. Draw the background before the InlineHelp text.
  bytes.erase(bytes.begin() + 0x3EF5, bytes.begin() + 0x3F07);
  PutBe32(bytes.data() + 0x3CF5, 5);
  std::vector<uint8_t> member;
  SceneString(member, "portrait_back.mesh");
  bytes.insert(bytes.begin() + 0x3D08, member.begin(), member.end());
  std::vector<uint8_t> declaration;
  SceneString(declaration, "Mesh"); SceneString(declaration, "portrait_back.mesh");
  bytes.insert(bytes.begin() + 0x2AF, declaration.begin(), declaration.end());
  PutBe32(bytes.data() + 0x1E, 44);
  PutBe32(bytes.data() + 0x22, 390 + 19); // name table includes each terminating NUL
  PutBe32(bytes.data() + 0x27, 21);
  return PackStored(bytes, scene, error);
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
  std::vector<PortraitObject> added;
  auto material = std::vector<uint8_t>(buttons.begin() + 88961, buttons.begin() + 89242);
  SceneColor(material, 0x19, .063f, .051f, .11f, .92f);
  RenameSceneString(material, "options_slot.tex", "");
  added.push_back({"Mat", "portrait_action.mat", std::move(material)});
  auto color = std::vector<uint8_t>(bytes.begin() + 143303, bytes.begin() + 143340);
  SceneColor(color, 17, .97f, .97f, .99f, 1);
  added.push_back({"UIColor", "portrait_action.color", std::move(color)});
  std::vector<uint8_t> members;
  for (const char* action : {"continue", "back", "skip"}) {
    const std::string name = std::string("portrait_") + action + ".mesh";
    auto mesh = std::vector<uint8_t>(buttons.begin() + 91238, buttons.begin() + 91635);
    // The slot's vertices lie in X/Y; retain its authored rotation into X/Z.
    // Each mesh follows its own native hint, including the responsive scale.
    for (size_t matrix : {size_t(0x15), size_t(0x45)}) {
      PutFloat(mesh.data() + matrix, layout.button_width / (300 * layout.button_scale));
      PutFloat(mesh.data() + matrix + 20, 38 / (30 * layout.button_scale));
      PutFloat(mesh.data() + matrix + 36, -layout.button_width / (2 * layout.button_scale));
      PutFloat(mesh.data() + matrix + 44, 12 / layout.button_scale);
    }
    RenameSceneString(mesh, "options_menu.grp", std::string(action) + ".ihp");
    RenameSceneString(mesh, "slot.mat", "portrait_action.mat");
    RenameSceneString(mesh, "slot.mesh", name);
    SceneString(members, name);
    added.push_back({"Mesh", name, std::move(mesh)});
  }
  // Apply changes to the measured hint records before inserting their longer
  // color references. Their labels keep the normal device/localization path.
  struct Edit { size_t begin, end; std::vector<uint8_t> data; };
  std::vector<Edit> edits;
  for (const auto [begin, end] : {std::pair<size_t, size_t>{147259, 147550},
                                {144026, 144321}, {143406, 143697}}) {
    auto hint = std::vector<uint8_t>(bytes.begin() + begin, bytes.begin() + end);
    RenameSceneString(hint, "UIColor.color", "portrait_action.color");
    edits.push_back({begin, end, std::move(hint)});
  }
  auto group = std::vector<uint8_t>(bytes.begin() + 142967, bytes.begin() + 143303);
  constexpr std::string_view first = "facebook.mesh";
  const auto member = std::search(group.begin(), group.end(), first.begin(), first.end());
  if (member == group.end()) return Fail(error, "portrait connection draw group changed");
  const size_t at = size_t(member - group.begin());
  if (at < 8 || Be32(group.data() + at - 8) != 8)
    return Fail(error, "portrait connection draw count changed");
  PutBe32(group.data() + at - 8, 11);
  group.insert(group.begin() + at - 4, members.begin(), members.end());
  edits.push_back({142967, 143303, std::move(group)});
  for (const auto& edit : edits) {
    bytes.erase(bytes.begin() + edit.begin, bytes.begin() + edit.end);
    bytes.insert(bytes.begin() + edit.begin, edit.data.begin(), edit.data.end());
  }
  std::vector<uint8_t> declarations;
  uint32_t names = 0;
  for (const auto& object : added) {
    SceneString(declarations, object.type); SceneString(declarations, object.name);
    names += uint32_t(object.name.size() + 1);
    bytes.insert(bytes.end(), object.data.begin(), object.data.end());
  }
  PutBe32(bytes.data() + 0x26, 529 + names);
  PutBe32(bytes.data() + 0x2B, 36);
  bytes.insert(bytes.begin() + 0x3B7, declarations.begin(), declarations.end());
  return PackStored(bytes, scene, error);
}

bool PreparePortraitSplash(const std::vector<uint8_t>& source, float aspect,
                           std::vector<uint8_t>* scene, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!DecodeStream(source, &bytes, error)) return false;
  if (bytes.size() != 10448706 || Crc32(bytes.data(), bytes.size()) != 0x6234458E)
    return Fail(error, "portrait splash art source revision changed");
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
  return PackStored(bytes, scene, error);
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
  PutFloat(bytes.data() + 0x54A, 58.5f);
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
    if (border) SceneColor(material, 0x19, .90f, .85f, .95f, .19f);
    else if (selected) SceneColor(material, 0x19, 1, .855f, .086f, 1);
    else SceneColor(material, 0x19, .063f, .051f, .11f, .67f);
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
  if (bytes.size() != 72649 || Crc32(bytes.data(), bytes.size()) != 0x31A0083B)
    return Fail(error, "portrait button font source revision changed");
  // A private copy keeps the menu's white/black labels independent of the
  // moving background lights, without changing the shared font elsewhere.
  bytes[0x104BB + 0x29] = 0; // Mat::use_environ
  return PackStored(bytes, scene, error);
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
  return PackStored(bytes, scene, error);
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
