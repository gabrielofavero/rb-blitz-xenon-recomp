"""Read and extract Harmonix ARK archives (Rock Band / Rock Band Blitz / Guitar
Hero style ``.hdr`` + ``.ark`` pairs).

The archive is split in two files: the ``.hdr`` carries an encrypted index (file
names, sizes, decompressed sizes and offsets), and one or more ``.ark`` files
hold the concatenated entry data. Nothing in this tool touches the game's code;
it only unpacks the container, byte for byte.

Header decryption (ARK v4 and later) is the Park-Miller LCG stream cipher the
title itself uses: the first little-endian dword seed of the file seeds a
``16807 * key mod 2147483647`` generator whose successive low bytes are XORed
over the rest of the file. A header that is *not* encrypted already starts with
its version (``<= 10``) and is read as-is.

Known entry payloads are stored raw, exactly as the game writes them:
``.milo_xbox``, ``.mid``, ``.mogg`` and ``.bik`` are plaintext, while ``.dtb``
and the ``.png_xbox`` / ``.bmp_xbox`` textures carry a further engine-side
obfuscation layer that is deliberately *not* undone here.

The layout and the cipher come from the public community description of these
archives (OpenNyx's ``Onyx.Harmonix.Ark``, and the dtbcrypt LCG xorloser
published for the same generator). This reader is an independent implementation,
verified against the retail dump - it is not a port of either.

Usage:
    python scripts/hmx_ark.py list    --hdr game/gen/main_xbox.hdr
    python scripts/hmx_ark.py extract --hdr game/gen/main_xbox.hdr --out extracted
    python scripts/hmx_ark.py extract --hdr game/gen/main_xbox.hdr --out extracted \\
                                      --manifest extracted/_ark_main.json
"""
import argparse
import json
import os
import struct
import sys
import zlib
from dataclasses import dataclass, field

# Park-Miller minimal-standard constants, spelled the way the title spells them
# (Schrage's method so the 32-bit product cannot overflow).
_LCG_MOD = 0x7FFFFFFF
_LCG_MUL = 0x41A7  # 16807
_LCG_DIV = 0x1F31D  # 127773 = 2147483647 // 16807
_LCG_REM = 0xB14  # 2836   = 2147483647 % 16807

_MAX_PLAINTEXT_VERSION = 10


def _to_int32(value):
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def _trunc_div(a, b):
    """C-style integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def _crypt_round(key):
    q = _trunc_div(key, _LCG_DIV)
    ret = _to_int32((key - q * _LCG_DIV) * _LCG_MUL - q * _LCG_REM)
    return ret + _LCG_MOD if ret <= 0 else ret


def _keystream(seed, length):
    out = bytearray(length)
    key = seed
    for i in range(length):
        out[i] = key & 0xFF
        key = _crypt_round(key)
    return bytes(out)


def decrypt_header(blob):
    """Return the plaintext header index for an encrypted (or raw) ``.hdr``."""
    if len(blob) < 4:
        raise ValueError("header file is too small to hold a version")
    if struct.unpack_from("<I", blob, 0)[0] <= _MAX_PLAINTEXT_VERSION:
        return blob

    seed = _crypt_round(struct.unpack_from("<i", blob, 0)[0])
    stream = _keystream(seed, len(blob) - 4)
    body = bytes(b ^ k for b, k in zip(blob[4:], stream))
    if struct.unpack_from("<i", body, 0)[0] < 0:
        body = bytes(b ^ (k ^ 0xFF) for b, k in zip(blob[4:], stream))
    return body


class _Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def _take(self, size):
        if self.pos + size > len(self.data):
            raise ValueError(
                f"header is truncated: wanted {size} bytes at 0x{self.pos:X}, "
                f"only {len(self.data) - self.pos} left"
            )
        out = self.data[self.pos : self.pos + size]
        self.pos += size
        return out

    def u32(self):
        return struct.unpack("<I", self._take(4))[0]

    def i32(self):
        return struct.unpack("<i", self._take(4))[0]

    def u64(self):
        return struct.unpack("<Q", self._take(8))[0]

    def i64(self):
        return struct.unpack("<q", self._take(8))[0]

    def bytes(self, size):
        return self._take(size)


@dataclass
class FileEntry:
    offset: int
    name: bytes
    folder: bytes = None
    size: int = 0
    inflate: int = 0  # decompressed size, 0 when the entry is stored as-is

    @property
    def path(self):
        name = self.name.decode("latin-1")
        if self.folder:
            return f"{self.folder.decode('latin-1')}/{name}"
        return name

    @property
    def compressed(self):
        return self.inflate not in (0, self.size)


@dataclass
class ArkHeader:
    version: int
    arks: list = field(default_factory=list)  # (path or None, size)
    files: list = field(default_factory=list)
    extra: bytes = b""  # version 6's leading 20-byte hash/key block
    checksums: bytes = b""  # the per-part checksum block, kept verbatim on rebuild

    def ark_parts(self, header_path):
        """Resolve every ark part next to the header, in archive order."""
        header_dir = os.path.dirname(os.path.abspath(header_path))
        stem = os.path.splitext(os.path.basename(header_path))[0]
        parts = []
        for index, (path, _size) in enumerate(self.arks):
            if path is None:
                candidates = [
                    os.path.join(header_dir, f"{stem}_{index}.ark"),
                    os.path.join(header_dir, f"{stem}_{index}.ARK"),
                ]
            else:
                text = path.decode("latin-1")
                candidates = [
                    os.path.join(header_dir, text),
                    os.path.join(header_dir, os.path.basename(text)),
                    os.path.join(os.path.dirname(header_dir), text),
                ]
            parts.append(_first_existing(candidates) or candidates[0])
        return parts

    def select_ark(self, offset):
        if not self.arks:
            return 0, offset
        remaining = offset
        for index, (_path, size) in enumerate(self.arks):
            if remaining < size:
                return index, remaining
            remaining -= size
        raise ValueError(f"entry offset 0x{offset:X} lies past the last ark part")


def _first_existing(candidates):
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    return None


def _resolve_string(strings, offsets, index):
    if index < 0 or index >= len(offsets):
        raise ValueError(f"string index {index} is out of range")
    start = offsets[index]
    end = strings.find(b"\x00", start)
    if end < 0:
        raise ValueError(f"string at offset {start} is not NUL-terminated")
    return strings[start:end]


def parse_header(blob):
    reader = _Reader(decrypt_header(blob))
    version = reader.u32()
    extra = b""
    checksums = b""

    def parse_strings():
        return reader.bytes(reader.u32())

    def parse_offsets():
        return [reader.u32() for _ in range(reader.u32())]

    def parse_entries(read_offset):
        count = reader.u32()
        entries = []
        for _ in range(count):
            entries.append(
                FileEntry(
                    offset=read_offset(),
                    name=reader.i32(),
                    folder=reader.i32(),
                    size=reader.u32(),
                    inflate=reader.u32(),
                )
            )
        return entries

    def resolve(entries, strings, offsets):
        for entry in entries:
            entry.name = _resolve_string(strings, offsets, entry.name)
            if entry.folder != -1:
                entry.folder = _resolve_string(strings, offsets, entry.folder)
            else:
                entry.folder = None
        return entries

    if version == 2:  # Amplitude (PS2)
        entries = parse_entries(reader.u32)
        strings = parse_strings()
        offsets = parse_offsets()
        files = resolve(entries, strings, offsets)
    elif version == 3:  # Guitar Hero 1/2/80s
        ark_count = reader.u32()
        _expect_equal(reader.u32(), ark_count, "ARK v3 ark count")
        arks = [(None, reader.u32()) for _ in range(ark_count)]
        strings = parse_strings()
        offsets = parse_offsets()
        files = resolve(parse_entries(reader.u32), strings, offsets)
    elif version == 4:  # Rock Band, Rock Band 2 (PS2)
        ark_count = reader.u32()
        _expect_equal(reader.u32(), ark_count, "ARK v4 ark count")
        arks = [(None, reader.u64()) for _ in range(ark_count)]
        strings = parse_strings()
        offsets = parse_offsets()
        files = resolve(parse_entries(reader.u64), strings, offsets)
    elif version in (5, 6):  # Rock Band 2 (360/PS3), Rock Band 3 / Blitz
        if version == 6:
            extra = reader.bytes(20)  # leading hash/key block
        ark_count = reader.u32()
        _expect_equal(reader.u32(), ark_count, f"ARK v{version} ark count")
        sizes = [reader.u32() for _ in range(ark_count)]
        _expect_equal(reader.u32(), ark_count, f"ARK v{version} ark path count")
        paths = [reader.bytes(reader.u32()) for _ in range(ark_count)]
        arks = list(zip(paths, sizes))
        _expect_equal(reader.u32(), ark_count, f"ARK v{version} checksum count")
        checksums = reader.bytes(4 * ark_count)
        strings = parse_strings()
        offsets = parse_offsets()
        files = resolve(parse_entries(reader.i64), strings, offsets)
    elif version == 9:  # Amplitude (PS3)
        reader.bytes(20)
        ark_count = reader.u32()
        _expect_equal(reader.u32(), ark_count, "ARK v9 ark count")
        sizes = [reader.u32() for _ in range(ark_count)]
        _expect_equal(reader.u32(), ark_count, "ARK v9 ark path count")
        paths = [reader.bytes(reader.u32()) for _ in range(ark_count)]
        arks = list(zip(paths, sizes))
        _expect_equal(reader.u32(), ark_count, "ARK v9 checksum count")
        reader.bytes(4 * ark_count)
        _expect_equal(reader.u32(), ark_count, "ARK v9 string count")
        for _ in range(ark_count):
            for _ in range(reader.u32()):
                reader.bytes(reader.u32())
        files = []
        for _ in range(reader.u32()):
            offset = reader.i64()
            path = parse_strings()
            reader.i32()  # flags
            size = reader.u32()
            reader.bytes(4)  # hash
            folder, _, name = path.rpartition(b"/")
            files.append(FileEntry(offset, name, folder or None, size))
    else:
        raise ValueError(
            f"unsupported ARK version {version} "
            "(this tool implements 2-6 and 9)"
        )

    return ArkHeader(version=version, arks=arks, files=files, extra=extra,
                     checksums=checksums)


def _expect_equal(value, expected, what):
    if value != expected:
        raise ValueError(f"{what} mismatch: {value} != {expected}")


def load_header(path):
    with open(path, "rb") as handle:
        return parse_header(handle.read())


def check_layout(header, part_sizes):
    """Report entries that fall outside their ark part; returns problem strings."""
    problems = []
    for entry in header.files:
        try:
            index, offset = header.select_ark(entry.offset)
        except ValueError as exc:
            problems.append(f"{entry.path}: {exc}")
            continue
        if offset + entry.size > part_sizes[index]:
            problems.append(
                f"{entry.path}: {entry.size} bytes at 0x{offset:X} run past ark "
                f"part {index} ({part_sizes[index]} bytes)"
            )
    return problems


def _decompress(raw, expected):
    for wbits in (31, 15, -15):
        try:
            data = zlib.decompress(raw, wbits)
        except zlib.error:
            continue
        if expected and len(data) != expected:
            continue
        return data
    return None


def _normalise_path(entry_path):
    """Map an in-archive path onto a safe relative path.

    Entries are written ``./xbox_shaders`` or, in the RB3-family archives, carry
    the build tree's ``../../system/run/...`` prefix. A ``..`` component is
    renamed to ``dotdot`` - the convention other ARK extractors use - so an entry
    can never escape the output tree, while still keeping every path unique and
    nothing silently dropped. Returns the components and whether one was renamed.
    """
    parts = []
    rewritten = False
    for part in entry_path.replace("\\", "/").split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            rewritten = True
            parts.append("dotdot")
        else:
            parts.append(part)
    if not parts:
        raise ValueError(f"entry has no usable path: {entry_path!r}")
    return parts, rewritten


def extract(header, header_path, out_dir, overwrite=False, log=print):
    parts = header.ark_parts(header_path)
    for path in parts:
        if not os.path.isfile(path):
            raise FileNotFoundError(f"ark part not found: {path}")
    handles = [open(path, "rb") for path in parts]
    written = skipped = inflated = renamed = 0
    problems = []
    try:
        for entry in header.files:
            index, offset = header.select_ark(entry.offset)
            handles[index].seek(offset)
            raw = handles[index].read(entry.size)
            if len(raw) != entry.size:
                problems.append(f"{entry.path}: short read from ark part {index}")
                continue
            if entry.inflate:
                data = _decompress(raw, entry.inflate)
                if data is None:
                    problems.append(
                        f"{entry.path}: not a decodable deflate stream, wrote raw bytes"
                    )
                    data = raw
                else:
                    inflated += 1
            else:
                data = raw
            components, was_rewritten = _normalise_path(entry.path)
            target = os.path.join(out_dir, *components)
            if was_rewritten:
                renamed += 1
            if os.path.exists(target) and not overwrite:
                skipped += 1
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as out:
                out.write(data)
            written += 1
    finally:
        for handle in handles:
            handle.close()
    if inflated:
        log(f"  inflated {inflated} compressed entries")
    if renamed:
        log(f"  kept {renamed} '..' paths inside the tree as dotdot/...")
    if skipped:
        log(f"  skipped {skipped} existing files (pass --overwrite to replace)")
    for problem in problems:
        log(f"  warning: {problem}")
    return written, problems


def write_patched_ark(header, header_path, source_ark, out_path, replacements, overwrite=False):
    """Copy ``source_ark`` to ``out_path`` and overwrite whole entries in it.

    ``replacements`` maps an in-archive entry path to its new bytes. Each must be
    exactly as long as the entry it replaces: the archive index describes offsets and
    sizes, so only a same-size swap keeps the ``.hdr`` valid and needs no rebuild.
    Returns the size of the written archive.
    """
    by_path = {entry.path: entry for entry in header.files}
    plan = []
    for path, data in replacements.items():
        entry = by_path.get(path)
        if entry is None:
            raise ValueError(f"no entry named {path!r} in {header_path}")
        if len(data) != entry.size:
            raise ValueError(
                f"{path} is {entry.size} bytes, the replacement is {len(data)}"
            )
        index, offset = header.select_ark(entry.offset)
        if index != 0:
            raise ValueError(f"{path} lives in ark part {index}, which this tool does not patch")
        plan.append((offset, data, path))

    source = os.path.abspath(source_ark)
    target = os.path.abspath(out_path)
    if source == target:
        raise ValueError("refusing to patch the archive in place; write a copy instead")
    if os.path.exists(target) and not overwrite:
        raise FileExistsError(f"{target} exists (pass --overwrite to replace it)")
    os.makedirs(os.path.dirname(target), exist_ok=True)

    with open(source, "rb") as src, open(target, "wb") as dst:
        while True:
            chunk = src.read(1 << 20)
            if not chunk:
                break
            dst.write(chunk)
    with open(target, "r+b") as dst:
        for offset, data, _path in plan:
            dst.seek(offset)
            dst.write(data)
    return os.path.getsize(target)


def encode_index(entries, part_path, ark_size, extra, checksums, key):
    """Serialise one v6 index for ``entries`` (a list of FileEntry) and encrypt it.

    Only version 6 is written: it is what Rock Band Blitz and its Ultimate payload
    use, and the leading hash block and per-part checksum are carried over verbatim
    from the header the caller names as the donor.
    """
    # Both retail headers keep offset 0 of the string area for the empty string, and
    # the engine reads a value of 0 as "no string". A real name at offset 0 therefore
    # resolves to nothing and the loader dereferences null, so intern from offset 1.
    strings = bytearray(b"\x00")
    offsets = []
    seen = {}

    def intern(text):
        if text not in seen:
            seen[text] = len(offsets)
            offsets.append(len(strings))
            strings.extend(text)
            strings.append(0)
        return seen[text]

    body = bytearray()
    body += struct.pack("<I", 6)
    if len(extra) != 20:
        raise ValueError("a version 6 index needs its 20-byte leading block")
    body += extra
    path = part_path.encode("latin-1")
    body += struct.pack("<III", 1, 1, ark_size)
    body += struct.pack("<I", 1) + struct.pack("<I", len(path)) + path
    body += struct.pack("<I", 1) + checksums[:4].ljust(4, b"\x00")

    table = bytearray(struct.pack("<I", len(entries)))
    for entry in entries:
        table += struct.pack("<QiiII", entry.offset, intern(entry.name),
                             intern(entry.folder) if entry.folder else -1,
                             entry.size, entry.inflate)

    body += struct.pack("<I", len(strings)) + bytes(strings)
    body += struct.pack("<I", len(offsets)) + b"".join(struct.pack("<I", o) for o in offsets)
    body += table

    stream = _keystream(_crypt_round(_to_int32(key)), len(body))
    return struct.pack("<I", key & 0xFFFFFFFF) + bytes(b ^ k for b, k in zip(body, stream))


def write_ark(hdr_path, part_file, entries, donor, part_name=None, key=0x5A5A5A5A,
              extra=None, align=16):
    """Write a one-part version 6 archive (header plus data) from ``entries``.

    ``entries`` maps an in-archive path to its bytes; ``donor`` supplies the leading
    hash block and checksum field, which this tool does not interpret; ``part_name``
    is the path the index records for the data file (an in-game path such as
    ``gen/patch_xbox_0.ark``). Entries are padded to a 16-byte boundary and the
    leading block defaults to the all-ones one a real patch header carries, both of
    which the titles' overlay archives use. Returns the archive size.
    """
    blobs = sorted(entries.items())
    data = bytearray()
    files = []
    for path, blob in blobs:
        if align and len(data) % align:
            data += bytes(align - len(data) % align)
        folder, _, name = path.rpartition("/")
        files.append(FileEntry(offset=len(data), name=name.encode("latin-1"),
                               folder=(folder or ".").encode("latin-1"),
                               size=len(blob), inflate=0))
        data += blob
    index = encode_index(files, part_name or part_file.replace("\\", "/"), len(data),
                         extra if extra is not None else donor.extra, donor.checksums, key)
    for target, blob in ((hdr_path, index), (part_file, bytes(data))):
        os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
        with open(target, "wb") as handle:
            handle.write(blob)
    return len(data)


def manifest(header, header_path, out_dir):
    return {
        "header": os.path.basename(header_path),
        "version": header.version,
        "arks": [
            {"path": path.decode("latin-1") if path else None, "size": size}
            for path, size in header.arks
        ],
        "part_files": [os.path.basename(p) for p in header.ark_parts(header_path)],
        "count": len(header.files),
        "files": [
            {
                "path": entry.path,
                "out": "/".join(_normalise_path(entry.path)[0]),
                "offset": entry.offset,
                "size": entry.size,
                "inflate": entry.inflate,
            }
            for entry in header.files
        ],
    }


def _summarise(header):
    counts = {}
    for entry in header.files:
        name = entry.path.rsplit("/", 1)[-1]
        ext = name.rsplit(".", 1)[-1].lower() if "." in name else "(no extension)"
        counts[ext] = counts.get(ext, 0) + 1
    return sorted(counts.items(), key=lambda item: (-item[1], item[0]))


def cmd_list(args):
    header = load_header(args.hdr)
    print(f"{args.hdr}: ARK v{header.version}, {len(header.arks)} part(s), "
          f"{len(header.files)} entries")
    for path, size in header.arks:
        name = path.decode("latin-1") if path else "(implicit)"
        print(f"  part {name} ({size} bytes)")
    for ext, count in _summarise(header):
        print(f"  {ext:<24} {count}")
    if args.verbose:
        for entry in header.files:
            print(f"  {entry.offset:>12} {entry.size:>10} {entry.path}")
    return 0


def cmd_extract(args):
    header = load_header(args.hdr)
    out_dir = os.path.abspath(args.out)
    total = sum(entry.size for entry in header.files)
    print(f"{args.hdr}: ARK v{header.version}, {len(header.files)} entries, "
          f"{total} bytes")
    if args.dry_run:
        for ext, count in _summarise(header):
            print(f"  {ext:<24} {count}")
        print(f"  would write to {out_dir}")
        return 0

    part_sizes = [os.path.getsize(p) for p in header.ark_parts(args.hdr)]
    problems = check_layout(header, part_sizes)
    for problem in problems:
        print(f"  warning: {problem}")

    written, extract_problems = extract(
        header, args.hdr, out_dir, overwrite=args.overwrite
    )
    print(f"  wrote {written} files to {out_dir}")
    for ext, count in _summarise(header):
        print(f"  {ext:<24} {count}")
    if args.manifest:
        target = os.path.abspath(args.manifest)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as handle:
            json.dump(manifest(header, args.hdr, out_dir), handle, indent=1)
            handle.write("\n")
        print(f"  manifest: {target}")
    return 1 if problems or extract_problems else 0


def cmd_patch(args):
    header = load_header(args.hdr)
    replacements = {}
    for item in args.entry:
        if "=" not in item:
            raise ValueError(f"--entry wants <archive path>=<file>, got {item!r}")
        path, _, filename = item.partition("=")
        with open(filename, "rb") as handle:
            replacements[path] = handle.read()
    source = args.ark or header.ark_parts(args.hdr)[0]
    size = write_patched_ark(header, args.hdr, source, args.out, replacements,
                             overwrite=args.overwrite)
    for path, data in replacements.items():
        print(f"  replaced {path} ({len(data)} bytes)")
    print(f"{source} -> {args.out} ({size} bytes)")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(
        description="Read and extract Harmonix ARK archives (.hdr + .ark)."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    list_parser = sub.add_parser("list", help="print the archive index")
    list_parser.add_argument("--hdr", required=True, help="path to the .hdr")
    list_parser.add_argument("-v", "--verbose", action="store_true",
                             help="print every entry")
    list_parser.set_defaults(func=cmd_list)

    extract_parser = sub.add_parser("extract", help="write every entry to disk")
    extract_parser.add_argument("--hdr", required=True, help="path to the .hdr")
    extract_parser.add_argument("--out", required=True, help="output directory")
    extract_parser.add_argument("--manifest", help="write the index as JSON here")
    extract_parser.add_argument("--overwrite", action="store_true",
                                help="replace files that already exist")
    extract_parser.add_argument("--dry-run", action="store_true",
                                help="report what would be written, then stop")
    extract_parser.set_defaults(func=cmd_extract)

    patch_parser = sub.add_parser(
        "patch", help="write a copy of an ark with whole entries replaced (same size only)"
    )
    patch_parser.add_argument("--hdr", required=True, help="path to the .hdr")
    patch_parser.add_argument("--out", required=True, help="where to write the patched .ark")
    patch_parser.add_argument("--ark", help="source ark (default: the one the .hdr names)")
    patch_parser.add_argument("--entry", action="append", default=[], metavar="PATH=FILE",
                              help="replace an entry; repeatable")
    patch_parser.add_argument("--overwrite", action="store_true")
    patch_parser.set_defaults(func=cmd_patch)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
