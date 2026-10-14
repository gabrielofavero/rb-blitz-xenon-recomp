# Layout — where each document belongs

Place a new document by analogy with the ones already there. If the analogy is unclear, prefer
extending an existing document over adding a file.

## The tree

```
docs/
  README.md          the index: one row per document, plus the conventions and the "start here" table
  guides/            task guides — "how do I build / run / ship / extract / configure this"
  reference/         standing records — append a row, keep ids stable
  engine/            how the recompiled guest behaves, one subsystem per document
  settings/          the in-game settings program (fixed set of six roles)
  plans/
    todo/            written, not started
    in-progress/     actively being worked
    finished/        delivered, still cited
    cancelled/       abandoned or superseded, kept so the ground is not walked twice
  history/           the append-only bring-up log + its index
  rythmverse-responses/   raw API captures (fixtures), never hand-edited
```

## What belongs where

| Folder | Belongs | Does not belong |
| --- | --- | --- |
| `guides/` | procedures with exact commands; format notes; gates and their failure modes; policy records (`ultimate-compat.md`, `distributable.md`) | subsystem measurements (→ `engine/`), plans (→ `plans/`) |
| `reference/` | open issues and accepted limitations (`known-issues.md`), the unstarted list (`backlog.md`), the guest image inventory (`symbols.md`), other projects' techniques worth porting (`rb3-references.md`) | how-to steps, current status of a program |
| `engine/` | a study of one subsystem: the panel/file/loader, the addresses, the hook point, what was measured, where it lives, what is open | anything about the host UI or packaging |
| `settings/` | the settings program only — see the role table below | a new subsystem study (→ `engine/`) |
| `plans/<status>/` | plans, package implementation records, hand-offs; each dated | living reference material (→ `guides/`/`reference/`) |
| `history/` | chronological "what happened"; newest section at the top | anything a topical document should own |

## `settings/` roles (do not add a seventh without the owner)

| Document | Role | Grows by |
| --- | --- | --- |
| `plan.md` | the master plan: requirements, the row-by-row menu specification, storage and precedence, the package sequence P0–P12 | new packages, scope changes |
| `contracts.md` | frozen interfaces (P0). Changes are a decision, not an edit | new frozen shapes, marked as decisions |
| `architecture.md` | how it is built: parts 1–6 (bridge, panel API, storage, boot wiring, Ultimate adapter, hot apply) | a new part when a new mechanism lands |
| `menu.md` | the player-visible surfaces: parts 1–4 (merged menu, menu compatibility, Controller Settings, installer) | a new part per surface |
| `graphics.md` | resolution and graphics: parts 1–3 (resolution, capability policy, custom resolutions) | a new part, or extend the owning one |
| `status.md` | **the live tracker**: what ships, the punch list, the numbered open-item ledger, verification state | rewrite in place; keep the numbers |

## The current inventory

`docs/README.md` holds the authoritative index (folder → documents → one line each). Keep it in sync:
it is the first thing a reader and an agent sees, and the only place that names every document.

## Adding a folder

Only when a topic outgrows a single document and has an owner — the settings program is the precedent
(it went from one plan to sixteen records to six consolidated documents). Steps:

1. Propose it to the owner with the documents that would move into it.
2. Create the folder **with a `README.md`** that says what lives there and the reading order.
3. Move the documents in with `git mv` + `move_docs.py` (see `moving.md`).
4. Add one row to `docs/README.md`.

## Related

- [`content.md`](./content.md) — what each kind of document must contain.
- [`moving.md`](./moving.md) — move, merge, split, retire.
- [`review.md`](./review.md) — the review checklist and what the checker enforces.
