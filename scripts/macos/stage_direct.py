#!/usr/bin/env python3
"""Stage validated controller art and a Finder command for a private local build."""
from pathlib import Path
import argparse
import shlex
import shutil
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))
from native_assets import controller_entries
from runtime_inventory import (runtime_targets, target_filename, manifest_entries,
                               vulkan_source, molten_dependency_license)


def verify_runtime(build: Path) -> list[Path]:
    required = [build / "rb_blitz", *(build / target_filename(name, "macos")
                for name in runtime_targets(build, "macos"))]
    for path in required:
        if not path.is_file() or not path.resolve().is_relative_to(build.resolve()):
            raise ValueError(f"missing or external SDK runtime module: {path.name}; rebuild before staging")
    candidates = {path.name: path for path in [*required, *(build / "vulkan/lib").glob("*.dylib")]}
    pending = list(required)
    inspected = set()
    while pending:
        binary = pending.pop()
        if binary in inspected: continue
        inspected.add(binary)
        output = subprocess.run(["otool", "-L", str(binary)], check=True,
                                text=True, capture_output=True).stdout
        for line in output.splitlines()[1:]:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith(("/usr/lib/", "/System/Library/")): continue
            name = Path(dependency).name
            target = candidates.get(name)
            if target is None:
                raise ValueError(f"{binary.name} requires unstaged dependency {dependency}")
            pending.append(target)
    return sorted(inspected)


def stage_licenses(build: Path) -> int:
    config = json.loads((ROOT / "installer/config/platform-packaging.json").read_text())["macos"]
    count = 0
    for entry in manifest_entries(config, build):
        marker = "/Contents/Resources/licenses/"
        if marker not in entry["to"]: continue
        if "fromVulkanSource" in entry: source = vulkan_source(build, entry["fromVulkanSource"])
        elif "fromMoltenDependency" in entry: source = molten_dependency_license(build, entry["fromMoltenDependency"])
        elif "fromSdkStage" in entry: source = build / entry["fromSdkStage"]
        else: source = ROOT / entry["from"]
        if not source.is_file() or source.is_symlink():
            raise ValueError(f"missing or linked license source: {source}")
        destination = build / "licenses" / entry["to"].split(marker, 1)[1]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    runtime = verify_runtime(build)
    licenses = stage_licenses(build)
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
    print(f"Direct build ready: {build / 'rb_blitz'} ({len(entries)} controller files, {len(runtime)} runtime images, {licenses} licenses)")
    print(f"Run with sh scripts/macos/run.sh or open {command}")


if __name__ == "__main__":
    main()
