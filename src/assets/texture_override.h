// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "input/input_icons.h"

namespace rb_blitz::assets {

struct ControllerSchemeProgress {
  uint64_t ticket = 0;
  std::string scheme;
};

// Monotonic request identity across GPU polls. A newer request supersedes an
// older font job even when its scheme token is the same (A/B/A). The provider
// calls Complete only after texture invalidation and any required font result.
class ControllerSchemeCompletion {
 public:
  uint64_t Request() {
    waiting_.reset();
    return ++last_request_;
  }
  void Selected(uint64_t ticket, std::string scheme, uint64_t font_revision) {
    if (ticket != last_request_) return;
    waiting_ = ControllerSchemeProgress{ticket, std::move(scheme)};
    required_font_revision_ = font_revision;
  }
  void Complete(uint64_t installed_font_revision) {
    if (!waiting_ || (required_font_revision_ != 0 &&
                      installed_font_revision < required_font_revision_)) return;
    applied_ = std::move(waiting_);
    waiting_.reset();
  }
  std::optional<ControllerSchemeProgress> LatestApplied() const { return applied_; }

 private:
  uint64_t last_request_ = 0;
  uint64_t required_font_revision_ = 0;
  std::optional<ControllerSchemeProgress> waiting_;
  std::optional<ControllerSchemeProgress> applied_;
};

// Unused action-hint slots carry the header's Previous/Next binding images to
// the button-sheet provider. The header labels themselves use buttons.milo.
inline constexpr std::size_t kHeaderPreviousIcon = 0;
inline constexpr std::size_t kHeaderNextIcon = 13;

// Internal P1 marker and fingerprint probe. No guest pixels or scene files are
// read here. Disabled unless RB_BLITZ_P1_TRACE or a marker hash is set.
void InstallP1TextureOverride();
// Scan at boot, publish the validated catalog, and install the texture provider.
void InstallControllerSchemes(const std::filesystem::path& install_root,
                              const std::filesystem::path& user_root,
                              const std::function<void()>& pump_startup_window = {});
// Queue a selected, validated token for the GPU thread. Returns false for an unavailable token.
bool SelectControllerScheme(const std::string& id, uint64_t* ticket = nullptr);
std::optional<ControllerSchemeProgress> LatestAppliedControllerScheme();
// Publish the image glyphs used by the currently entered mapping page. The guest list text
// receives matching character codes; the GPU thread consumes this immutable batch in Poll().
void PublishInputIcons(std::string signature, std::vector<input::icons::Icon> icons);
// Publish every device variant together so the texture worker can prepare them
// before the next keyboard, mouse, or controller input changes the active art.
void PublishHintIcons(std::string signature,
    std::array<std::vector<input::icons::Icon>, 3> variants,
    std::array<input::icons::Icon, 3> calibration,
    input::icons::Family active_family);
bool InputIconsAvailable();
const controller_schemes::Registry* ControllerSchemeRegistry();

}  // namespace rb_blitz::assets
