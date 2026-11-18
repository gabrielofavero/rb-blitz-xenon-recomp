# Live display profile corrections

Date: 2026-11-18. Status: finished; owner game/UI acceptance unverified.

## Deliverables

- D1: Keep anti-aliasing editable when its startup preference queues before the
  GPU settings owner registers; preserve explicit higher-priority overrides.
- D2: Apply saved desktop resolutions and mobile scales at the next active
  context frame, including after restart, with independent interface/gameplay values.
- D3: Preserve the native window size across profile edits and context changes;
  fit each active aspect with black bars rather than stretching the picture.
- D4: Keep conservative recovery profiles editable and validate the changed
  guest resource protocol, registry replay and pointer mapping without game navigation.

## Findings and implementation

- 2026-11-18 [owner log]: `out/build/win-amd64-release/logs/rb_blitz_003.log`
  reports texture initialization refusal for both 1280x540 and 640x360. The latter
  is the saved 720p profile under the fixed 2x backend multiplier. Persistence and
  frame selection reached the allocator; the prior frame was retained.
- D1 (2026-11-18, implemented, static): `SettingsBoot::WriteCvar` now records
  accepted deferred SDK writes as owned. `FinishInit` verifies them after replay,
  so successful AA initialization is not mistaken for an external source.
  `runtime_values.cpp` includes the exact source-aspect binding before GPU
  registration; unknown names still fail instead of being silently accepted.
- D2 (2026-11-18, implemented, static): `sub_82508270` / `sub_82507B00` write
  pitch and data through their output argument and return no status. Their return
  register can contain a high-bit guest pointer. The resource initializer uses
  typed output fields and no longer interprets that register as an HRESULT.
  Fresh resources are initialized before publish; real failure retains the old frame.
- D3 (2026-11-18, implemented, static): boot window fitting uses the native
  display size independently of the selected render profile. Live profile changes
  only rebuild guest resources. SDK patch 0094 already reads the active frontbuffer
  aspect in both backends; the newly queued flag makes that route active. The
  guest swap packet obtains dimensions from the current resolve texture, so no
  device or swapchain metadata is patched. Pointer mapping uses the same aspect.
- D4 (2026-11-18, implemented, static): recovery seeds both context profiles at
  720p on desktop (native fallback where needed) or 0.5 on mobile, retaining saved
  requests. It keeps the profile renderer active and recomputes the internal
  frame from recovered choices. Explicit edits can apply live during recovery.

## Validation

- 2026-11-18 [build]: `cmake --build --preset win-amd64-release` passed with
  the final sources (`out/live-display-fixes-build.log`).
- 2026-11-18 [host tests]: all 14 focused CTest registrations passed in 16.50s:
  preparation settings registry, window policy, display mode, native mouse,
  settings bridge/screen/store/migration/service/restore payload/graphics/hot
  apply/bridge service, and menu payload. Payload registrations use the Windows
  test runner. Evidence: `out/live-display-fixes-tests.log`.
- 2026-11-18 [docs]: checker passed with zero errors and 143 existing warnings
  (`out/live-display-fixes-docs.log`); `git diff --check` passed. The known
  unrelated path-policy failure from the earlier wider run was not rerun.

D1-D4 implementation and available scoped build/test gates are complete.
Regressions cover
late registration of AA/source-aspect settings, valid high-bit guest addresses,
clear/unlock ordering, zero-data refusal, conservative recovery choices and
720p side bars within a fixed ultrawide window. No SDK patch changed in this
correction; the earlier 66-patch replay remains the stack evidence.

The owner performs game/UI acceptance; no game navigation is authorized here.
Windows AMD64 is available for compilation. Native Vulkan/D3D12 transitions,
mobile touch, unusual aspect ratios and long-session resource lifetime remain
unverified after this correction. Exact host dimensions can still round by a
few pixels under the fixed integer backend multiplier.
