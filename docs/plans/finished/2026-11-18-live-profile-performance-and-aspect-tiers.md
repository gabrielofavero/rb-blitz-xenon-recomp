# Live profile performance and native aspect tiers

Date: 2026-11-18. Status: finished implementation; owner performance confirmation pending.

## Deliverables

- D1: Correct sustained GPU stalls after live interface resolution transitions.
- D2: Offer lower native-aspect desktop resolutions alongside 16:9 equivalents,
  keeping native first, precise dimension labels and the 720-pixel height floor.
- D3: Build, run scoped host checks and document runtime limits. Game navigation
  and visual/performance acceptance remain owner checks.

## Evidence

- 2026-11-18 [owner log]: `out/build/win-amd64-release/logs/rb_blitz_004.log`
  records one successful rebuild for each selected profile, not a repeated
  allocation loop. Before edits the menu averaged approximately 59.8 FPS.
  After selecting 720p, submission fence waits became sustained; restoring
  native left approximately 3 FPS through shutdown. The late native interval
  has 114 submission waits with a median around 325.5 ms. Helper compilation
  finished by 20:05:18.043 and totalled 178.3 ms, while the stall continued for
  about 37 seconds. Pipeline compilation alone does not explain the regression.
- D2 (2026-11-18, implemented, static): desktop offers include each lower
  standard height tier at the native display aspect and at 16:9, rounded to
  whole pixels, sorted by height then width, with duplicates removed. Examples:
  2560x1080 offers 1707x720 and 1280x720; 3440x1440 offers 1720x720 and 1280x720.
  Persisted tokens remain dimensions, and native keeps its stable token.

## Runtime correction

- D1 (2026-11-18, implemented, static): Xbox target/depth bindings store raw
  surface pointers without adding references. After publishing fresh resources,
  `src/hooks/render_settings.cpp` now calls the native `sub_823E9CE0` to rebind
  targets and invalidate the material cache before releasing old surfaces.
  An unchanged, correctly tiled initial frame no longer allocates replacements.
- D1 (2026-11-18, implemented, static): SDK render targets are cached by EDRAM
  base, pitch, format and sample count. Different resolution tiers retain more
  full-EDRAM-height images; texture eviction does not bound this target map.
  Patch 0095 adds opt-in cleanup once per valid source-size transition using
  existing GPU-idle frame-end backend cleanup, preserving owned EDRAM contents
  and compiled pipelines. Logs record resize requests and target counts before
  and after pruning. The log does not prove GPU memory thrashing; the observed
  fence stalls and this retention path motivate the correction, whose runtime
  performance benefit remains unverified.
- Patch responsibility and platform scope: 0094 continues to own aspect fitting.
  New 0095 separately owns resize cache lifetime, changes no public API, and
  defaults off. It applies to shared Xenos D3D12 on Windows and Vulkan on
  Windows/Linux/macOS/iOS/Android, including MoltenVK, on supported architectures.
  Other title defaults, dependencies and compiled pipeline retention are preserved.

## Validation

- 2026-11-18 [build]: `cmake --build --preset win-amd64-release` passed,
  including both Windows D3D12 and Vulkan modules. Evidence:
  `out/live-profile-performance-build.log` and final incremental
  `out/live-profile-performance-build-final.log`.
- 2026-11-18 [host tests]: 15/15 focused CTest registrations passed in 17.52s:
  preparation settings registry, present resize, window policy, display mode,
  native mouse, settings bridge/screen/store/migration/service/restore payload/
  graphics/hot apply/bridge service, and menu payload. The resize tracker covers
  first, stable, invalid, disabled, repeated context transitions and independent
  instances; graphics offers cover ultrawide, 16:10, 4:3, 16:9 and small displays.
  The registry fixture verifies cleanup defaults off and queues title opt-in
  before driver registration. Payload registrations use the Windows runner.
  Evidence: `out/live-profile-performance-tests.log`.
- 2026-11-18 [patch replay]: all 67 production patches replayed, zero files
  needing updates; unrelated untracked SDK dependencies were retained.
  Evidence: `out/live-profile-performance-sdk-audit.log`.
- 2026-11-18 [docs]: checker passed with zero errors and 143 existing warnings
  (`out/live-profile-performance-docs.log`); `git diff --check` passed.

D1-D3 implementation and available scoped build/test gates are complete.
The runtime cause has not been isolated by GPU memory measurements, and no
post-correction FPS result is claimed. The owner should repeat native -> lower
16:9 -> 720p -> native, then verify native-aspect tiers and interface/gameplay
transitions. Inspect resize/cache-retirement log lines alongside frame pacing.
The owner performs final game/UI checks; no game navigation is authorized.
Windows AMD64 is available for compilation; cross-platform runtime performance
and visual acceptance remain unverified.
