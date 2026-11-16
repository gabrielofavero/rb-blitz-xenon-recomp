# Desktop aspect ratio and render scale

**Status: delivered 2026-11-15; authored UI save/restart acceptance unverified.**
Implementation, the current native Release build and available code checks are
complete. The owner explicitly stopped further game/UI launches after the last
UI-focused launch was closed at the offline prompt. The observed full-song aspect
gate and the exact remaining manual gate are recorded below. This is the owner's successor
to the fixed-resolution/Auto selection model. It follows the measured
[Mac performance investigation](../../engine/vulkan-performance.md#root-cause-follow-up-rendering-cost-and-audio-rewind-2026-11-15);
it applies to all desktop releases, with platform-specific capability limits.

## Request and acceptance

Replace the visible Resolution preset list with **Aspect Ratio** and **Render
Scale**. Auto aspect uses the user's monitor proportions. The initial scale
recommendation uses a cheap hardware/capability check rather than a gameplay
benchmark; the player can select a different supported scale. Lowering scale must
preserve aspect, guest layout, HUD placement, mouse mapping and presentation.

The immediate regression was a successful 60-update/s native-scale M1 song that
used the standalone `resolution_scale=1` override. It selected the retail 16:9
base on a 16:10 screen and stretched the picture. The two settings must drive one
coherent guest-size/scale plan instead of independently changing renderer flags.

## Decisions

- **D1 — Stable identities and migration.** Add Aspect Ratio and Render Scale as
  new stable setting IDs (26 and 27 respectively). Preserve the old Resolution ID 20 and storage compatibility
  needed to migrate existing preferences and graphics recovery. Existing explicit
  new values win over inferred values. Other TOML fields remain intact.
- **D2 — Aspect choices.** Auto is the default; explicit choices cover 16:9,
  16:10, 4:3, 21:9 and 32:9 where the bounded title planner can realize them. Auto
  follows the actual display, including 16:10. A wide ratio must use a safe guest
  base rather than exceed the measured guest-width/height limits: the normal
  base has height 720 with aligned width, while 32:9 uses 1920x540 to stay inside
  the measured 1920-pixel guest-width gate.
- **D3 — Scale and capability.** Use the name Render Scale and integer multipliers
  (1x, 2x and 3x where supported). Automatic recommendation is separate from
  aspect. The backend's per-buffer/size constraints remain binding; a display's
  pixel count is not proof that its GPU can render that size at 60 Hz. Auto takes
  the largest backend-supported integer scale no greater than the display height
  divided by its native baseline (floored, at least 1): 540 when the display aspect
  exceeds 1920:720, otherwise 720. It is capped at 1x for up to 8 GiB system RAM,
  2x for up to 16 GiB and 3x above that. Unknown memory uses 1x. For example, a
  3840x1080 32:9 display with 32 GiB RAM recommends 2x. This is a cheap conservative
  recommendation, not a GPU benchmark or a 60-Hz guarantee.
- **D4 — Guest surface.** Use the existing guest-authored settings route. Every
  page has at most six entries, option labels at most 28 characters including the
  current value, and ASCII captions at most 96 characters including apply/restart
  suffixes. Validate Normal, Big and Biggest caption layouts.
- **D5 — Application boundary.** Aspect and render scale remain restart-scoped
  until cache/EDRAM migration is separately proven. Manual scale and aspect choices
  remain independent of automatic recommendations.

## Delivery ledger

| Step | Deliverable and gate | State |
| --- | --- | --- |
| P1 | Schema, stable IDs, persistence and legacy-resolution migration; host regression tests | Delivered; host gates passed; live persistence unverified |
| P2 | One aspect/scale planner, cheap initial recommendation and boot/recovery/override integration; planner and boot tests | Host gates and M1 Auto/1x aspect run passed |
| P3 | Authored menu reorganization, six-entry pages, concise labels/captions and size/layout fixtures | Delivered; host/font gates passed; live new-control interaction unverified |
| P4 | Current native Release build, focused tests, SDK patch replay and documentation checker | Passed: build, nine tests, 71-patch replay, docs checker |
| P5 | Aspect-correct native-scale M1 run and settings restart/save/relaunch verification | Live Auto/1x aspect and full song passed; UI save/restart unverified after owner stopped launches |

## Implementation and validation record (2026-11-15)

The schema now carries `graphics.aspect_ratio` (default `"auto"`) and
`graphics.render_scale` (default `"auto"`). Supported scale choices are filtered
by the renderer capability snapshot. A manual aspect and scale are independent
of the recommendation. Existing new keys win over legacy-resolution inference;
the old resolution row is retained internally for migration and recovery and is
removed from the visible menu. An explicit command-line `resolution_scale` now
keeps the planned guest shape and the visible Render Scale reports the effective
override. Invalid aspect recovery preserves the selected
scale, and graphics recovery recomputes output pixels from the recovered aspect
and scale rather than clamping through an old fixed-resolution preset.

The settings-screen fixture passed 132,112 checks
(`out/macos/root-cause/settings-ui-tests.log`). Both authored captions are within
96 ASCII characters (50 for Aspect Ratio, 42 for Render Scale), and displayed
labels stay within 28 characters. A fixture using the bundled macOS ProggyTiny
font passed 48 wrapping/fit cases: two captions, three Settings Labels sizes and
eight display dimensions spanning the supported ratios
(`out/macos/root-cause/measure-settings-captions.cpp`). These are host/font gates;
they do not establish live menu or pause-screen acceptance.

For the measured 8 GiB M1 on a 2880x1800 display, Auto aspect plus Auto scale
therefore selects a 1152x720 guest base at 1x. The guest frame, HUD mapping and
presentation aspect must all use 16:10. This is the intended replacement for the
stretched standalone-scale override. The first live replacement run failed:
the owner saw horizontal bars and closed before the menu. Aspect acceptance
was still open at that point; the corrected follow-up below passed it.

`out/macos/root-cause/aspect-auto-final.log` records the intended guest base,
scale and `video_mode_width`/`video_mode_height` values, not the actual dimensions
returned by the SDK video-mode query. Source inspection found that
`GetConfiguredVideoModeHeight` treated explicit `720` as unset because it equaled
the cvar default. It then substituted the Retina window's 360 logical pixels and
clamped that height to 480. Width 1152 remained explicit, yielding a 1152:480
(2.4:1) presentation aspect on the 16:10 display. SDK patch
`0072-explicit-native-video-mode.patch` now retains explicit dimension intent
using the cvar source in both the kernel query and SDL paths. The
`video_mode_intent` fixture uses the production predicate and real cvar registry
for explicit native 1280x720, 1152x720 and reset-to-default cases. The patch audit
replayed 71 patches with zero drift. The rebuilt follow-up passed the live aspect
gate described below; the original boot log alone did not prove the picture.

The current native Release build passed (`aspect-bars-build.log`), as did all
nine affected CTest registrations in `aspect-bars-tests.log`: video-mode intent,
settings store/migration/service/graphics/screen/bridge, audio stability and upload
hazards. The Auto/1x follow-up on the same 16:10 M1 filled the display without
horizontal bars, with circular interface elements intact. Captures under
`out/macos/root-cause/` are `aspect-bars-fixed-prea.png`,
`aspect-bars-fixed-menu.png` (gameplay despite the filename) and
`aspect-bars-fixed-results.png`. One Week finished at raw song time
172043.765625 ms, with 176 measured windows and no backward clock windows. The
weighted rate was 50.63 completed guest draws/s; the slowest window was 10.06/s
(`aspect-bars-fixed-metrics.json`). This was not locked 60 FPS. A vmmap snapshot
reported 2.2 GiB current/peak physical footprint, compared with the previous 2x
run's 4.4 GiB peak. System swap was about 12.5 GB with active `photolibraryd`
and another project's `dotnet` build; the sample includes presenter-fence and
drawable waits. These runs are not a controlled performance comparison. The
unverified live gate is settings save/restart/relaunch through the authored UI.
The later UI-focused launch ended at the offline prompt when the owner closed
it; no interaction with either new Display control was observed.

## Prior performance evidence carried forward

On 2026-11-15, `out/macos/root-cause/vertex-fma.log` completed One Week at the
previous Auto 2x configuration with 26.27 completed guest draws/s and no backward
one-second song-clock windows. `native1x-final.log` completed the song at about
60 draws/s. The owner called that the best run and reported stretching; aspect
was not established by that run; the corrected 16:10 follow-up above supplies
that evidence. Those are observed runs, not a
matched-scene benchmark or proof for every song/device.

The Mac Release game builds. Audio buffering and upload-hazard tests passed,
including 3,627 hazard checks. The expanded `vulkan_fragment_gradient` test passed
with real Metal access in 0.22 seconds (`vertex-gradient-escalated.log`); its
sandboxed run skipped because Metal was unavailable. The SDK audit replayed all
70 patches without drift. These gates cover the preceding performance changes,
not the new settings implementation before P4 integration.

## Reproduction and outstanding coverage

Build on the native host with `cmake --build --preset mac-arm64-release --target
rb_blitz`, or the corresponding Windows/Linux desktop preset. Run the affected
settings storage, policy, boot, menu and layout tests; run `sdk_patch_stack`, the
SDK patch audit, and `python .github/skills/documentation/scripts/check_docs.py`.
The GPU regression requires real Vulkan/Metal device access and reports a skip
when no device is available.

The owner initially authorized driven gameplay, then explicitly said to stop
booting the game. No further game/UI launches are authorized by this handoff.

The remaining manual gate, if the owner later chooses to resume it, is to open
Help & Options -> Audio & Video -> Display on the current build, change Aspect
Ratio and Render Scale independently, leave Help & Options, confirm Restart now,
and verify both saved choices and the resulting aspect/scale after relaunch. This
includes both authored menu and pause contexts and the Normal/Big/Biggest captions;
font/host fixtures passed, but these live interactions were not observed. A future
record must name output size, guest base, aspect and effective scale and confirm
that manual choices persist without stretching.

Windows, Linux, Intel Mac, other display shapes, physical controllers and
installed distributions remain unverified. Residual frame-pacing limits remain
recorded in the performance study; neither this delivery nor its full-song
acceptance claims locked 60 FPS.
