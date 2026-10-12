// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4 - graphics capabilities: turning what the machine and the build actually expose into
// the frozen CapabilitySnapshot (contracts §5.3).
//
// SDK-free on purpose. The probing half (which displays exist, whether a backend's runtime
// initialized) is host work that needs a window and a GPU; the *compilation* half below is
// pure - it takes the probe results as plain data and produces the snapshot - so the rules
// that decide what a user may be offered are tested without a GPU, a monitor or a boot.
// That is the same split P3 used for the store, and for the same reason.
//
// Three rules the compilation enforces, all of them "do not lie" rules:
//
//   1. A backend whose DLL or loader is merely present is not usable. Only an initialization
//      that actually succeeded makes a renderer `usable` (the plan is explicit: do not list
//      an unusable backend just because its Vulkan loader exists).
//   2. `supported_resolutions` contains only exact render dimensions the render path can
//      really produce (P2: the guest screen size x the SDK's integer draw scale), trimmed to
//      what the primary display can show. It is never an output-size list.
//   3. A failed display enumeration is not evidence that a size is unsupported. With no
//      monitor query the producible set is published unchanged, so a temporarily detached
//      display cannot silently erase a valid 4K request.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "settings/model.h"

namespace rb_blitz::settings {

// --- what the render path can place ------------------------------------------------------
//
// P2 measured that the title chooses between a small set of guest screen sizes and the SDK
// multiplies the result by an integer draw scale (kMaxDrawResolutionScaleAlongAxis, 1..7).
// `base_sizes` is the guest sizes the implementation can actually ask the title for. The
// measured 960x540 guest mode can be scaled to 1920x1080 without changing the title's allocation.
// A base size the implementation cannot ask for must not be listed - that is what keeps an
// unavailable 1080p out of the offer set instead of offering a size the render path cannot
// reach.
struct RenderPathLimits {
  std::vector<DisplayMode> base_sizes = {{1280, 720}};
  // Draw scales the render path offers. The SDK's ceiling is 7; the project offers the
  // scales that correspond to standard sizes rather than every integer.
  std::vector<int32_t> scales = {1, 2, 3};
  // Largest render dimension the project offers (the plan's §7.1 matrix tops out at 4K).
  DisplayMode offer_ceiling = {3840, 2160};
};

// Title modes actually implemented by the production hook.
inline RenderPathLimits TitleRenderPath() {
  RenderPathLimits limits;
  limits.base_sizes = {{960, 540}, {1280, 720}};
  return limits;
}

// Every exact render size the render path can produce: each base size x each scale, dropping
// anything above the offer ceiling, deduplicated and sorted by area then width, largest
// first. Deterministic, so the UI order is stable across boots.
std::vector<DisplayMode> RenderPathSizes(const RenderPathLimits& limits);

// --- what the host probed -----------------------------------------------------------------
//
// One renderer as the host found it. `built` is a compile-time fact of this build, `detected`
// means the runtime it needs is present on the machine, `usable` means the host actually
// initialized it, and `active` means it is the backend this boot uses. They are separate
// because the plan requires the distinction to survive to the UI.
struct RendererProbe {
  std::string token;  // "d3d12" | "vulkan" (never an index)
  bool built = false;
  bool detected = false;
  bool usable = false;
  bool active = false;
  // The AA sample semantics this device verified (never the device's advertised maximum),
  // and the highest eligible anisotropic-filtering step, per contracts §5.3.
  std::string native_aa_label;
  int32_t filtering_ceiling = 0;
  std::string note;  // why it is not usable, for the log; never shown in the UI
};

struct CapabilityInputs {
  uint32_t logical_processors = 1;  // usable logical processors (the thread-count ceiling)
  bool display_enumeration_available = false;
  std::string primary_display_id;
  DisplayMode primary_display_size;
  std::vector<DisplayMode> display_modes;  // truthful; may be empty
  std::vector<RendererProbe> renderers;
  RenderPathLimits render_path;
  rb_blitz::launcher::LaunchTarget active_target = rb_blitz::launcher::LaunchTarget::kCommon;
  ProviderState provider;
  uint64_t revision = 1;
};

// The offer set: the render path's sizes trimmed to the primary display. With no usable
// enumeration (or no primary size) the producible set is returned unchanged, and the list is
// never empty - the smallest producible size is always the floor.
std::vector<DisplayMode> SupportedResolutions(const RenderPathLimits& limits,
                                              const std::vector<DisplayMode>& display_modes,
                                              bool enumeration_available,
                                              const DisplayMode& primary_display_size);

// Deduplicates and orders display modes by area then width then height, largest first. Used
// for both the enumerated modes and the offer set so a list never shows a size twice.
std::vector<DisplayMode> NormalizeDisplayModes(std::vector<DisplayMode> modes);

// Compiles a frozen CapabilitySnapshot. See the three rules in the file header.
CapabilitySnapshot BuildCapabilitySnapshot(const CapabilityInputs& inputs);

// --- small queries the policy and the UI share --------------------------------------------

// The renderer the host reports active, or the single usable one, or nullptr.
const RendererCapability* ActiveRenderer(const CapabilitySnapshot& caps);
const RendererCapability* FindRenderer(const CapabilitySnapshot& caps, std::string_view token);

// True when `size` is one of the snapshot's supported render sizes (exact match, no clamping).
bool IsSupportedResolution(const CapabilitySnapshot& caps, const DisplayMode& size);

// The largest supported size that fits inside the primary display, or the largest supported
// size when there is no primary size. Nullopt-free: returns a zero size when nothing is
// supported, which the caller treats as "no recommendation".
DisplayMode LargestSupportedFitting(const CapabilitySnapshot& caps);
DisplayMode SmallestSupported(const CapabilitySnapshot& caps);

// "1920x1080", the stable token form (and the token the store/schema use for a resolution).
std::string DisplayModeToken(const DisplayMode& size);
bool ParseDisplayModeToken(std::string_view token, DisplayMode* out);

}  // namespace rb_blitz::settings
