// SPDX-License-Identifier: GPL-2.0-only
#include "ui/boot_loading_art.h"

#include "assets/texture_codec.h"
#include "settings/ultimate_adapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>

// Milo chunks use raw DEFLATE. Keep stb's decoder private to this translation
// unit, as the installed button-scene reader does for its own Milo records.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#include "../../rexglue-sdk/thirdparty/stb/stb_image.h"

namespace rb_blitz::ui::boot_loading_art {
namespace {

constexpr std::string_view kScenePath =
    "ui/loading_spinners/gen/spinner_autosave.milo_xbox";
constexpr std::string_view kTexturePath = "textures/spinner_plate_keep.bmp";
constexpr size_t kSheetWidth = 512;
constexpr size_t kSheetHeight = 128;
constexpr size_t kBc3Bytes = kSheetWidth * kSheetHeight;
constexpr size_t kMiloHeaderBytes = 0x810;

bool Fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}

uint32_t Le32(const uint8_t* bytes) {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
         (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

uint32_t Be32(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
         (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

bool ReadScene(const std::filesystem::path& game_data_root,
               std::vector<uint8_t>* scene, std::string* error) {
  namespace adapter = rb_blitz::settings::ultimate_adapter;
  const auto header_path = game_data_root / "gen" / "main_xbox.hdr";
  std::vector<uint8_t> header;
  if (!adapter::ReadFileBytes(header_path, &header, error)) return false;
  adapter::ArkIndex index;
  if (!adapter::ReadArkIndex(header.data(), header.size(), &index, error)) return false;
  const auto* entry = index.Find(kScenePath);
  if (!entry || entry->compressed() || entry->size < kMiloHeaderBytes ||
      entry->size > 1024 * 1024)
    return Fail(error, "installed autosave spinner scene is missing or invalid");

  uint64_t local_offset = entry->offset;
  size_t part = 0;
  while (part < index.part_sizes.size() && local_offset >= index.part_sizes[part]) {
    local_offset -= index.part_sizes[part++];
  }
  if (part >= index.part_sizes.size() || part >= index.parts.size() ||
      entry->size > index.part_sizes[part] - local_offset)
    return Fail(error, "installed autosave spinner crosses an ARK boundary");

  // The validated index supplies a relative path such as gen/main_xbox_0.ark.
  // Do not follow an absolute or parent-relative path from a damaged header.
  const std::filesystem::path relative(index.parts[part]);
  if (relative.is_absolute() || relative.empty() ||
      std::find(relative.begin(), relative.end(), "..") != relative.end())
    return Fail(error, "invalid autosave spinner ARK path");
  std::ifstream file(game_data_root / relative, std::ios::binary);
  if (!file || local_offset > uint64_t(std::numeric_limits<std::streamoff>::max()))
    return Fail(error, "cannot open installed autosave spinner ARK");
  scene->resize(static_cast<size_t>(entry->size));
  file.seekg(static_cast<std::streamoff>(local_offset));
  if (!file.read(reinterpret_cast<char*>(scene->data()),
                 static_cast<std::streamsize>(scene->size())))
    return Fail(error, "cannot read installed autosave spinner scene");
  return true;
}

bool DecodeMilo(const std::vector<uint8_t>& scene, std::vector<uint8_t>* stream,
                std::string* error) {
  constexpr uint32_t kStored = 0xCABEDEAF;
  constexpr uint32_t kDeflate = 0xCDBEDEAF;
  constexpr uint32_t kStoredChunk = 0x01000000;
  constexpr uint32_t kChunkMask = 0x00FFFFFF;
  if (scene.size() < kMiloHeaderBytes || Le32(scene.data() + 4) != kMiloHeaderBytes)
    return Fail(error, "invalid autosave spinner Milo header");
  const uint32_t format = Le32(scene.data());
  const uint32_t chunks = Le32(scene.data() + 8);
  if ((format != kStored && format != kDeflate) || chunks == 0 || chunks > 512)
    return Fail(error, "unsupported autosave spinner Milo stream");
  stream->clear();
  size_t position = kMiloHeaderBytes;
  for (uint32_t i = 0; i < chunks; ++i) {
    const uint32_t word = Le32(scene.data() + 16 + i * 4);
    const size_t stored_size = word & kChunkMask;
    if (stored_size == 0 || stored_size > scene.size() - position)
      return Fail(error, "truncated autosave spinner Milo chunk");
    if (format == kStored || (word & kStoredChunk)) {
      if (stored_size > 2 * 1024 * 1024 - stream->size())
        return Fail(error, "autosave spinner Milo exceeds size limit");
      stream->insert(stream->end(), scene.begin() + position,
                     scene.begin() + position + stored_size);
    } else {
      if (stored_size < 5)
        return Fail(error, "short autosave spinner Milo chunk");
      const uint32_t expanded = Le32(scene.data() + position);
      if (expanded == 0 || expanded > 1024 * 1024 ||
          expanded > 2 * 1024 * 1024 - stream->size())
        return Fail(error, "invalid autosave spinner Milo chunk size");
      const size_t before = stream->size();
      stream->resize(before + expanded);
      const int got = stbi_zlib_decode_noheader_buffer(
          reinterpret_cast<char*>(stream->data() + before), static_cast<int>(expanded),
          reinterpret_cast<const char*>(scene.data() + position + 4),
          static_cast<int>(stored_size - 4));
      if (got != expanded) return Fail(error, "autosave spinner Milo DEFLATE failed");
    }
    position += stored_size;
  }
  if (position != scene.size()) return Fail(error, "autosave spinner Milo has trailing bytes");
  return true;
}

bool DecodePlate(const std::vector<uint8_t>& stream,
                 std::vector<uint8_t>* rgba, std::string* error) {
  const auto path = std::search(stream.begin(), stream.end(),
                                kTexturePath.begin(), kTexturePath.end());
  if (path == stream.end()) return Fail(error, "autosave spinner plate texture is missing");
  const size_t path_at = size_t(path - stream.begin());
  const size_t path_end = path_at + kTexturePath.size();
  // The installed scene's RndTex record has 11 bytes between its source path
  // and the 23-byte bitmap header, then an eight-byte zero preamble.
  constexpr size_t kGap = 11;
  constexpr size_t kRecordBytes = 31;
  const size_t record = path_end + kGap;
  const size_t payload = record + kRecordBytes;
  constexpr std::array<uint8_t, 12> kShape{
      8, 0, 0, 0, 0x18, 3, 2, 0, 0, 128, 2, 0};
  if (path_at < 4 || Be32(stream.data() + path_at - 4) != kTexturePath.size() ||
      payload > stream.size() || kBc3Bytes > stream.size() - payload ||
      !std::equal(kShape.begin(), kShape.end(), stream.begin() + record + 4) ||
      std::any_of(stream.begin() + record + 23, stream.begin() + payload,
                  [](uint8_t byte) { return byte != 0; }))
    return Fail(error, "autosave spinner plate format changed");

  std::vector<uint8_t> blocks(stream.begin() + payload,
                              stream.begin() + payload + kBc3Bytes);
  rb_blitz::assets::SwapSixteenBitWords(blocks.data(), blocks.size());
  rgba->resize(kSheetWidth * kSheetHeight * 4);
  rb_blitz::assets::DecodeDxt5(blocks.data(), kSheetWidth, kSheetHeight,
                               rgba->data());
  return true;
}

}  // namespace

bool LoadBootLoadingFrames(const std::filesystem::path& game_data_root,
                           BootLoadingFrames* frames, std::string* error) {
  if (!frames) return Fail(error, "missing autosave spinner output");
  std::vector<uint8_t> scene;
  std::vector<uint8_t> stream;
  std::vector<uint8_t> sheet;
  if (!ReadScene(game_data_root, &scene, error) ||
      !DecodeMilo(scene, &stream, error) ||
      !DecodePlate(stream, &sheet, error)) return false;

  struct PolarSample {
    uint16_t x = 0;
    uint8_t y = 0;
    uint8_t edge_alpha = 0;
  };
  std::vector<PolarSample> samples(kFrameSize * kFrameSize);
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kInnerRadius = 7.0;
  constexpr double kOuterRadius = 58.0;
  constexpr double kCenter = (kFrameSize - 1) / 2.0;
  // The retail spinner bends the four-emblem strip around eight wedges.
  // Its authored MatAnim translates the texture by four repeats in 240 frames
  // at 30 fps. One texture repeat therefore lasts two seconds. Sample that
  // cycle at the authored 30 fps while preserving the title's material color.
  for (size_t y = 0; y < kFrameSize; ++y) {
    for (size_t x = 0; x < kFrameSize; ++x) {
      const double dx = double(x) - kCenter;
      const double dy = double(y) - kCenter;
      const double radius = std::hypot(dx, dy);
      if (radius <= kInnerRadius || radius >= kOuterRadius) continue;
      double angle = std::atan2(dy, dx);
      if (angle < 0) angle += 2 * kPi;
      const size_t source_x =
          static_cast<size_t>(angle * (2 * kSheetWidth) / (2 * kPi)) % kSheetWidth;
      const size_t source_y = std::min<size_t>(
          kSheetHeight - 1,
          static_cast<size_t>((radius - kInnerRadius) * kSheetHeight /
                              (kOuterRadius - kInnerRadius)));
      const double edge = std::min({radius - kInnerRadius,
                                    kOuterRadius - radius, 1.0});
      samples[y * kFrameSize + x] = {
          static_cast<uint16_t>(source_x), static_cast<uint8_t>(source_y),
          static_cast<uint8_t>(edge * 255)};
    }
  }
  BootLoadingFrames ready;
  for (size_t frame = 0; frame < ready.size(); ++frame) {
    auto& pixels = ready[frame];
    pixels.resize(kFrameSize * kFrameSize * 4);
    const size_t phase = frame * kSheetWidth / ready.size();
    for (size_t pixel = 0; pixel < samples.size(); ++pixel) {
      const auto& sample = samples[pixel];
      if (!sample.edge_alpha) continue;
      const auto* source = sheet.data() +
          (size_t(sample.y) * kSheetWidth + (sample.x + phase) % kSheetWidth) * 4;
      auto* target = pixels.data() + pixel * 4;
      target[0] = uint8_t(uint32_t(source[0]) * 196 / 255);
      target[1] = uint8_t(uint32_t(source[1]) * 253 / 255);
      target[2] = uint8_t(uint32_t(source[2]) * 122 / 255);
      target[3] = uint8_t(uint32_t(source[3]) * sample.edge_alpha / 255);
    }
  }
  *frames = std::move(ready);
  if (error) error->clear();
  return true;
}

}  // namespace rb_blitz::ui::boot_loading_art
