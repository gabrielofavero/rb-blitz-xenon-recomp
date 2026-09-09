---
status: not-started
milestone: 4
last_updated: 2026-09-09
---

# Milestone 4 — Menus, content discovery, input, and saves

## Entry state

- Milestone 3 done: ten consecutive clean launches reach the title/offline
  prompt.
- **Knowledge folded in:** *(edit as Milestone 3 finishes — run command,
  implemented kernel imports, offline-service behavior, useful log categories.)*

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

## Handoff to Milestone 5

Write into `05-complete-one-song.md`:

- the named bundled song(s) available for the full-playthrough test;
- input mapping details (which XInput controls map to which actions);
- save/storage serialization format and where writable state lives;
- any content-enumeration quirks that could affect song load.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
