#!/usr/bin/env python3
"""Package a bundled-assets or first-launch importer iPhone/iPad IPA."""
from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import hashlib
import json
import mmap
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))
from native_assets import controller_entries

SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")
LOAD_COMMANDS = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"}
GAME_FILES = {"default.xex", "gen/main_xbox.hdr", "gen/main_xbox_0.ark"}
ULTIMATE_FILES = {"gen/patch_xbox.hdr", "gen/patch_xbox_0.ark"}
IMPORTER_IDENTITY = b"RB_BLITZ_IOS_IMPORTER_V1"
VA_ENTITLEMENT = "com.apple.developer.kernel.extended-virtual-addressing"


def fail(message: str) -> None:
    raise ValueError(f"iOS packaging: {message}")


def run(*command: str) -> str:
    return subprocess.run(command, check=True, text=True, capture_output=True).stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_file(root: Path, relative: str) -> Path:
    name = Path(relative)
    if name.is_absolute() or not name.parts or ".." in name.parts:
        fail(f"unsafe source name: {relative}")
    current = root
    if current.is_symlink():
        fail(f"linked source root: {root}")
    for part in name.parts:
        current = current / part
        if current.is_symlink():
            fail(f"linked source: {current}")
    if not current.is_file() or not current.resolve().is_relative_to(root.resolve()):
        fail(f"missing or escaping source: {current}")
    return current


def verify_game(game: Path, fingerprints: Path) -> list[str]:
    with fingerprints.open("rb") as stream:
        records = tomllib.load(stream)["files"]
    if {item["path"] for item in records} != GAME_FILES or len(records) != len(GAME_FILES):
        fail("game fingerprint manifest differs from the three-file runtime contract")
    for item in records:
        path = safe_file(game, item["path"])
        if path.stat().st_size != item["size"] or sha256(path) != item["sha256"]:
            fail(f"unsupported or corrupt game input: {item['path']}")
    return sorted(GAME_FILES)


def verify_ultimate(payload: Path, fingerprints: Path = ROOT / "installer/config/ultimate_fingerprints.toml") -> list[str]:
    with fingerprints.open("rb") as stream:
        records = [item for item in tomllib.load(stream)["files"]
                   if item["role"] in {"ultimate-archive-header", "ultimate-archive-data"}]
    if {item["path"] for item in records} != ULTIMATE_FILES or len(records) != len(ULTIMATE_FILES):
        fail("Ultimate fingerprint manifest differs from the two-file payload contract")
    for item in records:
        path = safe_file(payload, item["path"])
        if path.stat().st_size != item["size"] or sha256(path) != item["sha256"]:
            fail(f"unsupported or corrupt Ultimate input: {item['path']}")
    return sorted(ULTIMATE_FILES)


def stage_ultimate(app: Path, game: Path,
                   fingerprints: Path = ROOT / "installer/config/ultimate_fingerprints.toml") -> None:
    # Bundle only the supported data pair. The mod's XEX must never replace the
    # owner's verified retail entrypoint; runtime hooks apply its required delta.
    for name in verify_ultimate(game / "ultimate", fingerprints):
        copy_file(safe_file(game / "ultimate", name), app / "game/ultimate" / name)
    verify_ultimate(app / "game/ultimate", fingerprints)
    # Ultimate's authored menu offers Save Screenshot only if this guest folder
    # exists. Actual captures are redirected to Documents/Pictures by the host.
    placeholder = app / "game/ultimate/screenshots/_keepme"
    placeholder.parent.mkdir(parents=True, exist_ok=True)
    placeholder.write_bytes(b"zxc")


def load_commands(binary: Path) -> list[tuple[str, str]]:
    commands = []
    kind = ""
    for line in run("xcrun", "otool", "-l", str(binary)).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            kind = line.split()[1]
        elif ((kind == "LC_RPATH" and line.startswith("path ")) or
              ((kind in LOAD_COMMANDS or kind == "LC_ID_DYLIB") and line.startswith("name "))):
            commands.append((kind, line.split(" ", 1)[1].rsplit(" (offset ", 1)[0]))
    return commands


def version_tuple(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", value):
        fail(f"invalid version: {value}")
    return tuple((list(map(int, value.split("."))) + [0])[:3])


def audit_device_binary(binary: Path, minimum: str) -> None:
    if run("xcrun", "lipo", "-archs", str(binary)).split() != ["arm64"]:
        fail(f"expected a single arm64 device image: {binary.name}")
    build = run("xcrun", "vtool", "-show-build", str(binary))
    platforms = re.findall(r"(?m)^\s*platform\s+(\S+)", build)
    minimums = re.findall(r"(?m)^\s*minos\s+(\S+)", build)
    if platforms != ["IOS"] or len(minimums) != 1:
        fail(f"expected an iPhoneOS image, not macOS or a simulator: {binary.name}")
    if version_tuple(minimums[0]) > version_tuple(minimum):
        fail(f"{binary.name} requires iOS {minimums[0]}, newer than declared {minimum}")


def find_library(name: str, roots: list[Path]) -> Path:
    if Path(name).name != name or not name.endswith(".dylib"):
        fail(f"unsupported bundled dependency: {name}")
    candidates = set()
    for root in roots:
        for path in root.rglob(name):
            resolved = path.resolve()
            if not resolved.is_relative_to(root.resolve()):
                fail(f"library link escapes allowed build tree: {path}")
            if resolved.is_file():
                candidates.add(resolved)
    if not candidates:
        fail(f"missing device library: {name}")
    if len({sha256(path) for path in candidates}) != 1:
        fail(f"ambiguous device library: {name}; use a clean build tree")
    return sorted(candidates)[0]


def collect_libraries(build: Path, roots: list[Path], minimum: str) -> dict[str, Path]:
    libraries = {}
    pending = ["librexruntime.dylib", "librexgpu-xenos.dylib", "libMoltenVK.dylib"]
    binaries = [build / "rb_blitz"]
    while pending or binaries:
        if pending:
            name = pending.pop()
            if name in libraries:
                continue
            binary = find_library(name, roots)
            libraries[name] = binary
        else:
            binary = binaries.pop()
        audit_device_binary(binary, minimum)
        for kind, dependency in load_commands(binary):
            if kind not in LOAD_COMMANDS or dependency.startswith(SYSTEM_PREFIXES):
                continue
            if dependency.startswith("/") and not any(
                    Path(dependency).resolve().is_relative_to(root.resolve()) for root in roots):
                fail(f"dependency is outside approved library directories: {dependency}")
            if not dependency.startswith(("/", "@rpath/", "@loader_path/", "@executable_path/")):
                fail(f"unrecognized dependency: {dependency}")
            pending.append(Path(dependency).name)
    return libraries


def framework_path(library: str) -> str:
    stem = library.removeprefix("lib").removesuffix(".dylib").replace("-", "_").replace(".", "_")
    if not re.fullmatch(r"[A-Za-z0-9_]+", stem):
        fail(f"invalid framework name: {library}")
    return f"{stem}.framework/{stem}"


def stage_framework(app: Path, source: Path, library: str, bundle_id: str,
                    version: str, minimum: str) -> None:
    binary = app / "Frameworks" / framework_path(library)
    copy_file(source, binary, executable=True)
    info = {"CFBundleExecutable": binary.name, "CFBundleIdentifier": f"{bundle_id}.{binary.name.replace('_', '-')}",
            "CFBundleInfoDictionaryVersion": "6.0", "CFBundlePackageType": "FMWK",
            "CFBundleName": binary.name, "CFBundleVersion": version,
            "CFBundleShortVersionString": version, "MinimumOSVersion": minimum,
            "CFBundleSupportedPlatforms": ["iPhoneOS"]}
    (binary.parent / "Info.plist").write_bytes(plistlib.dumps(info))


def prepare_binaries(app: Path, minimum: str, libraries: list[str]) -> list[Path]:
    relative = {name: framework_path(name) for name in libraries}
    if len(set(relative.values())) != len(relative):
        fail("library names collide after framework conversion")
    binaries = [app / "rb_blitz", *(app / "Frameworks" / value for value in relative.values())]
    for binary in binaries:
        # A final signer must sign the changed bytes. An unsigned IPA is explicitly
        # an input for a sideloading signer, never claimed to be installable as-is.
        signed = subprocess.run(["codesign", "-d", str(binary)], capture_output=True)
        if signed.returncode == 0:
            run("codesign", "--remove-signature", str(binary))
        args = []
        framework_rpath = "@executable_path/Frameworks"
        has_framework_rpath = False
        for kind, value in load_commands(binary):
            if kind == "LC_ID_DYLIB":
                args.extend(["-id", f"@rpath/{binary.parent.name}/{binary.name}"])
            elif kind == "LC_RPATH":
                if value == framework_rpath:
                    has_framework_rpath = True
                else:
                    args.extend(["-delete_rpath", value])
            elif kind in LOAD_COMMANDS and not value.startswith(SYSTEM_PREFIXES):
                if Path(value).name not in relative:
                    fail(f"unresolved bundled dependency: {value}")
                args.extend(["-change", value, f"@rpath/{relative[Path(value).name]}"])
        if not has_framework_rpath:
            args.extend(["-add_rpath", framework_rpath])
        run("xcrun", "install_name_tool", *args, str(binary))
        audit_device_binary(binary, minimum)
        for kind, value in load_commands(binary):
            if kind == "LC_RPATH" and value != "@executable_path/Frameworks":
                fail(f"unexpected final rpath in {binary.name}: {value}")
            if kind in LOAD_COMMANDS and not value.startswith(SYSTEM_PREFIXES):
                if value not in {f"@rpath/{path}" for path in relative.values()}:
                    fail(f"unresolved final dependency in {binary.name}: {value}")
    return binaries


def signing_entitlements(profile: dict, bundle_id: str, now: dt.datetime | None = None) -> dict:
    now = now or dt.datetime.now(dt.timezone.utc)
    expiry = profile.get("ExpirationDate")
    if not isinstance(expiry, dt.datetime) or expiry.replace(tzinfo=dt.timezone.utc) <= now:
        fail("provisioning profile is expired or has no expiry")
    if "iOS" not in profile.get("Platform", []):
        fail("provisioning profile is not for iOS")
    if not profile.get("ProvisionedDevices") and not profile.get("ProvisionsAllDevices"):
        fail("profile cannot sideload to devices; use a development or ad hoc profile")
    teams = profile.get("TeamIdentifier", [])
    if len(teams) != 1:
        fail("provisioning profile must identify exactly one team")
    allowed = profile.get("Entitlements", {})
    prefixes = profile.get("ApplicationIdentifierPrefix", teams)
    if len(prefixes) != 1:
        fail("provisioning profile must identify exactly one application prefix")
    application = f"{prefixes[0]}.{bundle_id}"
    if not fnmatch.fnmatchcase(application, allowed.get("application-identifier", "")):
        fail("bundle identifier does not match the provisioning profile")
    if allowed.get(VA_ENTITLEMENT) is not True:
        fail(f"provisioning profile must permit {VA_ENTITLEMENT}; the guest needs extended address space")
    return {"application-identifier": application,
            "com.apple.developer.team-identifier": teams[0],
            "get-task-allow": bool(allowed.get("get-task-allow", False)),
            VA_ENTITLEMENT: True}


def sign_app(app: Path, binaries: list[Path], identity: str, profile_path: Path,
             bundle_id: str, temporary: Path) -> None:
    decoded = subprocess.run(["security", "cms", "-D", "-i", str(profile_path)],
                             check=True, capture_output=True).stdout
    profile = plistlib.loads(decoded)
    entitlements = signing_entitlements(profile, bundle_id)
    entitlement_path = temporary / "entitlements.plist"
    entitlement_path.write_bytes(plistlib.dumps(entitlements))
    shutil.copyfile(profile_path, app / "embedded.mobileprovision")
    for binary in binaries[1:]:
        run("codesign", "--force", "--sign", identity, "--timestamp=none", str(binary.parent))
    run("codesign", "--force", "--sign", identity, "--timestamp=none",
        "--entitlements", str(entitlement_path), str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    certificate = temporary / "signer-"
    run("codesign", "-d", "--extract-certificates", str(certificate), str(app))
    if certificate.with_name(certificate.name + "0").read_bytes() not in profile.get("DeveloperCertificates", []):
        fail("signing certificate is not permitted by the provisioning profile")


def copy_file(source: Path, target: Path, executable: bool = False) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(0o755 if executable else 0o644)


def stage_game_resources(app: Path, game: Path, mode: str) -> None:
    if mode == "importer":
        return
    if mode != "bundled":
        fail(f"unknown asset mode: {mode}")
    fingerprints = ROOT / "config/game_fingerprints.toml"
    verify_ultimate(game / "ultimate")
    for name in verify_game(game, fingerprints):
        copy_file(safe_file(game, name), app / "game" / name)
    verify_game(app / "game", fingerprints)
    stage_ultimate(app, game)


def stage_resources(app: Path, build: Path, game: Path, validator: Path, mode: str = "bundled") -> None:
    stage_game_resources(app, game, mode)
    resources = [("config/game_fingerprints.toml", "game-fingerprints.toml"),
                 ("installer/config/ultimate_fingerprints.toml", "ultimate-fingerprints.toml"),
                 ("config/graphics_preparation.json", "graphics-preparation.json"),
                 ("assets/messages/feed.json", "assets/messages/feed.json"),
                 ("LICENSE", "LICENSE")]
    manifest = json.loads((ROOT / "installer/config/platform-packaging.json").read_text())
    for entry in manifest["macos"]["stageManifest"]:
        relative = entry["to"].split("Contents/Resources/", 1)[-1]
        if relative.startswith("licenses/") and "from" in entry:
            resources.append((entry["from"], relative))
    for entry in controller_entries(ROOT, validator, "assets/controller"):
        resources.append((entry["from"], entry["to"]))
    for source, destination in resources:
        copy_file(safe_file(ROOT, source), app / destination)
    for name in ("cereal", "spirv-cross"):
        copy_file(safe_file(build, f"licenses/{name}-LICENSE"), app / f"licenses/{name}/LICENSE")


def audit_preparation(app: Path) -> None:
    raw = (app / "graphics-preparation.json").read_bytes().replace(b"\r\n", b"\n")
    data = json.loads(raw)
    if data.get("schema") != 1 or data.get("title_id") != "5841122D":
        fail("graphics preparation catalogue has the wrong identity")
    with (app / "rb_blitz").open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as image:
        if image.find(raw) < 0:
            fail("game does not embed the staged graphics catalogue; rebuild before packaging")


def audit_asset_mode(app: Path, mode: str) -> None:
    # Refuse the wrong executable: an asset-free IPA without a linked importer
    # would install successfully but have no way to reach the game.
    with (app / "rb_blitz").open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as binary:
        has_importer = binary.find(IMPORTER_IDENTITY) >= 0
    if has_importer != (mode == "importer"):
        fail("executable does not match the requested asset mode; use the corresponding build preset")
    if mode == "importer":
        for path in app.rglob("*"):
            if path.relative_to(app).parts[0] == "game" or path.suffix.lower() in {".xex", ".ark", ".hdr"}:
                fail(f"retail/mod asset in importer IPA: {path.relative_to(app)}")


def write_info(app: Path, bundle_id: str, version: str, minimum: str, mode: str = "bundled") -> None:
    icons = []
    for pixels in (120, 152, 167, 180):
        name = f"BlitzIcon{pixels}.png"
        run("sips", "-z", str(pixels), str(pixels), str(ROOT / "assets/icon/blitz-apple.png"),
            "--out", str(app / name))
        icons.append(name)
    icon_data = {"CFBundlePrimaryIcon": {"CFBundleIconFiles": icons,
                                         "UIPrerenderedIcon": False}}
    info = {"CFBundleDevelopmentRegion": "en", "CFBundleDisplayName": "Rock Band Blitz",
            "CFBundleName": "Rock Band Blitz", "CFBundleExecutable": "rb_blitz",
            "CFBundleIdentifier": bundle_id, "CFBundleInfoDictionaryVersion": "6.0",
            "RBBlitzAssetMode": mode,
            "CFBundlePackageType": "APPL", "CFBundleShortVersionString": version,
            "CFBundleVersion": version, "MinimumOSVersion": minimum,
            "CFBundleSupportedPlatforms": ["iPhoneOS"], "LSRequiresIPhoneOS": True,
            "UIDeviceFamily": [1, 2], "UIRequiresFullScreen": True,
            "UIFileSharingEnabled": True, "LSSupportsOpeningDocumentsInPlace": True,
            "UIRequiredDeviceCapabilities": ["arm64", "metal"],
            "NSMotionUsageDescription": "Rapid shakes deploy your ready power-up when Shake to Deploy is on.",
            "UISupportedInterfaceOrientations": ["UIInterfaceOrientationPortrait",
                                                   "UIInterfaceOrientationLandscapeLeft",
                                                   "UIInterfaceOrientationLandscapeRight"],
            "UIViewControllerBasedStatusBarAppearance": False, "UIStatusBarHidden": True,
            "UILaunchScreen": {}, "CFBundleIcons": icon_data, "CFBundleIcons~ipad": icon_data}
    (app / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_XML))
    (app / "PkgInfo").write_bytes(b"APPL????")


def archive_ipa(payload: Path, output: Path) -> None:
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(payload.rglob("*")):
            if path.is_symlink():
                fail(f"unexpected bundle symlink: {path}")
            if path.is_file():
                archive.write(path, path.relative_to(payload.parent).as_posix())
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            fail("IPA failed archive integrity verification")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("bundled", "importer"), default="bundled")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "out/ios/packages")
    parser.add_argument("--controller-validator", type=Path, required=True,
                        help="native macOS validator executable, never the cross-compiled one")
    parser.add_argument("--library-dir", type=Path, action="append", default=[])
    parser.add_argument("--bundle-id")
    parser.add_argument("--minimum-ios", default="17.0")
    parser.add_argument("--sign-identity")
    parser.add_argument("--provisioning-profile", type=Path)
    args = parser.parse_args()
    args.bundle_id = args.bundle_id or ("io.github.gabrielofavero.rbblitz" +
                                        (".importer" if args.mode == "importer" else ""))
    if bool(args.sign_identity) != bool(args.provisioning_profile):
        parser.error("--sign-identity and --provisioning-profile must be supplied together")
    if not re.fullmatch(r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", args.bundle_id):
        parser.error("--bundle-id must be a reverse DNS identifier")
    if args.sign_identity == "-":
        parser.error("ad hoc code signing cannot produce a device-installable IPA")
    version_tuple(args.minimum_ios)
    build = args.build_dir.resolve()
    roots = [build, *(path.resolve() for path in args.library_dir)]
    with (ROOT / "installer/config/pins.toml").open("rb") as stream:
        version = tomllib.load(stream)["installer"]["version"]
    version_tuple(version)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suffix = "-importer" if args.mode == "importer" else ""
    destination = output / f"RockBandBlitz-{version}-arm64-ios{suffix}.ipa"
    if destination.exists():
        fail(f"archive the existing IPA before rebuilding: {destination}")
    libraries = collect_libraries(build, roots, args.minimum_ios)
    with tempfile.TemporaryDirectory(prefix="rb-blitz-ipa-", dir=output) as directory:
        temporary = Path(directory)
        payload = temporary / "Payload"
        app = payload / "Rock Band Blitz.app"
        copy_file(build / "rb_blitz", app / "rb_blitz", executable=True)
        for name, source in libraries.items():
            stage_framework(app, source, name, args.bundle_id, version, args.minimum_ios)
        stage_resources(app, build, args.game_dir.absolute(), args.controller_validator.resolve(), args.mode)
        audit_preparation(app)
        audit_asset_mode(app, args.mode)
        write_info(app, args.bundle_id, version, args.minimum_ios, args.mode)
        binaries = prepare_binaries(app, args.minimum_ios, list(libraries))
        if args.sign_identity:
            sign_app(app, binaries, args.sign_identity, args.provisioning_profile.resolve(),
                     args.bundle_id, temporary)
        candidate = temporary / destination.name
        archive_ipa(payload, candidate)
        report = {"version": version, "platform": "iOS", "architecture": "arm64",
                  "minimumOS": args.minimum_ios, "bundleId": args.bundle_id,
                  "signed": bool(args.sign_identity), "assetMode": args.mode,
                  "containsPrivateGameData": args.mode == "bundled",
                  "deviceGameplayVerified": False, "includesUltimate": args.mode == "bundled",
                  "defaultGameVersion": "selectedDuringImport" if args.mode == "importer" else "ultimate",
                  "sha256": sha256(candidate),
                  "files": {path.relative_to(app).as_posix(): sha256(path)
                            for path in sorted(app.rglob("*")) if path.is_file()}}
        candidate.replace(destination)
        destination.with_suffix(".audit.json").write_text(json.dumps(report, indent=2) + "\n")
        if not args.sign_identity:
            destination.with_suffix(".entitlements.plist").write_bytes(plistlib.dumps({VA_ENTITLEMENT: True}))
    print(destination)
    if not args.sign_identity:
        print("Unsigned IPA: re-sign for your device with a profile permitting extended virtual addressing.")
    if args.mode == "bundled":
        print("Contains your private game assets; keep this IPA private. Device gameplay remains unverified.")
    else:
        print("Importer IPA: no retail/Ultimate assets bundled. Select your game on first launch.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"{error}: {error.stderr or error.stdout or ''}") from error
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from error
