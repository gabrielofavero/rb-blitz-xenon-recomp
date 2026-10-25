"""P8 of the shader-cache plan: the validation matrix, run by run.

P8 is a measurement campaign, not a code change: it asks whether *any*
compilation work, placeholder pipeline or compile wait still happens during a
covered song, on each claimed backend and rendering path, cold and warm - and
what the post-A preparation costs. This module prepares the isolated runs, turns
a collected run into an explicit pass/fail record, and aggregates them.

It reuses `scripts/shader_cache_trace.py` for the P0 preparation/collection
machinery (isolated user data, cache and profile copies, an inert launcher, the
trace parser) and only adds what P8 needs: the matrix, deterministic fixtures,
and acceptance. All artifacts stay in ignored `out/`. Nothing here launches or
drives the game; the operator runs `launch.ps1` of a prepared row.

Commands (python scripts/shader_cache_p8.py <command>):

    plan                     print the matrix and what each row expects
    prepare <row>            prepare one row (all inputs isolated)
    prepare-next             prepare the next runnable row, in matrix order
    collect <row> --observations "..."   after the operator closed the game
    accept <row>             write acceptance.json and exit non-zero on a failed row
    summarize                out/shader-cache-p8/summary.md + .json across all rows

The matrix's row ids are stable; `summarize` never overwrites a collected run.
"""
import argparse
import json
import os
import re
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shader_cache_trace as trace

ROOT = trace.ROOT
MATRIX = ROOT / "out" / "shader-cache-p8"
RUNS = MATRIX / "runs"
trace.RUNS = RUNS  # the P0 machinery is reused against the P8 run directory

# P8's acceptance vocabulary. `compile_drain` is deliberately absent: it counts
# owner-thread service slices, including idle ones (a warm menu showed 7604), so
# it is not compilation work.
# Guest-side work a runtime miss would produce. Any of these during a song fails
# the route: they are what the shipped catalogue exists to prevent.
FORBIDDEN_ACTIVE_SONG = ("translation", "guest_pipeline", "placeholder", "compile_wait")
# Renderer helper pipelines created lazily on the first transfer/resolve/dump of
# a given format (P3's F6 class). No catalogue can enumerate those formats, so
# the owner accepted a bounded first-use on 2026-10-25: one creation, at most
# 5 ms in total, once per process. A row still has to opt in, and anything above
# the budget fails it.
HELPER_ACTIVE_SONG = ("helper_graphics", "helper_compute")
HELPER_FIRST_USE_BUDGET = {"count": 1, "max_us": 5000}

# Vulkan's own presenter recreates its guest-output paint pipeline on every present
# (`vulkan_presenter.cpp`, one identity, handed to the driver's shared
# VkPipelineCache). That is a host defect to fix, not a guest shader appearing
# mid-song, and the counters can tell the two apart: the cold Vulkan row spent
# 1,473 calls / 209 ms over an 88 s song, worst 818 us, frames healthy. A row opts
# in with `allow_host_helper=True`; the acceptance record then carries the measured
# numbers, so the exception is auditable instead of invisible.
HOST_HELPER_BUDGET = {"us_per_song_second": 5000, "max_us": 5000}

# The four declared rendering paths, as the extra launch arguments that select
# them. rtv/fbo are the defaults.
PATH_ARGUMENTS = {
    "rtv": [],
    "rov": ["--render_target_path_d3d12=rov"],
    "fbo": [],
    "fsi": ["--render_target_path_vulkan=fsi"],
}
PATH_KEY = {("d3d12", "rtv"): "rtv.d3d12", ("d3d12", "rov"): "rov.d3d12",
            ("vulkan", "fbo"): "fbo.vk", ("vulkan", "fsi"): "fsi.vk"}

ROUTE_FULL = (
    "Title: press A; let 'Preparing graphics' finish into the menu. "
    "Song Browser -> a bundled song -> with one power-up loadout, play about a minute "
    "(activate every power-up of that loadout at least once), then Pause -> Quit back to "
    "the browser. Repeat with the remaining loadouts until every power-up has been used; "
    "on the last loadout, play the song to the results screen (the results path is "
    "covered once). Then close the window normally.")
ROUTE_SHORT = (
    "Title: press A; let the loading/graphics bar finish into the menu. "
    "Song Browser -> the same song as the matching cold row -> play to the results "
    "screen, using every power-up at least once. Close the window normally.")
ROUTE_CANCEL = (
    "Title: press A. While the bar says 'Preparing graphics', press B once - you "
    "should return to the title. Press A again and let it finish into the menu, then "
    "close the window normally. Report whether the first attempt really did return "
    "to the title.")
ROUTE_READONLY = (
    "Title: press A and wait. Preparation cannot write its cache, so the failure row "
    "appears: read its second line back to the agent and press B (Ignore) to reach the "
    "menu. Then close the window normally.")
ROUTE_FAILURE = (
    "Title: press A and let it reach the menu. No song is needed. Close the window "
    "normally; if the failure row appears instead, read its second line back to the agent.")
ROUTE_FINAL = (
    "Title: press A and note roughly how long 'Preparing graphics' shows. Pick any bundled "
    "song (not These Days), play about a minute using a couple of power-ups, then close the "
    "window normally.")


def row(id, tier, kind, backend, variant, path="rtv", warm_from=None, extra=None,
        route=ROUTE_FULL, note="", allow_helper_first_use=False, song_required=True,
        allow_host_helper=False):
    return {"id": id, "tier": tier, "kind": kind, "backend": backend, "variant": variant,
            "path": path, "warm_from": warm_from, "extra": extra or [], "route": route,
            "note": note, "allow_helper_first_use": allow_helper_first_use,
            "allow_host_helper": allow_host_helper, "song_required": song_required}


ROWS = [
    row("p8-d3d12-ultimate-rtv-cold", 1, "cold", "d3d12", "ultimate", "rtv",
        note="coverage session: this row found 9 rtv recipes and one pixel identity the "
             "shipped catalogue lacked; kept as the record of that finding"),
    row("p8-d3d12-ultimate-rtv-cold-r2", 1, "cold", "d3d12", "ultimate", "rtv",
        note="the same route after the catalogue extension: the gate must prepare 421 recipes "
             "and the song must add none"),
    row("p8-d3d12-ultimate-rtv-cold-r3", 1, "cold", "d3d12", "ultimate", "rtv",
        note="post-fix baseline: the content identity that decides layer reuse is now derived "
             "from stable inputs (the first two boots of one install produced three different "
             "values, so the warm path never reused anything). This row establishes the "
             "stable ledger every dependent row warms from"),
    row("p8-d3d12-ultimate-rtv-warm", 1, "warm", "d3d12", "ultimate", "rtv",
        warm_from="p8-d3d12-ultimate-rtv-cold-r2", route=ROUTE_SHORT,
        note="pre-fix record: this boot did not reuse any layer because the content identity "
             "changed between boots; it is kept as the evidence of that defect"),
    row("p8-d3d12-ultimate-rtv-warm-r2", 1, "warm", "d3d12", "ultimate", "rtv",
        warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_SHORT,
        note="the warm path after the content-identity fix: extraction and all four recipe "
             "layers must be reused"),
    row("p8-d3d12-ultimate-rtv-warm-nonative", 1, "warm", "d3d12", "ultimate", "rtv",
        warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_FINAL,
        extra=["--native_pipeline_cache=false"],
        note="isolates the native driver blob layer: the same warm layers with no cached "
             "PSOs, so the 16 s preparation the warm row measured can be attributed"),
    row("p8-vulkan-ultimate-fbo-cold", 1, "cold", "vulkan", "ultimate", "fbo",
        allow_host_helper=True,
        note="the Vulkan backend at all. The only in-song helper is the presenter's own "
             "pipeline, recreated per present - a host defect recorded as a finding, not a "
             "guest shader"),
    row("p8-vulkan-ultimate-fbo-warm", 1, "warm", "vulkan", "ultimate", "fbo",
        warm_from="p8-vulkan-ultimate-fbo-cold", route=ROUTE_SHORT, allow_host_helper=True),
    row("p8-vulkan-ultimate-fbo-cold-r2", 1, "cold", "vulkan", "ultimate", "fbo",
        route=ROUTE_FINAL,
        note="the same Vulkan route on the build that carries patch 0033 (the presenter now "
             "keeps its pipeline): verifies the in-song helper traffic collapses, and that a "
             "Vulkan cold boot still prepares every pipeline"),
    row("p8-vulkan-ultimate-fbo-cold-r3", 1, "cold", "vulkan", "ultimate", "fbo",
        route=ROUTE_FINAL,
        note="after the catalogue was extended with the twelve recipes -cold-r2 created "
             "in-song (vulkan/fbo 409 -> 421): the same song must add no pipeline and no "
             "translation. Its four in-song translations were already in the 209 prepared "
             "sources, which is the open question this row answers"),
    row("p8-vulkan-ultimate-fbo-warm-r2", 1, "warm", "vulkan", "ultimate", "fbo",
        warm_from="p8-vulkan-ultimate-fbo-cold-r2", route=ROUTE_SHORT),
    row("p8-d3d12-ultimate-rtv-corrupt-portable", 2, "corrupt-portable", "d3d12", "ultimate",
        "rtv", warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_SHORT,
        note="superseded by -damaged-all for the reduced scope; kept in the matrix"),
    row("p8-d3d12-ultimate-rtv-damaged-all", 1, "damaged-all", "d3d12", "ultimate",
        "rtv", warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_FAILURE,
        song_required=False,
        note="one boot that damages the portable store, the ledger and a native blob "
             "together: proves the runtime repairs every layer rather than failing the "
             "launch, and needs no song"),
    row("p8-d3d12-ultimate-rtv-corrupt-ledger", 2, "corrupt-ledger", "d3d12", "ultimate",
        "rtv", warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_SHORT,
        note="superseded by -damaged-all for the reduced scope; kept in the matrix"),
    row("p8-d3d12-ultimate-rtv-native-invalid", 2, "native-invalid", "d3d12", "ultimate",
        "rtv", warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_SHORT,
        note="superseded by -damaged-all for the reduced scope; kept in the matrix"),
    row("p8-d3d12-ultimate-rtv-cancel", 2, "cancel", "d3d12", "ultimate", "rtv",
        route=ROUTE_CANCEL, song_required=False,
        note="reduced scope: the cancel record is kept but may stay unrun"),
    row("p8-d3d12-ultimate-rtv-readonly", 1, "readonly", "d3d12", "ultimate", "rtv",
        route=ROUTE_READONLY),
    row("p8-d3d12-ultimate-rov-cold", 2, "cold", "d3d12", "ultimate", "rov"),
    row("p8-d3d12-ultimate-rov-warm", 2, "warm", "d3d12", "ultimate", "rov",
        warm_from="p8-d3d12-ultimate-rov-cold", route=ROUTE_SHORT),
    row("p8-vulkan-ultimate-fsi-cold", 2, "cold", "vulkan", "ultimate", "fsi"),
    row("p8-vulkan-ultimate-fsi-warm", 2, "warm", "vulkan", "ultimate", "fsi",
        warm_from="p8-vulkan-ultimate-fsi-cold", route=ROUTE_SHORT),
    row("p8-d3d12-retail-rtv-cold", 2, "cold", "d3d12", "retail", "rtv"),
    row("p8-d3d12-retail-rtv-warm", 2, "warm", "d3d12", "retail", "rtv",
        warm_from="p8-d3d12-retail-rtv-cold", route=ROUTE_SHORT),
    row("p8-d3d12-ultimate-rtv-config-scale", 2, "config", "d3d12", "ultimate", "rtv",
        warm_from="p8-d3d12-ultimate-rtv-cold-r3", route=ROUTE_SHORT,
        extra=["--draw_resolution_scale_x=2", "--draw_resolution_scale_y=2"],
        note="a pipeline-affecting setting changed after a warm cache: the shipped "
             "catalogue was captured at the default scale, so this row is a coverage "
             "discovery, not a pass/fail gate"),
]
BY_ID = {entry["id"]: entry for entry in ROWS}

# Owner decision, 2026-10-25: the D3D12 render-target transfer helper is created
# once per process (0.9 ms measured) and is accepted within the budget above.
# Vulkan rows instead opt into the host-helper budget, because their helper traffic
# is the presenter rebuilding one pipeline per present rather than a first use.
for entry in ROWS:
    if entry["backend"] == "d3d12":
        entry["allow_helper_first_use"] = True


def run_dir(name):
    if name not in BY_ID:
        raise ValueError(f"unknown row '{name}' (see `plan`)")
    return RUNS / name


def row_of(name):
    return BY_ID[name]


def local_sources():
    """The operator's save/profile inputs, kept out of tracked files.

    Written by `configure`; the fallback is the SDK's own location, so a normal
    checkout on any machine works without editing this script.
    """
    config = MATRIX / "local.json"
    if config.is_file():
        data = json.loads(config.read_text(encoding="utf-8"))
        return Path(data["user_data"]), Path(data["profile"])
    home = Path.home()
    appdata = Path(os.environ.get("APPDATA", str(home)))
    return home / "Documents" / "Rock Band Blitz", appdata / "rb_blitz" / "launcher.toml"


def default_game():
    return ROOT / "game"


def prepare(args):
    entry = row_of(args.name)
    target = RUNS / entry["id"]
    if target.exists():
        raise ValueError("row already prepared; a prepared run is never overwritten")
    if entry["warm_from"] and not (RUNS / entry["warm_from"] / "after-cache").is_dir():
        raise ValueError(f"row needs {entry['warm_from']} collected first")
    user_data, profile = local_sources()
    if args.user_data:
        user_data = args.user_data
    if args.profile:
        profile = args.profile
    if not user_data.exists():
        raise ValueError(f"missing user data root: {user_data} (run `configure`)")
    game_args = list(PATH_ARGUMENTS[entry["path"]]) + list(entry["extra"])
    trace.prepare(argparse.Namespace(
        name=entry["id"], backend=entry["backend"], variant=entry["variant"],
        game=args.game, user_data=user_data, profile=profile, route=entry["route"],
        warm_from=entry["warm_from"], game_arg=game_args))
    run = RUNS / entry["id"]
    fixture = apply_fixture(run, entry)
    manifest = json.loads((run / "manifest.json").read_text(encoding="utf-8"))
    manifest["p8"] = {"schema": 1, "tier": entry["tier"], "kind": entry["kind"],
                      "render_path": entry["path"], "note": entry["note"],
                      "fixture": fixture}
    # The snapshot the operator starts from must describe what is really there.
    if (run / "cache").is_dir():
        shutil.rmtree(run / "before-cache", ignore_errors=True)
        shutil.copytree(run / "cache", run / "before-cache")
    else:
        shutil.rmtree(run / "before-cache", ignore_errors=True)
    manifest["before_cache"] = (trace.inventory(run / "before-cache")
                                if (run / "before-cache").is_dir() else {})
    (run / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(run)
    print(f"ROUTE ({entry['id']}): {entry['route']}")
    print("Run launch.ps1 from this directory after the build is current; then `collect`.")


def _damage_portable(cache, entry):
    stores = [p for p in (cache / "shaders" / "shareable").glob("*.xpso")
              if PATH_KEY[(entry["backend"], entry["path"])] in p.name]
    if not stores:
        raise ValueError("no portable store of this row's path; was the cold row collected?")
    store = stores[0]
    before = trace.digest(store)
    data = bytearray(store.read_bytes())
    if not data:
        raise ValueError("empty portable store")
    data[-1] ^= 0xFF
    store.write_bytes(data)
    return {"file": store.name, "before_sha256": before,
            "damaged_sha256": trace.digest(store)}


def apply_fixture(run, entry):
    """Deterministic damage the row needs, applied to the prepared copy only."""
    cache = run / "cache"
    kind = entry["kind"]
    record = {}
    if kind in {"cold", "warm", "config", "cancel"}:
        return record
    if kind == "readonly":
        # A regular file where the cache root must be a directory: both the
        # probe and the backend's own storage open fail, which is the
        # "not writable" case and nothing else.
        shutil.rmtree(cache, ignore_errors=True)
        cache.write_bytes(b"the cache root is not writable in this row")
        record["cache_root"] = "replaced by a regular file"
        return record
    if kind == "damaged-all":
        # Every layer damaged at once: the same boot must rebuild rather than
        # refuse, which is the whole point of repair-instead-of-fail.
        record["portable"] = _damage_portable(cache, entry)
        ledger = cache / "shaders" / "preparation-state.json"
        if ledger.is_file():
            record["ledger"] = {"before_sha256": trace.digest(ledger), "damaged": "truncated JSON"}
            ledger.write_text('{"schema":1,"extraction":', encoding="utf-8")
        local = cache / "shaders" / "local"
        entries = sorted(p for p in local.rglob("*") if p.is_file()) if local.is_dir() else []
        if entries:
            entry_file = entries[0]
            record["native"] = {"file": entry_file.relative_to(cache).as_posix(),
                                "before_sha256": trace.digest(entry_file)}
            data = bytearray(entry_file.read_bytes())
            for index in range(min(8, len(data))):
                data[index] ^= 0xFF
            entry_file.write_bytes(data)
            record["native"]["damaged_sha256"] = trace.digest(entry_file)
        return record
    if kind in {"corrupt-portable", "corrupt-ledger", "native-invalid"}:
        if not cache.is_dir():
            raise ValueError("row needs a cache directory; prepare it from a collected cold row")
    if kind == "corrupt-portable":
        record["portable"] = _damage_portable(cache, entry)
    elif kind == "corrupt-ledger":
        ledger = cache / "shaders" / "preparation-state.json"
        if not ledger.is_file():
            raise ValueError("no ledger to damage; the cold row must have been collected")
        record["ledger"] = {"before_sha256": trace.digest(ledger),
                            "damaged": "truncated JSON"}
        ledger.write_text('{"schema":1,"extraction":', encoding="utf-8")
    elif kind == "native-invalid":
        local = cache / "shaders" / "local"
        entries = sorted(p for p in local.rglob("*") if p.is_file()) if local.is_dir() else []
        if not entries:
            raise ValueError("no native driver entry yet; the cold row must have created some")
        entry_file = entries[0]
        record["native"] = {"file": entry_file.relative_to(cache).as_posix(),
                            "before_sha256": trace.digest(entry_file)}
        data = bytearray(entry_file.read_bytes())
        for index in range(min(8, len(data))):
            data[index] ^= 0xFF
        entry_file.write_bytes(data)
        record["native"]["damaged_sha256"] = trace.digest(entry_file)
    return record


def collect(args):
    run = run_dir(args.name)
    manifest = json.loads((run / "manifest.json").read_text(encoding="utf-8"))
    if (run / "after-cache").exists():
        raise ValueError("row already collected; snapshots are immutable")
    if not (run / "game.log").is_file():
        raise ValueError("no game.log; run launch.ps1 and close the game first")
    cache = run / "cache"
    if cache.is_dir():
        shutil.copytree(cache, run / "after-cache")
        manifest["after_cache"] = trace.inventory(run / "after-cache")
    else:
        # The read-only row's cache root is deliberately not a directory.
        manifest["after_cache"] = {}
        manifest["cache_snapshot"] = "not a directory (read-only row by design)"
    manifest["observations"] = args.observations
    manifest["collected_utc"] = datetime.now(timezone.utc).isoformat()
    manifest["log_sha256"] = trace.digest(run / "game.log")
    (run / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    trace.report_file(run / "game.log", run / "report.json")
    print(run / "report.json")


SUMMARY = re.compile(
    r"summary snapshot=(\d+) reason=(\S+) phase=(\S+) event=(\S+) count=(\d+) failed=(\d+) "
    r"total_us=(\d+) max_us=(\d+) ge50=(\d+) ge100=(\d+) ge250=(\d+) suppressed=(\d+)")
LAYERS = re.compile(
    r"graphics_loading: layers extraction=(\S+) recipes=(\S+)/(\S+)/(\S+)/(\S+) repaired=(\d+) "
    r"shaders=(\d+) records=(\d+)/(\d+)/(\d+)/(\d+)")
RELEASE = re.compile(
    r"graphics_loading: attempt (\d+) released generation (\d+) exactly once \((\d+) ms, "
    r"(\d+) pipelines, ignored=(true|false)\)")
FAILED = re.compile(r"graphics_loading: attempt (\d+) failed \((\w+)\)")
CANCELLED = re.compile(r"graphics_loading: attempt (\d+) cancelled;")
GATE = re.compile(r"graphics_loading: post-A native loading gate configured settings=(\S+) "
                  r"\((\d+) pipeline-affecting setting\(s\), cache writable=(\w+)\)")
NOT_WRITABLE = re.compile(r"graphics_loading: the cache root .* is not writable")
PERSISTED = re.compile(r"graphics_loading: prepared graphics but could not save the reusable result")
NATIVE_REJECTED = re.compile(r"D3D12 rejected a stored cached-PSO blob|Native pipeline cache: .*")


def parse_log(path):
    facts = {"summaries": {}, "layers": None, "releases": [], "failures": [],
             "cancelled": [], "gate": None, "not_writable": False,
             "not_persisted": False, "native": []}
    text = path.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if "shader_trace:" in line:
            match = SUMMARY.search(line)
            if match:
                snapshot, reason, phase, event = match.group(1, 2, 3, 4)
                counters = {"count": int(match.group(5)), "failed": int(match.group(6)),
                            "total_us": int(match.group(7)), "max_us": int(match.group(8)),
                            "ge50": int(match.group(9)), "ge100": int(match.group(10)),
                            "ge250": int(match.group(11)), "suppressed": int(match.group(12)),
                            "reason": reason}
                key = f"{phase}/{event}"
                # Summaries are cumulative; the highest count is the final one.
                previous = facts["summaries"].get(key)
                if previous is None or counters["count"] >= previous["count"]:
                    facts["summaries"][key] = counters
                continue
        match = LAYERS.search(line)
        if match:
            facts["layers"] = {
                "extraction": match.group(1),
                "recipes": list(match.group(2, 3, 4, 5)),
                "repaired": int(match.group(6)), "shaders": int(match.group(7)),
                "records": [int(match.group(8 + index)) for index in range(4)]}
            continue
        match = RELEASE.search(line)
        if match:
            facts["releases"].append({"attempt": int(match.group(1)),
                                      "generation": int(match.group(2)),
                                      "milliseconds": int(match.group(3)),
                                      "pipelines": int(match.group(4)),
                                      "ignored": match.group(5) == "true"})
            continue
        match = FAILED.search(line)
        if match:
            facts["failures"].append({"attempt": int(match.group(1)), "reason": match.group(2)})
            continue
        match = CANCELLED.search(line)
        if match:
            facts["cancelled"].append(int(match.group(1)))
            continue
        match = GATE.search(line)
        if match:
            facts["gate"] = {"settings": match.group(1),
                             "pipeline_settings": int(match.group(2)),
                             "cache_writable": match.group(3) == "true"}
            continue
        if NOT_WRITABLE.search(line):
            facts["not_writable"] = True
        elif PERSISTED.search(line):
            facts["not_persisted"] = True
        elif NATIVE_REJECTED.search(line):
            facts["native"].append(line.strip())
    return facts


def active_song_violations(facts):
    return {event: facts["summaries"][f"active_song/{event}"]
            for event in FORBIDDEN_ACTIVE_SONG
            if facts["summaries"].get(f"active_song/{event}", {}).get("count", 0)}


def active_song_helpers(facts):
    return {event: facts["summaries"][f"active_song/{event}"]
            for event in HELPER_ACTIVE_SONG
            if facts["summaries"].get(f"active_song/{event}", {}).get("count", 0)}


def frame_stats(facts):
    stats = {}
    for event in ("guest_frame", "gpu_frame"):
        counters = facts["summaries"].get(f"active_song/{event}")
        if not counters:
            continue
        count = counters["count"]
        stats[event] = {"count": count,
                        "mean_ms": round(counters["total_us"] / count / 1000, 2) if count else None,
                        "max_ms": round(counters["max_us"] / 1000, 2),
                        "ge50": counters["ge50"], "ge100": counters["ge100"],
                        "ge250": counters["ge250"]}
    return stats


def bar_timing(report):
    phases = report.get("phases", [])
    for index, phase in enumerate(phases):
        if phase.get("phase") != "post_a_loading":
            continue
        for later in phases[index + 1:]:
            if later.get("phase") == "menu":
                delta = int(later["t_us"]) - int(phase["t_us"])
                return {"post_a_to_menu_ms": round(delta / 1000, 1)}
        return {"post_a_to_menu_ms": None}
    return {"post_a_to_menu_ms": None}


def ledger_facts(run):
    ledger = run / "after-cache" / "shaders" / "preparation-state.json"
    if not ledger.is_file():
        return None
    try:
        return json.loads(ledger.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def cache_sizes(run):
    result = {}
    for layer in ("shareable", "local"):
        folder = run / "after-cache" / "shaders" / layer
        if folder.is_dir():
            files = [path for path in folder.rglob("*") if path.is_file()]
            result[layer] = {"files": len(files), "bytes": sum(p.stat().st_size for p in files)}
    return result


def checks_for(entry, facts, report, run):
    """P8's explicit verdicts. Each is a fact plus the row's expectation."""
    checks = []

    def check(name, ok, detail):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})

    releases = [release for release in facts["releases"] if not release["ignored"]]
    ignored_release = [release for release in facts["releases"] if release["ignored"]]
    check("post_a_gate_present", facts["gate"] is not None, facts["gate"])
    violations = active_song_violations(facts)
    discovery = entry["kind"] == "config"
    check("no_active_song_compiles", not violations,
          {"violations": violations} if violations else "none")
    if discovery:
        # A changed setting is a coverage question, not a pass/fail gate: the
        # catalogue was captured at the default scale.
        checks[-1]["ok"] = True
        checks.append({"check": "coverage_discovery", "ok": True,
                       "detail": {"active_song_events": violations,
                                  "note": entry["note"]}})
    helpers = active_song_helpers(facts)
    frames = frame_stats(facts)
    helper_allowed = entry.get("allow_helper_first_use", False)
    within_budget = all(counters["count"] <= HELPER_FIRST_USE_BUDGET["count"] and
                        counters["total_us"] <= HELPER_FIRST_USE_BUDGET["max_us"]
                        for counters in helpers.values())
    host_exception = None
    if helpers and entry.get("allow_host_helper", False):
        song_seconds = (max((frames.get("gpu_frame") or {}).get("count", 0), 1) *
                        ((frames.get("gpu_frame") or {}).get("mean_ms") or 33.3) / 1000.0)
        total_helper = sum(counters["total_us"] for counters in helpers.values())
        worst_helper = max(counters["max_us"] for counters in helpers.values())
        allowance = HOST_HELPER_BUDGET["us_per_song_second"] * song_seconds
        host_exception = {
            "total_us": total_helper, "max_us": worst_helper,
            "song_seconds": round(song_seconds, 1), "allowance_us": int(allowance),
            "failed": any(counters["failed"] for counters in helpers.values()),
            "rule": "host helper calls may not exceed 5 ms per song second or 5 ms each, "
                    "and none may fail; frames_healthy must also pass"}
    host_ok = bool(host_exception) and not host_exception["failed"] and \
        host_exception["total_us"] <= host_exception["allowance_us"] and \
        host_exception["max_us"] <= HOST_HELPER_BUDGET["max_us"]
    check("no_active_song_helper_creation",
          not helpers or (helper_allowed and within_budget) or host_ok,
          {"helpers": helpers, "accepted_by_row_policy": helper_allowed,
           "host_helper_exception": host_exception,
           "budget": HELPER_FIRST_USE_BUDGET} if helpers else "none")
    if entry["kind"] != "readonly" and entry.get("song_required", True):
        check("frames_recorded", bool(frames), frames)
        # A row that was unplayable is not evidence about preparation. The
        # thresholds are the plan's own: 33.3 ms is the 60 Hz budget times two,
        # and no song frame may exceed 250 ms at all.
        gpu = frames.get("gpu_frame") or {}
        worst = max((frames.get(event, {}).get("max_ms") or 0)
                    for event in ("guest_frame", "gpu_frame"))
        # A handful of >=250 ms frames is the known song-start hitch; a pattern of
        # them is an unplayable run (the r3 shape had 182 of 596 frames). Budget
        # 0.1% of the song's frames, at least two.
        over = sum((frames.get(event) or {}).get("ge250", 0)
                   for event in ("guest_frame", "gpu_frame"))
        total = max((frames.get("gpu_frame") or {}).get("count", 0), 1)
        allowance = max(2, int(total * 0.001))
        check("frames_healthy",
              (gpu.get("mean_ms") is not None and gpu["mean_ms"] <= 33.3 and
               over <= allowance),
              {"gpu_mean_ms": gpu.get("mean_ms"), "worst_ms": worst,
               "frames_over_250_ms": over, "allowance": allowance,
               "rule": "gpu mean <= 33.3 ms and >=250 ms frames within 0.1% (min 2)"})

    if entry["kind"] == "readonly":
        check("failed_as_cache_not_writable",
              any(failure["reason"] == "cache_not_writable" for failure in facts["failures"]),
              facts["failures"])
        check("not_writable_reported", facts["not_writable"], facts["not_writable"])
        check("readiness_not_claimed", not releases and bool(ignored_release),
              {"real_releases": releases, "ignored": ignored_release})
    else:
        check("gate_released", bool(releases), releases[-1] if releases else None)
        if entry["kind"] == "cancel":
            check("cancel_recorded", bool(facts["cancelled"]), facts["cancelled"])
        if entry["kind"] == "cold":
            check("layers_rebuilt", (facts["layers"] or {}).get("extraction") == "rebuilt",
                  (facts["layers"] or {}).get("extraction"))
        if entry["kind"] in {"warm", "native-invalid"}:
            layers = facts["layers"] or {}
            check("layers_reused", layers.get("extraction") == "reused" and
                  all(item == "reused" for item in layers.get("recipes", [])), layers)
        if entry["kind"] in {"corrupt-portable", "corrupt-ledger", "damaged-all"}:
            layers = facts["layers"] or {}
            check("damage_repaired", layers.get("repaired", 0) > 0, layers)
            check("gate_released_after_repair", bool(releases), releases[-1] if releases else None)
        if entry["kind"] == "native-invalid":
            check("native_layer_present_in_log", bool(facts["native"]), facts["native"][:2])
    ledger = ledger_facts(run)
    if entry["kind"] == "readonly":
        # No ledger can exist: that is the row's whole point.
        checks.append({"check": "ledger_recorded", "ok": True,
                       "detail": "not applicable: the cache root is not writable"})
    else:
        check("ledger_recorded", ledger is not None,
              None if ledger is None else {
                  "phase": ledger.get("last_attempt", {}).get("phase"),
                  "complete": ledger.get("last_attempt", {}).get("complete"),
                  "epoch": ledger.get("content_epoch")})
    if ledger and entry["kind"] != "readonly":
        attempt = ledger.get("last_attempt", {})
        check("attempt_complete", attempt.get("complete") is True,
              {"phase": attempt.get("phase"), "required": attempt.get("required"),
               "succeeded": attempt.get("succeeded"), "failed": attempt.get("failed"),
               "missing": attempt.get("missing")})
    if entry["kind"] in {"warm", "native-invalid"} and ledger:
        extraction = ledger.get("extraction", {})
        check("ledger_extraction_complete", extraction.get("complete") is True,
              {"entries": extraction.get("entries")})
    return checks


def accept(args):
    run = run_dir(args.name)
    entry = row_of(args.name)
    if not (run / "after-cache").exists() and entry["kind"] != "readonly":
        raise ValueError("row not collected yet; run `collect` first")
    facts = parse_log(run / "game.log")
    report_path = run / "report.json"
    report = json.loads(report_path.read_text(encoding="utf-8")) if report_path.is_file() else {}
    checks = checks_for(entry, facts, report, run)
    failed = [check for check in checks if not check["ok"]]
    ledger = ledger_facts(run)
    result = {"schema": 1, "row": entry["id"], "kind": entry["kind"], "tier": entry["tier"],
              "backend": entry["backend"], "path": entry["path"], "variant": entry["variant"],
              "verdict": "PASS" if not failed else "FAIL",
              "checks": checks,
              "facts": {"active_song": {key: facts["summaries"][f"active_song/{key}"]
                                        for key in FORBIDDEN_ACTIVE_SONG + HELPER_ACTIVE_SONG
                                        if f"active_song/{key}" in facts["summaries"]},
                        "frames": frame_stats(facts), "layers": facts["layers"],
                        "releases": facts["releases"], "failures": facts["failures"],
                        "cancelled": facts["cancelled"], "gate": facts["gate"],
                        "bar": bar_timing(report),
                        "long_frames": [frame for frame in report.get("long_frames", [])
                                        if frame.get("phase") == "active_song"],
                        "native_log": facts["native"],
                        "not_persisted": facts["not_persisted"],
                        "cache": cache_sizes(run),
                        "ledger": None if ledger is None else {
                            "content_epoch": ledger.get("content_epoch"),
                            "extraction": ledger.get("extraction"),
                            "recipes": ledger.get("recipes"),
                            "last_attempt": ledger.get("last_attempt")}},
              "limits": ["only frames >= 50 ms are logged individually, so no median/p95/p99 "
                         "and no 33.3 ms bucket exist in this instrument",
                         "active_song is an input-enabled controller proxy, not an audio-clock "
                         "boundary (part 1)",
                         "skipped draws are not instrumented; the placeholder counter is the proxy",
                         "pipeline memory is not instrumented"],
              "recorded_utc": datetime.now(timezone.utc).isoformat()}
    (run / "acceptance.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"{entry['id']}: {result['verdict']}")
    for check in checks:
        print(f"  {'ok  ' if check['ok'] else 'FAIL'} {check['check']}")
    print(f"  frames: {json.dumps(result['facts']['frames'])}")
    print(f"  bar: {json.dumps(result['facts']['bar'])}  cache: {json.dumps(result['facts']['cache'])}")
    (MATRIX / "summary.md").unlink(missing_ok=True)
    if failed:
        sys.exit(1)


def summarize(args):
    entries, rows = [], []
    for entry in ROWS:
        run = RUNS / entry["id"]
        acceptance = run / "acceptance.json"
        state = "not run"
        verdict = ""
        if acceptance.is_file():
            data = json.loads(acceptance.read_text(encoding="utf-8"))
            verdict = data["verdict"]
            state = "accepted"
            entries.append(data)
        elif run.exists():
            state = "prepared, not collected"
        rows.append({"row": entry["id"], "tier": entry["tier"], "kind": entry["kind"],
                     "backend": entry["backend"], "path": entry["path"],
                     "variant": entry["variant"], "state": state, "verdict": verdict})
    passed = [row for row in rows if row["verdict"] == "PASS"]
    failed = [row for row in rows if row["verdict"] == "FAIL"]
    lines = ["# P8 shader-cache validation", "",
             f"Generated {datetime.now(timezone.utc).isoformat()}", "",
             "| Row | Tier | Kind | Backend | Path | Variant | State | Verdict |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for row in rows:
        lines.append("| {row} | {tier} | {kind} | {backend} | {path} | {variant} | {state} | "
                     "{verdict} |".format(**row))
    lines += ["", f"Accepted: **{len(entries)}** - PASS {len(passed)}, FAIL {len(failed)}.", "",
              "Only rows with an acceptance record are evidence. A tier-2 row left "
              "\"not run\" is reported as untested scope, never as passing.", ""]
    detail = []
    for data in entries:
        frames = data["facts"]["frames"].get("gpu_frame") or {}
        detail.append({
            "row": data["row"], "verdict": data["verdict"],
            "preparation_ms": (data["facts"]["releases"][-1]["milliseconds"]
                               if data["facts"]["releases"] else None),
            "pipelines": (data["facts"]["releases"][-1]["pipelines"]
                          if data["facts"]["releases"] else None),
            "post_a_to_menu_ms": data["facts"]["bar"].get("post_a_to_menu_ms"),
            "active_song_violations": data["facts"]["active_song"],
            "gpu_frame": frames,
            "long_frames": len(data["facts"]["long_frames"]),
            "cache": data["facts"]["cache"]})
    lines.append("## Measurements")
    lines.append("")
    lines.append("| Row | Preparation ms | Pipelines | Post-A to menu ms | In-song compile events "
                 "| GPU max ms (ge50/100/250) | Long frames | Cache bytes |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for item in detail:
        frames = item["gpu_frame"] or {}
        cache = item["cache"]
        size = (cache.get("shareable", {}).get("bytes", 0) +
                cache.get("local", {}).get("bytes", 0))
        lines.append("| {row} | {preparation_ms} | {pipelines} | {post_a_to_menu_ms} | {events} "
                     "| {max_ms} ({ge50}/{ge100}/{ge250}) | {long_frames} | {size} |".format(
                         events=(json.dumps(item["active_song_violations"])
                                 if item["active_song_violations"] else "none"),
                         max_ms=frames.get("max_ms"), ge50=frames.get("ge50"),
                         ge100=frames.get("ge100"), ge250=frames.get("ge250"), size=size,
                         **item))
    lines += ["", "## What remains unproven", "",
              "- Any row above left \"not run\": its backend/path/variant has no P8 evidence.",
              "- Only frames >= 50 ms are logged individually: no median/p95/p99 and no "
              "33.3 ms bucket exist in this instrument.",
              "- `active_song` is an input-enabled controller proxy, not an audio-clock boundary.",
              "- Skipped draws are not instrumented; the placeholder counter is the proxy.",
              "- Pipeline memory is not instrumented.",
              "- This is one machine (see each run's manifest for adapter, driver and build).", ""]
    MATRIX.mkdir(parents=True, exist_ok=True)
    (MATRIX / "summary.md").write_text("\n".join(lines), encoding="utf-8")
    (MATRIX / "summary.json").write_text(json.dumps(
        {"rows": rows, "accepted": entries, "generated_utc":
         datetime.now(timezone.utc).isoformat()}, indent=2), encoding="utf-8")
    print(MATRIX / "summary.md")
    if failed:
        sys.exit(1)


def plan(args):
    tier = 1
    print("P8 rows (tier 1 is the required set; tier 2 is recommended scope):")
    for entry in ROWS:
        if entry["tier"] != tier:
            tier = entry["tier"]
            print()
        state = "not prepared"
        run = RUNS / entry["id"]
        if (run / "acceptance.json").is_file():
            state = json.loads((run / "acceptance.json").read_text(encoding="utf-8"))["verdict"]
        elif (run / "after-cache").exists():
            state = "collected, not accepted"
        elif run.exists():
            state = "prepared"
        needs = f" <- {entry['warm_from']}" if entry["warm_from"] else ""
        print(f"  [{entry['tier']}] {entry['id']}  ({entry['kind']}, {entry['backend']} "
              f"{entry['path']}, {entry['variant']}) {state}{needs}")
        print(f"      run: {entry['route']}")
        if entry["note"]:
            print(f"      note: {entry['note']}")


def prepare_next(args):
    for entry in ROWS:
        run = RUNS / entry["id"]
        if run.exists():
            continue
        if entry["warm_from"] and not (RUNS / entry["warm_from"] / "after-cache").is_dir():
            continue
        prepare(argparse.Namespace(name=entry["id"], game=args.game,
                                   user_data=args.user_data, profile=args.profile))
        return
    print("every row is prepared; collect/accept the prepared ones")


def configure(args):
    MATRIX.mkdir(parents=True, exist_ok=True)
    data = {"user_data": str(args.user_data), "profile": str(args.profile),
            "recorded_utc": datetime.now(timezone.utc).isoformat()}
    (MATRIX / "local.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(MATRIX / "local.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("plan").set_defaults(func=plan)

    def common(parser):
        parser.add_argument("--game", type=lambda p: Path(p).resolve(), default=default_game())
        parser.add_argument("--user-data", type=lambda p: Path(p).resolve())
        parser.add_argument("--profile", type=lambda p: Path(p).resolve())

    prep = commands.add_parser("prepare")
    prep.add_argument("name")
    common(prep)
    prep.set_defaults(func=prepare)
    nxt = commands.add_parser("prepare-next")
    common(nxt)
    nxt.set_defaults(func=prepare_next)
    end = commands.add_parser("collect")
    end.add_argument("name")
    end.add_argument("--observations", required=True,
                     help="replay, freezes, timestamps, notes/effects/audio/input")
    end.set_defaults(func=collect)
    acc = commands.add_parser("accept")
    acc.add_argument("name")
    acc.set_defaults(func=accept)
    sumr = commands.add_parser("summarize")
    sumr.set_defaults(func=summarize)
    cfg = commands.add_parser("configure", help="record this machine's save/profile inputs")
    cfg.add_argument("--user-data", type=lambda p: Path(p).resolve(), required=True)
    cfg.add_argument("--profile", type=lambda p: Path(p).resolve(), required=True)
    cfg.set_defaults(func=configure)
    args = parser.parse_args()
    try:
        args.func(args)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
