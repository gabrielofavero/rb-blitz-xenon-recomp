"""P2 local shader index. No game launch, cache mutation, or shipped shader bytes.

The XOBX loader is sub_823EC540, NOT the base-class sub_8224DC70.
Vertex declaration linking and bootstrap programs use the supported installed XEX.
See docs/engine/shader-cache-p2.md for the layout and validated candidate scope.
The index is evidence/source locators, not a live graphics-readiness marker.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

import hmx_ark
import shader_vertex_link

SCHEMA = 1
MAX_ASSET = 32 * 1024 * 1024
MAX_MICROCODE = 65536
MAX_RECORDS = 32768
ASSET_NAMES = ("xbox_preinit_shaders", "xbox_shaders")
ROOT = Path(__file__).resolve().parents[1]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_bounded(path, limit=MAX_ASSET):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{path}: exceeds {limit}-byte limit")
    return data


class Reader:
    def __init__(self, data):
        if len(data) > MAX_ASSET:
            raise ValueError("XOBX exceeds asset limit")
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.pos:
            raise ValueError(f"truncated XOBX at 0x{self.pos:x}, length {size}")
        start = self.pos
        self.pos += size
        return self.data[start:self.pos]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]


@dataclass(frozen=True)
class Program:
    entry: str
    permutation: int
    stage: str
    offset: int
    code: bytes
    fetches: tuple = ()


def _program(data, meta_start, meta_end, code_start, code_end, stage):
    # Pixel header: object +40. Vertex header: object +872. Offsets here
    # are D3D object-relative; all its fields and microcode are guest big-endian.
    header = meta_start + (40 if stage == "pixel" else 872)
    if header + 36 > meta_end:
        raise ValueError(f"truncated {stage} object header")
    version, metadata_size, buffer_size = struct.unpack_from(">III", data, header)
    if version != (0x102A1100 if stage == "pixel" else 0x102A1101):
        raise ValueError(f"unsupported {stage} program version {version:08x}")
    if metadata_size < 36 or metadata_size > meta_end - header or buffer_size > code_end - code_start:
        raise ValueError(f"invalid {stage} object extent")
    descriptor = struct.unpack_from(">I", data, header + 24)[0]
    if descriptor < 36 or descriptor > metadata_size - 8:
        raise ValueError(f"invalid {stage} program descriptor")
    offset, size = struct.unpack_from(">II", data, header + descriptor)
    if not size or size > MAX_MICROCODE or size % 12 or offset % 4 or offset > buffer_size or size > buffer_size - offset:
        raise ValueError(f"invalid {stage} microcode length {size}")
    fetches = ()
    if stage == "vertex":
        desc = header + descriptor
        if desc + 36 > header + metadata_size:
            raise ValueError("truncated vertex linkage descriptor")
        index, count = struct.unpack_from(">II", data, desc + 24)
        table = desc + (index + 9) * 4
        if count > 32 or table > header + metadata_size or count * 4 > header + metadata_size - table:
            raise ValueError("invalid vertex fetch table")
        fetches = struct.unpack_from(f">{count}I", data, table)
        if any((f & 4095) * 12 + 12 > size for f in fetches):
            raise ValueError("vertex fetch instruction outside microcode")
    return offset, data[code_start + offset:code_start + offset + size], fetches


def parse_xobx(data):
    r = Reader(data)
    if r.take(4) != b"XOBX" or r.u32() != 1:
        raise ValueError("unsupported XOBX magic/version")
    count = r.u32()
    if not 0 < count <= 256:
        raise ValueError("invalid XOBX entry count")
    programs, names, total = [], set(), 0
    for _ in range(count):
        size = r.u32()
        if not 0 < size <= 128:
            raise ValueError("invalid shader name length")
        name = r.take(size).decode("ascii")
        if not re.fullmatch(r"[a-zA-Z0-9_]+", name) or name in names:
            raise ValueError("invalid/duplicate shader entry name")
        names.add(name)
        variants = r.u32()
        total += variants
        if not variants or total > MAX_RECORDS // 2:
            raise ValueError("invalid/oversized permutation table")
        banks = []
        # Pixel object bank, pixel code bank, vertex object bank, vertex code bank.
        for _ in range(2):
            a, b = r.u32(), r.u32()
            for length in (a, b):
                banks.append((r.pos, length))
                r.take(length)
        table = [struct.unpack("<Q4I", r.take(24)) for _ in range(variants)]
        if len({row[0] for row in table}) != variants:
            raise ValueError("duplicate permutation key")
        boundaries = []
        for bank, (_, length) in enumerate(banks):
            offsets = sorted({row[bank + 1] for row in table})
            if offsets[0] != 0 or any(o >= length or o % 16 for o in offsets):
                raise ValueError("invalid bank-relative permutation offset")
            boundaries.append(dict(zip(offsets, offsets[1:] + [length])))
        for key, *offsets in table:
            for stage, bank in (("pixel", 0), ("vertex", 2)):
                m, c = offsets[bank:bank + 2]
                code_start = banks[bank + 1][0] + c
                relative, code, fetches = _program(data, banks[bank][0] + m,
                                banks[bank][0] + boundaries[bank][m], code_start,
                                banks[bank + 1][0] + boundaries[bank + 1][c], stage)
                programs.append(Program(name, key, stage, code_start + relative, code, fetches))
    if r.pos != len(data):
        raise ValueError(f"XOBX trailing bytes at 0x{r.pos:x}")
    return programs


def sdk_hashes(codes, helper):
    if len(codes) > MAX_RECORDS:
        raise ValueError("too many shader hashes")
    payload = bytearray()
    for code in codes:
        if not code or len(code) > MAX_MICROCODE or len(code) % 4:
            raise ValueError("invalid microcode hash input")
        payload += struct.pack("<I", len(code)) + code
    result = subprocess.run([str(helper)], input=payload, capture_output=True,
                            check=True, timeout=60)
    hashes = result.stdout.decode("ascii").splitlines()
    if len(hashes) != len(codes) or any(not re.fullmatch("[0-9a-f]{16}", h) for h in hashes):
        raise ValueError("invalid SDK hash helper output")
    return hashes


def parse_xsh(data, hasher):
    if len(data) > MAX_ASSET or data[:8] != b"XESH\x20\x20\x12\x19":
        raise ValueError("unsupported XSH magic/version/size")
    pos, records = 8, []
    while pos < len(data):
        if len(records) == MAX_RECORDS or len(data) - pos < 12:
            raise ValueError("truncated/oversized XSH record table")
        identity, packed = struct.unpack_from("<QI", data, pos)
        pos += 12
        size = (packed & 0x7fffffff) * 4
        if not size or size > MAX_MICROCODE or size > len(data) - pos:
            raise ValueError("invalid XSH microcode length")
        records.append(("pixel" if packed >> 31 else "vertex",
                        f"{identity:016x}", data[pos:pos + size]))
        pos += size
    hashes = hasher([code for _, _, code in records])
    unique, identities = {}, {}
    for (stage, identity, code), actual in zip(records, hashes):
        if identity != actual:
            raise ValueError(f"XSH microcode hash mismatch: {identity} != {actual}")
        key = stage, identity
        if identity in identities and identities[identity] != code:
            raise ValueError("XSH shader hash collision")
        identities[identity] = code
        unique[key] = code
    return [(stage, identity, code) for (stage, identity), code in unique.items()]


def _relative(path):
    parts = path.replace("\\", "/").split("/")
    if any(p == ".." or ":" in p for p in parts) or path.startswith(("/", "\\")):
        raise ValueError("unsafe archive path")
    return "/".join(p for p in parts if p not in ("", ".")).lower()


def installed_assets(game_root, overlay_root=None):
    """Inspect the effective host union, then patch-before-main archive precedence.

    overlay_root must be the actually mounted provider, not a requested UI mode.
    This tool does not decide which provider the next game boot should activate.
    """
    roots = [Path(overlay_root), Path(game_root)] if overlay_root else [Path(game_root)]

    def effective(relative):
        return next((root / relative for root in roots if (root / relative).is_file()), None)

    assets, provenance = {}, []
    for archive in ("patch_xbox", "main_xbox"):
        header_path = effective(f"gen/{archive}.hdr")
        if header_path is None:
            if archive == "main_xbox":
                raise ValueError("missing effective main_xbox.hdr")
            continue
        raw_header = read_bounded(header_path, 8 * 1024 * 1024)
        header = hmx_ark.parse_header(raw_header)
        if header.version != 6 or not 0 < len(header.arks) <= 16:
            raise ValueError("unsupported installed ARK layout")
        part_paths = []
        for index, (path, _) in enumerate(header.arks):
            if path is None:
                raise ValueError("missing ARK part name")
            relative = _relative(path.decode("latin-1"))
            # Archive paths are game-root-relative (gen/...) in this title.
            relative = relative if "/" in relative else f"gen/{relative}"
            part = effective(relative)
            if part is None:
                raise ValueError(f"missing effective archive part {relative}")
            part_paths.append(part)
        provenance.append({"header": str(header_path.resolve()),
                           "sha256": sha256(raw_header)})
        found = set()
        for entry in header.files:
            path = entry.path.replace("\\", "/")
            if path not in ASSET_NAMES and path not in tuple("./" + n for n in ASSET_NAMES):
                continue
            name = _relative(path)
            if name in found:
                raise ValueError(f"ambiguous shader asset in {header_path}: {name}")
            found.add(name)
            if name in assets:
                continue
            if entry.offset < 0 or not 0 < entry.size <= MAX_ASSET or entry.inflate > MAX_ASSET:
                raise ValueError("invalid shader archive entry size/offset")
            index, offset = header.select_ark(entry.offset)
            part = part_paths[index]
            if entry.size > header.arks[index][1] - offset:
                raise ValueError("shader crosses declared ARK part boundary")
            with part.open("rb") as stream:
                stream.seek(offset)
                data = stream.read(entry.size)
            if len(data) != entry.size:
                raise ValueError("truncated shader archive entry")
            if entry.compressed:
                # Unlike the general extractor, never inflate without an output cap.
                decoded = None
                for wbits in (31, 15, -15):
                    try:
                        z = zlib.decompressobj(wbits)
                        candidate = z.decompress(data, entry.inflate + 1)
                        if z.eof and not z.unused_data and len(candidate) == entry.inflate:
                            decoded = candidate
                            break
                    except zlib.error:
                        pass
                if decoded is None:
                    raise ValueError("invalid/oversized compressed shader entry")
                data = decoded
            assets[name] = (data, {"archive": str(part.resolve()), "offset": offset,
                                   "stored_bytes": entry.size, "header_sha256": sha256(raw_header)})
    if set(assets) != set(ASSET_NAMES):
        raise ValueError("installed shader asset is missing")
    return assets, provenance


class Resolver:
    def __init__(self, assets, hasher):
        self.assets = assets
        self.programs = []
        self.entries = {}
        self.templates = {}
        self.resolved = {}
        self.hasher = hasher
        self.executable_source = None
        self.linkage_fingerprint = None
        self.vertex_linked = False
        for asset in sorted(assets):
            data, _ = assets[asset]
            self.programs += [(asset, p) for p in parse_xobx(data)]
        codes = list(dict.fromkeys(p.code for _, p in self.programs))
        hashes = dict(zip(codes, hasher(codes)))
        identities = {}
        for asset, p in self.programs:
            identity = hashes[p.code]
            key = p.stage, identity
            if identity in identities and identities[identity] != p.code:
                raise ValueError("asset shader hash collision")
            identities[identity] = p.code
            # Vertex templates have NOT been linked against a vertex declaration.
            # They must never satisfy a request for a runtime vertex identity.
            destination = self.entries if p.stage == "pixel" else self.templates
            destination.setdefault(key, []).append({"asset": asset, "entry": p.entry,
                "permutation": f"{p.permutation:016x}", "offset": p.offset,
                "bytes": len(p.code)})
            if p.stage == "pixel":
                self.resolved[key] = p.code

    def add_vertex_sources(self, image, manifest, executable_source):
        if self.vertex_linked:
            raise ValueError("vertex sources already resolved; create a new resolver for changed content")
        layouts, masks, sizes, bootstraps = shader_vertex_link.source_layouts(image, manifest)
        generated, requests, total_bytes = {}, [], 0
        for asset, p in self.programs:
            if p.stage != "vertex":
                continue
            for name, elements, strides in layouts:
                key = p.code, p.fetches, name
                if key not in generated:
                    code = shader_vertex_link.link(p, elements, strides, masks, sizes)
                    total_bytes += len(code)
                    if len(generated) >= MAX_RECORDS - len(bootstraps) or total_bytes > 64 * 1024 * 1024:
                        raise ValueError("vertex resolution generation exceeds work/byte limit")
                    generated[key] = code
                requests.append((key, {"asset": asset, "entry": p.entry,
                    "permutation": f"{p.permutation:016x}", "offset": p.offset,
                    "bytes": len(p.code), "vertex_layout": name}))
        codes = list(generated.values()) + [code for _, code in bootstraps]
        hashes = self.hasher(codes)
        identities = dict(zip(generated, hashes))
        pending = {}
        for code, identity in zip(codes, hashes):
            key = "vertex", identity
            if key in pending and pending[key] != code:
                raise ValueError("linked vertex shader hash collision")
            for (_, known_hash), known in self.resolved.items():
                if identity == known_hash and code != known:
                    raise ValueError("linked/asset shader hash collision")
            pending[key] = code
        entries = {}
        for key, locator in requests:
            entries.setdefault(("vertex", identities[key]), []).append(locator)
        for (row, code), identity in zip(bootstraps, hashes[len(generated):]):
            if row.get("hash") != identity:
                raise ValueError("bootstrap shader identity mismatch")
            entries.setdefault(("vertex", identity), []).append({"asset": "default.xex",
                "entry": row.get("name"), "offset": row["offset"], "bytes": len(code)})
        # Publish only after all source, layout and hash checks succeeded.
        self.entries.update(entries)
        self.resolved.update(pending)
        self.executable_source = executable_source
        self.linkage_fingerprint = sha256(json.dumps(manifest, sort_keys=True,
                                                    separators=(",", ":")).encode())
        self.vertex_linked = True

    def resolve(self, stage, identity):
        return self.resolved.get((stage, identity))

    def index(self):
        sources = [{"name": name, "bytes": len(data), "sha256": sha256(data), **source}
                   for name, (data, source) in sorted(self.assets.items())]
        if self.executable_source:
            sources.append(self.executable_source)
        fingerprint = sha256(json.dumps([(s["name"], s["sha256"]) for s in sources],
                                        separators=(",", ":")).encode())
        def rows(table):
            return [{"stage": stage, "hash": identity, "sources": locators}
                    for (stage, identity), locators in sorted(table.items())]
        return {"schema": SCHEMA, "title_id": "5841122D", "resolver": "xobx-v1-d3d9-2",
                "identity": "XXH3_64bits(guest-endian microcode)",
                "content_fingerprint": fingerprint, "assets": sources,
                "linkage_fingerprint": self.linkage_fingerprint,
                "resolved": rows(self.entries), "unlinked_vertex_templates": rows(self.templates),
                "blockers": [] if self.vertex_linked else ["Installed executable vertex sources have not been resolved."],
                "live_ready": False}

    def coverage(self, requests):
        results = []
        for stage, identity, expected in requests:
            code = self.resolve(stage, identity)
            if code is not None and expected is not None and code != expected:
                raise ValueError(f"runtime/asset shader hash collision: {identity}")
            results.append({"stage": stage, "hash": identity,
                "resolved": code is not None,
                "reason": None if code is not None else (
                    ("vertex_layout_not_covered" if self.vertex_linked else "vertex_declaration_linker_required")
                    if stage == "vertex" else "absent_from_installed_assets")})
        return {"required": len(results), "resolved": sum(r["resolved"] for r in results),
                "unresolved": sum(not r["resolved"] for r in results),
                "complete": bool(results) and all(r["resolved"] for r in results),
                "entries": results}


def catalogue_requests(data):
    if not isinstance(data, dict) or data.get("schema") != SCHEMA or data.get("title_id") != "5841122D":
        raise ValueError("unsupported shader identity catalogue")
    rows = data.get("shaders")
    if not isinstance(rows, list) or len(rows) > MAX_RECORDS:
        raise ValueError("invalid catalogue shader table")
    requests = set()
    for row in rows:
        if not isinstance(row, dict) or row.get("stage") not in ("pixel", "vertex") or not isinstance(row.get("hash"), str) or not re.fullmatch("[0-9a-fA-F]{16}", row["hash"]):
            raise ValueError("invalid shader identity")
        requests.add((row["stage"], row["hash"].lower(), None))
    return sorted(requests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--effective-overlay-root", type=Path,
                        help="actually mounted provider root; omit for an unoverlaid root")
    parser.add_argument("--hash-helper", type=Path, default=ROOT / "out/build/win-amd64-release/rb_blitz_shader_microcode_hash.exe")
    parser.add_argument("--catalogue", type=Path, default=ROOT / "config/shader_resolution_candidate.json",
                        help="schema-1 hash/stage-only candidate catalogue")
    parser.add_argument("--vertex-layouts", type=Path, default=ROOT / "config/shader_vertex_layouts.json")
    parser.add_argument("--runtime", type=Path, action="append", default=[],
                        help="optional local .xsh validation evidence, never a resolution source")
    parser.add_argument("--output", type=Path, required=True, help="new local JSON index (no shader bytes)")
    args = parser.parse_args()
    try:
        hasher = lambda codes: sdk_hashes(codes, args.hash_helper.resolve())
        assets, provenance = installed_assets(args.game_root, args.effective_overlay_root)
        resolver = Resolver(assets, hasher)
        vertex_error = None
        try:
            from shader_xex_source import load_supported_image
            image, executable_source = load_supported_image(args.game_root, ROOT)
            resolver.add_vertex_sources(image, json.loads(read_bounded(args.vertex_layouts)), executable_source)
        except (ValueError, OSError, ImportError, struct.error) as error:
            vertex_error = str(error)
        report = resolver.index()
        if vertex_error:
            report["blockers"].append(vertex_error)
        report["installation"] = {"game_root": str(args.game_root.resolve()),
            "effective_overlay_root": str(args.effective_overlay_root.resolve()) if args.effective_overlay_root else None,
            "headers": provenance}
        if args.catalogue:
            report["catalogue"] = resolver.coverage(catalogue_requests(json.loads(read_bounded(args.catalogue))))
        report["runtime_evidence"] = []
        for runtime in args.runtime:
            raw = read_bounded(runtime)
            coverage = resolver.coverage(parse_xsh(raw, hasher))
            report["runtime_evidence"].append({"path": str(runtime.resolve()),
                "sha256": sha256(raw), **coverage})
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive create: never overwrite a cache, catalogue, or earlier evidence.
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2)
            stream.write("\n")
        print(f"{len(resolver.entries)} resolved shader identities; {len(resolver.templates)} vertex source templates")
        if "catalogue" in report:
            coverage = report["catalogue"]
            print(f"Candidate: {coverage['resolved']}/{coverage['required']} resolved; {coverage['unresolved']} unresolved")
        for evidence in report["runtime_evidence"]:
            print(f"{evidence['path']}: {evidence['resolved']}/{evidence['required']} resolved, {evidence['unresolved']} blocked")
        print(f"Index: {args.output}; live_ready=false")
        # Resolved bytes are still separate from live GPU residency (live_ready=false).
        complete = report.get("catalogue", {}).get("complete", False)
        complete = complete and all(e["complete"] for e in report["runtime_evidence"])
        return 0 if complete and not report["blockers"] else 2
    except (ValueError, OSError, subprocess.SubprocessError, UnicodeError,
            struct.error, RecursionError) as error:
        print(f"shader resolver: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
