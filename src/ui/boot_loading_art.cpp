// SPDX-License-Identifier: GPL-2.0-only
#include "ui/boot_loading_art.h"

#include "assets/texture_codec.h"
#include "settings/ultimate_adapter.h"

#include <algorithm>
#include <array>
#include <bit>
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
constexpr std::string_view kBloomPath =
    "textures/spinner_plate_bloom_keep.bmp";
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

float BeFloat(const uint8_t* bytes) {
  return std::bit_cast<float>(Be32(bytes));
}

float BeHalf(const uint8_t* bytes) {
  const uint16_t bits = (uint16_t(bytes[0]) << 8) | bytes[1];
  const float sign = bits & 0x8000 ? -1.0f : 1.0f;
  const int exponent = (bits >> 10) & 31;
  const int mantissa = bits & 1023;
  if (!exponent) return sign * std::ldexp(float(mantissa), -24);
  if (exponent == 31) return std::numeric_limits<float>::quiet_NaN();
  return sign * std::ldexp(float(mantissa + 1024), exponent - 25);
}

struct MeshVertex {
  float x, y, u, v;
};

bool ReadWedge(const std::vector<uint8_t>& stream,
               std::array<MeshVertex, 21>* vertices, std::string* error) {
  constexpr std::string_view kName = "Shape1.mesh";
  const auto name = std::find_end(stream.begin(), stream.end(),
                                  kName.begin(), kName.end());
  if (name == stream.end()) return Fail(error, "autosave spinner wedge is missing");
  const size_t at = size_t(name - stream.begin());
  constexpr std::array<uint8_t, 13> kHeader{
      0, 0, 0, 21, 1, 0, 0, 0, 36, 0, 0, 0, 1};
  const size_t header = at + 20;
  const size_t data = header + kHeader.size();
  if (data + 21 * 36 + 4 + 7 * 6 > stream.size() ||
      !std::equal(kHeader.begin(), kHeader.end(), stream.begin() + header) ||
      Be32(stream.data() + data + 21 * 36) != 7)
    return Fail(error, "autosave spinner wedge format changed");
  for (size_t i = 0; i < vertices->size(); ++i) {
    const auto* source = stream.data() + data + i * 36;
    auto& vertex = (*vertices)[i];
    vertex = {BeFloat(source), BeFloat(source + 4),
              BeHalf(source + 16), BeHalf(source + 18)};
    if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
        !std::isfinite(vertex.u) || !std::isfinite(vertex.v) ||
        std::abs(vertex.x) > 2 || std::abs(vertex.y) > 2 ||
        vertex.u < 0 || vertex.u > 1 || vertex.v < 0 || vertex.v > 1)
      return Fail(error, "invalid autosave spinner wedge vertex");
  }
  for (size_t face = 0; face < 7; ++face) {
    const auto* indices = stream.data() + data + 21 * 36 + 4 + face * 6;
    for (size_t corner = 0; corner < 3; ++corner) {
      const uint16_t index = (uint16_t(indices[corner * 2]) << 8) |
                             indices[corner * 2 + 1];
      if (index != face * 3 + corner)
        return Fail(error, "invalid autosave spinner wedge face");
    }
  }
  return true;
}

struct RasterVertex {
  float x, y, u, v;
};

std::array<uint8_t, 4> SamplePlate(const std::vector<uint8_t>& sheet,
                                   float u, float v) {
  const float tx = u * kSheetWidth - 0.5f;
  const float ty = v * kSheetHeight - 0.5f;
  const int x = int(std::floor(tx));
  const int y = int(std::floor(ty));
  const float fx = tx - x;
  const float fy = ty - y;
  auto texel = [&](int sx, int sy, size_t channel) {
    const size_t wrapped_x = (sx + int(kSheetWidth)) % kSheetWidth;
    const size_t wrapped_y = (sy + int(kSheetHeight)) % kSheetHeight;
    return float(sheet[(wrapped_y * kSheetWidth + wrapped_x) * 4 + channel]);
  };
  std::array<uint8_t, 4> rgba;
  for (size_t channel = 0; channel < 4; ++channel) {
    const float upper = texel(x, y, channel) * (1.0f - fx) +
                        texel(x + 1, y, channel) * fx;
    const float lower = texel(x, y + 1, channel) * (1.0f - fx) +
                        texel(x + 1, y + 1, channel) * fx;
    rgba[channel] = uint8_t(std::clamp(
        std::lround(upper * (1.0f - fy) + lower * fy), 0L, 255L));
  }
  return rgba;
}

void RasterTriangle(const std::array<RasterVertex, 3>& triangle,
                    const std::vector<uint8_t>& sheet,
                    const std::vector<uint8_t>& bloom, float phase,
                    std::vector<uint8_t>* pixels) {
  const auto& a = triangle[0];
  const auto& b = triangle[1];
  const auto& c = triangle[2];
  const float denominator = (b.y - c.y) * (a.x - c.x) +
                            (c.x - b.x) * (a.y - c.y);
  if (std::abs(denominator) < 0.00001f) return;
  const int left = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
  const int right = std::min(int(kFrameSize),
                             int(std::ceil(std::max({a.x, b.x, c.x}))));
  const int top = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
  const int bottom = std::min(int(kFrameSize),
                              int(std::ceil(std::max({a.y, b.y, c.y}))));
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const float sample_x = x + 0.5f;
      const float sample_y = y + 0.5f;
      const float wa = ((b.y - c.y) * (sample_x - c.x) +
                        (c.x - b.x) * (sample_y - c.y)) / denominator;
      const float wb = ((c.y - a.y) * (sample_x - c.x) +
                        (a.x - c.x) * (sample_y - c.y)) / denominator;
      const float wc = 1.0f - wa - wb;
      if (wa < -0.00001f || wb < -0.00001f || wc < -0.00001f) continue;

      // spinner_base.mat scales U to one quarter of the four-emblem plate.
      // spinner.mnm then translates U through four wraps over 240 frames.
      float u = (wa * a.u + wb * b.u + wc * c.u) * 0.25f + phase;
      float v = wa * a.v + wb * b.v + wc * c.v;
      u -= std::floor(u);
      v -= std::floor(v);
      const auto source = SamplePlate(sheet, u, v);
      const auto glow = SamplePlate(bloom, u, v);
      auto* target = pixels->data() + (y * kFrameSize + x) * 4;
      for (size_t channel = 0; channel < 3; ++channel)
        target[channel] = uint8_t(std::min(
            255, int(source[channel]) +
                     int(glow[channel]) * int(glow[3]) / 255));
      target[3] = source[3];
    }
  }
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
                 std::string_view texture_path,
                 std::vector<uint8_t>* rgba, std::string* error) {
  const auto path = std::search(stream.begin(), stream.end(),
                                texture_path.begin(), texture_path.end());
  if (path == stream.end()) return Fail(error, "autosave spinner plate texture is missing");
  const size_t path_at = size_t(path - stream.begin());
  const size_t path_end = path_at + texture_path.size();
  // The installed scene's RndTex record has 11 bytes between its source path
  // and the 23-byte bitmap header, then an eight-byte zero preamble.
  constexpr size_t kGap = 11;
  constexpr size_t kRecordBytes = 31;
  const size_t record = path_end + kGap;
  const size_t payload = record + kRecordBytes;
  constexpr std::array<uint8_t, 12> kShape{
      8, 0, 0, 0, 0x18, 3, 2, 0, 0, 128, 2, 0};
  if (path_at < 4 || Be32(stream.data() + path_at - 4) != texture_path.size() ||
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
  std::vector<uint8_t> bloom;
  std::array<MeshVertex, 21> wedge;
  if (!ReadScene(game_data_root, &scene, error) ||
      !DecodeMilo(scene, &stream, error) ||
      !DecodePlate(stream, kTexturePath, &sheet, error) ||
      !DecodePlate(stream, kBloomPath, &bloom, error) ||
      !ReadWedge(stream, &wedge, error)) return false;

  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kMeshRadius = 1.7777778f;
  constexpr float kImageRadius = kFrameSize * (57.0f / 128.0f);
  BootLoadingFrames ready;
  for (size_t frame = 0; frame < ready.size(); ++frame) {
    auto& pixels = ready[frame];
    pixels.assign(kFrameSize * kFrameSize * 4, 0);
    const float phase = float(frame) / float(ready.size());
    for (size_t copy = 0; copy < 8; ++copy) {
      const float angle = float(copy) * kPi / 4.0f;
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);
      for (size_t face = 0; face < 7; ++face) {
        std::array<RasterVertex, 3> triangle;
        for (size_t corner = 0; corner < 3; ++corner) {
          const auto& source = wedge[face * 3 + corner];
          const float x = source.x * cosine - source.y * sine;
          const float y = source.x * sine + source.y * cosine;
          triangle[corner] = {
              kFrameSize * 0.5f + kImageRadius * x / kMeshRadius,
              kFrameSize * 0.5f - kImageRadius * y / kMeshRadius,
              source.u, source.v};
        }
        RasterTriangle(triangle, sheet, bloom, phase, &pixels);
      }
    }
  }
  *frames = std::move(ready);
  if (error) error->clear();
  return true;
}

}  // namespace rb_blitz::ui::boot_loading_art
