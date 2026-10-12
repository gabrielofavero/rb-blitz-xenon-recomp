// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pure half of P4's capability probe. See capabilities.h for the three "do not lie"
// rules this file enforces and docs/plans/in-game-settings-graphics-findings.md for the
// measurements behind the render-path limits.

#include "settings/capabilities.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace rb_blitz::settings {

namespace {

int64_t Area(const DisplayMode& size) {
  return int64_t(size.width) * int64_t(size.height);
}

// Area desc, then width desc, then height desc: one deterministic order for every list.
bool LargerFirst(const DisplayMode& lhs, const DisplayMode& rhs) {
  const int64_t lhs_area = Area(lhs);
  const int64_t rhs_area = Area(rhs);
  if (lhs_area != rhs_area) {
    return lhs_area > rhs_area;
  }
  if (lhs.width != rhs.width) {
    return lhs.width > rhs.width;
  }
  return lhs.height > rhs.height;
}

bool Positive(const DisplayMode& size) { return size.width > 0 && size.height > 0; }

// An aspect in per-mille (1920x1080 -> 1777) so two sizes compare without floating point.
int64_t AspectPerMille(const DisplayMode& size) {
  if (!Positive(size)) {
    return 0;
  }
  return int64_t(size.width) * 1000 / size.height;
}

// Same shape within 1%: real modes are not exact ratios (a 21:9 frame reports 2.370, and 1176x664 is
// 16:9 in practice while 1920x1080 is not 16:9 in integers).
bool SameAspect(int64_t lhs, int64_t rhs) {
  if (lhs <= 0 || rhs <= 0) {
    return lhs == rhs;
  }
  const int64_t difference = lhs > rhs ? lhs - rhs : rhs - lhs;
  return difference * 100 <= rhs;
}

}  // namespace

std::vector<DisplayMode> DiagnosticGuestBases() {
  std::vector<DisplayMode> sizes;
  const char* raw = std::getenv("RBBLITZ_PROBE_GUEST_BASES");
  if (raw == nullptr) {
    return sizes;
  }
  std::string_view text(raw);
  size_t start = 0;
  while (start <= text.size()) {
    const size_t comma = text.find(',', start);
    const std::string_view item =
        text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    const size_t cross = item.find('x');
    if (cross != std::string_view::npos) {
      const DisplayMode size{std::atoi(std::string(item.substr(0, cross)).c_str()),
                             std::atoi(std::string(item.substr(cross + 1)).c_str())};
      // A degenerate entry is dropped rather than carried into the planner.
      if (Positive(size)) {
        sizes.push_back(size);
      }
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return sizes;
}

std::vector<DisplayMode> NormalizeDisplayModes(std::vector<DisplayMode> modes) {
  std::vector<DisplayMode> out;
  out.reserve(modes.size());
  for (const DisplayMode& mode : modes) {
    if (!Positive(mode)) {
      continue;
    }
    bool duplicate = false;
    for (const DisplayMode& kept : out) {
      if (kept.width == mode.width && kept.height == mode.height) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      out.push_back(mode);
    }
  }
  std::stable_sort(out.begin(), out.end(), LargerFirst);
  return out;
}

std::vector<DisplayMode> RenderPathSizes(const RenderPathLimits& limits) {
  std::vector<DisplayMode> sizes;
  for (const DisplayMode& base : limits.base_sizes) {
    if (!Positive(base)) {
      continue;
    }
    for (const int32_t scale : limits.scales) {
      if (scale < 1) {
        continue;
      }
      const DisplayMode scaled{base.width * scale, base.height * scale};
      // Above the offer ceiling the EDRAM and render targets stop being reasonable and no
      // display in the plan's matrix asks for it, so it is not offered.
      if (scaled.width > limits.offer_ceiling.width || scaled.height > limits.offer_ceiling.height) {
        continue;
      }
      sizes.push_back(scaled);
    }
  }
  return NormalizeDisplayModes(std::move(sizes));
}

std::vector<DisplayMode> SupportedResolutions(const RenderPathLimits& limits,
                                              const std::vector<DisplayMode>& display_modes,
                                              bool enumeration_available,
                                              const DisplayMode& primary_display_size) {
  const std::vector<DisplayMode> producible = RenderPathSizes(limits);
  if (producible.empty()) {
    return producible;
  }
  if (!enumeration_available) {
    // Rule 3: a failed monitor query is not evidence a size is unsupported. Publish the
    // producible set unchanged rather than shrinking the user's choices to a guess.
    return producible;
  }

  DisplayMode ceiling = primary_display_size;
  if (!Positive(ceiling)) {
    // No primary size named: the largest enumerated mode is the best available ceiling.
    const std::vector<DisplayMode> modes = NormalizeDisplayModes(display_modes);
    if (!modes.empty()) {
      ceiling = modes.front();
    }
  }

  std::vector<DisplayMode> out;
  for (const DisplayMode& size : producible) {
    if (ceiling.width <= 0 || (size.width <= ceiling.width && size.height <= ceiling.height)) {
      out.push_back(size);
    }
  }
  // A display's own mode is offered when the render path can produce it, even though it is not in
  // the 16:9/4:3 catalogue: that is precisely the ultrawide case (2560x1080 on a 21:9 primary gets
  // planned as a forced 1280x540 guest frame at 2x). The two questions - "can the render path make
  // this size?" and "is this size in the catalogue?" - are answered by the same planner.
  //
  // Only the *primary display's own aspect family* is added, though. A 21:9 monitor also advertises
  // 4:3 and PAL modes, and offering them would put sizes in the list whose only effect is the black
  // bars this project is trying to remove (the presenter letterboxes a frame whose aspect is not the
  // window's). A mode off that family is not a render size this project offers; 16:9 is the
  // catalogue's job, so the family check only ever *adds* wide/native shapes.
  const int64_t ceiling_aspect = AspectPerMille(ceiling);
  for (const DisplayMode& mode : NormalizeDisplayModes(display_modes)) {
    if (mode.width > limits.offer_ceiling.width || mode.height > limits.offer_ceiling.height) {
      continue;
    }
    if (!PlanGuestScreen(limits, mode).supported) {
      continue;
    }
    const bool in_catalogue = [&] {
      for (const DisplayMode& produced : producible) {
        if (produced.width == mode.width && produced.height == mode.height) {
          return true;
        }
      }
      return false;
    }();
    if (!in_catalogue && !SameAspect(AspectPerMille(mode), ceiling_aspect)) {
      continue;
    }
    bool duplicate = false;
    for (const DisplayMode& kept : out) {
      if (kept.width == mode.width && kept.height == mode.height) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      out.push_back(mode);
    }
  }
  out = NormalizeDisplayModes(std::move(out));
  if (out.empty()) {
    // Never offer an empty list: the smallest producible size is the floor (it fits the
    // smallest display the title itself supports).
    out.push_back(producible.back());
  }
  return out;
}

GuestScreenPlan PlanGuestScreen(const RenderPathLimits& limits, const DisplayMode& requested) {
  GuestScreenPlan plan;
  if (requested.width <= 0 || requested.height <= 0) {
    return plan;
  }
  // The title's own sizes first, largest guest size then largest scale: a request those can reach
  // exactly keeps the title's own layout, which is the behaviour every shipped 16:9 size has.
  std::vector<DisplayMode> bases = limits.base_sizes;
  std::stable_sort(bases.begin(), bases.end(), [](const DisplayMode& lhs, const DisplayMode& rhs) {
    return int64_t(lhs.width) * lhs.height > int64_t(rhs.width) * rhs.height;
  });
  std::vector<int32_t> scales = limits.scales;
  std::stable_sort(scales.begin(), scales.end(), std::greater<int32_t>());
  for (const DisplayMode& base : bases) {
    if (base.width <= 0 || base.height <= 0) {
      continue;
    }
    for (const int32_t scale : scales) {
      if (scale < 1) {
        continue;
      }
      if (base.width * scale == requested.width && base.height * scale == requested.height) {
        plan.supported = true;
        plan.guest_size = base;
        plan.draw_scale = scale;
        return plan;
      }
    }
  }

  // Otherwise the guest's width is *forced* beside a height the title does lay out at. Both
  // dimensions have to divide exactly by the scale, because the render target is the frame times
  // that one integer multiplier - a frame that only approximates the target is the output-size
  // substitute the project forbids (docs/plans/ultrawide-findings.md). Measured 2026-10-12:
  // 1680x720 as guest 1680x720 x1 and 2560x1080 as 1280x540 x2 both render exactly their size.
  for (const int32_t scale : scales) {
    if (scale < 1 || requested.width % scale != 0 || requested.height % scale != 0) {
      continue;
    }
    const DisplayMode guest{requested.width / scale, requested.height / scale};
    if (guest.width < static_cast<int32_t>(limits.min_guest_width) ||
        guest.width > static_cast<int32_t>(limits.max_guest_width) ||
        guest.height < static_cast<int32_t>(limits.min_guest_height) ||
        guest.height > static_cast<int32_t>(limits.max_guest_height)) {
      continue;
    }
    const int64_t aspect_x1000 = int64_t(guest.width) * 1000 / guest.height;
    if (aspect_x1000 < limits.min_aspect_x1000 || aspect_x1000 > limits.max_aspect_x1000) {
      continue;
    }
    plan.supported = true;
    plan.guest_size = guest;
    plan.draw_scale = scale;
    return plan;
  }
  return plan;
}

CapabilitySnapshot BuildCapabilitySnapshot(const CapabilityInputs& inputs) {
  CapabilitySnapshot caps;
  caps.revision = inputs.revision;
  caps.logical_processors = inputs.logical_processors < 1 ? 1 : inputs.logical_processors;
  caps.primary_display_id = inputs.primary_display_id;
  caps.primary_display_size = inputs.primary_display_size;
  caps.display_modes = NormalizeDisplayModes(inputs.display_modes);
  caps.active_target = inputs.active_target;
  caps.provider = inputs.provider;

  for (const RendererProbe& probe : inputs.renderers) {
    // Rule 1: a renderer nobody asked about - not built, not detected, never initialized,
    // not active - is not published at all. Its DLL being on disk is not evidence.
    if (!probe.built && !probe.detected && !probe.usable && !probe.active) {
      continue;
    }
    if (probe.token.empty()) {
      continue;
    }
    RendererCapability renderer;
    renderer.token = probe.token;
    renderer.built = probe.built;
    renderer.detected = probe.detected;
    renderer.usable = probe.usable;
    renderer.active = probe.active;
    renderer.native_aa_label = probe.native_aa_label;
    renderer.filtering_ceiling = probe.filtering_ceiling;
    caps.renderers.push_back(std::move(renderer));
  }

  caps.supported_resolutions =
      SupportedResolutions(inputs.render_path, inputs.display_modes,
                           inputs.display_enumeration_available, inputs.primary_display_size);
  return caps;
}

const RendererCapability* FindRenderer(const CapabilitySnapshot& caps, std::string_view token) {
  for (const RendererCapability& renderer : caps.renderers) {
    if (renderer.token == token) {
      return &renderer;
    }
  }
  return nullptr;
}

const RendererCapability* ActiveRenderer(const CapabilitySnapshot& caps) {
  for (const RendererCapability& renderer : caps.renderers) {
    if (renderer.active) {
      return &renderer;
    }
  }
  const RendererCapability* only_usable = nullptr;
  for (const RendererCapability& renderer : caps.renderers) {
    if (!renderer.usable) {
      continue;
    }
    if (only_usable != nullptr) {
      return nullptr;  // more than one usable and none active: the host must say which
    }
    only_usable = &renderer;
  }
  return only_usable;
}

bool IsSupportedResolution(const CapabilitySnapshot& caps, const DisplayMode& size) {
  for (const DisplayMode& supported : caps.supported_resolutions) {
    if (supported.width == size.width && supported.height == size.height) {
      return true;
    }
  }
  return false;
}

DisplayMode LargestSupportedFitting(const CapabilitySnapshot& caps) {
  // The list is normalized largest-first, so the first one that fits is the answer. With no
  // primary size every supported size "fits" and the largest wins.
  const bool have_display = Positive(caps.primary_display_size);
  for (const DisplayMode& supported : caps.supported_resolutions) {
    if (!have_display || (supported.width <= caps.primary_display_size.width &&
                          supported.height <= caps.primary_display_size.height)) {
      return supported;
    }
  }
  return DisplayMode{};
}

DisplayMode SmallestSupported(const CapabilitySnapshot& caps) {
  if (caps.supported_resolutions.empty()) {
    return DisplayMode{};
  }
  return caps.supported_resolutions.back();  // normalized largest-first
}

std::string DisplayModeToken(const DisplayMode& size) {
  return std::to_string(size.width) + "x" + std::to_string(size.height);
}

bool ParseDisplayModeToken(std::string_view token, DisplayMode* out) {
  if (out == nullptr) {
    return false;
  }
  const std::size_t x = token.find('x');
  if (x == std::string_view::npos || x == 0 || x + 1 >= token.size()) {
    return false;
  }
  int32_t width = 0;
  for (const char character : token.substr(0, x)) {
    if (character < '0' || character > '9') {
      return false;
    }
    width = width * 10 + (character - '0');
    if (width > 100000) {
      return false;
    }
  }
  int32_t height = 0;
  for (const char character : token.substr(x + 1)) {
    if (character < '0' || character > '9') {
      return false;
    }
    height = height * 10 + (character - '0');
    if (height > 100000) {
      return false;
    }
  }
  if (width <= 0 || height <= 0) {
    return false;
  }
  out->width = width;
  out->height = height;
  return true;
}

}  // namespace rb_blitz::settings
