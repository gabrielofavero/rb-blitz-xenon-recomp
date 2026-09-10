---
status: in-progress
milestone: 3
last_updated: 2026-09-09
---

# Milestone 3 — Reach guest entry and stable offline boot

## Entry state

- Milestone 2 done: the native executable links and launches enough to begin
  executing guest code.
- **Knowledge folded in (Milestone 2):**
  - Build: `cmake --build out/build/win-amd64-debug` after
    `cmake --preset win-amd64-debug`. Executable:
    `out/build/win-amd64-debug/rb_blitz.exe` (Debug, ~77.9 MB).
  - Imports: 262 function imports resolved (113 `xam.xex` + 162
    `xboxkrnl.exe` − 13 patched variable imports), 0 unresolved. Full list in
    `out/imports.txt`. `__C_specific_handler` is imported → SEH present, but
    codegen reported no funclets and no manual EH hints were needed.
  - EH flags: `-fasync-exceptions` (clang) is already carried by
    `generated/rexglue.cmake`; keep it in sync when enabling EH.
  - Threading: untested until first boot — watch for worker-thread/timer/shutdown
    deadlocks (step 6).
  - Build-config fix carried in project `CMakeLists.txt`: host target gets the
    vendored imgui include dir (B-004). Do not hand-edit `generated/`.

- **Knowledge folded in (Milestone 3, in progress — see
  `docs/bringup-log.md` for full detail):**
  - **GPU plugin must be enabled or the guest stalls before graphics init.**
    `rb_blitz` now calls `rexglue_setup_target(rb_blitz GPU_PLUGINS xenos)`
    (`CMakeLists.txt`) and `RbBlitzApp::OnPreSetup` sets
    `config.gpu_plugin = "xenos"` (B-005). This stages `rexgpu-xenosd.dll`
    (Debug) / `rexgpu-xenos.dll` (Release) next to the executable.
  - **Path policy** lives in `RbBlitzApp::OnConfigurePaths`
    (`src/rb_blitz_app.h`): writable roots (user/update/cache) are forced
    outside `game_data_root`; `game/` stays read-only (SDK default
    `allow_game_relative_writes=false`). `user_data_root` defaults to
    `<Documents>/rb_blitz`.
  - **Run command:** `rb_blitz.exe --game_data_root=<abs path to game/>`.
    Running from the build dir also needs `rexruntime.dll` +
    `rexgpu-xenos[d].dll` beside the exe (staged by the build). Logs land in
    `out/build/<preset>/logs/rb_blitz_NNN.log`.
  - **Standing blocker (B-006):** guest boot hits
    `[FATAL] Call to invalid or unregistered function at guest address 0xXXXXXXXX`.
    These are real functions that end in `bctr` (indirect tail-call), have no
    PDATA entry and no static `bl` caller, so codegen `GapFill` never registers
    them. Fix = add `[functions."0x…"]` (no size) to `config/functions.toml`,
    one at a time. Registered so far: `0x82789360`, `0x8278A708`, `0x8279A888`,
    `0x82779A70`, `0x82783D18`.
  - **Fast iteration tooling (important):** the Debug codegen CLI re-runs full
    analysis (~435 s) whenever `config/functions.toml` changes. A Release
    codegen CLI was built from the pinned SDK at
    `rexglue-sdk/out/win-amd64/Release/rexglue.exe` (~55 s/run). The project
    Release config is `cmake --preset win-amd64-release` →
    `out/build/win-amd64-release` (first build done; iterate there).

## Reference points

- `docs/baselines/xenia-canary-80679bc.md` — Xenia reached an animated
  title-logo loop but not the offline prompt. Notable unresolved externs seen
  there (relevance unproven, do not stub blindly):
  `XamVoiceSetMicArrayIdleUsers` (ordinal `0x48C`), `XeKeysSetKey` at
  `0x827F6BA4`, `XeKeysAesCbc` at `0x827F6BB4`.
- Goal is to **exceed** that baseline: reach the title screen / offline prompt.

## Steps

### 1. Separate read-only and writable roots — **done**

- Implemented in `RbBlitzApp::OnConfigurePaths` (`src/rb_blitz_app.h`).
- Game data is read-only; writable state lives outside `game/`.

### 2. Verify VFS — **partly verified**

- `game:`/`d:` mount to the game-data root; `update:\gen\patch_xbox.hdr`
  correctly fails with `0xc000000f` (no title update in this base dump).
- Still to do: confirm case/path normalization against **every** attempted open;
  keep the `fs` log category as the structured record.

### 3. Implement blocking kernel imports / register missed functions — **in progress**

- Work from the import inventory produced in Milestone 2.
- Prefer existing ReXGlue runtime implementations; add `REX_HOOK` whole-function
  overrides in `src/hooks/` only when necessary, recording the guest address.
- **Current work is B-006**: register `bctr`-terminated indirect-call targets in
  `config/functions.toml` as the runtime reveals them (5 done, keep going).
- Optional root-cause fix (deferred, plan fix-order #6): make GapFill split code
  regions on `bctr` when the next word is a known function entry
  (`rexglue-sdk/src/codegen/phase_gapfill.cpp`).

### 4. Offline services fail fast and faithfully — **not started**

- Make unavailable network services fail quickly enough for the title's own
  offline path.
- Do **not** fake a successful service unless the game cannot proceed without
  it and the exact contract is understood (see plan).
- Will become observable once boot proceeds past graphics/audio init.

### 5. Crash/hang diagnostics — **partial**

- The SDK `REX_FATAL` already reports the last guest PC
  (`Call to invalid or unregistered function at guest address 0x…`).
- Still to add: SDK version, game fingerprint, caller (LR), and a hang watchdog.
  `InvalidFunctionTrap` (`rexglue-sdk/src/system/function_dispatcher.cpp`)
  currently logs only `ctx.last_indirect_target`; logging `ctx.lr` would reveal
  the caller and the function-pointer table (enables batch fixing).

### 6. Shutdown and threads — **not started**

- Verify worker threads, events, timers, and shutdown do not deadlock.

## Acceptance / exit criteria

- [ ] Ten consecutive launches reach the title screen / offline prompt.
- [ ] Each launch closes cleanly (no forced kill).

## Resume here (tomorrow)

State is mid-iteration and **not clean**: `config/functions.toml` already has
`[functions."0x82783D18"]`, but the last Release build was cancelled, so the
generated code does not include it yet.

```powershell
# 1. Rebuild Release (regenerates code ~55 s, then partial compile + link)
cmake --build out/build/win-amd64-release

# 2. Run and read the newest FATAL
Remove-Item out\build\win-amd64-release\logs\*.log
Start-Process -FilePath .\out\build\win-amd64-release\rb_blitz.exe `
  -ArgumentList "--game_data_root=$PWD\game" `
  -WorkingDirectory "$PWD\out\build\win-amd64-release"
Get-ChildItem out\build\win-amd64-release\logs\*.log |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1 |
  ForEach-Object { Select-String -Path $_.FullName -Pattern "FATAL|unregistered" }

# 3. Add the new address as [functions."0x…"] in config/functions.toml,
#    rebuild, re-run. Kill the hung process each time:
Stop-Process -Name rb_blitz -Force
```

## Handoff to Milestone 4

Write into `04-menus-content-input-saves.md`:

- the exact run command and required arguments;
- which kernel imports were implemented and how;
- any offline-service behavior the game depends on;
- known-good log categories to watch during menu/content work.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.

