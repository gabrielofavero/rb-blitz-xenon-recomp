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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    sdk = ROOT / "rexglue-sdk"
    pinned = git(ROOT, "ls-tree", "HEAD", "rexglue-sdk").decode().split()[2]
    if git(sdk, "rev-parse", "HEAD").decode().strip() != pinned:
        raise SystemExit("SDK is not at the parent repository's pinned commit")
    patches = sorted((ROOT / "patches/rexglue-sdk").glob("*.patch"))
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
        unexpected = [name for name in changed if name and Path(name) not in paths]
        if unexpected:
            raise SystemExit(f"SDK has edits outside the patch set: {unexpected}")
        if args.check and replacements:
            raise SystemExit(f"SDK patches need applying ({len(replacements)} files)")
        for path, expected in replacements:
            destination = sdk / path
            if expected is None:
                destination.unlink(missing_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(expected)
        print(f"SDK patches: {len(patches)} replayed, {len(replacements)} files updated")


if __name__ == "__main__":
    main()
