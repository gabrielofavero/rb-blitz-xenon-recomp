# Shader cache

One document for the shader-cache program: what the title compiles during loading and gameplay, how
shader identities resolve from installed content, the pipeline recipe catalogue, the cancellable
preparation lifecycle, the native driver pipeline caches, and the post-A guest loading gate that
runs preparation. It merges the six per-delivery records (P0, P2-P6) unchanged: each is a part
below, and the plan that scopes them is
[`docs/plans/in-progress/2026-10-10-shader-cache-plan.md`](../plans/in-progress/2026-10-10-shader-cache-plan.md).

Cite as "`docs/engine/shader-cache.md` part N §X". Part numbers are stable; section numbers repeat
across parts on purpose.

## Part 1 — Loading boundary and measurement workflow (was `docs/engine/shader-cache-p0.md`, P0)

> Update, 2026-10-24: part 6 now implements the source-supported
> post-A gate and native loading phase. The blocker statements below describe the
> P0 investigation; all-route UI validation remains open in P6/P8.


2026-10-24. Project base `e1c27e3`, SDK `c94f5eb` plus the project patch stack.
Implements the inspection and diagnostic portion of
[P0](../plans/in-progress/2026-10-10-shader-cache-plan.md#p0--establish-the-post-a-loading-gate-and-baseline-measurements).
Controlled D3D12 empty/warm and Vulkan empty traces were collected on 2026-10-24
(see [Measured runs](#measured-runs-2026-10-24)); they measure the in-song compile
work but do not certify a compilation cause for every long frame, implement a
preparation barrier, or prove a runtime route. The gate blocker below still stands.

### What can be gated, and what is still unproven

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

### Effective content and source evidence

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

### Entry, save/settings, content and exit map

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

### All identified exits and returns

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

### Progress ownership and lifetime

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

### Instrument coverage and limits

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
  (`key=0`), **not** a complete per-variant recipe key. P3 inventoried the 26 sites
  and their key compositions in
  [`config/pipeline_helper_inventory.json`](../../config/pipeline_helper_inventory.json)
  (part 3); the trace still logs call-site identity,
  so per-variant attribution remains an explicit gap for P4/P8.
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

### Measured runs (2026-10-24)

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

#### What these runs establish

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

### Repeatable workflow

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

### Validation status and changed files

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

## Part 2 — Local shader resolution (was `docs/engine/shader-cache-p2.md`, P2)

P2's local shader-resolution criterion is now met for the current candidate:
**186/186 identities resolve from the installed content**, including all 120 pixel
and 66 vertex identities in ten distinct preserved P0 stores. The former vertex
blocker is resolved by reproducing declaration linking and extracting two bootstrap
programs from the supported executable. Resolution requires no previous gameplay or
developer cache on the user's machine. P3's catalogue now consumes this resolver for
every recipe-referenced identity (part 3); P4 must
integrate preparation. This alone does not establish live GPU readiness or
remove in-song compilation.

Implementation: [shader_asset_resolver.py](../../scripts/shader_asset_resolver.py),
[SDK hash helper](../../tools/shader_microcode_hash.cpp),
[vertex linker](../../scripts/shader_vertex_link.py),
[bounded XEX source reader](../../scripts/shader_xex_source.py),
[layout recipes](../../config/shader_vertex_layouts.json),
[hash-only candidate](../../config/shader_resolution_candidate.json),
[hermetic tests](../../tests/shader_asset_resolver_tests.py).
This is offline tooling; the game does not invoke it yet. Integration with the
preparation operation belongs to P4. No SDK changes or game/UI runs were needed.

### Reproducing the inspection

Build first:

```powershell
cmake --build --preset win-amd64-release
python scripts/shader_asset_resolver.py --game-root game --output out/shader-cache-p2/resolved-retail-index.json
python scripts/shader_asset_resolver.py --game-root game --effective-overlay-root game/ultimate --output out/shader-cache-p2/resolved-ultimate-index.json
ctest --test-dir out/build/win-amd64-release -R '^shader_asset_resolver' --output-on-failure
```

Use a **new** output path for each invocation. The tool refuses to overwrite an
existing file. Exit 0 means every requested candidate identity resolved; exit 2
means unresolved coverage or unavailable vertex sources; exit 1 means invalid
input or an I/O/tool failure. The offline tool uses Python 3.11+, PyCryptodome (also
used by the existing XEX tools), and the SDK's existing key source. It never logs
keys or writes a decoded executable to disk.

`--effective-overlay-root` names the provider actually mounted by the runtime's
`ultimate::Configure`, rather than a requested Ultimate mode. Omit it for an
unoverlaid retail root or a root with a merged patch archive. The reader follows
overlay-before-base host-file precedence, then patch-before-main archive-record
precedence, including part files selected through the same host union. It reads the
two shader assets directly from their installed ARKs; it does not depend on the
developer's extracted `assets/game/360` tree. It records the selected header hashes,
part paths/offsets, asset and executable SHA-256 values, a combined content
fingerprint and a separate linkage-recipe fingerprint.
Identical effective shader content has the same fingerprint even if its provider
or installation path differs. Future runtime callers should supply the actual
mounted sources, including any additional content provider, rather than infer them
from a target label. External title updates, DLC and custom shader providers are
not certified by these two base assets.

By default it verifies the committed 186-identity candidate. `--catalogue path.json`
can select another shader-identity request list with this shape:

```json
{"schema":1,"title_id":"5841122D","shaders":[{"stage":"pixel","hash":"f920af5cd865655e"}]}
```

This is a shader-identity request list, not P3's pipeline-recipe catalogue. The tool
reports each request as resolved or unresolved. An empty list is not complete.
`--runtime path.xsh` (repeatable) validates local captured evidence, including the
SDK format version and every microcode hash. It compares resolved bytes with the
capture byte for byte. Runtime stores are **never** a resolution source. Neither
command emits `.xsh` data, imports records into the player's cache, or bundles retail
shader bytes. The JSON contains hashes and source locators, not code.

`Resolver.resolve(stage, hash)` returns verified bytes from the current parsed
sources and linkage recipes or `None`; it never substitutes an unlinked template.
Persisted index locators are
evidence only: no API trusts or executes them. Rebuilding the index reparses and
fingerprints the current content. `live_ready` is always false; GPU preparation and
process residency are separate work.

### Established asset layout

The framing blocker in the earlier investigation came from inspecting the **base
class** parser, `sub_8224DC70`. The actual D3D shader-cache vtable points to
`sub_823EC540` at offset +92 (the table near guest `0x820783D8`). Its read order is:

```text
XOBX                           4 bytes
version = 1                    little-endian u32
named_entry_count              little-endian u32
for each named entry:
    name_length, name          little-endian u32, then ASCII bytes
    permutation_count          little-endian u32
    pixel_object_bank_size     little-endian u32
    pixel_code_bank_size       little-endian u32
    pixel_object_bank
    pixel_code_bank
    vertex_object_bank_size    little-endian u32
    vertex_code_bank_size      little-endian u32
    vertex_object_bank
    vertex_code_bank
    for each permutation:
        key                    little-endian u64
        pixel_object_offset    little-endian u32
        pixel_code_offset      little-endian u32
        vertex_object_offset   little-endian u32
        vertex_code_offset     little-endian u32
```

Offsets are bank-relative, aligned to 16 bytes; shared bank offsets are allowed.
The earlier supposed summed vertex/pixel strides are the first pair of **bank
lengths**. The name scan also missed `multimesh` and `standard`, so its 21-name
estimate was incomplete. The parser now consumes both files to EOF: 23 named entries
and 2,098 permutations in `xbox_shaders`; 22 and 89 in `xbox_preinit_shaders`.

The object banks contain guest big-endian D3D metadata. A pixel object's program
header is at +40, a vertex object's at +872. Their version words are respectively
`0x102A1100` and `0x102A1101`. Header +4 gives metadata length; +8 gives data-buffer
length; +24 points to a descriptor within that metadata. The descriptor's first
two big-endian dwords give the microcode offset and byte length within the selected
code-buffer slice. This indirection matters: bloom's pixel buffer has a 64-byte
constants prefix; hashing the whole buffer or header +8 bytes gives the wrong
identity. The helper hashes only the indicated guest-endian microcode with the same
SDK `XXH3_64bits` used by both pipeline caches.

The parser bounds the asset, entries, names, total permutations, four banks, every
table offset, object header, descriptor, data buffer and microcode slice before
reading or hashing them. It rejects duplicate names/keys, unknown versions,
trailing data, truncated records and hash collisions. ARK reads and decompression,
runtime `.xsh` records and the helper's binary input are bounded too. Its 32 MiB
asset and 64 KiB microcode limits deliberately reject unsupported larger content
rather than assuming coverage.

### Measurements from local assets and preserved captures

Both the retail and effective separate-Ultimate installation selected these assets:

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `xbox_preinit_shaders` | 160,557 | `959b6c14f8f7cd29fc68dc736743df10fffe05f26a0d6520d6374e30c076c608` |
| `xbox_shaders` | 5,271,431 | `70616f5aca230ae9a9be776a0a5fd32f30dc33d50c63e20c509357483b57e2e2` |

Earlier asset-only fingerprint (before vertex source integration):
`321d016c1ff7ff77acac323ee1a2df05405490dba43480d5ddb015a7f5a09a6e`.
The current fingerprint including the supported executable is
`875dac1915e208c64f5c673f0568b57d13c730b5df18ca230da7766061adc95b`.
This certifies equal shader inputs on this installation, not equal pipeline coverage
or compatibility for arbitrary Ultimate versions.

| Input | Pixel identities resolved exactly | Vertex identities resolved exactly |
| --- | ---: | ---: |
| Original P2 comparison: `d3d12-ultimate-empty-r3/inputs/.../5841122D.xsh` | 117 / 117 | 64 / 64 |
| Union of seven preserved P0 `after-cache` stores | 75 / 75 | 41 / 41 |
| Union of all ten distinct preserved P0 `.xsh` file contents | 120 / 120 | 66 / 66 |

The current index contains 565 unique pixel and 763 reconstructed vertex identities
(1,328 total), plus 178 distinct source vertex templates and provenance for all
2,187 permutations. It applies five bounded layout recipes to the source templates;
it does not infer readiness from template hashes. The broadest union
includes developer input-cache records, so it is experimental shader-identity
evidence, not a promise of exercised gameplay coverage.

Local evidence (ignored, not distributed):

- `out/shader-cache-p2/retail-index.json`: original 181-record comparison.
- `out/shader-cache-p2/ultimate-validation.json`: individual seven after-cache
  comparisons, plus their 116-identity candidate union.
- `out/shader-cache-p2/candidate-shaders.json`: that union as hash/stage-only requests.
- `out/shader-cache-p2/ultimate-index.json`: CLI resolution of those requests through
  the effective Ultimate installation, without a runtime store as input.
- `out/shader-cache-p2/all-preserved-stores.json`: each distinct store's digest,
  earlier unresolved identity result and the broadest 186-identity union.
- `out/shader-cache-p2/resolved-retail-index.json` and
  `resolved-ultimate-index.json`: successful fresh local resolution of the committed
  candidate with no `.xsh` input.
- `out/shader-cache-p2/resolved-all-preserved-stores.json`: final **byte-for-byte**
  comparison of every shader in all ten distinct stores, using the same resolver
  whose source inputs are exclusively the installation and declarative layout recipes.

### Resolved vertex declaration linking and bootstrap sources

`sub_823EC540` initializes pixel objects through `sub_82514420` and vertex objects
through `sub_82516010`, then attaches them with `sub_823EB860`. The latter does not
perform the final vertex transformation. `sub_82516010` preserves the source fetch
instructions in per-shader storage. The later D3D submission paths
`sub_82522410` / `sub_82522208` invoke **`sub_82521908`** with the live vertex
declaration and stream mapping. It matches usage/index against declaration elements
(12-byte records starting at declaration +52), rewrites fetch format, stream and
offset fields, sorts/coalesces fetches and writes the resulting instructions into
the uploaded program. This explains why matching stored tails did not recover the
runtime hashes; merely extracting the correct vertex bank is insufficient.

`shader_vertex_link.py` now reproduces the fetch rewriting, declaration component
mapping, ordering, prefetch counts and mini-fetch coalescing. It uses the source
asset's bounded fetch-slot table. Only those instructions are rewritten; the asset
supplies the rest of the shader. Layout recipes contain declaration locations and
stream strides, not replacement shader instruction words. The observed layouts are:

| Layout | Installed declaration / initialization | Stream strides in dwords |
| --- | --- | --- |
| Packed mesh | Image offset `0x80C910` | 9 |
| Unpacked mesh | Image offset `0x80C970` | 24 |
| Packed multimesh | Image offset `0x80CBF0` | 9, 1 |
| Particles | Image offset `0x80CD18` | 9 |
| Fullscreen | FVF 450 via `sub_82512730` | 7 |

The fullscreen case came from `sub_823EAB58`: it calls `sub_825161F8` with 450, then
draws with a 28-byte stride. The constructor produces XYZ, diffuse and specular
colors, and one float2 UV at byte offset 20. This accounts for the two postprocess
identities absent from the four static declaration cases. The other two missing
identities were **not** asset templates: they occur byte for byte in the executable
at image offsets `0xAA180` (108 bytes) and `0xA9D10` (96 bytes).

The XEX reader verifies `default.xex` against the project's entrypoint fingerprint
before using any address-based recipe. The key stays in its existing SDK owner.
Basic-compression block lengths, image allocation and each source read are bounded.
It parses declarations with a 32-element cap and reads the supported executable's
swizzle-mask and format-size tables. Bootstrap lengths are bounded and their exact
SDK hashes must match. Linked data is published only after source and hash checks
succeed, with a 32,768-program / 64 MiB generation cap. A second source-generation
call requires a new resolver, so an earlier content generation cannot leak through.

Candidate/layout recipes were established against preserved developer captures,
as pipeline catalogues can be. On a new installation those captures are unnecessary:
all shader bytes come from that user's assets or executable, and the linker applies
the semantic recipes. The shipped JSON contains identities, offsets and declaration
parameters, not retail shader bytes. Unknown executable revisions, malformed sources
or identities requiring another layout remain explicitly unsupported. Missing vertex
requests report `vertex_layout_not_covered`; missing pixels report
`absent_from_installed_assets`. This resolves P2 for the current candidate; arbitrary
DLC/custom declarations and complete P3 pipeline coverage remain unproven.

### Validation

Release build succeeded with `cmake --build --preset win-amd64-release`, including
the SDK hash helper, `rb_blitz.exe` and both renderer DLLs. The build reports the
existing non-frozen local toolchain (Clang 19.1.7 and CMake 4.4.0).

The `shader_asset_resolver` ctest passes nineteen hermetic tests: every byte truncation
of a synthetic asset; malformed counts, versions, stages, descriptors and offsets;
SDK hash/endianness and protocol checks; collision refusal; constant prefixes;
duplicate/shared records; content fingerprint changes; stage-aware unresolved
coverage; XSH corruption; and real synthetic retail/overlay/merged ARKs including
truncation, duplicate entries and bounded decompression; CLI failure/blocker exit
codes and refusal to overwrite evidence; synthetic declaration linking, sorting,
mini-fetch coalescing, unpublished failures, FVF construction, and bounded plaintext/
encrypted basic-XEX decoding. No retail shader blobs are in the fixtures. Separate
`shader_asset_resolver_retail` and `shader_asset_resolver_ultimate` tests verify the
committed candidate directly from installed content when those installations exist.

The full release ctest run passed **54/54** with `TEMP` and `TMP` set to the writable
workspace directory `out/_swap/p2/test-temp`. With the default temporary location,
the unchanged `path_policy` test fails at `tests/path_policy_tests.cpp:88` for a
not-yet-created descendant path; an isolated rerun reproduced it there and passed
with the workspace temporary directory. No path-policy code was changed.

## Part 3 — Pipeline recipe catalogue (was `docs/engine/shader-cache-p3.md`, P3)

P3 turns the renderer's learned pipeline descriptions into a **versioned, validated
catalogue**: normalized D3D12 and Vulkan recipes with their shader modifications,
deduplicated across every captured store, with provenance, local shader-identity
resolution and an explicit helper-pipeline inventory. It is offline tooling plus one
configuration record; it does not integrate preparation (P4), gate the loading
transition (P6) or make any in-song compile-work claim. The candidate catalogue for
the preserved 2026-10-24 runs is produced and every shader identity it references
resolves locally; **helper recipes are inventoried and their runtime creation keys are
carried by the trace** (SDK patch 0026), so the catalogue no longer has an unresolved
helper identity.

Five owner-driven coverage sessions (2026-10-24) then browsed the options surface and
exercised **every power-up on both backends**, and finally the two alternate rendering
paths. The D3D12 `rtv` catalogue went from 175 to **412 recipes**, the Vulkan `fbo` one
from 144 to **409**; `d3d12/rov` and `vulkan/fsi` were captured separately (219 and 228
recipes), which took the combined catalogue to **1,268 recipes across all four declared
paths, 208/208 shader identities resolved** and the committed hash-only candidate from
186 to 208.

Implementation: [shader_pipeline_catalogue.py](../../scripts/shader_pipeline_catalogue.py),
[helper inventory](../../config/pipeline_helper_inventory.json),
[hermetic tests](../../tests/shader_pipeline_catalogue_tests.py). The catalogue itself
needs no SDK change — it reads the files both caches already write; SDK patch 0026 only
improves the separate trace (helper key identity).

### What the catalogue contains

| Part | Content |
| --- | --- |
| `stores` | Every input file's SHA-256, size, backend/rendering path, verified record count, duplicates and any unverified tail. |
| `backends.<backend>.paths.<path>` | One entry for every declared rendering path (`rtv`, `rov`, `fbo`, `fsi`) with `captured: true/false`; an uncaptured path is recorded rather than omitted. `recipes` holds each unique normalized `PipelineDescription`: its raw 64-byte (D3D12) or 60-byte (Vulkan) bytes, its stored `description_hash`, the vertex/pixel shader identity and translation modification, decoded state, and the stores it came from. |
| `backends.<backend>.paths.<path>.coverage` | Recipe count, unique shader and modification counts, shader pairs, recipes without a pixel shader, and the decoded state axes actually present. |
| `helpers` | The helper-pipeline inventory (creation site, key composition, dependencies, when it is created) and its coverage counts. |
| `coverage` | Shader-resolution result and reasons, committed-candidate scope, helper key-identity gaps, `paths_not_captured`, explicit blockers, and `live_ready: false`. |
| `provenance` | Input order and tool identity. |

Recipes are **not** game shader bytes: a description is the renderer's own
normalized state plus the 64-bit shader identities it references. Microcode stays in
the user's `.xsh`/`.xpso` storage and is never embedded in the catalogue.

Merging is a **union, never a replacement**: every input record is kept, identical
descriptions collect all their stores as provenance, and no richer store can be
replaced by a smaller one. A same-hash/different-bytes pair is refused rather than
deduplicated.

### Storage format, measured

The runtime writes a 12-byte header and fixed-size records
([D3D12](../../rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp),
[Vulkan](../../rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp)):

| Field | D3D12 | Vulkan |
| --- | --- | --- |
| File magic | `XEPS` (`0x53504558`) | `XEPS` |
| API selector | `DXRT` (rtv) / `DXRO` (rov) | `0` (fbo) / `1` (fsi) |
| Stored version (byte-swapped) | `0x20260226` = max(`PipelineDescription::kVersion` `0x20210425`, `DxbcShaderTranslator::Modification::kVersion` `0x20260226`) | `0x20260228` = max(`0x20260228`, SPIR-V `Modification::kVersion` `12`) |
| Description | 64 bytes, packed | 60 bytes, packed |
| Stored record | 8-byte hash + 64 = 72 bytes | 8-byte hash + 60 = 68 bytes |

Measured headers (2026-10-24): a preserved D3D12 store begins
`58 45 50 53 44 58 52 54 20 26 02 26`; a preserved Vulkan store begins
`58 45 50 53 00 00 00 00 20 26 02 28`. The record sizes are confirmed by every
preserved file dividing exactly: 25,068 = 12 + 348 × 72; 9,804 = 12 + 144 × 68.

Each record's hash is `XXH3_64bits(description)` and is checked with the SDK's own
implementation through `rb_blitz_shader_microcode_hash` — the same bounded helper
P2 uses for microcode. A wrong stride, endianness or field layout fails the whole
store instead of publishing wrong recipes. The tests also re-read the four SDK
headers and refuse the recorded version constants when they drift.

The loader itself stops at the first corrupt record and truncates the file on its
next load. The catalogue mirrors that rule: an unverified tail is refused unless
`--accept-truncated-tail` is passed, and then only the verified prefix is used, the
byte count is recorded, and coverage is incomplete.

### Commands

```powershell
# One store, catalogue only (shader resolution not checked -> exit 2).
python scripts/shader_pipeline_catalogue.py --store <store.xpso> --catalogue-out out/catalogue.json

# Merge every preserved store for a backend and verify its identities locally.
python scripts/shader_pipeline_catalogue.py --store <a.xpso> --store <b.xpso> `
  --game-root game --catalogue-out out/d3d12-catalogue.json --merged-store out/merged-rtv.d3d12.xpso

# Cheap re-check against a P2 resolver index, without reading installed content.
python scripts/shader_pipeline_catalogue.py --store <store.xpso> `
  --resolver-index <p2-index.json> --catalogue-out out/catalogue.json
```

`--store` also accepts a directory (searched recursively for `*.xpso`). A catalogue
and a merged store are created exclusively and never overwritten. The merged store
is byte-compatible with the cache: same magic, API selector and version, records
sorted by description hash, every description byte preserved.

Exit codes: **0** = published with complete declared coverage; **2** = published
with explicit blockers (unresolved shader identities, helper recipes whose runtime
key is only a call site, an accepted corrupt tail, or shader resolution not checked);
**1** = invalid input or tool failure.

### Measurements from the preserved runs (2026-10-24)

All inputs are the preserved runs under `out/shader-cache-p0/runs/` — the controlled
P0 traces plus the five owner-driven coverage sessions below; outputs are under
`out/shader-cache-p3/`. Every store parsed to its last record with no duplicates
inside a store and no unverified bytes.

| Input set | Stores | Records | Unique recipes | Referenced shader identities | Resolved |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 (`rtv`), preserved P0 runs | 8 | 801 | 175 | 116 | 116 / 116 |
| D3D12 (`rtv`), plus coverage r1/r2 | 10 | 1,601 | 412 | 206 | 206 / 206 |
| D3D12 (`rov`), coverage r1 | 1 | 219 | 219 | 163 | 163 / 163 |
| Vulkan (`fbo`), preserved P0 runs | 2 | 193 | 144 | 95 | 95 / 95 |
| Vulkan (`fbo`), plus coverage r1 | 3 | 602 | 409 | 200 | 200 / 200 |
| Vulkan (`fsi`), coverage r1 | 1 | 228 | 228 | 172 | 172 / 172 |
| D3D12, both paths | 11 | 1,820 | 631 | 206 | 206 / 206 |
| All four paths, one catalogue | 15 | 2,650 | 1,268 | 208 | 208 / 208 |

**All four declared rendering paths are now captured.** Each alternate path has its own
store (`*.rov.d3d12.xpso`, `*.fsi.vk.xpso`) and is selected explicitly with
`render_target_path_d3d12=rov` / `render_target_path_vulkan=fsi`; the default path on
this machine is `rtv`/`fbo`. The path is part of every recipe's identity, and the two
D3D12 paths share no recipe at all (412 + 219 = 631), so a catalogue that reported only
`rtv` would have implied coverage it did not have. A merged store may not span paths:
the tool refuses that, which is why the 11-store D3D12 catalogue is published without one
and each path keeps its own merged store.

The D3D12 warm store that P0 measured as a **132-pipeline boot preload** is exactly
132 records here; the empty first-play store adds 46, 144, 160, 22, 137 and 16 in
the other runs. The D3D12 coverage sessions started from that 132-record store, added
256 records in the first session (388) and 24 more in the second (412); the Vulkan
session started from the 144-record retail store and added 265 (409). Each backend's
final store **already contains every recipe of that backend's earlier stores**, so
the merges add ordering, provenance and validation rather than new descriptions.

| Backend / path | Recipes | Vertex shaders | Pixel shaders | Vertex mods | Pixel mods | Shader pairs | No pixel shader |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D12 `rtv` | 412 | 71 | 135 | 72 | 145 | 272 | 4 |
| D3D12 `rov` | 219 | 64 | 99 | 65 | 100 | 199 | 2 |
| Vulkan `fbo` | 409 | 70 | 130 | 71 | 140 | 265 | 5 |
| Vulkan `fsi` | 228 | 64 | 108 | 65 | 109 | 206 | 3 |

State axes present (decoded values, not a completeness claim):

- D3D12: MSAA 1x/2x/4x (`host_msaa_samples` 0/1/2, 362/48/2 recipes); depth formats
  0/1; depth funcs 4/6/7; cull modes 0/2; primitive topologies 2 (393), 1 (12) and
  0 (7); strip cut always 0; every used render target is color format 0
  (`k_8_8_8_8`), with write masks 15 (328 recipes), 7 (77) and 0 (7); depth write is
  on for 289 recipes and the stencil test for 3.
- Vulkan: render-pass MSAA 1x/2x/4x (`render_pass.msaa_samples` 0/1/2,
  346/61/2 recipes); primitive topologies 3 (375), 4 (13), 6 (12) and 0 (9); depth
  compare 4 (271), 6 (59), 7 (52) and 0 (27); depth write on for 279 recipes and the
  stencil test for 3; polygon mode 0; tessellation mode 0; sample-rate shading 0;
  used color view format 0; color write masks 15 (331), 7 (69) and 0 (9); six
  distinct render-pass keys.

These are **the recipes the captured sessions reached**, not the renderer's full
reachable set. On both backends they now include title, loading, the options surface,
the menu, songs with every power-up exercised (owner-confirmed) and both rendering
paths of each backend. **Owner scope decision (2026-10-24, gameplay judgment rather
than a measurement): ordinary songs and DLC packages do not vary shader usage**, so
the song/DLC axis is not a coverage obligation; the graphics-settings matrix is the
remaining axis. P8 owns it.

The merged stores are replayed by the same parser and reproduce the unique sets:
`out/shader-cache-p3/merged-rtv-coverage.d3d12.xpso` = 29,676 bytes / 412 records,
`merged-fbo-coverage.vk.xpso` = 27,824 bytes / 409 records, `merged-rov.d3d12.xpso` =
15,780 bytes / 219 records and `merged-fsi.vk.xpso` = 15,516 bytes / 228 records (all
re-sorted by description hash, so not byte-identical to the session store each
summarises); `merged-rtv.d3d12.xpso` = 12,612 bytes / 175 records and
`merged-fbo.vk.xpso` = 9,804 bytes / 144 records are the preserved-P0-only sets.
Nothing in `out/` is distributed.

The 208 recipe-referenced identities **exceed** P2's original 186-identity candidate
by 22 (5 vertex, 17 pixel): the candidate was the union of every `.xsh` translated
shader at that time, and the power-up route reached shaders no earlier store
referenced. All 208 resolve from the installed content with no prior gameplay, and
`config/shader_resolution_candidate.json` was extended to exactly that set (still
hash-only, no shader bytes).

### Coverage sessions (2026-10-24)

Five sessions with the owner driving the game, all on the same workstation, each
prepared by `scripts/shader_cache_trace.py prepare` with its own cache, saves and
profile copies and `--shader_compile_trace=true`:

| Run | Start | Records after | Boot preload | Route and outcome |
| --- | --- | ---: | --- | --- |
| `d3d12-ultimate-coverage-r1` | 132-record learned store | 388 | 132 | Options surface and songs with several power-ups. Ended in an unhandled guest access violation (read of guest `0x3FFF1A00`, thread `0xF8000028`) that the owner attributes to a different ticket; the store still parsed to its last record with **no unverified tail**, so the crash cost no captured evidence. |
| `d3d12-ultimate-coverage-r2` | r1's collected 388-record snapshot | 412 | 388 required / 388 succeeded, 0 failed, 0 missing in 367 ms | Every power-up exercised (owner-confirmed) and the game closed normally, so the cache writers drained. |
| `vulkan-ultimate-coverage-r1` | the 144-record retail `fbo` store | 409 | 144 required / 144 succeeded, 0 failed, 0 missing | Vulkan/Ultimate — a combination P0 never booted — with every power-up exercised (owner-confirmed), closed normally. |
| `d3d12-rov-coverage-r1` | the 412-record `rtv` store plus the 208-shader `.xsh` | 219 (`rov`) | none: the `rov` store did not exist yet | `render_target_path_d3d12=rov`, one song with power-ups, closed normally. The `rtv` store was untouched beside it. Owner reported heavy first-play-style stuttering. |
| `vulkan-fsi-coverage-r1` | the 409-record `fbo` store plus the `.xsh` | 228 (`fsi`) | none: the `fsi` store did not exist yet | `render_target_path_vulkan=fsi` (the device advertises `fragmentShaderSampleInterlock` and `fragmentShaderPixelInterlock`), one song with power-ups, closed normally. |

The Vulkan power-up session seeded **retail** records and then ran **Ultimate** content,
and its final store still contains every recipe of both preserved Vulkan stores, so the
two variants' pipelines overlapped completely on this machine. The alternate paths did
not overlap at all: `rtv` and `rov` share no recipe, and a merged store spanning two
paths is refused by the tool.

In-song work is where these recipes come from on both backends. The `active_song` phase
counters below are cumulative CPU-time sums, not wall time (work overlaps across
workers), but they show the shape clearly: a path whose store is empty creates every
pipeline during the song, and the cost is pipeline creation, not shader translation.

| Session | Guest pipelines created in `active_song` | Creation work (cumulative) | Compile waits |
| --- | ---: | ---: | --- |
| `d3d12` `rtv` r2 (412-record store preloaded) | 226 | 827 ms, worst 19.3 ms | 23, 110 ms total, worst 13.2 ms |
| `vulkan` `fbo` (144-record store preloaded) | 221 | 11.3 s, worst 536 ms | 29, 2.90 s total, worst 168.3 ms |
| `d3d12` `rov` (no `rov` store) | 165 | **61.9 s, worst single 8.38 s** | **29, 11.9 s total, worst 859 ms**, 23 of them ≥250 ms |
| `vulkan` `fsi` (no `fsi` store) | 172, plus 172 placeholders | **85.2 s, worst 1.86 s** | **34, 13.4 s total, worst 701 ms**, 25 of them ≥250 ms |

The ROV and FSI sessions are the owner's "felt like playing for the first time"
reports: on those runs only 44 ms and 1.54 s respectively were spent translating
shaders, so the stalls were driver pipeline creation on an empty per-path store. This
is the first-play cost P4/P6 exist to move off playback; it is **not** a regression
and nothing in P3 changed when a pipeline is created.

| Change | D3D12 recipes | Vulkan recipes | D3D12 identities | Vulkan identities |
| --- | ---: | ---: | ---: | ---: |
| Preserved P0 stores | 175 | 144 | 116 | 95 |
| After the first coverage session | 388 (+213) | — | 197 | — |
| Primary paths (`rtv`, `fbo`) | 412 (+237) | 409 (+265) | 206 (+90) | 200 (+105) |
| Alternate paths (`rov`, `fsi`) | 631 (+219) | 637 (+228) | 206 (no new) | 206 (no new) |

**What this establishes:** the power-up effects, the options surface, the songs
actually played and **all four declared rendering paths** are captured, validated and
locally resolvable, and each store subsumes the earlier ones on its own path. **What
it does not establish:** saturation. Graphics settings were not varied; the owner's
judgment that songs and DLC do not vary shader usage removes that axis from the list
but is not itself a measurement. If a future package or setting does introduce a new
effect, the renderer keeps learning and the catalogue gains a recipe, so the cost is
an extension of this
catalogue rather than a broken guarantee.

### Helper pipeline inventory

Guest pipelines live in `.xpso`; the renderer also creates graphics and compute
pipelines directly (render-target transfer, dump, resolve, presenters, immediate
drawers). Those are not stored in `.xpso`, so P0's trace identifies them by creation
site. P3 inventories every such site in
[`config/pipeline_helper_inventory.json`](../../config/pipeline_helper_inventory.json)
with its key composition and dependencies. The test checks each declared trace site
still appears verbatim in the SDK source, so the list cannot silently drift.

| Backend | Helper | Kind | Created | Trace key (upper byte = `HelperKind`) |
| --- | --- | --- | --- | --- |
| D3D12 | `clear_rtv` | graphics | startup | `kClearRtv`, format index << 8 \| sample index |
| D3D12 | `resolve_copy` | compute | startup | `kResolveCopy`, shader index \| 0x100 when scaled |
| D3D12 | `host_depth_store` | compute | startup | `kHostDepthStore`, 0/1/2 = 1x/2x/4x |
| D3D12 | `rov_resolve_clear` | compute | startup (ROV) | `kRovClear`, 32bpp/64bpp \| 0x100 when scaled |
| D3D12 | `transfer` | graphics | lazy | `kTransfer` + `TransferShaderKey` |
| D3D12 | `transfer_stencil_bit` | graphics | lazy | `kTransferStencilBit` + key; variant = stencil bit |
| D3D12 | `dump` | compute | lazy | `kDump` + `DumpPipelineKey` |
| D3D12 | `immediate_drawer_triangle` / `_line` | graphics | startup | unique site, no key needed |
| D3D12 | `presenter_intermediate` / `_final` | graphics | startup | `kPresenter`, effect \| 0x100 for intermediate |
| D3D12 | `texture_load` / `_scaled` | compute | startup | `kTextureLoad`, load shader index \| 0x100 when scaled |
| D3D12 | `swap_gamma_compute` / `swap_fxaa` / `resolve_downscale` | compute | startup | `kSwapChain`, variant ranges 0x200–0x500 |
| D3D12 | `compute_util` | compute | shared helper | `kComputeUtil` + the caller's key |
| Vulkan | `transfer` | graphics | lazy | `kTransfer`, `render_pass_key` << 32 \| `shader_key` |
| Vulkan | `transfer_masked_samples` | graphics | lazy | same key; variant = sample index |
| Vulkan | `resolve_copy` / `host_depth_store` / `fsi_resolve_clear` / `dump` | compute | startup or lazy | `kResolveCopy` / `kHostDepthStore` / `kRovClear` / `kDump` |
| Vulkan | `texture_conversion` | compute | startup | `kTextureConversion` (one pipeline, format is a push constant) |
| Vulkan | `texture_load` / `_scaled` | compute | startup | `kTextureLoad`, load shader index \| 0x100 when scaled |
| Vulkan | `swap_gamma_graphics` / `_rb_swap` / `_compute` / `swap_fxaa` / `resolve_downscale` | graphics/compute | startup | `kSwapChain`, variant ranges 0x000–0x500 |
| Vulkan | `immediate_drawer_triangle` / `_line` | graphics | startup | unique site, no key needed |
| Vulkan | `presenter` | graphics | startup | `kPresenter`, effect \| 0x100 for intermediate |
| Vulkan | `compute_util` | compute | shared helper | `kComputeUtil` + the caller's key |

**35 entries: 4 site-unique, 31 key-identified, 0 unresolved.** `HelperKind` names the
creator in the key's upper byte and the rule is documented per entry in the
inventory's `trace.key_bits`, so a logged `key=` can be read back into a recipe. Two
tests keep that honest: every declared trace site must still occur verbatim in the SDK
source, and every `kind N (kName)` in the inventory must match the value the SDK header
gives that enumerator. A creation logged with `key=0` at a shared util site is a caller
that has not been keyed yet — it is visible as such rather than mistaken for identity.
The GPU texture-conversion helper was added after these captures and has headless
numeric validation; its gameplay validation remains pending (see
[Vulkan performance](vulkan-performance.md)).

### What is still unproven

- **Reachability.** A captured store set is evidence of exercised pipelines, not
  proof that a setting or a future package cannot add a recipe. With the coverage
  sessions every power-up is represented on both backends and all four declared
  rendering paths are captured, and the owner states that ordinary songs and DLC do
  not vary shader usage (judgment, not a measurement); the graphics-settings matrix
  remains untested. No cartesian enumeration of shader pairs was performed or claimed.
- **Helper key identity.** Carried since 2026-10-24 (patch 0026). What remains is
  that a key is only as good as its fields: a helper whose recipe also depends on a
  setting not in its key would collide. The documented layouts are the current claim;
  P8's route matrix is what would expose a collision.
- **Portability.** A recipe is valid only for the same backend/rendering path,
  translator/settings generation, render-pass format and shader identities. The
  catalogue records the version constants and the path, and refuses a mismatched
  store; it does not prove another GPU, driver or DLC installation is covered.
- **Preparation.** No live GPU pipeline is created by this work, nothing is imported
  into a player's cache, and `.xsh`/`.xpso` files on disk are untouched. P4 and P6
  own that. A merged store has been re-parsed by this tool and structurally verified
  (header, record size, every hash); **no game boot has loaded one yet**.
- **`live_ready`.** Always false: local resolution and a stored recipe are not a
  resident, verified GPU pipeline.

### Validation

`cmake --build --preset win-amd64-release` links `rb_blitz` and the helper. The new
`shader_pipeline_catalogue` ctest runs seventeen hermetic tests: both backends'
field decoding; every header/API/version rejection; title mismatch; an accepted
corrupt tail and its byte count; cross-store deduplication and provenance; hash
collisions; resolver-index and candidate scope; inventory schema/duplicate/site
validation; the key-identity pairing rules (`key` ⇔ `key_identified`, and a keyed
entry must document its bit layout); CLI exit codes and overwrite refusal; a
replayable merged store; a refused mixed-backend merge; directory expansion; the SDK
version pins; the shipped inventory's source/trace-site drift guard and its
`HelperKind` value guard; and — when the local P0 evidence exists — every preserved
store parses with no unverified tail. Full release `ctest` is **55/55** with
`TEMP`/`TMP` under the workspace `out/` (the pre-existing `path_policy` default-temp
issue is unchanged). The tooling itself required no game run; the three coverage
sessions above were owner-driven. After each candidate extension the shader subset
(`shader_asset_resolver`, `_retail`, `_ultimate`, `shader_pipeline_catalogue`,
`shader_compile_trace_enabled`, `_disabled`) passes **6/6**, including the
installed-content check that all 208 candidate identities still resolve. After patch
0026, `apply_sdk_patches.ps1 -Check` replays 25 patches with 0 failed and 0
unexpected SDK files, and the combined catalogue exits **0** — complete declared
coverage across 15 stores and all four rendering paths, which the earlier 13-store
revision (same records, one path per backend, call-site-only helper keys) reported as
exit 2.

Next: P4 separates storage opening from a reusable asynchronous preparation
operation and consumes this catalogue; P6 attaches it to the proven post-A
boundary; P8 validates the route matrix and helper coverage on both backends.

## Part 4 — Graphics preparation lifecycle (was `docs/engine/shader-cache-p4.md`, P4)

P4 separates *preparing* persistent shader and pipeline storage from *blocking the launch* on
it. Before it, both backends ran one long call — open the storage, import every description,
translate every variant, create every stored pipeline — either before the guest started
(`ReXApp::LaunchModule`) or on the command processor thread. The loading UI could not animate
during that call, and a driver pipeline creation could hold the thread for seconds. P4 makes the
same work a **cancellable generation** that the command processor advances in bounded slices
between packets, with explicit progress and a result that is only "ready" when every required
entry produced a real pipeline.

It does not add the visible *Preparing graphics* phase (P6), native caches (P5) or route
validation (P8). It gives them the lifecycle they need, and it keeps the launch-time behaviour as
the default until P6 integrates the guest-owned bar.

### What a generation is

`include/rex/graphics/graphics_preparation.h` (SDK patch 0027) defines:

| Part | Content |
| --- | --- |
| `GraphicsPreparationPhase` | `importing` → `translating` → `creating` → `finalizing`, then one terminal: `succeeded`, `incomplete` (finished, but entries failed/missing, or the storage declared nothing), `cancelled`, `failed` (storage could not be opened). A phase never moves backwards. |
| `GraphicsPreparationKey` | Title id, the caller's configuration epoch and a hash of the resolved cache root. A result is only valid for the key it was produced for. |
| `PipelinePreloadResult` | The P1 counts plus `cancelled`. `Complete()` requires work, nothing unfinished, nothing failed, missing or cancelled. |
| `GraphicsPreparationProgress` | One immutable snapshot: generation token, key, phase, counts, backend and rendering path, `has_catalogue`, cancellation flag, elapsed time and the number of service slices. `ready()` is the single readiness question. |
| `GraphicsPreparationController` | The state machine: `Begin` (refuses a second concurrent generation), `AdvancePhase`, `NoteCatalogue`, entry accounting forwarded to `PipelinePreloadTracker`, `RequestCancel`, `Finish`, `Snapshot`, `WaitForTerminal`, `Reset`. |

Rules the controller enforces for every caller:

- One generation at a time, and the generation token strictly increases across `Begin`/`Reset`,
  so a stale snapshot can never be mistaken for the current one.
- A zero-entry catalogue is **not** coverage. It finishes `incomplete` even when every entry that
  did run succeeded (`Complete()` is false), which is what keeps "the storage was empty" from
  reading as "everything is prepared".
- Cancellation moves not-yet-started entries to a terminal *cancelled* state, which is never a
  success. Work already in flight — a driver creation cannot be interrupted — reports its own
  result, and the pipelines it produced stay resident because they are real.
- `Reset()` (shutdown, re-initialization) releases every waiter; a barrier can never be left
  blocking on work nobody will finish.

### Where it runs

`CommandProcessor` owns the controller and services it from its worker loop
(`src/graphics/command_processor.cpp`):

- one slice per loop iteration, after queued host work and before executing the ring;
- further slices while the ring is idle, so a quiet guest still makes progress;
- an atomic active flag, so the idle spin does not take the controller's lock per iteration.

`BeginShaderStoragePreparation()` on the command processor takes the key, calls `Begin` and
schedules the import with `CallInThread` — the pipeline maps and the storage streams belong to
the command processor thread, so even the import runs there. The caller gets the generation token
back immediately.

Both backends expose the same trio to that thread:

| Method | D3D12 | Vulkan |
| --- | --- | --- |
| `BeginShaderStoragePreparation` | opens the two storage streams, reads and validates every stored description, loads the guest shaders and starts the translation pool, then returns. | the same, but the translation list is only collected — translations are done one per slice. |
| `ServiceShaderStoragePreparation` | while translating: polls the translation workers; after they join: enqueues 32 descriptions per slice and creates up to 4 pipelines on the command processor thread; workers create the rest. | translates one variant per slice, then enqueues 16 creation arguments per slice and creates up to 2 on the command processor thread; workers create the rest. |
| `CancelShaderStoragePreparation` | cancels not-yet-started entries, stops enqueueing, and the generation finishes `cancelled` after in-flight creation is joined. | the same. |

**Preparation never uses the gameplay creation queue.** Each backend gets a second, bounded pool
and queue for preparation. That is the ownership rule that makes the phase safe: `EndSubmission()`
still waits only for pipelines a draw actually needs, and preparation never makes a submission
wait — while a draw that needs a pipeline preparation has already published reuses it once its
real state exists. The Vulkan `startup_loading_` flag is left tied to the old blocking path; the
preparation path does not set it, so submissions keep their normal semantics.

The old blocking `InitializeShaderStorage(cache_root, title_id, blocking)` remains as a thin
wrapper: `Begin`, then drive `Service` to completion on the calling thread, then wait. Its result,
log lines and readiness rules are the P1 contract, now including the cancelled count and the
terminal phase name. The launch path uses it unless
`shader_storage_async_preparation=true`, which starts the generation after `main_thread->Resume()`
so the guest — and whatever the title is drawing — keeps running while it is prepared.

### App-facing surface

- `IGraphicsSystem` gained `BeginShaderStoragePreparation`, `CancelShaderStoragePreparation`,
  `QueryShaderStoragePreparation` and `WaitForShaderStoragePreparation` (default: unsupported),
  implemented by `GraphicsSystem` on top of the command processor. A caller never needs the
  concrete backend type.
- `shader_storage_async_preparation` (GPU category, default **false**) is the launch switch. When
  on, the launch logs `Preparing shader storage for title … beside the guest...` and the guest
  starts immediately; when off, the old `Initializing shader storage …` blocking path runs.
- Progress is readable from any thread; it is a snapshot, not a callback, because the UI phase
  (P6) is decided by the guest's own records, not by a host thread.

### Tests

`tests/graphics_preparation_tests.cpp` → ctest `graphics_preparation` exercises the real header
with real threads (not a copied model): one generation at a time and increasing tokens, forward
only phases, progress counting only completed work, the zero-entry catalogue that can never be
ready, cancellation that cancels queued entries without counting them as success while a running
entry keeps the generation open, the delayed-worker barrier (waiting at least 150 ms for a 200 ms
entry), shutdown releasing a waiter, idle snapshots, and the tracker's cancelled terminal state.

### Gates and limits

- Verified in this session: the full release build, the complete ctest roster including the new
  suite, and the SDK patch audit with patch 0027. Numbers are recorded in the P4 record of the
  [shader-cache plan](../plans/in-progress/2026-10-10-shader-cache-plan.md#p4--add-asynchronous-preparation-without-starving-the-loading-screen).
- **Not measured:** no game/UI run was made for P4. This document makes no claim that in-song
  compile work dropped, that a warm boot is faster, or that every pipeline a song needs is in the
  catalogue — those are P8's route matrix, P5's native caches and P3's coverage respectively.
- **Not yet integrated:** the visible *Preparing graphics* progress and the post-A gate are P6;
  the loading screen's own pipelines are whatever the title already draws before preparation
  starts, because preparation is scheduled after the guest resumes and never runs on the
  submission path.
- The configuration key is stored and compared, but nothing invalidates a generation
  automatically yet: a settings change that alters pipelines must start a new generation (P7).

### Sources

| Source | Role |
| --- | --- |
| `rexglue-sdk/include/rex/graphics/graphics_preparation.h` | The lifecycle, key, progress and controller. |
| `rexglue-sdk/include/rex/graphics/pipeline_preload.h` | P1 entry states, extended with cancellation. |
| `rexglue-sdk/src/graphics/command_processor.cpp` | The worker loop's slices and the app-facing begin/cancel/query/wait. |
| `rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp`, `src/graphics/vulkan/pipeline_cache.cpp` | The backend import, translation, enqueue and creation slices and their preparation pools. |
| `rexglue-sdk/src/ui/rex_app.cpp` | The launch switch between the blocking and asynchronous forms. |

## Part 5 — Native pipeline caches (was `docs/engine/shader-cache-p5.md`, P5)

P5 persists the *driver's own* pipeline results beside the shareable recipes. Before it, both
backends replayed a validated recipe catalogue on every boot: the portable stores describe what the
title asked for (`.xsh` guest microcode, `.xpso` pipeline descriptions), and the driver turned each
recipe into a pipeline object from nothing. A D3D12 `ID3D12PipelineState` can hand the driver its own
cached blob, and Vulkan has a `VkPipelineCache`; both are exactly the state that makes the second
creation of a pipeline cheaper, and both are only valid for the adapter, driver build and translator
that produced them.

This document records what P5 added, how it is keyed and validated, what it refuses, and what is
**not** yet established. It does not claim a timing win: no cold-versus-warm boot was measured in
this pass, and the plan's own rule is that cache-file existence is not a performance result (P8 owns
that measurement).

### 1. The component

`rexglue-sdk/include/rex/graphics/native_pipeline_cache.h` (SDK patch
`0028-native-pipeline-caches.patch`) is header-only and backend-agnostic, like
[`pipeline_preload.h`](../../rexglue-sdk/include/rex/graphics/pipeline_preload.h) and
[`graphics_preparation.h`](../../rexglue-sdk/include/rex/graphics/graphics_preparation.h). It keeps
three things apart, because confusing any two of them turns a cache into a correctness bug:

| Part | What it is | Where it is used |
| --- | --- | --- |
| `NativePipelineCacheIdentity` | Adapter (vendor, device, subsystem, revision), driver version, Vulkan `pipelineCacheUUID`, backend, title id, rendering path, container schema version, `PipelineDescription::kVersion`, translator version. `Hash()` is the identity an entry file records. | Written into every entry; a file under another identity is refused, not reinterpreted. |
| Entry key | The portable description hash (`XXH3_64bits(&PipelineDescription)`) for a D3D12 guest pipeline, or one constant for Vulkan's whole-cache entry. | Names *which* pipeline the payload belongs to, independently of the identity. |
| Payload | Opaque driver bytes: an `ID3D12PipelineState` cached blob, or serialized `VkPipelineCache` data. | Never interpreted here - only length- and hash-checked. |

`NativePipelineCacheStore` writes one file per entry:

```text
<identity-dependent directory>/<key, 16 uppercase hex>.pso
  NativePipelineCacheEntryHeader (40 bytes)
    magic 'RBNP' | format version | identity hash | key | payload hash | payload size
  payload (opaque)
```

Rules the store enforces:

- **A rejected entry is never handed to a driver.** Wrong magic, wrong format version, wrong
  identity, wrong key, a payload size that does not match the file, or a payload hash mismatch all
  end in `rejected`; the file stays on disk for the owner to `Forget()` once it has decided to replace
  it.
- **Writes are atomic and bounded.** A flush writes a temp sibling and renames it over the entry, so a
  reader sees the old or the new complete file. One entry is capped at 64 MiB, pending bytes at
  128 MiB / 8192 entries, the directory at 1 GiB / 32768 entries. Everything refused is counted, never
  silently dropped.
- **Serialization happens outside play.** Entry bytes are *staged* in memory when a pipeline is
  created and only reach the disk on an explicit `Flush()`, which both backends call when a
  preparation generation finishes and when the storage shuts down - never from a draw.
- **Two instances sharing one cache directory cannot corrupt each other.** Each write is a complete
  atomic replace, and Vulkan's whole-cache file is merged (see §3) rather than blindly overwritten.
- A disabled store answers nothing and writes nothing, so a machine without a writable cache
  directory keeps running on the shareable recipes alone.

### 2. Direct3D 12 - cached PSO blobs

`PipelineCache` opens `shaders/local/<TITLEID>.<rtv|rov>.d3d12/` from `OpenShaderStorage`, once the
rendering path is known, and derives the identity from the live device: `ID3D12Device::GetAdapterLuid()`
→ `IDXGIFactory4::EnumAdapterByLuid` → `DXGI_ADAPTER_DESC1` (vendor, device, subsystem, revision) plus
the UMD version from `IDXGIAdapter::CheckInterfaceSupport(__uuidof(IDXGIDevice))`. The UMD version is
the field a driver update changes; when a driver does not implement `CheckInterfaceSupport` the field
stays zero and the blob is still guarded by the driver's own validation plus the retry below.

Creation (`CreateD3D12Pipeline`) now:

1. hashes the `PipelineDescription` (the same hash the portable store uses) and loads that entry;
2. passes the bytes as `state_desc.CachedPSO` when an entry exists;
3. on failure **with** a blob: logs the HRESULT, `Forget()`s the entry, clears `CachedPSO` and creates
   the pipeline the ordinary way - a rejected blob is never reported to the caller, and the portable
   recipe is untouched;
4. after a successful creation that did not use a blob, calls `ID3D12PipelineState::GetCachedBlob()`
   and stages the result.

Blob capture is per *newly created* pipeline, not per draw, and the write happens at the next flush.

### 3. Vulkan - a real `VkPipelineCache`

`VulkanPipelineCache::OpenNativeCache` creates `shaders/local/<TITLEID>.<fbo|fsi>.vk/` and one
`VkPipelineCache` from the stored bytes. Validation happens twice before the driver sees anything: the
store's container (identity, key, hash) and Vulkan's own `VkPipelineCacheHeaderVersionOne`
(`headerVersion`, `headerSize`, `vendorID`, `deviceID`, `pipelineCacheUUID` against
`VkPhysicalDeviceProperties::pipelineCacheUUID`). If `vkCreatePipelineCache` still refuses the data,
the store entry is forgotten, an empty cache is created, and the run continues.

The handle is published on `VulkanDevice::shared_pipeline_cache()`, and **every** creation path on the
device passes it - guest graphics pipelines, the render-target transfer/resolve pipelines, the
swap-effect gamma/FXAA pipelines, the presenter and immediate-drawer pipelines, and
`ui::vulkan::util::CreateComputePipeline`. That is why the compute helpers the P3 inventory names
(transfer, dump, clear, resolve copy, host depth store) are covered on Vulkan without touching their
sites individually: they are device-wide creations, and the cache is device-wide state.

Serialization is guarded by the one condition that makes it legal: no pipeline creation may run while
the cache is snapshotted. `SaveNativeCache` therefore refuses to serialize while
`IsCreatingPipelines()`, and a new gameplay creation can only be enqueued by the command processor
thread, which is the thread doing the snapshot - so "queue empty and no worker busy" is a real
quiescence window. When a preparation generation finishes while the guest is playing, the snapshot can
be skipped and logged; the shutdown path (after the creation threads are joined) always succeeds.

Concurrent instances: at save time the store's current file is read back; if it is valid and differs
from what this instance loaded, it is created as a temporary `VkPipelineCache` and merged with
`vkMergePipelineCaches` before serializing. The last writer therefore includes the other run's
pipelines instead of discarding them. `CloseNativeCache` stops publishing the handle first, and skips
`vkDestroyPipelineCache` when a creation is still in flight (re-initializing the storage while the
guest draws), abandoning the object rather than racing it.

`vkCreatePipelineCache`, `vkDestroyPipelineCache`, `vkGetPipelineCacheData` and
`vkMergePipelineCaches` were added to the SDK's Vulkan 1.0 device function list
(`include/rex/ui/vulkan/functions/device_1_0.inc`); all four are core 1.0, so their absence would mean
a broken loader, and a missing pointer still fails device creation the same way every other entry does.

### 4. The switch and the log

`native_pipeline_cache` (GPU, default **true**, requires restart) disables the whole layer on both
backends; the shareable recipe path is unaffected either way. It exists for A/B measurement and as a
rapid escape hatch, not as a product feature.

Boot and boundary lines, all at info level except the failures:

```text
Native pipeline cache: adapter 10DE:13C2 subsystem 1111 revision A1 driver 32.0.15.7602
Native pipeline cache: adapter 10DE:13C2 driver 0x…, 123456 byte(s) restored
D3D12 rejected a stored cached-PSO blob (hr=0x…); creating the pipeline without it and replacing the entry
Native pipeline cache: 132 cached PSO blob(s) written (d3d12 native pipeline cache: 132 reused, … entry file(s) / … byte(s) on disk)
Native pipeline cache: serialization deferred at shader storage shutdown, pipelines are still being created
```

### 5. Tests

`tests/native_pipeline_cache_tests.cpp` → ctest `native_pipeline_cache`, 86 checks against the real
header with a real scratch directory: the identity hash covers every compatibility field, an entry
round-trips, a flipped payload byte / truncated file / forged size / wrong key / foreign identity are
all refused, a store configured for another adapter refuses the entry on load and leaves the file in
place, staging replaces atomically and counts one pending entry per key, `Forget` removes the entry
and the directory counters, the size bounds refuse instead of writing, and a disabled store answers
nothing.

### 6. Gates and limits

- Verified in this session: the full release build with both backends (`rexruntime.dll`,
  `rexgpu-xenos.dll`, `rb_blitz.exe`), the complete ctest roster (**60/60**, including the new suite),
  and the SDK patch audit (**27 patches, 0 failed, 78 patched files, 0 unexpected**).
- **Not measured:** no game/UI run was made in this pass. Nothing here claims that a song now compiles
  less, that a warm boot is faster, or what the cache costs on disk in practice - those need P8's
  route matrix (cold, replay, full restart, backend switch) with the preparation time and disk size
  reported per backend.
- **P6/P7 still own the lifecycle around it:** the visible *Preparing graphics* phase and the post-A
  gate are P6; invalidation by settings/content/SDK change and partial-write handling are P7. This
  layer only refuses what its own identity and the driver reject.
- **Direct3D 12 scope:** guest graphics pipelines only. D3D12's helper compute pipelines are created
  from `ui::d3d12::util::CreateComputePipeline`, which does not see the cache's store - and the ones
  created during command-processor initialization run before a storage root exists, so they could not
  use it even if the signature were extended. Guest PSOs are the measured dominant cost there (81 per
  song on an empty cache — part 1, Measured runs); extending the layer to the
  helper compute sites is a follow-up measurement, not a claim of this pass.
- Persisting translated shader bytecode (a third layer) stays conditional on a measurement that the
  two layers are not enough; P5 deliberately did not add it.

### Sources

| Source | Role |
| --- | --- |
| `rexglue-sdk/include/rex/graphics/native_pipeline_cache.h` | The identity, the entry format and the bounded store. |
| `rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp` | Adapter identity, blob load/retry/capture, flushes at preparation end and shutdown. |
| `rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp` | `VkPipelineCache` creation, validation, merge-at-save, quiescence rule, close. |
| `rexglue-sdk/include/rex/ui/vulkan/device.h` | The device-wide handle every creation path reads. |
| `rexglue-sdk/include/rex/ui/vulkan/functions/device_1_0.inc` | The four pipeline-cache functions the loader now resolves. |
| `rexglue-sdk/src/graphics/graphics_system.cpp` | The `native_pipeline_cache` cvar. |
| `patches/rexglue-sdk/0028-native-pipeline-caches.patch` | The applied change set. |
| [`tests/native_pipeline_cache_tests.cpp`](../../tests/native_pipeline_cache_tests.cpp) | ctest `native_pipeline_cache`. |

## Part 6 — Post-A graphics preparation (was `docs/engine/shader-cache-p6.md`, P6)

2026-10-24. Implementation of the [P6 prompt](../plans/in-progress/2026-10-10-shader-cache-plan.md#p6--integrate-the-post-a-preparing-graphics-phase-and-native-loading-bar).
Status: implemented and release/headless checked. Cold/warm UI,
mouse-disabled, focus, trial and error-route evidence remains unproven until an
authorized operator runs those routes. P8 retains gameplay/timing acceptance.

### Loading boundary and guest UI

`src/diag/shader_trace.cpp` observes the real native connect attempt at
`0x822CB0D0`. The shared poll hook in `src/hooks/dlc_refresh.cpp` retains the
DLC-refresh yield and, at state 29, `0x822CB618` tests content completion:
`BE32(BE32(0x8280A64C)+44) == 1`. This is the same predicate as native
`0x82375048`, latched before the original poll advances to name entry/login.
Mode 1's song-cache/global-options load and other modes' required trial/global
options precede state 29 in `0x822CA460`. Option at a save prompt posts the
original save choice; Option at name entry occurs after this content boundary.
Host pipeline flags/device choices are final before guest launch; Ultimate's
menu-entry `check_trial` changes input/overscan, not the host pipeline configuration.
Later settings/content invalidation remains P7.

`AddGraphicsLoadingGate` in `src/settings/menu_payload.cpp` authors the effective
compiled connect record. It requires exactly the original Confirm and Option
menu continuations, replacing both with `rbbz_begin_graphics`. R3's read-time
`AutoAccept` detects this method and calls it too, rather than adding another
menu transition. All named native states, save choices and content enumeration
remain the title's. Unknown record shapes refuse the required payload and guest
launch (`CanLaunchModule`, SDK patch 0030), instead of silently leaving a bypass.

While pending, the guest changes its own `status.lbl` to **Checking graphics
cache...** or **Preparing graphics... completed / required**. It writes its own
loaded directory's `current_content` / `total_content` properties; the original
Flow draws `progress_bar.grp` and `content_percent.lbl`. Unknown totals remain
-1, which hides the determinate bar. These are graphics-owned values: the DLC
progress object's counts and lifetime are never reused or overwritten.
No host drawing or overlay is added.

The ServerConnectPanel native poll does not call generic UIPanel Poll. The hook
therefore calls the original base `0x824B5F68` only while graphics is pending,
on isolated registers and a reserved stack frame. That dispatches the authored
`poll` method and keeps the directory polling. This path runs with mouse navigation
disabled and does not depend on host input polling; its visual behavior is unproven
until a UI run. Native polling, window events and GPU submission continue.

`rbbz-graphics/` queries ride the already proven guest `file_exists` builtin ABI;
current/total return integer DataNodes, and all other files retain their normal
behavior. The channel is independent of the settings bridge diagnostic switches.
The record tests live readiness before changing a visible widget: if work completed
during ordinary loading, the original continuation executes immediately. Otherwise
Confirm/Option are suppressed during work and Back keeps the original title return.
On failure, A retries graphics in the same connect attempt and B explicitly ignores
preparation for this process (owner refinement below). Exit retires the pending continuation. The host releases a requested attempt exactly
once, only for its expected successful P4 generation and configuration epoch.
A zero-entry, missing, failed, cancelled or stale generation cannot release it as
ready. The owner-authorized Ignore action is a separate, logged bypass that makes
no readiness claim.

### Local inputs and renderer lifecycle

`config/graphics_preparation.json` embeds the P3 union: 412 rtv, 219 rov, 409 fbo,
228 fsi recipes and 208 shader source locators. It contains renderer descriptions,
hashes, offsets, vertex-fetch metadata and declaration recipes; no retail shader
instructions, developer paths or native GPU blobs. `graphics_preparation_manifest.py`
rebuilds it from a validated P3 catalogue and the owner's installed assets.
CMake embeds it in the executable, so runtime does not need Python or an external
manifest file. The P9 installer audit/release work remains separate.

At the safe post-A boundary a cancellable host worker validates the local .xsh,
resolves missing shaders from the mounted patch-before-main archive view and the
already fingerprint-validated loaded executable, links vertex declarations using
the P2 algorithm, and checks every final XXH3 identity. It merges portable recipes
with local discoveries; hash collisions, unsupported versions and corrupt inputs
fail closed. Writes are flushed temporary siblings atomically replaced before
GPU import. Warm runs validate cached microcode and avoid asset extraction when
all identities exist. Native GPU restoration is still verified in this process.

`src/graphics/preparation_loading.cpp` begins P4 only after that worker finishes.
The renderer selects its real rendering path and imports the corresponding union.
Both backends use their separate P4 preparation queues and P5 native persistence.
`DeferShaderStoragePreparation` (SDK patch 0029) opts this title out of both old
bulk launch paths, including the async diagnostic; pre-A draws bootstrap on demand.
Cancelling joins extraction and requests renderer cancellation; a new attempt waits
for its predecessor to retire. Reopening a terminal/cancelled store closes its old
writer first, retaining the live pipeline maps. A successful live generation can
be reused on a later connect/refresh in the same process; P7 owns invalidation.
Window close retires the worker before runtime teardown.

### Limits and verification

The manifest covers captured graphics states; the graphics-settings matrix and
new helpers created only by later scenes remain P8 obligations. This is not a claim
of zero in-song compile work. D3D12 helper-compute native reuse retains P5's limit.
Compressed/replaced shader assets, unwritable or incompatible storage, and missing
shader inputs report a guest failure message with native Try Again / Ignore actions. They never report
readiness; broader recovery and layer-specific invalidation remain P7. Concurrent
instances' portable-store merge policy remains unproven (P5's native-store policy
is separate).

Headless evidence is kept under `out/shader-cache-p6/`: installed-source extraction
and exact identity verification, production compiled-record/R3 route checks, the
release build, ctest and SDK replay audit. 2026-10-24 results:

- Release build links `rb_blitz.exe`, `rexruntime.dll` and `rexgpu-xenos.dll` with
  both backends. Build evidence: `out/shader-cache-p6/build.log`.
- The C++ installed-source extractor produced 208/208 shaders matching the independent
  Python P2 resolver byte for byte. All four stores pass the P3 parser/hash checks:
  412 rtv / 219 rov / 409 fbo / 228 fsi. A second extraction is identical; the
  synthetic warm test proves no installed-asset read when saved identities suffice.
  Evidence: `extraction-check.json` under that directory and ctest `preparation_assets`.
- The production compiled-record check passes 12 assertions on the installed
  `server_connect.dtb`: both manual continuations converge, applying the byte-neutral
  R3 transform adds only the same gate call, and the only `main_menu` continuation
  remains inside the ready handler. `menu_payload` and `refresh_discovery` pass.
- Full release ctest: **64/65 pass**. The unchanged `path_policy` case fails at
  `tests/path_policy_tests.cpp:88` for a nonexistent descendant, including a standalone
  rerun; P6 does not change that test or `src/fs/path_policy.h`. `ctest.log` records it.
- SDK `-Check`: **29 patches, 0 failed, 79 patched files, 0 unexpected** (`sdk-check.log`).
- Docs checker: 40 errors from pre-existing source lines, 0 new error lines; the
  missing RhythmVerse/binding-row plan references remain outside this change.
  Evidence: `docs-check.json` and `docs-baseline-check.json`.

No game/UI has been driven by the agent during this implementation. The owner chose
manual verification and reported the two failures recorded below; successful
preparation and the recovery UI still require a new owner run after this fix.

### Owner failure report and in-place recovery, 2026-10-24

The owner's screenshot shows the guest failure label and B Back. The preserved
`out/shader-cache-p6/recovery/owner-failures.log` (source `rb_blitz_327.log`) records
attempt 1 failing at 22:13:33 and attempt 2 at 22:16:30, both with
`local catalogue preparation: installed shader asset is missing`. Neither attempt
reached GPU preparation. The second title/connect pass repeated native DLC loading.

Cause: `ArkIndex::Find` matches stored paths exactly. Root shader entries are
`./xbox_shaders` / `./xbox_preinit_shaders`, whereas the manifest requests their bare
names. The original headless test read already-extracted asset files, so it missed
the runtime ARK-name boundary. `preparation_archive.h` now resolves only these two
known names with or without the leading dot separator, retaining patch-before-main
precedence and validating final shader identities as before. Missing-asset logs now
include the requested name. The new archive-mode headless test reads the real ARK
headers/parts using the same lookup helper rather than pre-extracted shader inputs.

Owner-requested recovery replaces leaving the loading screen after failure:

- **A Try Again** joins the completed extraction worker, clears only failed graphics
  state and starts a new graphics generation in the same connect attempt. The
  content-ready latch remains set; no title navigation, save reload or DLC discovery
  is requested. A second A while preparation runs cannot start another retry.
- **B Ignore** invokes the saved menu continuation and bypasses graphics preparation
  for this process. If extraction failed before storage opened, it requests the
  existing learned storage asynchronously so ordinary on-demand compilation and
  cache recording can continue. A GPU generation already ending in failure has
  opened its storage/writer. Ignore is explicitly logged and never marks graphics
  ready; covered-first-song guarantees do not apply to the ignored session.
- The title draws both actions using its existing `continue.ihp` / `back.ihp` and
  `set_action_token`; the failure label is shortened to avoid the previous sentence
  spanning the loading screen. During work, B retains its normal Back behavior.

Recovery validation (2026-10-24):

- The archive-mode test resolves `./xbox_shaders` and `./xbox_preinit_shaders` through
  the production lookup helper, extracts all 208 shaders and builds the four stores.
  Every output file is byte-identical to the independent P2-verified baseline
  (1,268 recipes); a warm repeat does not read either shader asset. Evidence:
  `out/shader-cache-p6/recovery/archive-test.log` and `archive-output-check.json`.
- The production-record headless check passes **32 assertions**, executing its
  failure button branches with a simulated host: Retry reaches no content/save call
  or navigation, repeated A during work cannot retry again, and Ignore navigates once
  and retires the guest pending flag. Evidence: `guest-recovery-test.log` there.
- The affected release ctest subset passes **9/9** (shader resolvers/catalogue,
  preparation lifecycle/assets, refresh discovery and menu payload/options).
  Evidence: `ctest.log` there. The prior full-suite path_policy failure is unchanged.
- The release rebuild passes (`build-final.log` there). No SDK patch changed
  in this recovery fix. The final docs checker reports 49 reference errors: the
  previous 40 plus 9 references to P0, whose file is concurrently staged for removal.
  Evidence: `docs-check-final.json`; this recovery does not restore that removal.

UI appearance/input verification of the new recovery actions remains with the
owner; no additional game/UI run was driven by the agent.

## Sources

The six per-delivery records this document merges, in part order:

| Part | Was | Delivery |
| --- | --- | --- |
| 1 | `docs/engine/shader-cache-p0.md` | P0: the post-A loading gate, the trace instrument and the 2026-10-24 measurements |
| 2 | `docs/engine/shader-cache-p2.md` | P2: local shader resolution from installed content |
| 3 | `docs/engine/shader-cache-p3.md` | P3: the pipeline recipe catalogue and helper inventory |
| 4 | `docs/engine/shader-cache-p4.md` | P4: the cancellable graphics-preparation lifecycle |
| 5 | `docs/engine/shader-cache-p5.md` | P5: native pipeline caches |
| 6 | `docs/engine/shader-cache-p6.md` | P6: the post-A guest loading gate and its recovery UI |
