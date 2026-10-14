# Plans

Work plans, filed by status. A plan is named `YYYY-MM-DD-<name>.md` — the date the document itself
states, or the date it was first added when it states none — so the folder sorts chronologically and
the age of a plan is visible from its name.

| Folder | Meaning |
| --- | --- |
| [`todo/`](todo) | Written, not started. |
| [`in-progress/`](in-progress) | Actively being worked. |
| [`finished/`](finished) | Delivered; kept because the measurements and decisions are still cited. |
| [`cancelled/`](cancelled) | Abandoned or superseded before delivery, kept so the same ground is not walked twice. |

Moving a plan is one `git mv` into the next folder; the date prefix never changes. The naming and
placement rules are part of the `documentation` skill
([`.github/skills/documentation/SKILL.md`](../../.github/skills/documentation/SKILL.md)), which also
re-points the references in code comments when a plan moves.

## Other plans

Two programs outgrew the plan format and now live with their subject instead:

- **In-game settings** — sixteen per-package records were consolidated into
  [`../settings/`](../settings). The live tracker is [`../settings/status.md`](../settings/status.md).
- **The launcher → Controller Settings** — its plan is finished
  ([`finished/2026-09-27-launcher-plan.md`](finished/2026-09-27-launcher-plan.md)); the application was
  renamed on 2026-10-14
  ([`finished/2026-10-14-controller-settings-rename.md`](finished/2026-10-14-controller-settings-rename.md)),
  and what it still owes the settings work is tracked in `../settings/status.md`.

The engine-level studies that plans here refer to (menu layout, toggles, input, diagnostics) live in
[`../engine/`](../engine), not in this folder.
