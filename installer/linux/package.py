"""Stage and package the native Linux build from the reviewed file manifest.

Run on a native Linux build host. No package manager script imports game data;
the installed desktop launcher does that later as the logged-in user.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "installer/config/platform-packaging.json"


def fail(message: str) -> None:
    raise SystemExit(f"Linux packaging: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def target_path(build: Path, name: str) -> Path:
    filenames = {
        "rb_blitz": "rb_blitz",
        "rb_blitz_linux_launcher": "rb_blitz_linux_launcher",
        "rexruntime": "librexruntime.so",
        "rexgpu-xenos": "librexgpu-xenos.so",
    }
    return build / filenames[name]


def stage(build: Path, destination: Path, config: dict) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for entry in config["stageManifest"]:
        relative = Path(entry["to"])
        if relative.is_absolute() or ".." in relative.parts:
            fail(f"unsafe destination {relative}")
        if "fromTarget" in entry:
            source = target_path(build, entry["fromTarget"])
        else:
            source = ROOT / entry["from"]
        if not source.is_file() or source.is_symlink():
            fail(f"missing or linked stage source: {source}")
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, output)
        output.chmod(0o755 if "fromTarget" in entry else 0o644)
    actual = {path.relative_to(destination).as_posix() for path in destination.rglob("*") if path.is_file()}
    expected = {entry["to"] for entry in config["stageManifest"]}
    if actual != expected:
        fail(f"stage differs from manifest; missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    for name in ("rb_blitz", "rb_blitz_linux_launcher", "librexruntime.so", "librexgpu-xenos.so"):
        binary = destination / "opt/rock-band-blitz-recomp/bin" / name
        if binary.open("rb").read(4) != b"\x7fELF":
            fail(f"not an ELF binary: {binary}")
    forbidden = (b"CON ", b"LIVE", b"PIRS", b"XEX2", b"MZ")
    for path in destination.rglob("*"):
        if path.is_file() and path.suffix.lower() in {".xex", ".ark", ".hdr", ".pdb", ".map"}:
            fail(f"forbidden payload extension: {path}")
        if path.is_file() and path.name not in {"rb_blitz", "rb_blitz_linux_launcher", "librexruntime.so", "librexgpu-xenos.so"}:
            if path.open("rb").read(4) in forbidden:
                fail(f"retail/archive/executable signature in staged resource: {path}")


def nfpm_from_archive(archive: Path, expected: str, temporary: Path) -> Path:
    if not archive.is_file() or sha256(archive) != expected:
        fail("nFPM archive is missing or differs from the pinned SHA-256")
    with tarfile.open(archive, "r:gz") as bundle:
        members = [member for member in bundle if Path(member.name).name == "nfpm" and member.isfile()]
        if len(members) != 1:
            fail("pinned nFPM archive has no unique executable")
        source = bundle.extractfile(members[0])
        if source is None:
            fail("cannot read nFPM executable")
        executable = temporary / "nfpm"
        with executable.open("wb") as output:
            shutil.copyfileobj(source, output)
        executable.chmod(0o755)
    return executable


def yaml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def package(stage_root: Path, out: Path, nfpm: Path, version: str, config: dict) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for kind in ("deb", "rpm"):
        dependencies = config.get("dependencies", {}).get(kind)
        if not dependencies:
            fail(f"native dependency inventory for {kind} is incomplete")
        with tempfile.TemporaryDirectory(prefix="rb-blitz-nfpm-") as temp_name:
            recipe = Path(temp_name) / "nfpm.yaml"
            lines = [
                f"name: {yaml_string(config['packageName'])}",
                f"arch: {yaml_string(config['architecture'])}",
                "platform: linux",
                f"version: {yaml_string(version)}",
                "section: games",
                "priority: optional",
                "license: GPL-2.0-only",
                "description: Recompiled Rock Band Blitz runtime; requires user-supplied game data",
                "maintainer: Rock Band Blitz Recomp contributors",
                "depends:",
                *[f"  - {yaml_string(dependency)}" for dependency in dependencies],
                "contents:",
            ]
            for entry in config["stageManifest"]:
                source = (stage_root / entry["to"]).resolve()
                lines.extend([f"  - src: {yaml_string(str(source))}",
                              f"    dst: {yaml_string('/' + entry['to'])}"])
            recipe.write_text("\n".join(lines) + "\n", encoding="utf-8")
            artifact = out / f"RockBandBlitz-{version}-linux-amd64.{kind}"
            subprocess.run([str(nfpm), "pack", "--packager", kind, "--config", str(recipe),
                            "--target", str(artifact)], check=True)
            print(f"{artifact.name} sha256={sha256(artifact)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--nfpm-archive", type=Path)
    parser.add_argument("--stage-only", action="store_true")
    args = parser.parse_args()
    if sys.platform != "linux":
        fail("stage and package generation require a native Linux host")
    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    config = data["linux"]
    subprocess.run([sys.executable, str(ROOT / "installer/tools/check_platform_packaging.py")], check=True)
    if not args.stage_only and (config["nativeDependencyInventoryPending"] or
                                config["releaseAuditPending"]):
        fail("native dependency inventory and full release audit must be completed")
    with (ROOT / "installer/config/pins.toml").open("rb") as stream:
        version = tomllib.load(stream)["installer"]["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        fail("installer version is not a package-safe semantic version")
    stage_root = ROOT / "out/linux-package/stage"
    if stage_root.exists():
        shutil.rmtree(stage_root)
    stage(args.build_dir.resolve(), stage_root, config)
    print(f"staged {len(config['stageManifest'])} reviewed files in {stage_root}")
    if args.stage_only:
        return
    subprocess.run([sys.executable, str(ROOT / "installer/tools/check_platform_packaging.py"),
                    "--target", "linux", "--stage-root", str(stage_root)], check=True)
    if not args.nfpm_archive:
        fail("pass the pinned nFPM archive with --nfpm-archive")
    with tempfile.TemporaryDirectory(prefix="rb-blitz-nfpm-") as temp_name:
        binary = nfpm_from_archive(args.nfpm_archive, config["nfpmArchiveSha256"], Path(temp_name))
        package(stage_root, ROOT / "out/linux-package/dist", binary, version, config)


if __name__ == "__main__":
    main()
