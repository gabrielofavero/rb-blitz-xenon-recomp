#!/usr/bin/env python3
"""Re-point references after a document was moved, renamed or merged.

Run it *after* the `git mv` (or after the merged document exists), from anywhere in the checkout:

    python .github/skills/documentation/scripts/move_docs.py \
        --map docs/<old>.md=docs/<new>.md [--map <old>.md=docs/<other>.md] \
        [--hint docs/<old>.md=" (part 3, storage)"] [--dry-run]

What it rewrites
  * every relative markdown link whose target moved, and every relative link inside a moved file
    (a file one folder deeper needs one more `../` — that is the case that silently breaks);
  * the same links when the target was merged into a document, so they point at the merged file;
  * root-relative `docs/...` mentions in any tracked/untracked text file — source comments, configs,
    scripts, and the docs themselves.

What it never touches
  * provenance lines: `(was `docs/...`)` headings and `| 1 | `docs/...` |` table rows;
  * external URLs, anchors, and links into `rexglue-sdk/`, `out/`, `game/`;
  * line endings. Files are read and written as bytes, so a CRLF checkout stays CRLF.
    (A naive text-mode rewrite flips whole files to LF here; see references/moving.md.)

Exit status: 0 = nothing left unresolved, 1 = unresolved links were reported, 2 = could not run.
"""

from __future__ import annotations

import argparse
import os
import posixpath
import re
import subprocess
import sys

TEXT_SUFFIXES = (
    ".md", ".cpp", ".h", ".hpp", ".ps1", ".py", ".toml", ".cmake", ".txt", ".iss",
    ".json", ".rc", ".yml", ".yaml", ".bat", ".cmd", ".in", ".gitignore", ".gitattributes",
)
SKIP_PREFIXES = (
    "out/", "rexglue-sdk/", "game/", "assets/", "generated/", "_scratch/", ".git/",
    "patches/rexglue-sdk/", "installer/out/", "profile/out/", "tools/milotex/bin/",
    "tools/milotex/obj/",
)
SKIP_EXACT = ("compile_commands.json",)

LINK_RE = re.compile(r"\]\(([^)\s]+)\)")
PROVENANCE_RE = re.compile(r"\(was `docs/|^\|\s*\d+\s*\|\s*`docs/")


def repo_root() -> str:
    proc = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    return proc.stdout.strip() if proc.returncode == 0 else ""


def norm(path: str) -> str:
    return posixpath.normpath(path.replace("\\", "/").strip())


def files_in(root: str) -> list[str]:
    out: set[str] = set()
    for args in (["git", "ls-files"], ["git", "ls-files", "--others", "--exclude-standard"]):
        proc = subprocess.run(args, cwd=root, capture_output=True, text=True)
        out.update(p.replace("\\", "/") for p in proc.stdout.splitlines())
    return sorted(
        p for p in out
        if not p.startswith(SKIP_PREFIXES) and p not in SKIP_EXACT and p.endswith(TEXT_SUFFIXES)
    )


def is_external(target: str) -> bool:
    return target.startswith("#") or "://" in target or target.startswith(("mailto:", "<", "untitled:"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", action="append", required=True, metavar="OLD=NEW",
                        help="a document that moved (or was merged into NEW); repeatable")
    parser.add_argument("--hint", action="append", default=[], metavar="OLD=TEXT",
                        help='text appended to plain-text mentions of OLD, e.g. \'docs/<doc>.md= (part 3, storage)\'')
    parser.add_argument("--dry-run", action="store_true", help="report what would change; write nothing")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    root = repo_root()
    if not root:
        print("move_docs: not inside a git checkout", file=sys.stderr)
        return 2
    os.chdir(root)

    moves: dict[str, str] = {}
    for item in args.map:
        if "=" not in item:
            print(f"move_docs: bad --map '{item}' (expected OLD=NEW)", file=sys.stderr)
            return 2
        old, new = (norm(part) for part in item.split("=", 1))
        if not os.path.exists(new):
            if args.dry_run:
                print(f"move_docs: note - target does not exist yet ({new}); dry run continues")
            else:
                print(f"move_docs: target does not exist yet: {new}\n"
                      f"           run `git mv` first (or create the merged document).", file=sys.stderr)
                return 2
        moves[old] = new

    hints: dict[str, str] = {}
    for item in args.hint:
        if "=" not in item:
            print(f"move_docs: bad --hint '{item}' (expected OLD=TEXT)", file=sys.stderr)
            return 2
        old, text = item.split("=", 1)
        hints[norm(old)] = text

    # new path -> old path, so links *inside* a moved file are re-relativized too.
    by_new = {new: old for old, new in moves.items()}
    ordered = sorted(moves, key=len, reverse=True)

    changed: list[tuple[str, int, int, int]] = []
    unresolved: list[tuple[str, int, str]] = []

    for path in files_in(root):
        try:
            with open(path, "rb") as fh:
                raw = fh.read()
        except (IsADirectoryError, FileNotFoundError):
            continue
        if b"\0" in raw[:4096]:
            continue
        # utf-8 with surrogateescape keeps any stray byte intact while preserving CRLF (bytes I/O).
        text = raw.decode("utf-8", errors="surrogateescape")
        old_path = by_new.get(path, path)
        new_dir = posixpath.dirname(path) or "."
        old_dir = posixpath.dirname(old_path) or "."
        lines = text.splitlines(keepends=True)

        links_changed = 0
        mentions_changed = 0
        for index, line in enumerate(lines):
            new_line = line
            if path.endswith(".md") and LINK_RE.search(new_line):
                def repl(match: re.Match) -> str:
                    nonlocal links_changed
                    target = match.group(1)
                    if is_external(target) or target.startswith("/"):
                        return match.group(0)
                    body, sep, anchor = target.partition("#")
                    resolved = posixpath.normpath(posixpath.join(old_dir, body))
                    moved = moves.get(resolved, resolved)
                    rel = posixpath.relpath(moved, new_dir)
                    # Same file from the new location -> nothing to do (also avoids normalizing
                    # harmless spellings such as "./x" into "x").
                    if posixpath.normpath(rel) == posixpath.normpath(body):
                        return match.group(0)
                    links_changed += 1
                    return f"]({rel}{sep}{anchor})"

                new_line = LINK_RE.sub(repl, new_line)
            if not PROVENANCE_RE.search(new_line):
                for old in ordered:
                    count = new_line.count(old)
                    if count:
                        new_line = new_line.replace(old, moves[old] + hints.get(old, ""))
                        mentions_changed += count
            lines[index] = new_line

        new_text = "".join(lines)
        if new_text != text:
            if not args.dry_run:
                with open(path, "wb") as fh:
                    fh.write(new_text.encode("utf-8", errors="surrogateescape"))
            changed.append((path, links_changed, mentions_changed, 0))

    # a second pass reports any relative link that still does not resolve (informational)
    if not args.dry_run:
        for path, *_ in changed:
            try:
                with open(path, "rb") as fh:
                    text = fh.read().decode("utf-8", errors="surrogateescape")
            except OSError:
                continue
            if not path.endswith(".md"):
                continue
            for number, line in enumerate(text.split("\n"), 1):
                for target in LINK_RE.findall(line):
                    if is_external(target) or target.startswith("/"):
                        continue
                    body = target.partition("#")[0]
                    if not body:
                        continue
                    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), body))
                    if not os.path.exists(resolved):
                        unresolved.append((path, number, target))

    verb = "would rewrite" if args.dry_run else "rewrote"
    print(f"move_docs: {verb} {len(changed)} file(s) for {len(moves)} mapping(s)")
    if not args.quiet:
        for path, links, mentions, _ in changed:
            print(f"  {path}  ({links} link(s), {mentions} mention(s))")
    if unresolved:
        print(f"\nunresolved links after the rewrite: {len(unresolved)}")
        for path, number, target in unresolved[:40]:
            print(f"  {path}:{number}  ->  {target}")
        print("  (check these by hand; a moved target that was already broken stays broken)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
