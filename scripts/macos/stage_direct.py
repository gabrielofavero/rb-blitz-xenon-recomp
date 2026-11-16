#!/usr/bin/env python3
"""Stage validated controller art and a Finder command for a private local build."""
from pathlib import Path
import argparse
import shlex
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))
from native_assets import controller_entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    entries = controller_entries(ROOT, build / "rb_blitz_controller_schemes", "assets/controller")
    destination = build / "assets/controller"
    if destination.exists():
        shutil.rmtree(destination)
    for entry in entries:
        target = build / entry["to"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / entry["from"], target)
    command = ROOT / "out/macos/Run Rock Band Blitz.command"
    command.parent.mkdir(parents=True, exist_ok=True)
    command.write_text("#!/bin/sh\nexec sh " + shlex.quote(str(ROOT / "scripts/macos/run.sh")) + ' "$@"\n')
    command.chmod(0o755)
    print(f"Direct build ready: {build / 'rb_blitz'} ({len(entries)} controller files)")
    print(f"Run with sh scripts/macos/run.sh or open {command}")


if __name__ == "__main__":
    main()
