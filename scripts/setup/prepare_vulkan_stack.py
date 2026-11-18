#!/usr/bin/env python3
"""Prepare and audit the SDK's pinned Vulkan sources, including the MoltenVK patch."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-dir", type=Path, default=ROOT / "rexglue-sdk")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Use this path as REXGLUE_VULKAN_SOURCE_ROOT when configuring")
    parser.add_argument("--patch-root", type=Path, default=ROOT / "patches/rexglue-sdk-v0.11")
    parser.add_argument("--only", nargs="+", choices=["vulkan-headers", "vulkan-loader",
                                                    "vulkan-memory-allocator", "moltenvk"])
    parser.add_argument("--mirror-root", type=Path)
    parser.add_argument("--mirror", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    engine = args.sdk_dir.resolve() / "scripts/prepare_vulkan_stack.py"
    if not engine.is_file():
        parser.error("Apply the SDK source-dependency patch before preparing Vulkan sources")
    canonical = args.patch_root.resolve() / "thirdparty/moltenvk/0001-retain-presentation-drawable.patch"
    command = [sys.executable, str(engine), "--source-root", str(args.source_root.resolve()),
               "--canonical-moltenvk-patch", str(canonical)]
    if args.only:
        command += ["--only", *args.only]
    if args.mirror_root:
        command += ["--mirror-root", str(args.mirror_root.resolve())]
    for mirror in args.mirror:
        command += ["--mirror", mirror]
    if args.check:
        command += ["--check"]
    if args.offline:
        command += ["--offline"]
    return subprocess.run(command).returncode


if __name__ == "__main__":
    raise SystemExit(main())
