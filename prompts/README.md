# Milestone prompts

This directory holds one **living agent prompt per milestone** in
[`DECOMPILATION_PLAN.md`](../DECOMPILATION_PLAN.md). Each file is a
self-contained, actionable brief that an AI agent can execute against this
repository. Run them in order, one at a time.

## Why these exist

The plan is the high-level map; these files are the **forward-looking,
specific instructions** for the next chunk of work. They carry the current
verified state, exact commands, and the exact acceptance criteria for each
milestone — so each session starts from the best known information instead of
re-deriving it.

## How to run

1. Pick the first file whose status is not `done`.
2. Hand that file (plus this README) to the AI and say: *"Execute this
   milestone prompt."*
3. Work through it, following the bring-up loop at the bottom of each file.

## The editing loop (the important part)

Prompts get **more assertive** as we learn. After every session or every
completed step, write the new knowledge back into four places:

1. **`docs/bringup-log.md`** — the chronological record (what happened, in
   order).
2. **`docs/known-issues.md`** / **`docs/symbols.md`** — enduring facts that
   outlive a single blocker.
3. **`docs/rb3-references.md`** — cross-project knowledge: what the Rock Band 3
   recompilation and decompilations already solved on this engine, and which
   technique transfers to us. Re-read it before starting anything that smells
   like an *engine* problem rather than a Blitz-specific one.
4. **These prompt files** — fold the knowledge into:
   - the **"Entry state"** section of the prompt you just worked on, and
   - the **"Knowledge folded in"** and **"Handoff to next milestone"**
     sections of the *following* prompts.

That third step is what makes the next prompt accurate: instead of guessing,
the next agent starts knowing exactly what was discovered, what was tried, and
what failed.

### Rules

- Never contradict `DECOMPILATION_PLAN.md`. If the plan and a prompt disagree,
  the plan wins and the prompt must be corrected.
- Keep every fact in a prompt verifiable: guest addresses, hashes, sizes,
  file paths, and commands should be copy-pastable.
- Prompts carry **specifics** (addresses, commands, current status); the plan
  carries **intent and policy** (ownership boundaries, fix-order, "working"
  definition).
- Update the `last_updated` date and `status` in each file's header when you
  edit it.
- Prefer knowledge over code when borrowing from another project. Every
  *address* in `docs/rb3-references.md` is RB3's, never ours.

## Reference mining

Rock Band Blitz shares its engine with Rock Band 3, so other projects have
already walked a lot of this road:

- `ihatecompvir/band3_recomp` — RB3 on the same ReXGlue SDK, with a mature hook
  and patch set. **GPL-2.0, which is what we license under: its code may be
  adapted, with attribution.**
- `freeqaz/rb3-xenon` — RB3 for **Xbox 360** (same compiler, ABIs and kernel
  imports) and the RB3DX patch set that fixes real retail consoles.
- `freeqaz/rb3` — RB3 for Wii: the engine source of truth, plus the audio
  verification methodology.
- `ultimate-mods-rb/blitz-ultimate` — **Rock Band Blitz Ultimate**, the community QoL
  mod for Blitz and the reason online unblocking is not our job: it is the Blitz
  sibling of the RB3DX patch set (a different project), and our obligation is to run
  an installed payload by drag-and-drop rather than to restore services
  ([`docs/ultimate-compat.md`](../docs/ultimate-compat.md)).

[`docs/rb3-references.md`](../docs/rb3-references.md) catalogues what is worth
porting. It is a living document: when a lead there is proved or disproved
against Blitz, record the outcome in `docs/bringup-log.md` and correct the
document.

## Status map

| Milestone | File | Status |
| --- | --- | --- |
| 0 — Baseline safe & reproducible | `00-baseline-reproducible.md` | done |
| 1 — Scaffold & generate | `01-scaffold-generate.md` | done |
| 2 — Close analysis & compile | `02-close-analysis-compile.md` | done |
| 3 — Guest entry & offline boot | `03-guest-entry-boot.md` | done |
| 4 — Menus, content, input, saves | `04-menus-content-input-saves.md` | done |
| 5 — Complete one song | `05-complete-one-song.md` | in-progress |
| 6 — Reproducible release | `06-reproducible-release.md` | not-started |

Milestones 0–4 are **done**. The title is **playable and milestone 5's exit
criterion is recorded**: as of 2026-09-20
[scripts/acceptance_song.ps1](../scripts/acceptance_song.ps1) drives
launch → the named song "These Days" → results three times in a row from clean
processes, keeping every screen and log it asserted on. Milestone 4 closed the same
day — its ARK/HDR read audit, pad-driven verification and save persistence all have
evidence now — so milestone 5 is open on measurement rather than function (Xenia
draw comparison, audio voices/clocks/pause-resume, drift, frame pacing).
[docs/bringup-log.md](../docs/bringup-log.md) has the chronological detail
("Milestone 4 close-out", "Milestone 4 close-out completed", "Milestone 5 — first
songs played to the end", "Milestone 5 acceptance: one named song, three
clean-process runs") and [docs/known-issues.md](../docs/known-issues.md) has the
enduring facts.
