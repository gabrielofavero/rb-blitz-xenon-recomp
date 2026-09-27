# Backlog

What is still pending. The bring-up milestones 0–5 are closed: the chronology is in
[history/bringup-log.md](history/bringup-log.md), the standing limits in
[known-issues.md](known-issues.md). Everything below is open work, roughly in the
order it should be picked up.

## Milestone 6 — reproducible bring-up release

Exit criterion: another authorized developer follows [README.md](../README.md) from a
clean checkout and reproduces the acceptance run.

- [ ] **Debug and Release smoke tests on a clean checkout** — both presets, one clean
      tree, the documented route from `scripts/acceptance_song.ps1`.
- [ ] **Freeze the supported toolchain** — SDK pin, compiler paths and versions — in
      a `toolchain.md`. The game fingerprint is already frozen and enforced
      ([config/game_fingerprints.toml](../config/game_fingerprints.toml)).
- [ ] **Close the wrong-data test.** The build half fails closed; record the runtime
      half deliberately (a mismatched `--game_data_root` warns and continues, because
      an Ultimate payload is a supported content variant).
- [ ] **Audit the distributable** — confirm no retail data, symbols derived from
      proprietary databases, credentials or machine-specific paths reach the
      installer payload or a packaged build ([installer/](../installer)).

The one-command configure/build flow, the runtime data-path argument and the log
locations are already documented in [README.md](../README.md); nothing is owed there.

## Fixes implied by open known-issues

- [ ] `0x827EC038` — the last forced `functions.toml` entry of the indirect-call
      class: a 24-byte leaf whose address is only taken in data, so no segment
      mentions it (B-006).
- [ ] 32-bit integer textures (`num_format = 1`) still fail to create, and one
      converted texture plus its mips must fit a single 2 MiB upload page (B-010).
- [ ] `longjmp_address` / `setjmp_address` are unset, so guest `longjmp`/`setjmp` has
      no host bridge. Find Blitz's equivalents — `band3_recomp` sets
      `0x82BBB620` / `0x82BBBA50` — or record why they are not needed
      ([rb3-references.md](rb3-references.md) §8).
- [ ] **Hook hygiene**: every hook file states the faithful behaviour and the reason
      for deviating, including each early return.
- [ ] **Host test coverage beyond the five existing targets.** Everything that needs
      a boot is still verified by hand: the SDK-touching hooks, pause/resume, and the
      UI paths outside the offline song loop.
- [ ] **Mouse navigation limits** are accepted, not fixed — a screen whose rows a
      frame difference cannot read is given up on rather than measured, there is no
      guest cursor, and the overlay/foreground gates are reasoned rather than
      exercised. The absolute mapping the old note asked for exists now: the guest's
      own frames are read back (`CaptureGuestOutput`) and the highlight is measured
      onto the pointer's row. What is left is the relative *fallback*, used only when
      frames cannot be read.

## Engine knowledge still to port

[rb3-references.md](rb3-references.md) is the catalogue; these are the items in it
that name an action.

- [ ] **Name functions**: the three tail-branch thunks, then the MOGG rows, then the
      wider anonymous set ([symbols.md](symbols.md)). Names go in
      `config/functions.toml` in the same commit as the doc update.
- [ ] Move `d3d12_readback_resolve` out of the manifest into the build-tree-local
      `rb_blitz.toml`.
- [ ] Check the RB3DX groups 2–10 against Blitz one by one; the `NewFile` hook is P1.
- [ ] Adopt the audio-verify methodology (chroma correlation, speed, distortion) as a
      regression check (§7.1).
- [ ] Add a provenance-table row for anything adapted from another project (§9).

## Ultimate compatibility

- [ ] **Script an Ultimate run** — boot → menu → a song against an installed payload.
      Acceptance criterion 1 currently rests on the Milestone 5 hand observation
      ([ultimate-compat.md](ultimate-compat.md) §7).
- [ ] Re-derive the 12-byte patch table if a payload release ships a different
      `default.xex` (compare its hash against `390e0ae0…` first).

## Scoped, not planned

Designs with their own milestones and kill gates. None of them gates milestone 6 or
the release; each is picked up only if someone chooses to.

- [ ] [plans/av-settings-plan.md](plans/av-settings-plan.md) — host settings
      (resolution, v-sync, volume, safe area) adjustable from inside the game's own
      Audio/Video screen. AV0's five investigations come before any product code, and
      AV0's kill gate may collapse the whole framing.
- [ ] [plans/button-mapping-plan.md](plans/button-mapping-plan.md) — per-action
      control binding layered above the guest input boundary. IM0 task 7, the
      `drive_ui.ps1` D-pad defect, is a tooling prerequisite for its own acceptance.
- [ ] [plans/vr-port-plan.md](plans/vr-port-plan.md) — Meta Quest, then iOS. Its
      precondition is the reproducible Windows build milestone 6 owes.
- [ ] Answer the **input arbitration** question once for both plans and record it in
      each: can the guest consume input while a host dialog is open?

Also deferred, with no design yet: pixel-perfect graphics and UI upgrades, unlocked
frame rate, latency tuning, DLC/export/custom-song compatibility, other controller
backends, Linux/macOS/ARM hosts, packaging and auto-update, symbol-name campaigns,
and mod APIs.

Permanent non-goals: online services — Rock Central, leaderboards, achievements,
challenges, multiplayer. Blitz ships its own offline mode, so restoring them was
never required, and unblocking them is the community mod's job. Our obligation runs
the other way: staying compatible with the **Rock Band Blitz Ultimate** payload
([ultimate-compat.md](ultimate-compat.md)).

## Ground rules

Keep four layers separate: **game input** (locally dumped content — read-only,
untracked, fingerprinted), the **pinned SDK** (no title-specific edits), **generated
code** (regenerate, never hand-edit), and **project code** (manifest, hints, hooks,
patches, diagnostics — every piece of Blitz knowledge lives here). Change the SDK
only when the behaviour is generically wrong for Xbox 360 software and could go
upstream, and then as a patch file ([patches/README.md](../patches/README.md)).

Fix order: correct boundaries or hints → an existing SDK implementation →
a whole-function hook with the guest address recorded → a named data/code patch →
a mid-ASM hook → an SDK patch file. Every hook or patch states its guest address,
the observed failure, the intended behaviour, the evidence, and the milestone that
required it.

Record as you go: chronology in [history/bringup-log.md](history/bringup-log.md),
enduring facts in [known-issues.md](known-issues.md) and [symbols.md](symbols.md),
cross-project knowledge in [rb3-references.md](rb3-references.md). A disproof edits
the document that made the claim.
