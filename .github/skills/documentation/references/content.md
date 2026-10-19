# Expected content, by document type

Every document here is a lab notebook page: what was measured, when, from what evidence, and what is
still unknown. The tables below say what each type must carry. They describe the documents that exist;
they are not a template to fill mechanically.

## Universal rules

| Rule | Why |
| --- | --- |
| Measurements carry a date and the path the evidence lives at (`out/_swap/...`, a log, a capture) | the evidence is gitignored and regenerated, so the path is the provenance |
| A claim is a number, a `file:line`, a quoted log line, or "unproven" | "works", "faster", "fixed" without one is a defect |
| Evidence tags where the document already uses them: `[tree]`, `[boot]`, `[measured]`, `[static]`, `[cited]`, `[assumed]` | a reader must know which claims are proven |
| Ids are stable and citable: `B-0xx`, `R<n>`, `D<n>`, `A<n>`, `P<n>`, prompt ids, ledger item numbers | code comments and other documents cite them by number |
| No machine paths, no usernames, no absolute checkout paths | the repo is public and moved between machines |
| Superseded statements are marked, not deleted | "superseded 2026-10-13 by …" keeps the trail honest |

## Guide (`docs/guides/`)

Expect: a task framing ("build, run, capture a log"), prerequisites, the exact commands, the gates
that can stop you and how they fail, a "what this does not cover", and — where a step is subtle — the
measurement that established it. Numbered sections; commands in fenced blocks with the working
directory implied or stated. `build-and-run.md` is the model; `toolchain.md` and
`distributable.md` are the same shape for their subjects.

Do not put: chronology ("we then tried…") — that is `history/`; plans or open items.

## Reference (`docs/reference/`)

Expect: a registry shape — a table or list you **append a row to**, ids that never change, and a
definition per row. `known-issues.md` splits `Open` from `Accepted (by design, not bugs)`;
`toggles.md` has one row per toggle with its faithful (off) behaviour and the module that implements
it; `backlog.md` says what is not started and the ground rules; `rb3-references.md` carries a
"borrowing rules" section (what may be adapted, with provenance).

Do not put: a fix's narrative (link the owning document), or a row you cannot keep true.

## Engine study (`docs/engine/`)

Expect: what the subsystem is made of (panel, record, file, loader), the addresses and file offsets,
the script/format details, what was measured with evidence, where the code that implements the
conclusion lives, and an explicit **Open** section. The document a toggle row points at must contain
the addresses; that is the contract between `toggles.md` and these files.

Do not put: host-side settings-application or installer behaviour; a plan for the fix.

## Program documents (`docs/settings/`)

Six fixed roles — see [`layout.md`](./layout.md). Consolidated documents (all but `plan.md` and
`contracts.md`) additionally carry:

- a `## Sources` table: `| Part | Original document | Package | Status |`;
- `## Part N — <topic> (was \`docs/plans/<original>.md\`, P<n>)` headings;
- each part's **original section numbers**, demoted one heading level, so an old citation still lands.

`status.md` is the live record: what ships, the owner punch list, the numbered open-item ledger
(numbers are cited — never renumber), the verification state, and the post-mortems. Rewrite it in
place; do not fork a "status-2".

## Plan (`docs/plans/<status>/YYYY-MM-DD-<name>.md`)

Expect, in roughly this order: a **status line** (planning / executed / superseded, with the date);
the request normalized into requirements; what exists today and what does not; **decisions with ids**
(`D1`, `D4`…) that later code comments cite; optionally a table of self-contained prompts (goal,
deliverable, evidence) for executing agents; open questions; and how to reproduce the measurements.
A finished plan also records what each prompt landed and what remains unverified.

Do not: edit a `finished/` plan to reflect new reality (move it, or write the successor); file a plan
without a date; keep a plan in `todo/` once work has started.

## Live status / hand-off

Expect: the date, what changed in this pass, what was measured, what is verified, **what is not
verified** (the important half), and the recommended next steps. A hand-off is superseded by the
document that continues it — say so in one line at the top.

## History (`docs/history/bringup-log.md`)

Append one `## <subject> (<date>)` section **at the top**; one section per subject, not per day.
Number blockers `B-0xx` and keep them citable from code (they are). Do not rewrite old sections;
correct the record in a new section.

## Fixtures (`docs/rythmverse-responses/`)

Raw captures, referenced by the plan that uses them. Never hand-edit; re-capture.

## Checklists

For the pre-hand-over review of a documentation change, see [`review.md`](./review.md).
