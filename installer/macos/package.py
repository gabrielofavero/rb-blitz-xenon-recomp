#!/usr/bin/env python3
"""Stage the ReXGlue macOS app and build one native-architecture unsigned pkg.

The stage manifest is reviewed in installer/config/platform-packaging.json.
Package creation stays gated until native dependency, toolchain, icon, and
full-release audits are recorded on a real Mac.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "installer/config/platform-packaging.json"
MACHO_MAGIC = {b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe",
               b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
FORBIDDEN_EXTENSIONS = {".xex", ".ark", ".hdr", ".con", ".live", ".pirs", ".pdb", ".map", ".dsym"}


def fail(message: str) -> None:
    raise SystemExit(f"macOS packaging: {message}")


def run(*command: str) -> str:
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    return result.stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def create_icon(destination: Path, temporary: Path) -> None:
    source = ROOT / "assets/icon/blitz.png"
    if not source.is_file():
        fail("authored PNG icon is missing")
    iconset = temporary / "RockBandBlitz.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            suffix = "@2x" if scale == 2 else ""
            output = iconset / f"icon_{size}x{size}{suffix}.png"
            run("sips", "-z", str(size * scale), str(size * scale), str(source),
                "--out", str(output))
    run("iconutil", "-c", "icns", str(iconset), "-o", str(destination))


def target_path(build: Path, name: str) -> Path:
    names = {
        "rb_blitz": "rb_blitz",
        "rb_blitz_macos_launcher": "rb_blitz_macos_launcher",
        "rexruntime": "librexruntime.dylib",
        "rexgpu-xenos": "librexgpu-xenos.dylib",
    }
    return build / names[name]


def stage(build: Path, stage_root: Path, config: dict, version: str, temporary: Path) -> Path:
    app = stage_root / config["bundleName"]
    for entry in config["stageManifest"]:
        relative = Path(entry["to"])
        if relative.is_absolute() or ".." in relative.parts:
            fail(f"unsafe stage destination: {relative}")
        destination = stage_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if "fromGenerated" in entry:
            create_icon(destination, temporary)
            continue
        if "fromTarget" in entry:
            source = target_path(build, entry["fromTarget"])
        elif "fromSdkStage" in entry:
            source = build / entry["fromSdkStage"]
        else:
            source = ROOT / entry.get("from", entry.get("fromTemplate"))
        if not source.is_file() or source.is_symlink():
            fail(f"missing or linked stage source: {source}")
        if "fromTemplate" in entry:
            template = source.read_text(encoding="utf-8")
            if template.count("@RBBLITZ_GAME_VERSION@") != 2:
                fail("Info.plist version template has changed")
            destination.write_text(template.replace("@RBBLITZ_GAME_VERSION@", version), encoding="utf-8")
        else:
            shutil.copyfile(source, destination)
        destination.chmod(0o755 if "fromTarget" in entry else 0o644)
    expected = {entry["to"] for entry in config["stageManifest"]}
    actual = {path.relative_to(stage_root).as_posix() for path in stage_root.rglob("*")
              if path.is_file() or path.is_symlink()}
    if actual != expected:
        fail(f"stage differs from manifest; missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if (info.get("CFBundleExecutable") != "rb_blitz_macos_launcher" or
            info.get("CFBundleShortVersionString") != version or
            info.get("CFBundleIdentifier") != config["bundleId"]):
        fail("staged app identity differs from package metadata")
    return app


def audit_app(app: Path, stage_root: Path, arch: str, config: dict) -> None:
    paths = [path for path in app.rglob("*") if path.is_file()]
    for path in app.rglob("*"):
        if path.is_symlink():
            fail(f"unexpected bundle symlink: {path}")
    for path in paths:
        if path.suffix.lower() in FORBIDDEN_EXTENSIONS:
            fail(f"forbidden bundle file type: {path}")
        with path.open("rb") as stream:
            magic = stream.read(4)
        if magic in {b"CON ", b"LIVE", b"PIRS", b"XEX2", b"MZ"}:
            fail(f"unexpected retail or PE signature: {path}")
        if path.stat().st_size <= 100 * 1024 * 1024:
            raw = path.read_bytes()
            if str(ROOT).encode() in raw or str(Path.home()).encode() in raw:
                fail(f"build-machine path found in bundle file: {path}")
    macos = app / "Contents/MacOS"
    macho = [macos / "rb_blitz", macos / "rb_blitz_macos_launcher",
             macos / "librexruntime.dylib", macos / "librexgpu-xenos.dylib",
             macos / "vulkan/lib/libvulkan.1.dylib",
             macos / "vulkan/lib/libMoltenVK.dylib"]
    for binary in macho:
        if binary.open("rb").read(4) not in MACHO_MAGIC:
            fail(f"not a Mach-O binary: {binary}")
        slices = run("lipo", "-archs", str(binary)).split()
        if arch not in slices:
            fail(f"{binary} lacks the {arch} slice: {slices}")
        output = run("otool", "-L", str(binary)).splitlines()[1:]
        for line in output:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dependency.startswith("@executable_path/"):
                resolved = macos / dependency.removeprefix("@executable_path/")
            elif dependency.startswith("@loader_path/"):
                resolved = binary.parent / dependency.removeprefix("@loader_path/")
            elif dependency.startswith("@rpath/"):
                name = dependency.removeprefix("@rpath/")
                candidates = [binary.parent / name, macos / name, macos / "vulkan/lib" / name]
                if not any(candidate.is_file() for candidate in candidates):
                    fail(f"unresolved @rpath dependency in {binary}: {dependency}")
                continue
            else:
                fail(f"developer or unbundled dependency in {binary}: {dependency}")
            if not resolved.is_file():
                fail(f"unresolved bundle dependency in {binary}: {dependency}")
    expected = {entry["to"] for entry in config["stageManifest"]}
    actual = {path.relative_to(stage_root).as_posix() for path in paths}
    if actual != expected:
        fail("app inventory changed after staging")


def compare_expanded_pkg(package: Path, stage_root: Path, temporary: Path, config: dict) -> None:
    expanded = temporary / "expanded"
    run("pkgutil", "--expand-full", str(package), str(expanded))
    matches = [path for path in expanded.rglob(config["bundleName"]) if path.is_dir()]
    if len(matches) != 1:
        fail(f"expanded package has {len(matches)} app bundles, expected one")
    packaged = matches[0]
    staged = stage_root / config["bundleName"]
    expected = {path.relative_to(staged).as_posix(): sha256(path)
                for path in staged.rglob("*") if path.is_file()}
    actual = {path.relative_to(packaged).as_posix(): sha256(path)
              for path in packaged.rglob("*") if path.is_file()}
    if actual != expected:
        fail(f"expanded package differs from app; missing={sorted(expected.keys() - actual.keys())}, "
             f"extra={sorted(actual.keys() - expected.keys())}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("arm64", "x86_64"), required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "out/macos/packages")
    parser.add_argument("--stage-only", action="store_true")
    args = parser.parse_args()
    if sys.platform != "darwin":
        fail(".app/.pkg assembly requires macOS")
    if platform.machine() != args.arch:
        fail("this route requires a native build for the requested architecture")
    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    config = data["macos"]
    subprocess.run([sys.executable, str(ROOT / "installer/tools/check_platform_packaging.py")], check=True)
    if args.arch not in config["architectures"]:
        fail("architecture is absent from the stage contract")
    with (ROOT / "installer/config/pins.toml").open("rb") as stream:
        version = tomllib.load(stream)["installer"]["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        fail("package version is invalid")
    stage_root = ROOT / "out/macos/stage" / args.arch
    if stage_root.exists():
        shutil.rmtree(stage_root)
    with tempfile.TemporaryDirectory(prefix="rb-blitz-macos-") as temp_name:
        temporary = Path(temp_name)
        app = stage(args.build_dir.resolve(), stage_root, config, version, temporary)
        audit_app(app, stage_root, args.arch, config)
        print(f"staged and inspected {app}")
        if args.stage_only:
            return
        if any(config[key] for key in ("nativeDependencyInventoryPending", "iconConversionPending",
                                        "xcodeBuildPinPending", "releaseAuditPending")):
            fail("native dependency, icon, Xcode, and full release audits must be completed")
        subprocess.run([sys.executable, str(ROOT / "installer/tools/check_platform_packaging.py"),
                        "--target", "macos", "--stage-root", str(stage_root)], check=True)
        pkgbuild = run("xcrun", "--find", "pkgbuild")
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=True)
        package = output / f"RockBandBlitz-{version}-mac-{args.arch}.pkg"
        if package.exists():
            fail(f"archive the existing package before rebuilding: {package}")
        run(pkgbuild, "--root", str(stage_root), "--install-location", config["installLocation"],
            "--component-plist", str(ROOT / config["componentProperties"]),
            "--identifier", config["packageId"], "--version", version, str(package))
        compare_expanded_pkg(package, stage_root, temporary, config)
        print(f"{package.name} sha256={sha256(package)}")


if __name__ == "__main__":
    main()
