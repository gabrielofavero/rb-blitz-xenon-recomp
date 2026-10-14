#!/usr/bin/env python3
"""Validate this repository's documentation.

Run from anywhere inside the checkout:

    python .github/skills/documentation/scripts/check_docs.py [--quiet] [--json]

Checks
  links   broken relative markdown links in tracked/untracked .md files   (error)
  stale   `docs/...` mentions that no longer exist                        (error)
  plans   plan files not named/placed per docs/plans/README.md            (error)
  paths   machine-specific values (C:\\Users\\..., usernames)             (error)
  drives  other drive-letter paths (may be a neutral example)             (warning)
  eol     mixed or LF-only line endings in tracked text files             (warning)
  orphan  a document under docs/ that no other file references            (warning)

Exit status: 0 = no errors, 1 = at least one error, 2 = could not run.
"""

from __future__ import annotations

import argparse
import json
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
SKIP_EXACT = (
    "compile_commands.json",
    # the allowlist names the very paths it exempts
    ".github/skills/documentation/references/known-broken-links.txt",
)
STATUS_FOLDERS = ("todo", "in-progress", "finished", "cancelled")

LINK_RE = re.compile(r"\]\(([^)\s]+)\)")
DOC_TOKEN_RE = re.compile(r"docs[/\\][A-Za-z0-9._/-]+")
PLAN_NAME_RE = re.compile(r"^\d{4}-\d{2}-\d{2}-[A-Za-z0-9._-]+\.md$")
MACHINE_RE = re.compile(r"[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/][^\\/\s]+", re.IGNORECASE)
DRIVE_RE = re.compile(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")
LINE_SUFFIX_RE = re.compile(r":\d+(?:-\d+)?$")

# Lines that legitimately name a document that is not there (provenance, merge notes, the index's
# own "never added to this repo" section).
PROVENANCE_RE = re.compile(
    r"\(was `docs/|^\|\s*\d+\s*\|\s*`docs/|preceding revision of|consolidated into|"
    r"never added to this repo|Known stale references|known-broken-links"
)


def repo_root() -> str:
    proc = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    if proc.returncode != 0:
        return ""
    return proc.stdout.strip()


def find_files(root: str, extra_suffixes: tuple[str, ...] = ()) -> list[str]:
    suffixes = TEXT_SUFFIXES + extra_suffixes
    out: set[str] = set()
    for args in (["git", "ls-files"], ["git", "ls-files", "--others", "--exclude-standard"]):
        proc = subprocess.run(args, cwd=root, capture_output=True, text=True)
        out.update(p.replace("\\", "/") for p in proc.stdout.splitlines())
    kept = []
    for path in sorted(out):
        if path.startswith(SKIP_PREFIXES) or path in SKIP_EXACT or not path.endswith(suffixes):
            continue
        kept.append(path)
    return kept


def read_text(path: str) -> str | None:
    try:
        with open(path, encoding="utf-8", newline="") as fh:
            return fh.read()
    except (UnicodeDecodeError, IsADirectoryError, FileNotFoundError):
        return None


def read_bytes(path: str) -> bytes | None:
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except (IsADirectoryError, FileNotFoundError):
        return None


def is_external(target: str) -> bool:
    return target.startswith("#") or "://" in target or target.startswith(("mailto:", "<", "untitled:"))


def strip_suffixes(target: str) -> str:
    path = target.partition("#")[0]
    return LINE_SUFFIX_RE.sub("", path)


def check_links(root: str, files: list[str], known: set[str]) -> tuple[list[dict], list[dict]]:
    errors, warnings = [], []
    for path in files:
        if not path.endswith(".md"):
            continue
        text = read_text(os.path.join(root, path))
        if text is None:
            continue
        for number, line in enumerate(text.split("\n"), 1):
            for target in LINK_RE.findall(line):
                if is_external(target):
                    continue
                rel = strip_suffixes(target)
                if not rel:
                    continue
                if rel.startswith("/"):
                    errors.append({"check": "links", "loc": f"{path}:{number}", "detail": f"absolute link {target}"})
                    continue
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), rel))
                if resolved.startswith(".."):
                    errors.append({"check": "links", "loc": f"{path}:{number}", "detail": f"link escapes the repo: {target}"})
                elif os.path.exists(os.path.join(root, resolved)):
                    continue
                elif resolved in known:
                    warnings.append({"check": "links", "loc": f"{path}:{number}", "detail": f"known broken: {target}"})
                else:
                    errors.append({"check": "links", "loc": f"{path}:{number}", "detail": f"{target} does not exist"})
    return errors, warnings


def check_stale(root: str, files: list[str], known: set[str]) -> tuple[list[dict], list[dict]]:
    errors, warnings = [], []
    for path in files:
        text = read_text(os.path.join(root, path))
        if text is None:
            continue
        in_reference_section = False
        for number, line in enumerate(text.split("\n"), 1):
            if path == "docs/README.md":
                if line.startswith("## "):
                    in_reference_section = "stale references" in line.lower()
                if in_reference_section:
                    continue
            if PROVENANCE_RE.search(line):
                continue
            for token in DOC_TOKEN_RE.findall(line):
                rel = token.replace("\\", "/").rstrip(".,;:)]`\"'")
                rel = LINE_SUFFIX_RE.sub("", rel)
                if rel.endswith("/") or "." not in posixpath.basename(rel):
                    continue
                if os.path.exists(os.path.join(root, rel)):
                    continue
                entry = {"check": "stale", "loc": f"{path}:{number}", "detail": rel}
                if rel in known or os.path.basename(rel) in {os.path.basename(k) for k in known}:
                    warnings.append(entry)
                else:
                    errors.append(entry)
    return errors, warnings


def check_plans(root: str, files: list[str], known: set[str]) -> tuple[list[dict], list[dict]]:
    errors: list[dict] = []
    prefix = "docs/plans/"
    for path in files:
        if not path.startswith(prefix) or not path.endswith(".md"):
            continue
        rest = path[len(prefix):]
        parts = rest.split("/")
        if len(parts) == 1:
            if rest != "README.md":
                errors.append({"check": "plans", "loc": path, "detail": "plan is not inside a status folder"})
            continue
        folder, name = parts[0], parts[-1]
        if folder not in STATUS_FOLDERS:
            errors.append({"check": "plans", "loc": path, "detail": f"unknown status folder '{folder}'"})
            continue
        if name != "README.md" and not PLAN_NAME_RE.match(name):
            errors.append({"check": "plans", "loc": path, "detail": "expected YYYY-MM-DD-<name>.md"})
    return errors, []


def check_paths(root: str, files: list[str], known: set[str]) -> tuple[list[dict], list[dict]]:
    errors, warnings = [], []
    for path in files:
        text = read_text(os.path.join(root, path))
        if text is None:
            continue
        for number, line in enumerate(text.split("\n"), 1):
            for match in MACHINE_RE.finditer(line):
                # `C:\Users\<name>\...` is the sanctioned example form, not a leak.
                rest = line[match.end():]
                if rest.startswith("\\...") or rest.startswith("\\<"):
                    continue
                errors.append({"check": "paths", "loc": f"{path}:{number}", "detail": match.group(0)})
            if DRIVE_RE.search(line) and "$" not in line:
                warnings.append({"check": "drives", "loc": f"{path}:{number}", "detail": DRIVE_RE.search(line).group(0)})
    return errors, warnings


def check_eol(root: str, files: list[str], known: set[str]) -> tuple[list[dict], list[dict]]:
    warnings: list[dict] = []
    autocrlf = subprocess.run(
        ["git", "config", "--get", "core.autocrlf"], cwd=root, capture_output=True, text=True
    ).stdout.strip().lower() == "true"
    for path in files:
        if path.endswith(".patch"):
            continue
        raw = read_bytes(os.path.join(root, path))
        if raw is None or b"\0" in raw[:4096]:
            continue
        crlf = raw.count(b"\r\n")
        lf = raw.count(b"\n")
        if crlf and lf > crlf:
            warnings.append({"check": "eol", "loc": path, "detail": "mixed line endings"})
        elif autocrlf and not crlf and lf:
            warnings.append({"check": "eol", "loc": path, "detail": "LF-only in a CRLF checkout"})
    return [], warnings


def check_orphans(root: str, files: list[str], known: set[str]) -> tuple[list[dict], list[dict]]:
    referenced: set[str] = set()
    for path in files:
        text = read_text(os.path.join(root, path))
        if text is None:
            continue
        for target in LINK_RE.findall(text):
            if is_external(target):
                continue
            rel = strip_suffixes(target)
            if rel:
                referenced.add(posixpath.normpath(posixpath.join(posixpath.dirname(path), rel)))
        for token in DOC_TOKEN_RE.findall(text):
            referenced.add(token.replace("\\", "/").rstrip(".,;:)]`\"'"))
    warnings = []
    for path in files:
        if not path.startswith("docs/") or not path.endswith(".md"):
            continue
        if os.path.basename(path) == "README.md" or path == "docs/history/bringup-log.md":
            continue
        if path not in referenced and os.path.basename(path) not in {
            os.path.basename(r) for r in referenced
        }:
            warnings.append({"check": "orphan", "loc": path, "detail": "no other file references it"})
    return [], warnings


def load_known(root: str) -> set[str]:
    listing = os.path.join(
        root, ".github", "skills", "documentation", "references", "known-broken-links.txt"
    )
    known: set[str] = set()
    text = read_text(listing)
    if text:
        for line in text.split("\n"):
            line = line.strip()
            if line and not line.startswith("#"):
                known.add(line)
    return known


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the documentation.")
    parser.add_argument("--quiet", action="store_true", help="only print errors")
    parser.add_argument("--all", action="store_true", help="also report drive-letter paths (usually neutral examples)")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args()

    root = repo_root()
    if not root:
        print("check_docs: not inside a git checkout", file=sys.stderr)
        return 2
    os.chdir(root)

    files = find_files(root)
    known = load_known(root)

    errors: list[dict] = []
    warnings: list[dict] = []
    for collect in (check_links, check_stale, check_plans, check_paths, check_eol, check_orphans):
        new_errors, new_warnings = collect(root, files, known)
        errors += new_errors
        warnings += new_warnings
    if not args.all:
        warnings = [w for w in warnings if w["check"] != "drives"]

    if args.json:
        print(json.dumps({"errors": errors, "warnings": warnings}, indent=2))
    else:
        print(f"check_docs: {len(files)} file(s), {len(errors)} error(s), {len(warnings)} warning(s)")
        for group, items in (("errors", errors), ("warnings", warnings)):
            if group == "warnings" and args.quiet:
                continue
            if not items:
                continue
            print(f"\n{group}:")
            by_check: dict[str, list[dict]] = {}
            for item in items:
                by_check.setdefault(item["check"], []).append(item)
            for check, entries in sorted(by_check.items()):
                print(f"  {check}: {len(entries)}")
                for entry in entries[:20]:
                    print(f"    {entry['loc']}  {entry['detail']}")
                if len(entries) > 20:
                    print(f"    … and {len(entries) - 20} more ({check})")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
