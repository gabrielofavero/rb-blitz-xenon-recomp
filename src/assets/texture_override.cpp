// SPDX-License-Identifier: GPL-2.0-only
#include "assets/texture_override.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <rex/graphics/pipeline/texture/cache.h>
#include <rex/logging.h>

#include "assets/texture_codec.h"
#include "assets/input_font_atlas.h"
#include "controller_schemes/schemes.h"
#include "input/last_device.h"
#include "settings/schema.h"
#include "ui/input_font_scene.h"

namespace rb_blitz::assets {
namespace {

bool DecodeSchemePngTimed(const std::filesystem::path& path,
                          controller_schemes::Image* image, std::string* why) {
  const auto started = std::chrono::steady_clock::now();
  const bool decoded = controller_schemes::DecodePngWic(path, image, why);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  if (elapsed >= 500)
    REXLOG_WARN("controller_schemes: slow image '{}' decode_ms={} success={}",
                path.string(), elapsed, decoded);
  return decoded;
}

void LogSchemeDiscovery(const std::filesystem::path& folder, bool begin,
                        bool accepted, uint64_t elapsed_ms) {
  if (begin)
    REXLOG_INFO("controller_schemes: loading package '{}'", folder.string());
  else
    REXLOG_INFO("controller_schemes: package '{}' accepted={} elapsed_ms={}",
                folder.string(), accepted, elapsed_ms);
}

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

// Reproduced from the owner's 360 scene bytes with the SDK's tiled 2D layout,
// then checked against P1's guest trace. These hashes identify all eight pad
// slots independently, including the four not visited in the first P1 run.
constexpr std::pair<uint64_t, const char*> kPadFingerprints[] = {
    {0x1607043F166B232Dull, "xbox_0"}, {0x06FFB32889EA3A64ull, "xbox_1"},
    {0x54855E715F650967ull, "xbox_2"}, {0x66AE0BAD190B484Dull, "xbox_3"},
    {0x4747E8979C00EEFBull, "ps_0"}, {0xC6948849BBEEB180ull, "ps_1"},
    {0xF2158119E803554Eull, "ps_2"}, {0x86AAF3CCA6CE2365ull, "ps_3"},
};
constexpr uint64_t kButtonFingerprint = 0x3F044908A62A89ADull;
constexpr uint64_t kExtraIconsFingerprint = 0x1163F9F7F2FFF53Dull;

class SchemeOverride final : public rex::graphics::TextureOverrideProvider {
 public:
  explicit SchemeOverride(controller_schemes::Registry registry)
      : registry_(std::move(registry)) {}
  const controller_schemes::Registry* Registry() const { return &registry_; }
  bool Select(const std::string& id) {
    if (id != "retail" && !registry_.Find(id)) return false;
    std::lock_guard lock(pending_mutex_);
    pending_ = id;
    return true;
  }
  void PublishIcons(std::string signature, std::vector<input::icons::Icon> icons) {
    std::lock_guard lock(pending_mutex_);
    if (signature == last_icon_signature_) return;
    last_icon_signature_ = signature;
    pending_icons_ = std::move(icons);
    has_pending_icons_ = true;
  }
  void PublishHints(std::string signature, std::vector<input::icons::Icon> icons) {
    std::lock_guard lock(pending_mutex_);
    if (signature == last_hint_signature_) return;
    last_hint_signature_ = std::move(signature);
    pending_hints_ = std::move(icons);
    has_pending_hints_ = true;
  }
  void Poll() override {
    std::string next;
    std::vector<input::icons::Icon> icons;
    std::vector<input::icons::Icon> hints;
    bool icons_changed = false;
    {
      std::lock_guard lock(pending_mutex_);
      next = std::move(pending_);
      pending_.clear();
      if (has_pending_icons_) {
        icons = std::move(pending_icons_);
        has_pending_icons_ = false;
        icons_changed = true;
      }
      if (has_pending_hints_) {
        hints = std::move(pending_hints_);
        has_pending_hints_ = false;
        icons_changed = true;
      }
    }
    const bool scheme_changed = !next.empty() && next != selected_;
    if (scheme_changed) {
      selected_ = std::move(next);
      buttons_.clear();
      icons_.clear();
      pads_.clear();
      font_levels_.clear();
      retail_font_levels_.clear();
      logged_.clear();
      const auto* scheme = registry_.Find(selected_);
      if (scheme) {
        const auto start = std::chrono::steady_clock::now();
        for (const auto& [hash, id] : kPadFingerprints) {
          if (const auto* image = scheme->Pad(id))
            pads_[hash] = controller_schemes::ComposePad(*image, 7);
        }
        if (scheme->has_icons) icons_ = controller_schemes::ComposeIcons(scheme->icons, 11);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        REXLOG_INFO("controller_schemes: selected '{}' with {} prepared pad texture(s) in {} ms",
                    selected_, pads_.size(), elapsed.count());
      }
    }
    if (icons_changed) {
      if (!icons.empty()) row_icons_ = std::move(icons);
      if (!hints.empty()) hint_icons_ = std::move(hints);
      if (!hint_icons_.empty() || row_icons_.size() > ui::input_font::kHintFirst) {
        input::icons::Icon blank;
        blank.art = input::icons::Art::kBuiltIn;
        blank.width = blank.height = 1;
        blank.rgba = {0, 0, 0, 0};
        font_icons_.assign(ui::input_font::kIconCount, blank);
        for (size_t i = 0; i < row_icons_.size(); ++i)
          font_icons_[ui::input_font::RowGlyphIndex(static_cast<uint32_t>(i))] = row_icons_[i];
        for (size_t i = 0; i < hint_icons_.size() && i < ui::input_font::kHintCount; ++i)
          font_icons_[ui::input_font::kHintFirst + i] = hint_icons_[i];
      } else font_icons_ = row_icons_;
      font_levels_.clear();
      retail_font_levels_.clear();
    }
    if (scheme_changed || icons_changed || refresh_font_after_buttons_) {
      refresh_font_after_buttons_ = false;
      rex::graphics::InvalidateTextureOverrides();
    }
  }
  bool GetOverride(const rex::graphics::TextureOverrideRequest& request,
                   const uint8_t* guest_data,
                   std::vector<std::vector<uint8_t>>* host_levels,
                   bool* refresh_target) override {
    const uint64_t hash = ContentHash(guest_data, request.guest_base_size);
    const bool button = request.width == 512 && request.height == 512 &&
                        hash == kButtonFingerprint;
    const bool icons = request.width == 1024 && request.height == 512 &&
                       hash == kExtraIconsFingerprint;
    const char* pad_slot = nullptr;
    if (request.width == 1024 && request.height == 1024) {
      for (const auto& [fingerprint, slot] : kPadFingerprints) {
        if (hash == fingerprint) {
          pad_slot = slot;
          break;
        }
      }
    }
    const auto private_original = ui::input_font::PrivateBc3();
    const auto retail_original = ui::input_font::OriginalBc3();
    auto matches_font = [&](uint32_t size,
                            const std::shared_ptr<const std::vector<uint8_t>>& expected,
                            std::unordered_set<uint64_t>* hashes,
                            const char* name) {
      if (pad_slot || request.width != size || request.height != size || !expected ||
          request.guest_base_size != size * size) return false;
      if (hashes->contains(hash)) return true;
        std::vector<uint8_t> canonical;
        const bool decoded = controller_schemes::UntileBc3Base(
            guest_data, request.guest_base_size, request.width, request.tiled,
            uint32_t(request.endianness), &canonical);
        const bool matches = decoded && canonical.size() <= expected->size() &&
                             std::equal(canonical.begin(), canonical.end(), expected->begin());
        const uint64_t trace_key = hash ^ (uint64_t(size) << 32);
        if (font_probe_logged_.size() < 32 && font_probe_logged_.insert(trace_key).second) {
          const size_t first_diff = decoded && canonical.size() <= expected->size()
              ? size_t(std::mismatch(canonical.begin(), canonical.end(),
                                     expected->begin()).first - canonical.begin())
              : 0;
          REXLOG_INFO("input_icons: {} font candidate {:016X} guest=0x{:08X} tiled={} "
                      "endian={} decoded={} first_diff={} match={}", name,
                      hash, request.guest_base_address, request.tiled,
                      uint32_t(request.endianness), decoded, first_diff, matches);
        }
        if (matches) {
          hashes->insert(hash);
          REXLOG_INFO("input_icons: matched {} font atlas texture {:016X}", name, hash);
        }
        return matches;
    };
    const bool private_font = matches_font(ui::input_font::kAtlasSize,
        private_original, &private_font_hashes_, "private");
    const bool retail_font = matches_font(ui::input_font::kRetailAtlasSize,
        retail_original, &retail_font_hashes_, "retail");
    const bool font = private_font || retail_font;
    // The base-game texture must be a refresh candidate before any custom scheme is selected.
    // Otherwise a retail boot uploads it once and a later generation bump has nothing to evict.
    if (button || icons || pad_slot || font) *refresh_target = true;
    if (button && original_buttons_.empty() &&
        controller_schemes::UntileBc3Base(guest_data, request.guest_base_size,
            request.width, request.tiled, uint32_t(request.endianness), &original_buttons_)) {
      refresh_font_after_buttons_ = true;
    }
    if (font) {
      if (font_icons_.empty() || original_buttons_.empty()) {
        REXLOG_WARN("input_icons: font override waiting for icons={} button_sheet={}",
                    font_icons_.size(), !original_buttons_.empty());
        return false;
      }
      if (font_levels_.empty()) {
        if (!private_original || !input_font_atlas::Compose(*private_original, original_buttons_,
                                      registry_.Find(selected_), font_icons_, &font_levels_)) {
          REXLOG_WARN("input_icons: could not compose mapping font images");
          return false;
        }
        REXLOG_INFO("input_icons: composed {} image slots, hint tiles {}/{}",
                    font_icons_.size(), ui::input_font::kHintFirst + 1,
                    ui::input_font::kHintFirst + 2);
      }
      if (retail_font && retail_font_levels_.empty() &&
          (!retail_original || !input_font_atlas::ComposeRetail(
              *retail_original, font_levels_, &retail_font_levels_))) {
        REXLOG_WARN("input_icons: could not compose retail font fallback");
        return false;
      }
      const auto& chosen = retail_font ? retail_font_levels_ : font_levels_;
      if (request.mip_count > chosen.size()) return false;
      host_levels->assign(chosen.begin(), chosen.begin() + request.mip_count);
      return true;
    }
    const auto* scheme = registry_.Find(selected_);
    if (!scheme) {
      if (button && logged_.insert(hash).second)
        REXLOG_INFO("controller_schemes: tracking original button sheet for live selection");
      return false;
    }
    if (button) {
      if (scheme->buttons.empty()) return false;
      if (buttons_.empty()) {
        const auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> original;
        if (!controller_schemes::UntileBc3Base(
                guest_data, request.guest_base_size, request.width,
                request.tiled, uint32_t(request.endianness), &original) ||
            !controller_schemes::ComposeButtons({original}, *scheme, &buttons_)) {
          REXLOG_WARN("controller_schemes: cannot compose original button sheet");
          return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        REXLOG_INFO("controller_schemes: composed button sheet in {} us", elapsed.count());
      }
      if (request.mip_count > buttons_.size()) return false;
      host_levels->assign(buttons_.begin(), buttons_.begin() + request.mip_count);
      if (logged_.insert(hash).second)
        REXLOG_INFO("controller_schemes: overrode button sheet ({} mip level(s))",
                    request.mip_count);
      return true;
    }
    if (pad_slot) {
      auto it = pads_.find(hash);
      if (it == pads_.end() || request.mip_count > it->second.size()) return false;
      host_levels->assign(it->second.begin(), it->second.begin() + request.mip_count);
      if (logged_.insert(hash).second)
        REXLOG_INFO("controller_schemes: overrode {} pad ({} mip level(s))", pad_slot,
                    request.mip_count);
      return true;
    }
    if (icons) {
      if (request.mip_count > icons_.size()) return false;
      host_levels->assign(icons_.begin(), icons_.begin() + request.mip_count);
      if (logged_.insert(hash).second)
        REXLOG_INFO("controller_schemes: overrode extra icons ({} mip level(s))",
                    request.mip_count);
      return true;
    }
    return false;
  }
 private:
  controller_schemes::Registry registry_;
  std::string selected_;
  std::vector<std::vector<uint8_t>> buttons_;
  std::vector<std::vector<uint8_t>> icons_;
  std::vector<std::vector<uint8_t>> font_levels_;
  std::vector<std::vector<uint8_t>> retail_font_levels_;
  std::vector<input::icons::Icon> font_icons_;
  std::vector<input::icons::Icon> row_icons_;
  std::vector<input::icons::Icon> hint_icons_;
  std::vector<input::icons::Icon> pending_icons_;
  std::vector<input::icons::Icon> pending_hints_;
  std::vector<uint8_t> original_buttons_;
  std::string last_icon_signature_;
  std::string last_hint_signature_;
  std::unordered_set<uint64_t> private_font_hashes_;
  std::unordered_set<uint64_t> retail_font_hashes_;
  std::unordered_set<uint64_t> font_probe_logged_;
  bool has_pending_icons_ = false;
  bool has_pending_hints_ = false;
  bool refresh_font_after_buttons_ = false;
  std::map<uint64_t, std::vector<std::vector<uint8_t>>> pads_;
  std::unordered_set<uint64_t> logged_;
  std::mutex pending_mutex_;
  std::string pending_;
};

SchemeOverride* g_scheme_provider = nullptr;

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

void InstallControllerSchemes(const std::filesystem::path& install_root,
                              const std::filesystem::path& user_root) {
  const auto started = std::chrono::steady_clock::now();
  REXLOG_INFO("controller_schemes: discovery begin install='{}' user='{}'",
              install_root.string(), user_root.string());
  auto registry = controller_schemes::Discover(install_root, user_root,
                                                DecodeSchemePngTimed, LogSchemeDiscovery);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  REXLOG_INFO("controller_schemes: discovery end elapsed_ms={}", elapsed);
  REXLOG_INFO("controller_schemes: {}", registry.Summary());
  for (const auto& rejected : registry.rejected)
    REXLOG_WARN("controller_schemes: rejected '{}': {}", rejected.folder.string(), rejected.reason);
  for (const auto& shadowed : registry.shadowed)
    REXLOG_INFO("controller_schemes: {}", shadowed);
  for (const auto& [id, scheme] : registry.schemes)
    for (const auto& extra : scheme.extra_files)
      REXLOG_WARN("controller_schemes: '{}' ignores extra file '{}'", id,
                  extra.generic_string());
  std::vector<settings::Choice> choices{{"retail", "Xbox 360"}};
  for (const auto& [id, scheme] : registry.schemes)
    choices.push_back({id, scheme.name});
  settings::schema::SetControllerSchemes(std::move(choices));
  static SchemeOverride provider(std::move(registry));
  g_scheme_provider = &provider;
  rex::graphics::SetTextureOverrideProvider(&provider);
  REXLOG_INFO("button_schemes: active retail (device controller)");
}

bool SelectControllerScheme(const std::string& id) {
  if (!g_scheme_provider || !g_scheme_provider->Select(id)) return false;
  const auto before = input::last_device::Current();
  input::last_device::SelectScheme(id);
  if (before.scheme != id) {
    REXLOG_INFO("button_schemes: active {} (device {})", id,
                before.family == input::last_device::Family::kController
                    ? "controller" : "keyboard");
  }
  return true;
}

void PublishInputIcons(std::string signature, std::vector<input::icons::Icon> icons) {
  if (g_scheme_provider) g_scheme_provider->PublishIcons(std::move(signature), std::move(icons));
}
void PublishHintIcons(std::string signature, std::vector<input::icons::Icon> icons) {
  if (g_scheme_provider) g_scheme_provider->PublishHints(std::move(signature), std::move(icons));
}
bool InputIconsAvailable() {
  return g_scheme_provider != nullptr && ui::input_font::PrivateBc3() != nullptr;
}
const controller_schemes::Registry* ControllerSchemeRegistry() {
  return g_scheme_provider ? g_scheme_provider->Registry() : nullptr;
}

}  // namespace rb_blitz::assets
