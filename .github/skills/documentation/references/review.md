# Review checklist and what the checker enforces

## The checker

```powershell
python .github/skills/documentation/scripts/check_docs.py [--quiet] [--json]
```

| Check | Severity | What it means |
| --- | --- | --- |
| `links` | error | a relative markdown link in a tracked `.md` file does not resolve |
| `stale` | error | a `docs/...` mention names a path that does not exist (provenance lines, the index's "known stale references" section, and `known-broken-links.txt` exempt) |
| `plans` | error | a file in `docs/plans/<status>/` is not `YYYY-MM-DD-<name>.md`, or lives in an unknown status folder |
| `paths` | error | a user-profile path (`C:\Users\<name>\...`) or an absolute checkout path in a tracked file; the `\...` placeholder form is treated as an example |
| `eol` | warning | mixed line endings, or a file that is LF-only while the checkout is CRLF |
| `orphan` | warning | a document under `docs/` that no other document links to |
| `drives` | warning | other drive-letter paths (may be a legitimate neutral example such as `D:\Songs`) |

Exit code is 1 when any `error` fires. Warnings never fail the run; read them.

Run it **before** a move (baseline), after the rewrite, and before handing over.

## Review checklist

Use this for any documentation change (the **docs-auditor** agent runs it and reports):

**Placement and naming**
- [ ] The document is in the folder that owns its topic; nothing similar already exists there.
- [ ] Plans: `YYYY-MM-DD-<name>.md`, in the right `plans/<status>/` folder, date taken from the
      document or its creation date.
- [ ] The folder README and `docs/README.md` list it (or no longer list it, if it moved).

**Content**
- [ ] Every new claim carries evidence: a measurement with a date, a `file:line`, a quoted log line,
      or an explicit "unproven".
- [ ] Ids are new (no reuse), and cited ids still resolve to the same thing.
- [ ] The "not done" / "unverified" half is present — a document that only reports success is incomplete.
- [ ] Superseded statements are marked with the date and the successor.
- [ ] No machine-specific values; placeholders (`<checkout>`, `<home>`, `D:\Songs`) where an example is
      needed.
- [ ] Voice: impersonal, lab-notebook, no marketing, no emoji.

**Mechanics**
- [ ] `check_docs.py` reports 0 errors.
- [ ] Relative links are relative (not absolute paths, not `file:///`), and section references name
      the part when they point into a consolidated document.
- [ ] The diff of non-markdown files is comment-only (`git diff --numstat -- '*.cpp' '*.h' '*.ps1' '*.toml'`).
- [ ] No file flips to LF-only (the CRLF trap in [`moving.md`](./moving.md)).
- [ ] Nothing was renumbered that is cited elsewhere; a split/merge records its section mapping.

**Consequences**
- [ ] The documents that own the touched subject were updated (see the table in the skill's §4:
      `toggles.md`, `known-issues.md`, the owning dated plan, its status, the live tracker, `docs/README.md`).
- [ ] A plan whose work finished moved to `finished/`, and its plan README row says what it delivered.

## How the auditor reports

One table, ordered by severity, each row actionable — then what was **not** checked:

| Severity | Location | Finding | Why it matters | Suggested fix |
| --- | --- | --- | --- | --- |
| error / warning | `path:line` | what the checker or the reader sees | the consequence (broken citation, stale truth, context cost) | the smallest correct change |

Close with: files inspected, checks run, and the limits of the audit (for example: "no runtime
evidence was re-verified; the claims were checked for internal consistency only").
