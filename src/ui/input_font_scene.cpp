// SPDX-License-Identifier: GPL-2.0-only
#include "ui/input_font_scene.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
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
std::atomic<std::shared_ptr<const std::vector<uint8_t>>> g_original_buttons_bc3;
std::atomic<std::shared_ptr<const RetailPsPads>> g_original_ps_pads_bc3;
std::atomic<std::shared_ptr<const CalibrationButtons>> g_calibration_buttons;
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
  g_original_buttons_bc3.store(
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
  g_original_ps_pads_bc3.store(std::make_shared<const RetailPsPads>(std::move(pads)));
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
  g_calibration_buttons.store(
      std::make_shared<const CalibrationButtons>(std::move(originals)));
  return PackStored(bytes, scene, error);
}
std::shared_ptr<const CalibrationButtons> OriginalCalibrationButtons() {
  return g_calibration_buttons.load();
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
  return g_original_bc3.load();
}
std::shared_ptr<const std::vector<uint8_t>> PrivateBc3() {
  return g_private_bc3.load();
}
std::shared_ptr<const std::vector<uint8_t>> OriginalButtonsBc3() {
  return g_original_buttons_bc3.load();
}
std::shared_ptr<const RetailPsPads> OriginalPsPadsBc3() {
  return g_original_ps_pads_bc3.load();
}
}  // namespace rb_blitz::ui::input_font
