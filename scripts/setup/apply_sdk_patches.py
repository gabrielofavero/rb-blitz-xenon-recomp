#!/usr/bin/env python3
"""Apply the SDK patch set on native hosts without PowerShell.

Replay the complete set against HEAD in a disposable tree first. Only pristine
files or files matching that final replay may be replaced; preserve other edits.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def git(directory: Path, *arguments: str, check: bool = True) -> bytes:
    result = subprocess.run(["git", "-C", str(directory), *arguments],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise SystemExit(result.stderr.decode(errors="replace").strip())
    return result.stdout


def plan_patch_set(sdk: Path, patch_directory: Path, pinned: str,
                   windows_compatible: bool = False) -> tuple[list, list]:
    sdk = sdk.resolve()
    patch_directory = patch_directory.resolve()
    if not (sdk / ".git").exists():
        raise SystemExit(f"SDK submodule is not initialized: {sdk}")
    if git(sdk, "rev-parse", "HEAD").decode().strip() != pinned:
        raise SystemExit(f"{sdk} is not at the parent repository's pinned commit")
    # Nested dependencies retain their upstream pins and their own exact replay
    # audit. Only a successfully audited child may account for a dirty gitlink.
    audited_children = set()
    replacements = []
    reports = []
    for entry in git(sdk, "ls-tree", "-rz", "HEAD").decode().split("\0"):
        if not entry.startswith("160000 commit "):
            continue
        metadata, name = entry.split("\t", 1)
        child_patches = patch_directory / name
        if not any(child_patches.glob("*.patch")):
            continue
        child_replacements, child_reports = plan_patch_set(
            sdk / name, child_patches, metadata.split()[2], windows_compatible)
        replacements.extend(child_replacements)
        reports.extend(child_reports)
        audited_children.add(name)
    patches = sorted(patch_directory.glob("*.patch"))
    paths = set()
    for patch in patches:
        for line in patch.read_text().splitlines():
            if line.startswith("+++ b/") or line.startswith("--- a/"):
                path = Path(line[6:])
                if path.is_absolute() or ".." in path.parts:
                    raise SystemExit(f"unsafe SDK patch path: {path}")
                paths.add(path)
    with tempfile.TemporaryDirectory(prefix="rb-blitz-sdk-patches-") as temporary:
        replay = Path(temporary)
        git(replay, "init", "-q")
        git(replay, "config", "core.autocrlf", "false")
        pristine = {}
        for path in paths:
            result = subprocess.run(["git", "-C", str(sdk), "show", f"HEAD:{path.as_posix()}"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            pristine[path] = result.stdout if result.returncode == 0 else None
            if pristine[path] is not None:
                destination = replay / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(pristine[path])
        for patch in patches:
            git(replay, "apply", "--whitespace=nowarn", str(patch))
        count = 0
        def comparable(data):
            return data.replace(b"\r\n", b"\n") if windows_compatible and data is not None else data

        for path in paths:
            expected = (replay / path).read_bytes() if (replay / path).is_file() else None
            actual = (sdk / path).read_bytes() if (sdk / path).is_file() else None
            if ((sdk / path).is_symlink() or
                    ((sdk / path).exists() and not (sdk / path).is_file()) or
                    comparable(actual) not in (comparable(pristine[path]), comparable(expected))):
                raise SystemExit(f"SDK has local edits; refusing to replace {path}")
            if comparable(actual) != comparable(expected):
                replacements.append((sdk / path, expected))
                count += 1
        # Audit dirt outside the patch set as well (the parent ignores SDK dirt).
        changed = git(sdk, "diff", "HEAD", "--ignore-submodules=untracked",
                      "--name-only", "-z").decode().split("\0")
        untracked = git(sdk, "ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")
        if not windows_compatible:
            changed += untracked
        unexpected = [name for name in changed
                      if name and Path(name) not in paths and name not in audited_children]
        if unexpected:
            raise SystemExit(f"SDK has edits outside the patch set: {unexpected}")
        reports.append(f"{sdk.name} patches: {len(patches)} replayed, {count} files need updating")
        if windows_compatible:
            outside = [n for n in untracked if n and Path(n) not in paths]
            if outside:
                reports.append(f"{sdk.name} unrelated untracked files preserved: {outside}")
    return replacements, reports


def apply_patch_set(sdk: Path, patch_directory: Path, pinned: str, check: bool,
                    windows_compatible: bool = False) -> None:
    # Validate the entire dependency tree before writing any file. A parent
    # conflict must not leave a nested dependency partially updated.
    replacements, reports = plan_patch_set(sdk, patch_directory, pinned, windows_compatible)
    if check and replacements:
        raise SystemExit(f"SDK patches need applying ({len(replacements)} files)")
    for destination, expected in replacements:
        if expected is None:
            destination.unlink(missing_ok=True)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(expected)
    for report in reports:
        print(report)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--repo-root", type=Path, default=ROOT)
    parser.add_argument("--sdk-dir", default="rexglue-sdk")
    parser.add_argument("--patch-root", default="patches")
    parser.add_argument("--windows-compatible", action="store_true",
                        help="Accept CRLF equivalents and report unrelated untracked files")
    args = parser.parse_args()
    sdk = args.repo_root / args.sdk_dir
    # Resolve nested gitlinks from their immediate repository rather than
    # accidentally finding an enclosing HEAD for an uninitialized dependency.
    entry = git(args.repo_root, "ls-tree", "HEAD", "--", args.sdk_dir).decode().split()
    if len(entry) < 3 or entry[0] != "160000":
        raise SystemExit(f"No pinned SDK gitlink found for {args.sdk_dir}")
    apply_patch_set(sdk, args.repo_root / args.patch_root / args.sdk_dir,
                    entry[2], args.check, args.windows_compatible)


if __name__ == "__main__":
    main()
