"""Validate organic coverage promotion with real SDK hashes and synthetic recipes."""
import argparse
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/shaders"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/assets"))
import shader_coverage as coverage
import shader_pipeline_catalogue as pc
import shader_pipeline_catalogue_tests as fixtures

HELPER = None
COLLECTOR = None


def manifest():
    return {"schema": 1, "title_id": "5841122D", "paths": {p: [] for p in coverage.PATHS},
            "sources": [{"stage": "vertex", "hash": "0000000000001111"},
                        {"stage": "pixel", "hash": "0000000000002222"}]}


def record(raw=None, path="vulkan/fbo"):
    raw = raw or fixtures.vulkan_description()
    return {"schema": 1, "title_id": "5841122D", "path": path,
            "version": coverage.PATHS[path]["version"], "raw": raw.hex(),
            "hash": pc.sdk_hashes([raw], HELPER)[0]}


class CoverageToolsTests(unittest.TestCase):
    def load(self, lines):
        with tempfile.TemporaryDirectory() as directory:
            journal = Path(directory) / "journal.jsonl"
            journal.write_text("\n".join(lines)+"\n")
            return coverage.load_discoveries(journal, lambda codes: pc.sdk_hashes(codes, HELPER))

    def test_deduplication_and_truncated_line_recovery(self):
        row = record()
        discoveries, damaged = self.load(["{unfinished", json.dumps(row), json.dumps(row)])
        self.assertEqual(len(discoveries), 1)
        self.assertEqual(damaged, [1])

    def test_all_paths_merge_without_changing_original_or_other_records(self):
        rows = [record(fixtures.d3d12_description() if p.startswith("d3d12/") else None, p)
                for p in coverage.PATHS]
        discoveries, _ = self.load([json.dumps(row) for row in rows])
        original = manifest()
        result, additions, missing = coverage.merge(original, discoveries)
        self.assertEqual(additions, {p: 1 for p in coverage.PATHS})
        self.assertFalse(missing)
        self.assertTrue(all(not entries for entries in original["paths"].values()))
        repeated, second, _ = coverage.merge(result, discoveries)
        self.assertEqual(result, repeated)
        self.assertFalse(second)

    def test_corrupt_hash_stale_generation_and_unknown_path_refused(self):
        for changes in ({"hash": "0000000000000000"}, {"version": 0}, {"path": "vulkan/unknown"}):
            row = record()
            row.update(changes)
            with self.assertRaises(ValueError):
                self.load([json.dumps(row)])

    def test_missing_sources_are_derived_from_description(self):
        row = record(fixtures.vulkan_description(vertex=0x3333))
        row["unresolved_sources"] = []  # Never trust the journal's old catalogue assessment.
        discoveries, _ = self.load([json.dumps(row)])
        _, _, missing = coverage.merge(manifest(), discoveries)
        self.assertEqual(missing, [("vertex", "0000000000003333")])

    def test_unresolved_source_blocks_candidate_and_output_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, journal, output = (root / name for name in ("manifest.json", "journal.jsonl", "candidate.json"))
            source.write_text(json.dumps(manifest()))
            journal.write_text(json.dumps(record(fixtures.vulkan_description(vertex=0x3333)))+"\n")
            args = ["--journal", str(journal), "--manifest", str(source), "--hash-helper", HELPER,
                    "--candidate-out", str(output)]
            self.assertEqual(coverage.main(args), 2)
            self.assertFalse(output.exists())
            journal.write_text(json.dumps(record())+"\n")
            self.assertEqual(coverage.main(args), 0)
            written = output.read_bytes()
            self.assertEqual(coverage.main(args), 1)
            self.assertEqual(output.read_bytes(), written)

    def test_runtime_collector_to_promotion_round_trip(self):
        if not COLLECTOR:
            self.skipTest("collector executable not supplied")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, journal = root / "manifest.json", root / "journal.jsonl"
            cache = root / "cache"
            stores = cache / "shaders/shareable"
            stores.mkdir(parents=True)
            source.write_text(json.dumps(manifest()))
            raw = fixtures.vulkan_description()
            (stores / "5841122D.fbo.vk.xpso").write_bytes(fixtures.store_bytes("vulkan", "fbo", [raw]))
            for _ in range(2):
                ran = subprocess.run([COLLECTOR, "--scan", str(source), str(cache), str(journal)],
                                     capture_output=True, text=True)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            rows = journal.read_text().splitlines()
            self.assertEqual(len(rows), 1)
            discoveries, damaged = coverage.load_discoveries(journal, lambda codes: pc.sdk_hashes(codes, HELPER))
            result, additions, missing = coverage.merge(manifest(), discoveries)
            self.assertFalse(damaged)
            self.assertFalse(missing)
            self.assertEqual(additions, {"vulkan/fbo": 1})
            self.assertEqual(result["paths"]["vulkan/fbo"], [
                {"hash": pc.sdk_hashes([raw], HELPER)[0], "raw": raw.hex()}])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--hash-helper", required=True)
    parser.add_argument("--collector")
    args, rest = parser.parse_known_args()
    HELPER = args.hash_helper
    fixtures.HELPER = HELPER
    COLLECTOR = args.collector
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)
