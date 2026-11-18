"""Prepare and preserve P0 comparisons without launching or driving the game.

Use --help for commands. All generated artifacts stay in ignored out/.
No normal shader cache is cleared. A warm run explicitly copies an earlier cache.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "assets"))
import hmx_ark
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
RUNS = ROOT / "out/shader-cache-p0/runs"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def inventory(folder):
    return {p.relative_to(folder).as_posix(): {"bytes": p.stat().st_size, "sha256": digest(p)}
            for p in sorted(folder.rglob("*")) if p.is_file()}


def runtime_binary_inventory(build):
    """Record configured modules and every shared dependency staged with the host."""
    build = Path(build).resolve()
    platform = "windows" if (build / "rb_blitz.exe").is_file() else "macos" if any(build.glob("*.dylib")) else "linux"
    executable = build / ("rb_blitz.exe" if platform == "windows" else "rb_blitz")
    if not executable.is_file(): raise ValueError(f"missing host executable: {executable}")
    modular = build / ("rexbase.dll" if platform == "windows" else "librexbase.dylib" if platform == "macos" else "librexbase.so")
    if modular.is_file():
        sys.path.insert(0, str(ROOT / "installer/tools"))
        from runtime_inventory import runtime_targets, target_filename
        required = [build / target_filename(name, platform) for name in runtime_targets(build, platform)]
    else:
        # Retain old capture comparisons while using one contract for modular SDKs.
        names = ["rexruntime.dll", "rexgpu-xenos.dll"] if platform == "windows" else [
            "librexruntime.dylib", "librexgpu-xenos.dylib"] if platform == "macos" else [
            "librexruntime.so", "librexgpu-xenos.so"]
        required = [build / name for name in names]
    missing = [path.name for path in required if not path.is_file()]
    if missing: raise ValueError("incomplete staged runtime: "+", ".join(missing))
    paths = {executable, *required}
    candidates = list(build.iterdir())
    for directory in (build / "vulkan", build / "Frameworks"):
        if directory.is_dir(): candidates.extend(directory.rglob("*"))
    for path in candidates:
        framework_binary = any(parent.suffix == ".framework" and path.name == parent.stem
                               for parent in path.parents if parent != build)
        if path.is_file() and (path.suffix.lower() in {".dll", ".dylib"} or
                               re.search(r"\.so(?:\.\d+)*$", path.name) or framework_binary):
            if not path.resolve().is_relative_to(build):
                raise ValueError(f"staged runtime escapes its build directory: {path.name}")
            paths.add(path)
    for path in paths:
        if not path.resolve().is_relative_to(build):
            raise ValueError(f"staged runtime escapes its build directory: {path.name}")
    return {path.relative_to(build).as_posix(): {"sha256": digest(path), "bytes": path.stat().st_size}
            for path in sorted(paths)}


def git(*args, cwd=ROOT):
    return subprocess.check_output(["git", *args], cwd=cwd, text=True, encoding="utf-8")


def run_dir(name):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,79}", name):
        raise ValueError("run name must be a short alphanumeric slug")
    return RUNS / name


def validate_warm_source(previous, binaries, backend, variant, game_sha256):
    if (previous.get("binaries") != binaries or previous.get("backend") != backend or
            previous.get("variant") != variant or previous.get("game_xex_sha256") != game_sha256):
        raise ValueError("warm source must use the same binary bytes, backend, variant and game input")


def capture_arguments(exe, args, run, cache):
    """Build one scalar log-level option while preserving isolated capture roots."""
    extra = list(args.game_arg)
    protected = ("--cache_root", "--user_data_root", "--launcher_profile", "--log_file")
    if any(arg.startswith(protected) for arg in extra):
        raise ValueError("extra arguments cannot override isolated cache/save/profile/log roots")
    levels = [arg for arg in extra if arg.startswith("--log_level")]
    if len(levels) > 1 or any(not re.fullmatch(r"--log_level=[A-Za-z][A-Za-z0-9_-]*", arg) for arg in levels):
        raise ValueError("provide at most one log level as --log_level=VALUE")
    argv = [str(exe), f"--game_data_root={args.game}", f"--user_data_root={run / 'user-data'}",
            f"--launcher_profile={run / 'launcher.toml'}", f"--cache_root={cache}",
            f"--gpu_backend={args.backend}", f"--ultimate_mode={1 if args.variant == 'ultimate' else 0}",
            "--shader_compile_trace=true"]
    if not levels:
        argv.append("--log_level=info")
    argv.append(f"--log_file={run / 'game.log'}")
    return argv + extra


def prepare(args):
    source_root = getattr(args, "source_root", ROOT)
    source_git = lambda *arguments: git(*arguments, cwd=source_root)
    run = run_dir(args.name)
    if run.exists():
        raise ValueError("run already exists; use a new name (nothing is overwritten)")
    exe = args.build_dir / "rb_blitz.exe"
    for path in [exe, args.game / "default.xex", args.user_data, args.profile]:
        if not path.exists():
            raise ValueError(f"missing input: {path}")
    binaries = runtime_binary_inventory(exe.parent)
    game_sha256 = digest(args.game / "default.xex")
    if args.warm_from:
        previous_run = run_dir(args.warm_from)
        previous = json.loads((previous_run / "manifest.json").read_text(encoding="utf-8"))
        validate_warm_source(previous, binaries, args.backend, args.variant, game_sha256)
    if run.is_relative_to(args.user_data):
        raise ValueError("user-data source cannot contain the capture destination")
    # Require a real snapshot of the comparison's saves and settings. Empty saves
    # change the connect flow and must be a separate, explicitly recorded route.
    run.mkdir(parents=True)
    shutil.copytree(args.user_data, run / "user-data",
                    ignore=lambda folder, names: {"cache"} if Path(folder) == args.user_data else set())
    shutil.copy2(args.profile, run / "launcher.toml")
    settings = args.profile.parent / ("settings.toml" if args.profile.name == "launcher.toml"
                                      else args.profile.stem + ".game-settings.toml")
    for suffix in ["", ".graphics-boot.toml"]:
        source = Path(str(settings) + suffix)
        if source.exists():
            shutil.copy2(source, run / ("settings.toml" + suffix))
    cache = run / "cache"
    if args.warm_from:
        previous = run_dir(args.warm_from) / "after-cache"
        if not previous.is_dir():
            raise ValueError("warm source must have a collected after-cache snapshot")
        shutil.copytree(previous, cache)
    else:
        source_cache = args.user_data / "cache"
        if source_cache.is_dir():
            # Empty only the shader layer. Keep DLC enumeration and other cache
            # inputs fixed; disposable authored UI records are rebuilt per PID.
            shutil.copytree(source_cache, cache, ignore=lambda folder, names:
                            {"shaders", "settings-menu"} if Path(folder) == source_cache else set())
        else:
            cache.mkdir()
    shutil.copytree(cache, run / "before-cache")
    inputs = run / "inputs"
    inputs.mkdir()
    shutil.copytree(run / "user-data", inputs / "user-data")
    for name in ["launcher.toml", "settings.toml", "settings.toml.graphics-boot.toml"]:
        if (run / name).exists():
            shutil.copy2(run / name, inputs / name)
    build = exe.parent
    for name in ["rb_blitz.toml", "paths.toml", "settings_dir.txt"]:
        if (build / name).is_file():
            shutil.copy2(build / name, inputs / name)
    (inputs / "project.diff").write_text(source_git("diff", "HEAD"), encoding="utf-8")
    (inputs / "sdk.diff").write_text(source_git("-C", "rexglue-sdk", "diff", "HEAD"), encoding="utf-8")
    # git diff omits untracked implementation files. Keep their exact bytes too.
    changed = set(source_git("diff", "--name-only", "HEAD").splitlines())
    changed.update(source_git("ls-files", "--others", "--exclude-standard", "src", "scripts", "tools",
                              "tests", "patches", "installer").splitlines())
    for name in sorted(changed):
        source = source_root / name
        if source.is_file():
            target = inputs / "source" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    # SDK git diff also omits newly introduced public headers and module sources.
    sdk_untracked = source_git("-C", "rexglue-sdk", "ls-files", "--others", "--exclude-standard",
                               "src", "include", "cmake").splitlines()
    for name in sorted(sdk_untracked):
        source = source_root / "rexglue-sdk" / name
        if source.is_file():
            target = inputs / "sdk-source" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    (inputs / "sdk-submodules.txt").write_text(
        source_git("-C", "rexglue-sdk", "submodule", "status", "--recursive"), encoding="utf-8")
    capture_tools = inputs / "capture-tools"
    capture_tools.mkdir()
    for tool in [Path(__file__), ROOT / "scripts/diagnostics/matched_performance.py",
                 ROOT / "installer/tools/runtime_inventory.py",
                 ROOT / "installer/config/runtime-modules.json"]:
        if tool.is_file(): shutil.copy2(tool, capture_tools / tool.name)
    # Cache and save overrides are explicit; they outrank paths.toml/profile values.
    argv = capture_arguments(exe, args, run, cache)
    headers = {str(p.relative_to(args.game)): digest(p) for p in
               [args.game / "gen/main_xbox.hdr", args.game / "gen/patch_xbox.hdr",
                args.game / "ultimate/gen/patch_xbox.hdr"] if p.is_file()}
    manifest = {"schema": 1, "prepared_utc": datetime.now(timezone.utc).isoformat(),
                "project": source_git("rev-parse", "HEAD").strip(),
                "sdk": source_git("-C", "rexglue-sdk", "rev-parse", "HEAD").strip(),
                "source_root": str(source_root),
                "capture_tools": inventory(capture_tools),
                "binaries": binaries, "argv": argv, "backend": args.backend,
                "variant": args.variant, "route": args.route, "warm_from": args.warm_from,
                "source_user_data": str(args.user_data), "source_profile": str(args.profile),
                "content_headers": headers,
                "game_xex_sha256": game_sha256,
                "before_cache": inventory(cache), "initial_user_data": inventory(run / "user-data")}
    (run / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    # Quote PowerShell strings, including literal apostrophes. This file is inert
    # until the authorized operator runs it; there is no input or auto-A helper.
    quote = lambda s: "'" + s.replace("'", "''") + "'"
    command = "Push-Location -LiteralPath " + quote(str(build)) + "\ntry {\n  & "
    command += " ".join(map(quote, argv)) + "\n} finally { Pop-Location }\n"
    (run / "launch.ps1").write_text(command, encoding="utf-8")
    print(run)
    print("Prepared only. Build before testing; obtain UI authorization before running launch.ps1.")


def collect(args):
    run = run_dir(args.name)
    manifest = json.loads((run / "manifest.json").read_text(encoding="utf-8"))
    if (run / "after-cache").exists():
        raise ValueError("run already collected; snapshots are immutable")
    log_hash = digest(run / "game.log")
    # Operator must close the game normally first, so cache writers have drained.
    shutil.copytree(run / "cache", run / "after-cache")
    manifest["after_cache"] = inventory(run / "after-cache")
    manifest["collected_utc"] = datetime.now(timezone.utc).isoformat()
    manifest["observations"] = args.observations
    manifest["log_sha256"] = log_hash
    (run / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    report_file(run / "game.log", run / "report.json")


def report_file(log, output):
    events, counters, phases = [], {}, []
    damaged = []
    for number, line in enumerate(log.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if "shader_trace:" not in line:
            continue
        try:
            fields = dict(re.findall(r'(\w+)=("(?:\\.|[^"\\])*"|[^\s]+)', line))
            # New logs quote sites; older unquoted call sites remain supported.
            for key, value in fields.items():
                if value.startswith('"'):
                    fields[key] = json.loads(value)
            if "summary snapshot=" in line:
                int(fields.get("suppressed", 0))
                counters[fields["phase"] + "/" + fields["event"]] = fields
            elif "event=" in line:
                thread = re.search(r'\[t(\d+)\]|\bt(\d+)\b(?=\s+shader_trace:)', line)
                fields["thread_id"] = int(thread[1] or thread[2]) if thread else None
                for key in ["begin_us", "end_us", "duration_us"]:
                    fields[key] = int(fields[key])
                if not fields.get("event") or fields["end_us"] < fields["begin_us"]:
                    raise ValueError("invalid event or interval")
                events.append(fields)
            elif "previous" in fields and "phase" in fields:
                phases.append(fields)
        except (ValueError, KeyError) as error:
            damaged.append({"line": number, "reason": str(error)})
    operations = [e for e in events if e["event"] not in {"gpu_frame", "guest_frame"}]
    gaps = []
    for frame in events:
        if frame["event"] not in {"gpu_frame", "guest_frame"}:
            continue
        overlaps = [e for e in operations if e["begin_us"] < frame["end_us"]
                    and e["end_us"] > frame["begin_us"]]
        gaps.append({**frame, "overlapping_work": overlaps,
                     "assessment": "temporal overlap; causation unproven" if overlaps
                     else "unresolved: no recorded operation overlap"})
    result = {"schema": 1, "log_sha256": digest(log), "phases": phases,
              "damaged_trace_lines": damaged,
              "latest_cumulative_counters": counters, "long_frames": gaps,
              "detail_truncated": any(int(c.get("suppressed", 0)) for c in counters.values()),
              "limits": ["summaries are cumulative; do not sum snapshots",
                         "CPU work can overlap across workers; do not sum durations as wall time",
                         "suppressed details and final work after last summary limit attribution",
                         "damaged or partial trace lines are listed; their coverage is unknown",
                         "long guest/file/idle/present/GPU operations retain only CPU spans >=50 ms",
                         "file reads include file-lock and completion work; GPU idle includes yielding and preparation",
                         "nested work and work on other threads are not additive or proof of blocking",
                         "untimed operations and remaining stalls are unresolved"]}
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(output)


def inspect(args):
    """Decode every installed record, retaining only loading/menu references.

    The production C++ parser/transform is used, rather than a second DTB grammar.
    Exported game data is private local evidence and must never be committed.
    """
    destination = ROOT / "out/shader-cache-p0/records" / args.name
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name) or destination.exists():
        raise ValueError("choose a new alphanumeric record-snapshot name")
    destination.mkdir(parents=True)
    header = hmx_ark.load_header(args.hdr)
    parts = header.ark_parts(args.hdr)
    tool = ROOT / "out/build/win-amd64-release/rb_blitz_loading_record_inspect.exe"
    evidence, unsupported, count = [], [], 0
    for entry in header.files:
        if not entry.path.endswith(".dtb"):
            continue
        if entry.size > 4 * 1024 * 1024 or entry.compressed:
            raise ValueError(f"unsupported DTB size/compression: {entry.path}")
        index, offset = header.select_ark(entry.offset)
        with open(parts[index], "rb") as stream:
            stream.seek(offset)
            data = stream.read(entry.size)
        if len(data) != entry.size:
            raise ValueError(f"truncated archive record: {entry.path}")
        # Flat names avoid trusting archive paths as host output paths.
        record = destination / f"record-{count}.dtb"
        record.write_bytes(data)
        argv = [str(tool), str(record)] + (["--offline"] if args.offline else [])
        result = subprocess.run(argv, check=False, capture_output=True, text=True,
                                encoding="utf-8", errors="backslashreplace")
        count += 1
        if result.returncode:
            unsupported.append({"entry": entry.path, "bytes": entry.size,
                                "sha256": digest(record), "error": result.stderr.strip()})
            continue
        text = result.stdout
        if any(token in text for token in ["main_menu", "server_connect_screen", "saveload_mgr",
                                           "start_connect_process", "ulti_settings.dta"]):
            (destination / (record.stem + ".txt")).write_text(text, encoding="utf-8")
            evidence.append({"entry": entry.path, "offset": entry.offset, "bytes": entry.size,
                             "sha256": digest(record), "dump": record.stem + ".txt",
                             "transform": result.stderr.strip()})
        else:
            record.unlink()  # Only this newly written scratch file; never installed data.
    manifest = {"hdr": str(args.hdr), "hdr_sha256": digest(args.hdr), "dtbs_scanned": count,
                "offline_transform_requested": args.offline, "matches": evidence,
                "unsupported_records": unsupported}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("name")
    prep.add_argument("--build-dir", type=lambda p: Path(p).resolve(),
                      default=ROOT / "out/build/win-amd64-release")
    prep.add_argument("--source-root", type=lambda p: Path(p).resolve(), default=ROOT,
                      help="source checkout matching the selected build, including its SDK")
    prep.add_argument("--backend", choices=["d3d12", "vulkan"], required=True)
    prep.add_argument("--variant", choices=["retail", "ultimate"], required=True)
    prep.add_argument("--game", type=lambda p: Path(p).resolve(), default=ROOT / "game")
    prep.add_argument("--user-data", type=lambda p: Path(p).resolve(), required=True)
    prep.add_argument("--profile", type=lambda p: Path(p).resolve(), required=True)
    prep.add_argument("--route", required=True, help="song, loadout, actions, cache/dialog/trial choices")
    prep.add_argument("--warm-from", help="name of a collected earlier run")
    prep.add_argument("--game-arg", action="append", default=[], help="extra CLI flag; use --game-arg=--flag=value")
    prep.set_defaults(func=prepare)
    end = commands.add_parser("collect")
    end.add_argument("name")
    end.add_argument("--observations", required=True, help="replay, freezes, timestamps, notes/effects/audio/input")
    end.set_defaults(func=collect)
    report = commands.add_parser("report")
    report.add_argument("log", type=Path)
    report.add_argument("--output", type=Path, required=True)
    report.set_defaults(func=lambda args: report_file(args.log, args.output))
    records = commands.add_parser("inspect", help="read-only installed loading/menu record evidence")
    records.add_argument("name")
    records.add_argument("--hdr", type=Path, required=True)
    records.add_argument("--offline", action="store_true", help="apply the real R3 transform to local copies")
    records.set_defaults(func=inspect)
    args = parser.parse_args()
    try:
        args.func(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
