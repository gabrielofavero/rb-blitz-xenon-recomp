---
status: not-started
milestone: 6
last_updated: 2026-09-19
---

# Milestone 6 — Reproducible bring-up release

## Entry state

- Milestone 5 done: one bundled song passes launch → results three times in a
  row.
- **Knowledge folded in:** *(edit as Milestone 5 finishes — acceptance song,
  documented quirks, smoke-test route.)*

## Reference leads (RB3 mining pass, 2026-09-19)

- **Licensing is an open decision, not a detail.** This repository has **no
  `LICENSE`** at its root, while `band3_recomp` is **GPL-2.0**: copying its code
  would force a licence choice nobody has made. Everything borrowed from it must
  stay *technique*, re-derived against Blitz's own addresses. The two `freeqaz`
  projects are CC0-1.0 but are decompilations of copyrighted game code — treat
  their code as reference, never vendor it.
- **Do not ship** the local reference clones, the retail image, extracted assets,
  or the deobfuscated AES keyset.
- **Audit the hooks for "faithful behaviour switched off."** The RB3 native port's
  audit (`docs/native/NATIVE_HACK_AUDIT_2026-06-08.md`) found that its dominant bug
  class was ports silently disabling engine behaviour instead of implementing it —
  every early-return hook needs a recorded reason. Useful alongside "Distribution
  hygiene" below, and it is what "documented quirks" in the handoff must cover.
- **Refresh and re-pin** [`docs/rb3-references.md`](../docs/rb3-references.md) at
  release time; its §10 table says which sections belong to which prompt.

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
