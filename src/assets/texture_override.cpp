// SPDX-License-Identifier: GPL-2.0-only
#include "assets/texture_override.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

using Levels = std::vector<std::vector<uint8_t>>;
using CalibrationLevels = std::array<Levels, 2>;

CalibrationLevels ComposeCalibrationIcon(const controller_schemes::Registry& registry,
                                         const input::icons::Icon& icon) {
  CalibrationLevels levels;
  const auto originals = ui::input_font::OriginalCalibrationButtons();
  const auto& source = icon.source;
  if (icon.scheme == "playstation-3" && source.kind == profile::remap::SourceKind::kPad &&
      source.name == "a" && originals) {
    levels[0] = (*originals)[2];
    levels[1] = (*originals)[3];
  } else if (icon.art == input::icons::Art::kUnbound) {
    controller_schemes::Image blank{256, 256, std::vector<uint8_t>(256u * 256u * 4u)};
    levels[0] = controller_schemes::ComposeCalibration(blank);
    levels[1] = levels[0];
  } else {
    const controller_schemes::Scheme* scheme = nullptr;
    if (source.kind == profile::remap::SourceKind::kKey ||
        source.kind == profile::remap::SourceKind::kMouse) {
      if (registry.has_keyboard_mouse) scheme = &registry.keyboard_mouse;
    } else scheme = registry.Find(icon.scheme);
    if (scheme) {
      const char* prefix = source.kind == profile::remap::SourceKind::kPad ? "pad:" :
                           source.kind == profile::remap::SourceKind::kSdl ? "sdl:" :
                           source.kind == profile::remap::SourceKind::kMouse ? "mouse:" : "key:";
      if (const auto* entry = scheme->InputFor(std::string(prefix) + source.name);
          entry && scheme->calibration_pairs.contains(entry->id)) {
        controller_schemes::Image released, pressed;
        std::string why;
        if (controller_schemes::LoadCalibrationImage(*scheme, *entry, false,
                DecodeSchemePngTimed, &released, &why) &&
            controller_schemes::LoadCalibrationImage(*scheme, *entry, true,
                DecodeSchemePngTimed, &pressed, &why)) {
          levels[0] = controller_schemes::ComposeCalibration(released);
          levels[1] = controller_schemes::ComposeCalibration(pressed);
        } else REXLOG_WARN("calibration: cannot load {} art: {}", entry->id, why);
      }
    }
  }
  return levels;
}

struct FontVariant {
  Levels retail;
  Levels private_font;
  CalibrationLevels calibration;
};
struct FontJob {
  uint64_t revision = 0;
  std::string scheme;
  std::vector<input::icons::Icon> rows;
  std::array<std::vector<input::icons::Icon>, 3> hints;
  std::array<input::icons::Icon, 3> calibration;
  std::shared_ptr<const std::vector<uint8_t>> original_font;
  std::shared_ptr<const std::vector<uint8_t>> original_buttons;
};
struct FontResult {
  uint64_t revision = 0;
  bool valid = true;
  std::array<FontVariant, 3> variants;
};

class SchemeOverride final : public rex::graphics::TextureOverrideProvider {
 public:
  explicit SchemeOverride(controller_schemes::Registry registry)
      : registry_(std::move(registry)) {
    worker_ = std::jthread([this](std::stop_token stop) { PrepareFonts(stop); });
  }
  ~SchemeOverride() override {
    worker_.request_stop();
    worker_cv_.notify_all();
  }
  const controller_schemes::Registry* Registry() const { return &registry_; }
  bool Select(const std::string& id, uint64_t* ticket) {
    if (id != "retail" && !registry_.Find(id)) return false;
    std::lock_guard lock(pending_mutex_);
    pending_ = id;
    pending_ticket_ = completion_.Request();
    if (ticket) *ticket = pending_ticket_;
    return true;
  }
  std::optional<ControllerSchemeProgress> LatestApplied() {
    std::lock_guard lock(pending_mutex_);
    return completion_.LatestApplied();
  }
  void PublishIcons(std::string signature, std::vector<input::icons::Icon> icons) {
    std::lock_guard lock(pending_mutex_);
    if (signature == last_icon_signature_) return;
    last_icon_signature_ = signature;
    pending_icons_ = std::move(icons);
    has_pending_icons_ = true;
  }
  void PublishHints(std::string signature,
                    std::array<std::vector<input::icons::Icon>, 3> variants,
                    std::array<input::icons::Icon, 3> calibration,
                    input::icons::Family active_family) {
    std::lock_guard lock(pending_mutex_);
    if (signature == last_hint_signature_ && active_family == last_active_family_) return;
    const bool content_changed = signature != last_hint_signature_;
    last_hint_signature_ = std::move(signature);
    last_active_family_ = active_family;
    if (content_changed) {
      pending_hints_ = std::move(variants);
      pending_calibration_ = std::move(calibration);
    }
    pending_hint_content_changed_ |= content_changed;
    pending_active_family_ = active_family;
    has_pending_hints_ = true;
  }
  void PrepareFonts(std::stop_token stop) {
    while (!stop.stop_requested()) {
      FontJob job;
      {
        std::unique_lock lock(worker_mutex_);
        worker_cv_.wait(lock, [&] { return stop.stop_requested() || pending_job_.has_value(); });
        if (stop.stop_requested()) return;
        job = std::move(*pending_job_);
        pending_job_.reset();
      }
      FontResult result;
      result.revision = job.revision;
      const auto* scheme = registry_.Find(job.scheme);
      for (size_t family = 0; family < result.variants.size(); ++family) {
        input::icons::Icon blank;
        blank.art = input::icons::Art::kBuiltIn;
        blank.width = blank.height = 1;
        blank.rgba = {0, 0, 0, 0};
        std::vector<input::icons::Icon> icons(ui::input_font::kIconCount, blank);
        for (size_t i = 0; i < job.rows.size(); ++i)
          icons[ui::input_font::RowGlyphIndex(static_cast<uint32_t>(i))] = job.rows[i];
        for (size_t i = 0; i < job.hints[family].size() && i < ui::input_font::kHintCount; ++i)
          icons[ui::input_font::kHintFirst + i] = job.hints[family][i];
        auto& variant = result.variants[family];
        Levels composed;
        if (!input_font_atlas::Compose(*job.original_font, *job.original_buttons,
                                       scheme, icons, &composed) ||
            !input_font_atlas::ComposeRetail(*job.original_font, composed, &variant.retail) ||
            !input_font_atlas::ComposePrivate(*job.original_buttons, scheme, icons,
                                               &variant.private_font)) {
          result.valid = false;
          REXLOG_WARN("input_icons: background font preparation failed for family {}", family);
        }
        variant.calibration = ComposeCalibrationIcon(registry_, job.calibration[family]);
      }
      REXLOG_INFO("input_icons: prepared controller, keyboard and mouse font variants revision={}",
                  result.revision);
      {
        std::lock_guard lock(worker_mutex_);
        completed_job_ = std::move(result);
      }
    }
  }
  void Poll() override {
    std::string next;
    uint64_t next_ticket = 0;
    std::vector<input::icons::Icon> icons;
    std::array<std::vector<input::icons::Icon>, 3> hints;
    std::array<input::icons::Icon, 3> calibration;
    bool rows_changed = false;
    bool hints_changed = false;
    bool family_changed = false;
    {
      std::lock_guard lock(pending_mutex_);
      next = std::move(pending_);
      pending_.clear();
      next_ticket = std::exchange(pending_ticket_, 0);
      if (has_pending_icons_) {
        icons = std::move(pending_icons_);
        has_pending_icons_ = false;
        rows_changed = true;
      }
      if (has_pending_hints_) {
        if (pending_hint_content_changed_) {
          hints = std::move(pending_hints_);
          calibration = std::move(pending_calibration_);
          hints_changed = true;
          pending_hint_content_changed_ = false;
        }
        family_changed = active_family_ != pending_active_family_;
        active_family_ = pending_active_family_;
        has_pending_hints_ = false;
      }
    }
    const bool scheme_changed = !next.empty() && next != selected_;
    if (scheme_changed) {
      selected_ = std::move(next);
      buttons_.clear();
      for (auto& levels : header_buttons_) levels.clear();
      icons_.clear();
      pads_.clear();
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
    bool retail_ps_pads_ready = false;
    if (const auto* scheme = registry_.Find(selected_);
        scheme && scheme->use_retail_ps_pads && pads_.empty()) {
      // The Xbox preset page requests xbox_0..3. This layout reuses the PS
      // textures in the installed 360 scene without exporting pad PNGs.
      if (const auto original = ui::input_font::OriginalPsPadsBc3()) {
        for (size_t index = 0; index < original->size(); ++index)
          pads_[kPadFingerprints[index].first] = (*original)[index];
        retail_ps_pads_ready = true;
        REXLOG_INFO("controller_schemes: selected '{}' maps xbox_0..3 to retail ps_0..3",
                    selected_);
      }
    }
    if (rows_changed || hints_changed) {
      if (!icons.empty()) row_icons_ = std::move(icons);
      if (hints_changed) {
        hint_variants_ = std::move(hints);
        calibration_icons_ = std::move(calibration);
        for (auto& levels : header_buttons_) levels.clear();
      }
    }
    if (scheme_changed || rows_changed || hints_changed) ++font_revision_;
    if (font_revision_ != queued_font_revision_ && !hint_variants_[0].empty()) {
      const auto original_font = ui::input_font::OriginalBc3();
      const auto original_buttons = ui::input_font::OriginalButtonsBc3();
      if (original_font && original_buttons) {
        FontJob job;
        job.revision = font_revision_;
        job.scheme = selected_;
        job.rows = row_icons_;
        job.hints = hint_variants_;
        job.calibration = calibration_icons_;
        job.original_font = original_font;
        job.original_buttons = original_buttons;
        {
          std::lock_guard lock(worker_mutex_);
          pending_job_ = std::move(job);
        }
        queued_font_revision_ = font_revision_;
        worker_cv_.notify_one();
      }
    }
    bool fonts_ready = false;
    {
      std::lock_guard lock(worker_mutex_);
      if (completed_job_ && completed_job_->revision == font_revision_) {
        if (completed_job_->valid) installed_font_revision_ = font_revision_;
        font_variants_ = std::move(completed_job_->variants);
        completed_job_.reset();
        fonts_ready = true;
      } else if (completed_job_) completed_job_.reset();
    }
    if (scheme_changed || family_changed || hints_changed || fonts_ready || retail_ps_pads_ready ||
        refresh_font_after_buttons_) {
      refresh_font_after_buttons_ = false;
      rex::graphics::InvalidateTextureOverrides();
    }
    {
      std::lock_guard lock(pending_mutex_);
      if (next_ticket) {
        const uint64_t required_font_revision =
            queued_font_revision_ == font_revision_ &&
            installed_font_revision_ < font_revision_ ? font_revision_ : 0;
        completion_.Selected(next_ticket, selected_, required_font_revision);
      }
      completion_.Complete(installed_font_revision_);
    }
  }
  bool GetOverride(const rex::graphics::TextureOverrideRequest& request,
                   const uint8_t* guest_data,
                   std::vector<std::vector<uint8_t>>* host_levels,
                   bool* refresh_target) override {
    const uint64_t hash = ContentHash(guest_data, request.guest_base_size);
    int calibration_slot = -1;
    if (request.width == 256 && request.height == 256 &&
        request.guest_base_size == 256u * 256u) {
      if (const auto original = ui::input_font::OriginalCalibrationButtons()) {
        const auto known = calibration_hashes_.find(hash);
        if (known != calibration_hashes_.end()) calibration_slot = known->second;
        else {
          std::vector<uint8_t> canonical;
          if (controller_schemes::UntileBc3Base(guest_data, request.guest_base_size,
                  256, request.tiled, uint32_t(request.endianness), &canonical)) {
            for (size_t i = 0; i < original->size(); ++i)
              if (canonical == (*original)[i][0]) { calibration_slot = int(i); break; }
          }
          calibration_hashes_[hash] = calibration_slot;
        }
      }
    }
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
    const auto retail_original = ui::input_font::OriginalBc3();
    const auto private_original = ui::input_font::PrivateBc3();
    if (original_buttons_.empty()) {
      if (const auto source_buttons = ui::input_font::OriginalButtonsBc3())
        original_buttons_ = *source_buttons;
    }
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
    const bool font = matches_font(ui::input_font::kRetailAtlasSize,
        retail_original, &font_hashes_, "retail");
    const bool private_font = matches_font(ui::input_font::kAtlasSize,
        private_original, &private_font_hashes_, "private");
    // The base-game texture must be a refresh candidate before any custom scheme is selected.
    // Otherwise a retail boot uploads it once and a later generation bump has nothing to evict.
    if (button || icons || pad_slot || font || private_font || calibration_slot >= 0)
      *refresh_target = true;
    const auto& variant = font_variants_[static_cast<size_t>(active_family_)];
    if (calibration_slot >= 0 && !variant.calibration[calibration_slot % 2].empty()) {
      const auto& levels = variant.calibration[calibration_slot % 2];
      if (request.mip_count > levels.size()) return false;
      host_levels->assign(levels.begin(), levels.begin() + request.mip_count);
      return true;
    }
    if (button && original_buttons_.empty() &&
        controller_schemes::UntileBc3Base(guest_data, request.guest_base_size,
            request.width, request.tiled, uint32_t(request.endianness), &original_buttons_)) {
      refresh_font_after_buttons_ = true;
    }
    if (private_font) {
      if (request.mip_count > variant.private_font.size()) return false;
      host_levels->assign(variant.private_font.begin(),
                          variant.private_font.begin() + request.mip_count);
      return true;
    }
    if (font) {
      const auto& chosen = variant.retail;
      if (request.mip_count > chosen.size()) return false;
      host_levels->assign(chosen.begin(), chosen.begin() + request.mip_count);
      return true;
    }
    const auto* scheme = registry_.Find(selected_);
    if (button) {
      if (buttons_.empty()) {
        const auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> original;
        if (!controller_schemes::UntileBc3Base(
                guest_data, request.guest_base_size, request.width,
                request.tiled, uint32_t(request.endianness), &original)) {
          REXLOG_WARN("controller_schemes: cannot compose original button sheet");
          return false;
        }
        if (scheme && !scheme->buttons.empty()) {
          if (!controller_schemes::ComposeButtons({original}, *scheme, &buttons_)) {
            REXLOG_WARN("controller_schemes: cannot compose selected button sheet");
            return false;
          }
        } else buttons_ = {std::move(original)};
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        REXLOG_INFO("controller_schemes: composed button sheet in {} us", elapsed.count());
      }
      if (request.mip_count > buttons_.size()) return false;
      if (active_family_ == input::icons::Family::kController &&
          (!scheme || scheme->buttons.empty())) return false;
      const Levels* chosen = &buttons_;
      if (active_family_ != input::icons::Family::kController) {
        const size_t family = static_cast<size_t>(active_family_);
        auto& header = header_buttons_[family];
        if (header.empty()) {
          controller_schemes::Scheme replacement;
          const auto& hints = hint_variants_[family];
          for (const auto [index, slot] :
               {std::pair<size_t, const char*>{kHeaderPreviousIcon, "left_trigger"},
                {kHeaderNextIcon, "right_trigger"}}) {
            if (index >= hints.size()) continue;
            const auto& icon = hints[index];
            if (icon.source.kind != profile::remap::SourceKind::kKey &&
                icon.source.kind != profile::remap::SourceKind::kMouse &&
                icon.art != input::icons::Art::kUnbound) continue;
            if (!icon.width || !icon.height ||
                icon.rgba.size() != size_t(icon.width) * icon.height * 4) continue;
            replacement.buttons.emplace(slot, controller_schemes::Image{
                icon.width, icon.height, icon.rgba});
          }
          if (!replacement.buttons.empty() &&
              !controller_schemes::ComposeButtons(buttons_, replacement, &header))
            REXLOG_WARN("controller_schemes: cannot compose header key hints");
        }
        if (!header.empty()) chosen = &header;
      }
      host_levels->assign(chosen->begin(), chosen->begin() + request.mip_count);
      if (logged_.insert(hash).second)
        REXLOG_INFO("controller_schemes: overrode button sheet ({} mip level(s))",
                    request.mip_count);
      return true;
    }
    if (!scheme) return false;
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
  std::array<Levels, 3> header_buttons_;
  std::vector<std::vector<uint8_t>> icons_;
  std::vector<input::icons::Icon> row_icons_;
  std::array<std::vector<input::icons::Icon>, 3> hint_variants_;
  std::array<input::icons::Icon, 3> calibration_icons_;
  std::array<FontVariant, 3> font_variants_;
  std::vector<input::icons::Icon> pending_icons_;
  std::array<std::vector<input::icons::Icon>, 3> pending_hints_;
  std::array<input::icons::Icon, 3> pending_calibration_;
  std::unordered_map<uint64_t, int> calibration_hashes_;
  std::vector<uint8_t> original_buttons_;
  std::string last_icon_signature_;
  std::string last_hint_signature_;
  input::icons::Family last_active_family_ = input::icons::Family::kController;
  input::icons::Family pending_active_family_ = input::icons::Family::kController;
  input::icons::Family active_family_ = input::icons::Family::kController;
  std::unordered_set<uint64_t> font_hashes_;
  std::unordered_set<uint64_t> private_font_hashes_;
  std::unordered_set<uint64_t> font_probe_logged_;
  bool has_pending_icons_ = false;
  bool has_pending_hints_ = false;
  bool pending_hint_content_changed_ = false;
  bool refresh_font_after_buttons_ = false;
  uint64_t font_revision_ = 0;
  uint64_t queued_font_revision_ = 0;
  uint64_t installed_font_revision_ = 0;
  std::map<uint64_t, std::vector<std::vector<uint8_t>>> pads_;
  std::unordered_set<uint64_t> logged_;
  std::mutex pending_mutex_;
  std::string pending_;
  uint64_t pending_ticket_ = 0;
  ControllerSchemeCompletion completion_;
  std::mutex worker_mutex_;
  std::condition_variable worker_cv_;
  std::optional<FontJob> pending_job_;
  std::optional<FontResult> completed_job_;
  std::jthread worker_;
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
                              const std::filesystem::path& user_root,
                              const std::function<void()>& pump_startup_window) {
  const auto started = std::chrono::steady_clock::now();
  REXLOG_INFO("controller_schemes: discovery begin install='{}' user='{}'",
              install_root.string(), user_root.string());
  auto observer = [&](const std::filesystem::path& folder, bool begin,
                      bool accepted, uint64_t elapsed_ms) {
    LogSchemeDiscovery(folder, begin, accepted, elapsed_ms);
    if (pump_startup_window) pump_startup_window();
  };
  auto registry = controller_schemes::Discover(install_root, user_root,
                                                DecodeSchemePngTimed, observer);
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

bool SelectControllerScheme(const std::string& id, uint64_t* ticket) {
  if (!g_scheme_provider || !g_scheme_provider->Select(id, ticket)) return false;
  const auto before = input::last_device::Current();
  input::last_device::SelectScheme(id);
  if (before.scheme != id) {
    REXLOG_INFO("button_schemes: active {} (device {})", id,
                before.family == input::last_device::Family::kController ? "controller" :
                before.family == input::last_device::Family::kMouse ? "mouse" : "keyboard");
  }
  return true;
}

std::optional<ControllerSchemeProgress> LatestAppliedControllerScheme() {
  return g_scheme_provider ? g_scheme_provider->LatestApplied() : std::nullopt;
}

void PublishInputIcons(std::string signature, std::vector<input::icons::Icon> icons) {
  if (g_scheme_provider) g_scheme_provider->PublishIcons(std::move(signature), std::move(icons));
}
void PublishHintIcons(std::string signature,
                      std::array<std::vector<input::icons::Icon>, 3> variants,
                      std::array<input::icons::Icon, 3> calibration,
                      input::icons::Family active_family) {
  if (g_scheme_provider) g_scheme_provider->PublishHints(std::move(signature),
      std::move(variants), std::move(calibration), active_family);
}
bool InputIconsAvailable() {
  return g_scheme_provider != nullptr && ui::input_font::PrivateBc3() != nullptr;
}
const controller_schemes::Registry* ControllerSchemeRegistry() {
  return g_scheme_provider ? g_scheme_provider->Registry() : nullptr;
}

}  // namespace rb_blitz::assets
