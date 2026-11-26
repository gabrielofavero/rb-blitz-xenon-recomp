# Native render scale and pointer controls

Status: delivered, 2026-11-26; physical iOS testing remains unverified.

## Requirements and decisions

- D1: Mobile Interface and Gameplay use Audio-style buttons and one bottom native wheel.
- D2: 1.0 means physical device native pixel density in the selected orientation. The Xbox 360 720p source is not the native endpoint. Portrait uses the expanded landscape source needed for a native-density full-height crop.
- D3: Both contexts use ten calculated quality stops, bounded by a 720-pixel source height and device native height. A display at or below 720 has one native stop.
- D4: First boot probes capabilities and saves conservative recommendations before graphics allocation. A per-context pending-attempt journal recovers both profiles after a crash or forced termination, including after a resolution has already presented. Unknown performance starts at 720p. Display size alone is not performance evidence.
- D5: Each platform chooses its backend multiplier from both selected profiles. Changes within that capacity apply live; capacity growth remains pending restart. This avoids reserving native-sized backing for a conservative first boot. Existing failed-boot recovery remains active.

## Scope and implementation

Shared project policy covers Windows, Linux, macOS, iOS and mobile preview, with existing D3D12/Vulkan routes retained. Vulkan allocation admission belongs to existing SDK patch 0097; other SDK behavior is retained. Existing explicit requests remain saved; legacy tenths reconcile to the calculated ladder. New lower choices store exact pixel heights; token `10` retains native intent across devices and orientations.

Implementation: `src/settings/resolution_profiles.h`, schema/service/boot, authored settings panel/scene, render-profile hook and native mouse slider handling. The draw hook captures UISlider hit geometry at `0x824DC1E8`; `0x824DC2F0` is traversal rather than actual drawing.

## Recovery lifecycle

The journal records an active allocation before setup/drawing, requires 120 distinct
refreshes at that context and retains the marker until clean shutdown. A later
hang remains recoverable. Initialization refusals and early quits keep unconfirmed
markers. An accepted settings restart clears the stopped old frame's trial so a
new saved capacity can be attempted. Unchanged running geometry is not re-armed
just because a higher value was saved for a later restart. Shutdown retires the
journal before any final guest draws can write new markers.

## Validation (2026-11-26)

- macOS ARM64 and iOS ARM64 Release builds pass, including final recovery changes.
- All 16 focused CTest suites pass, including native Vulkan allocation, registry feed, settings, mouse and profile recovery.
- The actual native Audio scene passes 90 focused checks across landscape, phone and tablet aspects.
- Eight iOS integration units also passed device syntax checks before the full device build.
- All 73 SDK patches replay; the six files owned by 0097 match replay exactly. The official live-tree audit retains unrelated `src/ui/prompt.cpp` drift.
- Documentation checker: zero errors, 49 existing warnings.
- Windows Release build unavailable on this Mac; Windows/Linux runtime acceptance unverified.
- An exploratory broad portrait-fixture run reported 35/2090 failures before the final rebuild; it is not current UI acceptance. Focused scale-scene checks pass independently.

Evidence: `out/render-scale-build-current.log`, `out/render-scale-final-recovery-build.log`, `out/render-scale-ios-build-current.log`, `out/render-scale-tests-final.log`, `out/render-scale-scene.log`, `out/render-scale-sdk-owned-replay.log`, `out/render-scale-docs.log`.

## Device handoff

Audited unsigned bundled IPA: `out/ios/packages/render-scale-20261126/RockBandBlitz-0.1.0-arm64-ios.ipa`; its audit records iOS 17.0, arm64 and unsigned status. Device signing and testing remain manual. No game UI was driven.

Check independent Interface/Gameplay values, tap/drag, native labels, pending capacity changes and recovery after an interrupted higher-quality attempt. Native is a real pixel target; device allocation limits can refuse it. Full live backend-capacity growth remains unsupported and requires a renderer lifecycle redesign.
