---
status: not-started
milestone: 6
last_updated: 2026-09-19
---

# Milestone 6 — Reproducible bring-up release

## Entry state

- Milestone 5 done: one bundled song passes launch → results three times in a
  row.
- **Knowledge folded in (2026-09-19, partial — Milestone 5 is still running):**
  - **The acceptance test is not scripted.** Nothing reproducibly drives a song;
    the only existing script (`scripts/acceptance_launches.ps1`) stops at boot. The
    exit criterion below depends on that driver existing, and Milestone 5 needs the
    same one.
  - **Log identity is enforced (2026-09-19).** Both halves of
    definition-of-working #10 are now automatic: every boot logs `boot identity:`
    (the SDK build stamp) and `game data identity:` (the `default.xex` digest, or a
    `MODIFIED` warning when it is not the supported revision), and the build
    refuses to recompile against a dump other than the one
    `config/game_fingerprints.toml` describes —
    [docs/build-and-run.md](../docs/build-and-run.md) §3–§4.
  - **There is no `toolchain.md` repo memory.** The build works, but the absolute
    compiler/cmake/ninja paths live only in `out/build/<preset>/CMakeCache.txt`, and
    none of the three is on a plain shell's PATH; step 4 has to create that file.
  - **Runtime data already lives outside the source tree** (`--game_data_root`, and
    the writable root under the user's Documents), so step 1's data-path
    requirement is mostly a documentation task.

## Reference leads (RB3 mining pass, 2026-09-19)

- **Licensing is settled: GPL-2.0-only.** `LICENSE` is at the repo root and the
  [README](../README.md) states it. It was chosen to be compatible with
  `band3_recomp`'s GPL-2.0, so its code may now be adapted rather than only
  re-derived; the pinned SDK is BSD-3-Clause, which is compatible. Record every
  adapted file in the provenance table at the end of
  [`docs/rb3-references.md`](../docs/rb3-references.md) §9, and never relicense
  to GPL-3.0 (it would cut off those sources). The two `freeqaz` projects are
  CC0-1.0, but they are decompilations of copyrighted game code — port the
  structure, never vendor the tree.
- **Do not ship** the local reference clones, the retail image, extracted assets,
  or the deobfuscated AES keyset.
- **Audit the hooks for "faithful behaviour switched off."** The RB3 native port's
  audit (`docs/native/NATIVE_HACK_AUDIT_2026-06-08.md`) found that its dominant bug
  class was ports silently disabling engine behaviour instead of implementing it —
  every early-return hook needs a recorded reason. Useful alongside "Distribution
  hygiene" below, and it is what "documented quirks" in the handoff must cover.
- **Refresh and re-pin** [`docs/rb3-references.md`](../docs/rb3-references.md) at
  release time; its §10 table says which sections belong to which prompt.
- **Online unblocking is never a release requirement.** The title ships an offline
  mode, so this milestone does not restore or emulate any online service; the
  community's Rock Band Blitz Ultimate mod owns that. What we owe is compatibility —
  an installed payload used alongside the game-data root of the same executable —
  with its own
  criteria in [`docs/ultimate-compat.md`](../docs/ultimate-compat.md). The one concrete
  release risk it creates here is distribution hygiene (below).

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
- **State (2026-09-19): build-side implemented.** Codegen now fails closed on a
  wrong, missing or unreadable `game\default.xex` before any translation happens,
  and prints the digest it found next to the expected one. A *runtime*
  `--game_data_root` mismatch still starts and only warns — deliberate, so an
  Ultimate-installed root stays runnable — so what remains is judging whether the
  boot-time warning is enough for a wrong-root launch.

### 4. Freeze and document

- Freeze minimal supported SDK/game fingerprints and compiler versions
  (toolchain paths in repo memory `toolchain.md`).
- **State (2026-09-19): no home for the freeze yet** — `toolchain.md` does not
  exist anywhere in the repo, though [`docs/bringup-log.md`](../docs/bringup-log.md)
  currently records the three tool paths by hand.
- Document remaining issues in `docs/known-issues.md`; move non-blockers to the
  post-bring-up backlog.

### 5. Distribution hygiene

- Ensure a distributable build contains no retail game data, proprietary
  database-derived symbols, credentials, or machine-specific paths.
- Carry the notices a distributable build has to reproduce: the GPL-2.0 text for
  this project, the BSD-3-Clause notice for the ReXGlue/Xenia SDK, and
  attribution plus GPL-2.0 terms for anything adapted from `band3_recomp`
  (listed in [`docs/rb3-references.md`](../docs/rb3-references.md) §9).
- Do not ship, mirror or vendor Rock Band Blitz Ultimate (its patched `default.xex`,
  its `gen/` or `_ark/` data, or anything extracted from it), and do not bake its
  files into a release "for convenience". An Ultimate payload is user-supplied game
  data on the same footing as the retail dump
  ([`docs/ultimate-compat.md`](../docs/ultimate-compat.md)).

## Acceptance / exit criteria

- [ ] Another authorized developer follows `README.md` from a clean checkout and
      reproduces the working acceptance test.
- [ ] Debug and Release both smoke-test clean.
- [ ] Wrong/missing game data produces an actionable error.

## Handoff

Bring-up complete. Update `DECOMPILATION_PLAN.md` milestone checkboxes and
`docs/bringup-log.md`; start the post-bring-up backlog from the plan's
"Deferred" section — whose **Rock Band Blitz Ultimate compatibility** item (installed
payload as a game-data variant, same executable) is already delivered (B-012), with
its criteria in
[`docs/ultimate-compat.md`](../docs/ultimate-compat.md).

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
