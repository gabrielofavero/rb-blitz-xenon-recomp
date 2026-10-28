"""Read the container of a Harmonix ``.milo_xbox`` scene.

A scene is not a flat blob: it is a **ChunkStream**, the engine's chunked stream
format. It starts with the 0x810-byte ``ChunkInfo`` block, and the chunk payloads
follow it in order:

    +0x000  u32  stream id: 0xCDBEDEAF compressed, 0xCABEDEAF stored uncompressed
    +0x004  u32  0x810, the size of this block
    +0x008  u32  chunk count
    +0x00C  u32  largest decompressed chunk
    +0x010  u32  one word per chunk, up to 512 of them:
                   bits 0-23  stored size in bytes
                   bit  24    set when the chunk is stored *decompressed*
                   bits 25-31 unused

Every field is little-endian: the scenes were baked by the PC toolchain and the
engine byte-swaps on demand, which is also why a scene's string table is already
plaintext in the dump. A compressed chunk is ``[u32 decompressed size][raw deflate]``
- checked across the scenes here, every compressed chunk decodes as raw deflate with
that size in front - so this reader needs nothing but the standard library.

What it gives you: the chunk table of any scene, the decompressed stream, and the
asset names the scene declares, which is what tells you *which* screen owns which
texture (``splash.milo_xbox`` declares 13 `.tex` assets, `list_panel.tex` among them).

For the measured button, extra-icon and controller-config scenes, ``texture`` finds
the 23-byte DXT5 header and its following eight-byte zero preamble, then exports
the first mip level. ``slots`` compares the Xbox 360 and PS3 button-font Glyph tables
and writes the original-art-free authoring guides. Other scenes and formats are
not assumed to share this layout.

The `.tex` names and the corpus are here to work against:

    python scripts/hmx_milo.py info  assets/game/360/ui/splash/gen/splash.milo_xbox
    python scripts/hmx_milo.py list  assets/game/360/ui/splash/gen/splash.milo_xbox --assets
    python scripts/hmx_milo.py dump  assets/game/360/ui/splash/gen/splash.milo_xbox --chunk 3

References: the chunk table and the deflate framing come from the RB3 decompilation
(`freeqaz/rb3-xenon`, ``utl/ChunkStream``); every claim here was re-verified against
the dump rather than assumed.
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import zlib

CHUNK_INFO_SIZE = 0x810
CHUNK_SIZE_MASK = 0x00FFFFFF
CHUNK_STORED_MASK = 0x01000000
STREAM_IDS = {0xCDBEDEAF: "deflate", 0xCABEDEAF: "stored"}
STREAM_STORED = 0xCABEDEAF
STREAM_DEFLATE = 0xCDBEDEAF


class MiloError(Exception):
    pass


class Chunk:
    """One entry of the chunk table, with the bytes it stores."""

    def __init__(self, index, word, offset, data):
        self.index = index
        self.word = word
        self.stored_size = word & CHUNK_SIZE_MASK
        self.stored = bool(word & CHUNK_STORED_MASK)
        self.offset = offset
        self.data = data

    def describe(self, codec):
        what = "decompressed" if self.stored else f"compressed ({codec})"
        return (f"  chunk {self.index:>3}: {self.stored_size:>9} B {what:<18} "
                f"at 0x{self.offset:08x}")


class Milo:
    def __init__(self, path, data):
        self.path = path
        self.data = data
        if len(data) < CHUNK_INFO_SIZE:
            raise MiloError(f"{path} is shorter than a {CHUNK_INFO_SIZE}-byte chunk table")
        self.id, self.info_size, self.chunk_count, self.max_chunk_size = \
            struct.unpack_from("<IIII", data, 0)
        self.codec = STREAM_IDS.get(self.id)
        if self.codec is None:
            raise MiloError(f"{path} starts with {self.id:#010x}, which is not a chunk "
                            f"stream (expected "
                            f"{', '.join(f'{i:#010x}' for i in STREAM_IDS)})")
        if self.info_size != CHUNK_INFO_SIZE:
            raise MiloError(f"{path} declares a {self.info_size:#x}-byte chunk table, "
                            f"expected {CHUNK_INFO_SIZE:#x}")
        if not 1 <= self.chunk_count <= 512:
            raise MiloError(f"{path} declares {self.chunk_count} chunks; the format "
                            f"allows 1 to 512")
        self.chunks = []
        self._stream = None
        offset = self.info_size
        for index in range(self.chunk_count):
            word, = struct.unpack_from("<I", data, 0x10 + 4 * index)
            size = word & CHUNK_SIZE_MASK
            if size == 0 or offset + size > len(data):
                raise MiloError(f"{path} chunk {index} claims {size} bytes at {offset}, "
                                f"which does not fit in the {len(data)}-byte file")
            self.chunks.append(Chunk(index, word, offset, data[offset:offset + size]))
            offset += size
        self.trailing = len(data) - offset

    def decompressed(self, index):
        """One chunk's payload: stored as-is, or raw deflate with its size in front."""
        chunk = self.chunks[index]
        if chunk.stored or self.id == STREAM_STORED:
            return chunk.data
        if len(chunk.data) < 4:
            raise MiloError(f"{self.path} chunk {index} is too short to hold a size")
        expected, = struct.unpack_from("<I", chunk.data, 0)
        try:
            out = zlib.decompress(chunk.data[4:], -15)
        except zlib.error as exc:
            raise MiloError(f"{self.path} chunk {index} is not raw deflate: {exc}") from exc
        if len(out) != expected:
            raise MiloError(f"{self.path} chunk {index} declares {expected} bytes but "
                            f"deflated to {len(out)}")
        return out

    def stream(self):
        """Every chunk decompressed and concatenated - what the engine's reader sees."""
        if self._stream is None:
            self._stream = b"".join(self.decompressed(index)
                                    for index in range(len(self.chunks)))
        return self._stream

    def assets(self):
        """The names the scene declares, in table order, from its string table."""
        names = []
        for raw in re.findall(rb"[ -~]{3,}", self.stream()):
            name = raw.decode("ascii")
            if name not in names:
                names.append(name)
        return names

    def textures(self):
        """The scene's texture assets, which is what a swap has to aim at."""
        return [name for name in self.assets() if name.endswith(".tex")]

    def texture_records(self):
        """Every texture's record: its builder-side source path and its dimensions.

        A texture asset is serialised as a length-prefixed source path followed by
        the fields the engine loads it with - the interesting ones being three
        big-endian 16-bit values, width, height and row bytes, so the bits per
        pixel follows from ``row_bytes * 8 / width``. The path is what tells a
        reskin which file it is replacing (`img/xbox_0.png`,
        `../image/icons_buttons_xbox_nomip.bmp`), and the dimensions are what the
        pixel data has to match. ``scene_textures`` validates the measured inline
        DXT5 record for the three scene families used by button schemes.
        """
        stream = self.stream()
        records = []
        for match in re.finditer(rb"\.(?:bmp|BMP|png|PNG)\b", stream):
            end = match.end()
            path = None
            for back in range(1, 64):
                if stream[end - back] != back - 1:
                    continue
                start = end - back + 1
                if all(32 <= byte < 127 for byte in stream[start:end]):
                    path = stream[start:end].decode("latin-1")
                break
            if path is None:
                continue
            dims = self._dimensions_after(stream, end)
            if dims is None:
                continue
            width, height, row_bytes = dims
            records.append({
                "path": path,
                "width": width,
                "height": height,
                "row_bytes": row_bytes,
                "bpp": row_bytes * 8 // width,
                "at": end,
            })
        return records

    @staticmethod
    def _dimensions_after(stream, at):
        """The width/height/row-bytes triple that follows a source path, if any."""
        window = stream[at:at + 48]
        for offset in range(0, len(window) - 6):
            width, height, row_bytes = struct.unpack_from(">HHH", window, offset)
            if width < 8 or width > 4096 or height < 8 or height > 4096:
                continue
            if width & (width - 1) or height & (height - 1):
                continue
            if row_bytes != width * 8 // 8:  # every texture in these scenes is 8bpp
                continue
            if row_bytes > 4096:
                continue
            return width, height, row_bytes
        return None

    def describe(self):
        lines = [
            f"{self.path}: {len(self.data)} bytes, {self.codec} chunk stream, "
            f"{self.chunk_count} chunk(s), largest decompressed {self.max_chunk_size} B",
        ]
        for chunk in self.chunks:
            lines.append(chunk.describe(self.codec))
        lines.append(f"  {self.trailing} bytes past the last chunk")
        return "\n".join(lines)

    def scene_textures(self):
        """Locate measured DXT5 headers and payloads by their source paths."""
        stream = self.stream()
        found = []
        for source in self.texture_records():
            width, height = source["width"], source["height"]
            if width % 4 or height % 4:
                continue
            payload_size = width * height  # DXT5: 16 bytes per 4x4 block.
            for at in range(source["at"], min(source["at"] + 80, len(stream) - 31)):
                record = stream[at:at + 23]
                if (record[4:9] != b"\x08\x00\x00\x00\x18" or
                        record[10:16] != struct.pack(">HHH", width, height,
                                                     source["row_bytes"]) or
                        record[16:23] != bytes(7) or
                        stream[at + 23:at + 31] != bytes(8)):
                    continue
                payload_at = at + 31
                if payload_at + payload_size > len(stream):
                    raise MiloError(f"{source['path']}: DXT5 payload is truncated")
                found.append({**source, "record_at": at, "payload_at": payload_at,
                              "payload_size": payload_size, "mip_field": record[9]})
                break
        return found


def load(path):
    with open(path, "rb") as handle:
        return Milo(path, handle.read())


# ---------------------------------------------------------------------- commands --
def cmd_info(args):
    status = 0
    for path in args.milo:
        try:
            milo = load(path)
            stream = milo.stream()
            print(milo.describe())
            print(f"  {len(stream)} bytes decompressed, {len(milo.textures())} texture "
                  f"asset(s)")
        except (OSError, MiloError) as exc:
            print(f"{path}: {exc}")
            status = 1
    return status


def cmd_list(args):
    milo = load(args.milo)
    print(milo.describe())
    print(f"  {len(milo.stream())} bytes decompressed")
    names = milo.textures() if args.textures else milo.assets()
    what = "texture asset" if args.textures else "name"
    print(f"  {len(names)} {what}(s) in the string table")
    for name in names:
        print(f"    {name}")
    return 0


def cmd_dump(args):
    milo = load(args.milo)
    if not 0 <= args.chunk < len(milo.chunks):
        raise MiloError(f"{milo.path} has {len(milo.chunks)} chunk(s)")
    data = milo.decompressed(args.chunk)
    out = args.out or f"{os.path.basename(args.milo)}.chunk{args.chunk}.bin"
    with open(out, "wb") as handle:
        handle.write(data)
    print(f"{milo.path} chunk {args.chunk} -> {out} ({len(data)} bytes decompressed)")
    return 0


def cmd_records(args):
    """List the texture records of one or more scenes (or every scene under a root)."""
    paths = []
    for target in args.path:
        if os.path.isdir(target):
            for root, dirs, files in os.walk(target):
                paths += [os.path.join(root, f) for f in files if f.endswith(".milo_xbox")]
        else:
            paths.append(target)
    total = 0
    for path in sorted(paths):
        try:
            records = load(path).texture_records()
        except (OSError, MiloError) as exc:
            if args.verbose:
                print(f"{path}: {exc}")
            continue
        if not records:
            continue
        total += len(records)
        print(f"\n{os.path.relpath(path, args.root) if args.root else path}")
        for record in records:
            print(f"  {record['width']:>5}x{record['height']:<5} {record['bpp']:>3}bpp  "
                  f"{record['path']}")
    print(f"\n{total} texture record(s) in {len(paths)} scene(s)")
    return 0


def _require_pillow():
    try:
        from PIL import Image, ImageDraw
    except ImportError as exc:
        raise MiloError("PNG output requires Pillow (python -m pip install Pillow)") from exc
    return Image, ImageDraw


def cmd_texture(args):
    """Export the first DXT5 mip of a measured scene texture."""
    milo = load(args.milo)
    found = milo.scene_textures()
    if args.list:
        for item in found:
            print(f"{item['path']}: {item['width']}x{item['height']} DXT5 "
                  f"record=0x{item['record_at']:x} payload=0x{item['payload_at']:x} "
                  f"mip_field={item['mip_field']}")
        return 0
    matches = [item for item in found if item["path"] == args.asset]
    if len(matches) != 1:
        raise MiloError(f"expected one measured DXT5 record for {args.asset!r}; "
                        f"found {len(matches)} (use --list)")
    item = matches[0]
    import hmx_tex
    Image, _ = _require_pillow()
    start = item["payload_at"]
    encoded = milo.stream()[start:start + item["payload_size"]]
    # MiloLib's RndBitmap export swaps Xbox words and leaves PS3 words as-is.
    # Both scenes use the same BC3 block layout after this platform step.
    if milo.path.lower().endswith(".milo_xbox"):
        encoded = hmx_tex.unswap(encoded)
    elif not milo.path.lower().endswith(".milo_ps3"):
        raise MiloError("texture export supports .milo_xbox and .milo_ps3 scenes")
    rgba = bytearray(hmx_tex.decode_mip(encoded, 0,
                                        item["width"], item["height"], 16))
    # BC3 leaves arbitrary RGB in fully transparent texels. Clearing those
    # bytes does not change the visible art and avoids noisy PNG previews.
    for pixel in range(0, len(rgba), 4):
        if rgba[pixel + 3] == 0:
            rgba[pixel:pixel + 3] = b"\x00\x00\x00"
    Image.frombytes("RGBA", (item["width"], item["height"]), bytes(rgba)).save(args.out)
    print(f"{item['path']} -> {args.out} ({item['width']}x{item['height']} "
          f"DXT5, record 0x{item['record_at']:x}, payload 0x{start:x})")
    return 0


def button_glyphs(milo):
    """Read both measured Buttons RndFont Glyph arrays, without fixed offsets."""
    stream = milo.stream()
    marker = b"\x00\x00\x00\x22\x00\x20"  # count 34, first Glyph is space
    positions = [match.start() for match in re.finditer(re.escape(marker), stream)]
    candidates = []
    for at in positions:
        if struct.pack(">ff", 85.0, 85.0) not in stream[max(0, at - 256):at]:
            continue  # Both font objects declare an 85 x 85 base glyph.
        first = at + 4
        if first + 34 * 18 > len(stream):
            continue
        glyphs = [struct.unpack_from(">Hffff", stream, first + i * 18)
                  for i in range(34)]
        chars = [glyph[0] for glyph in glyphs]
        if chars[:2] != [0x20, 0x26] or chars[-1] != 0xa0 or len(set(chars)) != 34:
            continue
        if not all(all(math.isfinite(v) and 0 <= v <= 1.1 for v in glyph[1:])
                   for glyph in glyphs):
            continue
        candidates.append(glyphs)
    if len(candidates) != 2:
        raise MiloError(f"{milo.path}: expected two 34-entry Buttons Glyph tables; "
                        f"found {len(candidates)}")
    return candidates


_GLYPH_IDS = {
    "A": "a_button", "B": "b_button", "X": "x_button", "Y": "y_button",
    "L": "left_bumper", "R": "right_bumper",
    "l": "left_trigger", "r": "right_trigger",
    "S": "next_arrow", "s": "previous_arrow", "D": "dpad",
    "w": "dpad_up", "d": "dpad_right", "x": "dpad_down",
    "a": "dpad_left", "[": "left_stick", "]": "right_stick",
    "1": "instrument_drums", "2": "instrument_bass",
    "3": "instrument_guitar", "4": "instrument_vocals", "5": "instrument_keys",
    "6": "left_stick_up", "7": "left_stick_right",
    "8": "left_stick_down", "9": "left_stick_left",
    "^": "right_stick_up", "&": "right_stick_right",
    "(": "right_stick_left", "*": "right_stick_down",
}

# The font table's trigger height is shorter than the visible Xbox 360 art.
# Preserve the complete image when exporting slots and composing replacements.
_ART_HEIGHT_OVERRIDES = {"left_trigger": 83, "right_trigger": 83}


def _slots(tables):
    slots = []
    for charcode, u, v, scale_x, scale_y in tables[0]:
        if charcode in (0x20, 0xa0):
            continue  # Whitespace is in the font table but has no image slot.
        char = chr(charcode)
        # The font declares an 85 px base glyph. The Glyph entries give UV
        # origins in the 512 px sheet and size factors relative to that base.
        x, y = round(u * 512), round(v * 512)
        width, height = round(scale_x * 85), round(scale_y * 85)
        if not (0 <= x < 512 and 0 <= y < 512 and
                0 < width <= 512 - x and 0 < height <= 512 - y):
            raise MiloError(f"Glyph U+{charcode:04X} has an invalid rectangle")
        alternate = [g for g in tables[1] if g[1:] == (u, v, scale_x, scale_y)]
        if len(alternate) != 1:
            raise MiloError(f"Glyph U+{charcode:04X}: second font has no matching rectangle")
        glyph_chars = sorted({char, chr(alternate[0][0])})
        slot_id = _GLYPH_IDS.get(char, f"glyph_{charcode:04x}")
        height = _ART_HEIGHT_OVERRIDES.get(slot_id, height)
        slots.append({"id": slot_id,
                      "glyphs": glyph_chars, "rect": (x, y, width, height),
                      "row": y // 85, "column": x // 85})
    for i, a in enumerate(slots):
        ax, ay, aw, ah = a["rect"]
        for b in slots[i + 1:]:
            bx, by, bw, bh = b["rect"]
            if ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah:
                raise MiloError(f"Glyph rectangles overlap: {a['id']} and {b['id']}")
    return sorted(slots, key=lambda s: (s["row"], s["column"], s["id"]))


def cmd_slots(args):
    xbox, ps3 = load(args.xbox), load(args.ps3)
    xbox_tables, ps3_tables = button_glyphs(xbox), button_glyphs(ps3)
    if xbox_tables != ps3_tables:
        raise MiloError("Xbox 360 and PS3 Buttons Glyph tables differ; no "
                        "slot-for-slot authoring guide can be generated")
    slots = _slots(xbox_tables)
    Image, ImageDraw = _require_pillow()
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(args.buttons_template)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(args.pad_template)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Generated from matching Xbox 360 and PS3 Buttons Glyph tables.\n"
                     "# Named controls were checked against the correctly decoded Xbox 360 sheet.\n"
                     "# Trigger heights include visible atlas art below the font table's 63/64 px bounds.\n"
                     "# Unnamed glyphs retain their character code as a stable identifier.\n"
                     "schema_version = 1\nsheet_size = [512, 512]\n\n")
        for slot in slots:
            handle.write("[[slot]]\n"
                         f"id = \"{slot['id']}\"\n"
                         f"glyphs = {json.dumps(slot['glyphs'])}\n"
                         f"rect = {list(slot['rect'])}\n"
                         f"row = {slot['row']}\ncolumn = {slot['column']}\n\n")
    guide = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    draw = ImageDraw.Draw(guide)
    for slot in slots:
        x, y, width, height = slot["rect"]
        draw.rectangle((x, y, x + width - 1, y + height - 1),
                       outline=(125, 190, 255, 220), width=1)
        caption = "/".join(slot["glyphs"]) if width < 50 else slot["id"]
        draw.text((x + 2, y + 2), caption, fill=(255, 255, 255, 255),
                  stroke_width=1, stroke_fill=(0, 0, 0, 255))
    guide.save(args.buttons_template)
    pad = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    draw = ImageDraw.Draw(pad)
    draw.rectangle((1, 1, 1022, 1022), outline=(90, 150, 220, 180), width=2)
    draw.rectangle((128, 128, 895, 895), outline=(125, 190, 255, 220), width=2)
    draw.line((512, 128, 512, 896), fill=(125, 190, 255, 100))
    draw.line((128, 512, 896, 512), fill=(125, 190, 255, 100))
    draw.text((144, 144), "Controller art guide - transparent background",
              fill=(255, 255, 255, 255), stroke_width=1,
              stroke_fill=(0, 0, 0, 255))
    pad.save(args.pad_template)
    print(f"matching Glyph tables: {len(xbox_tables)} variants, "
          f"{len(xbox_tables[0])} entries each, {len(slots)} "
          f"drawable slots -> {args.out}")
    print(f"blank guides -> {args.buttons_template}, {args.pad_template}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="hmx_milo.py",
        description="Read the chunk stream of a .milo_xbox scene: its chunks and the "
                    "names of the assets it declares.")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="describe the chunk table")
    p.add_argument("milo", nargs="+")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("list", help="chunk table and asset names")
    p.add_argument("milo")
    p.add_argument("-t", "--textures", action="store_true",
                   help="only the .tex assets")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("dump", help="write one chunk's decompressed bytes out")
    p.add_argument("milo")
    p.add_argument("--chunk", type=int, default=0)
    p.add_argument("--out")
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser("records", help="list every texture's source path and dimensions")
    p.add_argument("path", nargs="+", help="a scene, or a directory to walk")
    p.add_argument("--root", help="strip this prefix from the printed paths")
    p.add_argument("-v", "--verbose", action="store_true",
                   help="also report files that are not chunk streams")
    p.set_defaults(func=cmd_records)

    p = sub.add_parser("texture", help="export measured scene DXT5 texture as PNG")
    p.add_argument("milo")
    p.add_argument("--list", action="store_true", help="show available texture records")
    p.add_argument("--asset", help="builder-side source path shown by --list")
    p.add_argument("--out", help="output PNG path")
    p.set_defaults(func=cmd_texture)

    p = sub.add_parser("slots", help="compare button Glyph tables and write blank guides")
    p.add_argument("xbox", help="Xbox 360 buttons.milo_xbox")
    p.add_argument("ps3", help="PS3 buttons.milo_ps3")
    p.add_argument("--out", default="assets/controller/slots.toml")
    p.add_argument("--buttons-template", default="assets/controller/template/buttons.png")
    p.add_argument("--pad-template", default="assets/controller/template/pad.png")
    p.set_defaults(func=cmd_slots)

    args = parser.parse_args(argv)
    try:
        if args.command == "texture" and not args.list and not (args.asset and args.out):
            parser.error("texture needs --asset and --out, or --list")
        return args.func(args)
    except (OSError, MiloError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
