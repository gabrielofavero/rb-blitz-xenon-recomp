# Song Load Guard

Song Load Guard is recorded in five parts: **part 1** is the feasibility study — which stall and
failure cases a cooperative 30s/60s timeout can recover from in-process, and which it must refuse —
**part 2** is the implementation prompts written against it, and **part 3** is what the 2026-10-25
implementation attempt actually built and measured. **Part 4** records the 2026-10-26 crash
diagnosis, independent MIDI fix and owner-authorized external crash restart. **Part 5** records
the owner's subsequent choice to use external restart for overdue loads too. Off/30s/60s,
next-load live application, external timeout and the native reason-specific popup are implemented.
**Finished 2026-10-26:** the owner confirmed timeout restart, the native dialog,
enabled background music and subsequent playback. **Same-process cancellation was superseded
by the owner's restart decision, and is not shipped or claimed safe.**

Parts 1–3 preserve the original proposal and its cancellation safety gate as historical evidence.
Parts 4–5 describe the delivered design and its measured scope. The owner confirmed fatal-load
restart and successful Can't Hold Us playback; part 5 records the final timeout acceptance.

**First reproducer candidate (owner report, 2026-10-24):** *Can't Hold Us* by Macklemore & Ryan Lewis
was reproduced under a debugger on 2026-10-26. Its optional LIPSYNC1 MIDI track overflows a
signed event counter; see part 4 §1. The package's stored data is intact (part 3 §2).

## Sources

| Part | Original document | Status |
| --- | --- | --- |
| 1 | `docs/plans/todo/2026-10-13-load-guard-findings.md` | Research only (2026-10-13) — no runtime feature implemented |
| 2 | `docs/plans/todo/2026-10-13-load-guard-prompts.md` | Proposed work (2026-10-13) — Prompt 1 partly run, Prompts 2–5 not run |
| 3 | `docs/plans/in-progress/2026-10-25-load-guard-runtime-findings.md` | Implementation evidence (2026-10-25) — controller and trace hooks landed, automatic recovery deliberately disconnected |

Parts 1 and 2 were merged into this document on 2026-10-22, and part 3 on 2026-10-26 (its source
already lived beside this file, so nothing moved between folders). Git history before each merge
holds the originals. Each part keeps its source's own section numbers, demoted one heading level, so
a citation such as "part 1 §2.4" still lands. Cite the result as
"`docs/plans/finished/2026-10-13-load-guard.md` part 1 §5".

---

## Part 1 — Feasibility and proposed behavior (was `docs/plans/todo/2026-10-13-load-guard-findings.md`)

**Date:** 2026-10-13. **Status:** research only; no runtime feature implemented.
Reviewed project revision `b67a0fb1c04b99962ea440fa14d5d46197a9b1cd`, the locally patched
SDK at `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`, generated guest functions, and locally
extracted Xbox 360 UI records. The implementation prompts in part 2 follow this study.

### 1. Finding

**A cooperative song-load timeout is a plausible implementation, subject to proving cancellation
and cleanup in a running game. A general “catch any crash and return to the menu” feature is not
supported by the current runtime.** These are different failure cases and need different handling.

| Situation during a song load | Assessment | Required behavior or limitation |
| --- | --- | --- |
| Loading never completes, but guest UI updates continue | Feasible candidate | At 30 or 60 seconds, request the title's normal cancellation, finish cleanup, reach the main menu, then show a popup. The cancellation contract still needs measurement. |
| A known missing/invalid asset or recoverable loader error is reported | Possible case by case | Convert the specific error into a normal failed-load result and use the same cancellation path immediately. Each failure needs a proven return/cleanup contract. |
| A host C++ exception inside a deliberately isolated load operation | Possible only with a documented boundary | Catch a specific supported exception where that operation can unwind safely and report failure. No such general song-load boundary was established here. |
| Guest access violation, heap corruption, stack overflow, fatal assertion, or arbitrary internal exception | No general safe recovery established | Detecting a fault does not restore loader state, locks, guest stack, or resources. Do not claim success or continue the corrupted process. |
| The guest UI thread is blocked or spinning inside the loader | Timeout detection alone cannot recover | A host watchdog could notice missing progress, but the thread needed to cancel cannot process the request. Requires cooperative cancellation inside the blocking operation, or a separate restart design. |
| GPU stops presenting while other threads run | Outside this feature | A stationary picture is not proof of a song-load stall. An in-game popup may also be impossible to draw. |

The requested destination is **the main menu (`splash_screen`)**, not the song browser
(`song_select_screen`). The earlier experiment only demonstrated returning to the browser.
Reaching it is useful intermediate evidence, but does not satisfy this request.

### 2. What was actually checked

This investigation was static/read-only apart from these documentation changes. It did **not**
launch the game, inject faults, reproduce an endless load, or validate automatic cancellation.
Historical measurements below are explicitly attributed to the previous investigation; they are
not new results or guarantees for today's build.

#### 2.1 Previous R11 work: useful evidence, with a corrected reproducer

[The R11 record](../../engine/loading-cancel.md) documents a loading-cancel feature removed on
2026-10-07. Its goal was primarily to show a cancel hint. It records:

- The loading message handler `sub_822C45A8` continued running in the observed waiting state;
  its reported call rate was about 65 Hz. The pad read `sub_827266E0` ran on a different guest
  thread. Neither frequency is a timing contract.
- B returned to song selection in the tested loading/prompt states. This suggests a native
  cancellation route worth tracing. It does not establish that every stuck loader is cancellable.
- Its second pass found that the missing `songs/<song>/<song>.milo` example actually completed
  its load attempt after about 1.2 seconds. It is **not a demonstrated infinite-load reproducer**.
  Some earlier “stalled load” interpretations therefore cannot be used as acceptance evidence.
- One access violation was introduced by the experiment itself: a hook executed the guest's
  completion twice. Every observer hook must forward the wrapped original exactly once.
- Directly invoking the completion evaluator with incomplete arguments faulted. Forcing completion
  is neither a cancellation API nor a safe way to display UI.

Keep that document as history. This proposal does not restore its removed timeout cvar, toggle,
or prompt-label patch. The new popup occurs after recovery, avoiding the earlier problem of
trying to make a hidden loading-screen label visible.

#### 2.2 Current loading-screen data and generated code

The local `ui/loading/gen/loading.dtb` and `ui/song_select/gen/song_select.dtb` confirm these
relationships. The extracted records are ignored retail content and must not be committed.

| Observation | Consequence |
| --- | --- |
| `loading_screen` is a `LoadingScreen` with `loading_panel` and `game_panel` among its panels | An image that looks like loading is insufficient; identify this screen and an actual song-load attempt. |
| Its `enter` runs `load_gems`, resets `is_load_done` to false, and selects the loading state | Instrument the start before potentially blocking load work. Discover whether native loading starts even earlier; do not rely solely on the first completed UI poll. |
| `wait_for_button_press` defaults to false in the definition; the inspected offline song-select route sets it true | Cover both automatic-start and wait-for-confirm paths. |
| `load_finished` either sets `is_load_done` and enters `waiting_for_button`, or calls `start_game` | The deadline ends on native load completion, even if the screen remains visible indefinitely waiting for A. |
| `start_game` clears the wait flag and requests `game_screen` | Normal completion must win over a pending timeout before cancellation is committed. |
| The loading screen's `BUTTON_DOWN_MSG` handles Confirm/Start when ready and otherwise returns unhandled | B's effective cancellation path must be traced outside this local handler. Do not invent a `cancel_load` method from its name. |
| Song selection's Cancel route exits search mode first if active; otherwise it requests `splash_screen` | Repeated synthetic B presses are not a deterministic main-menu transition. |

A fast failed-but-completed load will never reach the deadline. Handling it immediately requires
a separate supported error signal; observing `load_finished` alone does not prove the song is playable.

Generated code also still contains `sub_822C45A8` and `sub_822C38F8`. In the former, the call
returning at guest PC `0x822C47BC` prepares result/message storage on its own frame before
calling the latter. `sub_822C38F8` itself invokes other guest code and writes screen state.
Treat it as a completion observation candidate, **not** a public abort or recovery entry point.
Observe a confirmed completion after its original call returns; entry alone does not prove success.

The generated files inspected were `generated/default/rb_blitz_recomp.6.cpp`,
`rb_blitz_recomp.84.cpp`, and `rb_blitz_recomp.67.cpp`. These filenames are generation artifacts;
find the functions by address after regeneration. Never implement fixes by editing these files.

#### 2.3 A guest UI observation point already exists

[native_menu.cpp](../../../src/input/native_menu.cpp) owns the strong hook for
`sub_824B2958` (`UIManager::Poll`). It polls mouse input, calls the original, updates its screen
observation, and calls [HelpObserverTick](../../../src/hooks/help_observer.cpp). This is a good
candidate for sharing the existing hook with a load observer; do not add a second strong definition.

Current observation reads the manager's screen at `+44` and transition state at `+16`.
Those are useful ABI leads, not sufficient proof that cancellation is safe at any point in the
hook. Identify the precise ordering during the implementation spike. Native mouse availability
and window focus must not control the load guard.

Distinguish the **guest UI thread**, which can run guest screen transitions, from the **host UI
thread**, which draws overlays and owns the window. `CallInUIThreadDeferred` is not permission
to call guest DTA from the host UI thread. A pad thread, watchdog, or exception callback may
publish bounded host data; it must not dereference screen objects or invoke guest cleanup.

#### 2.4 The main menu already has an error popup

The inspected local `ui/error/gen/error_general.dtb` declares `error_popup_panel`:

- `show_popup(error_token, ret_panel)` stores the return panel, sets `error.lbl` through
  `set_token_fmt`, shows `all.grp`, and focuses the popup on the current screen.
- Confirm hides it and restores the stored return panel.
- `ui/splash/gen/splash.dtb` includes this panel and already calls it with `splash_panel`
  as the return target for errors such as unavailable achievements.

This establishes a native presentation mechanism. A likely final script operation is
`{error_popup_panel show_popup rb_load_timeout_30 splash_panel}` once the main menu is ready.
That is an existing DTA shape with a proposed new locale token, **not a tested recovery call**.
The dispatch mechanism, timing, and behavior with Ultimate still need validation. Native popup
entry hides the popup, so showing it before screen entry finishes could immediately erase it.

The project now authors and mounts modified UI records:
[dtb_author.cpp](../../../src/ui/dtb_author.cpp),
[menu_payload.cpp](../../../src/settings/menu_payload.cpp), and
[settings_payload.cpp](../../../src/hooks/settings_payload.cpp). They can supply project-owned
locale tokens and narrowly guarded lifecycle handlers while preserving the effective provider
archive. [settings_screen.cpp](../../../src/ui/settings_screen.cpp) also builds native confirmation
screens. Prefer the existing popup for this small error message; a new screen is a fallback if
its requirements cannot be met. No live scan/overwrite of locale memory is needed.

#### 2.5 The runtime does not provide transactional crash recovery

The checked [Windows exception handler](../../../rexglue-sdk/src/core/exception_handler_win.cpp)
passes access violations and illegal instructions through an ordered callback table. Other
exception codes immediately continue the OS search. Returning true writes the supplied CPU
context back and returns `EXCEPTION_CONTINUE_EXECUTION`. Unchanged context retries the same
instruction; changing its instruction pointer does not repair engine state.

[xmemory.cpp](../../../rexglue-sdk/src/system/xmemory.cpp) already uses faults for memory handling
and logs an unhandled guest access violation when it cannot service one. Legitimate SDK/MMIO
fault handling must not be intercepted as a failed song. A callback that merely queues “go to
menu” and returns false does not stop an unhandled fault from terminating the process either.

[xboxkrnl_debug.cpp](../../../rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_debug.cpp) is another important
case: guest `RtlRaiseException` routes the guest C++ exception code to `HandleCppException`,
which ends in a debug break; the generic path still has an unwinding TODO. A guest C++ exception
is not automatically a host C++ exception that a new `catch` block around a hook can repair.

The [codegen context-jump bridge](../../../rexglue-sdk/resources/templates/codegen/pch_h.inja) uses
a thread-local map of host `jmp_buf`s and aborts when `ppc_longjmp` cannot find a buffer.
An earlier hook frame that has returned is not a valid recovery target. Adding a persistent
outer frame would still require proving guest stack restoration, lock release, object ownership,
and cancellation across worker threads; the context jump does not supply those guarantees.

This matches Microsoft's description of [vectored exception handling](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling):
handlers run before stack unwinding and are not tied to a recovery frame. Its
[exception handling model documentation](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170)
also distinguishes ordinary C++ catches from asynchronous faults and warns that continuing after
arbitrary asynchronous exceptions can corrupt the process. This project builds with Clang; the
MSVC compiler switches are background on the distinction, not a proposed build configuration.

**Decision:** no global catch-all handler, forced instruction skip, thread termination, fabricated
load completion, or jump into the menu loop. Fix specific bad-input paths before a fatal fault,
or return a supported error at an established operation boundary.

#### 2.6 A song reported to crash: *Can't Hold Us* (owner report, 2026-10-24)

The owner reports that **Macklemore & Ryan Lewis - Can't Hold Us** takes the game down, and named it
as a good song to test a load guard against. This is a **report, not a measurement**: no build hash,
log, failure phase or fault code is attached yet, and nothing in this tree has reproduced it. It is
recorded because it is a real song from a player's library rather than a manufactured missing-file
package, and because Prompt 1 has to answer these questions about it:

| question | why it decides whether the guard applies |
| --- | --- |
| Which phase fails - package mount/STFS read, the `loading_screen`, the `load_finished` completion, or the first `game_screen` frame? | Only a load-phase failure is this feature's subject; a fault after gameplay starts is not a song-load stall. |
| Does the guest UI keep updating while it fails or stalls? | The cooperative deadline can only be acted on from a guest UI update (§4.1); a blocked UI thread needs the restart design, not this guard. |
| Does B leave the screen, as R11 measured on a stuck load (§2.1)? | The native cancellation route is the gate for any automatic abort. |
| Is the failure a returned loader error, or a guest access violation? | A fatal guest fault is outside the guard by §1: detecting it does not restore loader state. Fix the fault itself first, under Prompt 4's boundary rule. |
| Does a known-good song still load and play in the same process afterwards? | §5's repeated-failure test; a guard that leaves the process unusable has not recovered. |

Record the answers here with the build hash and the log path before this song is used as acceptance
evidence. If it turns out to be a fatal fault rather than an unfinished or failed load, it belongs in
`docs/reference/known-issues.md` as a correctness defect and **not** in this feature's test matrix.

**Update (2026-10-26, part 3):** part 3 §2 is the read-only inspection of this package — every stored
block matched its digest, and the boot log's `cantholdus.pan` STFS warning is explained by that
entry's zero logical length — and part 3 §3 lists which of the questions above the manual run still
owes. The owner report above is still unmeasured; nothing here classifies the song as corrupt.

### 3. Proposed setting and player behavior

| Item | Proposal |
| --- | --- |
| Row label | **Load Guard** |
| Choices | **Off / 30s / 60s** |
| Default | **Off**, preserving existing behavior until explicitly enabled |
| Location | Help & Options → General → Downloadable Content |
| Help caption | “Return to the main menu if a song takes too long to load or a supported load error occurs.” |
| Native storage | `[general] load_guard = "off"`, `"30s"`, or `"60s"` |
| Setting/bridge token | `general.load_guard` |
| Provider | Native: available for retail and Ultimate; loading logic also covers bundled songs |
| Apply policy | Live for the next load, after a successful settings commit; each attempt snapshots the policy |

“Load Guard” is ten characters and narrower than “Intercept Crashes”. Confirm its actual rendered
fit at the supported resolutions; character count alone does not prove fit. If implementation
ships timeout handling before any immediate error handling is validated, shorten the help caption
to “Return to the main menu if a song takes too long to load.” Do not advertise missing behavior.

Enabled means **both** the chosen deadline and immediate handling of explicitly supported load
errors. Off disables this feature's automatic timeout/error recovery and popup. Existing native
error handling and independent correctness fixes remain in effect. There is no promise that every
exception is recoverable, and no automatic retry of the failed song.

#### Timing and scope

1. Create a new attempt identity when a song load actually begins, before the first relevant
   potentially blocking load call. Map the exact lifecycle signal in the spike. Merely selecting
   a song or seeing a screen pointer is insufficient.
2. Use a monotonic clock and an absolute deadline of 30 or 60 seconds. This is total load duration,
   **not** “30 seconds since the last animation, read, or progress-bar change”. Slow progress does
   not reset it. The 60-second option accommodates slower legitimate loads.
3. Stop the deadline on native completion, manual cancellation, or departure from that attempt.
   Waiting at “Press A to begin” does not count. Handle the auto-start path too. Keep an already
   established supported failure pending through its normal failed-load completion/return;
   completion does not turn a known failed attempt into a successful one.
4. Expiry requests cancellation at the next verified safe guest UI update. It is not a guarantee
   that the main menu is visible at exactly 30.000 or 60.000 seconds; cleanup and transitions take
   additional time. If that thread cannot run, same-process recovery cannot be guaranteed.
5. Changing the option affects the next attempt. A failed commit does not change the live policy.
   Brief focus loss does not reset the deadline. Explicit process/emulator suspension or system
   resume must invalidate/rebase timing before evaluating expiry so user-paused time does not
   cause a surprise abort; establish the available lifecycle signal before claiming this behavior.
6. Exclude boot, DLC discovery/cache refresh, audio/video settings initialization, menu music,
   results, and active gameplay. Trace tutorial/restart-song entry paths explicitly before
   claiming coverage. A known boot GPU freeze in [known-issues.md](../../reference/known-issues.md) is not a
   valid timeout acceptance case for this feature.

#### Recovery and popup

The recovery sequence is: record the reason once → cancel through the title's verified route →
finish or detach the old attempt safely → transition to `splash_screen` → verify screen entry
and no pending transition → show the popup once. If native cancellation first returns to song
selection, wait for that transition and its cleanup before requesting the next one.

Suggested messages, supplied as new locale tokens:

| Reason | Popup text |
| --- | --- |
| 30-second deadline | “The song did not finish loading within 30 seconds. Returned to the main menu. Try another song or check this song's files.” |
| 60-second deadline | Same text with “60 seconds”. |
| Supported load error | “The song could not be loaded. Returned to the main menu. Try another song or check this song's files.” |

Use the popup's existing confirm-to-dismiss behavior and restore focus to `splash_panel`.
Only say “Returned” after the destination is confirmed. Keep guest addresses, exception numbers,
file paths, and stack traces in logs. A timeout alone is not evidence that a DLC is corrupt.
Avoid dynamic song titles initially: the existing popup takes a locale token, and its text fitting
and format-argument contract should not be guessed. The log can name the song/package.

### 4. Implementation shape

#### 4.1 Controller and thread ownership

Add a small SDK-free controller, for example `src/loading/load_guard.{h,cpp}`, driven by injected
time and semantic events. These names are proposals, not existing APIs:

```text
Idle -- Begin(attempt, policy, deadline) --> Watching
Watching -- Completed without supported error / ManualExit --> Idle
Watching -- Deadline / SupportedError --> AbortRequested
AbortRequested -- timeout only, completion before cancel commitment --> Idle
AbortRequested -- safe cancellation dispatched once --> Cancelling
Cancelling -- cleanup acknowledged --> ReturningToMenu
ReturningToMenu -- main menu ready --> PopupPending
PopupPending -- shown once --> Idle
```

If cancellation/transition cannot complete within a separately bounded recovery grace period
(proposal: 10 seconds from cancellation dispatch), record `recovery_unavailable` once and stop
issuing recovery operations. Do not claim success, free live objects, or endlessly inject input.
This is a diagnostic limit, not another player setting and not proof the process can be restarted.
Use an available host notice only if the host remains healthy; otherwise retain diagnostics for
the next launch. The grace period itself cannot execute on a permanently blocked guest thread.

Assign each attempt a monotonically increasing generation, even when the engine reuses the
same screen and song. Carry it with timeout/error/recovery messages. Validate generation and
current lifecycle at consumption. Copy diagnostic text while valid; do not retain raw guest
pointers in host queues. A generation check protects this controller, but **does not cancel
native loader callbacks**. Prove their cleanup, detachment, or safe completion separately.

Run lifecycle decisions and guest operations on the guest UI thread. An optional host watchdog
may read a published attempt/heartbeat snapshot and queue a request. It adds no ability to
interrupt an unsafe guest stack and must never execute guest calls. A separate timer thread is
unnecessary for the first cooperative implementation; elapsed monotonic time on the existing
poll is enough to request expiry when that poll continues.

Re-check completion and manual exit immediately before committing cancellation. A completed load
defeats a timeout-only request; a latched supported error still needs its failure outcome, even if
the engine emits completion. Distinguish a native failure return from a player's manual exit.
If native failure already cleaned up and left the screen, advance from that acknowledged state
rather than cancelling it again. Once cancellation is committed, the native abort protocol must
prevent late completion from starting gameplay or accessing retired state. Do not drop arbitrary
guest callbacks: some are needed to release resources.

Log attempt generation, song identifier, reason, lifecycle phase, elapsed time, guest thread,
cancellation/cleanup outcome, and confirmed destination at state changes rather than every frame.

#### 4.2 Integration map

| Area | Existing files / required work |
| --- | --- |
| Lifecycle and safe point | Share the `UIManager::Poll` hook in [native_menu.cpp](../../../src/input/native_menu.cpp); add a separate loading module. Confirm entry/completion/cancel hooks against generated code. Keep mouse-off behavior independent. |
| Settings identity and schema | Extend [model.h](../../../src/settings/model.h), [model.cpp](../../../src/settings/model.cpp), and [schema.cpp](../../../src/settings/schema.cpp). Allocate a new unused stable ID; do not reuse retired IDs. Use a horizontal enum selector and strict choices. |
| Persisted/live policy | Integrate [boot.cpp](../../../src/settings/boot.cpp), [bridge_sink.cpp](../../../src/settings/bridge_sink.cpp), and [hot_apply_runtime.cpp](../../../src/hooks/hot_apply_runtime.cpp). The current runtime hot applier only knows boolean VSync; adding a schema row alone is insufficient. |
| Configuration | If a developer cvar is useful, use a new validated 0/30/60 mapping and wire precedence/readback through [cvars.cpp](../../../src/settings/cvars.cpp). Cache/publish policy from host configuration code; guest recovery hooks must not query the registry. Do not resurrect R11's removed names. |
| Menu and help | Add the row in [settings_screen.cpp](../../../src/ui/settings_screen.cpp), including its generated in-song counterpart, choices and caption. Use the existing service/bridge for commit, overrides, and save errors. No duplicate launcher setting is needed after its settings cutover. |
| Popup and lifecycle scripts | Extend [menu_payload.cpp](../../../src/settings/menu_payload.cpp) through the existing effective-archive reader in [settings_payload.cpp](../../../src/hooks/settings_payload.cpp), with shape checks and project-owned locale tokens. Preserve provider changes and original handlers. |
| Build/tests | Add the controller/integration sources to [CMakeLists.txt](../../../CMakeLists.txt); add focused controller tests and extend the settings/payload tests affected by the new row. |

Prefer an atomic immutable-policy publication or another bounded host-to-guest snapshot for live
updates. Classify the row as hot only after its actual applier and readback work. If applying it
live proves impossible, make restart-required behavior explicit; do not silently save a setting
that the active controller never sees. Missing/invalid stored values should use the existing
schema reconciliation and retain unknown unrelated settings.

#### 4.3 Immediate errors require their own evidence

For each proposed recoverable error, record: exact caller/callee and guest thread; error signal;
which resources already exist; ownership/locks; native cleanup and expected return value; how
the failure reaches the UI thread; and evidence that the next good song still plays.

Useful candidates include a required file open/read failure or a decoder/parser rejection that
already supports returning failure. A warning in the log is not a typed error signal. A missing
optional asset is not necessarily fatal. Preflight only checks required assets for the selected
song; avoid scanning the whole library at each launch and do not assume preflight can detect all
bad content. Checks against unsafe lengths/pointers belong at the operation that consumes them.

Do not catch `std::exception` around the entire guest poll, translate all SEH faults into C++
exceptions, or simply return from `RtlRaiseException`. No supported immediate exception-recovery
site was proven in this study. The timeout portion can be developed independently, with honest
help text and a documented error-coverage list.

#### 4.4 Process restart is a separate fallback, not this feature's success path

[game_restart.h](../../../src/platform/game_restart.h) and
[rb_blitz_app.h](../../../src/rb_blitz_app.h) already coordinate a healthy settings restart, including
saving, draining refresh work, spawning a child, and waiting for the old process to exit. That
path relies on functioning threads and cannot simply be called from a fatal exception callback.

Broader crash containment would need an external supervisor or a deliberately isolated worker,
bounded crash diagnostics, process-exit detection, a one-shot startup reason, and restart-loop
prevention. It would restart the game, potentially lose unsaved work, and may need the DLC/menu
music path to avoid immediately reusing bad content. It is a separate product behavior and is
not included in the implementation prompts below.

### 5. Evidence required before shipping

The first gate is a current-build cancellation experiment with a **real or controlled unfinished
load**, not the old missing-milo example. Start with the owner-reported *Can't Hold Us* package
(§2.6) and characterize its failure before using it: only an unfinished load or a returned loader
error exercises this feature, while a fatal guest fault belongs to §1's unsupported row and needs a
correctness fix first. A controlled stall must leave native cancellation and cleanup executable;
simply suppressing `load_finished` can leave a completed loader waiting and does not represent all
incomplete loads. Record which phase is stalled.

| Test | Expected evidence |
| --- | --- |
| Off with a slow/stalled load | No automatic cancellation or new popup; normal native behavior preserved. |
| 30s and 60s with incomplete loading and active guest UI | One cancellation request at/after the deadline, confirmed cleanup, main menu, one popup. Record actual cancellation and menu times. |
| Normal fast load; completion immediately before/at deadline | Normal ready/game path; no timeout popup or double completion. |
| Wait at “Press A” for more than 60 seconds | Never aborted after completion. |
| Manual Back before expiry; re-enter same song/screen | No stale timeout, popup, or callback affects the new attempt. |
| Completion/error arriving during cancellation | No gameplay start from the retired attempt; necessary native cleanup still executes once. |
| Each explicitly supported error with both enabled values | Immediate safe failure path without waiting for the deadline; one main-menu popup. |
| Cancellation or transition makes no progress | One bounded failure report; no repeated Back, forged completion, or successful-recovery claim. |
| Guest UI blocked; unrelated GPU/boot stall | Classified outside cooperative recovery; no unsafe cross-thread guest calls. |
| Repeated failure → menu → good song → play | Proves subsequent loading/play, input, audio and saving still work; inspect resource growth over repeated cycles. |
| Retail/Ultimate; offline/other song-entry routes | Provider handlers preserved; routes without evidence marked unvalidated. Demo coverage depends on a working demo boot. |
| Settings save/reload, invalid value, explicit override, failed write | Correct persistence, precedence, default Off, next-attempt policy, and existing save-error UX. |
| Mouse support off; controller/keyboard/mouse dismissal | Guard remains active; popup input restores main-menu focus without activating the row underneath. |
| Focus loss and supported suspend/resume | Timing matches the stated policy; a paused/resumed session does not trigger a spurious abort. |

Use a fake clock for unit tests; no 30/60-second sleeps in tests. Runtime acceptance needs actual
30s/60s settings as well as a short development-only timeout for iteration. Developer injection
must be off by default, scoped to one attempt, and must not mutate installed DLC or save data.
Use the existing `scripts/observe_ui.ps1`, `drive_ui.ps1`, `frame_diff.ps1`, and `ocr_image.ps1`
where applicable; screenshots alone do not prove cleanup or thread safety.

Until cancellation, cleanup, main-menu navigation, and a subsequent good song are demonstrated,
the result remains **conditionally feasible**, not an implemented or validated safety feature.

### 6. Research navigation

The static asset inspection used the existing local scratch decoder `out/_swap/dtb_tree.py`
against the four records named above. That script/output is ignored and is not a dependency for
implementation. For reproducible tools, use the tracked `ui::dtb::Parse` implementation in
[dtb_author.cpp](../../../src/ui/dtb_author.cpp) and locally extracted assets. Do not commit decoded
retail scripts. To locate the current hook bodies:

```powershell
rg -n --no-ignore 'DEFINE_REX_FUNC\(sub_822C(45A8|38F8|3768)' generated/default -g '*.cpp'
rg -n 'REX_HOOK_RAW\(sub_824B2958\)' src
rg -n 'EXCEPTION_CONTINUE|STATUS_ACCESS_VIOLATION' rexglue-sdk/src/core/exception_handler_win.cpp
rg -n 'HandleCppException|RtlRaiseException_entry|unwinding' rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_debug.cpp
```

Run the implementation prompts in part 2 in order. The first can disprove the proposed cancellation
route; later prompts must use its evidence rather than assume the gate passed.

---

## Part 2 — Implementation prompts (was `docs/plans/todo/2026-10-13-load-guard-prompts.md`)

**2026-10-13 — proposed work, not completed implementation.** Read **part 1** (the findings and
behavior contract) before using these prompts. They implement the cooperative timeout and
individually validated load-error cases. They do not promise recovery from arbitrary access
violations, memory corruption, or a deadlocked guest UI.

**Status 2026-10-26:** Prompt 1 has been started and is only partly complete — part 3 records the
controller, the trace hooks and the package inspection it produced, and the runtime characterization it
still owes. Prompts 2–5 have not run. Paths inside the quoted prompt texts that named the old `todo/`
copy of this plan, or a separate runtime-findings file, were re-pointed to this document (part 1 here,
part 3 for the findings) when the records were merged; the prompt wording is otherwise unchanged.

Run these sequentially in the same checkout, inspecting the evidence at each gate. A failed gate
means record the remaining blocker and keep the unsupported behavior unavailable; it does not
mean fabricate completion or ship an inert setting. Runtime experiments need the local game
dump and disposable test configuration. Preserve existing user changes and data.

### Prompt 1 — Prove the song-load lifecycle and cancellation route

```text
Investigate the cancellation prerequisite for Load Guard in this repository. Read
docs/plans/finished/2026-10-13-load-guard.md (part 1) and docs/engine/loading-cancel.md first. This step
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

Start from the owner-reported "Macklemore & Ryan Lewis - Can't Hold Us" song (part 1
§2.6). Boot it with a trace, record the guest thread and the phase it fails in, and
classify the failure as an unfinished load, a returned loader error, or a guest
access violation. Use it as the cancellation reproducer only if the load can still
be left through the title's own B path; a fatal fault is a correctness defect for
known-issues, not evidence for this feature.

Measure manual cancellation, then the same native cancellation requested once at
a verified guest safe point. Prove resource/callback cleanup. If cancellation lands
on song_select_screen, establish the normal transition from there to splash_screen
after cleanup; do not inject repeated B or bypass the screen manager. Then load and
play a known-good song in the same process. Exercise cancellation more than once.

Write the runtime findings into part 3 of this document with exact hooks/ABIs, logs/capture
locations, cleanup evidence, provider differences, and a go/no-go decision. Separate
static inference, old measurements, controlled-stall evidence, and real fault evidence.
If no safe cancellation route exists, explain the specific blocker and stop before
implementing an automatic abort or advertising the setting.
```

### Prompt 2 — Implement the controller and cooperative timeout

```text
Implement the controller for Load Guard using docs/plans/finished/2026-10-13-load-guard.md (part 1) and
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

### Prompt 3 — Add the native setting and main-menu popup

```text
Integrate the proven Load Guard controller with settings and native UI. Follow
docs/plans/finished/2026-10-13-load-guard.md (part 1). Require Prompt 1's cancellation/cleanup proof and
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

### Prompt 4 — Handle specific load errors before they become fatal

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
error table in part 3 of this document; update the help caption only to match
coverage actually established. Keep generic crash/restart containment out of scope.
```

### Prompt 5 — Acceptance and final coverage report

```text
Validate the implemented Load Guard against section 5 of
docs/plans/finished/2026-10-13-load-guard.md (part 1). Read the runtime findings first; preserve unrelated
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

---

## Part 3 — Implementation evidence (was `docs/plans/in-progress/2026-10-25-load-guard-runtime-findings.md`)

**2026-10-25 — in progress.** Implements the controller and read-only prerequisites of part 1 (§3-§4)
and Prompt 1 of part 2. Automatic recovery, its setting and its popup are **not connected**: native
cancellation and cleanup still need measurement. Controller unit tests do not establish that it is
safe to cancel the guest loader.

### 1. Current implementation

`src/loading/load_guard.{h,cpp}`
([header](../../../src/loading/load_guard.h), [source](../../../src/loading/load_guard.cpp)) is an
SDK-free state machine with injected monotonic milliseconds, per-attempt policy and generation,
Off/30s/60s deadlines, completion-before-commit precedence, latched supported errors, explicit
cancellation/cleanup/return/popup acknowledgements, a ten-second recovery budget, and explicit
suspension rebasing. It has no worker, guest pointers, input injection, exception handler or resource
cleanup of its own. The production guest adapter is deliberately still absent.

[load_trace.cpp](../../../src/loading/load_trace.cpp) supplies pass-through hooks. `song_load_trace=true`
is a restart-scoped, disabled-by-default diagnostic. Configuration is read on the host before guest
launch; wrappers never query cvars or call an additional guest function. The existing
`UIManager::Poll` hook supplies the read-only heartbeat independently of mouse navigation.

| Address | Static ABI / observation | Evidence status |
| --- | --- | --- |
| `0x822C3428` | `r3=LoadingScreen`; setup enter/return, before/after the native body | Static: slot +116 of vtable `0x8203DD64`; earliest selected setup candidate, not yet a proven full-load start |
| `0x822C3878` | `r3=LoadingScreen`; sets native state `+72` to 3 and launches the game load | Static: native body and callers in `0x822C39D0` |
| `0x822C38F8` | `r3=result cell`, `r4=LoadingScreen`, `r5=caller-prepared message`; completion enter/return | Static: original called exactly once; no fabricated messages |
| `0x822C3F48` | `r3=LoadingScreen`, original `r4` retained; unload enter/return | Static: slot +124; clears content subscription, vector, native state and invokes base unload; does not by itself prove all cleanup |
| `0x824B2958` | `r3=UIManager`; observer runs after the original poll | Shared existing strong hook; reads current screen `+44` and transition `+16` |

The trace reports generation, monotonic elapsed time, guest thread, caller, screen and
native state. Bookkeeping is thread-local: cross-thread lifecycle events, if found, must
be reconciled explicitly rather than silently assuming UI ownership. After unload, the
observer stops reading the old screen. These hooks observe; none cancels, suppresses a
late callback, navigates or catches a fault.

### 2. Can't Hold Us package investigation

**Read-only inspection, 2026-10-25.** Local package:
`Macklemore & Ryan Lewis - Can't Hold Us (ft Ray Dalton)`, 22,573,056 bytes,
SHA-256 `e736c981e1ec87577a3d13d37f90efce281c8cb2211327d2c5bd5e630e28d983`.
Ignored extracted inspection inputs are under `out/load-guard/`.

All blocks consumed by the seven file entries matched their stored STFS SHA-1 digests.
No required declared data extent ran off its chain. This checks stored-byte consistency,
not package authenticity, script compatibility or successful game decoding.

| Entry under `songs/` | Declared size | Finding |
| --- | ---: | --- |
| `cantholdus/gen/cantholdus.milo_xbox` | 127,701 | Present, stored ChunkStream header |
| `cantholdus/gen/cantholdus_keep.png_xbox` | 349,588 | Present |
| `cantholdus/gen/cantholdus_weights.bin` | 15,706 | Present |
| `cantholdus/cantholdus.mid` | 929,681 | MThd, 12 MTrk extents covering the file, each ending with end-of-track |
| `cantholdus/cantholdus.mogg` | 20,945,466 | Unencrypted version 10, OggS at declared offset 4,660; Vorbis header declares 10 channels at 44,100 Hz |
| `cantholdus/cantholdus.pan` | 0 | One allocated block with zero logical bytes |
| `songs.dta` | 2,967 | Present; custom metadata declares `game_origin rb4_dlc` and a `context` script accessing `$world`/PostProc |

The MIDI contains the drum/bass/guitar/vocals/harmony/BEAT/VENUE/EVENTS tracks and a
769,290-byte LIPSYNC1 track. Header/extent checks do not validate every event or prove
that Blitz supports this custom conversion. Metadata pans/vols/cores each have ten values,
consistent with the audio channel count. The context script is an investigation lead,
**not an established crash cause**.

Existing boot `out/build/win-amd64-release/logs/rb_blitz_342.log` contains
`STFS failed to read correct block-chain for entry cantholdus.pan, read 0 blocks, expected 1`.
The SDK constructs readable blocks from logical file length and then compares their count
to allocated blocks. Zero length explains this warning without establishing damaged data.
That boot continues opening other packages afterwards; the warning is not evidence of
the owner's song-start failure. No package was modified or blacklisted.

### 3. Runtime test handoff

The owner chose to navigate/test manually and requested that Codex boot the game.
The rebuilt executable was launched with `--song_load_trace=true --log_flush_interval=1`,
using the normal library and preferences. No synthetic input was sent.

Diagnostic executable SHA-256:
`4d05daf521bae40a3e3078ec285ec7d69c06e5a7662cdd599f85997f49bc0d33`.
Active handoff log: `out/build/win-amd64-release/logs/rb_blitz_349.log`.
Unhandled-fault capture: `out/load-guard/manual-fault.log` and, if a fault occurs,
`out/load-guard/manual-fault.dmp`. The debugger passes first-chance access violations
through to the runtime; it supplies diagnostics, never recovery.

Initial sandboxed launches failed before guest launch because normal settings/save paths
were inaccessible (`HostPathDevice::Initialize` filesystem exception). Launching with normal
user access passed that point. This host startup failure is unrelated to song loading.
A temporary debugger attachment was restarted with local symbols to avoid symbol-server
waits. The diagnostic window was confirmed open. None of this is a cancellation test.

Still required from the manual run: failure phase and fault/result, whether guest UI polls
continue, one native B cancellation if possible, cleanup/resource/callback evidence and
subsequent known-good-song playback. Native main-menu transition and popup dispatch remain
unproven. Retail/Ultimate differences, auto-start, explicit suspension lifecycle and recovery
races remain unvalidated in the guest.

### 4. Checks and decision

`cmake --build --preset win-amd64-release` passed. The `load_guard` and `load_trace` CTests
passed. The trace test executes the real wrappers against original-function doubles and
isolated memory, verifying exactly-once forwarding, unchanged arguments and preserved
original results with tracing both off and on. The controller uses fake time, covering both
boundaries, Off, completion at expiry, wait-for-A, stale generations, manual exit, native
failed completion, error/timeout races, late completion, recovery failure and pause rebasing.

**Decision: no-go for enabling automatic guest cancellation yet.** There is no established
recoverable-error boundary and no supported-error table entry. In particular, Can't Hold Us
is not classified as corrupt or guard-recoverable. Finish the manual reproduction and trace
the native cancellation/ownership protocol before wiring the adapter, player setting and
native main-menu popup. No universal fault interception is implemented.


---

## Part 4 — Measured crash and external restart (2026-10-26)

### 1. Can't Hold Us is fixable without rejecting the package

The owner reproduced the same guest read fault at `0x3FFF1A00` in boots
`out/build/win-amd64-release/logs/rb_blitz_353.log` and `rb_blitz_354.log`.
The latter was run under CDB with load tracing. Evidence is retained in
`out/load-guard/cant-hold-us-20261026-debugger.log`, its `.dmp`, `context.bin`,
`guest-stack.bin`, `midi-parser.bin`, `dataarray.bin` and `text-events.bin`.

The UI poll continued in native loading state 3 until approximately 25 seconds
into the attempt. Native completion had not run. The faulting host instruction
maps through that build's `rb_blitz.map` to guest allocator `0x82457F50`.
The preceding calls are DataArray insertion (`0x823FA678`), MIDI event insertion
(`0x8242AE98` / `0x8242B6E0`), bracket-text parsing (`0x82437FE8`) and MIDI text
handling (`0x82439258`, called by `0x824A8578`).

The live parser's current-track symbol at +432 was **LIPSYNC1**. Its text-event
array had length **0x8000**, exceeding its signed 16-bit representation. The
next insertion sign-extends that count and requests **0xFFFC0008** bytes; the
size-class allocator consequently indexes below its table. The text being
parsed was `[New_hi 97 hold]`, classified as `unknown_event_type`.

Read-only MIDI parsing counted 38,163 events in LIPSYNC1. All track/event
extents and variable-length quantities were valid. Blitz's authored MIDI
parsers have no LIPSYNC track consumer. The zero-byte `.pan` allocation warning,
base `.milo` lookup and custom metadata context script are not the measured
fault site. The earlier speculation about the context script in part 3 is
superseded by this stack and live parser evidence.

[src/hooks/midi_tracks.cpp](../../../src/hooks/midi_tracks.cpp) extends native
`MidiParser::ShouldReadTrack` (`0x82438880`) to reject exactly LIPSYNC1 through
LIPSYNC4. It calls the original decision once and overrides an accepted result
only for those optional conversion tracks. The native caller (`0x824390B0`)
then invokes its own track rejection / skip protocol (`0x824A8D90` ->
`0x824A7280`): seek to the declared MTrk end, notify track end and continue.
Instrument, harmony, BEAT, EVENTS and VENUE tracks retain native processing.
No installed DLC is rewritten, no playable chart is truncated and no song is
blacklisted. This correctness fix applies with crash recovery switched off.

### 2. Fatal faults recover through a fresh process

The owner explicitly authorized automatic restart into the main menu for fatal
faults on 2026-10-26. This extends the original proposal's scope: part 1 §4.4
correctly described restart as separate work, and that work is now included.
The original cooperative timeout remains a separate, unvalidated path.

[crash_recovery.cpp](../../../src/loading/crash_recovery.cpp) starts a headless
`rb_blitz_load_monitor.exe` before guest launch. The monitor inherits only an
actual synchronization/query handle to its parent, an anonymous shared page and
a readiness event, using a restricted inherited-handle list. Guest launch waits
for the monitor to resolve the parent executable and acknowledge readiness.
It derives the executable identity
from the parent handle; there is no guest-supplied executable path, guest pointer,
exception callback or instruction-pointer rewrite. The page records only whether
a song attempt is active. Setup arms it; native completion, unload and normal
window close disarm it. Trace logging need not be enabled.

After the parent process has exited, a fatal Windows exception status or CRT
abort while that page is armed starts one replacement. A normal exit, ordinary
application error, task termination or exit outside an active load starts none.
Windows retires the failed process's threads, address space and handles;
no attempt is made to continue a guest whose state may already be corrupted.
A non-exiting deadlock, GPU stall or debugger-paused fault is not process exit
and is not recovered by this monitor. Unsaved state lost by a crash cannot be
restored by this mechanism.

The fresh boot receives an internal one-shot reason. It preserves the original
launch arguments, strips consumed restart/injection arguments, skips the offline
prompts for this boot and preserves the DLC background-song setting and its
launch overrides. The first acceptance build disabled that music for recovery;
the owner requested normal background-song preparation on the fast boot, so
that temporary override was removed. Preferences and DLC files are unchanged.
Native splash polling selects pad 0's local user and enters normal content
discovery. After discovery has returned to a stable main menu, the existing
`error_popup_panel show_popup` displays a project-owned locale message once,
with `splash_panel` as its return focus. No host overlay is added. Provider enter
and poll handlers are preserved, and incompatible splash shapes fail authoring.

Runtime controls:

| Flag | Default | Purpose |
| --- | --- | --- |
| `song_load_crash_recovery` | true | Enable the Windows external fatal-load monitor; restart-scoped |
| `song_load_recovered` | false | Internal one-shot boot reason supplied by the monitor |
| `song_load_test_crash` | false | Development acceptance: one synthetic access-violation exit before the first song setup; removed from recovery arguments |
| `song_load_trace` | false | Existing read-only lifecycle diagnostics |

The monitor logs its exit classification and replacement PID to
`<cache_root>/song-load-recovery.log`. Healthy restart also strips the internal
reason and injection flags. The companion is built with the game and included
in the installer's payload allow-list and snapshot provenance.

### 3. Validation and remaining acceptance

The MIDI regression executes the generated native acceptance, OnTrackName and
skip functions against isolated memory. It proves that retail initially accepts
LIPSYNC1, that the extension rejects only the four exact optional names, and that
the native path seeks once, notifies once and returns to the next-track state.
Near matches and playable tracks retain acceptance.

The monitor tests use actual child processes and an inherited event gate. They
check no replacement before parent death, exactly one replacement for an armed
abnormal exit, no replacement for normal/disarmed exits, argument quoting with
spaces/ampersands, and injection/reason filtering. Intermittent combined-test
failures exposed a startup race: the parent could exit before the monitor had
queried its executable path. The readiness handshake fixes that race before
guest launch. The rebuilt combined six-suite CTest run passed, including all
monitor checks and exclusion of ordinary task termination/error exits. Follow-up
argument regressions cover background music explicitly On, Off and unspecified.

Native dialog state tests cover menu readiness, discovery start, once-only
consumption and normal-boot silence. Payload tests preserve provider lifecycle
statements and reject unknown shapes without partial mutation.

Manual acceptance was booted with `song_load_test_crash=true`: the owner's first
song selection intentionally tests restart and the dialog; selecting Can't Hold
Us after dismissal tests the actual MIDI correction without reinjection.
Logs `rb_blitz_355.log` and `rb_blitz_356.log` plus
`<cache_root>/song-load-recovery.log` record the synthetic `c0000005` exit,
one replacement and consumption of the native popup request at the main menu.
The owner closed that run before testing again. The next manual run,
`rb_blitz_357.log` / `rb_blitz_358.log`, reproduced the injected failure and
restart; the owner then confirmed Can't Hold Us played successfully. The log
records native LIPSYNC1 rejection and completion in 1,192 ms. The owner observed
the shorter recovery boot and requested enabled background songs still load;
the follow-up preserves the normal music setting rather than forcing it Off.
Its in-game acceptance, explicit visual dialog/dismissal confirmation and a
subsequent good song remain unmeasured. The synthetic flag defaults false; the
MIDI fix is active on the first normal boot without any crash prerequisite.
Retail/other configurations remain unmeasured. The 30s/60s cooperative timeout
is not advertised as implemented.

---

## Part 5 — External timeout and completion audit (2026-10-26)

### 1. Owner decision and final behavior

When asked to finish the feature, the owner explicitly chose **restart-to-menu
for timed-out loads**. This supersedes part 1's same-process cancellation route
and part 2's dependent implementation prompts. It does not assert that native
cancellation, asynchronous guest unwinding or continued execution after a fault
has become safe. The old SDK-free cooperative controller is retained as research
and regression evidence; the shipped timeout runs in the external monitor.

Help & Options → General → Downloadable Content now exposes **Load Guard**:
**Off / 30s / 60s**, default Off. Stable identity is `general.load_guard`, native
SettingId 10, persisted as `[general] load_guard = "off"`, `"30s"` or `"60s"`.
Invalid tokens are refused. A successful commit writes and reads back the hot
`song_load_guard` cvar; its host callback publishes an atomic policy. Each new
song setup snapshots that value. Changing it does not alter an existing attempt,
and failed saves do not publish a new policy. Explicit config/environment/CLI
overrides use the existing settings precedence and read-only-row reporting.

Off disables the deadline. Fatal-load recovery remains the independent existing
`song_load_crash_recovery` control, default true; the monitor can enforce a chosen
deadline even when fatal-load recovery is disabled. It starts before guest launch
so a live change from Off needs no process restart.

The monitor uses Windows unbiased interrupt time, excluding system sleep and
hibernation; focus loss does not reset the total duration. Arbitrary debugger or
Task Manager process suspension is not a supported pause signal: use Off when
intentionally stopping the guest. The timer covers song setup until confirmed
native completion. It is disarmed before native unload/manual departure and on
normal window close. Discovery, boot graphics preparation, menu music, waiting
for A after completion, results and gameplay have no armed song deadline.

### 2. Process ownership and race contract

The anonymous shared page contains a generation, active/retired/claimed state,
absolute deadline and snapshotted duration. All mutable fields use Windows
interlocked operations. The monitor validates a coherent generation/deadline
snapshot, then atomically claims that active generation at expiry. Completion or
manual exit before the claim wins; an old timer cannot act on a new generation.
After the claim, the restart is committed. The monitor terminates only the real
inherited parent process handle, never a PID or path obtained from guest data.
It waits for actual process exit before launching exactly one replacement.
The OS retires the old address space, workers and resources; no guest cleanup
acknowledgement or completion callback is manufactured.

Only `30` or `60` is carried as the internal `song_load_timeout` reason. Recovery
arguments strip old reasons and both test injections; they preserve the normal
music setting. The existing native popup chooses a 30-second, 60-second or crash
locale token after normal discovery and stable main-menu entry. Messages explain
the restart and suggest another song/checking files; a timeout does not classify
the package as corrupt. Healthy settings restart also strips consumed reasons.

`song_load_test_stall` is a default-false development injection: the first song
job with an enabled deadline blocks before native work, leaving the external deadline to recover even
with the guest UI thread blocked. It neither changes installed songs nor touches
save data, and it is removed from replacement arguments. Production launches
have no injected stall or fault.

### 3. Verification and remaining measured scope

The release preset builds successfully. Twelve affected CTest suites passed:
load_guard, load_trace, midi_tracks, song_load_monitor, settings_screen,
menu_payload, settings_store, settings_migration, settings_service,
settings_hot_apply, game_restart and settings_bridge_service.

Fake-time assertions cover the exact 30s/60s boundaries and disabled deadlines.
Actual child-process tests cover fatal exit, normal/disarmed exit, timeout of a
blocked parent, native-completion retirement, same-address/new-generation
re-entry, crash-recovery Off with/without timeout, no early replacement, reason
selection and removal of one-shot injections. Settings tests cover strict enum
validation, hot classification, Off/30s/60s save/reload and both native menu
contexts. The generated payload still round-trips and retains provider handlers.

The initial final-test boot (`rb_blitz_359.log`) selected a song with the policy
still Off, so its injected stall had no timer. That diagnostic process was
stopped deliberately (`ffffffff`, no automatic replacement), and the injection
was corrected to defer while Off. The rebuilt `rb_blitz_360.log` uses a 30s CLI
override for the owner's timeout restart/dialog, enabled background music and
subsequent ordinary playback test. At that handoff the result was pending;
the close-out below records the owner's confirmation. Live settings-row
application, a 60s game run and broader retail/hardware coverage are not claimed
as manual measurements.
The earlier fatal-injection recovery and Can't Hold Us gameplay were confirmed
by the owner and logged in part 4.

**Final acceptance and close-out, 2026-10-26:** the owner answered **“All three
worked”** to timeout restart/dialog, restored background music and subsequent
ordinary playback. `<cache_root>/song-load-recovery.log` records `timeout_ms=30000
elapsed_ms=30038 generation=1`, exit `e042424c`, and exactly one replacement.
`rb_blitz_361.log` records DLC Menu Songs On, playback of the selected DLC menu
track, the native recovery popup request at stable main menu, and the next song's
native completion after 1,140 ms. The controlled stall is absent on that boot.
The original fatal-load case, the independent song fix and the chosen timeout
restart design are delivered. Same-process cancellation was superseded, rather
than marked implemented. The explicit coverage limits above remain limits.

The authored retail-record integration also passed 2,612 checks across all eight
store/header configurations (`out/load-guard/timeout-retail-records/`). The docs
checker still reports 43 pre-existing errors for the separately moved
RhythmVerse/binding plans; the completion check output is retained under
`out/load-guard/`. No generated retail data or test captures are committed.
