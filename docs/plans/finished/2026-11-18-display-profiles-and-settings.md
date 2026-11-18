# Display profiles and settings

Date: 2026-11-18. Status: finished; owner UI acceptance unverified.

## Deliverables

- D1: Restore editable window mode and native window resizing.
- D2: Replace aspect/scale rows with desktop resolution profiles and mobile scale wheels,
  independently persisted for interface and gameplay. Native is the first-run default;
  desktop choices descend to 720p and mobile scales range from 0.5 to 1.0.
- D3: Switch render resources at scene boundaries and apply profile edits without process restart.
- D4: Support pointer/touch selection and mouse wheel adjustment of guest sliders.
- D5: Default scanning to the supported processor maximum and song timeout to 60 seconds;
  move labels to Menus & Prompts and display Small / Normal / Big with Normal as default.

## Validation

Release build, focused settings/input/payload tests, and documentation checker are required.
Game navigation and visual acceptance remain owner checks. Windows is available locally;
Linux, macOS, iOS, Android, Vulkan and MoltenVK runtime behavior is unverified.

## Evidence

- 2026-11-18 [static]: the previous `src/settings/boot.cpp` forced `window_resizable=false` and
  classified non-default SDK layers as external even when they came from boot configuration.
- 2026-11-18 [static]: SDK GPU scale settings are restart-scoped integers. Guest framebuffer
  creation is in `sub_823F3698`; changing size fields alone does not rebuild its targets.


## Implementation record

- D1 (2026-11-18, implemented): `src/settings/boot.cpp` hands Window Mode to the
  existing guest Keep/Revert preview unless the SDK locks it. Native windows are
  resizable, including recovery from older file-level fixed-window preferences.
- D2 (2026-11-18, implemented): `src/settings/resolution_profiles.h` owns native,
  fitted 16:9 and descending desktop offers; schema/model/store rows persist the
  two contexts independently. Mobile has six scale positions, 0.5 through 1.0,
  hosted by copies of the guest audio scene. Aspect Ratio and legacy Render Scale
  remain readable for compatibility but are hidden from the settings pages.
- D3 (2026-11-18, implemented, static): `sub_823F3C40` chooses the context before
  drawing and transactionally rebuilds guest color/depth and resolve resources.
  Failed allocation retains the previous frame. Active edits are acknowledged
  only after their frame accepts them; inactive context edits remain queued.
  Recovered and forced-safe processes retain the conservative legacy frame and
  require restart for profile edits; the first draw cannot undo recovery.
  SDK patch 0094 makes source-frame aspect optional in D3D12 and Vulkan, default
  off for other consumers. It has a distinct responsibility from older boot-size
  patches and removes no API. Tile boundaries retain 32-pixel alignment. The
  backend multiplier stays fixed: dimensions not divisible by it round to the
  nearest guest pixel, so exact arbitrary host-pixel sizes remain unproven.
- D4 (2026-11-18, implemented, static): native slider geometry feeds pointer hit
  testing; taps/clicks choose one of six stops via acknowledged guest pad pulses.
  Wheel up/down send increase/decrease. Existing audio sliders and mobile scale
  wheels use this route. No additional host overlay is introduced.
- D5 (2026-11-18, implemented): scan threads default to the supported processor
  maximum, capped at 32. Song Load Timeout defaults to 60s. Settings Labels moves
  to Menus & Prompts with Small / Normal / Big; the middle size is the default.
  Stored size tokens retain their previous font sizes.

## Owner acceptance

The owner elected to test the game UI on 2026-11-18. No game navigation was
performed for this delivery. Check native window resizing and Keep/Revert, both
profile values across a restart, menu/gameplay/pause transitions with different
sizes, and click/touch/scroll behavior for both audio controls and mobile scale.
Visual layout, allocation failure recovery, long sessions, unusual display
shapes, and native mobile transitions remain unverified. Windows AMD64 is the
available compile target; Linux, macOS, iOS, Android and Vulkan/MoltenVK runtime
acceptance is unproven.


## Final validation (2026-11-18)

- [build] `cmake --build --preset win-amd64-release` passed with the latest
  recovery guard (`out/display-settings-build-final.log`). Local CMake cache
  was refreshed to use the pinned LLVM 20 compiler/resource tools after the
  installed CMake resource wrapper rejected the older cached RC selection.
  No tracked toolchain defaults changed.
- [host tests] 11/11 focused CTest registrations passed: display mode, native
  mouse, keyboard controller driver, settings screen, menu payload, settings
  store/service/restore payload/graphics/hot apply/bridge service. Evidence:
  `out/display-settings-focused-final.log`. After the recovery guard, the three
  affected settings service/graphics/bridge registrations passed again in
  `out/display-settings-recovery-final.log`. Payload tests used the registered
  Windows crash-dialog suppression runner.
- [host tests] The wider 125-test run initially found outdated expectations for
  retired settings rows, label defaults and native wheel pages. These checks
  were updated and passed in the focused rerun. The virtual controller harness
  now uses shared pads so a physical controller cannot displace its test input.
  The unchanged `path_policy` test still fails its nonexistent descendant case
  at `tests/path_policy_tests.cpp:88`; the implementation and test were not
  changed here. Evidence: `out/display-settings-all-tests.log` and
  `out/display-settings-retests.log`. This unrelated full-suite failure remains
  open; investigate filesystem canonicalization separately.
- [patch replay] All 66 Windows-compatible production SDK patches replayed with
  zero files needing updates; unrelated SDK untracked dependencies were retained.
  Evidence: `out/display-settings-sdk-audit.log`. Patch 0094 affects the shared
  Xenos setting plus D3D12/Vulkan presentation; only Windows AMD64 was compiled.
- [docs] Documentation checker passed with zero errors; pre-existing warnings
  remain (`out/display-settings-docs.log`). `git diff --check` passed. The
  graphics, display, menu, toggle, status and known-issues records describe the
  new route and its explicit runtime limits.

D1-D5 implementation and the available scoped build/test gates are complete.
No visual, game navigation or device-touch result is inferred from these checks.
