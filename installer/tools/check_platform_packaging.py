"""Validate the frozen native package inputs before staging or packaging.

The stage audit is intentionally unusable while a target's native dependency
inventory is pending. Prompt 2/4 must fill that inventory from linked binaries.
"""

from __future__ import annotations

import argparse
import json
import plistlib
import re
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "installer/config/platform-packaging.json"
SOURCES = {"from", "fromTarget", "fromSdkStage", "fromTemplate", "fromGenerated"}
TARGETS = {"rb_blitz", "rb_blitz_linux_launcher", "rb_blitz_macos_launcher", "rexruntime", "rexgpu-xenos"}


def fail(message: str) -> None:
    raise SystemExit(f"platform packaging: {message}")


def relative_path(value: str, label: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if not value or path.is_absolute() or ".." in path.parts or "\\" in value:
        fail(f"unsafe {label}: {value!r}")
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-root", type=Path, help="check an exact staged file tree")
    parser.add_argument("--target", choices=("linux", "macos"), help="stage target")
    args = parser.parse_args()
    if bool(args.stage_root) != bool(args.target):
        fail("--stage-root and --target must be given together")

    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        fail("unsupported schemaVersion")
    app_id = data.get("applicationId")
    if not isinstance(app_id, str) or not re.fullmatch(r"[a-z0-9]+(?:\.[a-z0-9]+)+", app_id):
        fail("invalid applicationId")
    if data.get("versionSource") != "installer/config/pins.toml:[installer].version":
        fail("version must come from installer/config/pins.toml")

    for target in ("linux", "macos"):
        config = data[target]
        entries = config["stageManifest"]
        destinations: set[str] = set()
        for entry in entries:
            sources = SOURCES.intersection(entry)
            if len(sources) != 1:
                fail(f"{target} entry must have one source: {entry!r}")
            source_type = next(iter(sources))
            destination = str(relative_path(entry["to"], "stage destination"))
            if destination in destinations:
                fail(f"{target} duplicate destination: {destination}")
            destinations.add(destination)
            source = entry[source_type]
            if source_type == "fromTarget":
                if source not in TARGETS:
                    fail(f"{target} unknown CMake target: {source}")
            elif source_type == "fromGenerated":
                if target != "macos" or source != "blitz.icns":
                    fail(f"{target} unknown generated resource: {source}")
            elif source_type == "fromSdkStage":
                relative_path(source, "SDK stage source")
            else:
                path = ROOT.joinpath(*relative_path(source, "source").parts)
                if not path.is_file():
                    fail(f"{target} missing source: {source}")

        if target == "linux":
            if config["nfpmVersion"] != "2.48.0" or not re.fullmatch(
                r"[0-9a-f]{64}", config["nfpmArchiveSha256"]
            ):
                fail("Linux nFPM archive pin is incomplete")
            if config["desktopEntry"] not in destinations:
                fail("Linux desktop entry is missing from the manifest")
            desktop = (ROOT / "installer/linux" / f"{app_id}.desktop").read_text(
                encoding="utf-8"
            )
            if f"Exec={config['installPrefix']}/bin/rb_blitz_linux_launcher" not in desktop:
                fail("Linux desktop Exec differs from installPrefix")
        else:
            if config["bundleId"] != app_id or config["packageId"] != app_id:
                fail("macOS bundle/package identifier differs from applicationId")
            component_path = ROOT / config["componentProperties"]
            with component_path.open("rb") as source_file:
                components = plistlib.load(source_file)
            if len(components) != 1 or components[0].get("RootRelativeBundlePath") != config["bundleName"]:
                fail("macOS component bundle path differs from bundleName")
            if components[0].get("BundleIsRelocatable") is not False or components[0].get(
                "BundleHasStrictIdentifier"
            ) is not True or components[0].get("BundleOverwriteAction") != "upgrade":
                fail("macOS component upgrade/relocation policy differs from contract")
            info_text = (ROOT / "installer/macos/Info.plist.in").read_text(encoding="utf-8")
            info = plistlib.loads(info_text.replace("@RBBLITZ_GAME_VERSION@", "0.1.0").encode())
            if info.get("CFBundleIdentifier") != app_id or info.get("CFBundleExecutable") != "rb_blitz_macos_launcher":
                fail("macOS Info.plist identity differs from contract")
            if info.get("LSMinimumSystemVersion") != config["deploymentTargetCandidate"]:
                fail("macOS Info.plist deployment target differs from contract")
            if f"{config['bundleName']}/Contents/Resources/blitz.icns" not in destinations:
                fail("macOS bundle icon is missing from the manifest")

        if args.target == target:
            if config["nativeDependencyInventoryPending"] or config.get("releaseAuditPending") or config.get("iconConversionPending") or config.get("xcodeBuildPinPending"):
                fail(f"{target} stage is blocked until native dependency/tool inputs are pinned")
            stage = args.stage_root.resolve()
            if not stage.is_dir():
                fail(f"stage directory does not exist: {stage}")
            actual = {
                path.relative_to(stage).as_posix()
                for path in stage.rglob("*")
                if path.is_file() or path.is_symlink()
            }
            missing = destinations - actual
            extra = actual - destinations
            if missing or extra:
                fail(f"{target} stage differs from manifest; missing={sorted(missing)}, extra={sorted(extra)}")

    print("platform packaging: metadata valid; native stage audits remain pending")


if __name__ == "__main__":
    main()
