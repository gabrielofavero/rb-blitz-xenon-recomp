# Portrait gameplay rendering and recovery dialog

Status: implementation delivered, 2026-11-26. Signed-device acceptance remains unverified.

## Scope

Extend compact portrait rendering from authored Interface screens to gameplay,
preserving the previous visible framing, HUD, touch, pause and mixed-profile
transitions. Make the existing graphics-recovery dialog readable and dismissible
on portrait touch devices. The previous [interface memory delivery](2026-11-26-interface-render-memory.md)
records the range-backed resolve storage and original Interface-only scope.

| Step | Deliverable | Acceptance | State |
| --- | --- | --- | --- |
| G1 | Recovery dialog layout and touch dismissal | Portrait host modal, native Confirm dispatch and production ImGui click/layout tests | Replaced with portrait overlay; host checks pass |
| G2 | Compact portrait gameplay targets | No unused horizontal crop in the main target; matching projection, native pointer/HUD matrices and context transitions | Implemented; host checks pass |
| G3 | Internal attachment/storage audit | Preserve EDRAM aliases and necessary effects; distinguish retained emulation storage from invisible side-band drawing | Audited; retained data preserved |
| G4 | Delivery | Focused tests, current macOS/iOS builds and existing IPA filenames; explicit signed-device checks | Complete; signed-device checks unverified |

## Boundaries

Keep the guest popup lifecycle and native Confirm handler. Portrait error dialogs
use a host overlay; landscape keeps the native scene and controls. Preserve the
existing display, layout and renderer work. Rendering changes are
title-specific and shared across supported backends unless SDK inspection proves
a separate responsibility. Define patch ownership/platform scope before any SDK
change and verify exact full-stack replay if one is required.

Narrowing the main scene must preserve required shadow/post-effect passes and
guest-visible EDRAM contents. Merely clipping the presenter is insufficient.
Reusing a partial buffer as though it contained retained guest data is incorrect.

Native validation available here is macOS/iOS arm64. Windows D3D12/Vulkan, Linux,
Intel macOS and other architectures require explicit untested status.

## Baseline and progress

- The recovery screenshot shows a narrow, unreadable body and a tiny Continue
  hint over the main menu. The actual route and input provider require inspection.
- Compact targets currently exclude gameplay. The native camera correction and
  first-frame refresh already exist and must be retained.
- Documentation baseline and connected-device logs are recorded under the
  existing `out/` root with `portrait-gameplay-` log filenames. Build and package
  directories remain unchanged; retain one current IPA per package mode.


## G1 Recovery dialog

The installed iPhone log `out/portrait-gameplay-device-before.log` identifies
`error_popup_panel` and repeated touch misses with zero captured targets. Its
camera already uses a compact 331x720 target, confirming that the screenshot
comes from the preceding Interface implementation rather than the old full-width
source. The generic error scene previously shrank its whole landscape panel,
including the body and Continue label.

`PreparePortraitErrorPopup` now reuses existing guest portrait dialog surfaces.
The body wraps in its own measured strip, the graphics-recovery text is shorter,
and a single large Continue surface sends the original native Confirm action.
The action is 40 authored units high and the panel is 216 units high. Unused
close art is removed. Native error confirmation still restores the original
background panel and consumes the recovery notice once.

`DialogTargetPublisher` synchronizes modal owner, directory, input scope, polling
sequence and drawn button bounds. A draw thread can have local frame zero while
polling is at frame 842; capture now uses the published polling sequence. Advancing
that sequence preserves the preceding draw for hit testing, while ownership
changes, stale/out-of-order draws and malformed bounds are rejected. A narrow
Confirm fallback applies only to the native error panel's plain Continue label.
No host overlay or alternate popup route is introduced.

## G2 Compact gameplay and culling

Compact mode follows visual orientation independently of the quality-profile
selection. Portrait gameplay and pause contexts retaining Gameplay quality use
narrow main targets; mixed Interface/Gameplay choices retain their selected
quality tier. Target dimensions, resolve textures and tile rectangles commit
before drawing, and the already-current camera refreshes after publication.
The presenter fits the actual compact frontbuffer instead of cropping it again.

Native camera projection and inverse preserve vertical framing while narrowing
horizontal coverage. The two local horizontal culling planes at +544/+560 are
transformed and normalized including distance, preserving perspective and
orthographic sphere-radius tests. Native `821FF8E8` rebuilds world culling planes
and pointer matrices. Custom-target cameras and partial viewports retain their
independent projections. Required effects and intersecting object bounds can
still generate work; no whole-scene occlusion guarantee is claimed.

Lane power-up bounds and pointer events now use the actual frontbuffer aspect
when source-aspect presentation is enabled. HUD width continues to use the same
visible portrait dimensions. The previous scissor diagnostic
`portrait_render_cutoff` remains only for a cropped fallback before compact
publication or after replacement allocation refusal; it does not disable compact
rendering or provide the former wide-frame comparison.

## G3 Attachment and storage audit

`RenderTargetCache::GetRenderTargetHeight` covers a full console EDRAM address
period. Render-target identities have pitch/base/format/MSAA but no independently
versioned physical height. Later ownership transfers, clears and resolves can
read retained tiles outside the current viewport. A screen-height clamp could
therefore corrupt guest-visible data.

Safe growth would require physical-extent versions, owned-tile preservation,
height-aware framebuffer keys/render areas and fence retirement across both
D3D12 and Vulkan. There is no blanket EDRAM initialization clear to remove. The
existing 10 MiB times scale-area buffer is populated by real guest operations;
historical resolved storage also survives while its validity survives. No SDK
patch or unsafe discard/clamp was added. The delivered optimization removes the
cropped horizontal primary frame, not necessary effects, tile padding or retained
console graphics data. Total resident memory and frame-time savings are unmeasured.

## G4 Validation and package

- macOS arm64 Release game and focused fixtures build:
  `cmake --build --preset mac-arm64-release --target rb_blitz`, plus the relevant
  test targets. Logs: `out/portrait-gameplay-mac-build.log` and
  `out/portrait-gameplay-record-build.log`.
- iOS arm64 bundled and importer Release targets build with the existing presets:
  `out/portrait-gameplay-ios-build.log` and
  `out/portrait-gameplay-ios-importer-build.log`.
- Nine focused CTest suites pass: camera, portrait layout, HUD layout and runtime,
  touch HUD, native mouse, menu payload, touch gestures and settings graphics.
  Log: `out/portrait-gameplay-tests.log`.
- `portrait_camera` passes 816 checks including mixed contexts, old-crop camera
  equivalence, actual-frontbuffer pointer mapping, perspective/orthographic
  culling, sphere margins and cached-camera/first-frame lifecycle.
- `native_mouse` passes 1,080 checks including real polling/drawing threads,
  published frame 842 versus draw-thread frame zero, concurrent snapshots and
  stale/context-replaced button rejection. The fixture links `Threads::Threads`
  for portable pthread requirements.
- The actual record command is
  `rb_blitz_menu_payload_tests check-portrait-records out/render-scale-source out/render-scale-payload`.
  All 2,342 checks pass (`out/portrait-gameplay-record-tests.log`). It validates
  the real generic error scene/body/button geometry over phone/tablet aspects.
- Thirty-five pre-existing record assertions assumed fixed offsets/empty parents.
  A temporary baseline using the previous generic prompt route reproduced exactly
  those failures (`out/portrait-gameplay-baseline-record-tests.log`). Assertions
  now parse variable-length parent/member fields and enforce the fitted root or
  native animation-parent contract. Unchanged scenes must equal precisely the
  common canvas adaptation, not arbitrary differences. No production layout was
  weakened to satisfy those assertions.

Existing unsigned package filenames under `out/ios/packages/` are overwritten
only after staging/audits succeed. Both package audits and executable text are
checked against the final device builds in `out/portrait-gameplay-delivery.json`.
No feature-specific package directories or IPA copies are created.

## Remaining device checks

The connected iPhone is available and UI testing is authorized. Installing the
new IPA still requires Sideloadly because no signing identity is available here.
The installed earlier binary supplied diagnosis but cannot validate the changes.
After installing the replacement, check dialog readability and Continue touch,
portrait song entry, effects, lane power-up touch, pause/settings/confirmations,
mixed orientations, return to menus and background/resume. Check effective render
sizes and camera activation logs separately from memory and frame-time captures.

These changes are shared title code for D3D12/Vulkan/MoltenVK; no SDK patch was
needed. Native macOS/iOS arm64 builds and host fixtures are validated. Windows,
Linux, Intel macOS, other architectures and actual gameplay/effect/touch behavior
on the signed iPhone build remain unverified.


## Final evidence

Both current unsigned IPAs were packaged with the established filenames. Audit
SHA-256 hashes and Mach-O executable text match the final bundled/importer builds;
`out/portrait-gameplay-delivery.json` records exact artifact identities. The
importer was rebuilt again after shared pause-scene sources changed in the
working tree, preserving that additional work. Only the current mode artifact
is replaced; no IPA history copies were created.

Final checks: all nine focused CTest suites and 2,342 actual portrait-record
checks pass; `git diff --check` passes. Documentation reports 0 errors and the
same 49 pre-existing warnings (`out/portrait-gameplay-docs.log`). The toolchain
remains Xcode 26.0.1/AppleClang 17 with the existing non-strict CMake/Ninja version
warnings. No Windows build was attempted on this Mac.

Implementation and available automated gates are complete. Physical-device
readability, touch dismissal, gameplay/effects, transitions and memory/frame-time
measurements remain open until the replacement IPA is signed and installed.

## Portrait dialog replacement

The native recovery-scene rewrite and subsequent frame-scaling experiments are
retired. Restoring the general menu fitting path removes their background framing
change. A portrait-only modal now uses achievement-style fonts and glowing rails,
copied localized UILabel text, and one generation-tagged Continue intent. The
guest poll executes its original Confirm commands; host exclusive input does not
gate that dispatch. Landscape and portrait retain the same native error scene.

The production ImGui overlay passes offscreen click and safe-area checks at four
phone/tablet sizes, long-message scrolling, and landscape input exclusion. Four
focused CTest suites and 2,274 real-record checks pass. Logs are
`out/portrait-dialog-tests.log` and `out/portrait-dialog-record-tests.log`.
The native panel draw suppression test verifies unchanged guest memory and zero
transform-dirty calls. macOS and iOS arm64 builds pass; signed-device acceptance
remains open. See [the current dialog route](../../engine/input-flow.md#portrait-recovery-dialogs-2026-11-26).
