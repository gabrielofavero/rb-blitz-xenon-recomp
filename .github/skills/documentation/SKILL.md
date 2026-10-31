---
name: documentation
description: 'Use when writing, editing, moving, renaming, merging, retiring or reviewing anything under `docs/`, or when a code change needs its documentation updated. Covers where a document belongs (guides/, reference/, engine/, settings/, plans/<status>/, history/), the naming rules (dated plans `YYYY-MM-DD-<name>.md`, stable ids in registries), what each kind of document must contain, the lab-notebook evidence rules, which documents a change obliges you to update (known-issues, toggles, plan status, the index at `docs/README.md`), and the scripts that check and re-point references when a document moves.'
---

# Documentation

This repository is documented as a **lab notebook**: measurements with dates, the evidence path they
came from, and an explicit "not proven" wherever something is unverified. The docs are load-bearing —
source comments cite them by section (`docs/settings/architecture.md` (part 3, storage) §3) — so a
document is part of the change that touches its subject, not an afterthought.

Start at [`docs/README.md`](../../../docs/README.md) (the index); it is the map this skill maintains.

## When to use

- Writing a new document, or deciding **where** one belongs.
- Moving, renaming, merging, splitting, superseding or deleting a document.
- A code change that invalidates or extends what a document says.
- Reviewing a docs diff, or answering "is the documentation still true?".

## When not to use

- General code work with no documentation consequence (see
  [`../../copilot-instructions.md`](../../copilot-instructions.md) for the repo rules that do).
- Reconstructing project history — that is [`../../../docs/history/bringup-log.md`](../../../docs/history/bringup-log.md).

## 1. Orient (60 seconds)

1. Read the nearest folder README — [`docs/README.md`](../../../docs/README.md),
   [`docs/engine/README.md`](../../../docs/engine/README.md),
   [`docs/settings/README.md`](../../../docs/settings/README.md),
   [`docs/plans/README.md`](../../../docs/plans/README.md),
   [`docs/history/README.md`](../../../docs/history/README.md).
2. If the change is a move or a merge, run the checker **before and after** (§6).
3. Find the document that owns the subject. Extending it beats adding a file — a new document is the
   answer only when nothing existing covers the topic (see [`references/layout.md`](./references/layout.md)).

## 2. Where a new document goes

| The content is… | It belongs in | Naming |
| --- | --- | --- |
| a task ("build, run, ship, extract") | `docs/guides/` | descriptive, no date |
| a standing record or registry (issues, backlog, symbols, porting notes) | `docs/reference/` | descriptive, no date |
| how the recompiled guest behaves (a subsystem study) | `docs/engine/` | descriptive, no date |
| the in-game settings program | `docs/settings/` | fixed set of six roles — see `settings/README.md` |
| a plan, a package record, a hand-off | `docs/plans/<status>/` | `YYYY-MM-DD-<name>.md` |
| history ("what happened on this date") | `docs/history/` | append at the top of the log |
| raw captured data | `docs/rythmverse-responses/` | leave as captured |

Rules: a plan's date is the date the document itself states, otherwise the day it was added. Status
folders are `todo/`, `in-progress/`, `finished/`, `cancelled/`. Never add a new top-level folder
under `docs/` for one document — propose it to the owner first.

## 3. What each document must contain

Per-type expectations (sections, tables, ids, evidence tags) are in
[`references/content.md`](./references/content.md). The rules that apply to **every** document:

- **Evidence, not adjectives.** A claim is a measurement, a `file:line`, a quoted log line, or an
  explicit "unproven". Tag sourced facts as `[tree]` / `[boot]` / `[measured]` / `[static]` when the
  document already uses that convention.
- **Dates on measurements**, and the gitignored path the evidence lives at (`out/_swap/...`).
- **Stable ids.** `B-0xx` (blockers), `R<n>` (enhancement items), `D<n>`/`A<n>`/`P<n>` (plan units),
  plan prompt ids, and numbered open-item ledgers are cited from code and other documents. Append;
  never renumber or reuse.
- **No machine-specific values.** No `C:\Users\<name>\...`, no absolute checkout paths, and no
  another machine's folders. Use `<checkout>`, `<home>`, or a neutral example such as `D:\Songs`.
- **Links resolve, and are relative.** Link to a section by number/name, e.g.
  "`docs/guides/dlc.md` §4.2".
- **Superseding is explicit.** When a document stops being true, either rewrite it (and say what
  changed) or add a one-line banner naming its successor. Never leave a wrong document looking
  current, and never silently delete a measurement — move it to the document that inherits the topic.

## 4. Documentation is part of the change

When a code change lands, ask which of these it obliges:

| If the change… | Update |
| --- | --- |
| adds, removes or renames an enhancement toggle or cvar | [`docs/engine/toggles.md`](../../../docs/engine/toggles.md) (one row: toggle, feature, faithful behaviour, implementing module) |
| fixes, introduces or narrows a limitation | [`docs/reference/known-issues.md`](../../../docs/reference/known-issues.md) (`Open` or `Accepted (by design)`) |
| changes what is not started yet | The owning plan in [`docs/plans/todo/`](../../../docs/plans/todo/README.md) |
| advances a plan or package | its status folder, and the live tracker (`docs/settings/status.md` for the settings program) |
| adds a document, or changes the layout | [`docs/README.md`](../../../docs/README.md) index (a new row, one line, in the right table) |
| changes the UI, a menu file or a hook point | the owning `docs/engine/*.md` study, not only the code comment |
| changes build, packaging or the toolchain record | `docs/guides/build-and-run.md`, `docs/guides/toolchain.md`, `docs/guides/distributable.md` |

A code comment that cites a document must cite a path that exists — that is checked (§6).

## 5. Moving, renaming, merging, splitting, retiring

The full procedure is in [`references/moving.md`](./references/moving.md). The short version:

1. `git mv` (never copy-and-delete) — history and blame are preserved.
2. Re-point every reference in the repository (substitute the real old and new paths):
   `python .github/skills/documentation/scripts/move_docs.py --map docs/<old>.md=docs/<new>.md`
   It is markdown-aware, relative-link aware and EOL-preserving; run `--dry-run` first.
3. **Merging** several documents into one: give each source its own `## Part N — <topic> (was
   `path`)` section, keep the source's **original section numbers** (demoted one heading level), and
   add a `## Sources` table. Cite the result as "`<doc>` part N §X".
4. Update the folder README/index rows, then verify (§6).

## 6. Verify

```powershell
python .github/skills/documentation/scripts/check_docs.py
```

It checks broken relative links, `docs/...` mentions that no longer exist, plan naming and placement,
machine paths in tracked files, and line endings. Exit 0 = clean; the checks and their severities are
in [`references/review.md`](./references/review.md). Run it after any move or merge, and before
handing a docs change over.

**Two traps it exists to catch**

- **Line endings.** This checkout is CRLF (`core.autocrlf=true`); only `*.patch` is pinned to LF. A
  script that rewrites a file must read and write it without newline translation (Python:
  `newline=""`, or bytes), or the whole file flips to LF and shows up as a bogus diff. The bundled
  scripts do this correctly — prefer them over ad-hoc edits.
- **References in code.** ~200 references live in C++/PowerShell/TOML comments, not in markdown. A
  plain `git mv` leaves them stale; `move_docs.py` rewrites them (and adds a `(part N, …)` hint when
  the target was merged into a part).

## 7. Delegating

- Auditing the docs without changing them → the read-only **docs-auditor** agent (see
  [`../../agents/README.md`](../../agents/README.md)). It runs the checker and reports findings.
- Gathering context across the codebase before writing → a read-only exploration subagent.

## Anti-patterns

- **Condensing away a measurement.** Shortening prose is fine; deleting a number, address, cvar,
  path or gotcha is not. If a rewrite must drop facts, say which, and where they went.
- **Renumbering a merged or cited document.** Section numbers are part of the documents' interface.
- **A new top-level folder or a new file for one paragraph.** Extend the document that owns the topic.
- **A document with no status.** Every plan says what is done and what is not; every study says what
  is unproven.
- **Editing a `finished/` plan.** Its conclusions are the record of that moment — write the new truth
  where it belongs and let the plan stand.
