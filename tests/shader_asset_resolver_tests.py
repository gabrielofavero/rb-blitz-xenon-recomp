"""Hermetic P2 tests. Synthetic XOBX/ARK/XSH bytes only; never boots a game."""
import argparse
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/shaders"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/assets"))
import hmx_ark
import shader_asset_resolver as r
import shader_vertex_link as vl
import shader_xex_source as xs

HELPER = None


def vertex_fixture():
    image = bytearray(256)
    declaration = [(0, 0, 0x2a23b9, 0, 0, 0, 0), (0, 12, 0x2c23a5, 0, 5, 0, 0)]
    for index, e in enumerate(declaration + [(255, 0, 0xffffffff, 0, 0, 0, 0)]):
        struct.pack_into(">HHIBBBB", image, 16 + index * 12, *e)
    struct.pack_into(">8H", image, 128, 0xe000, 0x1c00, 0x380, 0x70, 8, 10, 8, 14)
    image[144 + 37], image[144 + 57] = 2, 3
    manifest = {"schema": 1, "title_id": "5841122D", "linker": "d3d9-sub_82521908-v1",
                "swizzle_masks_offset": 128, "format_sizes_offset": 144,
                "layouts": [{"name": "synthetic", "declaration_offset": 16, "strides": [5]}],
                "bootstrap_shaders": []}
    return image, manifest


def xex_fixture():
    data = bytearray(0x220)
    data[:4] = b"XEX2"
    struct.pack_into(">I", data, 8, 0x210)
    struct.pack_into(">4I", data, 16, 0x40, 1, 0x3ff, 0x30)
    struct.pack_into(">IHHII", data, 0x30, 16, 0, 1, 16, 16)
    struct.pack_into(">I", data, 0x44, 32)
    data[0x210:] = bytes(range(16))
    return data


def hashes(codes):
    return r.sdk_hashes(codes, HELPER)


def fixture(pixel=bytes(range(36)), vertex=bytes(range(36, 72)), variants=2):
    def obj(stage, code):
        header = 40 if stage == "pixel" else 872
        # A constants prefix tests the code descriptor; a whole-bank hash is wrong.
        metadata_size = 48 if stage == "pixel" else 80
        data = bytearray(header + metadata_size)
        struct.pack_into(">III", data, header, 0x102A1100 + (stage == "vertex"),
                         metadata_size, len(code) + 16)
        struct.pack_into(">I", data, header + 24, 36)
        struct.pack_into(">II", data, header + 36, 16, len(code))
        return bytes(data), bytes(16) + code + bytes(12)
    pm, pc = obj("pixel", pixel)
    vm, vc = obj("vertex", vertex)
    data = b"XOBX" + struct.pack("<III", 1, 1, 4) + b"test" + struct.pack("<I", variants)
    data += struct.pack("<II", len(pm), len(pc)) + pm + pc
    data += struct.pack("<II", len(vm), len(vc)) + vm + vc
    # Duplicate bank offsets are legal; duplicate permutation keys are not.
    return data + b"".join(struct.pack("<Q4I", key, 0, 0, 0, 0) for key in range(variants))


def xsh(records):
    values = hashes([code for _, code in records])
    return b"XESH\x20\x20\x12\x19" + b"".join(
        struct.pack("<QI", int(value, 16), len(code) // 4 | (stage == "pixel") << 31) + code
        for (stage, code), value in zip(records, values))


def write_archive(root, name, records, compressed=False):
    root = Path(root)
    files, data = [], bytearray()
    for path, raw in records:
        blob = zlib.compress(raw) if compressed else raw
        files.append(hmx_ark.FileEntry(len(data), path.encode(), None, len(blob),
                                       len(raw) if compressed else 0))
        data += blob
    folder = root / "gen"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{name}_0.ark").write_bytes(data)
    header = hmx_ark.encode_index(files, f"gen/{name}_0.ark", len(data), bytes(20), bytes(4), 12345)
    (folder / f"{name}.hdr").write_bytes(header)


class ResolverTests(unittest.TestCase):
    def test_parser_stages_permutations_and_constants(self):
        programs = r.parse_xobx(fixture())
        self.assertEqual(len(programs), 4)
        self.assertEqual([(p.stage, p.permutation) for p in programs],
                         [("pixel", 0), ("vertex", 0), ("pixel", 1), ("vertex", 1)])
        self.assertEqual(programs[0].code, bytes(range(36)))
        self.assertEqual(programs[1].code, bytes(range(36, 72)))

    def test_every_truncation_and_trailing_byte_is_rejected(self):
        data = fixture()
        for length in range(len(data)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                r.parse_xobx(data[:length])
        with self.assertRaises(ValueError):
            r.parse_xobx(data + b"\0")

    def test_bad_headers_lengths_offsets_and_duplicates(self):
        data = fixture()
        cases = [(0, 0), (4, 2), (8, 0), (8, 257), (12, 129), (20, 0),
                 (20, 0xffffffff), (24, 0xffffffff)]
        pm = 32
        ph = pm + 40
        cases += [(ph, 0x00112a10), (ph + 4, 35), (ph + 8, 0xffffffff),
                  (ph + 24, 0xffffffff), (ph + 36, 0xffffffff), (ph + 40, 0),
                  (ph + 40, r.MAX_MICROCODE + 4), (ph + 40, 7)]
        for offset, value in cases:
            bad = bytearray(data)
            struct.pack_into(">I" if ph <= offset < ph + 44 else "<I", bad, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                r.parse_xobx(bad)
        for delta, value in [(24, 0), (16, 1), (12, 0xfffffff0)]:
            bad = bytearray(data)
            struct.pack_into("<I", bad, len(data) - delta, value)
            with self.subTest(delta=delta), self.assertRaises(ValueError):
                r.parse_xobx(bad)
        # Do not accidentally classify a vertex template as an uploaded vertex.
        bad = bytearray(data)
        bad[16:20] = b"bad/"
        with self.assertRaises(ValueError):
            r.parse_xobx(bad)

    def test_sdk_hash_matches_fixed_xxh3_and_raw_byte_order(self):
        self.assertEqual(hashes([b"abcd"]), ["6497a96f53a89890"])
        self.assertNotEqual(hashes([b"abcd"]), hashes([b"dcba"]))
        for payload in (b"\1", struct.pack("<I", 0), struct.pack("<I", 0xffffffff),
                        struct.pack("<I", 4) + b"abc"):
            result = subprocess.run([str(HELPER)], input=payload, capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_resolution_is_asset_only_and_stage_aware(self):
        resolver = r.Resolver({"synthetic": (fixture(), {})}, hashes)
        pixel, vertex = hashes([bytes(range(36)), bytes(range(36, 72))])
        self.assertEqual(resolver.resolve("pixel", pixel), bytes(range(36)))
        self.assertIsNone(resolver.resolve("vertex", pixel))
        self.assertIsNone(resolver.resolve("vertex", vertex))
        coverage = resolver.coverage([("pixel", pixel, bytes(range(36))),
                                      ("vertex", vertex, bytes(range(36, 72)))])
        self.assertEqual((coverage["required"], coverage["resolved"], coverage["unresolved"]), (2, 1, 1))
        self.assertFalse(coverage["complete"])
        self.assertFalse(resolver.coverage([])["complete"])
        self.assertFalse(resolver.index()["live_ready"])
        self.assertEqual(len(resolver.entries[("pixel", pixel)]), 2)
        with self.assertRaises(ValueError):
            resolver.coverage([("pixel", pixel, b"wrong")])

    def test_fingerprint_and_collision(self):
        first = r.Resolver({"a": (fixture(), {})}, hashes).index()
        same = r.Resolver({"a": (fixture(), {"archive": "elsewhere"})}, hashes).index()
        other = r.Resolver({"a": (fixture(pixel=bytes(36)), {})}, hashes).index()
        self.assertEqual(first["content_fingerprint"], same["content_fingerprint"])
        self.assertNotEqual(first["content_fingerprint"], other["content_fingerprint"])
        # Two different pixel programs deliberately assigned the same hash.
        with self.assertRaises(ValueError):
            r.Resolver({"a": (fixture(), {}), "b": (fixture(pixel=bytes(36)), {})},
                       lambda codes: ["0" * 16 for _ in codes])

    def test_runtime_validation_lengths_version_hash_and_deduplication(self):
        data = xsh([("pixel", bytes(range(36)))])
        self.assertEqual(len(r.parse_xsh(data + data[8:], hashes)), 1)
        for bad in (data[:7], data + b"\0", data[:-1], b"bad!" + data[4:],
                    data[:4] + bytes(4) + data[8:],
                    data[:8] + bytes(8) + data[16:],
                    data[:16] + struct.pack("<I", 0xffffffff) + data[20:]):
            with self.subTest(data=bad[:20]), self.assertRaises(ValueError):
                r.parse_xsh(bad, hashes)

    def test_catalogue_versions_stage_and_identity(self):
        def cat(rows):
            return {"schema": 1, "title_id": "5841122D", "shaders": rows}
        row = {"stage": "pixel", "hash": "A" * 16}
        self.assertEqual(r.catalogue_requests(cat([row, row])), [("pixel", "a" * 16, None)])
        for bad in ({}, {**cat([]), "schema": 2}, cat([{}]), cat([None]),
                    cat([{**row, "stage": "geometry"}]), cat([{**row, "hash": "xyz"}])):
            with self.assertRaises(ValueError):
                r.catalogue_requests(bad)

    def test_installed_retail_overlay_and_merged_precedence(self):
        with tempfile.TemporaryDirectory() as folder:
            base, overlay = Path(folder) / "base", Path(folder) / "overlay"
            original, modified = fixture(), fixture(pixel=bytes(36))
            write_archive(base, "main_xbox", [(name, original) for name in r.ASSET_NAMES])
            write_archive(overlay, "patch_xbox", [("xbox_shaders", modified)], compressed=True)
            retail, _ = r.installed_assets(base)
            ultimate, _ = r.installed_assets(base, overlay)
            self.assertEqual(retail["xbox_shaders"][0], original)
            self.assertEqual(ultimate["xbox_shaders"][0], modified)
            self.assertEqual(ultimate["xbox_preinit_shaders"][0], original)
            self.assertNotEqual(r.Resolver(retail, hashes).index()["content_fingerprint"],
                                r.Resolver(ultimate, hashes).index()["content_fingerprint"])
            write_archive(base, "patch_xbox", [("xbox_shaders", modified)])
            merged, _ = r.installed_assets(base)
            self.assertEqual(merged["xbox_shaders"][0], modified)

    def test_installed_truncated_duplicate_missing_and_inflate_bomb(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            records = [(name, fixture()) for name in r.ASSET_NAMES]
            with self.assertRaises(ValueError):
                r.installed_assets(root)
            write_archive(root, "main_xbox", records)
            part = root / "gen/main_xbox_0.ark"
            part.write_bytes(part.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                r.installed_assets(root)
            write_archive(root, "main_xbox", records + [records[0]])
            with self.assertRaises(ValueError):
                r.installed_assets(root)
            write_archive(root, "main_xbox", records, compressed=True)
            data = (root / "gen/main_xbox.hdr").read_bytes()
            header = hmx_ark.parse_header(data)
            header.files[0].inflate = 16  # bounded decompressor rejects more output
            (root / "gen/main_xbox.hdr").write_bytes(hmx_ark.encode_index(
                header.files, "gen/main_xbox_0.ark", part.stat().st_size, bytes(20), bytes(4), 12345))
            with self.assertRaises(ValueError):
                r.installed_assets(root)

    def test_cli_writes_blocked_index_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_archive(root, "main_xbox", [(n, fixture()) for n in r.ASSET_NAMES])
            output = root / "index.json"
            command = [sys.executable, str(Path(r.__file__)), "--game-root", str(root),
                       "--hash-helper", str(HELPER), "--output", str(output)]
            result = subprocess.run(command, capture_output=True)
            self.assertEqual(result.returncode, 2, result.stderr)
            saved = output.read_bytes()
            index = json.loads(saved)
            self.assertEqual(index["schema"], r.SCHEMA)
            self.assertFalse(index["live_ready"])
            self.assertTrue(index["blockers"])
            self.assertNotIn("code", index["resolved"][0])
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 1)
            self.assertEqual(output.read_bytes(), saved)

    def test_cli_malformed_encrypted_header_fails_without_an_index(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "gen").mkdir()
            (root / "gen/main_xbox.hdr").write_bytes(bytes.fromhex("1122334455"))
            output = root / "index.json"
            result = subprocess.run([sys.executable, str(Path(r.__file__)),
                "--game-root", str(root), "--hash-helper", str(HELPER),
                "--output", str(output)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(output.exists())
            self.assertNotIn(b"Traceback", result.stderr)

    def test_vertex_link_fields_and_fetch_coalescing(self):
        image, manifest = vertex_fixture()
        layouts, masks, sizes, _ = vl.source_layouts(image, manifest)
        _, elements, strides = layouts[0]
        code = struct.pack(">9I", 0, 0, 0, 1 << 12, 0x688, 0, 2 << 12, 0x688, 0)
        program = r.Program("synthetic", 0, "vertex", 0, code, (0x100001, 0x205002))
        linked = vl.link(program, elements, strides, masks, sizes)
        a, b, c, d, e, f = struct.unpack_from(">6I", linked, 12)
        self.assertEqual(((a >> 20) & 31) * 3 + ((a >> 25) & 3), 95)
        self.assertEqual((a >> 12) & 63, 1)
        self.assertEqual((d >> 12) & 63, 2)
        self.assertEqual((b >> 16) & 63, 57)  # float3
        self.assertEqual((e >> 16) & 63, 37)  # float2
        self.assertEqual((c & 255, f & 255), (5, 5))
        self.assertEqual((f >> 8) & 0x7fffff, 3)
        self.assertEqual((a >> 27) & 7, 4)  # five dwords prefetched
        self.assertTrue(e & 0x40000000)  # second fetch reuses the first
        self.assertEqual(linked[:12], code[:12])

    def test_vertex_link_sorts_by_declaration_offset(self):
        image, manifest = vertex_fixture()
        layouts, masks, sizes, _ = vl.source_layouts(image, manifest)
        _, elements, strides = layouts[0]
        code = struct.pack(">9I", 0, 0, 0, 1 << 12, 0x688, 0, 2 << 12, 0x688, 0)
        program = r.Program("synthetic", 0, "vertex", 0, code, (0x105001, 0x200002))
        linked = vl.link(program, elements, strides, masks, sizes)
        words = struct.unpack_from(">6I", linked, 12)
        self.assertEqual(((words[0] >> 12) & 63, (words[3] >> 12) & 63), (2, 1))
        self.assertEqual(((words[1] >> 16) & 63, (words[4] >> 16) & 63), (57, 37))

    def test_vertex_sources_publish_only_after_verified_generation(self):
        image, manifest = vertex_fixture()
        resolver = r.Resolver({"synthetic": (fixture(), {})}, hashes)
        resolver.add_vertex_sources(image, manifest, {"name": "synthetic.xex", "sha256": r.sha256(image)})
        identity = hashes([bytes(range(36, 72))])[0]
        self.assertEqual(resolver.resolve("vertex", identity), bytes(range(36, 72)))
        self.assertFalse(resolver.index()["blockers"])
        self.assertTrue(resolver.coverage([("vertex", identity, None)])["complete"])
        self.assertFalse(resolver.index()["live_ready"])
        changed = json.loads(json.dumps(manifest))
        changed["bootstrap_shaders"] = [{"name": "bad", "offset": 224, "bytes": 12, "hash": "f" * 16}]
        other = r.Resolver({"synthetic": (fixture(), {})}, hashes)
        with self.assertRaises(ValueError):
            other.add_vertex_sources(image, changed, {})
        self.assertFalse(other.vertex_linked)
        self.assertIsNone(other.resolve("vertex", identity))

    def test_vertex_source_malformed_recipes_and_bounds(self):
        image, manifest = vertex_fixture()
        cases = [None, [], {**manifest, "schema": 2}, {**manifest, "layouts": [None]},
                 {**manifest, "layouts": []}, {**manifest, "swizzle_masks_offset": -1},
                 {**manifest, "format_sizes_offset": 250},
                 {**manifest, "bootstrap_shaders": [None]}]
        for layout in ({"name": "x", "declaration_offset": 250, "strides": [5]},
                       {"name": "x", "declaration_offset": -1, "strides": [5]},
                       {"name": "x", "declaration_offset": 16, "strides": [256]},
                       {"name": "x", "fvf": 123, "strides": [5]}):
            cases.append({**manifest, "layouts": [layout]})
        for bad in cases:
            with self.subTest(manifest=bad), self.assertRaises(ValueError):
                vl.source_layouts(image, bad)
        for length in range(180):
            with self.assertRaises(ValueError):
                vl.source_layouts(image[:length], manifest)

    def test_fvf450_matches_28_byte_initialization_layout(self):
        elements = vl.fvf_declaration(450)
        self.assertEqual([(e[1], e[4], e[5]) for e in elements],
                         [(0, 0, 0), (12, 10, 0), (16, 10, 1), (20, 5, 0)])
        self.assertEqual(elements[-1][2] & 63, 37)  # final float2 ends at byte 28

    def test_xex_bounded_basic_decoding_and_encryption(self):
        from Crypto.Cipher import AES
        data = xex_fixture()
        expected = bytes(range(16)) + bytes(16)
        self.assertEqual(xs.decode_image(data, bytes(16)), expected)
        key, session = bytes(range(16)), bytes(range(16, 32))
        struct.pack_into(">H", data, 0x34, 1)
        data[0x190:0x1a0] = AES.new(key, AES.MODE_ECB).encrypt(session)
        data[0x210:] = AES.new(session, AES.MODE_CBC, bytes(16)).encrypt(bytes(range(16)))
        self.assertEqual(xs.decode_image(data, key), expected)

    def test_xex_malformed_lengths_and_unsupported_fingerprint(self):
        data = xex_fixture()
        for length in range(len(data)):
            with self.assertRaises(ValueError):
                xs.decode_image(data[:length], bytes(16))
        for offset, value in ((8, 0xffffffff), (16, 0x210), (20, 129),
                              (0x30, 0xffffffff), (0x44, 0xffffffff),
                              (0x38, 0xffffffff), (0x3c, 0xffffffff)):
            bad = bytearray(data)
            struct.pack_into(">I", bad, offset, value)
            with self.assertRaises(ValueError):
                xs.decode_image(bad, bytes(16))
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "default.xex").write_bytes(data)
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                xs.load_supported_image(folder, Path(r.ROOT))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--hash-helper", required=True, type=Path)
    parser.add_argument("--installation", type=Path)
    parser.add_argument("--effective-overlay-root", type=Path)
    args, remaining = parser.parse_known_args()
    HELPER = args.hash_helper.resolve()
    if args.installation:
        def installed_candidate():
            assets, _ = r.installed_assets(args.installation, args.effective_overlay_root)
            resolver = r.Resolver(assets, hashes)
            image, source = xs.load_supported_image(args.installation, r.ROOT)
            manifest = json.loads((r.ROOT / "config/shader_vertex_layouts.json").read_text())
            resolver.add_vertex_sources(image, manifest, source)
            candidate = json.loads((r.ROOT / "config/shader_resolution_candidate.json").read_text())
            coverage = resolver.coverage(r.catalogue_requests(candidate))
            if not coverage["complete"]:
                raise AssertionError(f"unresolved candidate: {coverage}")
            print(f"Installed candidate: {coverage['resolved']}/{coverage['required']} exact identities")
        result = unittest.TextTestRunner().run(unittest.TestSuite([
            unittest.FunctionTestCase(installed_candidate)]))
        sys.exit(0 if result.wasSuccessful() else 1)
    else:
        unittest.main(argv=[sys.argv[0], *remaining])
