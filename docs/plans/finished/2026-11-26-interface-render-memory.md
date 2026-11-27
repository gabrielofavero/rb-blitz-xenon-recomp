# Interface rendering memory

Status: implementation delivered, 2026-11-26. Signed-device acceptance remains unverified.

## Scope and acceptance

Reduce the startup memory cliff when increasing Interface render scale, preserve
guest-visible render/texture aliasing, and recover from allocation failure without
approving an incomplete frame. Investigate portrait menu targets separately from
gameplay projection. The broader Apple frame-time work remains in the
[performance plan](../todo/2026-11-26-apple-performance-plan.md).

| Step | Deliverable | Acceptance | State |
| --- | --- | --- | --- |
| M1 | Trace allocation paths and all scaled-memory consumers | Record device/log evidence and exact address/lifetime contracts | Complete; exact crash cause unproven |
| M2 | Range-backed scaled resolves on non-sparse MoltenVK | Alias/overlap, pending submission, rollback, readback and allocation-limit tests; native builds | Implemented and checked |
| M3 | Portrait target optimization | Preserve camera, UI layout, effects, touch and mixed-context transitions; prove allocation reduction | Implemented for authored Interface routes |
| M4 | Recoverable allocation rejection | Preserve saved requests, reject failed active profiles and retain conservative recovery | Implemented and checked |
| M5 | Integration and delivery | Strict full-stack patch replay, focused tests, macOS/iOS release builds and current IPA; record separate device checks | Builds and automated gates complete; device acceptance pending |

## Boundaries

The SDK range allocator is a distinct Vulkan memory-address mapping responsibility,
separate from 0098's host import and retention policy and 0097's dense admission
limits. Patch 0102 extends their output with compact range mapping. The new
backing policy applies to non-sparse MoltenVK; native sparse and other
Vulkan drivers and D3D12 retain their storage defaults. Shared APIs apply on Windows,
Linux, macOS and iOS and supported architectures. Native validation available
here is Apple arm64; record other platforms explicitly as untested.

Existing uncommitted portrait, display and recovery edits predate this work and
must be preserved. Use established iOS build/package directories and overwrite
the current package filename. Device game testing requires the current signed installation.

## Evidence and progress

- 2026-11-26 [tree]: boot chooses a fixed multiplier from both selected profiles.
  Before this change, Vulkan texture-cache initialization reserved 512 MiB times
  the scale area when scaled rendering was enabled. Imported host backing already
  existed; repeating lazy host allocation alone does not remove device limits.
- 2026-11-26 [static]: resolve and texture-load descriptors use range-relative
  addresses. Scaled CPU readback additionally supplies a global source offset
  and must be adapted with the range API. Pending descriptors must retain their
  old buffers when overlapping ranges merge.
- Initial documentation checker: 0 errors and 49 existing warnings, recorded in
  `out/interface-memory-docs-before.log`.


## M1 Allocation evidence and scope

The connected iPhone 16 Pro logs were copied read-only into
`out/interface-memory-device-before.log` and
`out/interface-memory-device-previous.log`. The preceding 2x boot requested
2024x932 and reached menus; the subsequent session recovered to 720p and reached
active gameplay. Neither log supplies an allocation error or OS termination
report identifying the reported boot crash. The structural allocation cliff is
confirmed by source; the exact crash mechanism and its removal remain unproven.

All scaled-memory consumers are covered: resolve destinations, texture base and
mip sources, and CPU downscale/readback. Their relative addressing permits compact
buffers without regenerating shader bytecode. Global scaled addresses remain
logical identities for alias tracking, including addresses beyond 4 GiB.

## M2 Range-backed resolves

SDK patch 0102 maps accessed ranges into 64 KiB aligned buffers. Contained ranges
reuse storage. Overlapping extensions allocate a union and copy existing GPU data
before publishing it. Old buffers survive through the current submission so
recorded descriptors remain valid. Allocation failure leaves mappings intact;
metadata failure after queued migration retires tentative storage by its fence.
Texture-cache clear preserves backing because resolved-page validity survives it.

Actual memory requirements and imported host alignment are checked before Vulkan
allocation. Active, retiring and tentative storage count against the peak budget:
at most one quarter of the device-local heap, capped at 512 MiB on iOS and 1 GiB
on macOS. These are policy ceilings, not measured process headroom. Per-buffer
allocation and descriptor-range limits still apply. A long overlap chain can
exceed a per-buffer limit and is rejected; full shader-address indirection is
not part of this implementation.

0098 retains host import/retention ownership and default behavior for existing
helper consumers. 0097 retains dense/sparse admission; only the compact policy
bypasses the monolithic-size gate and substitutes per-block/peak admission.
Presentation interface version 2 carries a latched allocation failure. The driver
preserves it through detach and clears it at the next title attachment, preventing
an ABI mismatch with older presentation plugins.

## M3 Authored portrait targets

`src/settings/portrait_render_target.h` derives a narrow target equivalent to the
existing centered crop. The guest allocation seam commits target dimensions,
tiles, textures and bindings before publishing the new projection ratio. A
camera already selected before lazy frame begin is refreshed immediately, so the
first compact frame uses matching matrices. Native selection refreshes reused
cameras; destruction removes metadata before address reuse.

The projection hook changes horizontal projection and inverse together, then
rebuilds the cached native WorldToScreen matrices used by pointer mapping. The
landscape layout constants retain authored vertical coverage. Presentation uses
the actual frontbuffer aspect and disables the extra crop in compact mode.

Only authored portrait Interface routes use this path. Gameplay, unadapted menus,
custom-target cameras and partial viewports retain their native paths. The first
pre-context frame still allocates landscape. Compact targets reduce guest frame
and resolve-image dimensions; EDRAM attachment height remains inferred from the
existing pitch/ownership model. No equivalent reduction for every backend
attachment or whole-process resident footprint is claimed.

## M4 Allocation recovery

An allocation refusal invalidates even an already-stable profile. Subsequent
refreshes cannot prove skipped/incomplete rendering. The title requests orderly
shutdown and finalizes the journal after GPU drain. A clean close or intentional
settings restart retains the failed-request latch. Relaunch selects conservative
profiles while preserving saved quality requests and layout. An explicitly changed
request permits another attempt. There is no same-process renderer rebuild.

## Validation and delivery

- `scaled_resolve_segments`: 8,785 checks, including an independent delayed-copy
  oracle, randomized overlap/alias operations, allocation rollback, peak budgets,
  address bounds and pending old descriptors. Standalone UBSan also passed;
  the host's older ASan runtime could not initialize its interceptors before main.
- `scaled_resolve_native` and `scaled_resolve_native_async`: 31 checks each on
  Apple M1, with 8 GiB logical storage and addresses above 5 GiB. Peak compact
  backing is 5,308,416 bytes plus a 3,276,800-byte readback buffer. GPU readback
  verifies both updated aliases and historical old descriptors before fence
  retirement. This is correctness evidence, not an iPhone memory/FPS measurement.
- `portrait_camera`: 757 checks with production raw hooks and independent
  crop-equivalence/pointer-matrix fixtures, including selected-before-frame timing.
- `settings_graphics`: 3,480 checks including late allocation rejection after
  steady output, normal close, intentional restart, safe recovery and changed
  requests. Existing `portrait_layout` also passes 100,977 checks.
- Exact replay of all 74 SDK patches passes; all 13 paths owned by 0102 match
  live SDK bytes. The official live audit stops on pre-existing unrelated
  `src/ui/prompt.cpp` drift, preserved unchanged. Evidence:
  `out/interface-memory-sdk-replay.log`.
- Final focused CTest run: all 11 suites pass (`out/interface-memory-tests.log`).
  The initial sandbox-only display run could not initialize SDL video; the native
  window-service run passed. Native GPU tests likewise require GPU access.
- SDK patch-tool tests: 36 registrations, 12 executed successfully and 24 skipped
  because PowerShell is unavailable (`out/interface-memory-patch-tool-tests.log`).
- Documentation baseline is 0 errors and 49 warnings. Native release/package
  results and the final checker are recorded below after completion.

Windows D3D12/Vulkan, Linux, Intel macOS, other architectures and native sparse
hardware are untested here. This Mac cannot run the Windows release preset.

## Signed-device acceptance

The iPhone is connected and game testing is authorized. No valid signing identity
is available on this Mac; the updated package will be installed with Sideloadly.
The installed older binary cannot validate these changes. After installation:

1. Confirm the executable/package identity and range-backed resolve log.
2. Boot at conservative Interface scale, verify Start/main-menu/Settings/Library
   appearance, camera activation and touch alignment.
3. Select a higher Interface tier with Gameplay at the floor, reopen when capacity
   requires restart, and verify the requested effective pixel dimensions.
4. Check pause/settings and mixed orientation/context transitions, effects,
   background/resume and a complete song; compare memory separately from timing.
5. Verify allocation-refusal recovery if encountered, retaining failed settings
   rather than repeatedly retrying them.

High-resolution iPhone boot stability, visual/touch correctness, resident footprint
and sustained performance remain unverified until this signed build is exercised.

## Packaging integration

The existing IPA packager refused an existing destination and required an archived
copy. It now stages/audits a candidate and atomically replaces the current mode's
IPA using its existing final replacement operation. The prior IPA and audit remain
intact on staging/archive failure, and the other package mode remains untouched.
Nineteen packaging tests pass, including real iPhoneOS Mach-O fixture checks and
transactional replacement/failure cases (`out/interface-memory-packaging-tests.log`).
Established build directories and `out/ios/packages/` are reused; no extra IPA
archive copies were created. Package executables are relocated and stripped of
signatures during staging, so source-file hashes differ; the bundled package's
Mach-O executable text matches the final device build byte-for-byte.



## Final build record

- macOS arm64 Release game and focused targets build successfully:
  `cmake --build --preset mac-arm64-release --target rb_blitz` and the focused
  targets (`out/interface-memory-mac-build.log`,
  `out/interface-memory-tests-build.log`).
- iOS arm64 bundled Release builds successfully with the final camera refresh:
  `cmake --build --preset ios-arm64-release --target rb_blitz`
  (`out/interface-memory-ios-build.log`).
- The existing iOS importer directory had no cache. It was configured with the
  existing Xcode/tool paths, then its fresh Release build passed:
  `cmake --build --preset ios-arm64-importer-release --target rb_blitz`
  (`out/interface-memory-ios-importer-configure.log`,
  `out/interface-memory-ios-importer-build.log`).
- Builds use Xcode 26.0.1, AppleClang 17, CMake 3.31.6 and Ninja 1.13.2. The
  existing non-strict gate warns that CMake/Ninja differ from the frozen versions;
  no compiler optimization or strictness policy was changed.
- The final bundled IPA is `out/ios/packages/RockBandBlitz-0.1.0-arm64-ios.ipa`.
  Its SHA-256 is
  `64b95cad0f79d51f91f81c4d3d42c18fed9da3edfa585808958ef6160b445336`.
  The package audit matches that hash. Its executable text SHA-256 is
  `a7e184b3583ad1dbe78c1a2e0151873bc94b2a8f525d9a4dcc542b770781d844`,
  matching the final bundled build. Packaging evidence:
  `out/interface-memory-ios-package.log`.


- The importer IPA is `out/ios/packages/RockBandBlitz-0.1.0-arm64-ios-importer.ipa`.
  Its SHA-256 is `eba559601153490a49225d78ce6711754ae26c9a092900f5e7653af609f98d73`;
  its audit matches and reports no private game assets. Packaging evidence:
  `out/interface-memory-ios-importer-package.log`.
- Both current mode packages are unsigned and need the established signing route.
  Documentation checks report 0 errors and the same 49 pre-existing warnings;
  `git diff --check` passes (`out/interface-memory-docs.log`).

Implementation, builds and available automated gates are complete. The separate
signed-device checks above remain open; no claim that the reported phone crash
is resolved, or that a higher resolution is sustainable, is made from host tests.
