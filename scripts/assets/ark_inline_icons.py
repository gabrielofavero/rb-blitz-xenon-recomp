"""Dump the icon bitmaps a UI list scene carries inline.

`rec_icon.milo_xbox` (the recommendation/inbox icon proxy) stores one 512x512 DXT1 icon per
chunk, so the icons can be inspected without the MiloTex/MiloLib route (which does not read
this title's scenes: "Symbol length too high" / missing end bytes).

Measured layout of every icon chunk after the chunk stream is decompressed:

    [0x00]  chunk header (type/flags/ids; not interpreted here)
    [0x20]  u8   source-path length
    [0x21]  the source path, e.g. b"img/icon_more_stars.png"
    +42     the bitmap header, big-endian: (19) bpp = 8, (20) mip levels - 1 = 5,
            (21..22) width = 512, (23..24) height = 512, (25..26) pitch = 256
    +42     the body: 512x512 DXT1, mips 512..16 = 174,720 bytes, in file order (not swapped)

The script wraps each body in the 32-byte `.png_xbox` envelope (src/assets/texture_codec.h)
so `scripts/assets/hmx_tex.py export` can write a PNG. Proven exact on 2026-10-23: the one icon that
also ships as a file (`ui/recommendations/img/gen/icon_more_stars_keep.png_xbox`) is
byte-identical to the inline copy this script extracts.

Usage:
    python scripts/assets/ark_inline_icons.py --hdr game/gen/main_xbox.hdr --out out/_swap/messages-icons
    Get-ChildItem out/_swap/messages-icons/*.png_xbox |
        ForEach-Object { python scripts/assets/hmx_tex.py export $_.FullName --out $_.FullName.Replace('.png_xbox','.png') }
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hmx_ark  # noqa: E402  (sibling tool)
import hmx_milo  # noqa: E402

HEADER_BYTES = 42
BODY_BYTES = 174720
WIDTH = HEIGHT = 512
PITCH = 256
MIPS = 6
NAME_AT = 32


def icons_from(milo_path: str, data: bytes):
    milo = hmx_milo.Milo(milo_path, data)
    found = []
    for index, chunk in enumerate(milo.chunks):
        blob = milo.decompressed(index)
        if len(blob) < NAME_AT + 1 + 3:
            continue
        name_len = blob[NAME_AT]
        if not 3 <= name_len <= 80:
            continue
        name = blob[NAME_AT + 1:NAME_AT + 1 + name_len].decode("latin1")
        if not name.startswith("img/"):
            continue
        head = blob[NAME_AT + 1 + name_len:NAME_AT + 1 + name_len + HEADER_BYTES]
        if len(head) != HEADER_BYTES:
            continue
        bpp, mips = head[19], head[20]
        width = struct.unpack_from(">H", head, 21)[0]
        height = struct.unpack_from(">H", head, 23)[0]
        pitch = struct.unpack_from(">H", head, 25)[0]
        if (bpp, mips, width, height, pitch) != (8, MIPS - 1, WIDTH, HEIGHT, PITCH):
            print(f"  chunk {index}: {name}: skipped (header {bpp} bpp, {mips + 1} mips, "
                  f"{width}x{height}, pitch {pitch})")
            continue
        start = NAME_AT + 1 + name_len + HEADER_BYTES
        body = blob[start:start + BODY_BYTES]
        if len(body) != BODY_BYTES:
            print(f"  chunk {index}: {name}: skipped (body {len(body)} bytes)")
            continue
        envelope = bytearray(32)
        envelope[0] = 2
        struct.pack_into("<H", envelope, 5, 0x0804)  # DXT1
        envelope[10] = MIPS - 1
        struct.pack_into("<H", envelope, 11, WIDTH)
        struct.pack_into("<H", envelope, 13, HEIGHT)
        struct.pack_into("<H", envelope, 15, PITCH)
        found.append((Path(name).stem, bytes(envelope) + body))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--hdr", required=True, help="the archive header, e.g. game/gen/main_xbox.hdr")
    parser.add_argument("--record", default="ui/recommendations/gen/rec_icon.milo_xbox",
                        help="the scene to read (default: the icon proxy)")
    parser.add_argument("--out", required=True, help="output directory")
    args = parser.parse_args(argv)

    header = hmx_ark.load_header(args.hdr)
    entry = next((e for e in header.files if e.path == args.record), None)
    if entry is None:
        print(f"{args.record}: not in {args.hdr}", file=sys.stderr)
        return 1
    parts = header.ark_parts(args.hdr)
    part_index, offset = header.select_ark(entry.offset)
    with open(parts[part_index], "rb") as handle:
        handle.seek(offset)
        data = handle.read(entry.size)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    icons = icons_from(args.record, data)
    for stem, blob in icons:
        target = out / f"{stem}.png_xbox"
        target.write_bytes(blob)
        print(f"{args.record}: {stem} -> {target} ({len(blob)} B)")
    print(f"{len(icons)} icon(s) in {args.record}")
    return 0 if icons else 2


if __name__ == "__main__":
    raise SystemExit(main())
