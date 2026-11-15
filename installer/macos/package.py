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
import mmap
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))
from native_assets import controller_entries

CONFIG = ROOT / "installer/config/platform-packaging.json"
MACHO_MAGIC = {b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe",
               b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
FORBIDDEN_EXTENSIONS = {".xex", ".ark", ".hdr", ".con", ".live", ".pirs", ".pdb", ".map", ".dsym",
                        ".spv", ".dxbc", ".dxil", ".pem", ".key"}


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
        "TracyClient": "libTracyClient.dylib",
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
        # Versioned dylibs normally have an SDK-generated unversioned alias.
        # Copy its bytes, never the symlink; only accept targets inside the build.
        if "fromTarget" in entry or "fromSdkStage" in entry:
            if not source.resolve().is_relative_to(build.resolve()):
                fail(f"stage source escapes build tree: {source}")
        elif source.is_symlink():
            fail(f"linked stage source: {source}")
        if not source.is_file():
            fail(f"missing stage source: {source}")
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


def macho_files(app: Path) -> list[Path]:
    result = []
    for path in sorted(app.rglob("*")):
        if path.is_file():
            with path.open("rb") as stream:
                if stream.read(4) in MACHO_MAGIC:
                    result.append(path)
    return result


def audit_preparation(app: Path) -> None:
    """The recipe catalogue may ship; installed shader bytes and native caches may not."""
    macos = app / "Contents/MacOS"
    raw = (macos / "graphics-preparation.json").read_bytes().replace(b"\r\n", b"\n")
    data = json.loads(raw)
    def keys(value: dict, allowed: set[str]) -> None:
        if not isinstance(value, dict) or set(value) - allowed:
            fail("unexpected graphics preparation fields")
    keys(data, {"schema", "title_id", "layouts", "assets", "sources", "paths"})
    if data.get("schema") != 1 or data.get("title_id") != "5841122D":
        fail("invalid graphics preparation identity")
    keys(data["layouts"], {"schema", "title_id", "source_revision", "linker", "scope",
                          "swizzle_masks_offset", "format_sizes_offset", "layouts", "bootstrap_shaders"})
    for layout in data["layouts"]["layouts"]:
        keys(layout, {"name", "declaration_offset", "strides", "fvf"})
    for shader in data["layouts"]["bootstrap_shaders"]:
        keys(shader, {"name", "offset", "bytes", "hash"})
    for asset in data["assets"].values():
        keys(asset, {"bytes"})
    for source in data["sources"]:
        keys(source, {"asset", "entry", "permutation", "offset", "bytes", "vertex_layout", "stage", "hash", "fetches"})
        if (not isinstance(source.get("bytes"), int) or not 0 < source["bytes"] <= 65536 or
                not re.fullmatch(r"[0-9a-f]{16}", source.get("hash", "")) or
                source.get("stage") not in ("pixel", "vertex")):
            fail("invalid source locator; shader bytes must never ship")
    if set(data["paths"]) != {"d3d12/rtv", "d3d12/rov", "vulkan/fbo", "vulkan/fsi"}:
        fail("graphics preparation paths are incomplete")
    for name, recipes in data["paths"].items():
        if not recipes:
            fail(f"empty graphics preparation path: {name}")
        for recipe in recipes:
            keys(recipe, {"hash", "raw"})
            length = 128 if name.startswith("d3d12/") else 120
            if (not re.fullmatch(r"[0-9a-f]{16}", recipe.get("hash", "")) or
                    not re.fullmatch(rf"[0-9a-f]{{{length}}}", recipe.get("raw", ""))):
                fail("invalid portable pipeline recipe")
    with (macos / "rb_blitz").open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as binary:
        if binary.find(raw) < 0:
            fail("game does not embed the staged graphics catalogue; rebuild before packaging")


def load_commands(binary: Path) -> list[tuple[str, str]]:
    """Read actual LC_RPATH, dependency and install-name entries."""
    commands = []
    command = ""
    for line in run("otool", "-l", str(binary)).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line.split()[1]
        elif (command == "LC_RPATH" and line.startswith("path ") or
              command.endswith("_DYLIB") and line.startswith("name ")):
            commands.append((command, line.split(" ", 1)[1].rsplit(" (offset ", 1)[0]))
    return commands


def prepare_binaries(app: Path) -> None:
    """Remove build rpaths/IDs and ad-hoc sign the final Apple Silicon bytes."""
    binaries = macho_files(app)
    libraries = {path.name: path for path in binaries if path.suffix == ".dylib"}
    identities = {value: path for path in binaries for kind, value in load_commands(path)
                  if kind == "LC_ID_DYLIB"}
    for binary in binaries:
        commands = load_commands(binary)
        arguments = []
        for kind, value in commands:
            if kind == "LC_ID_DYLIB":
                arguments += ["-id", f"@rpath/{binary.name}"]
            elif kind == "LC_RPATH":
                arguments += ["-delete_rpath", value]
            elif kind.endswith("_DYLIB") and not value.startswith(("/usr/lib/", "/System/Library/")):
                target = identities.get(value)
                if target is None and value.startswith("@"):
                    target = libraries.get(Path(value).name)
                if target is None:
                    fail(f"unreviewed dependency in {binary.name}: {value}")
                arguments += ["-change", value, f"@rpath/{target.name}"]
        for value in ("@loader_path", "@executable_path", "@executable_path/vulkan/lib"):
            arguments += ["-add_rpath", value]
        run("install_name_tool", *arguments, str(binary))
        run("strip", "-S", str(binary))
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(binary))
    run("codesign", "--force", "--sign", "-", "--timestamp=none", str(app))


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
        if magic in {b"CON ", b"LIVE", b"PIRS", b"XEX2", b"\x7fELF"} or magic[:2] == b"MZ":
            fail(f"unexpected retail or PE signature: {path}")
        # Scan large recompiled executables as well, with overlap between chunks.
        markers = (str(ROOT).encode(), str(Path.home()).encode(), b"/opt/homebrew/", b"/Users/")
        overlap = max(map(len, markers)) - 1
        tail = b""
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                raw = tail + chunk
                if any(marker in raw for marker in markers):
                    fail(f"build-machine path found in bundle file: {path}")
                tail = raw[-overlap:]
    macos = app / "Contents/MacOS"
    required = [macos / "rb_blitz", macos / "rb_blitz_macos_launcher",
             macos / "librexruntime.dylib", macos / "librexgpu-xenos.dylib",
             macos / "libTracyClient.dylib",
             macos / "vulkan/lib/libvulkan.1.dylib",
             macos / "vulkan/lib/libMoltenVK.dylib"]
    macho = macho_files(app)
    resolved_macho = {path.resolve() for path in macho}
    if any(binary not in macho for binary in required):
        fail("required runtime file is missing or is not Mach-O")
    for binary in macho:
        slices = run("lipo", "-archs", str(binary)).split()
        if slices != [arch]:
            fail(f"{binary} does not contain exactly the requested {arch} slice: {slices}")
        build_version = run("xcrun", "vtool", "-show-build", str(binary))
        minimum = re.findall(r"(?m)^\s+minos\s+(\d+\.\d+(?:\.\d+)?)", build_version)
        minimum += re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\d+\.\d+(?:\.\d+)?)",
                              build_version)
        declared = tuple(map(int, config["deploymentTargetCandidate"].split(".")))
        if not minimum or any(tuple(map(int, value.split(".")[:2])) > declared[:2] for value in minimum):
            fail(f"{binary} requires a newer OS than the bundle declares: {minimum}")
        run("codesign", "--verify", "--strict", str(binary))
        commands = load_commands(binary)
        def resolve(value: str) -> Path:
            if value.startswith("@executable_path/"):
                return macos / value.removeprefix("@executable_path/")
            if value == "@executable_path":
                return macos
            if value.startswith("@loader_path/"):
                return binary.parent / value.removeprefix("@loader_path/")
            if value == "@loader_path":
                return binary.parent
            fail(f"unsafe loader path in {binary}: {value}")
        rpaths = [resolve(value) for kind, value in commands if kind == "LC_RPATH"]
        for path in rpaths:
            if not path.resolve().is_relative_to(app.resolve()):
                fail(f"loader path escapes app: {path}")
        for kind, dependency in commands:
            if kind in ("LC_ID_DYLIB", "LC_RPATH"):
                continue
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dependency.startswith("@rpath/"):
                name = dependency.removeprefix("@rpath/")
                candidates = [(path / name).resolve() for path in rpaths]
                if not any(candidate in resolved_macho for candidate in candidates):
                    fail(f"unresolved @rpath dependency in {binary}: {dependency}")
            else:
                resolved = resolve(dependency).resolve()
                if resolved not in resolved_macho:
                    fail(f"unresolved bundle dependency in {binary}: {dependency}")
    run("codesign", "--verify", "--deep", "--strict", str(app))
    expected = {entry["to"] for entry in config["stageManifest"]}
    actual = {path.relative_to(stage_root).as_posix() for path in paths}
    # codesign adds this one bundle resource after the authored stage inventory.
    expected.add(f"{config['bundleName']}/Contents/_CodeSignature/CodeResources")
    if actual != expected:
        fail("app inventory changed after staging")


def compare_expanded_pkg(package: Path, stage_root: Path, temporary: Path, config: dict,
                         version: str) -> None:
    expanded = temporary / "expanded"
    run("pkgutil", "--expand-full", str(package), str(expanded))
    metadata = ET.parse(expanded / "PackageInfo").getroot()
    if (metadata.get("identifier") != config["packageId"] or
            metadata.get("version") != version or
            metadata.get("install-location") != config["installLocation"] or
            metadata.find("scripts") is not None):
        fail("expanded package identity/location/scripts differ from contract")
    payload = expanded / "Payload"
    if any(path.is_symlink() for path in expanded.rglob("*")):
        fail("expanded package contains a symlink")
    if any(path.relative_to(payload).parts[0] != config["bundleName"]
           for path in payload.rglob("*")):
        fail("expanded package contains files outside the application")
    matches = [path for path in expanded.rglob(config["bundleName"]) if path.is_dir()]
    if len(matches) != 1:
        fail(f"expanded package has {len(matches)} app bundles, expected one")
    packaged = matches[0]
    staged = stage_root / config["bundleName"]
    expected = {path.relative_to(staged).as_posix(): (sha256(path), path.stat().st_mode & 0o777)
                for path in staged.rglob("*") if path.is_file()}
    actual = {path.relative_to(packaged).as_posix(): (sha256(path), path.stat().st_mode & 0o777)
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
    parser.add_argument("--development", action="store_true",
                        help="build a local test pkg while gameplay release acceptance is pending")
    args = parser.parse_args()
    if sys.platform != "darwin":
        fail(".app/.pkg assembly requires macOS")
    if platform.machine() != args.arch:
        fail("this route requires a native build for the requested architecture")
    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    config = data["macos"]
    config.update(config.get("nativeValidation", {}).get(args.arch, {}))
    subprocess.run([sys.executable, str(ROOT / "installer/tools/check_platform_packaging.py")], check=True)
    if args.arch not in config["architectures"]:
        fail("architecture is absent from the stage contract")
    config["stageManifest"] += controller_entries(
        ROOT, args.build_dir.resolve() / "rb_blitz_controller_schemes", config["controllerAssets"])
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
        prepare_binaries(app)
        audit_preparation(app)
        audit_app(app, stage_root, args.arch, config)
        print(f"staged and inspected {app}")
        if args.stage_only:
            return
        if any(config[key] for key in ("nativeDependencyInventoryPending", "iconConversionPending",
                                        "xcodeBuildPinPending")):
            fail("native dependency, icon, Xcode, and full release audits must be completed")
        if config["releaseAuditPending"] and not args.development:
            fail("release acceptance is pending; use --development for an audited local test installer")
        pkgbuild = run("xcrun", "--find", "pkgbuild")
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=True)
        suffix = "-development" if args.development else ""
        package = output / f"RockBandBlitz-{version}-mac-{args.arch}{suffix}.pkg"
        if package.exists():
            fail(f"archive the existing package before rebuilding: {package}")
        candidate = temporary / package.name
        run(pkgbuild, "--root", str(stage_root), "--install-location", config["installLocation"],
            "--component-plist", str(ROOT / config["componentProperties"]),
            "--identifier", config["packageId"], "--version", version,
            "--ownership", "recommended", str(candidate))
        compare_expanded_pkg(candidate, stage_root, temporary, config, version)
        shutil.copyfile(candidate, package)
        inventory = {path.relative_to(stage_root).as_posix(): sha256(path)
                     for path in sorted(app.rglob("*")) if path.is_file()}
        report = {"version": version, "architecture": args.arch,
                  "development": args.development, "releaseAcceptancePending": config["releaseAuditPending"],
                  "packageSha256": sha256(package), "files": inventory,
                  "hostOS": run("sw_vers", "-productVersion"),
                  "xcode": run("xcodebuild", "-version"),
                  "appleSDK": run("xcrun", "--show-sdk-version")}
        package.with_suffix(".audit.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{package.name} sha256={sha256(package)}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        fail(f"{error.cmd[0]} exited {error.returncode}: {error.stderr or error.stdout or ''}")
