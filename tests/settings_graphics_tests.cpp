// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4 host tests for the graphics capabilities and the graphics policy
// (src/settings/capabilities.{h,cpp}, src/settings/graphics_policy.{h,cpp}). No SDK, no boot,
// no GPU: the probe results are plain data, so every rule the plan asks P4 to prove is a
// value-in/value-out check here.
//
// The cases the parent-plan P4 prompt names, one by one:
//   4K -> 1080p (a smaller monitor)           TestReconcileResolution
//   CPU-count reduction                        TestLogicalProcessors
//   a backend that disappears                   TestReconcileRenderer
//   AA / filter limit changes                   TestReconcileAntiAliasing, TestReconcileFiltering
//   a renderer change inside an edit            TestRendererChangeInsideEdit
//   missing display enumeration                 TestSupportedResolutions, TestReconcileResolution
//   display removal / reconnection              TestDisplayReconnection
//   unknown hardware                            TestFirstRun (conservative), TestReconcileRenderer
//   first-run policy                            TestFirstRun
//   last-known-good recovery                    TestRecovery

#include "check.h"

#include "settings/capabilities.h"
#include "settings/graphics_policy.h"
#include "settings/graphics_boot.h"
#include "settings/schema.h"
#include "settings/service.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::settings;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

// --- small builders -----------------------------------------------------------------------

Value ResolutionV(int32_t width, int32_t height) {
  Value out;
  out.kind = ValueKind::kResolution;
  out.width = width;
  out.height = height;
  return out;
}

Value EnumV(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

bool IsSize(const DisplayMode& mode, int32_t width, int32_t height) {
  return mode.width == width && mode.height == height;
}

std::string SizesToString(const std::vector<DisplayMode>& sizes) {
  std::string out;
  for (const DisplayMode& size : sizes) {
    if (!out.empty()) {
      out += ",";
    }
    out += DisplayModeToken(size);
  }
  return out;
}

bool IsRes(const Value& value, int32_t width, int32_t height) {
  return value.kind == ValueKind::kResolution && value.width == width && value.height == height;
}

bool IsToken(const Value& value, const char* token) {
  return (value.kind == ValueKind::kEnum || value.kind == ValueKind::kString) &&
         value.text == token;
}

// A renderer as a measured device reports it: verified native AA and a filtering ceiling.
RendererProbe GoodRenderer(std::string token, bool active, std::string native_aa = "2x MSAA",
                           int32_t ceiling = 5) {
  RendererProbe probe;
  probe.token = std::move(token);
  probe.built = true;
  probe.detected = true;
  probe.usable = true;
  probe.active = active;
  probe.native_aa_label = std::move(native_aa);
  probe.filtering_ceiling = ceiling;
  return probe;
}

// A machine whose only backend is d3d12 and whose primary display is `width x height`.
CapabilitySnapshot Machine(int32_t width, int32_t height, bool enumeration = true) {
  CapabilityInputs inputs;
  inputs.display_enumeration_available = enumeration;
  inputs.primary_display_id = "PRIMARY";
  inputs.primary_display_size = {width, height};
  if (enumeration) {
    inputs.display_modes = {{width, height}};
  }
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  inputs.renderers.push_back(GoodRenderer("d3d12", true));
  return BuildCapabilitySnapshot(inputs);
}

const Descriptor* Find(const std::vector<Descriptor>& descriptors, SettingId id) {
  for (const Descriptor& descriptor : descriptors) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

// --- capabilities -------------------------------------------------------------------------

void TestRenderPathSizes() {
  BeginCase("capabilities: producible sizes are base x scale, deduped and ordered");

  RenderPathLimits plain;
  plain.base_sizes = {{1280, 720}};
  const std::vector<DisplayMode> plain_sizes = RenderPathSizes(plain);
  CHECK_EQ(plain_sizes.size(), 3u);
  CHECK_TRUE(IsSize(plain_sizes[0], 3840, 2160));
  CHECK_TRUE(IsSize(plain_sizes[1], 2560, 1440));
  CHECK_TRUE(IsSize(plain_sizes[2], 1280, 720));

  // The Variant B guest height adds the real 1920x1080; 3840x2160 is 1280x720*3 and
  // 1920x1080*2 at once, so it appears once, and 1920x1080*3 is above the offer ceiling.
  RenderPathLimits with_1080;
  with_1080.base_sizes = {{1280, 720}, {1920, 1080}};
  CHECK_TRUE(SizesToString(RenderPathSizes(with_1080)) ==
             "3840x2160,2560x1440,1920x1080,1280x720");

  // A scale below 1 produces nothing; the offer ceiling trims the top.
  RenderPathLimits tiny;
  tiny.base_sizes = {{1280, 720}};
  tiny.scales = {0, 1};
  tiny.offer_ceiling = {1280, 720};
  const std::vector<DisplayMode> tiny_sizes = RenderPathSizes(tiny);
  CHECK_EQ(tiny_sizes.size(), 1u);
  CHECK_TRUE(IsSize(tiny_sizes[0], 1280, 720));
}

void TestSupportedResolutions() {
  BeginCase("capabilities: the offer set is the producible set trimmed to the primary display");

  RenderPathLimits limits;
  limits.base_sizes = {{1280, 720}, {1920, 1080}};

  const std::vector<DisplayMode> on_1080 =
      SupportedResolutions(limits, {{1920, 1080}, {1280, 720}}, true, {1920, 1080});
  CHECK_TRUE(SizesToString(on_1080) == "1920x1080,1280x720");

  const std::vector<DisplayMode> on_4k =
      SupportedResolutions(limits, {{3840, 2160}, {1920, 1080}}, true, {3840, 2160});
  CHECK_EQ(on_4k.size(), 4u);

  // Missing enumeration (rule 3): every producible size stays on offer.
  CHECK_EQ(SupportedResolutions(limits, {}, false, {}).size(), 4u);

  // A named primary with no explicit size falls back to the largest enumerated mode.
  CHECK_TRUE(SizesToString(SupportedResolutions(limits, {{2560, 1440}, {1920, 1080}}, true, {})) ==
             "2560x1440,1920x1080,1280x720");

  // A display smaller than everything producible still gets the smallest producible size.
  const std::vector<DisplayMode> too_small =
      SupportedResolutions(limits, {{320, 240}}, true, {320, 240});
  CHECK_EQ(too_small.size(), 1u);
  CHECK_TRUE(IsSize(too_small[0], 1280, 720));
}

void TestBuildSnapshot() {
  BeginCase("capabilities: an unusable backend is never published as usable");

  CapabilityInputs inputs;
  inputs.logical_processors = 0;  // a bad probe floors at 1, never zero
  inputs.display_enumeration_available = true;
  inputs.primary_display_id = "PRIMARY";
  inputs.primary_display_size = {1920, 1080};
  inputs.display_modes = {{1920, 1080}, {1920, 1080}, {960, 540}};
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};

  RendererProbe unbuilt;
  unbuilt.token = "vulkan";
  unbuilt.detected = true;  // the Vulkan loader exists on this machine
  inputs.renderers.push_back(unbuilt);
  RendererProbe absent;
  absent.token = "glide";  // neither built nor present: not published at all
  inputs.renderers.push_back(absent);
  inputs.renderers.push_back(GoodRenderer("d3d12", /*active=*/true));

  const CapabilitySnapshot caps = BuildCapabilitySnapshot(inputs);
  CHECK_EQ(caps.logical_processors, 1u);
  CHECK_EQ(caps.display_modes.size(), 2u);
  CHECK_TRUE(IsSize(caps.display_modes[0], 1920, 1080));
  CHECK_EQ(caps.renderers.size(), 2u);
  CHECK_TRUE(FindRenderer(caps, "glide") == nullptr);
  const RendererCapability* vulkan = FindRenderer(caps, "vulkan");
  CHECK_TRUE(vulkan != nullptr && vulkan->detected);
  CHECK_FALSE(vulkan->usable);  // "the loader exists" is not "it works"
  const RendererCapability* d3d12 = FindRenderer(caps, "d3d12");
  CHECK_TRUE(d3d12 != nullptr && d3d12->usable && d3d12->active);
  CHECK_TRUE(ActiveRenderer(caps) == d3d12);
  CHECK_TRUE(SizesToString(caps.supported_resolutions) == "1920x1080,1280x720,960x540");
  CHECK_TRUE(IsSupportedResolution(caps, {1920, 1080}));
  CHECK_FALSE(IsSupportedResolution(caps, {2560, 1440}));

  // The chosen renderer is the named one when it is usable, otherwise the active one. With
  // two usable renderers and neither marked active, the query refuses to guess.
  CapabilityInputs ambiguous_inputs;
  ambiguous_inputs.renderers.push_back(GoodRenderer("d3d12", false));
  ambiguous_inputs.renderers.push_back(GoodRenderer("vulkan", false));
  const CapabilitySnapshot ambiguous = BuildCapabilitySnapshot(ambiguous_inputs);
  CHECK_TRUE(ActiveRenderer(ambiguous) == nullptr);
  CHECK_TRUE(PolicyRenderer(ambiguous, "vulkan") == "vulkan");
}

void TestLogicalProcessors() {
  BeginCase("capabilities: a reduced CPU count is published truthfully");

  CapabilityInputs before;
  before.logical_processors = 8;
  CHECK_EQ(BuildCapabilitySnapshot(before).logical_processors, 8u);

  CapabilityInputs after;
  after.logical_processors = 4;
  CHECK_EQ(BuildCapabilitySnapshot(after).logical_processors, 4u);
}

void TestDisplayReconnection() {
  BeginCase("capabilities: a detached display narrows nothing; a reattached one narrows again");

  RenderPathLimits limits;
  limits.base_sizes = {{1280, 720}, {1920, 1080}};

  CapabilityInputs gone;
  gone.display_enumeration_available = false;
  gone.render_path = limits;
  CHECK_EQ(BuildCapabilitySnapshot(gone).supported_resolutions.size(), 4u);

  CapabilityInputs back;
  back.display_enumeration_available = true;
  back.primary_display_size = {1920, 1080};
  back.display_modes = {{1920, 1080}};
  back.render_path = limits;
  const CapabilitySnapshot reattached = BuildCapabilitySnapshot(back);
  CHECK_TRUE(SizesToString(reattached.supported_resolutions) == "1920x1080,1280x720");
  CHECK_TRUE(IsSize(LargestSupportedFitting(reattached), 1920, 1080));

  CapabilityInputs big;
  big.display_enumeration_available = true;
  big.primary_display_size = {3840, 2160};
  big.render_path = limits;
  CHECK_TRUE(IsSize(LargestSupportedFitting(BuildCapabilitySnapshot(big)), 3840, 2160));
}

// --- the reconciler ------------------------------------------------------------------------

void TestReconcileResolution() {
  BeginCase("policy: 4K on a monitor that is now 1080p is corrected, and the request is kept");

  const CapabilitySnapshot caps = Machine(1920, 1080);
  const std::vector<Descriptor> descriptors = schema::BuildDescriptors(caps);
  const Descriptor* resolution = Find(descriptors, SettingId::kResolution);
  CHECK_TRUE(resolution != nullptr);

  GraphicsReconciler reconciler;
  const Reconciliation corrected =
      reconciler.Reconcile(SettingId::kResolution, *resolution, ResolutionV(3840, 2160), caps);
  CHECK_TRUE(corrected.ok);
  CHECK_TRUE(corrected.corrected);
  CHECK_TRUE(IsRes(corrected.effective, 1920, 1080));
  CHECK_TRUE(corrected.correction_reason_key == kGraphicsCorrectedForDisplay);

  // A valid manual choice is never re-optimized, at any size in the offer set.
  const Reconciliation kept =
      reconciler.Reconcile(SettingId::kResolution, *resolution, ResolutionV(1280, 720), caps);
  CHECK_FALSE(kept.corrected);
  CHECK_TRUE(IsRes(kept.effective, 1280, 720));

  // A size the render path cannot produce at all (not merely too big) is a different reason.
  const Reconciliation unsupported =
      reconciler.Reconcile(SettingId::kResolution, *resolution, ResolutionV(1600, 900), caps);
  CHECK_TRUE(unsupported.corrected);
  CHECK_TRUE(unsupported.correction_reason_key == kGraphicsCorrectedUnsupported);
  CHECK_TRUE(IsRes(unsupported.effective, 1920, 1080));

  // Missing enumeration: the same 4K request is left alone, because a failed monitor query
  // is not evidence that 4K is unsupported (contracts §5.2).
  const CapabilitySnapshot blind = Machine(0, 0, /*enumeration=*/false);
  const std::vector<Descriptor> blind_descriptors = schema::BuildDescriptors(blind);
  const Descriptor* blind_resolution = Find(blind_descriptors, SettingId::kResolution);
  const Reconciliation kept_blind = reconciler.Reconcile(SettingId::kResolution, *blind_resolution,
                                                        ResolutionV(3840, 2160), blind);
  CHECK_FALSE(kept_blind.corrected);
  CHECK_TRUE(IsRes(kept_blind.effective, 3840, 2160));

  // The validation side: an unsupported size cannot be selected in the first place.
  CHECK_FALSE(
      reconciler.Validate(SettingId::kResolution, *resolution, ResolutionV(3840, 2160), caps).ok);
  CHECK_TRUE(
      reconciler.Validate(SettingId::kResolution, *resolution, ResolutionV(1920, 1080), caps).ok);
}

void TestReconcileRenderer() {
  BeginCase("policy: a backend that disappears is corrected to the one that works");

  CapabilityInputs inputs;
  inputs.display_enumeration_available = true;
  inputs.primary_display_size = {1920, 1080};
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  // Vulkan is built and its loader is present, but this machine's driver never initialized.
  RendererProbe vulkan;
  vulkan.token = "vulkan";
  vulkan.built = true;
  vulkan.detected = true;
  vulkan.usable = false;
  vulkan.note = "vkCreateInstance failed";
  inputs.renderers.push_back(vulkan);
  inputs.renderers.push_back(GoodRenderer("d3d12", /*active=*/true));

  const CapabilitySnapshot caps = BuildCapabilitySnapshot(inputs);
  const std::vector<Descriptor> descriptors = schema::BuildDescriptors(caps);
  const Descriptor* renderer = Find(descriptors, SettingId::kRenderer);
  CHECK_TRUE(renderer != nullptr);

  GraphicsReconciler reconciler;
  const Reconciliation corrected =
      reconciler.Reconcile(SettingId::kRenderer, *renderer, EnumV("vulkan"), caps);
  CHECK_TRUE(corrected.corrected);
  CHECK_TRUE(IsToken(corrected.effective, "d3d12"));
  CHECK_TRUE(corrected.correction_reason_key == kGraphicsCorrectedRenderer);

  // A usable renderer is kept untouched.
  CHECK_FALSE(reconciler.Reconcile(SettingId::kRenderer, *renderer, EnumV("d3d12"), caps).corrected);
  // ... and the unusable one cannot be selected (it is not in the row's own choice set).
  CHECK_FALSE(reconciler.Validate(SettingId::kRenderer, *renderer, EnumV("vulkan"), caps).ok);

  // Unknown hardware: nothing usable at all. The reconciler must not invent a backend; the
  // service falls back to the row's compiled default when reconciliation reports failure.
  CapabilitySnapshot none;
  const std::vector<Descriptor> none_descriptors = schema::BuildDescriptors(none);
  const Descriptor* none_renderer = Find(none_descriptors, SettingId::kRenderer);
  const Reconciliation no_renderer =
      reconciler.Reconcile(SettingId::kRenderer, *none_renderer, EnumV("vulkan"), none);
  CHECK_FALSE(no_renderer.corrected);  // nothing proved, nothing changed
  CHECK_TRUE(IsToken(no_renderer.effective, "vulkan"));
}

void TestReconcileAntiAliasing() {
  BeginCase("policy: AA is one enum and an unverified native path is not offered");

  const CapabilitySnapshot caps = Machine(1920, 1080);  // d3d12 with a verified label
  const std::vector<Descriptor> descriptors = schema::BuildDescriptors(caps);
  const Descriptor* aa = Find(descriptors, SettingId::kAntiAliasing);
  GraphicsReconciler reconciler;

  CHECK_TRUE(reconciler.Validate(SettingId::kAntiAliasing, *aa, EnumV("native"), caps).ok);
  const std::vector<std::string> all = AntiAliasingTokens(caps, "d3d12");
  CHECK_EQ(all.size(), 4u);
  CHECK_TRUE(all[0] == "none" && all[1] == "native" && all[2] == "fxaa" && all[3] == "fxaa_extreme");

  // A renderer with no verified sample semantics does not offer `native`; a stored native
  // request degrades deterministically to the other AA mechanism, and the UI is told why.
  CapabilityInputs inputs;
  inputs.display_enumeration_available = true;
  inputs.primary_display_size = {1920, 1080};
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  inputs.renderers.push_back(GoodRenderer("d3d12", true, /*native_aa=*/"", /*ceiling=*/3));
  const CapabilitySnapshot unverified = BuildCapabilitySnapshot(inputs);
  const std::vector<Descriptor> unverified_descriptors = schema::BuildDescriptors(unverified);
  const Descriptor* unverified_aa = Find(unverified_descriptors, SettingId::kAntiAliasing);
  const std::vector<std::string> tokens = AntiAliasingTokens(unverified, "d3d12");
  CHECK_EQ(tokens.size(), 3u);
  CHECK_TRUE(tokens[0] == "none" && tokens[1] == "fxaa" && tokens[2] == "fxaa_extreme");
  const Reconciliation degraded =
      reconciler.Reconcile(SettingId::kAntiAliasing, *unverified_aa, EnumV("native"), unverified);
  CHECK_TRUE(degraded.corrected);
  CHECK_TRUE(IsToken(degraded.effective, "fxaa"));
  CHECK_TRUE(degraded.correction_reason_key == kGraphicsCorrectedAntiAliasing);
  CHECK_FALSE(
      reconciler.Validate(SettingId::kAntiAliasing, *unverified_aa, EnumV("native"), unverified).ok);
  // FXAA needs no verified sample count, so it stays valid either way.
  CHECK_TRUE(
      reconciler.Validate(SettingId::kAntiAliasing, *unverified_aa, EnumV("fxaa"), unverified).ok);
}

void TestReconcileFiltering() {
  BeginCase("policy: filtering is capped by the device ceiling, never clamped by index");

  CapabilityInputs inputs;
  inputs.display_enumeration_available = true;
  inputs.primary_display_size = {1920, 1080};
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  // A device whose highest eligible step is 4x (value 3).
  inputs.renderers.push_back(GoodRenderer("d3d12", true, "2x MSAA", /*ceiling=*/3));
  const CapabilitySnapshot caps = BuildCapabilitySnapshot(inputs);
  const std::vector<Descriptor> descriptors = schema::BuildDescriptors(caps);
  const Descriptor* filtering = Find(descriptors, SettingId::kFiltering);
  GraphicsReconciler reconciler;

  const std::vector<std::string> tokens = FilteringTokens(caps, "d3d12");
  CHECK_EQ(tokens.size(), 5u);  // native, none, 1x, 2x, 4x
  CHECK_TRUE(tokens[0] == "native" && tokens[1] == "none" && tokens[2] == "1x" &&
             tokens[3] == "2x" && tokens[4] == "4x");

  const Reconciliation capped =
      reconciler.Reconcile(SettingId::kFiltering, *filtering, EnumV("16x"), caps);
  CHECK_TRUE(capped.corrected);
  CHECK_TRUE(IsToken(capped.effective, "4x"));  // the highest step the device proved
  CHECK_TRUE(capped.correction_reason_key == kGraphicsCorrectedFiltering);
  CHECK_FALSE(reconciler.Validate(SettingId::kFiltering, *filtering, EnumV("16x"), caps).ok);
  CHECK_TRUE(reconciler.Validate(SettingId::kFiltering, *filtering, EnumV("4x"), caps).ok);

  // A shorter request is never raised - only lowered to what the device can do.
  CHECK_FALSE(
      reconciler.Reconcile(SettingId::kFiltering, *filtering, EnumV("2x"), caps).corrected);

  // A device with no explicit-step information offers only native/none; a stored explicit
  // step falls back to native rather than to an arbitrary index.
  CapabilityInputs unknown;
  unknown.display_enumeration_available = true;
  unknown.primary_display_size = {1920, 1080};
  unknown.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  unknown.renderers.push_back(GoodRenderer("d3d12", true, "2x MSAA", /*ceiling=*/0));
  const CapabilitySnapshot bare = BuildCapabilitySnapshot(unknown);
  const std::vector<Descriptor> bare_descriptors = schema::BuildDescriptors(bare);
  const Descriptor* bare_filtering = Find(bare_descriptors, SettingId::kFiltering);
  const Reconciliation to_native =
      reconciler.Reconcile(SettingId::kFiltering, *bare_filtering, EnumV("8x"), bare);
  CHECK_TRUE(to_native.corrected);
  CHECK_TRUE(IsToken(to_native.effective, "native"));
}

void TestRendererChangeInsideEdit() {
  BeginCase("policy: a renderer change revalidates AA and filtering for the new backend");

  // d3d12 proved native AA and 16x filtering; the other backend proved neither.
  CapabilityInputs inputs;
  inputs.display_enumeration_available = true;
  inputs.primary_display_size = {1920, 1080};
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  inputs.renderers.push_back(GoodRenderer("d3d12", true, "2x MSAA", /*ceiling=*/5));
  inputs.renderers.push_back(GoodRenderer("vulkan", false, /*native_aa=*/"", /*ceiling=*/0));
  const CapabilitySnapshot caps = BuildCapabilitySnapshot(inputs);
  const std::vector<Descriptor> descriptors = schema::BuildDescriptors(caps);
  const Descriptor* aa = Find(descriptors, SettingId::kAntiAliasing);
  const Descriptor* filtering = Find(descriptors, SettingId::kFiltering);
  GraphicsReconciler reconciler;

  // An acceptance that was valid for the *old* renderer is refused for the new one, rather
  // than being clamped by index (contracts §6.3).
  CHECK_TRUE(reconciler
                 .ValidateForRenderer(SettingId::kAntiAliasing, *aa, EnumV("native"), caps, "d3d12")
                 .ok);
  CHECK_FALSE(reconciler
                  .ValidateForRenderer(SettingId::kAntiAliasing, *aa, EnumV("native"), caps,
                                       "vulkan")
                  .ok);
  CHECK_TRUE(reconciler
                 .ValidateForRenderer(SettingId::kFiltering, *filtering, EnumV("16x"), caps, "d3d12")
                 .ok);
  CHECK_FALSE(reconciler
                  .ValidateForRenderer(SettingId::kFiltering, *filtering, EnumV("16x"), caps,
                                       "vulkan")
                  .ok);
  // Resolution is backend-independent and stays valid for both.
  const Descriptor* resolution = Find(descriptors, SettingId::kResolution);
  CHECK_TRUE(reconciler
                 .ValidateForRenderer(SettingId::kResolution, *resolution, ResolutionV(1920, 1080),
                                      caps, "vulkan")
                 .ok);
}

// --- the render path's guest-size plan -------------------------------------------------------

void TestGuestScreenPlan() {
  BeginCase("policy: a render size plans back to (guest size, draw scale) exactly");
  const auto production = PlanGuestScreen(TitleRenderPath(), {1920, 1080});
  CHECK_TRUE(production.supported);
  CHECK_TRUE(IsSize(production.guest_size, 960, 540));
  CHECK_EQ(production.draw_scale, 2);

  RenderPathLimits limits;
  limits.base_sizes = {{1280, 720}, {1920, 1080}};
  limits.scales = {1, 2, 3};

  // The title's own size, the exact 1080p, and the two integer multiples. 3840x2160 plans as
  // 1080p x 2 rather than 720p x 3: the larger guest layout is the clearer one and both are
  // exact.
  const GuestScreenPlan native = PlanGuestScreen(limits, {1280, 720});
  CHECK_TRUE(native.supported);
  CHECK_TRUE(IsSize(native.guest_size, 1280, 720));
  CHECK_EQ(native.draw_scale, 1);

  const GuestScreenPlan exact_1080 = PlanGuestScreen(limits, {1920, 1080});
  CHECK_TRUE(exact_1080.supported);
  CHECK_TRUE(IsSize(exact_1080.guest_size, 1920, 1080));
  CHECK_EQ(exact_1080.draw_scale, 1);

  const GuestScreenPlan qhd = PlanGuestScreen(limits, {2560, 1440});
  CHECK_TRUE(qhd.supported);
  CHECK_TRUE(IsSize(qhd.guest_size, 1280, 720));
  CHECK_EQ(qhd.draw_scale, 2);

  const GuestScreenPlan uhd = PlanGuestScreen(limits, {3840, 2160});
  CHECK_TRUE(uhd.supported);
  CHECK_TRUE(IsSize(uhd.guest_size, 1920, 1080));
  CHECK_EQ(uhd.draw_scale, 2);

  // A size the render path cannot produce is not planned; the caller must not apply it.
  CHECK_FALSE(PlanGuestScreen(limits, {1600, 900}).supported);
  CHECK_TRUE(PlanGuestScreen(limits, {2880, 1620}).supported);  // 960x540 at 3x

  // Every offered size plans back to exactly itself - the two directions agree.
  for (const DisplayMode& size : RenderPathSizes(limits)) {
    const GuestScreenPlan plan = PlanGuestScreen(limits, size);
    CHECK_TRUE(plan.supported);
    CHECK_TRUE(plan.guest_size.width * plan.draw_scale == size.width);
    CHECK_TRUE(plan.guest_size.height * plan.draw_scale == size.height);
  }

  // Without the 1080 guest height (Variant B not compiled) the only 1080p available would be
  // the half-resolution experiment, which is not offered - so 1080p does not plan at all.
  RenderPathLimits variant_a_only;
  variant_a_only.base_sizes = {{1280, 720}};
  variant_a_only.scales = {1, 2};
  CHECK_TRUE(PlanGuestScreen(variant_a_only, {1920, 1080}).supported);  // bounded 540p layout
  CHECK_TRUE(PlanGuestScreen(variant_a_only, {2560, 1440}).supported);
}

// --- first run and recovery -----------------------------------------------------------------

void TestFirstRun() {
  BeginCase("policy: Auto follows the display at boot, without a benchmark");

  auto measured_1080 = Machine(1920, 1080);
  measured_1080.verified_sustainable_resolution = {1920, 1080};
  const GraphicsRecommendation on_1080 = RecommendGraphics(measured_1080);
  CHECK_TRUE(on_1080.applicable);
  CHECK_FALSE(on_1080.conservative);
  CHECK_TRUE(IsAutoResolution(on_1080.resolution));
  CHECK_TRUE(IsToken(on_1080.renderer, "d3d12"));
  CHECK_TRUE(IsToken(on_1080.anti_aliasing, "native"));  // the device verified it
  CHECK_TRUE(IsToken(on_1080.filtering, "native"));      // never imposed on a first run
  CHECK_TRUE(on_1080.reason_key == "settings.graphics.automatic_display");

  auto measured_4k = Machine(3840, 2160);
  CHECK_TRUE(IsAutoResolution(RecommendGraphics(measured_4k).resolution));
  measured_4k.verified_sustainable_resolution = {3840, 2160};
  const GraphicsRecommendation on_4k = RecommendGraphics(measured_4k);
  CHECK_TRUE(IsAutoResolution(on_4k.resolution));
  GraphicsReconciler policy;
  const auto* row = schema::FindRow(SettingId::kResolution);
  CHECK_TRUE(IsRes(policy.Reconcile(SettingId::kResolution, row->descriptor,
      on_1080.resolution, measured_1080).effective, 1920, 1080));
  CHECK_TRUE(IsRes(policy.Reconcile(SettingId::kResolution, row->descriptor,
      on_4k.resolution, measured_4k).effective, 3840, 2160));
  CHECK_TRUE(IsAutoResolution(ResolutionPreferenceForBoot(measured_1080, ResolutionV(3840, 2160))));
  CHECK_TRUE(IsRes(ResolutionPreferenceForBoot(measured_4k, ResolutionV(1920, 1080)), 1920, 1080));
  CHECK_TRUE(IsRes(ResolutionPreferenceForBoot(CapabilitySnapshot{}, ResolutionV(3840, 2160)), 3840, 2160));

  // Unknown hardware: the bounded conservative route, and it says so.
  const GraphicsRecommendation unknown = RecommendGraphics(CapabilitySnapshot{});
  CHECK_TRUE(unknown.applicable);
  CHECK_TRUE(unknown.conservative);
  CHECK_TRUE(IsAutoResolution(unknown.resolution));
  CHECK_TRUE(IsRes(policy.Reconcile(SettingId::kResolution, row->descriptor,
      unknown.resolution, CapabilitySnapshot{}).effective, 1280, 720));
  CHECK_TRUE(IsToken(unknown.anti_aliasing, "none"));
  CHECK_TRUE(IsToken(unknown.filtering, "native"));
  CHECK_TRUE(unknown.reason_key == "settings.graphics.automatic_display");

  // A backend with no verified label gets no AA, not a guessed one.
  CapabilityInputs unverified;
  unverified.display_enumeration_available = true;
  unverified.primary_display_size = {1920, 1080};
  unverified.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  unverified.renderers.push_back(GoodRenderer("d3d12", true, /*native_aa=*/""));
  CHECK_TRUE(IsToken(RecommendGraphics(BuildCapabilitySnapshot(unverified)).anti_aliasing, "none"));
}

void TestRecovery() {
  BeginCase("policy: only a boot that attempted and never presented triggers the safe route");

  const CapabilitySnapshot caps = Machine(3840, 2160);

  GraphicsBootMarker never;
  CHECK_FALSE(GraphicsFallbackWarranted(never));  // first run is not a graphics failure
  GraphicsBootMarker clean;
  clean.attempted = true;
  clean.presented = true;
  CHECK_FALSE(GraphicsFallbackWarranted(clean));
  GraphicsBootMarker failed;
  failed.attempted = true;
  failed.presented = false;
  CHECK_TRUE(GraphicsFallbackWarranted(failed));

  // No failure recorded: the request comes back untouched (no boot-time re-optimizing).
  const GraphicsFallback intact = RecoverGraphics(caps, ResolutionV(3840, 2160), "d3d12", false);
  CHECK_FALSE(intact.needed);
  CHECK_TRUE(IsRes(intact.resolution, 3840, 2160));

  // A failure recorded: the floor - the title's own 720p, the active backend, no AA, native
  // filtering.
  const GraphicsFallback safe = RecoverGraphics(caps, ResolutionV(3840, 2160), "d3d12", true);
  CHECK_TRUE(safe.needed);
  CHECK_TRUE(IsRes(safe.resolution, 1280, 720));
  CHECK_TRUE(IsToken(safe.renderer, "d3d12"));
  CHECK_TRUE(IsToken(safe.anti_aliasing, "none"));
  CHECK_TRUE(IsToken(safe.filtering, "native"));
  CHECK_TRUE(safe.reason_key == "settings.graphics.recovered_from_failure");

  // Recovery also repairs a renderer that vanished while the failure was being recorded.
  const GraphicsFallback repaired =
      RecoverGraphics(Machine(1920, 1080), ResolutionV(1920, 1080), "vulkan", true);
  CHECK_TRUE(IsToken(repaired.renderer, "d3d12"));
}

// --- the service integration ----------------------------------------------------------------

void TestServiceIntegration() {
  BeginCase("policy: the service uses the reconciler - effective corrected, requested retained");

  std::error_code code;
  const fs::path base = fs::temp_directory_path(code) / "rb_blitz-settings-graphics";
  fs::remove_all(base, code);
  fs::create_directories(base, code);

  const fs::path settings_file = base / "settings.toml";
  {
    std::ofstream out(settings_file, std::ios::binary);
    out << "schema_version = 1\n\n[graphics]\nresolution = [3840, 2160]\n"
           "renderer = 'd3d12'\nanti_aliasing = 'native'\nfiltering = '16x'\n";
  }

  StorePaths paths;
  paths.settings_file = settings_file;
  paths.paths_file = base / "paths.toml";
  paths.legacy_profile = base / "launcher.toml";

  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  CHECK_EQ(static_cast<uint64_t>(service.load_status()), static_cast<uint64_t>(DocumentStatus::kOk));

  // The machine this boot runs on: a 1080p monitor and a device that proved 4x filtering.
  CapabilityInputs inputs;
  inputs.display_enumeration_available = true;
  inputs.primary_display_size = {1920, 1080};
  inputs.display_modes = {{1920, 1080}};
  inputs.render_path.base_sizes = {{1280, 720}, {1920, 1080}};
  inputs.renderers.push_back(GoodRenderer("d3d12", true, "2x MSAA", /*ceiling=*/3));
  service.SetCapabilities(BuildCapabilitySnapshot(inputs));

  GraphicsReconciler reconciler;
  service.SetReconciler(&reconciler);

  // Requested keeps the user's 4K and 16x; effective is what this boot can actually apply.
  CHECK_TRUE(IsRes(service.RequestedValue(SettingId::kResolution), 3840, 2160));
  CHECK_TRUE(IsRes(service.EffectiveValue(SettingId::kResolution), 1920, 1080));
  CHECK_TRUE(IsToken(service.EffectiveValue(SettingId::kFiltering), "4x"));

  const Snapshot snapshot = service.GetSnapshot();
  const Descriptor* resolution = Find(snapshot.descriptors, SettingId::kResolution);
  CHECK_TRUE(resolution != nullptr);
  CHECK_EQ(resolution->choices.size(), 3u);  // Auto plus 1920x1080 and 1280x720

  // An unsupported size cannot be selected, and neither can a filtering step the device
  // never proved - both are refused at Set, not silently accepted and clamped later.
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(txn != 0);
  CHECK_FALSE(service
                  .Set(txn, SettingId::kResolution, ResolutionV(3840, 2160),
                       service.capability_revision())
                  .ok);
  CHECK_FALSE(service
                  .Set(txn, SettingId::kFiltering, EnumV("16x"), service.capability_revision())
                  .ok);
  CHECK_TRUE(service
                 .Set(txn, SettingId::kResolution, ResolutionV(1920, 1080),
                      service.capability_revision())
                 .ok);
  service.Cancel(txn);

  // A stale capability revision refuses the whole edit rather than clamping by index.
  const uint64_t stale_txn = service.BeginEdit(service.revision());
  CHECK_FALSE(service
                  .Set(stale_txn, SettingId::kResolution, ResolutionV(1920, 1080),
                       service.capability_revision() + 1)
                  .ok);
  service.Cancel(stale_txn);

  fs::remove_all(base, code);
}

}  // namespace

void TestGraphicsBootJournal() {
  BeginCase("graphics startup recovers failed attempts, preserves intent, and excludes live processes");
  const auto directory = std::filesystem::temp_directory_path() / "rb-blitz-graphics-boot-test";
  std::filesystem::create_directories(directory);
  const auto file = directory / "settings.toml";
  std::error_code ec;
  std::filesystem::remove(directory / "settings.toml.graphics-boot.toml", ec);
  const GraphicsValues safe = {{SettingId::kRenderer, EnumV("d3d12")},
      {SettingId::kResolution, ResolutionV(1280, 720)},
      {SettingId::kAntiAliasing, EnumV("none")}, {SettingId::kFiltering, EnumV("native")}};
  auto requested = safe;
  requested[SettingId::kResolution] = ResolutionV(1920, 1080);
  std::string error;
  {
    GraphicsBootState first;
    CHECK_TRUE(first.Open(file, &error));
    CHECK_TRUE(IsRes(first.Select(safe, safe, false).at(SettingId::kResolution), 1280, 720));
    CHECK_TRUE(first.Begin(safe, &error));
    GraphicsBootState concurrent;
    CHECK_FALSE(concurrent.Open(file, &error));
    CHECK_TRUE(first.Presented(&error));
  }
  {
    GraphicsBootState failed;
    CHECK_TRUE(failed.Open(file, &error));
    CHECK_TRUE(IsRes(failed.Select(requested, safe, false).at(SettingId::kResolution), 1920, 1080));
    CHECK_FALSE(failed.recovered());
    CHECK_TRUE(failed.Begin(requested, &error));  // simulate process death before a menu frame
  }
  {
    GraphicsBootState recovery;
    CHECK_TRUE(recovery.Open(file, &error));
    const auto effective = recovery.Select(requested, safe, false);
    CHECK_TRUE(recovery.recovered());
    CHECK_TRUE(IsRes(effective.at(SettingId::kResolution), 1280, 720));
    CHECK_TRUE(recovery.Begin(effective, &error));
    CHECK_TRUE(recovery.Presented(&error));
  }
  {
    GraphicsBootState next;
    CHECK_TRUE(next.Open(file, &error));
    CHECK_TRUE(IsRes(next.Select(requested, safe, false).at(SettingId::kResolution), 1280, 720));
    CHECK_TRUE(next.recovered());
    requested[SettingId::kResolution] = ResolutionV(2560, 1440);
    CHECK_TRUE(IsRes(next.Select(requested, safe, false).at(SettingId::kResolution), 2560, 1440));
    CHECK_FALSE(next.recovered());
    CHECK_TRUE(next.Begin(requested, &error));
    CHECK_TRUE(next.Closed(&error)); // an intentional close during loading is not a GPU failure
  }
  {
    GraphicsBootState clean;
    CHECK_TRUE(clean.Open(file, &error));
    CHECK_TRUE(IsRes(clean.Select(requested, safe, false).at(SettingId::kResolution), 2560, 1440));
    CHECK_FALSE(clean.recovered());
  }
  CHECK_FALSE(std::filesystem::exists(file)); // requested preferences were never rewritten
  std::filesystem::remove_all(directory, ec);
}

int main() {
  TestRenderPathSizes();
  TestSupportedResolutions();
  TestBuildSnapshot();
  TestLogicalProcessors();
  TestDisplayReconnection();
  TestReconcileResolution();
  TestReconcileRenderer();
  TestReconcileAntiAliasing();
  TestReconcileFiltering();
  TestRendererChangeInsideEdit();
  TestGuestScreenPlan();
  TestFirstRun();
  TestRecovery();
  TestGraphicsBootJournal();
  TestServiceIntegration();
  return Finish();
}
