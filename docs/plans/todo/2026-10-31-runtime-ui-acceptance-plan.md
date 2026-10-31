# Runtime UI and settings acceptance

**Status (2026-10-31):** planning only. This groups the current build's open
settings, display and input acceptance routes from
[known-issues.md](../../reference/known-issues.md). Host tests and older owner
observations are retained as evidence but do not establish these live cases.

## Prompt order and execution

| Prompt | Dependency | Result |
| --- | --- | --- |
| U1 | Current Vulkan build and gameplay route | HUD placement/visibility and frame verdict |
| U2 | Installed Ultimate payload | Gold Star Progress restart verdict |
| U3 | Graphics recovery fixture | Guest-drawn recovery note and retry verdict |
| U4 | Load Guard failure/timeout fixtures | 60s and live-row verdicts |
| U5 | Active button-layout plan P4/P9/P10/P11 | Acceptance evidence in that plan |
| U6a | Physical controller | Controls capture/cancel and preset verdict |
| U6b | Installed Ultimate payload | Ultimate Controls and remaining route verdict |

U6a–U6b are the input acceptance work formerly grouped as U6. Prompts are
independent except for their named fixtures; U5 executes within its owning
plan. Build the latest code before any game/UI acceptance. Before navigating
the game or driving its UI, ask whether the owner wants to perform that test
and wait for the answer. Host checks and builds can proceed independently.

### Prompt U1 — Verify HUD placement and visibility

**Starting evidence:** host fixtures cover the reanchoring correction and the
owner confirmed smooth, correctly placed Vulkan gameplay on 2026-10-25. The
hidden/visible and multi-song routes in [graphics.md](../../settings/graphics.md)
§9.2.7–9.2.9 remain unmeasured.

**Do:** compare the six named anchors with Song Progress Bar visible and
hidden, including the personal-best meter. Check startup, pause/resume and a
second song. Capture placement and frame timing against the known healthy
Vulkan route.

**Exit:** the left cluster, comparison meter and visibility transitions are
correct without the earlier frame regression; any mismatch is recorded by
element and route.

### Prompt U2 — Check Ultimate setting persistence through restart

**Starting evidence:** Gold Star Progress saves and reloads in host tests
using the installed payload; [architecture.md](../../settings/architecture.md)
part 5 §4 has no full guest-restart observation.

**Do:** change the setting in Ultimate, leave through the normal commit path,
restart the process and read the effective guest value. Preserve the
before/after store value and boot logs.

**Exit:** the selection survives restart and the guest draws the selected
state, or the adapter/restart boundary is identified for a focused fix.

### Prompt U3 — Explain graphics fallback in the guest menu

**Starting evidence:** the graphics boot journal can recover and retry, but
`settings.graphics.recovered_from_failure` is visible only in the log.
See [graphics.md](../../settings/graphics.md) §5.4 and
[status.md](../../settings/status.md).

**Do:** wire the already specified note into the guest-authored settings
screen for recovery and latched failure. Distinguish requested from effective
values and clear the note only after a successful retry. Cover the state
transitions with focused host checks, then run one failed-boot/retry route.

**Exit:** the menu explains why the effective mode differs, keeps the
requested choice editable, and clears the note after a successful retry.

### Prompt U4 — Finish Load Guard route coverage

**Starting evidence:** the owner's 30s timeout and fatal restart passed;
60s, live row application and broader provider/hardware routes remain
unmeasured in the [Load Guard record](../finished/2026-10-13-load-guard.md)
parts 4–5.

**Do:** verify Off/30s/60s apply at their declared boundary, then exercise a
60s timeout and available provider/hardware cases. Check monitor deadline,
restart and reason-specific native dialog. Record unavailable hardware as
unmeasured.

**Exit:** each exercised case returns to the native main menu with the right
reason and preserves the next playable song; failures get a narrow trace.

### Prompt U5 — Close button-layout acceptance in its owner plan

**Owner:** [button-layout plan](../in-progress/2026-10-19-button-layout-hotswap-plan.md)
P4/P9/P10/P11. This prompt is a handoff, not a second implementation scope.

**Do:** finish its live debounce and Vulkan family switching, physical
device/v2 images, in-song and >45-source behavior, and guest remap-aware
hints. Put screenshots, input identities and backend verdicts in that plan's
acceptance matrix.

**Exit:** the owner plan identifies every measured route and keeps its
45-source textual fallback explicit; unavailable devices stay unmeasured.

### Prompt U6a — Verify physical-controller Controls routes

**Starting evidence:** scripted menu/pause coverage exists, but real-pad
capture/cancel, stick-direction capture and preset switch → next song are
unmeasured. See [input-flow.md](../../engine/input-flow.md) and the
[mapping record](../finished/2026-10-17-gameplay-input-mapping-plan.md) §18.

**Do:** use a physical pad to capture, cancel and change a preset on the
retail page, then start a song and compare the observed preset with the
mapping applied. Check the connected-pad recovery arm.

**Exit:** the real input path and next-song preset have a route log and
verdict. If no suitable pad is available, retain the unmeasured status.

### Prompt U6b — Verify Ultimate and remaining Controls routes

**Do:** run the in-song Controls page on the installed Ultimate payload,
including return to pause/gameplay. Exercise focus loss, reset paths,
capture timeout, mouse capture and a keyboard-recovery refusal under a
locked setting where the available harness permits. Keep host-only cases
distinct from live observations.

**Exit:** each exercised arm has a current verdict; any remaining untested
arm is listed explicitly rather than inferred from the base-game route.

Post-A shader loading and renderer-specific recovery belong to the
[engine compatibility plan](2026-10-31-engine-compatibility-closeout-plan.md).
Resolution expansion and settings hot apply have existing
[resolution](2026-10-19-resolution-scale-expansion-plan.md) and
[hot-apply](2026-10-24-settings-hot-apply-plan.md) plans. Multi-DPI window fit,
installer migration from the retired Controller Settings profile and installer
caption automation are later release checks, not blockers claimed by this plan.
