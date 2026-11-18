"""P0 report attribution tests: uncertainty must survive incomplete evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/shaders"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/assets"))
import shader_cache_trace as trace


class ReportTests(unittest.TestCase):
    def report(self, lines):
        with tempfile.TemporaryDirectory() as folder:
            log, output = Path(folder) / "game.log", Path(folder) / "report.json"
            log.write_text("\n".join(lines), encoding="utf-8")
            trace.report_file(log, output)
            return json.loads(output.read_text(encoding="utf-8"))

    def test_temporal_overlap_is_not_claimed_as_cause(self):
        report = self.report([
            "shader_trace: connect_state panel=123 previous=18 requested=35 t_us=99 lr=123",
            "shader_trace: event=translation phase=active_song end_phase=active_song site=vulkan key=1 begin_us=100 end_us=200 duration_us=100 success=true",
            "shader_trace: event=gpu_frame phase=active_song end_phase=active_song site=swap key=0 begin_us=50 end_us=50050 duration_us=50000 success=true",
            "shader_trace: event=compile_wait phase=active_song end_phase=active_song site=wait key=0 begin_us=50050 end_us=50060 duration_us=10 success=true",
        ])
        frame = report["long_frames"][0]
        self.assertEqual(report["phases"], [])
        self.assertEqual([e["event"] for e in frame["overlapping_work"]], ["translation"])
        self.assertIn("causation unproven", frame["assessment"])

    def test_no_overlap_stays_unresolved_and_capped_details_are_visible(self):
        report = self.report([
            "shader_trace: summary snapshot=1 reason=periodic phase=active_song event=translation count=2048 suppressed=0",
            "shader_trace: summary snapshot=2 reason=periodic phase=active_song event=translation count=3000 suppressed=952",
            "shader_trace: event=guest_frame phase=active_song end_phase=active_song site=swap key=0 begin_us=1 end_us=300001 duration_us=300000 success=true",
        ])
        self.assertTrue(report["detail_truncated"])
        self.assertEqual(report["latest_cumulative_counters"]["active_song/translation"]["count"], "3000")
        self.assertIn("unresolved", report["long_frames"][0]["assessment"])

    def test_non_compile_gpu_operation_preserves_site_and_uncertainty(self):
        report = self.report([
            "shader_trace: event=gpu_operation phase=active_song end_phase=active_song site=d3d12.queue_execute key=42 begin_us=100 end_us=800100 duration_us=800000 success=true",
            "shader_trace: event=gpu_frame phase=active_song end_phase=active_song site=swap key=0 begin_us=50 end_us=800150 duration_us=800100 success=true",
        ])
        frame = report["long_frames"][0]
        self.assertEqual(frame["overlapping_work"][0]["event"], "gpu_operation")
        self.assertEqual(frame["overlapping_work"][0]["site"], "d3d12.queue_execute")
        self.assertIn("causation unproven", frame["assessment"])

    def test_run_names_cannot_escape_out(self):
        for name in ["../cache", "..", "D:/normal-cache", "", "a/b"]:
            with self.assertRaises(ValueError):
                trace.run_dir(name)

    def test_file_path_and_producer_thread_survive_quoted_site(self):
        source = 'cnt0001:\\songs\\Bounce Man\\a "quoted"\n.mogg'
        report = self.report([
            '[info] [core] [t123] shader_trace: event=file_read phase=active_song '
            f'end_phase=active_song site={json.dumps(source)} key=FF variant=100 '
            'begin_us=100 end_us=100100 duration_us=100000 success=true',
            '[info] [core] [t456] shader_trace: event=gpu_frame phase=active_song '
            'end_phase=active_song site="swap" key=0 begin_us=1 end_us=200001 '
            'duration_us=200000 success=true',
        ])
        frame = report['long_frames'][0]
        self.assertEqual(frame['thread_id'], 456)
        self.assertEqual(frame['overlapping_work'][0]['thread_id'], 123)
        self.assertEqual(frame['overlapping_work'][0]['site'], source)
        self.assertIn('causation unproven', frame['assessment'])

    def test_partial_records_are_reported_without_losing_usable_evidence(self):
        report = self.report([
            'shader_trace: event=gpu_frame phase=active_song end_phase=active_song '
            'site=swap key=0 begin_us=1 end_us=100001 duration_us=100000 success=true',
            'shader_trace: event=file_read site="truncated path',
            'shader_trace: event=file_read phase=active_song begin_us=500',
        ])
        self.assertEqual(len(report['long_frames']), 1)
        self.assertEqual([r['line'] for r in report['damaged_trace_lines']], [2, 3])
        self.assertIn('unresolved', report['long_frames'][0]['assessment'])


class CaptureArgumentsTests(unittest.TestCase):
    def arguments(self, extra):
        args = type("CaptureArgs", (), dict(game=Path("game"), backend="vulkan",
                    variant="ultimate", game_arg=extra))()
        return trace.capture_arguments(Path("rb_blitz.exe"), args, Path("run"), Path("run/cache"))

    def test_default_log_level_is_exactly_one_info_option(self):
        argv = self.arguments([])
        self.assertEqual([arg for arg in argv if arg.startswith("--log_level")], ["--log_level=info"])
        self.assertIn("--game_data_root=game", argv)
        self.assertIn("--ultimate_mode=1", argv)

    def test_explicit_level_replaces_default_without_changing_other_arguments(self):
        extra = ["--probe_framerate=true", "--log_level=debug", "--vsync=true"]
        argv = self.arguments(extra)
        self.assertEqual([arg for arg in argv if arg.startswith("--log_level")], ["--log_level=debug"])
        self.assertEqual(argv[-len(extra):], extra)
        self.assertIn("--game_data_root=game", argv)
        self.assertIn("--log_file="+str(Path("run/game.log")), argv)

    def test_duplicate_or_malformed_explicit_log_levels_are_rejected(self):
        for extra in (["--log_level=info", "--log_level=debug"],
                      ["--log_level=debug", "--log_level=debug"], ["--log_level"],
                      ["--log_level="], ["--log_level=debug=info"], ["--log_level=debug info"]):
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, "at most one"):
                self.arguments(extra)

    def test_protected_capture_paths_remain_unconditionally_rejected(self):
        for option in ("--cache_root", "--user_data_root", "--launcher_profile", "--log_file"):
            with self.subTest(option=option), self.assertRaisesRegex(ValueError, "isolated"):
                self.arguments(["--log_level=debug", option+"=other"])


if __name__ == "__main__":
    unittest.main()
