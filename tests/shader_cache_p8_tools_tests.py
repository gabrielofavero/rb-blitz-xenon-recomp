"""P8 acceptance tests: a row's verdict must follow its evidence, not its name.

Synthetic logs only - no game, no GPU and no prepared run. Each case builds the
exact lines the instrument and the loading gate emit and asks the acceptance
checker for its verdict.
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/shaders"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/assets"))
import shader_cache_p8 as p8


def summary(phase, event, count=0, failed=0, total=0, worst=0, over=0):
    return (f"shader_trace: summary snapshot=9 reason=window_close phase={phase} event={event} "
            f"count={count} failed={failed} total_us={total} max_us={worst} ge50={over} "
            f"ge100=0 ge250=0 suppressed=0")


def layers(extraction="reused", recipes=("reused", "reused", "reused", "reused"), repaired=0):
    return ("graphics_loading: layers extraction=" + extraction +
            " recipes=" + "/".join(recipes) + f" repaired={repaired} shaders=208 "
            "records=412/219/409/228")


def release(attempt=1, generation=3, milliseconds=15240, pipelines=432, ignored=False):
    return (f"graphics_loading: attempt {attempt} released generation {generation} exactly once "
            f"({milliseconds} ms, {pipelines} pipelines, ignored={'true' if ignored else 'false'})")


def gate():
    return ("graphics_loading: post-A native loading gate configured settings=0123456789ABCDEF "
            "(27 pipeline-affecting setting(s), cache writable=true)")


def clean_frames():
    return [summary("active_song", "guest_frame", count=600, total=9_000_000, worst=16_000),
            summary("active_song", "gpu_frame", count=600, total=9_100_000, worst=17_000,
                    over=0)]


class Row:
    def __init__(self, row_id, lines, ledger=True, after_cache=True, report=None):
        self.dir = p8.RUNS / row_id
        self.dir.mkdir(parents=True, exist_ok=True)
        (self.dir / "game.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (self.dir / "report.json").write_text(json.dumps(report or {
            "phases": [{"phase": "post_a_loading", "previous": "pre_a", "t_us": "1000000"},
                       {"phase": "menu", "previous": "post_a_loading", "t_us": "1100000"}],
            "long_frames": []}), encoding="utf-8")
        if after_cache:
            cache = self.dir / "after-cache" / "shaders"
            cache.mkdir(parents=True, exist_ok=True)
            if ledger:
                (cache / "preparation-state.json").write_text(json.dumps({
                    "schema": 1, "content_epoch": 1,
                    "extraction": {"complete": True, "entries": 208},
                    "recipes": {name: {"complete": True}
                                for name in ("d3d12/rtv", "d3d12/rov", "vulkan/fbo", "vulkan/fsi")},
                    "last_attempt": {"attempt": 1, "phase": 5, "complete": True,
                                     "required": 432, "succeeded": 432, "failed": 0,
                                     "missing": 0}}), encoding="utf-8")


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.saved = p8.RUNS
        self.saved_matrix = p8.MATRIX
        p8.RUNS = Path(self.folder.name) / "runs"
        p8.MATRIX = Path(self.folder.name)

    def tearDown(self):
        p8.RUNS = self.saved
        p8.MATRIX = self.saved_matrix
        self.folder.cleanup()

    def verdict(self, row_id):
        try:
            p8.accept(type("Args", (), {"name": row_id})())
        except SystemExit as exit_code:
            return exit_code.code
        return 0

    def failing_checks(self, row_id):
        acceptance = json.loads((p8.RUNS / row_id / "acceptance.json").read_text(encoding="utf-8"))
        return [check["check"] for check in acceptance["checks"] if not check["ok"]]

    def test_a_clean_warm_row_passes_and_measures_the_bar(self):
        Row("p8-d3d12-ultimate-rtv-warm",
            [gate(), layers(), summary("boot", "guest_pipeline", count=132),
             summary("active_song", "translation"), summary("active_song", "guest_pipeline"),
             summary("active_song", "helper_graphics")]
            + clean_frames() + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-warm"), 0)
        acceptance = json.loads(
            (p8.RUNS / "p8-d3d12-ultimate-rtv-warm" / "acceptance.json").read_text(encoding="utf-8"))
        self.assertEqual(acceptance["verdict"], "PASS")
        self.assertEqual(acceptance["facts"]["bar"]["post_a_to_menu_ms"], 100.0)
        self.assertEqual(acceptance["facts"]["frames"]["gpu_frame"]["max_ms"], 17.0)

    def test_any_active_song_compilation_event_fails_the_row(self):
        Row("p8-d3d12-ultimate-rtv-warm",
            [gate(), layers(), summary("active_song", "translation", count=3),
             summary("active_song", "placeholder", count=1)] + clean_frames() + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-warm"), 1)
        self.assertIn("no_active_song_compiles",
                      self.failing_checks("p8-d3d12-ultimate-rtv-warm"))

    def test_a_helper_first_use_needs_the_row_to_accept_it(self):
        # A row with no helper policy at all (the Vulkan fsi path) keeps the strict
        # check: any in-song helper creation fails it until that path is measured.
        row_id = "p8-vulkan-ultimate-fsi-warm"
        Row(row_id, [gate(), layers(),
                     summary("active_song", "helper_graphics", count=1, total=876, worst=876)]
            + clean_frames() + [release()])
        self.assertEqual(self.verdict(row_id), 1)
        self.assertIn("no_active_song_helper_creation", self.failing_checks(row_id))

        # A D3D12 row accepts it (owner decision, 2026-10-25) within the budget.
        row_id = "p8-d3d12-ultimate-rtv-warm"
        Row(row_id, [gate(), layers(),
                     summary("active_song", "helper_graphics", count=1, total=876, worst=876)]
            + clean_frames() + [release()])
        self.assertEqual(self.verdict(row_id), 0)
        acceptance = json.loads((p8.RUNS / row_id / "acceptance.json")
                                .read_text(encoding="utf-8"))
        check = {item["check"]: item for item in acceptance["checks"]}[
            "no_active_song_helper_creation"]
        self.assertTrue(check["ok"])
        self.assertTrue(check["detail"]["accepted_by_row_policy"])
        self.assertEqual(check["detail"]["budget"], p8.HELPER_FIRST_USE_BUDGET)

    def test_the_helper_budget_is_bounded(self):
        row_id = "p8-d3d12-ultimate-rtv-cold-r2"
        Row(row_id, [gate(), layers(extraction="rebuilt"),
                     summary("active_song", "helper_graphics", count=2, total=2000, worst=1000)]
            + clean_frames() + [release()])
        self.assertEqual(self.verdict(row_id), 1)
        self.assertIn("no_active_song_helper_creation", self.failing_checks(row_id))
        Row(row_id, [gate(), layers(extraction="rebuilt"),
                     summary("active_song", "helper_graphics", count=1, total=9000, worst=9000)]
            + clean_frames() + [release()])
        self.assertEqual(self.verdict(row_id), 1)
        self.assertIn("no_active_song_helper_creation", self.failing_checks(row_id))

    def test_a_vulkan_host_helper_is_accepted_with_its_numbers(self):
        # The cold Vulkan row's shape: the presenter rebuilds one pipeline per present.
        # 1,473 calls / 209 ms over an 86.6 s song is host noise, not a guest shader,
        # and the acceptance record has to carry the measurement.
        row_id = "p8-vulkan-ultimate-fbo-cold"
        long_frames = [summary("active_song", "guest_frame", count=2600, total=76_600_000,
                               worst=33_000),
                       summary("active_song", "gpu_frame", count=2600, total=76_600_000,
                               worst=33_000)]
        Row(row_id, [gate(), layers(extraction="rebuilt"),
                     summary("active_song", "helper_graphics", count=1473, total=209451,
                             worst=818)] + long_frames + [release()])
        self.assertEqual(self.verdict(row_id), 0)
        acceptance = json.loads((p8.RUNS / row_id / "acceptance.json")
                                .read_text(encoding="utf-8"))
        check = {item["check"]: item for item in acceptance["checks"]}[
            "no_active_song_helper_creation"]
        self.assertTrue(check["ok"])
        self.assertFalse(check["detail"]["accepted_by_row_policy"])
        self.assertEqual(check["detail"]["host_helper_exception"]["total_us"], 209451)
        self.assertLess(check["detail"]["host_helper_exception"]["total_us"],
                        check["detail"]["host_helper_exception"]["allowance_us"])

        # Spending more host time than the song can hide fails the row.
        Row(row_id, [gate(), layers(extraction="rebuilt"),
                     summary("active_song", "helper_graphics", count=1473, total=900_000,
                             worst=818)] + long_frames + [release()])
        self.assertEqual(self.verdict(row_id), 1)
        self.assertIn("no_active_song_helper_creation", self.failing_checks(row_id))

        # One call no cheaper than a compile also fails it.
        Row(row_id, [gate(), layers(extraction="rebuilt"),
                     summary("active_song", "helper_graphics", count=2, total=12_000,
                             worst=6_000)] + long_frames + [release()])
        self.assertEqual(self.verdict(row_id), 1)
        self.assertIn("no_active_song_helper_creation", self.failing_checks(row_id))

    def test_an_unplayable_row_cannot_pass(self):
        # The r3 shape: every frame 200-300 ms with zero compilation work. That
        # is not evidence about preparation, so it must not read as PASS.
        Row("p8-d3d12-ultimate-rtv-cold-r3",
            [gate(), layers(extraction="rebuilt"),
             summary("active_song", "guest_frame", count=299, total=70_901_422, worst=594_328,
                     over=298),
             summary("active_song", "gpu_frame", count=298, total=70_530_521, worst=300_469,
                     over=298)] + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-cold-r3"), 1)
        self.assertIn("frames_healthy",
                      self.failing_checks("p8-d3d12-ultimate-rtv-cold-r3"))

    def test_a_cold_row_must_rebuild_and_a_warm_row_must_reuse(self):
        Row("p8-d3d12-ultimate-rtv-cold",
            [gate(), layers(extraction="reused")] + clean_frames() + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-cold"), 1)
        self.assertIn("layers_rebuilt", self.failing_checks("p8-d3d12-ultimate-rtv-cold"))

    def test_a_repaired_row_needs_a_recorded_repair(self):
        Row("p8-d3d12-ultimate-rtv-corrupt-portable",
            [gate(), layers(repaired=1)] + clean_frames() + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-corrupt-portable"), 0)
        Row("p8-d3d12-ultimate-rtv-corrupt-ledger",
            [gate(), layers(extraction="rebuilt", repaired=0)] + clean_frames() + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-corrupt-ledger"), 1)
        self.assertIn("damage_repaired",
                      self.failing_checks("p8-d3d12-ultimate-rtv-corrupt-ledger"))

    def test_the_combined_damage_row_needs_a_recorded_repair(self):
        row_id = "p8-d3d12-ultimate-rtv-damaged-all"
        # No song is required for this row: reaching the menu after a repair is
        # the evidence, so frames must not be demanded.
        Row(row_id, [gate(), layers(extraction="rebuilt", repaired=5), release()])
        self.assertEqual(self.verdict(row_id), 0)
        self.assertNotIn("frames_recorded", [check["check"] for check in
                                            json.loads((p8.RUNS / row_id /
                                                        "acceptance.json").read_text(
                                                            encoding="utf-8"))["checks"]])

    def test_the_read_only_row_needs_its_reason_and_no_readiness_claim(self):
        Row("p8-d3d12-ultimate-rtv-readonly",
            [gate(), "graphics_loading: the cache root D:/x/cache is not writable; graphics can "
                     "still be prepared for this session, but a later launch will have to "
                     "prepare again",
             "graphics_loading: attempt 1 failed (cache_not_writable)",
             release(ignored=True)], ledger=False, after_cache=False)
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-readonly"), 0)
        Row("p8-d3d12-ultimate-rtv-readonly",
            [gate(), "graphics_loading: attempt 1 failed (cache_not_writable)",
             release(ignored=False)], ledger=False, after_cache=False)
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-readonly"), 1)
        self.assertIn("readiness_not_claimed",
                      self.failing_checks("p8-d3d12-ultimate-rtv-readonly"))

    def test_a_cancelled_attempt_is_evidence_the_row_asks_for(self):
        Row("p8-d3d12-ultimate-rtv-cancel",
            [gate(), layers(extraction="rebuilt"),
             "graphics_loading: attempt 1 cancelled; generation 1 retires and the next attempt "
             "prepares again"] + clean_frames() + [release(attempt=2, generation=2)])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-cancel"), 0)
        Row("p8-d3d12-ultimate-rtv-cancel",
            [gate(), layers(extraction="rebuilt")] + clean_frames() + [release(attempt=2)])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-cancel"), 1)
        self.assertIn("cancel_recorded", self.failing_checks("p8-d3d12-ultimate-rtv-cancel"))

    def test_a_changed_setting_row_is_a_discovery_not_a_gate(self):
        Row("p8-d3d12-ultimate-rtv-config-scale",
            [gate(), layers(), summary("active_song", "guest_pipeline", count=4)]
            + clean_frames() + [release()])
        self.assertEqual(self.verdict("p8-d3d12-ultimate-rtv-config-scale"), 0)
        acceptance = json.loads((p8.RUNS / "p8-d3d12-ultimate-rtv-config-scale" /
                                 "acceptance.json").read_text(encoding="utf-8"))
        names = [check["check"] for check in acceptance["checks"]]
        self.assertIn("coverage_discovery", names)

    def test_summary_never_reports_an_unrun_row_as_passing(self):
        Row("p8-d3d12-ultimate-rtv-warm",
            [gate(), layers()] + clean_frames() + [release()])
        p8.accept(type("Args", (), {"name": "p8-d3d12-ultimate-rtv-warm"})())
        p8.summarize(type("Args", (), {})())
        text = (p8.MATRIX / "summary.md").read_text(encoding="utf-8")
        self.assertIn("p8-d3d12-ultimate-rtv-warm", text)
        self.assertIn("not run", text)
        self.assertIn("untested scope, never as passing", text)


if __name__ == "__main__":
    unittest.main()
