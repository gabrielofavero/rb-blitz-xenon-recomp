---
status: not-started
milestone: 3
last_updated: 2026-09-09
---

# Milestone 3 — Reach guest entry and stable offline boot

## Entry state

- Milestone 2 done: the native executable links and launches enough to begin
  executing guest code.
- **Knowledge folded in:** *(edit as Milestone 2 finishes — expected blocking
  kernel imports, EH/threading requirements, working build/run commands.)*

## Reference points

- `docs/baselines/xenia-canary-80679bc.md` — Xenia reached an animated
  title-logo loop but not the offline prompt. Notable unresolved externs seen
  there (relevance unproven, do not stub blindly):
  `XamVoiceSetMicArrayIdleUsers` (ordinal `0x48C`), `XeKeysSetKey` at
  `0x827F6BA4`, `XeKeysAesCbc` at `0x827F6BB4`.
- Goal is to **exceed** that baseline: reach the title screen / offline prompt.

## Steps

### 1. Separate read-only and writable roots

- Configure the game-data root separately from writable user/update/cache
  roots in `OnConfigurePaths` / `OnPreSetup` (`src/rb_blitz_app.h`).
- Game data is read-only; writable state must live outside `game/`.

### 2. Verify VFS

- Confirm mounts and case/path normalization against **every attempted open**.
- Log each open to a structured category until stable, then quiet it down.

### 3. Implement blocking kernel imports one at a time

- Work from the import inventory produced in Milestone 2.
- Prefer existing ReXGlue runtime implementations; add `REX_HOOK` whole-function
  overrides in `src/hooks/` only when necessary, recording the guest address.

### 4. Offline services fail fast and faithfully

- Make unavailable network services fail quickly enough for the title's own
  offline path.
- Do **not** fake a successful service unless the game cannot proceed without
  it and the exact contract is understood (see plan).

### 5. Crash/hang diagnostics

- Capture the last guest PC/function on fatal exceptions and hangs.
- Emit the SDK version, game fingerprint, last guest PC/function, and the
  missing import or assertion (release definition of "working", item 10).

### 6. Shutdown and threads

- Verify worker threads, events, timers, and shutdown do not deadlock.

## Acceptance / exit criteria

- [ ] Ten consecutive launches reach the title screen / offline prompt.
- [ ] Each launch closes cleanly (no forced kill).

## Handoff to Milestone 4

Write into `04-menus-content-input-saves.md`:

- the exact run command and required arguments;
- which kernel imports were implemented and how;
- any offline-service behavior the game depends on;
- known-good log categories to watch during menu/content work.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
