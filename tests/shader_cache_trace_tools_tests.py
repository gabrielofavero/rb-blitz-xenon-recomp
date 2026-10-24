"""P0 report attribution tests: uncertainty must survive incomplete evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
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

    def test_run_names_cannot_escape_out(self):
        for name in ["../cache", "..", "D:/normal-cache", "", "a/b"]:
            with self.assertRaises(ValueError):
                trace.run_dir(name)


if __name__ == "__main__":
    unittest.main()
