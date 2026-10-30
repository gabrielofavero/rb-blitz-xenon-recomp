"""Hermetic P3 pipeline catalogue tests. Synthetic stores only; never boots a game."""
import argparse
import json
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/shaders"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/assets"))
import shader_pipeline_catalogue as pc
from shader_asset_resolver import sdk_hashes

HELPER = None
ROOT = Path(__file__).resolve().parents[1]


def d3d12_description(vertex=0x1111, pixel=0x2222, vertex_mod=1, pixel_mod=2, state=0,
                      stencil=0, render_targets=(0xFFFFFFFF, 0, 0, 0),
                      depth_bias=0, slope=0.0):
    return struct.pack("<QQQQifII4I", vertex, vertex_mod, pixel, pixel_mod, depth_bias, slope,
                       state, stencil, *render_targets)


def vulkan_description(vertex=0x1111, pixel=0x2222, vertex_mod=1, pixel_mod=2,
                       render_pass_key=0, state=0, stencil=0, render_targets=(0, 0, 0, 0)):
    return struct.pack("<QQQQIII4I", vertex, vertex_mod, pixel, pixel_mod, render_pass_key,
                       state, stencil, *render_targets)


def api_for(backend, path_kind):
    return next(api for api, name in pc.BACKENDS[backend]["apis"].items() if name == path_kind)


def store_bytes(backend, path_kind, descriptions, hashes=None, version=None):
    spec = pc.BACKENDS[backend]
    if hashes is None:
        hashes = sdk_hashes(descriptions, HELPER)
    body = bytearray(struct.pack("<III", pc.STORAGE_MAGIC, api_for(backend, path_kind),
                                 pc.stored_version(version or spec["version"])))
    for description, digest in zip(descriptions, hashes):
        body += struct.pack("<Q", int(digest, 16)) + description
    return bytes(body)


def write_store(folder, name, backend, path_kind, descriptions, **kwargs):
    path = Path(folder) / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(store_bytes(backend, path_kind, descriptions, **kwargs))
    return path


def fake_store(records, backend="d3d12", path_kind="rtv", store_id="s1"):
    return pc.Store(store_id, store_id, backend, path_kind, "sha", 0, records, len(records), 0, 0, None)


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="p3-catalogue-")
        self.folder = Path(self.temp.name)
        self.counter = 0

    def tearDown(self):
        self.temp.cleanup()

    def catalogue_path(self, name="catalogue.json"):
        self.counter += 1
        return self.folder / f"{self.counter}-{name}"

    def run_cli(self, stores, *extra):
        output = self.catalogue_path()
        arguments = []
        for store in stores:
            arguments += ["--store", str(store)]
        return pc.main([*arguments, "--catalogue-out", str(output),
                        "--hash-helper", HELPER,
                        "--helper-inventory", str(self.folder / "helpers.json"),
                        *extra]), output

    def write_helpers(self, entries=None):
        if entries is None:
            entries = [
                {"id": "d3d12.one", "kind": "graphics",
                 "site": "rexglue-sdk/src/ui/d3d12/d3d12_immediate_drawer.cpp",
                 "function": "Initialize", "when": "startup",
                 "trace": {"event": "helper_graphics",
                           "site": "src/ui/d3d12/d3d12_immediate_drawer.cpp:159",
                           "identity": "call_site"},
                 "key": {"fields": []}, "dependencies": ["root signature"], "variants": "1",
                 "coverage": "site_unique"},
            ]
        data = {"schema": 1, "title_id": "5841122D",
                "backends": {"d3d12": {"entries": list(entries)},
                             "vulkan": {"entries": []}}}
        path = self.folder / "helpers.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def test_d3d12_round_trip_and_decode(self):
        first = d3d12_description(vertex=0xA, pixel=0xB, vertex_mod=3, pixel_mod=4,
                                  state=(1 << 11) | (2 << 14) | (1 << 17),
                                  stencil=0x12,
                                  render_targets=(0b1 | (5 << 1) | (0xF << 27), 0, 0, 0))
        second = d3d12_description(vertex=0xC, pixel=0, vertex_mod=5, pixel_mod=0)
        path = write_store(self.folder, "5841122D.rtv.d3d12.xpso", "d3d12", "rtv",
                           [first, second])
        store = pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER), "5841122D")
        self.assertEqual((store.backend, store.path_kind), ("d3d12", "rtv"))
        self.assertEqual(store.valid_records, 2)
        self.assertEqual(store.duplicate_records, 0)
        self.assertEqual(store.records[1].pixel_hash, "", "a zero pixel hash means no pixel shader")
        decoded = pc.decode_d3d12(first)
        self.assertEqual(decoded["vertex"]["hash"], f"{0xA:016x}")
        self.assertEqual(decoded["host_msaa_samples"], 1)
        self.assertEqual(decoded["depth_func"], 2)
        self.assertEqual(decoded["depth_write"], 1)
        self.assertEqual(decoded["stencil_write_mask"], 0x12)
        self.assertEqual(decoded["render_targets"][0]["used"], 1)
        self.assertEqual(decoded["render_targets"][0]["format"], 5)
        self.assertEqual(decoded["render_targets"][0]["write_mask"], 0xF)
        summary = pc.summarize_recipes(pc.merge_stores([store]))
        self.assertEqual(summary["recipes"], 2)
        self.assertEqual(summary["without_pixel_shader"], 1)
        self.assertNotIn("vertex_shaders", summary["axes"], "axes must not duplicate shader lists")
        self.assertEqual(summary["axes"]["host_msaa_samples"], [0, 1])

    def test_vulkan_round_trip_and_decode(self):
        # msaa 2x (bits 0-1), depth used (bit 2), color 0 used (bit 3), color_0
        # format 4 (bits 8-11), color_1 format 6 (bits 12-15), transfer (bit 24).
        render_pass_key = 2 | (1 << 2) | (1 << 3) | (4 << 8) | (6 << 12) | (1 << 24)
        description = vulkan_description(vertex=0xD, pixel=0xE, render_pass_key=render_pass_key,
                                         state=(1 << 5) | (2 << 6),
                                         stencil=(3 << 3) | (1 << 12))
        path = write_store(self.folder, "5841122D.fbo.vk.xpso", "vulkan", "fbo", [description])
        store = pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER), "5841122D")
        self.assertEqual((store.backend, store.path_kind), ("vulkan", "fbo"))
        decoded = pc.decode_vulkan(description)
        self.assertEqual(decoded["primitive_restart"], 1)
        self.assertEqual(decoded["tessellation_mode"], 2)
        self.assertEqual(decoded["sample_rate_shading"], 1)
        self.assertEqual(decoded["stencil_back_pass_op"], 3)
        self.assertEqual(decoded["render_pass"]["msaa_samples"], 2)
        self.assertEqual(decoded["render_pass"]["depth_and_color_used"], 3)
        self.assertEqual(decoded["render_pass"]["color_0_view_format"], 4)
        self.assertEqual(decoded["render_pass"]["color_1_view_format"], 6)
        self.assertEqual(decoded["render_pass"]["color_rts_use_transfer_formats"], 1)
        summary = pc.summarize_recipes(pc.merge_stores([store]))
        # Only color 0 is marked used in the render pass; color_1's format is
        # present in the key but is not an active attachment.
        self.assertEqual(decoded["render_pass"]["color_1_view_format"], 6)
        self.assertEqual(summary["axes"]["render_target_formats"], [4])
        self.assertEqual(summary["axes"]["render_pass_msaa"], [2])

    def test_duplicate_records_and_cross_store_provenance(self):
        description = d3d12_description()
        one = write_store(self.folder, "one.rtv.d3d12.xpso", "d3d12", "rtv",
                          [description, description])
        two = self.folder / "two.rtv.d3d12.xpso"
        two.write_bytes(one.read_bytes())
        stores = [pc.parse_store(path, f"s{index + 1}", lambda codes: sdk_hashes(codes, HELPER),
                                 "5841122D")
                  for index, path in enumerate((one, two))]
        self.assertEqual(stores[0].valid_records, 2)
        self.assertEqual(stores[0].duplicate_records, 1)
        merged = pc.merge_stores(stores)
        self.assertEqual(len(merged), 1)
        self.assertEqual(merged[0].stores, ["s1", "s2"])

    def test_header_and_version_rejections(self):
        description = d3d12_description()
        good = store_bytes("d3d12", "rtv", [description])
        cases = {
            "short": good[:8],
            "magic": b"XXXX" + good[4:],
            "api": good[:4] + struct.pack("<I", 0xDEADBEEF) + good[8:],
            "version": good[:8] + struct.pack("<I", pc.stored_version(0x20200101)) + good[12:],
            "trailing": good + b"\x01\x02\x03",
        }
        for name, data in cases.items():
            path = self.folder / f"{name}.rtv.d3d12.xpso"
            path.write_bytes(data)
            with self.subTest(case=name), self.assertRaises(ValueError):
                pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER), "5841122D")

    def test_title_mismatch_is_refused(self):
        path = write_store(self.folder, "12345678.rtv.d3d12.xpso", "d3d12", "rtv",
                           [d3d12_description()])
        with self.assertRaises(ValueError):
            pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER), "5841122D")

    def test_truncated_prefix_requires_the_flag_and_counts_the_tail(self):
        descriptions = [d3d12_description(vertex=index + 1) for index in range(3)]
        good = bytearray(store_bytes("d3d12", "rtv", descriptions))
        struct.pack_into("<Q", good, 12 + 72 + 8, 0)  # corrupt record 1's stored hash
        good += b"\xAA\xBB\xCC"
        path = self.folder / "bad.rtv.d3d12.xpso"
        path.write_bytes(good)
        with self.assertRaises(ValueError):
            pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER), "5841122D")
        store = pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER), "5841122D",
                               accept_truncated=True)
        self.assertEqual(store.valid_records, 1)
        self.assertEqual(store.first_invalid_index, 1)
        self.assertEqual(store.trailing_bytes, len(good) - 12 - 72)

    def test_hash_collision_refused(self):
        one = pc.RecipeRecord("aaaaaaaaaaaaaaaa", b"\x01" * 64, "1" * 16, "2" * 16)
        two = pc.RecipeRecord("aaaaaaaaaaaaaaaa", b"\x02" * 64, "1" * 16, "2" * 16)
        with self.assertRaises(ValueError):
            pc.merge_stores([fake_store([one], store_id="s1"),
                             fake_store([two], store_id="s2")])
        mismatched = pc.RecipeRecord("bbbbbbbbbbbbbbbb", b"\x01" * 64, "1" * 16, "2" * 16)
        with self.assertRaises(ValueError):
            pc.merge_stores([fake_store([one], store_id="s1"),
                             fake_store([mismatched], store_id="s2")])

    def test_coverage_index_and_candidate_scope(self):
        requests = [("vertex", "1" * 16), ("pixel", "2" * 16)]
        complete = pc.coverage_with_index({("vertex", "1" * 16), ("pixel", "2" * 16)}, requests)
        self.assertTrue(complete["complete"])
        incomplete = pc.coverage_with_index({("vertex", "1" * 16)}, requests)
        self.assertFalse(incomplete["complete"])
        self.assertEqual(incomplete["entries"][1]["reason"], "absent_from_resolver_index")
        self.assertFalse(pc.coverage_with_index(set(), [])["complete"], "an empty set is not coverage")

        candidate = self.folder / "candidate.json"
        candidate.write_text(json.dumps({"schema": 1, "title_id": "5841122D", "shaders": [
            {"stage": "vertex", "hash": "1" * 16}]}), encoding="utf-8")
        self.assertEqual(pc.candidate_identities(candidate), {("vertex", "1" * 16)})
        broken = self.folder / "broken.json"
        broken.write_text(json.dumps({"schema": 1, "shaders": [{"stage": "other",
                                                               "hash": "1" * 16}]}),
                          encoding="utf-8")
        with self.assertRaises(ValueError):
            pc.candidate_identities(broken)

    def test_helper_inventory_validation(self):
        good = self.write_helpers()
        entries = pc.load_helper_inventory(good, "5841122D")
        self.assertEqual(len(entries), 1)
        self.assertEqual(pc.helper_coverage(entries)["complete"], True)

        base = {"id": "d3d12.one", "kind": "graphics", "site": "s.cpp", "function": "f",
                "when": "startup", "trace": {"event": "helper_graphics", "site": "s.cpp:1",
                                             "identity": "call_site"},
                "key": {"fields": []}, "dependencies": ["d"], "variants": "1",
                "coverage": "site_unique"}
        mutations = {
            "bad-kind": {**base, "kind": "text"},
            "bad-coverage": {**base, "coverage": "unknown"},
            "bad-event": {**base, "trace": {**base["trace"], "event": "other"}},
            "bad-site": {**base, "trace": {**base["trace"], "site": "not-a-site"}},
            "no-dependencies": {**base, "dependencies": []},
            "wrong-prefix": {**base, "id": "vulkan.one"},
            "missing-field": {key: value for key, value in base.items() if key != "variants"},
            "key-coverage-without-key-identity": {**base, "coverage": "key_identified"},
            "key-identity-with-site-unique": {
                **base, "trace": {**base["trace"], "identity": "key",
                                   "key_bits": "kind 1 (kDump)"}},
            "key-identity-without-bits": {
                **base, "coverage": "key_identified",
                "trace": {**base["trace"], "identity": "key"}},
        }
        for name, row in mutations.items():
            path = self.write_helpers([row])
            with self.subTest(case=name), self.assertRaises(ValueError):
                pc.load_helper_inventory(path, "5841122D")
        duplicate = self.write_helpers([base, base])
        with self.assertRaises(ValueError):
            pc.load_helper_inventory(duplicate, "5841122D")

    def test_cli_exit_codes_overwrite_and_resolution_modes(self):
        self.write_helpers()
        path = write_store(self.folder, "5841122D.rtv.d3d12.xpso", "d3d12", "rtv",
                           [d3d12_description(), d3d12_description(vertex=3)])
        index = self.folder / "index.json"
        # Resolver rows name shader identities (the ucode hashes), not descriptions.
        index.write_text(json.dumps({"schema": 1, "resolved": [
            {"stage": "vertex", "hash": f"{hash_value:016x}", "sources": []}
            for hash_value in (0x1111, 0x3)
        ] + [{"stage": "pixel", "hash": f"{0x2222:016x}", "sources": []}]}), encoding="utf-8")
        code, output = self.run_cli([path], "--resolver-index", str(index))
        self.assertEqual(code, 0, "site-unique helpers and resolved shaders are complete")
        self.assertTrue(output.exists())
        catalogue = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(catalogue["coverage"]["shaders"]["resolved"], 3)
        self.assertEqual(catalogue["coverage"]["resolution"], "resolver_index")
        self.assertFalse(catalogue["coverage"]["live_ready"])
        self.assertTrue(catalogue["backends"]["d3d12"]["paths"]["rtv"]["captured"])
        self.assertFalse(catalogue["backends"]["d3d12"]["paths"]["rov"]["captured"])
        self.assertIn("d3d12/rov", catalogue["coverage"]["paths_not_captured"])
        self.assertIn("vulkan/fsi", catalogue["coverage"]["paths_not_captured"])

        again = pc.main(["--store", str(path), "--catalogue-out", str(output),
                         "--hash-helper", HELPER, "--helper-inventory", str(self.folder / "helpers.json"),
                         "--resolver-index", str(index)])
        self.assertEqual(again, 1, "an existing catalogue must not be overwritten")

        code, output = self.run_cli([path])
        self.assertEqual(code, 2, "unchecked shader resolution is an explicit blocker")
        catalogue = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(catalogue["coverage"]["resolution"], "not_checked")
        self.assertEqual(catalogue["coverage"]["shaders"]["resolved"], 0)

    def test_repeated_helper_site_is_a_coverage_blocker(self):
        entries = pc.load_helper_inventory(self.write_helpers(), "5841122D")
        repeated = [{**entries[0], "id": "d3d12.repeated", "coverage": "site_repeated"}]
        coverage = pc.helper_coverage(repeated)
        self.assertFalse(coverage["complete"])
        self.assertEqual(coverage["key_identity_unresolved"], ["d3d12.repeated"])

    def test_key_identified_helper_is_not_a_blocker(self):
        row = {"id": "d3d12.keyed", "kind": "compute", "site": "s.cpp", "function": "f",
               "when": "startup",
               "trace": {"event": "helper_compute", "site": "s.cpp:1", "identity": "key",
                         "key_bits": "kind 4 (kDump) << 56 | DumpPipelineKey"},
               "key": {"fields": ["msaa_samples"]}, "dependencies": ["d"], "variants": "many",
               "coverage": "key_identified"}
        entries = pc.load_helper_inventory(self.write_helpers([row]), "5841122D")
        coverage = pc.helper_coverage(entries)
        self.assertTrue(coverage["complete"])
        self.assertEqual(coverage["key_identified"], 1)
        self.assertEqual(coverage["site_repeated"], 0)
        self.assertEqual(coverage["key_identity_unresolved"], [])

    def test_merged_store_is_replayable(self):
        self.write_helpers()
        first = d3d12_description(vertex=1)
        second = d3d12_description(vertex=2)
        one = write_store(self.folder, "one.rtv.d3d12.xpso", "d3d12", "rtv", [first])
        two = write_store(self.folder, "two.rtv.d3d12.xpso", "d3d12", "rtv", [first, second])
        merged = self.folder / "merged.rtv.d3d12.xpso"
        code, output = self.run_cli([one, two], "--merged-store", str(merged))
        self.assertEqual(code, 2, "the fixture helper inventory has no shader resolution")
        self.assertTrue(output.exists())
        data = merged.read_bytes()
        self.assertEqual(struct.unpack_from("<III", data, 0),
                         (pc.STORAGE_MAGIC, api_for("d3d12", "rtv"),
                          pc.stored_version(pc.BACKENDS["d3d12"]["version"])))
        store = pc.parse_store(merged, "s9", lambda codes: sdk_hashes(codes, HELPER), "5841122D")
        self.assertEqual(store.valid_records, 2)
        self.assertEqual(len(pc.merge_stores([store])), 2)

    def test_merged_store_refuses_mixed_backends(self):
        self.write_helpers()
        one = write_store(self.folder, "one.rtv.d3d12.xpso", "d3d12", "rtv", [d3d12_description()])
        two = write_store(self.folder, "two.fbo.vk.xpso", "vulkan", "fbo", [vulkan_description()])
        merged = self.folder / "mixed.xpso"
        code, _output = self.run_cli([one, two], "--merged-store", str(merged))
        self.assertEqual(code, 1)
        self.assertFalse(merged.exists())

    def test_directory_expansion_is_sorted_and_deduplicated(self):
        one = write_store(self.folder / "a", "one.rtv.d3d12.xpso", "d3d12", "rtv",
                          [d3d12_description()])
        two = write_store(self.folder / "b", "two.fbo.vk.xpso", "vulkan", "fbo",
                          [vulkan_description()])
        found = pc.expand_stores([self.folder, one])
        self.assertEqual([path.name for path in found],
                         ["one.rtv.d3d12.xpso", "two.fbo.vk.xpso"])
        self.assertTrue(two.exists())

    def test_sdk_storage_layout_pins(self):
        sdk = ROOT / "rexglue-sdk" / "include" / "rex" / "graphics"

        def description_version(relative):
            text = (sdk / relative).read_text(encoding="utf-8")
            match = re.search(r"REXPACKEDSTRUCT\(PipelineDescription, \{(.*?)\n  \}\);",
                              text, re.S)
            self.assertIsNotNone(match, f"{relative}: PipelineDescription not found")
            version = re.search(r"kVersion = 0x([0-9A-Fa-f]+)", match.group(1))
            self.assertIsNotNone(version, f"{relative}: PipelineDescription kVersion not found")
            return int(version.group(1), 16)

        def translator_version(relative):
            text = (sdk / relative).read_text(encoding="utf-8")
            version = re.search(r"kVersion = (0x[0-9A-Fa-f]+|\d+)", text)
            self.assertIsNotNone(version, f"{relative}: Modification kVersion not found")
            return int(version.group(1), 0)

        d3d12 = max(description_version("d3d12/pipeline_cache.h"),
                    translator_version("pipeline/shader/dxbc_translator.h"))
        vulkan = max(description_version("vulkan/pipeline_cache.h"),
                     translator_version("pipeline/shader/spirv_translator.h"))
        self.assertEqual(pc.BACKENDS["d3d12"]["version"], d3d12,
                         "D3D12 storage version drifted from the SDK headers")
        self.assertEqual(pc.BACKENDS["vulkan"]["version"], vulkan,
                         "Vulkan storage version drifted from the SDK headers")
        self.assertEqual(pc.BACKENDS["d3d12"]["description_size"] + 8, 72)
        self.assertEqual(pc.BACKENDS["vulkan"]["description_size"] + 8, 68)

        # P7: the in-game seed writes the same headers. A translator bump that
        # updates the SDK but not this constant would now be caught here rather
        # than at a player's first boot (where the layer is repaired, but the
        # shipped catalogue would still be described wrongly).
        seed_source = (ROOT / "src" / "graphics" / "preparation_assets.h").read_text(
            encoding="utf-8")
        seed = {name: int(value, 16) for name, value in re.findall(
            r"(kD3D12RecipeStorageVersion|kVulkanRecipeStorageVersion)\s*=\s*0x([0-9A-Fa-f]+)",
            seed_source)}
        self.assertEqual(seed.get("kD3D12RecipeStorageVersion"), d3d12,
                         "the seed's D3D12 storage version drifted from the SDK headers")
        self.assertEqual(seed.get("kVulkanRecipeStorageVersion"), vulkan,
                         "the seed's Vulkan storage version drifted from the SDK headers")

    def test_shipped_helper_inventory_matches_the_sdk_sites(self):
        entries = pc.load_helper_inventory(
            ROOT / "config" / "pipeline_helper_inventory.json", "5841122D")
        self.assertGreaterEqual(len(entries), 20)
        self.assertEqual({entry["backend"] for entry in entries}, {"d3d12", "vulkan"})
        for entry in entries:
            source = ROOT / entry["site"]
            self.assertTrue(source.exists(), f"{entry['id']}: source {entry['site']} is missing")
            site = entry["trace"]["site"]
            match = re.fullmatch(r"(.+):(\d+)", site)
            self.assertIsNotNone(match, f"{entry['id']}: trace site '{site}' is not file:line")
            trace_source = ROOT / "rexglue-sdk" / match.group(1)
            self.assertTrue(trace_source.exists(), f"{entry['id']}: {match.group(1)} is missing")
            self.assertIn(site, trace_source.read_text(encoding="utf-8"),
                          f"{entry['id']}: trace site '{site}' drifted out of the SDK source")

    def test_shipped_helper_keys_match_the_trace_enum(self):
        header = (ROOT / "rexglue-sdk" / "include" / "rex" / "graphics" /
                  "compile_trace.h").read_text(encoding="utf-8")
        kind_enum = {name: int(value)
                     for name, value in re.findall(r"(k\w+)\s*=\s*(\d+)\s*,", header)}
        self.assertIn("kTransfer", kind_enum, "HelperKind drifted out of the trace header")
        entries = pc.load_helper_inventory(
            ROOT / "config" / "pipeline_helper_inventory.json", "5841122D")
        self.assertFalse([entry["id"] for entry in entries
                          if entry["coverage"] == "site_repeated"],
                         "a helper recipe still carries no key and remains a coverage blocker")
        keyed = [entry for entry in entries if entry["coverage"] == "key_identified"]
        self.assertGreaterEqual(len(keyed), 20, "the keyed helper set shrank unexpectedly")
        for entry in keyed:
            match = re.match(r"kind (\d+) \((k\w+)\)", entry["trace"]["key_bits"])
            self.assertIsNotNone(match, f"{entry['id']}: key_bits do not name the helper kind")
            kind, name = int(match.group(1)), match.group(2)
            self.assertIn(name, kind_enum, f"{entry['id']}: {name} is not a HelperKind")
            self.assertEqual(kind_enum[name], kind,
                             f"{entry['id']}: {name} moved in the trace header")

    @unittest.skipUnless((ROOT / "out" / "shader-cache-p0" / "runs").is_dir(),
                         "no preserved P0 stores in this checkout")
    def test_preserved_p0_stores(self):
        stores = sorted((ROOT / "out" / "shader-cache-p0" / "runs").rglob("*.xpso"))
        self.assertGreaterEqual(len(stores), 2)
        parsed = 0
        for path in stores:
            store = pc.parse_store(path, "s1", lambda codes: sdk_hashes(codes, HELPER),
                                   "5841122D")
            parsed += 1
            record_size = pc.BACKENDS[store.backend]["record_size"]
            self.assertEqual(store.trailing_bytes, 0, f"{path}: unverified tail")
            self.assertEqual(store.valid_records * record_size + 12, store.bytes)
        self.assertGreater(parsed, 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--hash-helper", required=True)
    arguments, remaining = parser.parse_known_args()
    HELPER = arguments.hash_helper
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)
