# Shader-cache P0: loading boundary and measurement workflow

2026-10-24. Project base `e1c27e3`, SDK `c94f5eb` plus the project patch stack.
Implements the inspection and diagnostic portion of
[P0](../plans/in-progress/2026-10-10-shader-cache-plan.md#p0--establish-the-post-a-loading-gate-and-baseline-measurements).
Controlled D3D12 empty/warm and Vulkan empty traces were collected on 2026-10-24
(see [Measured runs](#measured-runs-2026-10-24)); they measure the in-song compile
work but do not certify a compilation cause for every long frame, implement a
preparation barrier, or prove a runtime route. The gate blocker below still stands.

## What can be gated, and what is still unproven

The source-supported continuation is the **pair of script statements that sets the
splash directory's `state` to `main_menu` and requests `splash_screen`**. They occur
in the connect panel's Confirm handler, its Option/skip handler, and the project's
R3 automatic offline transform. A future graphics gate must replace all three
continuations, retain an attempt/generation identity, and release the same pair
exactly once on the guest/UI owner thread. Cancellation must retire that attempt.

`ServerConnectPanel::set_state` at **`0x822CA8E8` is not an asynchronous completion
barrier**. It writes `this+56`, invokes the data-defined `update_state` message
synchronously at `0x822CA9CC`, then calls the native advance routine `0x822CA460`.
R3 can reenter this function and request menu navigation *inside* that message.
Sleeping there, holding its frame, or running blocking shader storage from it
would prevent guest UI progress. Hooking its return is also too late to stop R3's
navigation request. P0 forwards it unchanged and only records observations.

The source map identifies a place to author a deferred continuation; it does not
prove that every content dependency is finished at every exit. In particular,
Option/skip can leave a nonterminal connect state. The exact safe scheduling,
content-readiness predicate and all-route runtime behavior remain **P6 blockers**.
P1–P5 can proceed; P6 must validate this boundary and cannot use a guessed state
number or the enumeration counter as its readiness condition.

## Effective content and source evidence

Read-only inspection scanned all **131 retail DTBs and 42 Ultimate DTBs** in the
installed archive indexes. Local dumps and fingerprints are under ignored
`out/shader-cache-p0/`; game bytes are not part of this change.
The production-parser inspector reports the empty 11-byte `config/gen/spew.dtb`
as unsupported (`implausible root arity`) rather than hiding the rejection. The
loading/splash records parse successfully; all 42 Ultimate records parse.

| Input | Size | SHA-256 |
| --- | ---: | --- |
| Retail `ui/splash/gen/splash.dtb` | 7,148 | `bee24ce7239dcf0acf1de51b7a6549cb7d5aa08c7586a849a8cb96c9666ad81e` |
| Ultimate splash override | 8,808 | `3ec0ec65bb3a8067c95a47f0d664f11e64795635c643f0b0ec85a7ca85589a0c` |
| Retail `ui/net/gen/server_connect.dtb` | 11,646 | `6ffdeb03a2ada4ffa2f6e985b431ba6e86187598741017dcc43e9ee9d123eb77` |

The inspected Ultimate archive has **no server-connect override**. Both modes use
that retail state machine. Ultimate changes the splash artwork, adds its settings
row and `check_trial` work, and adds an explicit main-menu-state guard to the list
selection branch. The start-button and Start-key routes still enter the connect
screen in both modes. A different payload must be inspected again.

The archives alone are not the final UI:

- [settings_payload.cpp](../../src/hooks/settings_payload.cpp) reads records from
  the mounted main/patch indexes and calls `BuildMenuRecords`. The settings-menu
  archive is mounted before guest launch and its compiled records take precedence.
- [menu_payload.cpp](../../src/settings/menu_payload.cpp) authors the effective
  splash record: merged settings, Play Current Song, Refresh Songs, and store rows.
  Refresh Songs requests `server_connect_screen` again; keep it in the route matrix.
- [menu_filter.cpp](../../src/hooks/menu_filter.cpp) applies read-time, byte-neutral
  edits and checksum retargeting. `SkipOfflinePrompts` in
  [menu_options.cpp](../../src/ui/menu_options.cpp) appends the automatic transitions
  to `update_state`; it can still edit the retail connect record after overlay setup.
  A P6 gate implemented only in `BuildMenuRecords`' splash edit would miss this path.
- Play Current Song can launch playback from the main menu; readiness must protect
  every song entry, rather than only the normal PLAY row. Store installation and
  explicit refresh require the later generation/invalidation work in P7.

The local inspector uses the **production parser and R3 transform**. It does not
reimplement DTB evaluation or claim its dump is a runtime trace. Runtime logs of
overlay selection and `menu_filter` application must accompany each comparison.

## Entry, save/settings, content and exit map

| Boundary | Source-supported behavior | Implication |
| --- | --- | --- |
| Initial splash | `state_on_enter=start`; entry restores it to the loaded directory. `ui/gen/ui.dtb` selects `splash_screen` as the first screen. | Title display is pre-A work. It already needs working pipelines. |
| A/selection | `splash_panel.SELECT_MSG`, component `start.but`, requests `server_connect_screen` and selects the participating user. | Instrument the resulting native start, not arbitrary physical A presses. |
| Start key | `BUTTON_DOWN_MSG`, state `start`, `kAction_Start` selects the user and requests the same screen. | Equivalent entry route; no separate graphics bypass is allowed. |
| Connect entry | Screen owns `shell_music_panel`, `splash_panel`, `server_connect_panel`; focus is connect. Screen entry sets splash state `waiting`; panel entry calls `start_connect_process`. | The connect screen remains the polling/drawing owner during preparation. |
| Native start | `0x822CB0D0` stores user at `+52`, clears prior login/save state and calls `set_state(1)`. | Exact post-A marker; refresh can call it too, so it is an attempt marker rather than a physical-button counter. |
| Native save flow | `0x822CA460` advances from start according to `+60`: mode 1 begins song cache (`2→3`), mode 0 goes to content enumeration, the other branch includes trial/trophy handling. Song-cache completion advances `7→8`; global options start `8→9`, completion `13→28`. | Modes and error choices affect the ordering. One observed run cannot stand for every branch. |
| Save choices | `0x822CB618` polls song-cache/global-options results, including missing/corrupt/create/no-space states. Confirm/Option call the title's own `select_*_choice`. `0x822CB2A0` handles the completed save operation and selects `7` or `13`. | A prompt is not a completed settings load. Preserve defaults/error/skip paths in the matrix. |
| Content | Native advance `28` calls the content manager's refresh operation and selects `29`. `0x822CB618` leaves `29` when `0x82375048` returns true (its exact test is content-manager `+44 == 1`). It then branches by mode toward name/login or net utility. | This is a stronger source lead than a host enumeration total. Runtime mounting/parsing and native manager transitions must still be checked. |
| Online completion | Login callback `0x822CB1F0` maps results to failure/account/password states or `24`; native advance takes `24→25→26`, save/config callbacks continue through friends/config handling. `0x822CB618` handles `40→36` after trial-enumeration completion or its timeout. | Connected `36` is derived from the file's named constant, not invented as a new graphics state. |
| Offline completion | R3 adds `Failed(23)/NoValidLoginCandidate(18)→OfflineMode(35)` and then the menu continuation directly inside `update_state`. The unmodified Confirm handler performs the corresponding choices. | Both automatic and manual routes need the same deferred continuation. |
| Usable menu observation | `UIManager::Poll` is `0x824B2958`; manager `+44` is current screen and `+16` is pending transition. Stable splash focus `start.lst` distinguishes the menu from title focus `start.but`. | The old `splash_screen/splash_panel` test alone also matches a canceled title screen. P0 uses the focus identity for diagnostics; runtime validation is pending. |

The addresses above refer to the current generated recompiled bodies, whose
`__imp__sub_*` versions the probes call unchanged. `set_state` dispatches the
message through the title's virtual handler; P0 does not install a second
`UIManager::Poll` hook.

Host graphics settings are selected by `SettingsBoot::ConfigurePaths` before
device creation, then the game's executable-side `rb_blitz.toml` and explicit
environment/CLI sources may outrank them. `OnPreLaunchModule` finalizes the cvar
registry; the new diagnostic logs actual values there. The existing backend/GPU
initialization logs identify the device that was actually created. Do not derive
the active backend solely from the saved Renderer row or a blank auto-path cvar.

Guest console/global options become available through the native save flow above.
The existing [local-save hooks](../../src/hooks/local_save.cpp) restore player
data at `0x821CA620` and after teardown `0x821CA3D8`; these are separate from the
console global-options load. Ultimate's `check_trial` can additionally apply
controller mapping and default overscan on menu entry. Thus there is **no proven
single universal “all settings authoritative” callback** covering every guest/mod
setting. P6 must enumerate the settings that affect its catalogue and resolve
those before choosing a generation; P7 handles later changes.

## All identified exits and returns

1. **Confirm from OfflineMode or Connected:** sets splash directory `state=main_menu`,
   then requests `splash_screen`.
2. **Confirm from Failed or NoValidLoginCandidate:** selects OfflineMode first;
   a second confirmation normally performs (1).
3. **R3 automatic offline:** performs both steps from `update_state`, including a
   possible nested state change during the outer `set_state` call.
4. **Option/skip:** save-related states choose not to create/load that save. The
   fallback TRUE branch sets `main_menu` and requests splash directly (for example,
   the visible skip route while entering a name). This branch cannot be assumed to
   have completed the entire login/content sequence.
5. **Trial:** `WaitingForTrialEnumeration(40)` can advance to Connected via native
   completion or timeout. The splash `check_trial` changes rows and achievements;
   full-game free-pack upsell may focus `upsell_panel` before `start.lst` is usable.
   Neither is an independent proof of graphics readiness.
6. **Cancel/back:** connect Cancel clears the user, writes splash `state=start`,
   and returns to the title. It is not menu readiness; abandon the pending attempt.
7. **Error return:** `ui/net/gen/error_flow.dtb` purges caches/clears the user; its
   confirm/start handler resets `state_on_enter=start` and returns to splash.
8. **Returning screens:** splash stores its directory state in `state_on_enter`
   on exit and restores it on entry. Return from options, pause/results and other
   screens may therefore recover an already usable menu without another A/connect
   sequence. Preserve process readiness or re-gate after invalidation.
9. **Refresh Songs:** the payload and retail filter variants can request connect
   again. Treat this as new content discovery, not a second first-boot shortcut.
10. **External development auto-A helpers:** scripts that press A merely select
    an entry route; they do not establish readiness. The P0 launcher has no such
    helper and must not be executed before the required UI-testing choice.

These are the data-defined routes found in the inspected archives plus the
current project edits. Unseen payloads, native error recovery and route timing
remain runtime validation work; this is not a claim that arbitrary content has
no other path.

## Progress ownership and lifetime

`0x822CA838` loads the panel and allocates the 16-byte progress object, storing it
at connect-panel **`+120`**. Constructor `0x82759308` stores its target panel at
object `+4`, initializes current/total at `+8/+12` to `-1`, registers it with the
content manager (`0x82375698`), then pushes the properties (`0x82759190`).
The target passed by the constructor comes from the panel's loaded directory;
it is not a reusable host progress widget.

[content_progress.cpp](../../src/hooks/content_progress.cpp) observes this object
and pushes aggregate enumeration counts only during the host enumeration phase.
The guest's `SetTotal(0x827593A8)`, Increment (`0x827593B8`) and Complete
(`0x827593C8`) own subsequent mounting progress. `Complete` is a **subphase** end,
not the menu continuation. During unload, **`0x822CC648` destroys and clears
panel `+120`**, along with its other pending objects, and returns the panel state
to inactive. Never reuse the observer's cached object address after that boundary.
P6 needs an explicit graphics-owned lifetime and counter handoff.

## Instrument coverage and limits

`--shader_compile_trace=true` enables the instrument (restart required; default
false). One shared owner in `rexruntime.dll` supplies a monotonic microsecond clock,
phase state and counters to both the executable and GPU plugin.

- Phases: `boot`, `pre_a`, `post_a_loading`, `menu`, `song_loading`, `active_song`,
  `paused`, `unknown`. Stable `start.but` marks pre-A; native connect start marks
  loading; stable `start.lst` marks menu. UI context changes are also logged.
- The existing controller poll marks active-song work only with a live controller
  accepting input (`+52`) on the game screen. This is a source-supported diagnostic
  proxy, **not a measured audio-clock boundary**. Pause and loading are separated;
  a UI transition's boundary frame is marked unknown rather than charged to playback.
- Both backend translation methods are timed, including failures; shader hash and
  modification identify a translation. Native guest PSO creation is keyed by the
  pipeline-description hash. Vulkan placeholders have a separate event type.
- Native helper graphics calls in render-target caches, swap setup, presenters and
  immediate drawers are wrapped. Compute helpers are measured at each backend's
  shared compute-pipeline utility. Helper identity is the creation call site
  (`key=0`), **not a complete per-variant recipe key**; P3 must inventory those keys.
- Processor queue draining and actual completion-event waits are separate events.
  CPU worker durations may overlap; summing them does not measure wall-clock delay.
- `VdSwap` and `XE_SWAP` independently measure guest submission and GPU-command-
  processor frame gaps. Neither is an actual displayed-frame timestamp or GPU timer.
  Gaps across phase changes are attributed to `unknown`.
- Fixed-size counters retain attempts, failures, summed/max microseconds and counts
  at least 50/100/250 ms. Details are capped at **2,048 per event type**. Only frame
  gaps at least 50 ms and processor drains at least 1 ms get individual detail lines.
  Summaries are cumulative, emitted on phase changes, every 10 seconds at swaps and
  window close, with at most **256 snapshots**; UI/phase details are also capped.
  Counter collection continues after detail caps, but a long run can exhaust the
  summary budget. Preserve logs promptly and keep comparison runs short.
- A scope keeps its starting phase and records the ending phase. A boot operation
  finishing late is not silently recategorized as new gameplay work.

There is no per-draw disk logging. The opt-in instrument still performs locking
and logging, which can perturb timing: repeat a problematic route with tracing
off before attributing a change in performance to the renderer. Resource loading,
texture conversion, readback and other synchronization are **not timed here**.
Temporal overlap is evidence to investigate, not proof of causation.

## Measured runs (2026-10-24)

Four prepared runs under `out/shader-cache-p0/runs/` (the preparation script and the
scratch driver are ignored tools beside them). Each copies the player's saves and
launcher profile, loads an empty DLC root, renders a 1280x720 window at 1x with the
mouse off and native keyboard only, and runs with `--shader_compile_trace=true`.

| Run | What it is |
| --- | --- |
| `d3d12-ultimate-empty-final` | The first empty-cache trace. One active-song segment only: its driver never pressed A on the loading screen's `PRESS (A) TO BEGIN`, so the shot named `replay.png` is the load-out screen and **no replay was played**. `collect` refuses to rewrite a collected run, so the manifest's observation still claims an immediate replay; this row is the correction. Superseded by `-r4`. |
| `d3d12-ultimate-empty-r4` | Empty application cache: first play ~30 s, pause → Restart → YES, replay ~30 s, normal window close. |
| `d3d12-ultimate-warm-r4` | The same route after a full process restart, with `-r4`'s learned `shaders/shareable` records seeded into this run's cache. |
| `vulkan-retail-empty-r2` | Vulkan backend, retail target, empty cache, manual offline confirmations (two A presses) and retail's own pause → Restart screen. Retail has **no power-up screen**; the song starts straight from the song list. |

Two further runs are kept as superseded probes, both aborted by harness mistakes
and not used for numbers: `d3d12-ultimate-empty-r3` (the song was waiting on the
loading screen's `PRESS (A) TO BEGIN`; the operator's screenshot shows it) and
`vulkan-retail-empty-final` (the driver waited for a power-up screen retail does not
have). Both dirs carry the failed run's log and screenshots.

All durations below are the trace's CPU scopes (microseconds), summed at each phase
change; they can overlap across workers, so they are work, not wall time. "Frames"
are presented-frame intervals in that phase.

| Run / segment | translations | guest pipelines | helper graphics | compile waits | frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| empty-r4 boot | 8 (0.4 ms) | 8 (14 ms) | 52 (29 ms) | 1 (0.7 ms) | 107 |
| empty-r4 first play | 61 (8.0 ms, max 0.46) | 81 (402 ms, max 16.8) | 1 (0.9 ms) | 2 (8.6 ms) | 1,879 |
| empty-r4 replay (delta) | 1 (0.2 ms) | 1 (7.5 ms) | 0 | 0 | 1,932 |
| warm-r4 boot | 101 (8.8 ms) | 132 (545 ms) | 52 (20 ms) | 0, but 129 drains incl. one 133.6 ms | 106 |
| warm-r4 first play | 4 (0.5 ms) | 5 (35 ms, max 11.9) | 1 (0.8 ms) | 0 | 1,957 |
| warm-r4 replay (delta) | 0 | 0 | 0 | 0 | 1,903 |
| vulkan-retail-r2 boot | 11 (5.1 ms) | 13 (5.4 ms) | 1,114 (182 ms) + 8 placeholders | 0 | 478 |
| vulkan-retail-r2 first play | 56 (82 ms, max 4.1) | 79 (629 ms, max 224) | 1,618 (326 ms) + 79 placeholders | 4 (336 ms, max 179) | 1,768 |
| vulkan-retail-r2 replay (delta) | 7 (26.5 ms) | 7 (741 ms, max 234) | 2,413 (437 ms) + 7 placeholders | 1 (199 ms) | 1,864 |

Long frames at or above 50 ms inside the active song:

| Run | count | worst | records a compilation/wait overlap? |
| --- | ---: | --- | --- |
| empty-r4 | 8 | 306.4 ms guest, 140.6 ms GPU | four (306.4 / 164.4 / 156.5 guest, 140.6 GPU) list translation, pipeline or helper events; four (80–90 ms) list none |
| warm-r4 | 2 | 201.3 ms guest, 138.0 ms guest | one overlaps a single helper-graphics creation; one lists nothing |
| vulkan-retail-r2 | 4 | 420.4 ms guest, 434.6 ms GPU | the two worst overlap 177 / 188 translation, pipeline and helper events; the two ~90–100 ms frames overlap two each |

The Vulkan replay's cumulative `guest_frame` maximum is **549.9 ms** (above the first
segment's 420.4 ms), and its `compile_wait` delta is one 199.4 ms event — the Vulkan
side is still compiling on the replay, not only on first play.

### What these runs establish

1. **First play on an empty application cache does real translation and pipeline
   creation inside the song**, on both backends: 81 guest pipelines / 61 translations
   (D3D12) and 79 / 56 (Vulkan), with 336 ms of Vulkan compile wait. Four of the
   empty D3D12 song's eight long frames overlap that work.
2. **The same-process replay already reuses almost everything** on D3D12 (+1
   translation, +1 pipeline, no wait), which is why replay looked smooth before this
   trace existed.
3. **A full restart with the learned records moves the work to the boot preload**:
   the warm boot schedules 132 pipelines / 101 translations, and the first play then
   adds 5 pipelines / 4 translations with no compile wait. The warm boot still spent
   one 133.6 ms blocking drain in the D3D12 preload, and the leftover 5/4 entries are
   combinations the empty route never produced — the catalogue is learned, not complete.
4. **Vulkan is not simply "the same with more work"**: it creates an order of
   magnitude more helper graphics pipelines (1,114 at boot, 1,618 during the first
   play, another 2,413 during the replay) and still pays a 199 ms compile wait on the
   replay. A Vulkan warm leg has **not** been collected; that is P8's matrix, not P0's.
5. **Long frames without recorded compile work remain** (four of eight on the empty
   D3D12 song, one of two warm, both ~90–100 ms Vulkan frames): resource I/O, texture
   conversion, readback and other synchronisation are not timed here, so they stay
   unresolved rather than being attributed to shaders.

These runs do **not** establish that the reported freeze has a compilation cause: no
player report was correlated with a specific frame, the route is one bundled song with
no power-ups, and the operator was the agent driving the harness, not a player judging
notes, effects, audio or input. They also do not cover retail D3D12, Vulkan Ultimate,
DLC/custom content, other hardware, or the cancel/error/trial routes.

## Repeatable workflow

Build before preparing or testing:

```powershell
cmake --preset win-amd64-release -DBUILD_TESTING=ON
cmake --build --preset win-amd64-release
```

The `BUILD_TESTING` switch also builds the SDK-free record inspector and actual
runtime counter tests. For read-only archive evidence:

```powershell
python scripts/shader_cache_trace.py inspect retail-original --hdr game/gen/main_xbox.hdr
python scripts/shader_cache_trace.py inspect ultimate-original --hdr game/ultimate/gen/patch_xbox.hdr
python scripts/shader_cache_trace.py inspect retail-offline --hdr game/gen/main_xbox.hdr --offline
```

Use new snapshot names on a repeat; existing evidence is never replaced. The
inspector reads every DTB and keeps the loading/menu references plus fingerprints.
The offline arm runs the production R3 transform against copies. Mounted payload
authoring is additionally covered by the `menu_payload` host suite and the source
audit above; archive dumps alone do not prove what a running title evaluated.

Prepare an empty application-cache experiment using the **same real saves and
settings** as the comparison (replace these paths with the boot log's resolved
ones). Preparation copies inputs and writes an inert `launch.ps1`; it never starts
the game or presses A:

```powershell
python scripts/shader_cache_trace.py prepare d3d12-ultimate-empty --backend d3d12 --variant ultimate --user-data '<user_data_root>' --profile '<profile_dir>/launcher.toml' --route 'Chosen song and loadout; first play, immediate replay, then normal exit'
```

The two paths are the ones the boot log resolves (`user_data_root` and
`launcher_profile`); substitute the values the log prints on this machine.

**Before executing the generated launcher or navigating the game, obtain the
owner's choice of who drives it, as required by AGENTS.md.** The launcher keeps
the installed game/content and writes to copied saves, a copied profile/settings
store and a distinct cache under `out/shader-cache-p0/runs/<name>`. It preserves
binary hashes, source diffs/new files, command arguments, content-index hashes,
initial save/settings state and cache inventories. CLI arguments preserve the
isolated roots even when the normal path file points elsewhere. Extra settings,
DLC roots, R3 enabled/disabled and trial/full licence must be recorded via
`--game-arg=--flag=value`; verify the effective runtime values in the log.

Once the authorized operator has exited normally:

```powershell
python scripts/shader_cache_trace.py collect d3d12-ultimate-empty --observations 'Record first play versus replay; timestamps and visible event; notes/effects/audio/input; normal exit'
python scripts/shader_cache_trace.py prepare d3d12-ultimate-warm --backend d3d12 --variant ultimate --user-data '<user_data_root>' --profile '<profile_dir>/launcher.toml' --warm-from d3d12-ultimate-empty --route 'Same song, loadout and actions after full process restart'
```

Collect the warm run the same way. An empty **application** cache leaves OS and
driver caches warm; do not erase them or normal saves/caches to manufacture a
colder run. Repeat for retail/Ultimate and D3D12/Vulkan separately, using new run
names. Preserve manual save-error/skip/cancel/trial/upsell/refresh routes separately
from the standard first-song comparison. Mouse disabled and focus changes must
also be exercised before certifying loading-screen responsiveness.

Preliminary authorized probes are retained separately from timing comparisons:
the sandbox-desktop launch could not be focused/captured and was closed through
its own SDL window. The first interactive D3D12/Ultimate probe reached the cached
content bar and automatic offline menu with native states
`1→2→3→7→8→9→13→28→29→15→18→35→0`. It exposed the need to normalize the focus
component's virtual base before reading its object name. That diagnostic was
corrected before the controlled comparison; the preliminary run's phase labels
must not be used as pre-A/menu timing evidence.

The 2026-10-24 controlled runs (above) were driven by the scratch harness
`out/shader-cache-p0/drive_run.ps1`, which gates every step on a trace line rather
than a fixed sleep and reads the yellow highlight bar from a capture to pick a menu
row. Two route facts it had to learn, both measured: the title's loading screen
requires a **second A press** on `PRESS (A) TO BEGIN` (waiting for the game screen
stalls there forever — `d3d12-ultimate-empty-r3`), and **retail has no power-up
screen**, so the retail song starts directly from the song list
(`vulkan-retail-empty-final` died waiting for one). Its screenshots, `report.json`
and `manifest.json` per run are the evidence; nothing was deleted from an earlier
run.

`report.json` correlates each recorded long guest/GPU frame interval with recorded
translation/creation/drain/wait intervals. It reports unresolved gaps explicitly
and flags suppressed details. Do not sum cumulative snapshots, infer full coverage
from zero events, or call a frame with temporal overlap a proven compile stall.
The operator supplies rendering/audio/input observations; binary caches cannot
answer those questions.

## Validation status and changed files

The release build (configured with `-DBUILD_TESTING=ON`) builds and links the game,
the record inspector and the trace test target; `shader_compile_trace_enabled` /
`shader_compile_trace_disabled` pass, the Python report tests pass (3/3), and the full
release-tree `ctest` roster runs **50/50** after a complete build (an earlier run
reported 16 tests `Not Run` only because those executables had not been built yet). The
SDK patch audit exits 0. Runtime evidence lives separately in
`out/shader-cache-p0/runs/`: `d3d12-ultimate-empty-r4`, `d3d12-ultimate-warm-r4` and
`vulkan-retail-empty-r2` are the runs quoted above. What remains uncollected: a player's
own visual/audio/input judgment on the recorded routes, Vulkan warm, retail D3D12,
other hardware, and the cancel/error/trial/upsell routes. The post-A gate itself is
still a P6 blocker (see the top of this document).

Implementation files:

- `src/diag/shader_trace.{h,cpp}`, shared calls in `src/input/native_menu.cpp`,
  `src/hooks/gameplay_input.cpp`, `src/rb_blitz_app.h`, and `CMakeLists.txt`.
- `patches/rexglue-sdk/0020-bounded-shader-compilation-trace.patch`: runtime-owned
  counters, translation/creation/wait scopes and helper instrumentation.
  Patch `0019` also carries the bounded swap observations alongside its original
  trace-level swap/input diagnostics. No SDK gitlink was changed.
- `scripts/apply_sdk_patches.ps1`: exact replay/content audit rather than concatenated
  hunk comparison, which could not validate disjoint patches arriving out of source
  order; new patched files are checked too. Pristine replay lives under ignored
  `out/sdk-patch-audit/`, without changing the installed SDK or nested dependencies.
- `scripts/shader_cache_trace.py`, `tools/loading_record_inspect.cpp`,
  `tests/shader_compile_trace_tests.cpp`, `tests/shader_cache_trace_tools_tests.py`,
  this evidence map and the parent plan status.
