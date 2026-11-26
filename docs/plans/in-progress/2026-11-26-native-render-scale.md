# Native render scale and pointer controls

Status: implementation in progress, 2026-11-26.

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

## Validation

- Required Windows release build attempted; unavailable because this macOS checkout has no Windows build directory.
- Native Apple Silicon Release build and focused host tests: pending.
- Game navigation, pointer/touch behavior, physical-device performance and high-resolution allocation: unverified. No game UI was driven.

Evidence will be recorded under `out/render-scale-*` and the native build tree's CTest logs.
