---
status: in-progress
milestone: 4
last_updated: 2026-09-19
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
    `config/functions.toml`, re-run codegen, rebuild, re-run. **24 addresses are
    registered now:** 3 tail-branch targets, the 5 boot-time ones
    (`0x82789360`, `0x8278A708`, `0x8279A888`, `0x82779A70`, `0x82783D18`),
    `0x8278A6E0`, and the 2026-09-19 Milestone-5 batch (`0x827EC038` plus the 14
    adjuster-thunk holes listed in [docs/bringup-log.md](../docs/bringup-log.md)
    B-011).
  - **Fast iteration tooling:** the pinned Release codegen CLI
    (`rexglue-sdk/out/win-amd64/Release/rexglue.exe codegen
    rb_blitz_manifest.toml`, ~55 s) is much faster than the Debug CLI
    (~435 s). Only the 1–2 partitions containing the new function recompile.
  - **Input is already live:** SDL reports
    `OnControllerDeviceAdded: "Xbox One Controller"` (VendorID 0x045E,
    ProductID 0x02FF) and the log's later timestamps show the title reacting to
    button presses, so step 2 is verification, not bring-up. **Correction
    (2026-09-19):** the pad present in the last session is a different one —
    `"XInput Controller #1"`, VendorID `0x0B05`, ProductID `0x1B4C`.

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

## Progress (2026-09-19)

- **The offline route is reachable.** B dismisses the "Cannot connect to Rock
  Central" dialog and the title reaches its offline path; the author then navigated
  the menus, saw the bundled song list and started a song (which needs the
  Milestone 5 fixes as well). **The exact key sequence is not recorded** — capture
  it next session, because it is the first step of every later acceptance run.
- **Step 1 (content): menus and bundled songs work; the offset audit does not
  exist.** `gen/main_xbox.hdr` / `gen/main_xbox_0.ark` are opened without complaint,
  but nobody has compared the *read* offsets. The fingerprints themselves are
  enforced as of 2026-09-19: codegen is gated on `game\default.xex` matching
  `config/game_fingerprints.toml`, every boot logs the image it got, and
  `rb_blitz_fingerprint --all` audits the `.hdr`/`.ark` sizes and digests — see
  [docs/build-and-run.md](../docs/build-and-run.md) §3–§4.
- **Step 2 (input): pad enumerated, verification still keyboard-only.** The last
  session's log shows `SDL OnControllerDeviceAdded: "XInput Controller #1"`
  (VendorID `0x0B05`, ProductID `0x1B4C`); every navigation verified so far was
  keyboard-injected.
- **Step 3 (persistence): partial.** The SDK content path writes everything (see
  the close-out in [docs/bringup-log.md](../docs/bringup-log.md)). Weak cross-run
  signals exist — content files written by one run were enumerated by the next, and
  the shader cache is rewritten at process exit — but no deliberate restart,
  missing-root or corrupt-root run has been done.
- **Step 4 (online unavailable): satisfied.** The Rock Central sign-in is refused
  and dismissible, the DLC aggregate enumerator returns 0 items, and no service is
  faked. This is also the **final** behaviour for the vanilla route — do not spend
  Milestone 4 effort on unblocking online features. That work belongs to the
  community's Rock Band Blitz Ultimate mod; our deliverable — that an installed
  payload runs on this same executable — is implemented
  ([docs/ultimate-compat.md](../docs/ultimate-compat.md)).
- **B-010 is resolved and was never an M4 blocker:** 32-bit fixed-point textures
  are converted on the CPU (SDK patch 0002), which is what made the note highway
  and the 3D background appear. Two limits stand — see
  [docs/known-issues.md](../docs/known-issues.md).

## Reference leads (RB3 mining pass, 2026-09-19)

Full detail in [`docs/rb3-references.md`](../docs/rb3-references.md). Rock Band 3
is the same engine, and two projects have already solved a lot of this:

- **Any Blitz function is hookable today, with no symbol-map work.**
  `generated/default/rb_blitz_pch.h` declares every function as a *weak* alias
  (`Name`) over a *strong* original (`__imp__Name`), so
  `REX_HOOK(sub_82438F40, Native)` + `__imp__sub_82438F40(ctx, base)` works for
  any of the 38,344 functions. `band3_recomp`'s `__imp__NewFile`-style hooks are a
  ReXGlue **0.8** artefact; we are on 0.10.
- **`[[midasm_hook]]` is the right tool for forcing a branch or a return value**
  (our 0.10 schema is richer than 0.8's) — no hand-transcribed function body
  needed. Prime candidate: the **"Proceed in Offline Mode?"** branch (next steps
  #1) and the debugger trap below. See `docs/rb3-references.md` §3.
- **Give our proved functions real names.** Add `name =` to the
  `[functions."0x…"]` entries in `config/functions.toml` (the eleven MOGG rows in
  `docs/symbols.md` are the first batch). Readability only — not a prerequisite.
- **Expect the debugger trap.** RB3 is patched for it twice over
  (`band3_recomp/src/patches.cpp` `App__Run`, RB3DX group 2: `bl App::Run` →
  `bcl RunWithoutDebugging` at `0x82272E90`). Find Blitz's equivalent rather than
  waiting to trip it.
- **Our manifest sets no `longjmp_address`/`setjmp_address`.** The 0.10 SDK
  supports both at `[entrypoint]` level and `band3` sets them (`0x82BBB620` /
  `0x82BBBA50`, RB3 addresses — do **not** copy). Find Blitz's; an unbridged
  longjmp may already explain odd error-path deaths.
- **`NewFile` + a host overlay is the clean way to shadow shipped data.** band3's
  `NewFile` hook sanitises `..`, tries an `assets/<path>` fallback and sets
  `ctx.r4.u64 = flags | 0x10000` to force host-file reads. Our asset root is
  `game/`. Pair it with `StreamChecksum__ValidateChecksum` → `1` before editing
  any shipping file, or validation rejects the edit.
- **`OptionBool`/`OptionStr` inject host `argv` into guest DTA options.** That is
  a deterministic alternative to `scripts/drive_ui.ps1`'s keystroke injection and
  its required focus transition.
- **Check the `update:` snag against `SetDiskError`.** RB3 no-ops
  `PlatformMgr::SetDiskError` (band3 `patches.cpp`; RB3DX group 4, with 8 call
  sites listed in `docs/rb3-references.md` §6) and RB3DX also rewrites the content
  prefix `"UPDATE:"` → `"D:"` (group 8). **Confirmed against Blitz 2026-09-20:** the
  Ultimate mod makes exactly that `"UPDATE:"` → `"D:"` edit (B-012), which is why an
  installed payload finds its `patch_xbox.*` pair where retail looks in vain — and its
  third edit neuters Blitz's own `PlatformMgr::SetDiskError` (`sub_8236C108`, the
  checksum validator's `error = 3` path, which sleeps forever once it latches). Any
  ark we add will hit that path, so the fix travels with the install rather than with
  our hooks: `docs/ultimate-compat.md` §3.
- **The Ultimate payload is what exercises the `update:` path.** Rock Band Blitz
  Ultimate is the Blitz sibling of the RB3DX patch set, and its Xbox 360 install rolls
  TU5 into the base installation — the payload's `gen\patch_xbox.hdr` and
  `patch_xbox_0.ark` *are* that title update. It is also the mod's own reason that
  online unblocking is not our job:
  [docs/ultimate-compat.md](../docs/ultimate-compat.md).
- **Two config placements to remember:** `d3d12_readback_resolve` is a **cvar**
  in 0.10, so it belongs in the build-tree-local `rb_blitz.toml`
  (`out/build/<preset>/rb_blitz.toml`) runtime profile, not the build manifest.
  `band3` also has `longjmp_address`, above.
- **Input war story from the RB3 native port:** a missing `button_meanings` block
  in the shipped `config/joypad.dta` made **every menu key resolve to
  `kAction_None`** — silent, total input failure. If our input layer looks dead
  rather than wrong, suspect the DTA mapping, not the XInput path.

## Steps

### 1. Content access

- Confirm `gen/main_xbox.hdr` and `gen/main_xbox_0.ark` are read from the
  game-data root with correct offsets and sizes (fingerprints in
  `config/game_fingerprints.toml`).
- Reach the main menu and enumerate bundled songs with **no online dependency**.
- **State (2026-09-19):** menus and bundled-song enumeration work, and the
  fingerprint check now exists at build time, at boot and as an audit tool; the
  in-game offset/size audit is still to do.

### 2. Input

- Map one standard XInput controller.
- Verify navigation, accept/back, pause, and lane controls.
- Keep keyboard fallback parity with the Xenia baseline (`keyboard_mode = 1`).
- **State (2026-09-19):** first half done, second half open — every verified
  navigation was keyboard-injected, and nothing has been checked on a pad.

### 3. Local persistence

- Provide a deterministic local profile/storage response sufficient for offline
  use.
- Verify settings/save creation, restart persistence, and behavior with missing
  or corrupt writable data.
- **State (2026-09-19):** partial — the SDK content path creates the files;
  restart persistence and missing/corrupt writable data are untested.

### 4. Keep online features gracefully unavailable

- Achievements, leaderboards, and downloadable-song enumeration stay disabled or
  gracefully unavailable unless they block the core loop.
- **This is the final behaviour, not a stage.** The title ships an offline mode, so
  restoring or unblocking online services is permanently out of scope and belongs
  to the community's Rock Band Blitz Ultimate mod. Our side of that bargain is
  compatibility: an installed payload works with a vanilla `--game_data_root` on the
  same executable, with vanilla unaffected and no recomp-specific build —
  implementation, evidence and acceptance criteria in
  [docs/ultimate-compat.md](../docs/ultimate-compat.md).
- **State (2026-09-19):** done.

## Acceptance / exit criteria

- [ ] Launch → navigate menus → see bundled content → select a song → return to
      menu, repeatedly, without a crash or online dependency.
      **Partially met (2026-09-19):** the author did all of this while getting a
      song to play (see Progress above), but not repeatably and not with the
      return-to-menu leg on its own; this milestone should be exit-completed once
      the offline route is scripted and a pad has been used for at least one pass.

## Next steps (pick up here)

1. **Record the offline route** — which key dismisses the sign-in and which key
   picks "Proceed in Offline Mode?" — as a reusable action in `scripts/`, and write
   the sequence into [docs/bringup-log.md](../docs/bringup-log.md). The guest-side
   branch is still unidentified;
   `scripts/scan_process_strings.ps1 -Pattern "Offline Mode"` plus a grep of
   `generated/` is the lead.
2. **Verify with a pad instead of injected keys** (step 2): navigation,
   accept/back, pause, lane controls, and keyboard fallback parity on
   `XInput Controller #1`.
3. **The fingerprint file has consumers now; the offset audit is what is left.**
   The decision recorded here is made and implemented (2026-09-19): the gate fails
   closed before codegen, the boot logs both identity lines #10 asks for, and
   `rb_blitz_fingerprint --all` checks the `.hdr`/`.ark` sizes and digests. What
   no hashing can answer is the in-game offset audit for `main_xbox.hdr` /
   `_0.ark` — that is the remaining part of this item.
4. **Run the persistence cases deliberately** (step 3): launch twice and diff the
   writable root; then run with the root absent and with a deliberately corrupted
   `globaloptions`.
5. **Then Milestone 5.** Its exit criterion (three clean full-song runs) also has
   no driver, so write one script that does both: reach the offline menus, start a
   song, capture the log, and shut down cleanly.

Before writing any new hook, read **Reference leads** above: hooks, midasm hooks
and the offline-mode candidates are already mapped there.

Scope note: none of this is about restoring online services. The vanilla route
stays offline permanently, and a Rock Band Blitz Ultimate payload is the
community's upgrade path — we only owe it continued working
([docs/ultimate-compat.md](../docs/ultimate-compat.md)).

## Handoff to Milestone 5

Folded into [`05-complete-one-song.md`](./05-complete-one-song.md) on 2026-09-19
(content quirks, storage location, input mapping, the standing "audio frames are
produced" fact). What is still owed to it:

- the **named** bundled song used for the full-playthrough test;
- the recorded offline route, once captured (next step 1).

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
