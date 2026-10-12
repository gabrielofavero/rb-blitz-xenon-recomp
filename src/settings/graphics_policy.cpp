// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4's graphics policy. See graphics_policy.h for the two rules and the reason keys;
// docs/plans/in-game-settings-graphics-findings.md records the measurements and the honest
// unknowns (in particular: only D3D12 is built on this machine).

#include "settings/graphics_policy.h"

#include <algorithm>
#include <functional>
#include <string>

#include "settings/cvars.h"

namespace rb_blitz::settings {

namespace {

Value MakeEnum(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

Value MakeResolution(int32_t width, int32_t height) {
  Value out;
  out.kind = ValueKind::kResolution;
  out.width = width;
  out.height = height;
  return out;
}

bool Contains(const std::vector<std::string>& values, std::string_view wanted) {
  for (const std::string& value : values) {
    if (value == wanted) {
      return true;
    }
  }
  return false;
}

// The filtering steps in presentation order with the token the store uses. A "16x" label is
// never written as value 16 - the token's cvar value comes from FilteringToCvarValue.
struct FilteringStep {
  int32_t value;  // 1..5 == 1x..16x
  std::string_view token;
};
constexpr FilteringStep kFilteringSteps[] = {
    {1, "1x"}, {2, "2x"}, {3, "4x"}, {4, "8x"}, {5, "16x"},
};

// The highest explicit step at or below `ceiling` (0..5), or "" when none qualifies.
std::string HighestFilteringTokenAtMost(int32_t ceiling) {
  std::string best;
  for (const FilteringStep& step : kFilteringSteps) {
    if (step.value <= ceiling) {
      best = std::string(step.token);
    }
  }
  return best;
}

}  // namespace

bool IsGraphicsRow(SettingId id) {
  switch (id) {
    case SettingId::kResolution:
    case SettingId::kRenderer:
    case SettingId::kAntiAliasing:
    case SettingId::kFiltering:
      return true;
    default:
      return false;
  }
}

std::vector<std::string> AntiAliasingTokens(const CapabilitySnapshot& caps,
                                            std::string_view renderer_token) {
  std::vector<std::string> tokens{"none"};
  const RendererCapability* renderer = FindRenderer(caps, renderer_token);
  // `native` is offered only when this device's sample semantics were actually verified. An
  // empty label means "not proved on this device", and an unproved label does not ship
  // (contracts §7.1), so the choice simply is not offered.
  if (renderer != nullptr && renderer->usable && !renderer->native_aa_label.empty()) {
    tokens.push_back("native");
  }
  // FXAA is a swap post-effect and does not depend on a verified sample count, so any
  // usable renderer offers it. With no usable renderer, only `none` is honest.
  if (renderer != nullptr && renderer->usable) {
    tokens.push_back("fxaa");
    tokens.push_back("fxaa_extreme");
  }
  return tokens;
}

std::vector<std::string> FilteringTokens(const CapabilitySnapshot& caps,
                                         std::string_view renderer_token) {
  std::vector<std::string> tokens{"native"};
  const RendererCapability* renderer = FindRenderer(caps, renderer_token);
  const int32_t ceiling = renderer != nullptr ? renderer->filtering_ceiling : 0;
  if (ceiling >= 0) {
    tokens.push_back("none");
  }
  for (const FilteringStep& step : kFilteringSteps) {
    if (step.value <= ceiling) {
      tokens.push_back(std::string(step.token));
    }
  }
  return tokens;
}

std::string PolicyRenderer(const CapabilitySnapshot& caps, std::string_view wanted) {
  if (!wanted.empty()) {
    if (const RendererCapability* named = FindRenderer(caps, wanted);
        named != nullptr && named->usable) {
      return named->token;
    }
  }
  if (const RendererCapability* active = ActiveRenderer(caps);
      active != nullptr && active->usable) {
    return active->token;
  }
  for (const RendererCapability& renderer : caps.renderers) {
    if (renderer.usable) {
      return renderer.token;
    }
  }
  return {};
}

ValidationResult GraphicsReconciler::Validate(SettingId id, const Descriptor& descriptor,
                                              const Value& value,
                                              const CapabilitySnapshot& caps) const {
  return ValidateForRenderer(id, descriptor, value, caps, PolicyRenderer(caps, ""));
}

ValidationResult GraphicsReconciler::ValidateForRenderer(SettingId id, const Descriptor& descriptor,
                                                         const Value& value,
                                                         const CapabilitySnapshot& caps,
                                                         std::string_view renderer_token) const {
  // The kind and the row's own choice set are the default reconciler's job and stay its job.
  const ValidationResult base = Reconciler::Validate(id, descriptor, value, caps);
  if (!base.ok) {
    return base;
  }
  if (!IsGraphicsRow(id)) {
    return base;
  }
  switch (id) {
    case SettingId::kResolution: {
      if (value.kind != ValueKind::kResolution) {
        return ValidationResult{false, std::string(kGraphicsUnsupportedResolution)};
      }
      if (!IsSupportedResolution(caps, DisplayMode{value.width, value.height})) {
        return ValidationResult{false, std::string(kGraphicsUnsupportedResolution)};
      }
      return base;
    }
    case SettingId::kRenderer: {
      const RendererCapability* renderer = FindRenderer(caps, value.text);
      if (renderer == nullptr || !renderer->usable) {
        return ValidationResult{false, std::string(kGraphicsRendererUnavailable)};
      }
      return base;
    }
    case SettingId::kAntiAliasing: {
      if (!Contains(AntiAliasingTokens(caps, renderer_token), value.text)) {
        return ValidationResult{false, std::string(kGraphicsAntiAliasingUnavailable)};
      }
      return base;
    }
    case SettingId::kFiltering: {
      if (!Contains(FilteringTokens(caps, renderer_token), value.text)) {
        return ValidationResult{false, std::string(kGraphicsFilteringUnavailable)};
      }
      return base;
    }
    default:
      return base;
  }
}

Reconciliation GraphicsReconciler::Reconcile(SettingId id, const Descriptor& descriptor,
                                             const Value& requested,
                                             const CapabilitySnapshot& caps) const {
  Reconciliation result;
  result.effective = requested;
  if (!IsGraphicsRow(id)) {
    return result;
  }
  const std::string renderer = PolicyRenderer(caps, "");

  switch (id) {
    case SettingId::kResolution: {
      if (requested.kind == ValueKind::kResolution &&
          IsSupportedResolution(caps, DisplayMode{requested.width, requested.height})) {
        return result;  // a valid manual choice is never re-optimized
      }
      const DisplayMode replacement = LargestSupportedFitting(caps);
      if (replacement.width <= 0) {
        // Nothing is supported at all (a render path with no producible size). Report the
        // failure; the service then falls back to the row's compiled default.
        result.ok = false;
        result.reason_key = std::string(kGraphicsUnsupportedResolution);
        return result;
      }
      result.effective = MakeResolution(replacement.width, replacement.height);
      result.corrected = true;
      // The two cases the UI explains differently: a size the display cannot show (4K on a
      // monitor that is now 1080p) versus a size the render path cannot make at all.
      const bool exceeds_display =
          caps.primary_display_size.width > 0 &&
          (requested.width > caps.primary_display_size.width ||
           requested.height > caps.primary_display_size.height);
      result.correction_reason_key =
          std::string(exceeds_display ? kGraphicsCorrectedForDisplay : kGraphicsCorrectedUnsupported);
      result.reason_key = result.correction_reason_key;
      return result;
    }
    case SettingId::kRenderer: {
      if (!renderer.empty() && requested.text != renderer) {
        result.effective = MakeEnum(renderer);
        result.corrected = true;
        result.correction_reason_key = std::string(kGraphicsCorrectedRenderer);
        result.reason_key = result.correction_reason_key;
      }
      return result;
    }
    case SettingId::kAntiAliasing: {
      const std::vector<std::string> allowed = AntiAliasingTokens(caps, renderer);
      if (Contains(allowed, requested.text)) {
        return result;
      }
      // Deterministic degradation, never a silent index clamp: an unavailable native path
      // falls to the other AA mechanism that *is* offered (FXAA), and anything else falls to
      // no AA at all. Either way the user is told why.
      std::string replacement = "none";
      if (requested.text == "native" && Contains(allowed, "fxaa")) {
        replacement = "fxaa";
      }
      result.effective = MakeEnum(replacement);
      result.corrected = true;
      result.correction_reason_key = std::string(kGraphicsCorrectedAntiAliasing);
      result.reason_key = result.correction_reason_key;
      return result;
    }
    case SettingId::kFiltering: {
      const std::vector<std::string> allowed = FilteringTokens(caps, renderer);
      if (Contains(allowed, requested.text)) {
        return result;
      }
      int32_t wanted = -1;
      FilteringToCvarValue(requested.text, &wanted);
      const RendererCapability* device = FindRenderer(caps, renderer);
      const int32_t ceiling = device != nullptr ? device->filtering_ceiling : 0;
      // The highest offered step at or below the request, capped by the device ceiling.
      const int32_t bound = wanted >= 1 ? std::min(wanted, ceiling) : ceiling;
      std::string replacement = HighestFilteringTokenAtMost(bound);
      if (replacement.empty()) {
        replacement = "native";  // no explicit step qualifies: let the title decide
      }
      result.effective = MakeEnum(replacement);
      result.corrected = true;
      result.correction_reason_key = std::string(kGraphicsCorrectedFiltering);
      result.reason_key = result.correction_reason_key;
      return result;
    }
    default:
      return result;
  }
}

GuestScreenPlan PlanGuestScreen(const RenderPathLimits& limits, const DisplayMode& requested) {
  GuestScreenPlan plan;
  if (requested.width <= 0 || requested.height <= 0) {
    return plan;
  }
  // Largest guest size first, then largest scale first: the request is realised by the
  // clearest layout that divides it exactly.
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
  return plan;
}

GraphicsRecommendation RecommendGraphics(const CapabilitySnapshot& caps) {
  GraphicsRecommendation recommendation;
  const std::string renderer = PolicyRenderer(caps, "");
  const bool measured = !renderer.empty() &&
      IsSupportedResolution(caps, caps.verified_sustainable_resolution);
  recommendation.applicable = measured;
  recommendation.conservative = !measured;
  recommendation.renderer = MakeEnum(renderer.empty() ? "d3d12" : renderer);
  if (!measured) {
    // The bounded conservative route: the one combination that presented on every machine
    // this project measured, and a reason the UI can show.
    recommendation.reason_key = "settings.graphics.recommended_conservative";
    recommendation.resolution = MakeResolution(1280, 720);
    recommendation.anti_aliasing = MakeEnum("none");
    recommendation.filtering = MakeEnum("native");
    return recommendation;
  }

  DisplayMode best = caps.verified_sustainable_resolution;
  if (best.width <= 0) {
    best = SmallestSupported(caps);
  }
  if (best.width <= 0) {
    best = DisplayMode{1280, 720};
  }
  recommendation.reason_key = "settings.graphics.recommended_measured";
  recommendation.resolution = MakeResolution(best.width, best.height);

  const RendererCapability* device = FindRenderer(caps, renderer);
  // The device's verified native AA when it has one; otherwise no AA rather than a post
  // process the user did not ask for.
  const bool native_verified = device != nullptr && device->usable &&
                               !device->native_aa_label.empty();
  recommendation.anti_aliasing = MakeEnum(native_verified ? "native" : "none");
  // Filtering stays the title's own choice ("native") on a first run: the ceiling is a fact
  // the UI uses to bound what the user may *pick*, not a preference to impose.
  recommendation.filtering = MakeEnum("native");
  return recommendation;
}

bool GraphicsFallbackWarranted(const GraphicsBootMarker& marker) {
  return marker.attempted && !marker.presented;
}

GraphicsFallback RecoverGraphics(const CapabilitySnapshot& caps,
                                 const Value& requested_resolution,
                                 std::string_view requested_renderer, bool previous_boot_failed) {
  GraphicsFallback fallback;
  fallback.resolution = requested_resolution;
  fallback.renderer = MakeEnum(std::string(requested_renderer));
  if (!previous_boot_failed) {
    // Not re-optimizing: the request is returned untouched, and the AA/filtering fields stay
    // empty because this route has nothing to say about them.
    return fallback;
  }

  fallback.needed = true;
  fallback.reason_key = "settings.graphics.recovered_from_failure";

  DisplayMode floor{1280, 720};
  if (!IsSupportedResolution(caps, floor)) {
    floor = SmallestSupported(caps);
  }
  if (floor.width > 0) {
    fallback.resolution = MakeResolution(floor.width, floor.height);
  }
  const std::string renderer = PolicyRenderer(caps, requested_renderer);
  if (!renderer.empty()) {
    fallback.renderer = MakeEnum(renderer);
  }
  fallback.anti_aliasing = MakeEnum("none");
  fallback.filtering = MakeEnum("native");
  return fallback;
}

}  // namespace rb_blitz::settings
