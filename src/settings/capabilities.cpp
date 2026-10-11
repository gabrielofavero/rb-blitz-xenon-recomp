// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pure half of P4's capability probe. See capabilities.h for the three "do not lie"
// rules this file enforces and docs/plans/in-game-settings-graphics-findings.md for the
// measurements behind the render-path limits.

#include "settings/capabilities.h"

#include <algorithm>
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

}  // namespace

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
  if (!Positive(ceiling)) {
    return producible;
  }

  std::vector<DisplayMode> out;
  for (const DisplayMode& size : producible) {
    if (size.width <= ceiling.width && size.height <= ceiling.height) {
      out.push_back(size);
    }
  }
  if (out.empty()) {
    // Never offer an empty list: the smallest producible size is the floor (it fits the
    // smallest display the title itself supports).
    out.push_back(producible.back());
  }
  return out;
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
