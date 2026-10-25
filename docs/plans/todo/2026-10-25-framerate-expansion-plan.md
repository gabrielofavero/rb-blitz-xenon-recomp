# Framerate expansion: feasibility and implementation plan

**Status: planning, 2026-10-25. No runtime changes or new gameplay tests.**
This analysis uses the current source, the recorded 2026-09-20 pacing experiment,
and public RB3 sources inspected on 2026-10-25. Higher-rate gameplay, scoring and
physics correctness remain unproven. Existing shader-preparation changes in the
working tree are independent of this plan.

## 1. Finding and scope

**Feasible as a gated engineering project; not established safe as a settings-only
change.** The port already runs above 60 FPS when V-Sync is disabled. RB3's shared
engine has elapsed-time task scheduling and audio-driven gameplay time, so there
is no evidence that increasing render cadence necessarily doubles song speed.
However, it also has frame-dependent camera simulation, per-poll housekeeping
and a sample-count-based song-clock filter. These are concrete audit targets.

No universal safe maximum was found in the inspected RB3 sources. Neither 60 FPS
nor 120/240 FPS is a proven engine hard limit. The SDK's 240 Hz video-mode clamp
is an implementation limit of that setting, not a physics or gameplay guarantee.

| Id | Requirement |
| --- | --- |
| R1 | Identify the present default and the evidence for actual in-song cadence. |
| R2 | Preserve audio speed, song duration, judgement windows, score, power-up rules and pause behavior as render cadence changes. |
| R3 | Offer higher rates only after they pass sustained gameplay and visual checks. |
| R4 | Keep a reproducible 60 FPS reference and a way to revert. |
| R5 | Use the existing Help & Options graphics surface. No new host overlay is needed. |
| R6 | Preserve retail and Ultimate compatibility; prove Blitz addresses and layouts rather than transplanting RB3 constants. |

## 2. What the game does today

### 2.1 Default, configured cadence and measured cadence

**[tree]** There is a boolean V-Sync row, default **true**, and no separate FPS
limit row: [schema.cpp](../../../src/settings/schema.cpp), `kVsync`.
The SDK defaults `video_mode_refresh_rate` to **60.0 Hz**
([window.cpp](../../../rexglue-sdk/src/ui/window.cpp));
`GetConfiguredVideoModeRefreshRate` clamps an explicit request to **24..240 Hz**
([xboxkrnl_video.cpp](../../../rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp)).
This is a guest video mode, not an automatic copy of the monitor's refresh rate.

**[tree]** The vblank worker in
[graphics_system.cpp](../../../rexglue-sdk/src/graphics/graphics_system.cpp),
`GraphicsSystem::SetupGuestGpu`, reads the guest refresh once at worker startup. With
V-Sync on it signals at that rate; with V-Sync off it uses a **1 ms / nominal
1000 Hz interrupt interval**. `MarkVblank` advances the GPU counter and dispatches
the guest interrupt. That 1000 Hz interval is not 1000 rendered FPS. The worker
re-reads `vsync` live, but does not re-read the configured refresh rate.

The D3D12 presenter calls `Present(0, ...)` and may drop obsolete queued frames
([d3d12_presenter.cpp](../../../rexglue-sdk/src/ui/d3d12/d3d12_presenter.cpp)).
Consequently, this product's V-Sync control currently affects guest pacing; its
name does not establish conventional monitor-synchronized presentation.

**[measured, 2026-09-20]** The historical in-song experiment in
[bringup-log.md](../../history/bringup-log.md), "Close-out: frame pacing and input
polling", reports:

| Mode | SDK `XE_SWAP` cadence | Input sampling, user 0 | What this establishes |
| --- | --- | --- | --- |
| V-Sync on | 59.3 FPS average; 16.72 ms median frame interval | 402.1 polls/s | Approximately 60 FPS under that run's configuration. |
| V-Sync off | 68.3 FPS average; 14.34 ms median frame interval | 397.4 polls/s | The guest loop can exceed 60; input polling does not simply equal render FPS. |

The original reports were recorded at
`out/m5-pacing/vsync-{on,off}/pacing-input.{json,md}`. They are historical evidence,
not a measurement of the current executable or a new claim that those raw files
are still present. Their 317.1/316.0 s song windows are not a controlled full-song
duration comparison. The audit verified pacing and delivery of ordinary button
presses, **not** high-rate scoring, power-up collision equivalence or audio speed.
Actual current-run FPS is not measured by this analysis.

**[tree]** Content discovery temporarily sets `vsync=false` and restores a saved
value in [content_progress.cpp](../../../src/hooks/content_progress.cpp). Any new
cap must cooperate with this behavior and with settings changed during discovery.

### 2.2 Three different rates

Keep separate: guest update/draw iterations, GPU swap submissions, and actual
displayed images. A high monitor refresh or host UI tick does not prove that new
guest frames were generated. The existing `VdSwap` and `XE_SWAP` traces measure
submission and swap-packet handling; neither is a physical scanout counter.
Measure engine updates and presentation separately before claiming 120 FPS.

## 3. What the RB3 references establish

All external links below are pinned to the revisions fetched on 2026-10-25.
Reference files and GitHub tree metadata were downloaded read-only into the
ignored `out/_scratch/framerate-research/` directory. No source was ported.

| Source | Finding | Interpretation for Blitz |
| --- | --- | --- |
| [RB3 Xbox task scheduler](https://github.com/freeqaz/rb3-xenon/blob/fe7a8eb01e8bf171180ed89bbe5b602651e9518b/src/system/obj/Task.cpp#L493), `TaskMgr::Poll`, `TaskTimeline::Poll` | Normal scheduling uses timer milliseconds and elapsed task time. The fixed `1/30` path is inside `HX_NATIVE` headless support. | Rendering more often need not advance game time faster. The headless test path is not a retail 30 FPS cap. |
| [RB3 Wii scheduler](https://github.com/freeqaz/rb3/blob/0ee60352dae97181750d4d2d6a86f632919b2602/src/system/obj/Task.cpp#L388) | Its ordinary scheduler also uses elapsed timer time. Native replay has a separate recorded-clock branch. | Independent sibling evidence; native replay behavior must not be attributed to Blitz. |
| [RB3 Xbox gameplay](https://github.com/freeqaz/rb3-xenon/blob/fe7a8eb01e8bf171180ed89bbe5b602651e9518b/src/band3/game/Game.cpp#L1770), `Game::Poll` | Takes audio time, filters it with `DeJitter`, then calls `TheTaskMgr.SetSeconds` and polls gameplay with song milliseconds. | Supports an audio-led architecture rather than a globally fixed increment per rendered frame. Blitz's own route still needs mapping. |
| [RB3 Xbox animation](https://github.com/freeqaz/rb3-xenon/blob/fe7a8eb01e8bf171180ed89bbe5b602651e9518b/src/system/rndobj/Anim.cpp#L39), `gRateFpu`, `AnimTask::Poll` | Animation tracks use authored rates such as 30 frames per second; tasks map elapsed time into animation-frame space. | A `k30_fps` asset rate does not limit rendering to 30 FPS. Do not globally replace 30/60 constants. |
| [RB3 Xbox particles](https://github.com/freeqaz/rb3-xenon/blob/fe7a8eb01e8bf171180ed89bbe5b602651e9518b/src/system/rndobj/Part.cpp#L594), `Poll`, `MoveParticles` | Accumulates elapsed seconds multiplied by 30, integrates movement with a frame span, and exponentiates drag. There is also cleanup every 60 polls when not drawn. | Much of this is time-aware. Numerical integration, collisions, random emission and poll-count maintenance still require validation at different rates. The cleanup interval is not a hard FPS cap. |
| [RB3 Xbox camera](https://github.com/freeqaz/rb3-xenon/blob/fe7a8eb01e8bf171180ed89bbe5b602651e9518b/src/system/world/CameraShot.cpp#L1668), `CamShot::Shake` | Random shake trials occur per call; spring integration uses `60 / EmulateFPS`, defaulting to 1, rather than measured elapsed time. | Concrete frame dependence: more calls can change shake frequency and spring response. Confirm whether Blitz uses the same body and active path. |
| [RB3 recomp camera hook](https://github.com/ihatecompvir/band3_recomp/blob/0a04c13567470c53083b3271269099f30dd0e6dc/src/Hooks/camera_shake.cpp#L6) | Replaces camera scaling with elapsed host time times 60; clamps a long interval to 0.1 s. The source itself describes the implementation as fragile. | Prior art for a real high-FPS problem, not evidence that all engine timing is fixed or that a particular rate is safe. Do not copy its global timestamp into a multi-camera/pause path. |
| [RB3 Xbox song-clock filter](https://github.com/freeqaz/rb3-xenon/blob/fe7a8eb01e8bf171180ed89bbe5b602651e9518b/src/system/utl/DeJitter.cpp#L21), `NewMs` | Sample history, smoothing per call, an initial 16.666 ms delta and a bounded correction around the raw sample. | Not a 60 FPS cap; its effective smoothing duration changes with call rate. Test hit timing and restart/resume even when average song speed remains correct. |

The decompilation trees contain reconstructed bodies, native-port additions and
DC3-derived engine work. Their comments and `HX_NATIVE` branches help identify
which code claims retail correspondence, but **only Blitz disassembly plus runtime
evidence can establish the corresponding Blitz behavior**. No inspected source
documents a universal safe ceiling. This is a scoped result, not an exhaustive
claim about every upstream issue, branch or unpublished discovery.

## 4. Decisions

| Id | Decision |
| --- | --- |
| D1 | Keep today's configuration as the default through research. Treat **60 FPS** as the control; **120 FPS** is the first high-rate release candidate, not a promised safe maximum. |
| D2 | Probe 75/90 and 120 first, then 144, 165 and 240. Publish only individually validated choices. Uncapped stays a diagnostic mode until rate changes and overload behavior are proven. A failed rate stays unavailable. |
| D3 | Do not change guest clock frequency, time scaling, audio playback rate, chart tempo or global animation-rate constants to obtain higher FPS. |
| D4 | An FPS limit controls production of complete guest frames. A presenter-only limit that leaves engine updates unbounded does not meet the requirement. |
| D5 | Separate requested FPS limit from guest video refresh and host presentation. Keep the legacy combination reproducible. A higher guest cadence may require a higher guest video-mode refresh; do not silently infer it from monitor Hz. |
| D6 | Apply new FPS/guest-refresh configuration at process restart initially. The current worker samples refresh once. Do not mark the row hot until live reconfiguration is separately implemented and acknowledged. |
| D7 | Fix only demonstrated frame-sensitive Blitz paths. Favor a verified mid-function hook or a small native helper; generated code is regenerated, never edited. Any generic SDK work needs a tracked patch and patch-index entry. |
| D8 | No global host timestamp for camera objects. If the same shake algorithm is active, prefer per-object accumulated simulation at the original 60 Hz with interpolation, preserving random trials and damping per simulated step. Handle pause, resume, scene replacement and large stalls explicitly. A simple `dt * 60` multiplier alone is not a correctness proof. |
| D9 | Discovery's temporary acceleration must become an explicit temporary policy override if a new limiter would throttle it. Restore the latest player request, including cancellation, failure and settings changes during discovery. Never accelerate active gameplay under that override. |
| D10 | The owner's test workflow applies: build before a requested manual test, then ask whether the owner wants to perform the gameplay test and wait before driving the UI. This analysis did not navigate the game. |

## 5. Implementation packages and gates

**P0 -> P1 -> P2/P3 -> P4 -> P5.** P0/P1 are diagnostic work. No public high-rate
row is enabled until P4 passes for that rate and backend.

### P0. Map Blitz clocks and establish a current control

- Locate the Blitz frame-loop boundary, task/UI timers, song/audio clock setter,
  clock filter and draw/swap path using strings, callers, generated PPC and RB3
  structure. Record addresses, field layouts, evidence and confidence in
  [symbols.md](../../reference/symbols.md); RB3 addresses never transfer.
- Audit Blitz-specific gameplay: note judgement and miss detection, lane switches,
  Blitz mode, power-up duration/energy, checkpoints, moving targets and collisions.
  Include Pinball, Flame Notes, Shockwave, Road Rage and runaway/moving-note
  mechanics where present. Inventory fixed increments, frame counters, per-poll
  random trials, rounding and integer-millisecond assumptions.
- Add opt-in, bounded telemetry for real wall time, audio position, task seconds,
  UI seconds, update/draw counts, `VdSwap`/`XE_SWAP`, input and judgement timestamps,
  active power-up state, score and collision events. Do not add an on-screen panel.
- Extend the existing pacing audit with target-rate expectations and drift/event
  comparisons. Its 60 Hz bounds alone would accept an unsuccessful 120 FPS run.
- Build with `cmake --build --preset win-amd64-release`. After the owner's choice
  of who performs the UI test, capture a warm-cache 60 FPS full song and a control
  repeat on the current build. Tag resolution, backend, payload, clocks and limits.

**Gate:** distinguish update cadence from submitted/swapped/displayed cadence;
locate clock ownership; establish the baseline's own run-to-run variation. The
historical 68.3 FPS run does not replace this control.

### P1. Diagnostic rate control and feasibility experiment

First test the existing boot-only `video_mode_refresh_rate` at 75/90/120 with
`vsync=true`, recording the actual cvar and guest-reported mode. This uses the
SDK's existing vblank mechanism without inventing a new clock. Repeat selected
cases with `vsync=false` to expose frame-dependent assumptions under variable load.
All overrides use disposable profiles and remain outside the public setting.

If a precise independent cap is needed, place a monotonic deadline limiter once
per **complete guest main-loop iteration**, at a verified boundary before the
next frame's time sampling. Wait in host time, let the real audio/task clocks
advance naturally, and reset deadlines after pause, loading or long stalls rather
than accumulating catch-up frames. Never sleep once per object, draw call or
audio poll. Verify that waiting on this thread does not starve audio servicing or
input; otherwise choose a verified render/vblank boundary that preserves both.

Keep the roles explicit:

| Policy | Expected behavior |
| --- | --- |
| Original/default | Existing V-Sync and guest-mode behavior, including the present uncapped-off behavior. |
| Explicit cap + V-Sync off | Complete guest iterations no faster than the selected cap; existing 1 ms interrupt cadence is not an FPS claim. |
| Explicit cap + V-Sync on | No faster than both the cap and the verified guest-vblank cadence. Raising only the cap with a 60 Hz guest mode cannot promise 120 FPS. |

Determine and log how a higher **guest refresh** is selected for the high-rate
mode; setting it and limiting frames are separate operations. If this needs SDK
reconfiguration or presentation policy changes, specify a generic patch rather
than hiding them in a setting label. Verify Vulkan's present-mode constraints
separately. Monitor compatibility limits what can be displayed smoothly, not the
numerical correctness of an above-refresh simulation test.

**Gate:** actual sustained guest frames near the intended target on hardware that
can produce them, unchanged audio/task clock speed, no new deadlock, and useful
telemetry at 60 and 120. A CPU/GPU ceiling below the target is an inconclusive
test of that target, not proof that it works.

### P2. Correct demonstrated engine dependencies

Confirm active Blitz camera-shake bodies before changing them. Compare shake
events, damping and envelope per real second. Apply D8 only where needed, then
verify the original 60 FPS appearance as well as the higher-rate path.

Audit particles, damping, authored animation sampling, post-process `emulate_fps`,
any physics step/clamp, and draw-driven or poll-driven cleanup. Distinguish an
authored 30 FPS effect from an unintended speed change. Preserve intentional
effect rates. Use bounded substeps only where numerical behavior requires them;
do not move all game logic to a fixed timestep without evidence that it is needed.

Inspect the Blitz equivalent of `DeJitter` under actual audio sample granularity.
Repeated audio timestamps at high FPS, pause/resume and a changing frame interval
must not shift hit windows or stall song time. Change the filter only with measured
evidence; wholesale removal could worsen audio/visual synchronization.

**Gate:** every observed rate-dependent defect has a verified fix or excludes the
affected rate. If fixing core gameplay would require a wider scheduler rewrite,
stop the public expansion at the last passing rate and document the blocker.

### P3. Settings and loading integration

After the P1 semantics are settled, add a stable setting ID and persisted enum
for **Framerate limit** through
[model.h](../../../src/settings/model.h),
[schema.cpp](../../../src/settings/schema.cpp), the capability snapshot,
[boot.cpp](../../../src/settings/boot.cpp), and the existing authored settings
payload/bridge. Preserve existing profiles with **Original** as the migration
default; explicit **60** and validated **120** are the initial candidate choices.
Do not reuse an existing setting ID. Keep unvalidated choices diagnostic-only.

Explain the effective rate when V-Sync/guest refresh constrains a requested cap.
Persist the player's request, log the effective cap/guest Hz/presentation policy,
and require restart under D6. Test command-line overrides, invalid/old values and
save/cancel. Coordinate with the
[hot-apply plan](2026-10-24-settings-hot-apply-plan.md) without promising a hot
refresh update it has not implemented. Route discovery acceleration through D9.

**Gate:** settings tests cover policy combinations, migration, persistence,
restart classification and temporary-override restoration; one setting does not
silently defeat another.

### P4. Correctness and performance acceptance

Use warm shaders/pipelines for timing comparisons and retain separate cold/stall
tests. Shader-preparation changes can alter achieved FPS without changing engine
safety. Record exact build identity and do not compare different builds as if
framerate were their only difference.

| Test | Acceptance evidence |
| --- | --- |
| 60 versus 75/90/120; later 144/165/240 | Target-aware frame-time distributions and update/swap counts over a complete song, not menu FPS. Record dropped/displayed frames where observable. |
| Song speed and synchronization | Compare audio position and task seconds against wall time, after removing paused time. Predeclare a drift budget (initial target: no more than 10 ms additional drift per minute beyond the repeated 60 FPS control); check end-of-song behavior and pitch/speed. |
| Judgement and score | Deliver the same chart-time inputs with identical calibration at each rate, including hit-window edges, simultaneous notes, misses and lane switches. Event-level expected outcomes must match; differences in score, misses or rule timing block release pending explanation. Merely delivering the same wall-clock script or using autoplay does not prove real input judgement. |
| Power-ups and collisions | Controlled activation, movement, collision/hit and expiry comparisons for all equipped types and interactions. Normalize event timestamps to song time; distinguish random-seed differences from a rate-dependent outcome distribution. |
| Camera, particles and animation | Compare event counts, lifetimes and trajectory/envelope statistics per real second, plus owner visual review. Pixel identity is not expected from randomized effects. |
| Pause, restart, focus loss and long stalls | No time jump, accumulated burst of simulation, stuck effect or audio desynchronization after returning. Include an intentionally overloaded run with changing frame intervals. |
| Backend and content matrix | D3D12 and Vulkan separately, retail and Ultimate separately; stock and DLC/custom content, fast/dense charts, long songs and power-up-heavy scenes. Report unavailable arms as unproven. |
| Discovery/loading transitions | Temporary acceleration never leaks into gameplay; restores latest requested policy on success, cancellation and failure. |
| Input latency and cost | Poll gaps, key/button-to-judgement latency, CPU/GPU frame cost and hitch tails. Higher FPS alone is not evidence of lower input latency; existing polling is already faster than rendering. |

Define event-time tolerances from chart resolution, timestamp precision and the
repeated baseline **before** judging results. Do not broaden tolerances to make a
failed high-rate run pass. For randomized mechanics, use deterministic seeds if
available and event distributions otherwise; a successful song completion is
insufficient physics evidence. Repeat the passing control and high-rate runs.

**Gate:** only passing rates enter the public capability list. If 120 passes but
240 fails, ship 120 and record 240's specific blocker. If no above-60 rate passes,
retain the original behavior and explain the engine dependency rather than
shipping an unsafe unlock.

### P5. Document and close

Update the graphics setting contract/status and
[toggles.md](../../engine/toggles.md), record measured limitations in
[known-issues.md](../../reference/known-issues.md), and update RB3 provenance only
if code is actually adapted. Preserve raw results under
`out/_swap/framerate/<build>/<backend>/<payload>/<rate>/` and summarize dates,
build identity, coverage and failures here. Move this plan to `in-progress/` when
P0 implementation starts and to `finished/` only when the shipped choices pass.
Run relevant non-UI tests, a release build, the documentation checker and
`git diff --check`.

## 6. Remaining uncertainties

- The current executable's achieved in-song FPS and CPU/GPU headroom have not
  been measured here. A new control is necessary before any performance promise.
- Whether Blitz uses RB3's camera-shake body and which particle/physics routes
  are active remain unproven; matching type names alone cannot establish either.
- Blitz-specific judgement, moving power-ups and collisions have not yet been
  proven independent of frame count. RB3's band/instrument logic cannot answer
  those title-specific questions.
- No tested upper safe FPS ceiling is known from this research. 120 is a proposed
  first target; 240 is only inside the SDK's configurable refresh range.
- This is a documentation-only analysis. No build, game navigation or claim of
  newly validated high-FPS behavior is part of it.
