# Song Load Guard: implementation prompts

**2026-10-13 — proposed work, not completed implementation.** Read
[the findings and behavior contract](2026-10-13-load-guard-findings.md) before using these prompts.
They implement the cooperative timeout and individually validated load-error cases. They do not
promise recovery from arbitrary access violations, memory corruption, or a deadlocked guest UI.

Run these sequentially in the same checkout, inspecting the evidence at each gate. A failed gate
means record the remaining blocker and keep the unsupported behavior unavailable; it does not
mean fabricate completion or ship an inert setting. Runtime experiments need the local game
dump and disposable test configuration. Preserve existing user changes and data.

## Prompt 1 — Prove the song-load lifecycle and cancellation route

```text
Investigate the cancellation prerequisite for Load Guard in this repository. Read
docs/plans/todo/2026-10-13-load-guard-findings.md and docs/engine/loading-cancel.md first. This step
may add narrowly scoped, disabled-by-default diagnostics and a disposable harness;
do not expose the player setting yet.

Trace an actual song load from its start through native completion, waiting for A,
automatic start, manual cancellation, and screen exit. Cover retail and Ultimate
where the local installation permits. Record the guest thread, exact ABI and call
ordering for each lifecycle event; establish whether work begins before screen enter.
Find how B really leaves an unfinished load, including who owns in-flight reads,
decoders, game objects, and late callbacks. The loading screen's local button handler
only handles Confirm/Start: do not assume it implements cancellation itself.

Reuse the existing sub_824B2958 hook in src/input/native_menu.cpp for observations
if appropriate; do not define a duplicate strong hook. Reverify sub_822C45A8 and
sub_822C38F8 against generated code. Never call completion to force an abort, edit
generated sources, or run guest UI calls from the pad or host UI thread. Forward
observed original functions exactly once and preserve caller arguments/results.

Reproduce a genuinely unfinished song load, or add a one-attempt development-only
stall at a documented phase which still permits the real cancellation/cleanup path.
The old missing-milo example was a failed but completed load. Suppressing a final
notification alone proves only that artificial state, not unfinished I/O cancellation.
Do not alter installed DLC or saves to manufacture the reproducer.

Measure manual cancellation, then the same native cancellation requested once at
a verified guest safe point. Prove resource/callback cleanup. If cancellation lands
on song_select_screen, establish the normal transition from there to splash_screen
after cleanup; do not inject repeated B or bypass the screen manager. Then load and
play a known-good song in the same process. Exercise cancellation more than once.

Write docs/plans/load-guard-runtime-findings.md with exact hooks/ABIs, logs/capture
locations, cleanup evidence, provider differences, and a go/no-go decision. Separate
static inference, old measurements, controlled-stall evidence, and real fault evidence.
If no safe cancellation route exists, explain the specific blocker and stop before
implementing an automatic abort or advertising the setting.
```

## Prompt 2 — Implement the controller and cooperative timeout

```text
Implement the controller for Load Guard using docs/plans/todo/2026-10-13-load-guard-findings.md and
the runtime evidence from Prompt 1. Proceed with guest cancellation only if that
evidence proves a safe route and subsequent good-song playback.

Add an SDK-free state machine with injected monotonic time and a new generation per
load attempt. Policy is Off, 30s, or 60s and is snapshotted at Begin. The timer measures
total load time, never resets on cosmetic or I/O progress, and stops on native
completion even when the game is still waiting for A. Exclude boot, discovery,
settings loading, menu music, results, and gameplay. Cover auto-start and manual exit.

Use the measured lifecycle signals and guest safe point. Request native cancellation
once, wait for the verified cleanup acknowledgement, and transition to splash_screen
through normal UI flow. Do not free loader state yourself. Do not keep guest pointers
in host queues. Observe the current generation, screen and transition state before
acting, and reject stale work. Normal completion before cancellation commitment wins
over a timeout-only request. A known supported failure survives its native completion;
distinguish native failure return from manual exit and do not cancel an already cleaned
up attempt again. After commitment, use the verified native protocol to keep late
completion from starting gameplay while still allowing required cleanup to run.

Do not add a global exception handler, force-finish the load, terminate a guest
thread, longjmp out of it, or dispatch guest UI on a watchdog/pad/host UI thread.
A host watchdog is optional diagnostics only; it cannot make a blocked guest thread
recover. Keep a bounded cancellation/transition grace period and report unsuccessful
recovery once without another cancel loop or a success popup. Confirm the documented
focus-loss and explicit suspend/resume timing policy, or document a concrete remaining
platform limitation before claiming support.

Use development-only configuration until settings and popup integration are ready.
Add meaningful fake-clock tests: Off, both boundaries, completion at expiry, wait-for-A,
manual exit, same-address re-entry, stale generation, duplicate error/timeout, failed
but completed loads, late completion after commitment, failed recovery,
suspension/rebase and next-attempt policy.
Wire production sources and tests into CMake. Update runtime findings with measured
behavior; distinguish controller tests from in-game validation.
```

## Prompt 3 — Add the native setting and main-menu popup

```text
Integrate the proven Load Guard controller with settings and native UI. Follow
docs/plans/todo/2026-10-13-load-guard-findings.md. Require Prompt 1's cancellation/cleanup proof and
Prompt 2's functioning controller; do not add a setting that cannot perform recovery.

Add “Load Guard” under Help & Options -> General -> Downloadable Content, with a
horizontal Off / 30s / 60s selector, default Off. Store the enum at general.load_guard
and use that bridge token. Allocate an unused stable SettingId without renumbering
or reusing existing IDs. Wire model, schema, store/service behavior, boot policy,
bridge, generated menu rows, captions, and the actual runtime applier. Use the
existing in-song menu generation too. No second launcher control is needed.

Successful commit publishes the policy for the next load; an active attempt retains
its snapshot. Respect explicit config overrides and existing save-failure behavior.
The hot applier currently supports only VSync's bool: extend it deliberately and
verify effective readback, rather than just marking the schema hot. If adding a
developer cvar, validate only 0/30/60, wire precedence/readback, and publish a cached
policy from host code. Guest hooks must not read the cvar registry. Leave old R11
cvar names retired. Test missing/invalid persisted values and unrelated-key retention.

Use the existing error_popup_panel after splash_screen has finished entering and
the manager is no longer transitioning. Its show_popup takes a locale token and
return panel; splash_panel is the native main-menu return target. Determine a safe
dispatch path from the measured guest context or a guarded authored DTA handler.
Do not guess host-to-guest DataNode construction or reuse returned stack addresses.

Add project-owned tokens for the 30s/60s timeout and supported-load-error messages
from the findings. Integrate through the existing effective-archive/menu payload
and installed locale processing, preserving Ultimate handlers. Use shape checks;
an incompatible record must leave recovery unavailable with a diagnostic, not leave
a half-installed handler. The popup is consumed once per recovered attempt and
restores focus on dismissal without leaking a confirm press to the menu beneath it.

Until Prompt 4 establishes immediate error support, the help caption must promise
only the timeout. Check the label and message fit in real screenshots at supported
resolutions and with controller/keyboard/mouse input. Keep technical details in logs.
Extend the relevant settings, bridge and payload tests. Demonstrate save/reload,
mouse navigation disabled, native popup appearance, dismissal, and a good song after
recovery in retail and Ultimate wherever available. Record remaining coverage gaps.
```

## Prompt 4 — Handle specific load errors before they become fatal

```text
Extend Load Guard with immediate failure handling only at proven recoverable song-load
boundaries. Read the findings and prior runtime evidence; do not implement universal
crash interception. The enabled 30s and 60s policies both react immediately to supported
errors. Off leaves native handling unchanged, while independent correctness fixes
such as bounds validation must remain effective.

For each candidate (required asset open/read failure, supported parser/decoder error,
or a typed host exception in a contained operation), trace the actual result contract,
guest thread, ownership, locks and cleanup. Identify the normal failed-load return
path before changing code. A log warning, optional missing file, or ordinary SDK/MMIO
fault is not enough to classify a failed song. Do not scan the whole DLC library on
each load. Keep checks local to the selected song and the operation consuming data.

Catch only a specific exception at a boundary whose unwind and resource invariants
are proven. Do not wrap the whole guest poll in catch(...), swallow RtlRaiseException,
install a broad VEH, change compiler exception flags as a substitute for cleanup,
advance past a faulting instruction, or jump into the menu. If no safe boundary is
found, document that no exception interception is supported and retain timeout-only
behavior/help; do not simulate a successful implementation.

Report recoverable errors with attempt generation to the existing guest-thread
controller, then use exactly the same verified cleanup/main-menu/popup path. Ensure
timeout plus error yields only one recovery and native failed-load completion cannot
erase a latched error or trigger a second cleanup. Log a bounded reason/song identifier;
do not assert that all bad DLC or internal exceptions are handled.

Add focused tests and one real or safely injected end-to-end reproduction per
supported error. Show immediate recovery in both enabled modes, no feature-driven
recovery in Off, and successful subsequent good-song playback. Maintain a supported
error table in load-guard-runtime-findings.md; update the help caption only to match
coverage actually established. Keep generic crash/restart containment out of scope.
```

## Prompt 5 — Acceptance and final coverage report

```text
Validate the implemented Load Guard against section 5 of
docs/plans/todo/2026-10-13-load-guard-findings.md. Read the runtime findings first; preserve unrelated
work. Run the affected tests and the configured project's required checks, then the
in-game acceptance matrix. Use actual 30s and 60s values for final timing acceptance;
fake-clock tests and short diagnostic delays alone are insufficient.

Cover Off, normal loads, genuine/controlled unfinished loads, long wait-for-A,
completion at expiry, manual cancellation, same-song re-entry, stale callbacks,
timeout/error races, and unresponsive cancellation. Cover every claimed immediate
error separately. Record load start, completion or expiry, cancellation, cleanup,
main-menu-ready and popup timestamps for each attempt. Confirm exactly-once guest
completion and popup behavior, then play a good song after each recovery family.
Repeat failures enough to check for accumulating resources, stuck audio or unusable
input; verify that recovery does not damage existing save/progress behavior.

Check retail and Ultimate, relevant song-entry routes, bundled and DLC songs, settings
save/reload/override/failure, next-attempt application, mouse support off, popup input,
resolution/text fit, focus loss, and supported suspension/resume. Label unavailable
configurations (including a nonbooting demo) untested rather than passed. A GPU freeze
or a blocked guest UI is not evidence of successful cooperative timeout recovery.

Keep diagnostics off by default, remove temporary invasive experiments, and leave
retail binaries/decoded assets and captures out of version control. Update the
findings/runtime report and README with only the supported behavior and evidence.
Report unresolved cancellation or cleanup failures as release blockers. Do not mark
the feature done if it merely changes screens while work from the failed load can
still access old objects, or if it cannot load a good song afterward.
```
