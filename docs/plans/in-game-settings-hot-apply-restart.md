# In-game settings P9 — hot apply, and the restart coordinator

Status: **P9's host half executed** (2026-10-11). Per-row strategy recorded; one row proven hot and
applied live in a boot; the restart coordinator and its bootstrap written and measured against real
processes. Two items are deliberately left to the coordinator / a later UI pass, and they are named
in §6 rather than implied away.

Parent plan: [in-game-settings-plan.md](in-game-settings-plan.md) §5.4, §5.5, §8, §10.1 (P9).
Baseline: `b940bd1` plus the P9 working tree. Nothing committed.

---

## 1. What this package is, and what it is not

§5.5 replaced "v1 is restart-only" with a per-row decision: a row is `hot` when its value can be
applied to the **running** process **with a measured live effect**, and `restart` otherwise. §8 then
defines what happens for the rows that are left: save first, spawn exactly one child that waits for
this process to die, and only then close.

P9 is therefore two seams and one coordinator:

| Piece | File | What it owns |
| --- | --- | --- |
| Strategy in the frozen model | `src/settings/model.h` | `Descriptor::strategy` + `strategy_evidence`, replacing the v1 `restart_scoped = true` constant |
| The strategy table | `src/settings/schema.cpp` | One entry per `SettingId`, with the evidence string for a `hot` row |
| The application seam | `src/settings/hot_apply.{h,cpp}` | Split a commit into "apply now" and "needs a new process"; never claim an apply that did not happen |
| The runtime applier | `src/hooks/hot_apply_runtime.{h,cpp}` | The one place that knows **how** a hot row reaches the game |
| The channel's live half | `src/hooks/settings_bridge_hooks.cpp` | After a durable commit: plan, apply, log, and keep what did not take effect in the restart set |
| The restart coordinator | `src/platform/game_restart.{h,cpp}` | Handoff token, argument rebuild, exactly-once request, spawn, wait, bootstrap |

It is **not** the authored restart prompt (§6.1) and **not** the boot wiring that calls the
coordinator (`src/main.cpp`'s bootstrap entry, §6.2) - both are named, with the exact shape they
need, in §6.

## 2. The strategy table, and why exactly one row is hot

`src/settings/schema.cpp` carries a single table (`kStrategies`), materialized into
`Descriptor::strategy` by `BuildDescriptors` and read without a descriptor list by
`schema::StrategyOf`. Tests assert the table is complete, that a `hot` row always carries evidence,
that a `restart` row never does, and that it agrees **row by row** with the provider adapter's own
`RowBindings()` table (`tests/settings_hot_apply_tests.cpp`).

| Row | Strategy | Why |
| --- | --- | --- |
| `graphics.vsync` (`kVsync`) | **hot** | **Measured**: with the guest running, the host flipped this exact cvar and the discovery rate moved from 16.66 ms per item to 5.68 ms (46.7 s → 15.96 s for 2,807 items), the log recording `vsync false, was true` 7 ms after the flip — [dlc.md](../dlc.md) §"Frame pacing for the discovery". The mechanism is the GPU plugin's worker re-reading `vsync` every iteration. |
| everything else | restart | Nothing has a measured live effect. Notably `interface.hide_hud` — the parent plan §5.5 named Hide HUD as its hot example **before** P6 measured when the provider reads its values (the payload's loader fills its DTA variables before the first frame), so the honest answer for it is `restart`. The render-path rows are structurally restart (§5.5), and the remaining native rows are read at boot by their consumer. |

Two candidates have a live read in the code and **no measurement yet**, and they stay `restart`
until one exists — this is the narrowed next experiment:

* `general.dlc_menu_songs` (`enhancements_menu_dlc_songs`): `src/hooks/shell_music.cpp` reads the
  toggle when the menu is entered, so a live write should change the *next* menu entry's pool.
  Experiment: boot with a DLC library, apply the row live, leave and re-enter the main menu, and
  check for `shell_music: R9 plays the loaded DLC...` where the control boot has
  `shell_music: R9 off`. That is a log-line observation of a live effect, which §5.5 accepts.
* `renderer`/`anti_aliasing`/`resolution` are **not** candidates: re-initializing the renderer from
  inside a menu is its own spike (§5.5 explicitly forbids trying).

## 3. The application seam (`src/settings/hot_apply.h`)

```text
PlanHotApply(changed, descriptors) / PlanHotApplyByStrategy(changed) -> HotApplyPlan{live, pending_restart}
ApplyHotPlan(plan, applier) -> one HotApplyOutcome per live row (applied | no applier | refused | failed)
RestartSetAfterApply(plan, outcomes) -> pending_restart + every live row that did not take effect
```

Two rules are mechanical rather than documented:

* **A hot row with no applier is a restart row.** A build that cannot perform the write must not tell
  the player the value already took effect: `ApplyHotPlan` reports `kNoApplier` and
  `RestartSetAfterApply` puts the row back into the restart set. This is why a build the coordinator
  has not wired yet still behaves honestly.
* **`applied` means written and read back.** `RuntimeHotApplier` reads the cvar again after the
  write and fails the outcome if the value did not take, so a lifecycle that silently ignores a
  runtime write cannot be reported as applied.

The *coordinator* must ask `RestartSetAfterApply` for its prompt decision - not the descriptors -
because a session can contain a hot row whose applier is missing (plan §5.5's "the UI never claims
more than it can deliver").

## 4. The runtime applier, and the thread rule

`src/hooks/hot_apply_runtime.cpp` knows exactly one row today:

```text
graphics.vsync -> rex::cvar::SetFlagByName("vsync", "true"|"false")
```

* **By name**, because `vsync` is defined in `rexruntime.dll`'s GPU category and this project defines
  no storage symbol for it (the same reason R7 reads it by name).
* **On a host thread.** The commit that triggers an apply arrives on the *guest* thread - it is the
  guest's own `write_file` - and a guest thread must not touch the cvar registry
  ([song-cache-plan.md](song-cache-plan.md) §7). `Apply` therefore runs the write on a short-lived
  thread and **joins** it: R7 goes async because it is on the frame path, whereas a settings commit
  already writes a file synchronously, and joining is what lets the seam answer honestly.
* A row it does not know is reported as `kNoApplier` by the seam, never guessed.

`settings_bridge` gained two things so a boot can see this:

* `MemorySink::last_commit_changes()` - the tokens a commit **really changed** (a value written back
  unchanged reports nothing, so a no-op session cannot re-apply a hot row);
* `settings_hot_apply_diagnostic` (env `REX_SETTINGS_HOT_APPLY_DIAGNOSTIC=1`) - puts a hot row
  through the real channel (a `set` and a `commit`, exactly what the menu writes) at start-up, so a
  boot log proves the live path and not only the tests. It writes the row's own cvar and therefore
  changes that run, which is why it is off by default.

## 5. Evidence (2026-10-11)

Harness: `out\_swap\p9\run.ps1 -Label <name> [-Diagnostic] [-FullBoot] [-Seconds n]`. It boots
`rb_blitz.exe` against a disposable `--user_data_root`, waits for the channel's own marker in the log
and stops the game; `-FullBoot` runs P6's boot route to the title screen instead.

| Arm | Log | Observation |
| --- | --- | --- |
| `control` | `out/_swap/p9/control/control.log` | `settings_bridge: live applier: graphics.vsync -> cvar 'vsync'`, and **nothing else**: no row applied, no cvar written. The channel is silent on a boot that changes nothing. |
| `hot` | `out/_swap/p9/hot/hot.log` | `hot-apply diagnostic committed 'graphics.vsync' -> ok`; `settings_bridge: applied 'graphics.vsync' to the running game; the row carries no pending state`; `hot_apply: vsync = false (was true), applied to the running game`; `1 of 1 changed row(s) applied live; 0 row(s) still need a restart`. |
| `full` | `out/_swap/p9/full/full.log`, shot `out/_swap/p9/full/shots/01-menu.png` | The same apply, then a boot that carries on: the pad reaches the title screen with the value already changed mid-process, no fault in the log (`violation`/`guest access`/`fatal`: zero hits). |

The two apply lines come from **different thread ids** (`t13976` guest thread vs `t26980` the applier's
own thread), which is the thread rule visible in the log.

Claim boundary, stated plainly: the arms prove the *write, the read-back and that the process keeps
running*; the **live effect on the frame rate** is R7's measurement cited in §2, not re-measured
here. A boot that drives the merged menu's V-Sync row and measures the frame-rate change is the
acceptance test §12 should run (the row's own path is now real, so it can).

Tests: `ctest` is **33/33** green in `out/build/win-amd64-release`, including two new targets:

* `settings_hot_apply` - the strategy table (complete; hot ⇒ evidence; restart ⇒ none; the schema and
  the provider adapter agree), the split, and every apply outcome including the null-applier case.
* `game_restart` - the token (round trip, bound, and eleven refusal shapes), the Windows quoting
  rule, the argument allowlist rebuild (one-shot/transient arguments dropped, explicit developer
  arguments kept, the target's own flags), the state machine (exactly-once, stale revision,
  nothing-saved, spawn failure with a retry, no-launcher), and **real process legs**: a child
  started and waited for, a failing spawn, a child that waits for its parent's *verified identity*
  before it does anything, the reused-pid case that must not hold it hostage, and the timeout that
  refuses to start beside a living parent.

## 6. What P9 did not do

### 6.1 The authored restart prompt (UI)

The §8 visual prompt - native retail error/confirmation resources, "A Restart now / B Close" - is
authored DTA work in `src/ui/settings_screen.*`, and it is not in this pass. What exists today is
the seam it will call: the guest writes `rbbz-settings/restart/-/-` (the op is already in the
protocol), and a commit's restart set is what decides whether the prompt appears at all. The Target
row currently commits and leaves without offering the soft restart, which is the item §3.1 of the
plan describes and this pass did not close.

### 6.2 The boot wiring (coordinator)

`src/main.cpp` does not yet call the bootstrap, and nothing calls `RestartCoordinator::Request`.
The two insertions are small and exact:

1. **An early bootstrap mode in `rb_blitz.exe`**, before `REX_DEFINE_APP` starts the SDK: if the
   command line carries a handoff token, `platform::RunBootstrap(argc, argv, nullptr)` waits for the
   parent's verified identity and returns `kStartGame` with the one-shot arguments removed. Doing it
   in the existing executable avoids a second binary in the payload (§8 prefers this); a dedicated
   helper is the fallback and needs a packaging test.
2. **The request**, after the settings save barrier and on the UI thread:

```cpp
platform::LaunchContext context;
context.executable = /* this executable */;
context.game_data_root = /* the absolute root this boot used */;
context.working_directory = /* its folder */;
context.launcher_profile = /* the identity this boot was started with */;
context.target = /* the newly persisted target's token */;
context.developer_arguments = /* THIS boot's explicit dev flags, minus the retired ones */;
platform::RestartCoordinator coordinator(platform::CreateChildLauncher().release());
const platform::RestartOutcome outcome = coordinator.Request(saved_revision, current_revision, request_id, context);
if (coordinator.CloseRequested()) { /* ask the UI to close, then let OnClosing run */ }
```

`request_id` may be 0 (that is what a DTA script sends); the token still gets a non-zero identity.
`developer_arguments` is an explicit allowlist supplied by the caller: this module drops the retired
ones (`--restart-handoff=*`, `--restart-bootstrap`, anything naming `refresh_dlc_cache`) and keeps the
rest, because a debugging flag must survive a restart (§8) - filtering *launcher-generated setting
flags* is the caller's job, since that list lives in the launcher's own generated table.

### 6.3 Where the live apply runs today, and where it must move

The channel that exists is P1's `MemorySink`; P3's `Service`/`Store` are not wired into a boot yet
(P11). So today the live half runs in `settings_bridge_hooks.cpp` against the in-memory store. When
the coordinator wires the real service, the same three calls move onto `Service::last_commit_changes()`
(P9 added it) and the seam is unchanged - which is exactly why the seam is its own module.

### 6.4 Honest limits of the coordinator

* The **handshake is "the spawn succeeded"**. The child's first instruction is to wait for this
  process to exit, so a successful spawn means it is running and has touched nothing. A pipe-based
  child signal would be stronger and is not needed while the child does nothing else first.
* `WaitForExit` in the bootstrap is **bounded** (30 s by default) and a timeout returns
  `kStartGame == false`: two titles writing the same saves is the one outcome §8 forbids, so the
  child exits instead.
* The token carries the parent's **creation time** as well as its pid, and the waiter compares them
  before waiting: a reused pid is refused rather than waited on.

## 7. Reproduce

```powershell
cmake --preset win-amd64-release
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure          # 33/33

pwsh -NoProfile -File out\_swap\p9\run.ps1 -Label control                # the silent arm
pwsh -NoProfile -File out\_swap\p9\run.ps1 -Label hot  -Diagnostic       # the applied arm
pwsh -NoProfile -File out\_swap\p9\run.ps1 -Label full -Diagnostic -FullBoot   # + the title screen
```
