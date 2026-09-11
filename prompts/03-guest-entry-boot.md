---
status: done
milestone: 3
last_updated: 2026-09-10
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

- **Result (Milestone 3, complete 2026-09-10):** the guest boots to the
  **title screen with "PRESS A TO START"**, exceeding the Xenia baseline
  (which only looped the animated logo). Acceptance: **10/10** consecutive
  launches reached the title with no `[FATAL]`, and **10/10** closed cleanly.
  Log for a full boot is ~2 KB after B-007. See `docs/bringup-log.md`.
- **Knowledge folded in (Milestone 3 — see
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

### 2. Verify VFS — **verified**

- `game:`/`d:` mount to the game-data root; the title artwork loads, so ARK/HDR
  reads succeed.
- `update:\gen\patch_xbox.hdr` correctly fails with `0xc000000f` (no title
  update in this base dump) — consistent with the Xenia baseline.
- The `fs` log category is the structured record for attempted opens.

### 3. Implement blocking kernel imports / register missed functions — **done**

- Work from the import inventory produced in Milestone 2.
- Prefer existing ReXGlue runtime implementations; add `REX_HOOK` whole-function
  overrides in `src/hooks/` only when necessary, recording the guest address.
- **Current work is B-006**: register `bctr`-terminated indirect-call targets in
  `config/functions.toml` as the runtime reveals them (5 done, keep going).
- Optional root-cause fix (deferred, plan fix-order #6): make GapFill split code
  regions on `bctr` when the next word is a known function entry
  (`rexglue-sdk/src/codegen/phase_gapfill.cpp`).

### 4. Offline services fail fast and faithfully — **verified (non-blocking)**

- The title's offline path is reached with `XamVoiceSetMicArrayIdleUsers`,
  `XeKeysSetKey`, and `XeKeysAesCbc` left as honest `STUB`s (no faked success).
- Nothing network-related blocks boot; boot to title is ~20 s.

### 5. Crash/hang diagnostics — **partial**

- The SDK `REX_FATAL` already reports the last guest PC
  (`Call to invalid or unregistered function at guest address 0x…`).
- Still to add: SDK version, game fingerprint, caller (LR), and a hang watchdog.
  `InvalidFunctionTrap` (`rexglue-sdk/src/system/function_dispatcher.cpp`)
  currently logs only `ctx.last_indirect_target`; logging `ctx.lr` would reveal
  the caller and the function-pointer table (enables batch fixing).

### 6. Shutdown and threads — **verified**

- 10/10 launches closed cleanly via normal window close (no forced kill); no
  worker-thread/timer/shutdown deadlock observed.

## Acceptance / exit criteria

- [x] Ten consecutive launches reach the title screen / offline prompt.
- [x] Each launch closes cleanly (no forced kill).

## Reproduce / verify

Build and run the Release configuration (fast codegen; Debug also works):

```powershell
# Build + run the acceptance test (10 launches must reach the title and close)
cmake --preset win-amd64-release           # once
cmake --build out/build/win-amd64-release
.\scripts\acceptance_launches.ps1 -Runs 10 -BootWaitSec 26

# Single run, manual
Remove-Item out\build\win-amd64-release\logs\*.log
Start-Process -FilePath .\out\build\win-amd64-release\rb_blitz.exe `
  -ArgumentList "--game_data_root=$PWD\game" `
  -WorkingDirectory "$PWD\out\build\win-amd64-release"
# Close gracefully with the window close button (do not force-kill).
```

## Handoff to Milestone 4

Copy these facts into `04-menus-content-input-saves.md` (and `docs/symbols.md`
where durable):

- **Run command:**
  `out/build/win-amd64-release/rb_blitz.exe --game_data_root=<abs game/>` run
  with the build dir as cwd (needs `rexruntime.dll` + `rexgpu-xenos.dll` there;
  Debug uses `rexgpu-xenosd.dll`). Build: `cmake --build out/build/win-amd64-release`
  (or `-debug`). Logs: `out/build/<preset>/logs/rb_blitz_NNN.log`.
- **Kernel imports implemented:** none needed hooks — all 262 imports already
  resolved by the SDK runtime. The only boot blocker was codegen function
  discovery (B-006), fixed by five `config/functions.toml` `[functions]`
  entries (no `src/hooks/` overrides were required yet).
- **Offline-service behavior the game depends on:** none blocking. The title's
  offline path is reached with `XamVoiceSetMicArrayIdleUsers`, `XeKeysSetKey`,
  and `XeKeysAesCbc` as honest `STUB`s. Do not fake success.
- **Known-good log categories:** `core` (lifecycle, FATAL, clean shutdown),
  `gpu` (D3D12/pipelines/shaders), `apu` (audio endpoint), `fs` (VFS opens,
  `update:` misses), `krnl` (stubs). `sys` no longer floods after B-007.
- **Expected snags:** `update:\gen\patch_xbox.hdr` → `0xc000000f` (no title
  update, benign); three key/voice `STUB` warnings (benign).
- **Watch for (Milestone 4):** reaching the menu past "PRESS A TO START"
  requires input; ensure a controller (or `--mnk_mode` keyboard emulation) is
  mapped and the bundled content (`gen/main_xbox.hdr`/`_0.ark`) is enumerated
  offline. Any new `[FATAL] … unregistered function` means another
  `bctr`-missed target → add `[functions."0x…"]`.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.

