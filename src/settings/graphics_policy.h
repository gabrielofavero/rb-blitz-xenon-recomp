// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4 - the graphics policy: the reconciliation the settings service calls, the first-run
// recommendation, and the last-known-good recovery route.
//
// All of it is pure. It reads a frozen CapabilitySnapshot and a stored Value and returns a
// Value; it opens no file, touches no cvar and needs no boot, so every rule below is tested
// host-side (tests/settings_graphics_tests.cpp). The boot wiring that calls it is the
// coordinator's, exactly as P3's storage wiring is.
//
// The two rules that make this policy different from the default reconciler:
//
//   * It never re-optimizes a valid manual choice. A rejected value is corrected
//     deterministically and the *request* is left alone (contracts §5.2: requested/effective
//     are separate values), so a monitor that is unplugged for one boot does not erase the
//     4K request the user made.
//   * It never offers what the device cannot do. `AntiAliasingTokens`/`FilteringTokens`
//     answer per renderer from the verified `native_aa_label` and `filtering_ceiling`, so a
//     step the renderer has not proved is not in the list (contracts §7.1).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "settings/capabilities.h"
#include "settings/model.h"
#include "settings/service.h"

namespace rb_blitz::settings {

// The four rows this policy owns. Vsync is a graphics-category row but not a
// capability-dependent one, so it keeps the default reconciler's kind/choice check.
bool IsGraphicsRow(SettingId id);

// --- correction reasons (stable keys the UI localizes) ------------------------------------
inline constexpr std::string_view kGraphicsCorrectedUnsupported = "settings.graphics.unsupported";
inline constexpr std::string_view kGraphicsCorrectedForDisplay = "settings.graphics.for_display";
inline constexpr std::string_view kGraphicsCorrectedRenderer =
    "settings.graphics.renderer_unavailable";
inline constexpr std::string_view kGraphicsCorrectedAntiAliasing =
    "settings.graphics.aa_unavailable";
inline constexpr std::string_view kGraphicsCorrectedFiltering =
    "settings.graphics.filtering_ceiling";
inline constexpr std::string_view kGraphicsUnsupportedResolution =
    "settings.graphics.unsupported_resolution";
inline constexpr std::string_view kGraphicsRendererUnavailable =
    "settings.graphics.renderer_unavailable_choice";
inline constexpr std::string_view kGraphicsAntiAliasingUnavailable =
    "settings.graphics.aa_unavailable_choice";
inline constexpr std::string_view kGraphicsFilteringUnavailable =
    "settings.graphics.filtering_unavailable_choice";

// --- per-renderer choice sets --------------------------------------------------------------
//
// The AA choices a renderer can honour: `none` and the FXAA pair always, `native` only when
// the device's sample semantics were verified (a non-empty native_aa_label). The order is the
// presentation order; the caller (P5's menu) shows exactly this list rather than the static
// one, so a disabled native path is never offered as if it worked.
std::vector<std::string> AntiAliasingTokens(const CapabilitySnapshot& caps,
                                            std::string_view renderer_token);
// The filtering choices a renderer can honour, in presentation order: `native` always,
// `none`/`1x`..`16x` up to the device's ceiling. A "16x" label is never written as 16 (§7).
std::vector<std::string> FilteringTokens(const CapabilitySnapshot& caps,
                                         std::string_view renderer_token);

// The renderer a request or a transaction is evaluated against: the named token when it is
// usable, else the active renderer. Returns "" when the machine has no usable renderer.
std::string PolicyRenderer(const CapabilitySnapshot& caps, std::string_view wanted);

// --- the render path's guest-size plan -----------------------------------------------------
//
// P2's finding: the title chooses a guest screen size and the SDK multiplies the result by an
// integer draw scale, so a requested *render* size is realised as (guest base size, draw
// scale). This is the inverse of RenderPathSizes, and it is the whole of the render path's
// decision: given the resolution row's effective value, which guest size must the title be
// asked to lay out at, and what scale makes it the requested render size?
struct GuestScreenPlan {
  bool supported = false;
  DisplayMode guest_size;  // the size the title must be asked for
  int32_t draw_scale = 1;  // the SDK's integer multiplier (the GPU plugin's draw scale)
};

// The plan for an exact render size. The largest guest size that divides the request by an
// offered scale wins (the clearest UI and HUD layout), so 3840x2160 plans as 1080p x 2 rather
// than as 720p x 3. An unsupported size reports supported=false and must not be applied.
GuestScreenPlan PlanGuestScreen(const RenderPathLimits& limits, const DisplayMode& requested);

// --- the reconciler -----------------------------------------------------------------------

class GraphicsReconciler final : public Reconciler {
 public:
  ValidationResult Validate(SettingId id, const Descriptor& descriptor, const Value& value,
                            const CapabilitySnapshot& caps) const override;
  Reconciliation Reconcile(SettingId id, const Descriptor& descriptor, const Value& requested,
                           const CapabilitySnapshot& caps) const override;

  // Contracts §6.3: a renderer change revalidates AA/filtering/resolution together. The UI
  // (or the host) calls this for each affected row after the pending renderer changes, so an
  // acceptance from the *previous* renderer's list is refused rather than clamped by index.
  ValidationResult ValidateForRenderer(SettingId id, const Descriptor& descriptor,
                                       const Value& value, const CapabilitySnapshot& caps,
                                       std::string_view renderer_token) const;
};

// --- first run ----------------------------------------------------------------------------

struct GraphicsRecommendation {
  bool applicable = false;    // false when the machine could not be measured at all
  bool conservative = false;  // the bounded route for unknown hardware
  std::string reason_key;
  Value resolution;
  Value renderer;
  Value anti_aliasing;
  Value filtering;
};

// First-run graphics policy, from measurements only. A measured machine gets the primary
// display's own largest renderable size, the backend this boot actually uses, the verified
// native AA and the highest filtering step the device proved; a machine that could not be
// measured gets the bounded conservative route (720p, the only usable backend, no AA, native
// filtering) and says so. Applied only when the rows have no manual provenance.
GraphicsRecommendation RecommendGraphics(const CapabilitySnapshot& caps);

// --- last-known-good recovery --------------------------------------------------------------

// The durable marker a boot maintains: written before presentation is attempted, updated
// once a frame is presented, and cleared on a clean shutdown. P4 owns the shape; the
// coordinator owns where the boot reads and writes it.
struct GraphicsBootMarker {
  bool attempted = false;  // a boot reached presentation
  bool presented = false;  // ... and presented at least one frame
  Value attempted_resolution;
};

// "Attempted but never presented" is the failure signal. A marker that never attempted
// (first run, or a boot that failed before graphics) is not a graphics failure.
bool GraphicsFallbackWarranted(const GraphicsBootMarker& marker);

struct GraphicsFallback {
  bool needed = false;
  std::string reason_key;
  Value resolution;
  Value renderer;
  Value anti_aliasing;
  Value filtering;
};

// The route a boot that failed to present takes. When no failure is recorded the request is
// returned unchanged (a valid manual choice is never re-optimized on boot); when one is
// recorded the safe floor is returned - the title's native 720p, the active backend, no AA,
// native filtering - which is a combination that presented at least once on every machine
// this project has measured.
GraphicsFallback RecoverGraphics(const CapabilitySnapshot& caps, const Value& requested_resolution,
                                 std::string_view requested_renderer, bool previous_boot_failed);

}  // namespace rb_blitz::settings
