"""P3 pipeline recipe catalogue. Offline: no game launch, no cache mutation.

Reads the renderer's normalized pipeline description stores (``*.d3d12.xpso`` /
``*.vk.xpso``), validates their header and every record hash with the SDK's own
XXH3 implementation, deduplicates and merges recipes with provenance, checks the
referenced shader identities against P2's local resolver, and writes a versioned
catalogue plus an optional merged store. The helper-pipeline inventory in
``config/pipeline_helper_inventory.json`` is carried with its explicit coverage.

The description layouts and storage versions are the SDK's ``PipelineDescription``
structs: 64-byte D3D12 and 60-byte Vulkan descriptions, with an 8-byte stored
hash and a 12-byte file header. Every record is self-validating: its stored hash
is XXH3_64bits over the description bytes, so a wrong stride or field layout
fails the whole store rather than publishing wrong recipes.

Exit codes: 0 = catalogue published with complete declared coverage; 2 = the
catalogue was written but coverage has explicit blockers (unresolved shader
identities, helper recipes whose runtime key is only a call site, an accepted
corrupt tail, or shader resolution not checked); 1 = invalid input or tool
failure. See docs/engine/shader-cache.md (part 3, pipeline recipe catalogue).
"""
import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

from shader_asset_resolver import sdk_hashes, sha256

SCHEMA = 1
HEADER_SIZE = 12
STORAGE_MAGIC = 0x53504558  # 'XEPS'
MAX_STORE_BYTES = 64 * 1024 * 1024
MAX_RECORDS = 32768
MAX_STORES = 64
ROOT = Path(__file__).resolve().parents[2]

# The stored version is max(PipelineDescription::kVersion,
# <backend>ShaderTranslator::Modification::kVersion) and is physically written
# byte-swapped. tests/shader_pipeline_catalogue_tests.py re-reads the SDK
# headers and refuses these constants when they drift.
BACKENDS = {
    "d3d12": {
        "description_size": 64,
        "record_size": 72,
        "version": 0x20260226,
        "apis": {0x54525844: "rtv", 0x4F525844: "rov"},  # 'DXRT', 'DXRO'
    },
    "vulkan": {
        "description_size": 60,
        "record_size": 68,
        "version": 0x20260228,
        "apis": {0: "fbo", 1: "fsi"},
    },
}
STORE_SUFFIXES = (".xpso",)
TITLE_PATTERN = re.compile(r"^(?P<title>[0-9A-Fa-f]{8})\.(?P<path>[a-z]+)\.")


def read_bounded(path, limit=MAX_STORE_BYTES):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{path}: exceeds {limit}-byte limit")
    return data


def read_json(path, limit=4 * 1024 * 1024):
    return json.loads(read_bounded(path, limit).decode("utf-8"))


def stored_version(version):
    """The value the runtime writes: rex::byte_swap(max(...)), read little-endian."""
    return int.from_bytes(int(version).to_bytes(4, "big"), "little")


@dataclass(frozen=True)
class RecipeRecord:
    description_hash: str
    raw: bytes
    vertex_hash: str
    pixel_hash: str


@dataclass
class Store:
    id: str
    name: str
    backend: str
    path_kind: str
    sha256: str
    bytes: int
    records: list
    valid_records: int
    duplicate_records: int
    trailing_bytes: int
    first_invalid_index: object
    declared_path: object = None

    def summary(self):
        return {
            "id": self.id,
            "name": self.name,
            "backend": self.backend,
            "path": self.path_kind,
            "sha256": self.sha256,
            "bytes": self.bytes,
            "records": self.valid_records,
            "duplicate_records": self.duplicate_records,
            "unverified_tail_bytes": self.trailing_bytes,
            "first_invalid_record": self.first_invalid_index,
            "declared_path": self.declared_path,
        }


@dataclass
class MergedRecipe:
    backend: str
    path_kind: str
    description_hash: str
    raw: bytes
    vertex_hash: str
    pixel_hash: str
    stores: list = field(default_factory=list)

    def key(self):
        return (self.backend, self.path_kind, self.raw)


def expand_stores(paths):
    """Expand directories to their ``*.xpso`` files, preserving a stable order."""
    files, seen = [], set()
    for path in paths:
        path = Path(path)
        found = sorted(p for p in path.rglob("*") if p.suffix.lower() in STORE_SUFFIXES) \
            if path.is_dir() else [path]
        for candidate in found:
            resolved = candidate.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            files.append(candidate)
            if len(files) > MAX_STORES:
                raise ValueError(f"more than {MAX_STORES} stores requested")
    if not files:
        raise ValueError("no pipeline stores found")
    return files


def parse_store(path, store_id, hasher, title_id, accept_truncated=False):
    data = read_bounded(path)
    if len(data) < HEADER_SIZE:
        raise ValueError(f"{path}: shorter than the {HEADER_SIZE}-byte storage header")
    magic, api, version = struct.unpack_from("<III", data, 0)
    if magic != STORAGE_MAGIC:
        raise ValueError(f"{path}: magic 0x{magic:08X} is not 'XEPS'")
    backend = path_kind = None
    for name, spec in BACKENDS.items():
        if api in spec["apis"]:
            backend, path_kind = name, spec["apis"][api]
            break
    if backend is None:
        raise ValueError(f"{path}: unknown pipeline storage API 0x{api:08X}")
    spec = BACKENDS[backend]
    expected = stored_version(spec["version"])
    if version != expected:
        raise ValueError(
            f"{path}: {backend} storage version 0x{version:08X} does not match the "
            f"SDK generation 0x{expected:08X} (the caches are not interchangeable)")
    record_size = spec["record_size"]
    count, trailing = divmod(len(data) - HEADER_SIZE, record_size)
    if trailing and not accept_truncated:
        raise ValueError(
            f"{path}: {trailing} unverified trailing byte(s) after {count} record(s); "
            "pass --accept-truncated-tail to use only the complete prefix")
    if count > MAX_RECORDS:
        raise ValueError(f"{path}: {count} records exceeds the {MAX_RECORDS} limit")
    raw_records = []
    for index in range(count):
        offset = HEADER_SIZE + index * record_size
        stored_hash = struct.unpack_from("<Q", data, offset)[0]
        raw = data[offset + 8:offset + record_size]
        raw_records.append((stored_hash, raw))
    declared = TITLE_PATTERN.match(Path(path).name)
    declared_path = None
    if declared:
        if declared.group("title").upper() != title_id.upper():
            raise ValueError(f"{path}: title id {declared.group('title')} != {title_id}")
        declared_path = declared.group("path")
    hashes = hasher([raw for _, raw in raw_records]) if raw_records else []
    records, seen = [], set()
    valid_count, first_invalid = 0, None
    for index, ((stored_hash, raw), computed) in enumerate(zip(raw_records, hashes)):
        if f"{stored_hash:016x}" != computed:
            first_invalid = index
            break
        valid_count += 1
        if raw in seen:
            continue
        seen.add(raw)
        records.append(describe_record(backend, f"{stored_hash:016x}", raw))
    if first_invalid is not None and not accept_truncated:
        raise ValueError(
            f"{path}: record {first_invalid} at byte 0x{HEADER_SIZE + first_invalid * record_size:x} "
            "fails its description hash; pass --accept-truncated-tail to use only the "
            "verified prefix (the runtime truncates this tail on its next load)")
    verified_bytes = HEADER_SIZE + valid_count * record_size
    unverified = len(data) - verified_bytes
    return Store(id=store_id, name=str(path), backend=backend, path_kind=path_kind,
                 sha256=sha256(data), bytes=len(data), records=records,
                 valid_records=valid_count, duplicate_records=valid_count - len(records),
                 trailing_bytes=unverified, first_invalid_index=first_invalid,
                 declared_path=declared_path)


def describe_record(backend, description_hash, raw):
    expected = BACKENDS[backend]["description_size"]
    if len(raw) != expected:
        raise ValueError(f"{backend} description is {len(raw)} bytes, expected {expected}")
    vertex_hash, _, pixel_hash, _ = struct.unpack_from("<QQQQ", raw, 0)
    # 0 means "no pixel shader"; keep it empty so truthiness matches the runtime.
    return RecipeRecord(description_hash, raw, f"{vertex_hash:016x}",
                        f"{pixel_hash:016x}" if pixel_hash else "")


def _u32(raw, offset):
    return struct.unpack_from("<I", raw, offset)[0]


def decode_d3d12(raw):
    """Decode the SDK's packed D3D12 PipelineDescription (64 bytes)."""
    vertex_hash, vertex_modification, pixel_hash, pixel_modification = \
        struct.unpack_from("<QQQQ", raw, 0)
    depth_bias, depth_bias_slope_scaled = struct.unpack_from("<if", raw, 32)
    state, stencil = _u32(raw, 40), _u32(raw, 44)
    render_targets = []
    for index in range(4):
        bits = _u32(raw, 48 + index * 4)
        render_targets.append({
            "used": bits & 0x1,
            "format": (bits >> 1) & 0xF,
            "src_blend": (bits >> 5) & 0xF,
            "dest_blend": (bits >> 9) & 0xF,
            "blend_op": (bits >> 13) & 0x7,
            "src_blend_alpha": (bits >> 16) & 0xF,
            "dest_blend_alpha": (bits >> 20) & 0xF,
            "blend_op_alpha": (bits >> 24) & 0x7,
            "write_mask": (bits >> 27) & 0xF,
        })
    return {
        "vertex": {"hash": f"{vertex_hash:016x}", "modification": f"{vertex_modification:016x}"},
        "pixel": ({"hash": f"{pixel_hash:016x}", "modification": f"{pixel_modification:016x}"}
                  if pixel_hash else None),
        "depth_bias": depth_bias,
        "depth_bias_slope_scaled": depth_bias_slope_scaled,
        "strip_cut_index": state & 0x3,
        "primitive_topology": (state >> 2) & 0x3,
        "geometry_shader": (state >> 4) & 0x3,
        "fill_mode_wireframe": (state >> 6) & 0x1,
        "cull_mode": (state >> 7) & 0x3,
        "front_counter_clockwise": (state >> 9) & 0x1,
        "depth_clip": (state >> 10) & 0x1,
        "host_msaa_samples": (state >> 11) & 0x3,
        "depth_format": (state >> 13) & 0x1,
        "depth_func": (state >> 14) & 0x7,
        "depth_write": (state >> 17) & 0x1,
        "stencil_enable": (state >> 18) & 0x1,
        "stencil_read_mask": (state >> 19) & 0xFF,
        "stencil_write_mask": stencil & 0xFF,
        "stencil_front_fail_op": (stencil >> 8) & 0x7,
        "stencil_front_depth_fail_op": (stencil >> 11) & 0x7,
        "stencil_front_pass_op": (stencil >> 14) & 0x7,
        "stencil_front_func": (stencil >> 17) & 0x7,
        "stencil_back_fail_op": (stencil >> 20) & 0x7,
        "stencil_back_depth_fail_op": (stencil >> 23) & 0x7,
        "stencil_back_pass_op": (stencil >> 26) & 0x7,
        "stencil_back_func": (stencil >> 29) & 0x7,
        "render_targets": render_targets,
    }


def decode_vulkan(raw):
    """Decode the SDK's packed Vulkan PipelineDescription (60 bytes)."""
    vertex_hash, vertex_modification, pixel_hash, pixel_modification = \
        struct.unpack_from("<QQQQ", raw, 0)
    render_pass_key = _u32(raw, 32)
    state, stencil = _u32(raw, 36), _u32(raw, 40)
    render_targets = []
    for index in range(4):
        bits = _u32(raw, 44 + index * 4)
        render_targets.append({
            "src_color_blend_factor": bits & 0xF,
            "dst_color_blend_factor": (bits >> 4) & 0xF,
            "color_blend_op": (bits >> 8) & 0x7,
            "src_alpha_blend_factor": (bits >> 11) & 0xF,
            "dst_alpha_blend_factor": (bits >> 15) & 0xF,
            "alpha_blend_op": (bits >> 19) & 0x7,
            "color_write_mask": (bits >> 22) & 0xF,
        })
    return {
        "vertex": {"hash": f"{vertex_hash:016x}", "modification": f"{vertex_modification:016x}"},
        "pixel": ({"hash": f"{pixel_hash:016x}", "modification": f"{pixel_modification:016x}"}
                  if pixel_hash else None),
        "geometry_shader": state & 0x3,
        "primitive_topology": (state >> 2) & 0x7,
        "primitive_restart": (state >> 5) & 0x1,
        "tessellation_mode": (state >> 6) & 0x3,
        "depth_clamp_enable": (state >> 8) & 0x1,
        "polygon_mode": (state >> 9) & 0x3,
        "cull_front": (state >> 11) & 0x1,
        "cull_back": (state >> 12) & 0x1,
        "front_face_clockwise": (state >> 13) & 0x1,
        "rasterizer_discard": (state >> 14) & 0x1,
        "depth_write_enable": (state >> 15) & 0x1,
        "depth_compare_op": (state >> 16) & 0x7,
        "stencil_test_enable": (state >> 19) & 0x1,
        "stencil_front_fail_op": (state >> 20) & 0x7,
        "stencil_front_pass_op": (state >> 23) & 0x7,
        "stencil_front_depth_fail_op": (state >> 26) & 0x7,
        "stencil_front_compare_op": (state >> 29) & 0x7,
        "stencil_back_fail_op": stencil & 0x7,
        "stencil_back_pass_op": (stencil >> 3) & 0x7,
        "stencil_back_depth_fail_op": (stencil >> 6) & 0x7,
        "stencil_back_compare_op": (stencil >> 9) & 0x7,
        "sample_rate_shading": (stencil >> 12) & 0x1,
        "render_pass_key": render_pass_key,
        "render_pass": {
            "msaa_samples": render_pass_key & 0x3,
            "depth_and_color_used": (render_pass_key >> 2) & 0x1F,
            "depth_format": (render_pass_key >> 7) & 0x1,
            "color_0_view_format": (render_pass_key >> 8) & 0xF,
            "color_1_view_format": (render_pass_key >> 12) & 0xF,
            "color_2_view_format": (render_pass_key >> 16) & 0xF,
            "color_3_view_format": (render_pass_key >> 20) & 0xF,
            "color_rts_use_transfer_formats": (render_pass_key >> 24) & 0x1,
        },
        "render_targets": render_targets,
    }


def decode(backend, raw):
    return decode_d3d12(raw) if backend == "d3d12" else decode_vulkan(raw)


def merge_stores(stores):
    """Union recipes by exact normalized description; keep every store in provenance."""
    merged, collisions = {}, []
    for store in stores:
        for record in store.records:
            key = (store.backend, store.path_kind, record.raw)
            entry = merged.get(key)
            if entry is None:
                entry = MergedRecipe(store.backend, store.path_kind, record.description_hash,
                                     record.raw, record.vertex_hash, record.pixel_hash)
                merged[key] = entry
            elif entry.description_hash != record.description_hash:
                collisions.append((store.backend, store.path_kind, entry.description_hash,
                                   record.description_hash))
            if store.id not in entry.stores:
                entry.stores.append(store.id)
    by_hash = {}
    for entry in merged.values():
        by_hash.setdefault((entry.backend, entry.path_kind, entry.description_hash), set()).add(
            entry.raw)
    for (backend, path_kind, description_hash), variants in sorted(by_hash.items()):
        if len(variants) > 1:
            collisions.append((backend, path_kind, description_hash, "same hash, different bytes"))
    if collisions:
        raise ValueError("pipeline description hash collision: " +
                         "; ".join(":".join(str(part) for part in row) for row in collisions))
    return sorted(merged.values(), key=lambda e: (e.backend, e.path_kind, e.description_hash, e.raw))


def recipe_json(entry):
    decoded = decode(entry.backend, entry.raw)
    return {
        "description_hash": entry.description_hash,
        "backend": entry.backend,
        "path": entry.path_kind,
        "raw": entry.raw.hex(),
        "vertex": decoded["vertex"],
        "pixel": decoded["pixel"],
        "state": {key: value for key, value in decoded.items() if key not in ("vertex", "pixel")},
        "stores": entry.stores,
    }


def summarize_recipes(entries):
    axes = {}
    vertex_modifications, pixel_modifications = set(), set()
    for entry in entries:
        decoded = decode(entry.backend, entry.raw)
        vertex_modifications.add((entry.vertex_hash, decoded["vertex"]["modification"]))
        if decoded["pixel"]:
            pixel_modifications.add((entry.pixel_hash, decoded["pixel"]["modification"]))
        if entry.backend == "d3d12":
            for name in ("host_msaa_samples", "depth_format", "depth_func", "cull_mode",
                         "strip_cut_index", "primitive_topology"):
                axes.setdefault(name, set()).add(decoded[name])
            rt_formats = [rt["format"] for rt in decoded["render_targets"] if rt["used"]]
        else:
            for name in ("primitive_topology", "tessellation_mode", "polygon_mode",
                         "depth_compare_op", "sample_rate_shading"):
                axes.setdefault(name, set()).add(decoded[name])
            render_pass = decoded["render_pass"]
            axes.setdefault("render_pass_msaa", set()).add(render_pass["msaa_samples"])
            rt_formats = [render_pass[f"color_{index}_view_format"]
                          for index in range(4)
                          if render_pass["depth_and_color_used"] & (1 << (index + 1))]
        axes.setdefault("render_target_formats", set()).update(rt_formats)
    pairs = {(entry.vertex_hash, entry.pixel_hash) for entry in entries}
    return {
        "recipes": len(entries),
        "unique_vertex_shaders": len({entry.vertex_hash for entry in entries}),
        "unique_pixel_shaders": len({entry.pixel_hash for entry in entries if entry.pixel_hash}),
        "vertex_modifications": len(vertex_modifications),
        "pixel_modifications": len(pixel_modifications),
        "without_pixel_shader": sum(1 for entry in entries if not entry.pixel_hash),
        "shader_pairs": len(pairs),
        "axes": {name: sorted(values, key=str) for name, values in sorted(axes.items())},
    }


def identity_requests(entries):
    requests = set()
    for entry in entries:
        requests.add(("vertex", entry.vertex_hash))
        if entry.pixel_hash:
            requests.add(("pixel", entry.pixel_hash))
    return sorted(requests)


def resolver_index_identities(path):
    data = read_json(path)
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise ValueError(f"{path}: unsupported resolver index schema")
    rows = data.get("resolved")
    if not isinstance(rows, list):
        raise ValueError(f"{path}: resolver index has no resolved table")
    identities = set()
    for row in rows:
        if (not isinstance(row, dict) or row.get("stage") not in ("vertex", "pixel")
                or not re.fullmatch("[0-9a-fA-F]{16}", str(row.get("hash", "")))):
            raise ValueError(f"{path}: invalid resolver index row")
        identities.add((row["stage"], row["hash"].lower()))
    return identities


def build_game_resolver(game_root, overlay_root, vertex_layouts, hash_helper):
    """Build P2's resolver from the installed assets/executable. Vertex failure is reported."""
    import shader_asset_resolver as resolver_module

    assets, provenance = resolver_module.installed_assets(game_root, overlay_root)
    resolver = resolver_module.Resolver(
        assets, lambda codes: resolver_module.sdk_hashes(codes, hash_helper))
    error = None
    try:
        from shader_xex_source import load_supported_image
        image, executable_source = load_supported_image(game_root, ROOT)
        resolver.add_vertex_sources(image, read_json(vertex_layouts), executable_source)
    except (ValueError, OSError, ImportError, struct.error) as exc:
        error = str(exc)
    return resolver, error, provenance


def coverage_with_resolver(resolver, requests):
    result = resolver.coverage([(stage, identity, None) for stage, identity in requests])
    entries = [{"stage": row["stage"], "hash": row["hash"], "resolved": row["resolved"],
                "reason": row["reason"]} for row in result["entries"]]
    return {"required": result["required"], "resolved": result["resolved"],
            "unresolved": result["unresolved"], "complete": result["complete"],
            "entries": entries}


def coverage_with_index(identities, requests):
    entries = [{"stage": stage, "hash": identity, "resolved": (stage, identity) in identities,
                "reason": None if (stage, identity) in identities else "absent_from_resolver_index"}
               for stage, identity in requests]
    resolved = sum(row["resolved"] for row in entries)
    return {"required": len(entries), "resolved": resolved,
            "unresolved": len(entries) - resolved,
            "complete": bool(entries) and resolved == len(entries), "entries": entries}


def candidate_identities(path):
    if not path:
        return None
    data = read_json(path)
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise ValueError(f"{path}: unsupported shader identity catalogue schema")
    rows = data.get("shaders")
    if not isinstance(rows, list):
        raise ValueError(f"{path}: shader identity catalogue has no shader table")
    identities = set()
    for row in rows:
        if (not isinstance(row, dict) or row.get("stage") not in ("vertex", "pixel")
                or not re.fullmatch("[0-9a-fA-F]{16}", str(row.get("hash", "")))):
            raise ValueError(f"{path}: invalid shader identity row")
        identities.add((row["stage"], row["hash"].lower()))
    return identities


def load_helper_inventory(path, title_id):
    data = read_json(path)
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise ValueError(f"{path}: unsupported helper inventory schema")
    if data.get("title_id") != title_id:
        raise ValueError(f"{path}: title id {data.get('title_id')} != {title_id}")
    backends = data.get("backends")
    if not isinstance(backends, dict) or set(backends) != set(BACKENDS):
        raise ValueError(f"{path}: helper inventory must declare both backends")
    entries, ids = [], set()
    for backend in sorted(backends):
        rows = backends[backend].get("entries")
        if not isinstance(rows, list):
            raise ValueError(f"{path}: {backend} helper entries must be a list")
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError(f"{path}: invalid {backend} helper entry")
            for field_name in ("id", "kind", "site", "function", "when", "trace", "dependencies",
                               "variants", "coverage"):
                if field_name not in row:
                    raise ValueError(f"{path}: {backend} helper entry lacks '{field_name}'")
            if not isinstance(row["id"], str) or not row["id"].startswith(backend + "."):
                raise ValueError(f"{path}: helper id '{row['id']}' must start with '{backend}.'")
            if row["id"] in ids:
                raise ValueError(f"{path}: duplicate helper id '{row['id']}'")
            ids.add(row["id"])
            if row["kind"] not in ("graphics", "compute"):
                raise ValueError(f"{path}: helper '{row['id']}' has an invalid kind")
            if row["coverage"] not in ("site_unique", "key_identified", "site_repeated"):
                raise ValueError(f"{path}: helper '{row['id']}' has invalid coverage")
            trace = row["trace"]
            if not isinstance(trace, dict) or trace.get("event") not in (
                    "helper_graphics", "helper_compute"):
                raise ValueError(f"{path}: helper '{row['id']}' has an invalid trace event")
            if trace.get("identity") not in ("call_site", "shared_util_site", "key"):
                raise ValueError(f"{path}: helper '{row['id']}' has invalid trace identity")
            if (trace.get("identity") == "key") != (row["coverage"] == "key_identified"):
                raise ValueError(f"{path}: helper '{row['id']}' must pair a key identity with "
                                 "key_identified coverage, and only with it")
            if trace.get("identity") == "key" and not isinstance(trace.get("key_bits"), str):
                raise ValueError(f"{path}: helper '{row['id']}' carries a key but does not "
                                 "document its bit layout")
            if not isinstance(trace.get("site"), str) or not re.fullmatch(r"[^\s]+:\d+", trace["site"]):
                raise ValueError(f"{path}: helper '{row['id']}' has an invalid trace site")
            if not isinstance(row["dependencies"], list) or not row["dependencies"]:
                raise ValueError(f"{path}: helper '{row['id']}' has no dependencies")
            entries.append({"backend": backend, **row})
    if not entries:
        raise ValueError(f"{path}: helper inventory is empty")
    return entries


def helper_coverage(entries):
    repeated = [row["id"] for row in entries if row["coverage"] == "site_repeated"]
    key_identified = [row["id"] for row in entries if row["coverage"] == "key_identified"]
    site_unique = [row["id"] for row in entries if row["coverage"] == "site_unique"]
    return {
        "inventoried": len(entries),
        "site_unique": len(site_unique),
        "key_identified": len(key_identified),
        "site_repeated": len(repeated),
        "complete": not repeated,
        "key_identity_unresolved": repeated,
    }


def build_catalogue(stores, entries, helper_entries, shader_coverage, candidate, resolver_info):
    groups = {}
    for entry in entries:
        groups.setdefault((entry.backend, entry.path_kind), []).append(entry)
    # A declared rendering path with no captured store is recorded as uncaptured
    # rather than omitted, so a catalogue cannot look like it covered ROV or FSI.
    backends = {backend: {"paths": {path_kind: {"captured": False, "coverage": None,
                                                   "recipes": []}
                                for path_kind in sorted(spec["apis"].values())}}
                for backend, spec in BACKENDS.items()}
    for (backend, path_kind), group in sorted(groups.items()):
        backends[backend]["paths"][path_kind] = {
            "captured": True,
            "coverage": summarize_recipes(group),
            "recipes": [recipe_json(entry) for entry in group],
        }
    paths_not_captured = [f"{backend}/{path_kind}"
                          for backend in sorted(backends)
                          for path_kind, path_data in sorted(backends[backend]["paths"].items())
                          if not path_data["captured"]]
    requests = identity_requests(entries)
    outside = []
    if candidate is not None:
        outside = [{"stage": stage, "hash": identity,
                    "resolved": next((row["resolved"] for row in shader_coverage["entries"]
                                      if row["stage"] == stage and row["hash"] == identity), None)}
                   for stage, identity in requests if (stage, identity) not in candidate]
    helpers = helper_coverage(helper_entries)
    blockers = []
    if not shader_coverage["complete"]:
        blockers.append(f"{shader_coverage['unresolved']} shader identit"
                        f"{'y is' if shader_coverage['unresolved'] == 1 else 'ies are'} unresolved")
    if not helpers["complete"]:
        blockers.append(f"{helpers['site_repeated']} helper recipe(s) create several pipelines "
                        "at one trace site, so their runtime key identity is not captured")
    for store in stores:
        if store.trailing_bytes:
            blockers.append(f"store {store.name} has {store.trailing_bytes} unverified tail byte(s)")
    if resolver_info.get("error"):
        blockers.append("installed executable vertex sources could not be linked: " +
                        resolver_info["error"])
    return {
        "schema": SCHEMA,
        "kind": "rb_blitz.pipeline_catalogue",
        "title_id": resolver_info["title_id"],
        "identity": "normalized SDK PipelineDescription; XXH3_64bits(description) is the recipe hash",
        "layout": {
            "storage_magic": "XEPS",
            "storage_header_size": HEADER_SIZE,
            "backends": {name: {"description_size": spec["description_size"],
                                "record_size": spec["record_size"],
                                "storage_version": f"{spec['version']:08X}",
                                "paths": sorted(spec["apis"].values())}
                         for name, spec in BACKENDS.items()},
        },
        "stores": [store.summary() for store in stores],
        "backends": backends,
        "helpers": {"schema": SCHEMA, "entries": helper_entries, "coverage": helpers},
        "coverage": {
            "shaders": shader_coverage,
            "helpers": helpers,
            "resolution": resolver_info["resolution"],
            "candidate_identities": len(candidate) if candidate is not None else None,
            "outside_committed_candidate": outside,
            "paths_not_captured": paths_not_captured,
            "complete": shader_coverage["complete"] and helpers["complete"] and not blockers,
            "blockers": blockers,
            "live_ready": False,
        },
        "provenance": {"tool": "scripts/shaders/shader_pipeline_catalogue.py",
                       "store_order": [store.name for store in stores]},
    }


def write_json_exclusive(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")


def merged_store_bytes(backend, path_kind, entries):
    spec = BACKENDS[backend]
    api = next(api for api, name in spec["apis"].items() if name == path_kind)
    body = bytearray(struct.pack("<III", STORAGE_MAGIC, api, stored_version(spec["version"])))
    for entry in sorted(entries, key=lambda e: (e.description_hash, e.raw)):
        if len(entry.raw) != spec["description_size"]:
            raise ValueError("merged recipe has the wrong description size")
        body += struct.pack("<Q", int(entry.description_hash, 16)) + entry.raw
    return bytes(body)


def write_merged_store(path, backend, path_kind, entries):
    data = merged_store_bytes(backend, path_kind, entries)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
    return len(data)


def print_report(stores, entries, catalogue):
    groups = {}
    for store in stores:
        groups[(store.backend, store.path_kind)] = groups.get((store.backend, store.path_kind), 0) + 1
    count_text = ", ".join(f"{backend}/{path}: {count}"
                           for (backend, path), count in sorted(groups.items()))
    print(f"Stores: {len(stores)} ({count_text}); "
          f"{sum(store.valid_records for store in stores)} verified record(s), "
          f"{sum(store.duplicate_records for store in stores)} duplicate(s)")
    print(f"Recipes: {len(entries)} unique normalized description(s)")
    shaders = catalogue["coverage"]["shaders"]
    print(f"Shaders: {shaders['resolved']}/{shaders['required']} resolved "
          f"({catalogue['coverage']['resolution']})")
    helpers = catalogue["coverage"]["helpers"]
    print(f"Helpers: {helpers['inventoried']} inventoried "
          f"({helpers['site_unique']} site-unique, {helpers['key_identified']} key-identified, "
          f"{helpers['site_repeated']} site-repeated)")
    if catalogue["coverage"]["outside_committed_candidate"]:
        print(f"Outside the committed candidate: "
              f"{len(catalogue['coverage']['outside_committed_candidate'])} shader identit(ies)")
    if catalogue["coverage"]["paths_not_captured"]:
        print("Rendering paths not captured: " +
              ", ".join(catalogue["coverage"]["paths_not_captured"]))
    for blocker in catalogue["coverage"]["blockers"]:
        print(f"Coverage blocker: {blocker}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--store", type=Path, action="append", required=True,
                        help="recipe store file or a directory searched recursively for *.xpso")
    parser.add_argument("--catalogue-out", type=Path, required=True,
                        help="new catalogue JSON (refuses to overwrite)")
    parser.add_argument("--merged-store", type=Path,
                        help="optional new binary store; requires every input to share one backend/path")
    parser.add_argument("--title-id", default="5841122D")
    parser.add_argument("--hash-helper", type=Path,
                        default=ROOT / "out/build/win-amd64-release/rb_blitz_shader_microcode_hash.exe")
    parser.add_argument("--resolver-index", type=Path,
                        help="P2 resolver index JSON (identity evidence; no installed content read)")
    parser.add_argument("--game-root", type=Path,
                        help="installed content root; resolves shader identities with P2's resolver")
    parser.add_argument("--effective-overlay-root", type=Path,
                        help="provider actually mounted over --game-root")
    parser.add_argument("--vertex-layouts", type=Path,
                        default=ROOT / "config/shader_vertex_layouts.json")
    parser.add_argument("--candidate", type=Path,
                        default=ROOT / "config/shader_resolution_candidate.json",
                        help="committed shader identity candidate, used to report scope")
    parser.add_argument("--helper-inventory", type=Path,
                        default=ROOT / "config/pipeline_helper_inventory.json")
    parser.add_argument("--accept-truncated-tail", action="store_true",
                        help="use only the verified prefix of a store with a corrupt/partial tail")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.resolver_index and args.game_root:
            raise ValueError("--resolver-index and --game-root are mutually exclusive")
        if args.effective_overlay_root and not args.game_root:
            raise ValueError("--effective-overlay-root requires --game-root")
        files = expand_stores(args.store)
        hasher = lambda codes: sdk_hashes(codes, args.hash_helper.resolve())
        stores = [parse_store(path, f"s{index + 1}", hasher, args.title_id,
                              args.accept_truncated_tail)
                  for index, path in enumerate(files)]
        entries = merge_stores(stores)
        requests = identity_requests(entries)

        resolver_info = {"title_id": args.title_id, "resolution": "not_checked", "error": None}
        if args.resolver_index:
            identities = resolver_index_identities(args.resolver_index)
            shader_coverage = coverage_with_index(identities, requests)
            resolver_info["resolution"] = "resolver_index"
        elif args.game_root:
            resolver, error, _provenance = build_game_resolver(
                args.game_root, args.effective_overlay_root, args.vertex_layouts,
                args.hash_helper.resolve())
            shader_coverage = coverage_with_resolver(resolver, requests)
            resolver_info["resolution"] = "game_root"
            resolver_info["error"] = error
        else:
            shader_coverage = {"required": len(requests), "resolved": 0, "unresolved": len(requests),
                               "complete": False, "entries": [],
                               "reason": "shader resolution not checked"}
            shader_coverage["entries"] = [
                {"stage": stage, "hash": identity, "resolved": False,
                 "reason": "shader_resolution_not_checked"} for stage, identity in requests]
            resolver_info["resolution"] = "not_checked"

        candidate = candidate_identities(args.candidate) if args.candidate else None
        helper_entries = load_helper_inventory(args.helper_inventory, args.title_id)
        catalogue = build_catalogue(stores, entries, helper_entries, shader_coverage,
                                    candidate, resolver_info)
        write_json_exclusive(args.catalogue_out, catalogue)
        merged_bytes = None
        if args.merged_store:
            groups = {(entry.backend, entry.path_kind) for entry in entries}
            if len(groups) != 1:
                raise ValueError("--merged-store needs every input store to share one backend/path "
                                 f"({len(groups)} present)")
            backend, path_kind = next(iter(groups))
            merged_bytes = write_merged_store(args.merged_store, backend, path_kind, entries)
        if not args.quiet:
            print_report(stores, entries, catalogue)
            print(f"Catalogue: {args.catalogue_out}")
            if merged_bytes is not None:
                print(f"Merged store: {args.merged_store} ({merged_bytes} bytes)")
        return 0 if catalogue["coverage"]["complete"] else 2
    except (ValueError, OSError, struct.error, UnicodeError, RecursionError,
            json.JSONDecodeError) as error:
        print(f"pipeline catalogue: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
