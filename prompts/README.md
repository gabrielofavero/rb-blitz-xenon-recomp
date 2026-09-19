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
completed step, write the new knowledge back into three places:

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
  and patch set. **GPL-2.0: technique reference only, never copy its code.**
- `freeqaz/rb3-xenon` — RB3 for **Xbox 360** (same compiler, ABIs and kernel
  imports) and the RB3DX patch set that fixes real retail consoles.
- `freeqaz/rb3` — RB3 for Wii: the engine source of truth, plus the audio
  verification methodology.

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
| 4 — Menus, content, input, saves | `04-menus-content-input-saves.md` | in-progress |
| 5 — Complete one song | `05-complete-one-song.md` | not-started |
| 6 — Reproducible release | `06-reproducible-release.md` | not-started |
