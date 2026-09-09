---
status: not-started
milestone: 5
last_updated: 2026-09-09
---

# Milestone 5 — Complete one song

## Entry state

- Milestone 4 done: menus navigate, bundled content enumerates, a song can be
  selected, input works, saves persist.
- **Knowledge folded in:** *(edit as Milestone 4 finishes — chosen song, input
  mapping, storage format, content quirks.)*

## Reference points

- `docs/baselines/xenia-canary-80679bc.md`: historical Xenia report reached
  gameplay but lacked note tracks and 3D background. During bring-up, compare
  failing draws against a Xenia capture — especially resolve/readback and
  render-target transitions.

## Steps

### 1. Song load

- Get through song load without timeout, deadlock, or missing-file errors.

### 2. Graphics

- Verify the Xenos pipeline draws the note highway, notes, HUD, and essential
  background objects.
- Compare failing draws against a Xenia capture; prefer correcting reusable
  Xenos behavior over game-specific rendering hacks.

### 3. Audio

- Verify audio voices, sample formats, streaming, clocks, and pause/resume.
- Measure audio/gameplay drift across a full song; defer fine calibration unless
  drift makes play impossible.

### 4. Stability

- Verify frame pacing and input polling are stable for a complete run.
- Reach results, return to song select, and play again without leaked state or
  a crash.

## Acceptance / exit criteria

- [ ] One named bundled song passes the full launch → results path **three
      times in a row** from a clean process.

## Handoff to Milestone 6

Write into `06-reproducible-release.md`:

- the chosen song used for the acceptance test;
- any known graphics/audio quirks that must be documented for the release;
- the exact smoke-test route used.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
