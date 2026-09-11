---
status: in-progress
milestone: 4
last_updated: 2026-09-10
---

# Milestone 4 — Menus, content discovery, input, and saves

## Entry state

- Milestone 3 done: ten consecutive clean launches reach the title screen
  ("PRESS A TO START"); 10/10 close cleanly (Release build).
- **Knowledge folded in (Milestone 3):**
  - **Build + run:** `cmake --preset win-amd64-release` (once) →
    `cmake --build out/build/win-amd64-release`; run
    `out/build/win-amd64-release/rb_blitz.exe --game_data_root=<abs game/>`
    **with the build dir as cwd** (needs `rexruntime.dll` +
    `rexgpu-xenos.dll` beside the exe; Debug uses `rexgpu-xenosd.dll`).
    Logs: `out/build/<preset>/logs/rb_blitz_NNN.log`.
  - **Kernel imports implemented:** no hooks were needed — all 262 imports are
    resolved by the SDK runtime. The only boot blocker was codegen function
    discovery (B-006), fixed with `config/functions.toml` `[functions]` entries.
  - **Offline services the title depends on:** none blocking. The offline path
    is reached with `XamVoiceSetMicArrayIdleUsers`, `XeKeysSetKey`, and
    `XeKeysAesCbc` as honest `STUB`s — do **not** fake success.
  - **Useful log categories:** `core` (lifecycle, FATAL, clean shutdown), `gpu`
    (D3D12/pipelines/shaders), `apu` (audio endpoint), `fs` (VFS opens and
    `update:` misses), `krnl` (stubs).
  - **Expected benign snags:** `update:\gen\patch_xbox.hdr` → `0xc000000f` (no
    title update in this dump); three key/voice `STUB` warnings.
  - **B-006 is not closed — it is a *class*.** Functions discovered only through
    an indirect call (no PDATA, no static `bl`, or a hole inside a run of 8-byte
    adjuster thunks) are missing from the register table, and each one shows up
    as a runtime `[FATAL] Call to invalid or unregistered function at guest
    address 0x…`. Fix = add `[functions."0x…"]` (no size) to
    `config/functions.toml`, re-run codegen, rebuild, re-run. Registered so far:
    `0x82789360`, `0x8278A708`, `0x8279A888`, `0x82779A70`, `0x82783D18`,
    `0x8278A6E0`.
  - **Fast iteration tooling:** the pinned Release codegen CLI
    (`rexglue-sdk/out/win-amd64/Release/rexglue.exe codegen
    rb_blitz_manifest.toml`, ~55 s) is much faster than the Debug CLI
    (~435 s). Only the 1–2 partitions containing the new function recompile.
  - **Input is already live:** SDL reports
    `OnControllerDeviceAdded: "Xbox One Controller"` (VendorID 0x045E,
    ProductID 0x02FF) and the log's later timestamps show the title reacting to
    button presses, so step 2 is verification, not bring-up.

## Progress (2026-09-10)

- **B-008 (resolved):** pressing A at the title hit
  `[FATAL] Call to invalid or unregistered function at guest address 0x8278A6E0`
  (thread `t21068`). `0x8278A6E0` is a **hole** between two already-registered
  8-byte adjuster thunks (`0x8278A6D8`, `0x8278A6E8`) — same B-006 class.
  Registered as `[functions."0x8278A6E0"]` in `config/functions.toml`; Release
  rebuild boots past the title cleanly. **Expect one of these per newly-reached
  UI path.**
- **Input works, but only after a real focus transition.** Injected keys are
  dropped unless the window sees focus-lost/focus-gained, so
  `scripts/drive_ui.ps1` has a `refocus` action (minimize → restore →
  `SetForegroundWindow`) and now fails loudly when focus is not acquired.
  Keybinds are the SDK MnK emulation (`mnk_mode`): `space`/`semicolon`→A,
  `backspace`→B, `enter`→Start, `z`/`tab`→Back, arrows (+Shift = D-pad),
  WASD = sticks.
- **A at the title is an online sign-in, not the offline start.**
  `out/drive-ui/m4-trace-dialog.png`: *"Cannot connect to Rock Central. To
  connect, sign in to an Xbox LIVE-enabled profile, connect to Xbox LIVE, and
  return to the title screen."* (`A SELECT / B BACK`). **B dismisses it** and
  returns to the title. The offline entry must be found elsewhere on the title
  screen.
- **The offline path exists in the guest strings:** scanning the live process
  found **"Proceed in Offline Mode?"** (`scripts/scan_process_strings.ps1`).
  Next step: capture its guest address, find the code that loads it in
  `generated/`, and identify how to reach it without Xbox LIVE.
- **Content enumeration already runs (step 1 evidence):**
  `XamContentCreateEnumerator: added 2 items` (the `songcache:` /
  `globaloptions:` content devices) and
  `XamContentAggregateCreateEnumerator: added 0 items` with
  `game:\Content\0000000000000000` not found — bundled content enumerated, DLC
  aggregate correctly empty offline.
- **Audio is already submitting frames** (`XAudioRegisterRenderDriverClient` +
  repeating `XAudioSubmitRenderDriverFrame`) — that is **Milestone 5** step 4
  material, not a Milestone 4 criterion.

## Steps

### 1. Content access

- Confirm `gen/main_xbox.hdr` and `gen/main_xbox_0.ark` are read from the
  game-data root with correct offsets and sizes (fingerprints in
  `config/game_fingerprints.toml`).
- Reach the main menu and enumerate bundled songs with **no online dependency**.

### 2. Input

- Map one standard XInput controller.
- Verify navigation, accept/back, pause, and lane controls.
- Keep keyboard fallback parity with the Xenia baseline (`keyboard_mode = 1`).

### 3. Local persistence

- Provide a deterministic local profile/storage response sufficient for offline
  use.
- Verify settings/save creation, restart persistence, and behavior with missing
  or corrupt writable data.

### 4. Keep online features gracefully unavailable

- Achievements, leaderboards, and downloadable-song enumeration stay disabled or
  gracefully unavailable unless they block the core loop.

## Acceptance / exit criteria

- [ ] Launch → navigate menus → see bundled content → select a song → return to
      menu, repeatedly, without a crash or online dependency.

## Next steps (pick up here)

1. `scripts/scan_process_strings.ps1 -Pattern "Offline Mode"` on a live title,
   then grep `generated/` for the address it reports to find the branch that
   skips the Rock Central sign-in.
2. Reach the main menu and enumerate the bundled song list with no online
   dependency (step 1), confirming `gen/main_xbox.hdr` / `_0.ark` open with the
   expected offsets/sizes.
3. Verify accept/back/pause/lane controls against `config/game_fingerprints.toml`
   hashes unchanged (step 2), then local persistence (step 3).

## Handoff to Milestone 5

Write into `05-complete-one-song.md`:

- the named bundled song(s) available for the full-playthrough test;
- input mapping details (which XInput controls map to which actions);
- save/storage serialization format and where writable state lives;
- any content-enumeration quirks that could affect song load;
- the standing fact that audio submits frames from the title onward
  (`XAudioRegisterRenderDriverClient` + `XAudioSubmitRenderDriverFrame`), so M5
  starts from "frames are produced" and only has to prove they are audible.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
