"""Capture attribution, frame selection and isolated matched-run ordering."""
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

TOOL = Path(__file__).resolve().parents[1] / "scripts/diagnostics/matched_performance.py"
spec = importlib.util.spec_from_file_location("matched_performance", TOOL)
perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf)


class PerformanceTests(unittest.TestCase):
    def scheduled_capture(self, sample_costs, duration=2, interval=.25, exit_after=None):
        clock = SimpleNamespace(now=0.0)
        sampled_at, sleeps = [], []
        sampler = Mock(image=None, frequency=1000)

        def sample():
            index = len(sampled_at)
            clock.now += sample_costs[index] if index < len(sample_costs) else 0
            sampled_at.append(clock.now)
            exited = exit_after is not None and len(sampled_at) >= exit_after
            return dict(utc=str(clock.now), monotonic_ns=round(clock.now * 1e9),
                        qpc=round(clock.now * 1000), exit_100ns=int(exited), exit_code=0 if exited else 259)

        def sleep(delay):
            self.assertGreaterEqual(delay, 0)
            sleeps.append(delay)
            clock.now += delay

        sampler.sample.side_effect = sample
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "capture"
            args = SimpleNamespace(interval=interval, duration=duration, output=output,
                                   pid=123, presentmon=None)
            with patch.object(perf, "WindowsSampler", return_value=sampler), \
                 patch.object(perf.time, "perf_counter", side_effect=lambda: clock.now), \
                 patch.object(perf.time, "sleep", side_effect=sleep):
                perf.capture(args)
            metadata = json.loads((output / "capture.json").read_text())
            with (output / "process.csv").open(newline="") as stream:
                rows = list(csv.DictReader(stream))
        sampler.close.assert_called_once()
        self.assertEqual([float(row["utc"]) for row in rows], sampled_at)
        self.assertEqual([int(row["qpc"]) for row in rows], [round(value * 1000) for value in sampled_at])
        self.assertEqual(metadata["sample_rows"], len(sampled_at))
        return sampled_at, sleeps, metadata

    def test_capture_stall_skips_missed_absolute_slots_without_catchup_rows(self):
        sampled_at, sleeps, metadata = self.scheduled_capture([0, 0, 1])
        self.assertEqual(sampled_at, [0, .25, 1.5, 1.75, 2])
        self.assertEqual(sleeps, [.25, .25, .25, .25])
        self.assertEqual(metadata["stop_reason"], "time limit; target was not terminated")

    def test_capture_normal_cadence_stays_absolute_despite_sample_cost(self):
        sampled_at, sleeps, metadata = self.scheduled_capture([.05] * 5, duration=1)
        for actual, expected in zip(sampled_at, [.05, .30, .55, .80, 1.05]):
            self.assertAlmostEqual(actual, expected)
        self.assertEqual(len(sampled_at), 5)
        for delay in sleeps:
            self.assertAlmostEqual(delay, .20)
        self.assertEqual(metadata["stop_reason"], "time limit; target was not terminated")

    def test_capture_duration_deadline_and_normal_exit_keep_actual_final_sample(self):
        sampled_at, _, metadata = self.scheduled_capture([], duration=.6)
        self.assertEqual(sampled_at, [0, .25, .5, .6])
        self.assertEqual(metadata["stop_reason"], "time limit; target was not terminated")
        sampled_at, sleeps, metadata = self.scheduled_capture([], duration=10, exit_after=3)
        self.assertEqual(sampled_at, [0, .25, .5])
        self.assertEqual(sleeps, [.25, .25])
        self.assertEqual(metadata["stop_reason"], "process exited")
        self.assertEqual(metadata["target_exit_code"], 0)

    def finish_fixture(self, folder):
        session = "rb-blitz-" + "a" * 32
        metadata = dict(stop_reason="process exited", target_exit_code=0,
                        presentmon=dict(session_name=session))
        recorder = Mock()
        recorder.poll.return_value = None
        recorder.wait.return_value = 0
        return session, metadata, recorder, Path(folder)

    def stop_cli(self, code=0, text=""):
        def run(command, **kwargs):
            self.assertEqual(command[-3:], ["--session_name", "rb-blitz-" + "a" * 32,
                                            "--terminate_existing_session"])
            kwargs["stdout"].write(text.encode())
            return subprocess.CompletedProcess(command, code)
        return run

    def test_owned_session_stop_preserves_command_exit_and_raw_console(self):
        with tempfile.TemporaryDirectory() as folder:
            session, metadata, recorder, output = self.finish_fixture(folder)
            with patch.object(perf, "sha256", return_value=perf.PRESENTMON_SHA256), \
                 patch.object(perf.subprocess, "run", side_effect=self.stop_cli()) as stop:
                perf.finish_presentmon(recorder, metadata, "pinned.exe", session, output)
            stop.assert_called_once()
            self.assertEqual(metadata["presentmon"]["exit_code"], 0)
            self.assertEqual(metadata["presentmon"]["session_stop"]["exit_code"], 0)
            self.assertTrue((output / "presentmon-stop-console.raw").is_file())
            self.assertNotIn("forced_stop", metadata["presentmon"])
            recorder.terminate.assert_not_called()

    def test_recorder_already_exited_or_abnormal_target_never_stops_session(self):
        for already_exited, target_code in ((0, 0), (1, 0), (None, 1)):
            with tempfile.TemporaryDirectory() as folder:
                session, metadata, recorder, output = self.finish_fixture(folder)
                recorder.poll.return_value = already_exited
                metadata["target_exit_code"] = target_code
                with patch.object(perf.subprocess, "run") as stop:
                    perf.finish_presentmon(recorder, metadata, "pinned.exe", session, output)
                stop.assert_not_called()

    def test_session_missing_race_requires_exact_error_and_clean_recorder_exit(self):
        for recorder_code, message, accepted in ((0, "error: no existing sessions found: ", True),
                (1, "error: no existing sessions found: ", False), (0, "error: failed to terminate: ", False)):
            with tempfile.TemporaryDirectory() as folder:
                session, metadata, recorder, output = self.finish_fixture(folder)
                recorder.poll.side_effect = [None, recorder_code]
                with patch.object(perf, "sha256", return_value=perf.PRESENTMON_SHA256), \
                     patch.object(perf.subprocess, "run", side_effect=self.stop_cli(7, message + session + "\n")):
                    perf.finish_presentmon(recorder, metadata, "pinned.exe", session, output)
                stop = metadata["presentmon"]["session_stop"]
                self.assertEqual(stop["recorder_already_exited_race"], accepted)
                self.assertEqual(bool(stop.get("failed")), not accepted)

    def test_stop_failure_or_recorder_timeout_remains_unacceptable(self):
        with tempfile.TemporaryDirectory() as folder:
            session, metadata, recorder, output = self.finish_fixture(folder)
            recorder.wait.side_effect = [subprocess.TimeoutExpired("recorder", 10), 1]
            with patch.object(perf, "sha256", return_value=perf.PRESENTMON_SHA256), \
                 patch.object(perf.subprocess, "run", side_effect=self.stop_cli(7, "error: failed stop\n")):
                perf.finish_presentmon(recorder, metadata, "pinned.exe", session, output)
            self.assertTrue(metadata["presentmon"]["forced_stop"])
            self.assertTrue(metadata["presentmon"]["session_stop"]["failed"])
            recorder.terminate.assert_called_once()
            (output / "capture.json").write_text(json.dumps(metadata))
            (output / "presentmon-console.raw").write_bytes(b"Started recording.\n")
            (output / "presentmon.csv").write_text("placeholder")
            self.assertFalse(perf.capture_quality(output)["raw_capture_complete"])

    def test_unowned_session_is_never_stopped(self):
        with tempfile.TemporaryDirectory() as folder:
            session, metadata, recorder, output = self.finish_fixture(folder)
            with patch.object(perf.subprocess, "run") as stop:
                with self.assertRaisesRegex(ValueError, "unowned"):
                    perf.finish_presentmon(recorder, metadata, "pinned.exe", "PresentMon", output)
            stop.assert_not_called()

    def test_exception_or_time_limit_cleanup_stops_only_owned_session_and_cannot_pass(self):
        for reason in (None, "time limit; target was not terminated"):
            with tempfile.TemporaryDirectory() as folder:
                session, metadata, recorder, output = self.finish_fixture(folder)
                metadata.pop("stop_reason")
                if reason:
                    metadata["stop_reason"] = reason
                with patch.object(perf, "sha256", return_value=perf.PRESENTMON_SHA256), \
                     patch.object(perf.subprocess, "run", side_effect=self.stop_cli()) as stop:
                    perf.finish_presentmon(recorder, metadata, "pinned.exe", session, output, cleanup=True)
                stop.assert_called_once()
                self.assertIn("cleanup after", metadata["presentmon"]["session_stop"]["purpose"])
                (output / "capture.json").write_text(json.dumps(metadata))
                (output / "presentmon-console.raw").write_bytes(b"Started recording.\n")
                (output / "presentmon.csv").write_text("placeholder")
                self.assertFalse(perf.capture_quality(output)["raw_capture_complete"])

    def test_stop_timeout_is_recorded_before_forced_recorder_cleanup(self):
        with tempfile.TemporaryDirectory() as folder:
            session, metadata, recorder, output = self.finish_fixture(folder)
            recorder.wait.side_effect = [subprocess.TimeoutExpired("recorder", 10), 1]
            with patch.object(perf, "sha256", return_value=perf.PRESENTMON_SHA256), \
                 patch.object(perf.subprocess, "run", side_effect=subprocess.TimeoutExpired("stop", 10)):
                perf.finish_presentmon(recorder, metadata, "pinned.exe", session, output, cleanup=True)
            self.assertTrue(metadata["presentmon"]["session_stop"]["failed"])
            self.assertTrue(metadata["presentmon"]["forced_stop"])

    def test_capture_exception_preserves_original_failure_and_closes_owned_session(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "capture"
            sampler = Mock(image=None, frequency=100)
            sampler.sample.side_effect = RuntimeError("original sampler failure")
            recorder = Mock()
            recorder.poll.side_effect = [None, None, 0]
            recorder.wait.return_value = 0
            args = SimpleNamespace(interval=.25, duration=10, output=output, pid=123, presentmon="pinned.exe")
            def stop_cli(command, **kwargs):
                self.assertEqual(command[-1], "--terminate_existing_session")
                kwargs["stdout"].write(b"stopped own session\n")
                return subprocess.CompletedProcess(command, 0)
            with patch.object(perf, "WindowsSampler", return_value=sampler), \
                 patch.object(perf, "sha256", return_value=perf.PRESENTMON_SHA256), \
                 patch.object(perf.subprocess, "Popen", return_value=recorder), \
                 patch.object(perf.subprocess, "run", side_effect=stop_cli) as stop:
                with self.assertRaisesRegex(RuntimeError, "original sampler failure"):
                    perf.capture(args)
            stop.assert_called_once()
            recorder.terminate.assert_not_called()
            metadata = json.loads((output / "capture.json").read_text())
            self.assertEqual(metadata["presentmon"]["session_stop"]["exit_code"], 0)
            self.assertNotIn("stop_reason", metadata)
    def make_csv(self, path, rows):
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    def frame(self, qpc, duration, pid="22", chain="0x1", displayed="16.67"):
        return dict(ProcessID=pid, SwapChainAddress=chain, CPUStartQPC=str(qpc),
                    FrameTime=str(duration), CPUBusy="NA", GPUTime="5.5",
                    DisplayedTime=displayed, PresentRuntime="DXGI", PresentMode="Composed: Flip")

    def test_nearest_rank_uses_full_selected_rows_and_retains_not_displayed(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "present.csv"
            rows = [self.frame(n, n, displayed="NA" if n == 99 else "16.67") for n in range(1, 101)]
            self.make_csv(path, rows)
            result = perf.analyze_presentmon(path, qpc_begin=1, qpc_end=101)
            metric = result["metrics_ms"]["FrameTime"]
            self.assertEqual((metric["median"], metric["p95"], metric["p99"]), (50, 95, 99))
            self.assertEqual(result["selected_rows"], 100)
            self.assertEqual(result["not_displayed_rows"], 1)
            self.assertEqual(result["budget_misses"], 84)
            self.assertEqual(result["stalls"], {"50": 51, "100": 1, "250": 0})
            self.assertEqual(result["metrics_ms"]["CPUBusy"]["samples"], 0)
            self.assertIsNone(result["metrics_ms"]["CPUBusy"]["p99"])

    def test_mixed_processes_or_swapchains_require_selection(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "present.csv"
            self.make_csv(path, [self.frame(1, 500, "21"), self.frame(2, 16),
                                 self.frame(3, 18, chain="0x2"), self.frame(4, 20)])
            with self.assertRaisesRegex(ValueError, "--pid"):
                perf.analyze_presentmon(path)
            with self.assertRaisesRegex(ValueError, "--swapchain"):
                perf.analyze_presentmon(path, pid=22)
            result = perf.analyze_presentmon(path, 22, "0x1", 2, 4)
            self.assertEqual(result["selected_rows"], 1)
            self.assertEqual(result["metrics_ms"]["FrameTime"]["median"], 16)
            with self.assertRaisesRegex(ValueError, "increasing"):
                perf.analyze_presentmon(path, 22, "0x1", 5, 4)
            with self.assertRaisesRegex(ValueError, "no frames"):
                perf.analyze_presentmon(path, 22, "0x1", 5, 6)

    def test_wrong_schema_and_missing_frame_values_fail_or_remain_unknown(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "present.csv"
            self.make_csv(path, [dict(ProcessID="22", MsBetweenPresents="16")])
            with self.assertRaisesRegex(ValueError, "v2_metrics"):
                perf.analyze_presentmon(path)
            self.make_csv(path, [self.frame(1, "NA"), self.frame(2, "nan"), self.frame(3, 16.70)])
            result = perf.analyze_presentmon(path)
            self.assertEqual(result["invalid_frame_time_rows"], 2)
            self.assertEqual(result["budget_misses"], 0)  # 0.05 ms declared tolerance
            self.assertEqual(result["metrics_ms"]["FrameTime"]["samples"], 1)

    def test_cpu_units_system_idle_and_os_peak_are_distinct(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "process.csv"
            def row(t, cpu, idle, kernel, user, working, peak):
                return dict(monotonic_ns=t, process_cpu_100ns=cpu, creation_100ns=7,
                            exit_100ns=0, system_idle_100ns=idle, system_kernel_100ns=kernel,
                            system_user_100ns=user, working_set_bytes=working,
                            private_bytes=working * 2, peak_working_set_bytes=peak,
                            peak_commit_bytes=peak * 2)
            self.make_csv(path, [row(1_000_000_000, 30_000_000, 100, 200, 100, 10, 100),
                                 row(3_000_000_000, 40_000_000, 150, 300, 200, 20, 100)])
            result = perf.analyze_process(path)
            self.assertEqual(result["process_cpu_observed_seconds"], 1)
            self.assertEqual(result["process_cpu_lifetime_seconds"], 4)
            self.assertEqual(result["process_cpu_core_percent"], 50)
            self.assertEqual(result["host_busy_percent_mean"], 75)
            self.assertEqual(result["working_set_bytes_maximum"], 20)
            self.assertEqual(result["peak_working_set_bytes_maximum"], 100)

    def test_schedule_alternates_builds_and_seeds_warm_only_from_same_build(self):
        result = perf.schedule(5, ["d3d12", "vulkan"])
        self.assertEqual(len(result["runs"]), 40)
        self.assertEqual([r["build"] for r in result["runs"][:8]],
                         ["baseline", "baseline", "upgraded", "upgraded",
                          "upgraded", "upgraded", "baseline", "baseline"])
        for row in result["runs"]:
            self.assertEqual(row["status"], "pending")
            if row["cache_state"] == "warm":
                cold = next(r for r in result["runs"] if r["name"] == row["warm_from"])
                self.assertEqual((cold["backend"], cold["build"], cold["pair"]),
                                 (row["backend"], row["build"], row["pair"]))
        with self.assertRaisesRegex(ValueError, "five"):
            perf.schedule(4, ["d3d12"])

    def test_etw_lost_events_or_missing_frame_csv_prevent_acceptance(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "capture.json").write_text(json.dumps(dict(stop_reason="process exited",
                presentmon=dict(exit_code=0))), encoding="utf-8")
            (root / "presentmon-console.raw").write_bytes("warning: 12 ETW events were lost.\n".encode("utf-16"))
            quality = perf.capture_quality(root)
            self.assertFalse(quality["raw_capture_complete"])
            self.assertEqual(len(quality["acceptance_issues"]), 2)
            self.assertIn("lost ETW", quality["acceptance_issues"][0])
            (root / "presentmon-console.raw").write_bytes("Started recording.\nStopped recording.\n".encode())
            (root / "presentmon.csv").write_text("placeholder", encoding="utf-8")
            self.assertTrue(perf.capture_quality(root)["raw_capture_complete"])
            (root / "presentmon-console.raw").write_bytes(b"warning: 2 ETW buffers were lost.\n")
            self.assertFalse(perf.capture_quality(root)["raw_capture_complete"])

    def test_trace_summary_is_cumulative_and_audio_records_are_not_summed(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "game.log"
            path.write_text("\n".join([
                "shader_trace: summary snapshot=1 phase=active_song event=gpu_frame count=20 suppressed=0",
                "shader_trace: summary snapshot=2 phase=active_song event=gpu_frame count=50 suppressed=0",
                "Audio starvation: 3 missing frame(s) (16 ms)",
                "Audio starvation: 5 missing frame(s) (26 ms)",
                "Audio queue totals: 100 submitted, 5 starved frame(s)",
                "Audio device largest callback gap 7.2 ms",
                "framerate_probe: window_ms=1000 guest_iterations=60 guest_present_calls=60",
            ]), encoding="utf-8")
            result = perf.analyze_logs(path)
            self.assertEqual(result["trace"]["latest_cumulative_counters"]["active_song/gpu_frame"]["count"], "50")
            self.assertEqual(len(result["audio_records"]), 4)
            self.assertEqual(len(result["framerate_windows"]), 1)
            self.assertNotIn("p99", result)

    @unittest.skipUnless(sys.platform == "win32", "native Windows API sampler")
    def test_attach_sampler_observes_exit_and_preserves_raw_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "capture"
            child = subprocess.Popen([sys.executable, "-c", "import time; p=bytearray(1048576); time.sleep(.6)"])
            try:
                run = subprocess.run([sys.executable, str(TOOL), "capture", "--pid", str(child.pid),
                                      "--interval", ".05", "--duration", "3", "--output", str(output)],
                                     capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, run.stderr)
                metadata = json.loads((output / "capture.json").read_text())
                self.assertEqual(metadata["stop_reason"], "process exited")
                self.assertGreater(metadata["qpc_frequency"], 0)
                result = perf.analyze_process(output / "process.csv")
                self.assertTrue(result["process_exited"])
                self.assertGreater(result["samples"], 2)
                self.assertGreater(result["peak_working_set_bytes_maximum"], 0)
            finally:
                child.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
