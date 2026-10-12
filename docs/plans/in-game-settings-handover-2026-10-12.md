# In-game settings handover — 2026-10-12

**The feature is unfinished. Do not report release acceptance or a clean test suite.**
The user explicitly requested this handover because the session was running low on credits.
Stop implementation for this handover; the next agent should resume from the working tree.

## User intent and latest decisions

The original request was to finish the in-game settings feature described by the planning MDs,
and make startup-critical settings apply before game processing. The user noticed the retail
“Loading settings” message after pressing A and correctly questioned whether resolution was being
applied too late. A subsequent Luna turn made some changes and rewrote the remaining-work ledger,
but did not finish the feature. This audit found substantial missing wiring and bugs.

The user then specified the resolution behavior, superseding the old benchmark requirement:

> Default resolution is Auto. At startup, determine the best resolution for the current display
> and apply it immediately. Respect a compatible manual choice on later boots. If that choice
> becomes incompatible (for example, 4K saved, now a 1080p display), switch back to Auto.

No GPU benchmark is wanted. This is a display compatibility policy, not a performance claim.
Preserving an incompatible manual resolution forever is also superseded by this instruction.
Other manual graphics preferences should still survive temporary recovery/fallback.

The user confirmed another session was doing ultrawide work in this same workspace, but that
session is now stopped. **Preserve its work.** The user has also committed/staged edits while this
audit was running; some changes described here are already in HEAD, others staged, others unstaged.
Do not reset, unstage, or replace the working tree from an older commit.

## Workspace and operational details

- Repository: `D:\Gabriel\Documentos\Coding\decomps\360\rb-blitz-xenon-recomp`.
- PowerShell. `exec_command` with `login: false` avoids a slow shell startup.
- HEAD at handover: `a3fd698` (ultrawide resolution/diagnostics), preceded by `ebd7c27` and
  `6df0359` (graphics boot management/recovery). Recheck before editing; HEAD moved externally.
- No applicable repository/ancestor AGENTS.md was found. No relevant skill was needed.
- Do not spawn subagents unless explicitly authorized by the user or applicable instructions.
- The user has authorized checking and completing this feature. Tests are authorized; an old
  “only test when requested” sentence in the remaining-work MD is stale for this task.
- Never use the real save/profile for testing. Scratch lives in `out/_swap/settings-audit/`.
- Game data: `game/`; separate Ultimate payload: `game/ultimate/`.
- Both test game processes this session launched (22600 and 20760) were stopped. No game was
  intentionally left running. Check processes before a new run; only stop processes you own.
- Do not edit generated recompiled sources. Production hooks belong in handwritten sources/config.

## What changed in this audit

### Early boot, resolution, and recovery

Relevant files: `src/settings/{boot,graphics_policy,graphics_boot,capabilities,model,schema,store}.*`,
`src/rb_blitz_app.h`, `src/hooks/{render_settings,shell_music}.*`, `src/diag/probe_points.cpp`.

- Saved native preferences are loaded in `OnConfigurePaths`, before game processing. GPU plugin
  flags are applied in `OnPreSetup`, after plugin registration and before graphics setup.
- `FinishInit()` was moved before menu authoring so externally overridden rows can be disabled
  correctly. It now scans rows even when no native preference was saved and decodes compound
  target, resolution, filtering and AA cvars as groups. **This new scan needs runtime checking.**
- Production 1080p uses the title's actual 960x540 mode at backend draw scale 2. Merely replacing
  the guest height constant was insufficient; the wrapper for `sub_823F3698` also sets bit 0 of
  the title screen object's word at offset 796 when `BootRenderHeight() == 540`.
- The other session added bounded forced-width/ultrawide planning and diagnostics. Auto uses that
  capability list; do not regress it to a fixed 16:9 list. See `ultrawide-findings.md`.
- Auto is represented internally by a resolution `Value` with width and height both zero,
  detected with `IsAutoResolution()`. On disk it is `resolution = 'auto'`, not `[0, 0]`.
  Existing positive `[width, height]` preferences are still accepted.
- The schema defaults to Auto and includes an Auto choice. The bridge shows the Auto preference
  rather than the resolved pixels. The runtime menu choices also explicitly include Auto.
- `GraphicsReconciler` resolves Auto to the largest supported size fitting the current display;
  no display query falls back to 1280x720. `ResolutionPreferenceForBoot()` converts an incompatible
  manual choice to Auto, and boot persists that change with automatic provenance.
- First-run policy version is now 3. Renderer/AA/filtering use conservative capability-based
  defaults, without a benchmark. Some comments/tests still mention the obsolete measured policy;
  clean those up. An obsolete sustainable-resolution field may remain; inspect before removing.
- New `GraphicsBootState` writes a sibling `.graphics-boot.toml` marker and retains last-good
  effective graphics. A lifetime file lock distinguishes a live concurrent process from a crashed
  boot. Failed attempts fall back without erasing requested preferences, and the same failed
  preference does not cause an endless retry loop.
- A shell/menu poll asks the presenter's `CaptureGuestOutput()` for a nonblack guest frame before
  marking graphics successful. Closing the window marks startup closed cleanly. **This capture
  path is not yet verified at runtime; check for blocking/threading problems.**
- Added `--settings_safe_graphics=true`: select D3D12, 720p, no AA and native filtering before
  graphics setup, including bypassing explicit bad graphics flags. The last safe-mode edits have
  not received a successful runtime check. Check requested/effective reporting and editability.

### Persistence and service

Relevant files: `src/fs/atomic_file.h`, `src/settings/{store,paths,service,bridge_sink,ultimate_adapter}.*`.

- Added cross-process file locks and unique temporary siblings. Windows writes use `_wfopen`,
  checked `fflush`/`_commit`, and `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
- Native settings/path saves acquire locks before reread/merge. Existing unreadable files are
  refused instead of silently treated as absent. Text reads are bounded to 4 MB.
- Provider writes now use the same flushed atomic replacement and refuse unreadable existing
  provider files. Unknown provider fields remain preserved.
- Production provider edits now flow through `Service` via `InstalledProviderSink`, rather than
  the bridge's old local-only callback. Dynamic installed choices are supplied to the service.
  The local-provider fallback remains for old diagnostic/test harnesses.
- Combined native/provider saves now contain actual values and native baselines in a TOML
  `.commit-journal`. On provider attachment, recovery distinguishes a native replacement that
  happened from one that never happened, replays the provider portion, and keeps ambiguous or
  incompatible journals instead of pretending success.
- Partial commits do not advance the durable revision or offer restart. Provider values update
  after successful save. No-op rows no longer count as written. Failed native saves reload the
  requested state while retaining staged edits for retry. The bridge retains failed edits too.
- Recovery retries and attachment now take a journal lock. **Review this implementation carefully**:
  not every interruption/concurrent-writer scenario has been tested; provider same-key optimistic
  conflict checking against its originally read baseline is still absent.
- `Service::RequestRestart()` blocks an outstanding journal, but verify the production bridge
  actually consults this guard; its current restart handler primarily talks to the restart
  coordinator. `commit_ok_` blocks the ordinary failed-save path, but audit the recovered path.
- Added a service test for partial combined save, process recreation, replay, revision and no-op,
  plus Auto save/reload. **These most recent tests have not been built/run.**
- The latest store edit rejects nonpositive/overflowing numeric resolution arrays. It also needs
  a rebuild/test run.

### UI, provider integration and launcher

Relevant files: `src/ui/settings_screen.cpp`, `src/hooks/{settings_payload,settings_bridge_hooks,ultimate}.cpp`,
`src/settings/menu_payload.*`, launcher sources/tests.

- Fixed duplicate achievements confirmation panel/screen names that could replace the category
  screen and hide Unlock All. Confirmation now has separate `settings_achievements_confirm_*`
  names. Added object-name uniqueness assertions.
- Online achievements are a confirmed **guest** call to `acc_mgr earn_accomplishment` for the
  provider's four IDs, with a session latch. The test should not expect a host `invoke` request.
  Removed the stale runtime warning saying the action was unwired. Do not grant achievements
  against the user's real profile while testing.
- Fixed Ultimate target choice being marked provider-only, which hid it in base mode. Runtime
  target choices still depend on correct installed-provider detection; verify this in base mode.
- Horizontal selectors now use A to enter editing, Left/Right to stage, B to cancel, A to commit.
  Up/Down is suppressed while editing. The list preserves its index during value changes with
  global DTA variables `rbbz_settings_index` / `rbbz_settings_editing`, reset on panel entry.
  **This is new and unverified in a running game.** There is not yet a clear visual editing
  indicator/control hint; add one if needed after proving the event flow.
- Boolean rows still commit immediately. A restart-scoped commit opens the restart prompt;
  errors open the save-error prompt. Check returning from prompts preserves useful focus.
- Restart prompt currently defaults to **Close**, with Restart now second. This avoids accidental
  held-A restart, but does **not** implement the planned A=restart/B=close with a release guard.
  That requirement remains unresolved. Do not simply move Restart first without a held-input guard.
- Compatible Ultimate `mod_settings` is rerouted to the merged root; unknown providers keep
  their original route. `BuildMenuRecords` has an optional `merge_provider` parameter.
- The Hide Main Menu mirror now reads installed locale strings (English if present), with explicit
  labels for Advanced Settings and Refresh Song Library; custom/provider rows are retained.
  Verify localization, protected settings access, and duplicate entry behavior in the actual UI.
- Merged Ultimate installs now inspect the game root and mount a settings redirect there too,
  instead of only supporting a separate payload. **Untested.** Also review inactive/base-mode
  provider detection: `provider.compatible = provider.Active()` currently conflates compatible
  installation with active target, and `provider_root` selection may hide an installed separate
  provider while base mode is active.
- Added propagation of a failed live apply to the bridge's restart-required result through
  `RequireRestartAfterApply()`. This final small change has not been compiled/tested.
- Controller Settings is now the launcher's default layout. Its launch command no longer injects
  stale target/license overrides when using the controller-only path. Legacy full UI and retired
  schema/branches still exist in source; product cutover/cleanup and docs are not finished.
- The in-song screen construction-order fix from previous work remains: defer in-song screen
  construction until pause panels exist. Previous screenshots proved General/back/resume, but
  all current categories/confirmations still need checking in both base and Ultimate.

## Verification state — exact limitations

### Build and host tests

Build directory: `out/build/win-amd64-release`.

```powershell
cmake --build out/build/win-amd64-release --config Release -j 4
$env:TEMP = "$PWD/out/_swap/settings-audit/temp"
$env:TMP = $env:TEMP
ctest --test-dir out/build/win-amd64-release -C Release --output-on-failure
```

The game and launcher built successfully during this session. **The source is newer than that
successful build**, including the last service test, store validation, and live-apply result changes.
One intervening link failed because the test game still had the executable open; the process was
then stopped and the next build linked successfully. Do not confuse that with a source compile error.

The toolchain warns that installed Clang 19.1.7/CMake 4.4.0 differ from frozen 23.1.1/4.4.3. The build
allows this. Existing deprecation warnings also remain. No strict-toolchain pass is claimed.

Latest full CTest result: **33/35 passed**, failures:

1. `settings_screen`: `RequestPaths()` omits the newly emitted `rbbz-settings/cancel/-/-` path.
   Its “every literal is inventoried” assertion fails repeatedly around test line 592. Add the
   cancel path to `RequestPaths()` and rerun. Earlier stale `set_selected 0` assertions were
   updated to the index variable, and duplicate-name/action assertions now reflect the actual UI.
2. `launcher_general`: the empty-root fixture is nested deeply to avoid discovering the real
   checkout's `game/` when TEMP is in the workspace. Its expected path still contains mixed
   separators (`bare/a/b/...` vs Windows backslashes), failing around line 237. Normalize the
   fixture/expected path. The current source still has `scratch.Make("bare/a/b/c/d/e/f/g/h/i/j/k/l/m")`
   without `.lexically_normal()`/`.make_preferred()`; an attempted combined patch did not land.

The earlier `path_policy` failure disappeared with workspace TEMP/TMP: sandbox temporary-path
containment issue, not an established product bug. The earlier graphics tests were updated for
the other session's newly supported bounded render sizes and for the Auto choice. Migration now
expects legacy explicit 720p to be imported because new profiles default to Auto.

Logs: `out/_swap/settings-audit/build.log`, `tests.log`. They are overwritten by later runs.

### Runtime checks did NOT pass yet

Two disposable runs were attempted. Native settings read/application did happen early: the marker
recorded `resolution = 'auto'` resolved to **2560x1080**, before graphics setup on this display.
That proves the selection/application path ran; it does **not** prove correct visible rendering.

The latest screenshot, `out/_swap/settings-audit/runtime-ui/menu.png`, is a **black game window**.
The latest `runtime-ui/game.log` was still around Ultimate overlay initialization when inspected;
it did not provide a verified settings menu or successful presented marker. Investigate whether
startup is stalled, slow, or failing; do not assume it was a successful launch or attribute the
cause to the UI edits without isolating it. Compare base vs Ultimate, Auto vs explicit 720p,
and the last working build. Check provider attachment/journal locking and the new presentation
probe if the startup stack points there. Capture debugger stacks/logs if necessary.

The test processes were manually terminated when the user asked for handover. Their graphics
markers remain pending; use a fresh scratch profile for an unbiased next boot, or explicitly account
for the recovery that those manually terminated runs will trigger.

Windows automation details:

- A sandbox-launched game lived on a different desktop. Escalated automation could not see its
  SDL window, and sandbox automation could not foreground it. Launch and drive the disposable
  game on the interactive desktop with the appropriately reviewed elevated tool calls.
- `Start-Process -WindowStyle Hidden` was used per tool instructions; the game creates its SDL
  window normally. Use scratch profile/user/cache/log paths and `--mnk_mode=true --fullscreen=false`.
- Windows identified an IME/input-indicator overlay as `Process.MainWindowHandle`. The scripts
  `drive_ui.ps1` and `capture_window.ps1` were fixed to enumerate the process's `SDL_app` window.
  The latest interactive-desktop run did successfully foreground/capture and send A, but the
  captured game remained black.
- `drive_ui.ps1`: A is Space, B Backspace; use `lstick_down`/`lstick_up`/`lstick_left`/`lstick_right`
  for navigation. The script chooses the first rb_blitz process, so only one test game should run.
- A blank legacy launcher profile migrates its default target to **Ultimate**. For a real base
  test, seed `settings.toml` with `[general] target = 'common'` instead of assuming blank means base.

Previous-session artifacts under `out/_swap/settings-finish/` and `out/_swap/p4/` provide historical
comparison: pause General/back/resume, restart prompt, and measured 1080p captures. They are not
acceptance evidence for the current tree.

## Recommended continuation order

1. Read this document, check current git state, and inspect both staged and unstaged diffs.
   Preserve ultrawide work and any further user changes. Read the main plan/contracts for scope.
2. Fix the two known CTest issues above, rebuild the **current** sources, run all 35 tests, and
   run the new interrupted-commit/Auto persistence tests. Resolve genuine regressions, not just
   assertions. Add meaningful coverage for any remaining recovery/retry/Unicode/concurrency gaps.
3. Diagnose the black startup with fresh disposable profiles. Establish a working base 720p
   control, then base Auto, Ultimate 720p and Ultimate Auto. Verify actual render targets and
   visible output, not just cvar logs or a marker written before presentation.
4. Verify selector editing/cancel/save, boolean save and live VSync, restart close/accept/failure,
   target changes, direct boot readback, provider saves, Unlock All timing, and in-song navigation
   through every child screen followed by Back/resume. Keep achievement tests in disposable saves.
5. Finish restart input guarding/UX; review override decoding and Auto restart-needed semantics
   (requested Auto is 0x0 while boot-effective pixels are nonzero, so naive comparisons can
   falsely mark Auto as requiring restart). Verify successful boot baselines are captured after
   automatic recommendations, not before them.
6. Audit provider transaction completion/recovery, same-key conflict handling, merged installs,
   inactive-provider detection, and the production restart guard. Provider-only retries and
   native+provider partial saves must never become fake success.
7. Verify safe graphics/last-good recovery with induced failure; ensure no endless retries, no
   preference destruction, and no false recovery after a later gameplay crash or clean close.
8. Finish product cleanup: Controller Settings cutover/dead UI/schema, labels/localization,
   compatibility fallback, hidden-DLC refresh access, and installer/distribution checks described
   by the plan. Distinguish actual missing implementation from hardware/release QA not available
   on this machine. Do not silently remove requirements from the ledger.
9. Rewrite the main remaining-work/status docs to match evidence, explain Auto and safe recovery,
   and remove obsolete benchmark, deferred-GPU-apply, unwired-achievements and old development-gate
   claims. Keep historical findings clearly dated. **The existing remaining-work MD is currently
   misleading and is not a reliable completion certificate.**

No PR, deployment, or release was requested or performed by this session.
