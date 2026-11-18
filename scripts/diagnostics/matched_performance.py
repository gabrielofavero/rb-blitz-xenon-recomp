"""Attach-only Windows performance capture; never launches or drives the game.

Raw samples, PresentMon CSV and tool identities remain beside each report. Frame
quantiles require PresentMon v2 rows; the game's one-second probe cannot supply
them. Use schedule to create an inert, alternating cold/warm run inventory.
"""
import argparse
import csv
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

PRESENTMON_SHA256 = "364e5d98d4d134bd54dd25c22ed2ca2f4883f8bc3ed6502bee0c151e3436d30c"
PRESENTMON_SOURCE = "https://github.com/GameTechDev/PresentMon/releases/tag/v2.3.1"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def utc_now():
    return datetime.now(timezone.utc).isoformat()


class MemoryCounters(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [
        (name, C.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
            "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


def filetime(value):
    return (value.dwHighDateTime << 32) | value.dwLowDateTime


class WindowsSampler:
    def __init__(self, pid=None):
        if sys.platform != "win32":
            raise RuntimeError("Windows process/QPC capture requires Windows")
        self.api = C.WinDLL("kernel32", use_last_error=True)
        self.psapi = C.WinDLL("psapi", use_last_error=True)
        signatures = {
            "OpenProcess": ([W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
            "CloseHandle": ([W.HANDLE], W.BOOL),
            "GetProcessTimes": ([W.HANDLE] + [C.POINTER(W.FILETIME)] * 4, W.BOOL),
            "GetSystemTimes": ([C.POINTER(W.FILETIME)] * 3, W.BOOL),
            "GetExitCodeProcess": ([W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
            "QueryPerformanceCounter": ([C.POINTER(C.c_longlong)], W.BOOL),
            "QueryPerformanceFrequency": ([C.POINTER(C.c_longlong)], W.BOOL),
            "QueryFullProcessImageNameW": ([W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)], W.BOOL),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = args, result
        self.psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(MemoryCounters), W.DWORD]
        self.psapi.GetProcessMemoryInfo.restype = W.BOOL
        frequency = C.c_longlong()
        self.check(self.api.QueryPerformanceFrequency(C.byref(frequency)))
        self.frequency = frequency.value
        self.handle = None
        self.image = None
        if pid is not None:
            # Query information + VM read, no terminate/suspend/write access.
            self.handle = self.api.OpenProcess(0x0400 | 0x0010, False, pid)
            self.check(self.handle)
            size = W.DWORD(32768)
            path = C.create_unicode_buffer(size.value)
            self.check(self.api.QueryFullProcessImageNameW(self.handle, 0, path, C.byref(size)))
            self.image = path.value

    @staticmethod
    def check(value):
        if not value:
            raise C.WinError(C.get_last_error())

    def sample(self):
        before = time.perf_counter_ns()
        qpc = C.c_longlong()
        self.check(self.api.QueryPerformanceCounter(C.byref(qpc)))
        utc = utc_now()
        after = time.perf_counter_ns()
        idle, kernel, user = W.FILETIME(), W.FILETIME(), W.FILETIME()
        self.check(self.api.GetSystemTimes(C.byref(idle), C.byref(kernel), C.byref(user)))
        row = dict(monotonic_ns=(before + after) // 2, utc=utc, qpc=qpc.value,
                   clock_bracket_ns=after - before, system_idle_100ns=filetime(idle),
                   system_kernel_100ns=filetime(kernel), system_user_100ns=filetime(user))
        if self.handle:
            creation, exit_time, kernel, user = (W.FILETIME() for _ in range(4))
            self.check(self.api.GetProcessTimes(self.handle, C.byref(creation), C.byref(exit_time),
                                               C.byref(kernel), C.byref(user)))
            code = W.DWORD()
            self.check(self.api.GetExitCodeProcess(self.handle, C.byref(code)))
            row.update(creation_100ns=filetime(creation), exit_100ns=filetime(exit_time),
                       process_cpu_100ns=filetime(kernel) + filetime(user), exit_code=code.value)
            memory = MemoryCounters()
            memory.cb = C.sizeof(memory)
            available = self.psapi.GetProcessMemoryInfo(self.handle, C.byref(memory), memory.cb)
            for field, source in (("working_set_bytes", "WorkingSetSize"),
                                  ("peak_working_set_bytes", "PeakWorkingSetSize"),
                                  ("private_bytes", "PrivateUsage"),
                                  ("peak_commit_bytes", "PeakPagefileUsage")):
                row[field] = getattr(memory, source) if available else None
        return row

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def presentmon_command(tool, pid, output, duration, session):
    if sha256(tool) != PRESENTMON_SHA256:
        raise ValueError("PresentMon must match the pinned official 2.3.1 x64 binary")
    return [str(Path(tool).resolve()), "--process_id", str(pid), "--output_file", str(output),
            "--no_console_stats", "--qpc_time", "--v2_metrics", "--session_name", session,
            "--timed", str(math.ceil(duration)), "--terminate_after_timed", "--terminate_on_proc_exit"]


def decode_console(raw):
    return raw.decode("utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8",
                      errors="replace")


def finish_presentmon(presentmon, metadata, tool, session, output, cleanup=False):
    """Close this capture's ETW session after normal target exit, then drain it."""
    recorder = metadata["presentmon"]
    normal_exit = metadata.get("stop_reason") == "process exited" and metadata.get("target_exit_code") == 0
    if (normal_exit or cleanup) and presentmon.poll() is None and "session_stop" not in recorder:
        if not re.fullmatch(r"rb-blitz-[0-9a-f]{32}", session) or recorder.get("session_name") != session:
            raise ValueError("Refusing to stop an unowned PresentMon session")
        if sha256(tool) != PRESENTMON_SHA256:
            raise ValueError("PresentMon release hash changed before session stop")
        command = [str(Path(tool).resolve()), "--session_name", session, "--terminate_existing_session"]
        stop = recorder["session_stop"] = dict(command=command, raw_console="presentmon-stop-console.raw",
            purpose="normal target exit" if normal_exit else "cleanup after " + metadata.get("stop_reason", "sampler exception"))
        raw_path = output / stop["raw_console"]
        try:
            with raw_path.open("xb") as console:
                result = subprocess.run(command, stdout=console, stderr=subprocess.STDOUT,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), timeout=10)
            stop["exit_code"] = result.returncode
            # Process exit can close the session between poll() and this CLI.
            text = decode_console(raw_path.read_bytes())
            missing = "error: no existing sessions found: " + session
            errors = [line.strip() for line in text.splitlines() if "error:" in line.lower()]
            race = (result.returncode == 7 and presentmon.poll() == 0 and errors == [missing])
            stop["recorder_already_exited_race"] = race
            if (result.returncode != 0 or errors) and not race:
                stop["failed"] = True
        except (OSError, subprocess.TimeoutExpired) as error:
            stop.update(failed=True, error=str(error))
        finally:
            if raw_path.is_file():
                stop["raw_console_sha256"] = sha256(raw_path)
    try:
        recorder["exit_code"] = presentmon.wait(timeout=10)
    except subprocess.TimeoutExpired:
        # Terminate only our own recorder; this capture remains unacceptable.
        presentmon.terminate()
        recorder["exit_code"] = presentmon.wait(timeout=5)
        recorder["forced_stop"] = True


def capture_quality(folder):
    metadata = json.loads((Path(folder) / "capture.json").read_text(encoding="utf-8"))
    presentmon = metadata.get("presentmon")
    issues = []
    if metadata.get("stop_reason") != "process exited":
        issues.append("process sampling did not observe normal process exit")
    if metadata.get("target_exit_code", 0) != 0:
        issues.append("target process exited with an error")
    for name, expected in metadata.get("raw_files", {}).items():
        path = Path(folder) / name
        if not path.is_file() or sha256(path) != expected:
            issues.append("raw capture file changed: " + name)
    if not presentmon:
        issues.append("PresentMon was not enabled")
    else:
        if presentmon.get("exit_code") != 0 or presentmon.get("forced_stop"):
            issues.append("PresentMon did not finish successfully")
        stop = presentmon.get("session_stop")
        if stop:
            raw = Path(folder) / "presentmon-stop-console.raw"
            if (stop.get("failed") or not raw.is_file() or
                    sha256(raw) != stop.get("raw_console_sha256")):
                issues.append("PresentMon owned-session stop failed or its diagnostics changed")
        console = Path(folder) / "presentmon-console.raw"
        if not console.is_file():
            issues.append("raw PresentMon console missing")
        elif re.search(r"(?:\d+ ETW (?:events|buffers) were lost|\berror:)", decode_console(console.read_bytes()), re.I):
            issues.append("PresentMon reports lost ETW events or an error")
        if not (Path(folder) / "presentmon.csv").is_file():
            issues.append("PresentMon emitted no frame CSV")
    return dict(acceptance_issues=issues, raw_capture_complete=not issues,
                caveat="complete capture still requires idle-host, matched-route and segment validation")


def capture(args):
    if args.interval <= 0 or args.duration <= 0:
        raise ValueError("interval and duration must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sampler = WindowsSampler(args.pid)
    metadata = dict(schema=1, started_utc=utc_now(), pid=args.pid, image=sampler.image,
                    image_sha256=sha256(sampler.image) if sampler.image else None,
                    sampler_sha256=sha256(__file__), qpc_frequency=sampler.frequency,
                    logical_cpus=os.cpu_count(), interval_seconds=args.interval,
                    maximum_seconds=args.duration, platform=sys.platform,
                    argv=sys.argv, purpose="attach-only; no game launch or UI input")
    presentmon = None
    console = None
    try:
        if args.presentmon:
            if args.pid is None:
                raise ValueError("PresentMon requires the actual rendering child --pid")
            session = "rb-blitz-" + uuid.uuid4().hex
            command = presentmon_command(args.presentmon, args.pid, output / "presentmon.csv",
                                         args.duration, session)
            metadata["presentmon"] = dict(command=command, sha256=PRESENTMON_SHA256,
                                         source=PRESENTMON_SOURCE, session_name=session)
            console = (output / "presentmon-console.raw").open("xb")
            presentmon = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT,
                                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        start = time.perf_counter()
        rows = 0
        sample_slot = 0
        with (output / "process.csv").open("x", newline="", encoding="utf-8") as stream:
            writer = None
            while True:
                row = sampler.sample()
                if writer is None:
                    writer = csv.DictWriter(stream, fieldnames=list(row))
                    writer.writeheader()
                writer.writerow(row)
                stream.flush()
                rows += 1
                if args.pid is not None and row["exit_100ns"]:
                    metadata["stop_reason"] = "process exited"
                    metadata["target_exit_code"] = row["exit_code"]
                    break
                elapsed = time.perf_counter() - start
                if elapsed >= args.duration:
                    metadata["stop_reason"] = "time limit; target was not terminated"
                    break
                # Keep absolute cadence, skipping missed slots rather than catching up.
                sample_slot = max(sample_slot + 1, math.floor(elapsed / args.interval) + 1)
                time.sleep(max(0, min(start + sample_slot * args.interval - time.perf_counter(),
                                      args.duration - elapsed)))
        metadata["sample_rows"] = rows
        if presentmon:
            finish_presentmon(presentmon, metadata, args.presentmon, session, output, cleanup=True)
    finally:
        sampler.close()
        if presentmon and presentmon.poll() is None:
            try:
                finish_presentmon(presentmon, metadata, args.presentmon, session, output, cleanup=True)
            except Exception as error:
                metadata["presentmon"]["cleanup_error"] = str(error)
            if presentmon.poll() is None:
                presentmon.terminate()
                metadata["presentmon"]["exit_code"] = presentmon.wait(timeout=5)
                metadata["presentmon"]["forced_stop"] = True
        if console:
            console.close()
            raw = (output / "presentmon-console.raw").read_bytes()
            (output / "presentmon-console.txt").write_text(decode_console(raw), encoding="utf-8")
        metadata["finished_utc"] = utc_now()
        metadata["raw_files"] = {p.name: sha256(p) for p in output.iterdir() if p.is_file()}
        write_json(output / "capture.json", metadata)
    if presentmon:
        quality = capture_quality(output)
        write_json(output / "quality.json", quality)
        if quality["acceptance_issues"]:
            raise RuntimeError("capture incomplete: " + "; ".join(quality["acceptance_issues"]) +
                               "; raw console and process samples preserved")


def nearest_rank(values, percentile):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * percentile) - 1)] if values else None


def numeric(value):
    try:
        result = float(value)
        return result if math.isfinite(result) and result >= 0 else None
    except (TypeError, ValueError):
        return None


def summarize_values(values):
    return dict(samples=len(values), median=nearest_rank(values, .5),
                p95=nearest_rank(values, .95), p99=nearest_rank(values, .99),
                maximum=max(values) if values else None)


def analyze_presentmon(path, pid=None, swapchain=None, qpc_begin=None, qpc_end=None,
                       budget_ms=1000 / 60, tolerance_ms=.05):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"ProcessID", "SwapChainAddress", "CPUStartQPC", "FrameTime", "DisplayedTime"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("expected PresentMon 2.3.1 --v2_metrics --qpc_time CSV")
        rows = list(reader)
    if (qpc_begin is None) != (qpc_end is None) or (qpc_begin is not None and qpc_begin >= qpc_end):
        raise ValueError("provide an increasing --qpc-begin/--qpc-end pair")
    pids = sorted({r["ProcessID"] for r in rows})
    if pid is None and len(pids) != 1:
        raise ValueError("select --pid explicitly for empty or multiple-process CSV")
    pid = str(pid) if pid is not None else pids[0]
    process_rows = [r for r in rows if r["ProcessID"] == pid]
    chains = sorted({r["SwapChainAddress"] for r in process_rows})
    if swapchain is None and len(chains) != 1:
        raise ValueError("select --swapchain explicitly for empty or multiple-swapchain CSV")
    swapchain = swapchain or chains[0]
    selected = []
    for row in process_rows:
        if row["SwapChainAddress"] != swapchain:
            continue
        try:
            qpc = int(row["CPUStartQPC"])
        except ValueError:
            continue
        if qpc_begin is None or qpc_begin <= qpc < qpc_end:
            selected.append(row)
    if not selected:
        raise ValueError("no frames match the explicit capture selection")
    metrics = {name: summarize_values([value for row in selected
                                     if (value := numeric(row.get(name))) is not None])
               for name in ("FrameTime", "CPUBusy", "CPUWait", "GPUTime", "GPUBusy",
                            "GPUWait", "DisplayLatency", "DisplayedTime")}
    frames = [value for row in selected if (value := numeric(row["FrameTime"])) is not None]
    return dict(source_sha256=sha256(path), process_id=pid, swapchain=swapchain,
                qpc_begin=qpc_begin, qpc_end=qpc_end, source_rows=len(rows), selected_rows=len(selected),
                invalid_frame_time_rows=len(selected) - len(frames), metrics_ms=metrics,
                percentile_method="nearest rank", frame_budget_ms=budget_ms,
                budget_tolerance_ms=tolerance_ms,
                budget_misses=sum(v > budget_ms + tolerance_ms for v in frames),
                stalls={str(limit): sum(v >= limit for v in frames) for limit in (50, 100, 250)},
                not_displayed_rows=sum(numeric(r["DisplayedTime"]) is None for r in selected),
                presentation_modes=sorted({r.get("PresentMode", "") for r in selected}),
                runtimes=sorted({r.get("PresentRuntime", "") for r in selected}))


def analyze_process(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) < 2:
        raise ValueError("at least two process samples are required")
    elapsed = (int(rows[-1]["monotonic_ns"]) - int(rows[0]["monotonic_ns"])) / 1e9
    host = []
    for before, after in zip(rows, rows[1:]):
        total = sum(int(after[k]) - int(before[k]) for k in ("system_kernel_100ns", "system_user_100ns"))
        idle = int(after["system_idle_100ns"]) - int(before["system_idle_100ns"])
        if total > 0:
            host.append((100 * (1 - idle / total), total))
    result = dict(source_sha256=sha256(path), samples=len(rows), observed_seconds=elapsed,
                  host_busy_percent_mean=(sum(p * w for p, w in host) / sum(w for _, w in host)) if host else None,
                  host_busy_percent_max=max((p for p, _ in host), default=None))
    if "process_cpu_100ns" in rows[0]:
        identities = {r["creation_100ns"] for r in rows}
        if len(identities) != 1:
            raise ValueError("process creation identity changed")
        cpu = (int(rows[-1]["process_cpu_100ns"]) - int(rows[0]["process_cpu_100ns"])) / 1e7
        result.update(process_cpu_observed_seconds=cpu,
                      process_cpu_core_percent=100 * cpu / elapsed if elapsed else None,
                      process_cpu_lifetime_seconds=int(rows[-1]["process_cpu_100ns"]) / 1e7,
                      process_exited=bool(int(rows[-1]["exit_100ns"])))
        for name in ("working_set_bytes", "private_bytes", "peak_working_set_bytes", "peak_commit_bytes"):
            values = [int(r[name]) for r in rows if r.get(name)]
            result[name + "_maximum"] = max(values, default=None)
    return result


def analyze_logs(path):
    lines = Path(path).read_text(encoding="utf-8-sig", errors="replace").splitlines()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shaders"))
    import shader_cache_trace as trace
    # The maintained report deliberately retains uncertainty and cumulative counters.
    import tempfile
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "trace.json"
        trace.report_file(Path(path), output)
        compiled = json.loads(output.read_text(encoding="utf-8"))
    audio = [line for line in lines if re.search(
        r"Audio (?:starvation|queue totals|device largest callback gap|queue overflow)", line)]
    cache = [line for line in lines if "native pipeline cache" in line.lower()]
    probe = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in lines if "framerate_probe:" in line]
    totals, gaps, phase_intervals = [], [], []
    for line in audio:
        total = re.search(r"Audio queue totals: (\d+) submitted, (\d+) starved frame\(s\)", line)
        if total:
            totals.append(dict(submitted_frames=int(total[1]), starved_frames=int(total[2]), raw=line))
        gap = re.search(r"(?:Audio device largest callback gap|largest callback gap) ([\d.]+) ms", line)
        if gap:
            gaps.append(dict(milliseconds=float(gap[1]), raw=line))
    for before, after in zip(compiled["phases"], compiled["phases"][1:]):
        if "t_us" in before and "t_us" in after:
            phase_intervals.append(dict(phase=before["phase"], begin_us=int(before["t_us"]),
                end_us=int(after["t_us"]), duration_us=int(after["t_us"]) - int(before["t_us"])))
    return dict(source_sha256=sha256(path), trace=compiled, audio_records=audio,
                audio_queue_totals=totals, audio_callback_gaps=gaps, phase_intervals=phase_intervals,
                native_cache_records=cache, framerate_windows=probe,
                limitations=["audio totals may be per-client or aggregate; never sum cumulative warnings",
                             "framerate windows and thresholded trace details cannot supply full frame quantiles",
                             "song_loading includes route/press-A waits; active_song is an input-enabled proxy"])


def schedule(pairs, backends):
    if pairs < 5:
        raise ValueError("matched acceptance requires at least five pairs per backend and cache state")
    result = []
    for backend in backends:
        for pair in range(1, pairs + 1):
            for build in (("baseline", "upgraded") if pair % 2 else ("upgraded", "baseline")):
                cold = f"{backend}-pair{pair:02d}-{build}-cold"
                for state in ("cold", "warm"):
                    result.append(dict(name=f"{backend}-pair{pair:02d}-{build}-{state}", backend=backend,
                                       pair=pair, build=build, cache_state=state,
                                       warm_from=cold if state == "warm" else None,
                                       workloads=["startup", "post_start_preparation", "song_load",
                                                  "steady_song", "menu_music_loading"],
                                       status="pending"))
    return dict(schema=1, created_utc=utc_now(), pairs=pairs, runs=result,
                requirement="same-build cold cache seeds its warm run only after a valid completed route")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("capture", help="attach sampler and optional pinned PresentMon; never launch game")
    run.add_argument("--pid", type=int)
    run.add_argument("--presentmon", type=Path)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--interval", type=float, default=.25)
    run.add_argument("--duration", type=float, default=600)
    report = commands.add_parser("analyze")
    report.add_argument("--log", type=Path)
    report.add_argument("--samples", type=Path)
    report.add_argument("--presentmon", type=Path)
    report.add_argument("--capture", type=Path, help="capture directory for ETW integrity/exit checks")
    report.add_argument("--pid", type=int)
    report.add_argument("--swapchain")
    report.add_argument("--qpc-begin", type=int)
    report.add_argument("--qpc-end", type=int)
    report.add_argument("--budget-ms", type=float, default=1000 / 60)
    report.add_argument("--tolerance-ms", type=float, default=.05)
    report.add_argument("--output", type=Path, required=True)
    plan = commands.add_parser("schedule", help="write inert ordered run inventory")
    plan.add_argument("--pairs", type=int, default=5)
    plan.add_argument("--backend", action="append", choices=("d3d12", "vulkan"))
    plan.add_argument("--output", type=Path, required=True)
    idle = commands.add_parser("idle", help="sample host CPU only and record a bounded idle preflight")
    idle.add_argument("--output", type=Path, required=True)
    idle.add_argument("--duration", type=float, default=15)
    idle.add_argument("--interval", type=float, default=.5)
    idle.add_argument("--mean-limit", type=float, default=5)
    idle.add_argument("--peak-limit", type=float, default=10)
    mark = commands.add_parser("mark", help="append an operator/harness workload boundary; no UI input")
    mark.add_argument("--label", required=True)
    mark.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "capture":
        capture(args)
    elif args.command == "idle":
        args.pid, args.presentmon = None, None
        capture(args)
        result = analyze_process(args.output / "process.csv")
        result.update(mean_limit=args.mean_limit, peak_limit=args.peak_limit)
        result["idle"] = (result["host_busy_percent_mean"] is not None and
                          result["host_busy_percent_mean"] <= args.mean_limit and
                          result["host_busy_percent_max"] <= args.peak_limit)
        write_json(args.output / "idle.json", result)
        if not result["idle"]:
            raise SystemExit(2)
    elif args.command == "schedule":
        write_json(args.output, schedule(args.pairs, list(dict.fromkeys(args.backend or ["d3d12", "vulkan"]))))
    elif args.command == "mark":
        clock = WindowsSampler()
        try:
            row = clock.sample()
            row.update(label=args.label, qpc_frequency=clock.frequency)
            with args.output.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(row) + "\n")
        finally:
            clock.close()
    else:
        result = dict(schema=1, generated_utc=utc_now(), analyzer_sha256=sha256(__file__),
                      present_frame_quantiles="unmeasured")
        if args.samples:
            result["process"] = analyze_process(args.samples)
        if args.log:
            result["game"] = analyze_logs(args.log)
        if args.presentmon:
            if args.budget_ms <= 0 or args.tolerance_ms < 0:
                parser.error("budget must be positive and tolerance nonnegative")
            if args.capture and args.presentmon.resolve() != (args.capture / "presentmon.csv").resolve():
                parser.error("--presentmon must refer to the specified --capture directory")
            result["presentmon"] = analyze_presentmon(args.presentmon, args.pid, args.swapchain,
                args.qpc_begin, args.qpc_end, args.budget_ms, args.tolerance_ms)
            result["present_frame_quantiles"] = "measured from selected PresentMon rows only"
        if args.capture:
            result["capture_quality"] = capture_quality(args.capture)
        if len(result) == 4:
            parser.error("analyze needs --samples, --log or --presentmon")
        write_json(args.output, result)


if __name__ == "__main__":
    main()
