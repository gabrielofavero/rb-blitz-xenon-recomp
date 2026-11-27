# Mobile adaptive render scale implementation plan

Date 2026-11-27. Status is planning only. Target iOS arm64 with Vulkan/MoltenVK.

## 1. Behavior

| Mode | Interface | Gameplay |
| --- | --- | --- |
| Quality | Highest sustainable interface tier | Highest sustainable gameplay tier with margin for a steady 60 FPS |
| Performance | Same automatic interface policy | More frame-time headroom for dense sections, power-ups and warm sessions |
| Manual | Current Interface wheel | Current Gameplay wheel |

Quality and Performance share the same measurements and interface policy.
Performance can exceed 720p when there is sufficient headroom. Both modes use the
existing source-height tiers and aspect rules, including compact portrait targets.
Gameplay tier changes take effect on the next song entry. Keep the selected tier
for that playthrough, including pause and resume. Interface changes take effect
on entry to a stable menu route and hold through navigation within it. Defer
changes during loading, transitions, dialogs and calibration.

Use Performance for new mobile settings after device acceptance. Migrate existing
wheel settings to Manual and keep both saved tier selections when switching modes.
If automatic scaling cannot be supported on a device, use its conservative
recommended profile and keep Manual available.

## 2. Measure the useful range on iPhone

- [ ] Compare the same full songs on an older target phone and a newer phone.
  Include 720 source-height at backend 1×, the same effective size at 2× and
  higher tiers at 2×. Record source and visible internal dimensions separately
  for landscape and compact portrait. Include representative menus and transitions.
- [ ] Measure distinct delivered game frames, frame intervals and p95/p99,
  missed 60 FPS deadlines, CPU and GPU timing, the worst transition frame,
  app footprint, memory headroom, thermal state and peak memory during
  replacement. Compare cold and warm sessions using the full-song and
  sustained-run protocol in the [Apple performance plan](2026-11-26-apple-performance-plan.md#1-requirements-and-acceptance).
- [ ] Use per-frame records that correlate guest frames, GPU work and presenter
  submissions. Validate delivered-frame counts against a device trace.
  Instrument the existing [guest FPS probe](../../../src/diag/framerate_probe.cpp)
  and backend submission points. Read timestamp queries asynchronously where
  supported and validate the measured cost on MoltenVK. Record loading,
  compilation, queue waits and CPU work so that slow frames can be attributed.
  Use fixed conservative recommendations where the measurements are insufficient
  to support automatic upgrades.
- [ ] Establish a conservative ceiling for each validated device class from
  the measured startup, menu-to-song and replacement peaks. Continue with
  adaptive scaling where a higher tier holds the cadence target or a lower tier
  measurably recovers missed frames. Keep the conservative fixed recommendation
  where the higher backend multiplier costs more than the usable tiers deliver.

## 3. Plan capacity and apply tiers

- [ ] Reuse [resolution profile planning](../../../src/settings/resolution_profiles.h)
  for tier offers, orientation and guest extents. Plan against a validated
  startup ceiling and pick the smallest multiplier that supports both contexts'
  ceilings. Keep the startup ceiling, saved mode, saved Manual values and
  current automatic tiers as distinct state. Start conservatively or seed
  from a compatible tier validated in a previous session.
- [ ] Use the existing [startup admission](../../../src/settings/boot.cpp) and
  [pre-frame target replacement](../../../src/hooks/render_settings.cpp) paths.
  The guest frame limit is 1920×720 per multiplier unit. At 16:9, 1080p
  therefore requires a 2× startup capacity even if the active frame starts at
  720p. Admit tiers by actual planned dimensions. Use the existing reopen
  flow when a user-selected mode requires a higher startup capacity. Background
  adaptation stays within the running process capacity.
- [ ] Publish an automatic tier as a runtime choice. Commit it only after the
  replacement is acknowledged. Keep the prior frame on a recoverable refusal,
  suppress further probes of the rejected tier, and preserve existing relaunch
  recovery for backend failures. Attribute automatic refusals to the attempted
  tier and capacity so they cannot blacklist an unrelated saved Manual value.
  Budget for old and new resources coexisting and for deferred GPU retirement.
  Under memory pressure, block upgrades and skip any replacement that cannot
  safely fit its transient peak.

[Interface memory delivery](../finished/2026-11-26-interface-render-memory.md) · [Compact portrait gameplay](../finished/2026-11-26-portrait-gameplay-rendering.md)

## 4. Implement the automatic policy

- [ ] Combine [active gameplay state](../../../src/input/native_menu.h) with
  [song-load lifecycle signals](../../../src/loading/load_trace.cpp) and
  verified results and selection transitions. Confirm native routes for
  completion, quit, restart and replay before committing to a boundary. Track
  observations against a playthrough and the actually applied tier. Pause
  suspends sampling and preserves the playthrough. Quit, failed load and
  incomplete attempts supply no upgrade evidence. Ignore late timing results
  from an older attempt or tier.
- [ ] Summarize representative active gameplay. Tune separate increase and
  decrease thresholds from device captures. Raise one tier after repeated
  eligible songs with sustained headroom. Lower the tier for the next song
  after sustained GPU pressure or a thermal ceiling change. Use a cooldown
  after a failed upgrade and cap how often trials occur. Treat an isolated
  CPU or compilation stall as insufficient evidence to lower resolution.
  Performance uses stricter upgrade criteria and a larger margin to the
  16.7 ms frame budget. Quality can spend more of that budget when sustained
  delivery remains stable.
- [ ] Use a common interface controller for both modes. Learn from stable
  native menu windows at each applied tier. Choose the highest tier that
  preserves responsive menus, has memory and thermal headroom, and leaves
  the measured gameplay capacity viable. Use the same safety rules in both
  modes. Block upgrades on memory pressure or elevated thermals and allow
  a lower tier at the next eligible boundary. Reset timing windows after
  foreground return, orientation or graphics changes.
- [ ] Persist confirmed interface and gameplay tiers only after sufficient
  observations. Scope the records to device, renderer and build compatibility,
  orientation and relevant graphics settings such as AA. Revalidate after
  incompatible changes and ignore stale or failed records. Keep settings and
  journal writes off the frame-sampling path. Log each proposed tier, the
  applied dimensions, the trigger and any rejection.

## 5. Integrate the native settings page

- [ ] Add a mode selector to the existing mobile Render Scale page. Show
  Interface and Gameplay wheels in Manual. In automatic modes, use native
  labels to show the effective tiers and an apply or reopen status where
  needed. Keep menu and pause routes consistent and preserve touch, controller,
  Confirm and Back behavior. Keep at most six entries, names at most 28
  characters including values, and captions within 96 ASCII characters.
  Check the caption strip with all three Settings Labels sizes and the actual font.
- [ ] Cover persisted-mode migration, switching each direction, hardware
  fallback, reopen-required status, recovery and restoration of saved wheel
  selections. Exercise both mixed portrait/landscape orientations through the
  native settings routes.

[Settings schema](../../../src/settings/schema.cpp) · [Settings caption layout](../../../src/ui/help_layout.h)

## 6. Validate and decide rollout

- [ ] Add focused policy coverage for stable headroom, GPU versus CPU pressure,
  sparse or stale samples, thermal and memory changes, cooldown, bounds, and
  safe recovery from guest or backend allocation refusal. Test song completion,
  pause/resume, quit, restart, replay and missing results. Verify automatic
  mode changes do not mutate saved Manual tiers.
- [ ] Build iOS arm64 bundled and importer Release packages through the
  established presets and packaging paths. Validate the affected settings,
  resize and portrait fixtures on the available host builds. Shared title
  code must preserve desktop behavior on Windows, Linux and macOS across the
  applicable D3D12, Vulkan and MoltenVK paths. If instrumentation needs an SDK
  patch, identify the owning patch and backend scope before editing, update
  the lineage and verify full-stack replay. Record outstanding configurations.
- [ ] On signed iPhones, compare Manual, Quality and Performance over matching
  full songs and warm sessions. Include light and dense charts, power-ups,
  native menu transitions, both orientations, replays and background/resume.
  Verify actual frame sizes, video cadence, resize peak and hitch cost, audio
  timing, judgement, calibration, touch alignment and the return to menus.
- [ ] Enable the default only after the supported devices show repeatable
  quality or frame-pacing gains and bounded memory across transitions.
  Keep mid-song resizing deferred until a separate device trial validates
  the worst resize frame, resource lifetime and input timing.
