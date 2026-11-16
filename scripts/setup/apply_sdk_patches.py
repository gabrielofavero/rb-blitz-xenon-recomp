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
    return subprocess.run(["git", "-C", str(directory), *arguments], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def apply_patch_set(sdk: Path, patch_directory: Path, pinned: str, check: bool) -> None:
    if git(sdk, "rev-parse", "HEAD").decode().strip() != pinned:
        raise SystemExit(f"{sdk} is not at the parent repository's pinned commit")
    # Nested dependencies retain their upstream pins and their own exact replay
    # audit. Only a successfully audited child may account for a dirty gitlink.
    audited_children = set()
    for entry in git(sdk, "ls-tree", "-rz", "HEAD").decode().split("\0"):
        if not entry.startswith("160000 commit "):
            continue
        metadata, name = entry.split("\t", 1)
        child_patches = patch_directory / name
        if not any(child_patches.glob("*.patch")):
            continue
        apply_patch_set(sdk / name, child_patches, metadata.split()[2], check)
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
        replacements = []
        for path in paths:
            expected = (replay / path).read_bytes() if (replay / path).is_file() else None
            actual = (sdk / path).read_bytes() if (sdk / path).is_file() else None
            if (sdk / path).is_symlink() or actual not in (pristine[path], expected):
                raise SystemExit(f"SDK has local edits; refusing to replace {path}")
            if actual != expected:
                replacements.append((path, expected))
        # Audit dirt outside the patch set as well (the parent ignores SDK dirt).
        changed = git(sdk, "diff", "HEAD", "--name-only", "-z").decode().split("\0")
        changed += git(sdk, "ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")
        unexpected = [name for name in changed
                      if name and Path(name) not in paths and name not in audited_children]
        if unexpected:
            raise SystemExit(f"SDK has edits outside the patch set: {unexpected}")
        if check and replacements:
            raise SystemExit(f"SDK patches need applying ({len(replacements)} files)")
        for path, expected in replacements:
            destination = sdk / path
            if expected is None:
                destination.unlink(missing_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(expected)
        print(f"{sdk.name} patches: {len(patches)} replayed, {len(replacements)} files updated")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    pinned = git(ROOT, "ls-tree", "HEAD", "rexglue-sdk").decode().split()[2]
    apply_patch_set(ROOT / "rexglue-sdk", ROOT / "patches/rexglue-sdk", pinned, args.check)


if __name__ == "__main__":
    main()
