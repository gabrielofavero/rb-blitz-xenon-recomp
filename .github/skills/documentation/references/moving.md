# Moving, renaming, merging, splitting, retiring

A document's path and its section numbers are its interface: other documents and ~200 source
comments cite them. So a move is a small refactor, not a file rename.

## The procedure

1. **Decide the destination** with [`layout.md`](./layout.md). A move that is not an improvement in
   placement is not worth the churn.
2. **Move with git**, so history follows:
   ```powershell
   git mv <old-path> <new-path>
   ```
   Never copy-and-delete. For a plan also check the naming rule: `YYYY-MM-DD-<name>.md`, date from the
   document itself, else its creation date (`git log --diff-filter=A --format=%ad --date=short -- <path>`).
3. **Re-point every reference** (markdown *and* code comments):
   ```powershell
   python .github/skills/documentation/scripts/move_docs.py --map docs/<old>.md=docs/<new>.md --dry-run
   python .github/skills/documentation/scripts/move_docs.py --map docs/<old>.md=docs/<new>.md
   ```
   What it does: re-relativizes every relative markdown link (including links *within* the moved file,
   which silently break one level deeper), rewrites root-relative `docs/...` mentions in any tracked
   text file, and — for a merge — appends the part hint you pass with `--hint old=" (part 3, storage)"`.
   Provenance lines (`(was \`docs/...\`)`, `| 1 | \`docs/...\` |`) are preserved.
4. **Update the index and the folder README** (`docs/README.md` row/description, the folder's table).
5. **Verify** (see below), then read the diff: the code changes must be comment-only.

## Merging several documents into one

The settings program did this: sixteen per-package records → six documents. Rules that make the
result citable:

| Rule | Why |
| --- | --- |
| one `## Part N — <topic> (was \`docs/plans/<original>.md\`, P<n>)` section per source | the provenance survives inside the file |
| each part keeps the source's **original section numbers**, demoted one heading level | an old citation `storage.md §3` still resolves, as `architecture.md` part 3 §3 |
| a `## Sources` table at the end | names every original, its package and its status |
| a one-line note in the folder README mapping old path → new document/part | the reader's index of the merge |
| facts are moved, not dropped | merging is deduplication of prose, never of measurements |

When you write a reference to a merged document, include the part: "`docs/settings/graphics.md`
(part 2, graphics policy) §7", not a bare `§7` — the numbers deliberately repeat across parts.

## Splitting a document

Split only when two audiences need different halves and neither can cite the other cheaply. Keep the
section numbers on the half that is cited most, and **document the mapping** (old §N → new document
§M) in the folder README, because the citations in code cannot be re-derived later.

## Retiring and superseding

| Situation | Do |
| --- | --- |
| delivered plan | `git mv` to `plans/finished/` (keep the date prefix); add a row to `plans/finished/README.md` |
| abandoned plan | `git mv` to `plans/cancelled/`, one line at the top: the reason and what supersedes it |
| document superseded by another | one-line banner at the top naming the successor, and the successor says what it absorbed; then move it out of the reader's path |
| content duplicated by an existing document | merge the unique facts in, then delete — and say in the commit message where they went |
| document that was never written (a stale reference) | fix or drop the reference; do not create the document to satisfy a link. Known ones are listed in `known-broken-links.txt` |

Never delete the last copy of a measurement. If the topic dies, the measurements move to the document
that inherits the subject (usually `reference/known-issues.md` for limits, `engine/` for behaviour).

## Delete or keep?

Deleting needs the owner's approval unless the content is fully absorbed. Cheaper default: keep the
document in `finished/` or `cancelled/` where it costs nothing and answers "why not do it that way?".

## Verify

```powershell
python .github/skills/documentation/scripts/check_docs.py
```

Expect `0 error(s)`. Then check the diff of non-markdown files is comment-only:

```powershell
git diff --numstat -- '*.cpp' '*.h' '*.ps1' '*.toml'
```

The checker also reports line-ending damage: this checkout is CRLF (`core.autocrlf=true`, only
`*.patch` pinned to LF). A rewrite that reads a file in text mode and writes it back flips it to LF
across every line — harmless to git's normalisation, but it makes the file differ from the rest of the
checkout on disk. Read/write with `newline=""` (or bytes) whenever you script a text edit; the bundled
scripts do.

## Known-broken links

[`known-broken-links.txt`](./known-broken-links.txt) lists references that are already broken (files
that were deleted from the tree). The checker counts them as known and stays quiet; the list exists so
they are visible rather than silently ignored. Fix one and delete its line.
