---
status: not-started
milestone: 6
last_updated: 2026-09-09
---

# Milestone 6 — Reproducible bring-up release

## Entry state

- Milestone 5 done: one bundled song passes launch → results three times in a
  row.
- **Knowledge folded in:** *(edit as Milestone 5 finishes — acceptance song,
  documented quirks, smoke-test route.)*

## Steps

### 1. One-command flow

- Add a documented one-command configure/build flow in `README.md`.
- Add a clear runtime data-path argument (game data outside the source tree).

### 2. Clean-checkout smoke tests

- Run Debug and Release smoke tests on a clean checkout.
- Re-verify the second codegen run is still a no-op.

### 3. Error handling

- Test wrong/missing game data and confirm the error is actionable (fingerprint
  mismatch must fail closed, per `config/game_fingerprints.toml`).

### 4. Freeze and document

- Freeze minimal supported SDK/game fingerprints and compiler versions
  (toolchain paths in repo memory `toolchain.md`).
- Document remaining issues in `docs/known-issues.md`; move non-blockers to the
  post-bring-up backlog.

### 5. Distribution hygiene

- Ensure a distributable build contains no retail game data, proprietary
  database-derived symbols, credentials, or machine-specific paths.

## Acceptance / exit criteria

- [ ] Another authorized developer follows `README.md` from a clean checkout and
      reproduces the working acceptance test.
- [ ] Debug and Release both smoke-test clean.
- [ ] Wrong/missing game data produces an actionable error.

## Handoff

Bring-up complete. Update `DECOMPILATION_PLAN.md` milestone checkboxes and
`docs/bringup-log.md`; start the post-bring-up backlog from the plan's
"Deferred" section.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
