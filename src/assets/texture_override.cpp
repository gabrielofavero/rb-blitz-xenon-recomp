// SPDX-License-Identifier: GPL-2.0-only
#include "assets/texture_override.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <rex/graphics/pipeline/texture/cache.h>
#include <rex/logging.h>

#include "assets/texture_codec.h"

namespace rb_blitz::assets {
namespace {

std::optional<uint64_t> HashFromEnv(const char* name) {
  const char* value = std::getenv(name);
  if (!value || !*value) return std::nullopt;
  try {
    std::size_t used = 0;
    const uint64_t result = std::stoull(value, &used, 16);
    if (used == std::string(value).size()) return result;
  } catch (...) {}
  REXLOG_WARN("texture_override: invalid hex hash in {}", name);
  return std::nullopt;
}

uint64_t ContentHash(const uint8_t* bytes, std::size_t size) {
  uint64_t hash = 14695981039346656037ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash = (hash ^ bytes[i]) * 1099511628211ull;
  }
  return hash;
}

std::vector<uint8_t> MakeMarkerLevel(uint32_t size, bool pad) {
  std::vector<uint8_t> rgba(std::size_t(size) * size * 4);
  if (pad) {
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i] = 255;
      rgba[i + 1] = 0;
      rgba[i + 2] = 220;
      rgba[i + 3] = 255;
    }
  } else {
    // A conspicuous, original 6x6 pattern occupying each glyph cell. The
    // transparent background keeps the guest's normal glyph tinting visible.
    for (uint32_t y = 0; y < size; ++y) {
      for (uint32_t x = 0; x < size; ++x) {
        const uint32_t cell_x = x * 6 / size;
        const uint32_t cell_y = y * 6 / size;
        const uint32_t local_x = (x * 6) % size;
        const uint32_t local_y = (y * 6) % size;
        const bool stroke = local_x > size / 12 && local_x < size * 11 / 12 &&
            ((local_y > size / 12 && local_y < size / 6) ||
             (local_y > size * 2 / 5 && local_y < size / 2) ||
             (local_y > size * 5 / 6 && local_y < size * 11 / 12));
        const std::size_t i = (std::size_t(y) * size + x) * 4;
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
        rgba[i + 3] = stroke && ((cell_x + cell_y) % 2 == 0 || local_y < size / 6)
                          ? 255 : 0;
      }
    }
  }
  std::vector<uint8_t> blocks;
  EncodeDxt5(rgba.data(), size, size, &blocks);
  return blocks;
}

std::vector<std::vector<uint8_t>> MakeMarker(uint32_t size, bool pad) {
  std::vector<std::vector<uint8_t>> levels;
  for (; size; size >>= 1) levels.push_back(MakeMarkerLevel(size, pad));
  return levels;
}

class P1TextureOverride final : public rex::graphics::TextureOverrideProvider {
 public:
  P1TextureOverride(std::optional<uint64_t> button_hash,
                    std::optional<uint64_t> pad_hash, std::filesystem::path switch_file)
      : button_hash_(button_hash), pad_hash_(pad_hash),
        switch_file_(std::move(switch_file)),
        button_blocks_(MakeMarker(512, false)), pad_blocks_(MakeMarker(1024, true)) {}

  void Poll() override {
    if (switch_file_.empty()) return;
    std::error_code error;
    const bool enabled = !std::filesystem::exists(switch_file_, error);
    if (error || enabled == markers_enabled_) return;
    markers_enabled_ = enabled;
    REXLOG_INFO("texture_override: marker switch {}", enabled ? "on" : "off");
    rex::graphics::InvalidateTextureOverrides();
  }

  bool GetOverride(const rex::graphics::TextureOverrideRequest& request,
                   const uint8_t* guest_data,
                   std::vector<std::vector<uint8_t>>* host_levels,
                   bool* refresh_target) override {
    const uint64_t hash = ContentHash(guest_data, request.guest_base_size);
    const uint64_t trace_key = hash ^ (uint64_t(request.width) << 32);
    if (seen_.insert(trace_key).second) {
      REXLOG_INFO("texture_override: candidate #{} {}x{} mips={} hash={:016X} guest=0x{:08X} bytes={} tiled={} endian={}",
                  seen_.size(), request.width, request.height, request.mip_count, hash,
                  request.guest_base_address, request.guest_base_size,
                  request.tiled, uint32_t(request.endianness));
    }
    const std::vector<std::vector<uint8_t>>* marker = nullptr;
    if (request.width == 512 && request.height == 512 &&
        button_hash_ && hash == *button_hash_) marker = &button_blocks_;
    if (request.width == 1024 && request.height == 1024 &&
        pad_hash_ && hash == *pad_hash_) marker = &pad_blocks_;
    *refresh_target = marker != nullptr;
    if (!marker || !markers_enabled_ || request.mip_count > marker->size()) return false;
    host_levels->assign(marker->begin(), marker->begin() + request.mip_count);
    REXLOG_INFO("texture_override: replaced {}x{} mips={} hash={:016X}",
                request.width, request.height, request.mip_count, hash);
    return true;
  }

 private:
  std::optional<uint64_t> button_hash_;
  std::optional<uint64_t> pad_hash_;
  std::filesystem::path switch_file_;
  std::vector<std::vector<uint8_t>> button_blocks_;
  std::vector<std::vector<uint8_t>> pad_blocks_;
  std::unordered_set<uint64_t> seen_;
  bool markers_enabled_ = true;
};

}  // namespace

void InstallP1TextureOverride() {
  const char* trace = std::getenv("RB_BLITZ_P1_TRACE");
  const auto button = HashFromEnv("RB_BLITZ_P1_BUTTON_HASH");
  const auto pad = HashFromEnv("RB_BLITZ_P1_PAD_HASH");
  if ((!trace || std::string(trace) != "1") && !button && !pad) return;
  const char* switch_path = std::getenv("RB_BLITZ_P1_SWITCH_FILE");
  static P1TextureOverride provider(button, pad,
                                    switch_path ? std::filesystem::path(switch_path)
                                                : std::filesystem::path());
  rex::graphics::SetTextureOverrideProvider(&provider);
  REXLOG_INFO("texture_override: P1 trace active (button={}, pad={}, switch={})",
              button.has_value(), pad.has_value(), switch_path ? switch_path : "none");
}

}  // namespace rb_blitz::assets
