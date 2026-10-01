"""Read, convert and swap Harmonix console textures (``.png_xbox`` / ``.bmp_xbox``).

A texture file is a 32-byte envelope followed by the raw GPU texture data:

    byte 0     envelope version (1 or 2)
    bytes 1-4  version 2 only: a per-asset 32-bit id. It is not a checksum - files
               with byte-identical pixels carry different ids - so it identifies
               the asset, and a swap keeps the original
    then the signature block every version shares:
       u16le  format word: 0x0804 DXT1, 0x1808 DXT5, 0x2008 two-channel normal
       u16le  zero
       u8     zero
       u8     highest mip index (so levels = this + 1)
       u16le  width
       u16le  height
       ...    zero padding up to byte 32

Everything after byte 32 is the mip chain, largest first, with every 16-bit word
byte-swapped relative to the PC layout: ``.png_xbox`` is the swapped twin of
``.png_ps3``, which is why the engine reads it straight off the disc.

Because a swap keeps the format, the dimensions and the mip count, the rebuilt
file is exactly as large as the original. That is what makes texture swapping
possible without touching the archive index: ``swap`` writes a copy of the
``.ark`` with just that entry's byte range replaced, and the ``.hdr`` is
untouched.

Usage:
    python scripts/hmx_tex.py info   extracted/ui/image/gen/default_album_art_keep.png_xbox
    python scripts/hmx_tex.py export extracted/ui/image/gen/default_album_art_keep.png_xbox --out art.png
    python scripts/hmx_tex.py export extracted/ui/image/gen/default_album_art_keep.png_xbox --out art.dds
    python scripts/hmx_tex.py import art.png --template extracted/ui/image/gen/default_album_art_keep.png_xbox --out new.png_xbox
    python scripts/hmx_tex.py swap   art.png --template extracted/ui/image/gen/default_album_art_keep.png_xbox \\
                                     --hdr game/gen/main_xbox.hdr --patch-dir out/mods-texture

``--patch-dir`` writes a small overlay archive (``gen/patch_xbox.hdr`` plus
``gen/patch_xbox_0.ark``) holding just the swapped texture, which is how the title
loads a title update; ``--out`` instead writes a full-size copy of the archive with
the entry patched in place. Point the game at the first with
``--ultimate_payload_root=out/mods-texture``.

PNG conversion needs Pillow (``pip install pillow``). The DDS path needs nothing
and is byte-exact: export a DDS, edit it in a DXT-aware editor, import it back.

This covers the archive's own texture entries. It does *not* reach the art of the
menus, buttons and layout screens - that lives inside ``ui/**/*.milo_xbox`` scenes,
whose pixel data sits in the engine's ChunkStream chunks (LZX-compressed). See
``docs/assets.md``, "Texture swapping", for which entries the title actually reads
and what the ``.milo_xbox`` half of the job needs.
"""
import argparse
import os
import struct
import sys

HEADER_SIZE = 32
FMT_DXT1 = 0x0804
FMT_DXT5 = 0x1808
FMT_NORMAL = 0x2008
FORMATS = {FMT_DXT1: ("DXT1", 8), FMT_DXT5: ("DXT5", 16), FMT_NORMAL: ("NORMAL", 16)}


class TextureError(Exception):
    pass


# ---------------------------------------------------------------------------
# The envelope
# ---------------------------------------------------------------------------
class Texture:
    """A parsed texture envelope plus its mip chain."""

    def __init__(self, version, header, body):
        self.version = version
        self.header = header  # the 32 original bytes, reused when rebuilding
        self.body = body  # stored bytes (16-bit swapped), largest mip first
        offset = 5 if version == 2 else 1
        self.format_word = struct.unpack_from("<H", header, offset)[0]
        self.format, self.block = FORMATS[self.format_word]
        self.width = struct.unpack_from("<H", header, 11)[0]
        self.height = struct.unpack_from("<H", header, 13)[0]
        self.mip_levels = header[10] + 1

    @property
    def per_file_id(self):
        return self.header[1:5] if self.version == 2 else None

    @property
    def pixels(self):
        """The largest mip as RGBA bytes, with the byte swap undone."""
        return decode_mip(unswap(self.body), 0, self.width, self.height, self.block)

    def describe(self):
        ident = self.per_file_id.hex() if self.per_file_id else "(none)"
        return (f"envelope v{self.version}, {self.format}, {self.width}x{self.height}, "
                f"{self.mip_levels} mip(s), {len(self.body)} bytes of data, id {ident}")


def parse(data):
    if len(data) < HEADER_SIZE:
        raise TextureError(f"file is shorter than the {HEADER_SIZE}-byte envelope")
    version = data[0]
    if version not in (1, 2):
        raise TextureError(f"unknown envelope version {version}")
    offset = 5 if version == 2 else 1
    format_word = struct.unpack_from("<H", data, offset)[0]
    if format_word not in FORMATS:
        raise TextureError(f"unknown texture format word 0x{format_word:04X}")
    if format_word == FMT_NORMAL:
        raise TextureError("the envelope is a two-channel NORMAL texture, not DXT1/DXT5")
    texture = Texture(version, bytes(data[:HEADER_SIZE]), bytes(data[HEADER_SIZE:]))
    expected = mip_chain_size(texture.block, texture.width, texture.height, texture.mip_levels)
    if len(texture.body) != expected:
        raise TextureError(
            f"envelope says {texture.format} {texture.width}x{texture.height} with "
            f"{texture.mip_levels} mip(s) = {expected} bytes, the file carries "
            f"{len(texture.body)}"
        )
    return texture


def load(path):
    with open(path, "rb") as handle:
        return parse(handle.read())


def mip_block_size(block, width, height, level):
    w, h = max(1, width >> level), max(1, height >> level)
    return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block


def mip_chain_size(block, width, height, levels):
    return sum(mip_block_size(block, width, height, i) for i in range(levels))


def unswap(data):
    out = bytearray(data)
    out[0::2], out[1::2] = data[0::2], data[1::2]
    return bytes(out)


# ---------------------------------------------------------------------------
# Block decoding
# ---------------------------------------------------------------------------
def _c565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def _palette4(c0, c1):
    a, b = _c565(c0), _c565(c1)
    if c0 > c1:
        return [a, b,
                tuple((2 * a[i] + b[i]) // 3 for i in range(3)),
                tuple((a[i] + 2 * b[i]) // 3 for i in range(3))]
    return [a, b, tuple((a[i] + b[i]) // 2 for i in range(3)), (0, 0, 0)]


def _alpha_palette(a0, a1):
    if a0 > a1:
        return [a0, a1] + [(a0 * (7 - i) + a1 * i) // 7 for i in range(1, 7)]
    return [a0, a1] + [(a0 * (5 - i) + a1 * i) // 5 for i in range(1, 5)] + [0, 255]


def decode_mip(data, mip, width, height, block):
    """Decode one DXT1/DXT5 mip into RGBA bytes."""
    w, h = max(1, width >> mip), max(1, height >> mip)
    start = sum(mip_block_size(block, width, height, i) for i in range(mip))
    src = data[start:start + mip_block_size(block, width, height, mip)]
    out = bytearray(w * h * 4)
    bx_count, by_count = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    for by in range(by_count):
        for bx in range(bx_count):
            o = (by * bx_count + bx) * block
            if block == 8:
                c0, c1 = struct.unpack_from("<HH", src, o)
                bits = src[o + 4:o + 8]
                alpha, abits = None, 0
            else:
                alpha = _alpha_palette(src[o], src[o + 1])
                abits = int.from_bytes(src[o + 2:o + 8], "little")
                c0, c1 = struct.unpack_from("<HH", src, o + 8)
                bits = struct.unpack_from("<I", src, o + 12)[0]
            pal = _palette4(c0, c1)
            for y in range(min(4, h - by * 4)):
                for x in range(min(4, w - bx * 4)):
                    if block == 8:
                        index = (bits[y] >> (2 * x)) & 3
                        colour = pal[index]
                        a = 0 if (c0 <= c1 and index == 3) else 255
                    else:
                        index = (bits >> (2 * (y * 4 + x))) & 3
                        colour = pal[index]
                        a = alpha[(abits >> (3 * (y * 4 + x))) & 7]
                    p = ((by * 4 + y) * w + bx * 4 + x) * 4
                    out[p:p + 4] = bytes((colour[0], colour[1], colour[2], a))
    return bytes(out)


# ---------------------------------------------------------------------------
# Block encoding
# ---------------------------------------------------------------------------
def _pack565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _clamp(v):
    if v <= 0.0:
        return 0
    if v >= 255.0:
        return 255
    return int(v + 0.5)


def _principal_axis(pixels):
    """Mean colour and dominant direction of a block, by power iteration."""
    n = len(pixels)
    mean = [sum(p[i] for p in pixels) / n for i in range(3)]
    cov = [[0.0] * 3 for _ in range(3)]
    for r, g, b, _a in pixels:
        d = (r - mean[0], g - mean[1], b - mean[2])
        for i in range(3):
            for j in range(3):
                cov[i][j] += d[i] * d[j]
    v = [1.0, 1.0, 1.0]
    for _ in range(8):
        w = [cov[i][0] * v[0] + cov[i][1] * v[1] + cov[i][2] * v[2] for i in range(3)]
        norm = (w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) ** 0.5
        if norm < 1e-9:
            return None
        v = [x / norm for x in w]
    return mean, v


def _endpoint_pairs(pixels):
    """Candidate (c0, c1) endpoint pairs for a block: the colour range, insets of
    it around the block mean, and a fit along the block's dominant gradient."""
    lo, hi = [255.0, 255.0, 255.0], [0.0, 0.0, 0.0]
    for r, g, b, _a in pixels:
        for i, c in enumerate((r, g, b)):
            lo[i], hi[i] = min(lo[i], c), max(hi[i], c)
    mean = [sum(p[i] for p in pixels) / len(pixels) for i in range(3)]
    pairs = []
    for scale in (1.0, 0.94, 0.85, 1.06):
        pairs.append((_pack565([_clamp(mean[i] + (hi[i] - mean[i]) * scale) for i in range(3)]),
                      _pack565([_clamp(mean[i] + (lo[i] - mean[i]) * scale) for i in range(3)])))
    axis = _principal_axis(pixels)
    if axis is not None:
        base, direction = axis
        projections = [sum((p[i] - base[i]) * direction[i] for i in range(3)) for p in pixels]
        pmax, pmin = max(projections), min(projections)
        for scale in (1.0, 0.9, 1.06):
            pairs.append(
                (_pack565([_clamp(base[i] + direction[i] * pmax * scale) for i in range(3)]),
                 _pack565([_clamp(base[i] + direction[i] * pmin * scale) for i in range(3)])))
    return pairs


def _block_error(pixels, pal, transparent_alpha=None):
    error = 0
    for r, g, b, a in pixels:
        if transparent_alpha is not None and a < transparent_alpha:
            continue
        error += min((r - p[0]) ** 2 + (g - p[1]) ** 2 + (b - p[2]) ** 2 for p in pal)
    return error


def _choose_colours(pixels, transparent_alpha=None):
    """Best (c0, c1, palette, transparent-index) for one block.

    A 4-colour block needs c0 > c1; ``transparent_alpha`` selects the 3-colour
    variant instead, whose fourth entry is the transparent index.
    """
    best = None
    for c0, c1 in _endpoint_pairs(pixels):
        if c0 == c1:
            continue
        if transparent_alpha is not None:
            if c0 > c1:
                c0, c1 = c1, c0
            pal = _palette4(c0, c1)
            error = _block_error(pixels, pal, transparent_alpha)
            if best is None or error < best[0]:
                best = (error, c0, c1, pal)
        else:
            if c0 < c1:
                c0, c1 = c1, c0
            pal = _palette4(c0, c1)
            error = _block_error(pixels, pal)
            if best is None or error < best[0]:
                best = (error, c0, c1, pal)
    if best is None:
        c = _pack565(pixels[0][:3])
        return c, c, [_c565(c)] * 4
    return best[1], best[2], best[3]


def _indices(pixels, pal, count, transparent_alpha=None):
    """Pack 2-bit palette indices for a 4x4 block, row-major."""
    rows = bytearray(4)
    for y in range(4):
        row = 0
        for x in range(4):
            r, g, b, a = pixels[y * 4 + x]
            if transparent_alpha is not None and a < transparent_alpha:
                index = count
            else:
                index = min(range(count),
                            key=lambda i: (r - pal[i][0]) ** 2 + (g - pal[i][1]) ** 2
                            + (b - pal[i][2]) ** 2)
            row |= index << (2 * x)
        rows[y] = row
    return bytes(rows)


def encode_block_dxt1(pixels):
    transparent = any(p[3] < 128 for p in pixels)
    alpha = 128 if transparent else None
    c0, c1, pal = _choose_colours(pixels, alpha)
    if transparent:
        indices = _indices(pixels, pal, 3, transparent_alpha=128)
    else:
        indices = _indices(pixels, pal, 4)
    return struct.pack("<HH", c0, c1) + indices


def encode_block_dxt5(pixels):
    alphas = [p[3] for p in pixels]
    a0, a1 = max(alphas), min(alphas)
    pal = [a0] * 8 if a0 == a1 else _alpha_palette(a0, a1)
    abits = 0
    for i, a in enumerate(alphas):
        abits |= min(range(8), key=lambda j: (a - pal[j]) ** 2) << (3 * i)

    # DXT5's colour block has no transparent mode, so keep four opaque entries.
    c0, c1, pal4 = _choose_colours(pixels)
    if c0 == c1:
        return (struct.pack("<BB", a0, a1) + abits.to_bytes(6, "little")
                + struct.pack("<HH", c0, c0) + bytes(4))
    colour = struct.pack("<HH", c0, c1) + _indices(pixels, pal4, 4)
    return struct.pack("<BB", a0, a1) + abits.to_bytes(6, "little") + colour


def resample(pixels, src_w, src_h, dst_w, dst_h):
    """Box-filter an RGBA image down (used to build the mip chain)."""
    if (src_w, src_h) == (dst_w, dst_h):
        return pixels
    out = bytearray(dst_w * dst_h * 4)
    for y in range(dst_h):
        y0, y1 = y * src_h // dst_h, max(y * src_h // dst_h + 1, (y + 1) * src_h // dst_h)
        for x in range(dst_w):
            x0, x1 = x * src_w // dst_w, max(x * src_w // dst_w + 1, (x + 1) * src_w // dst_w)
            acc = [0, 0, 0, 0]
            for sy in range(y0, y1):
                for sx in range(x0, x1):
                    p = (sy * src_w + sx) * 4
                    for c in range(4):
                        acc[c] += pixels[p + c]
            count = (y1 - y0) * (x1 - x0)
            q = (y * dst_w + x) * 4
            out[q:q + 4] = bytes(v // count for v in acc)
    return bytes(out)


def encode_chain(pixels, width, height, levels, block):
    """Encode RGBA pixels plus their mip chain into stored (swapped) bytes."""
    out = bytearray()
    for level in range(levels):
        w, h = max(1, width >> level), max(1, height >> level)
        level_pixels = pixels if level == 0 else resample(pixels, width, height, w, h)
        encode = encode_block_dxt5 if block == 16 else encode_block_dxt1
        for by in range(max(1, (h + 3) // 4)):
            for bx in range(max(1, (w + 3) // 4)):
                block_pixels = []
                for y in range(4):
                    sy = min(by * 4 + y, h - 1)
                    for x in range(4):
                        sx = min(bx * 4 + x, w - 1)
                        p = (sy * w + sx) * 4
                        block_pixels.append(tuple(level_pixels[p:p + 4]))
                out += encode(block_pixels)
    return unswap(bytes(out))


# ---------------------------------------------------------------------------
# PNG and DDS interchange
# ---------------------------------------------------------------------------
def _require_pillow():
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - depends on the environment
        raise TextureError("PNG conversion needs Pillow (pip install pillow)") from exc
    return Image


def export_png(texture, out_path, mip=0):
    Image = _require_pillow()
    if mip >= texture.mip_levels:
        raise TextureError(f"mip {mip} does not exist, the texture has {texture.mip_levels}")
    pixels = decode_mip(unswap(texture.body), mip, texture.width, texture.height, texture.block)
    w, h = max(1, texture.width >> mip), max(1, texture.height >> mip)
    Image.frombytes("RGBA", (w, h), pixels).save(out_path)
    return w, h


def dds_header(fourcc, width, height, levels, linear_size):
    caps = 0x00001000 | 0x00000008 | 0x00400000  # texture | complex | mipmap
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # caps|h|w|pf|mips|linearsize
    pixel_format = struct.pack("<8I", 32, 0x4,
                               int.from_bytes(fourcc.encode("ascii"), "little"), 0, 0, 0, 0, 0)
    return (b"DDS " + struct.pack("<7I", 124, flags, height, width, linear_size, 0, levels)
            + bytes(44) + pixel_format + struct.pack("<5I", caps, 0, 0, 0, 0))


def export_dds(texture, out_path):
    top = mip_block_size(texture.block, texture.width, texture.height, 0)
    header = dds_header(texture.format, texture.width, texture.height,
                        texture.mip_levels, top)
    with open(out_path, "wb") as handle:
        handle.write(header + unswap(texture.body))
    return len(header) + len(texture.body)


def import_dds(path, template):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"DDS " or len(data) < 128:
        raise TextureError("not a DDS file")
    height, width = struct.unpack_from("<II", data, 12)
    levels = struct.unpack_from("<I", data, 28)[0] or 1
    fourcc = data[84:88].decode("latin-1").strip("\x00")
    if (width, height) != (template.width, template.height):
        raise TextureError(f"DDS is {width}x{height}, the texture is "
                           f"{template.width}x{template.height}; a swap keeps the dimensions")
    if fourcc != template.format:
        raise TextureError(f"DDS is {fourcc}, the texture is {template.format}")
    body = unswap(data[128:])
    if len(body) != len(template.body):
        raise TextureError(f"DDS carries {len(body)} bytes of mip data, the texture wants "
                           f"{len(template.body)} ({template.mip_levels} mips, {levels} given)")
    return body, ""


def import_png(path, template):
    Image = _require_pillow()
    image = Image.open(path).convert("RGBA")
    note = ""
    if image.size != (template.width, template.height):
        note = f" (resized from {image.size[0]}x{image.size[1]})"
        image = image.resize((template.width, template.height), Image.LANCZOS)
    body = encode_chain(image.tobytes(), template.width, template.height,
                        template.mip_levels, template.block)
    return body, note


def build_container(template, image_path):
    if image_path.lower().endswith(".dds"):
        body, note = import_dds(image_path, template)
    else:
        body, note = import_png(image_path, template)
    if len(body) != len(template.body):
        raise TextureError(f"the rebuilt texture is {len(body)} bytes, the archive entry is "
                           f"{len(template.body)}; a swap keeps format, size and mip count")
    return template.header + body, note


def entry_path_for(template_path, root):
    """In-archive path of an extracted texture, given the extraction root."""
    relative = os.path.relpath(os.path.abspath(template_path), os.path.abspath(root))
    return relative.replace(os.sep, "/")


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_info(args):
    status = 0
    for path in args.texture:
        try:
            print(f"{path}: {load(path).describe()}")
        except (OSError, TextureError) as exc:
            print(f"{path}: {exc}")
            status = 1
    return status


def cmd_export(args):
    texture = load(args.texture)
    if args.out.lower().endswith(".dds"):
        size = export_dds(texture, args.out)
        print(f"{args.texture} -> {args.out} ({texture.format}, all mips, {size} bytes)")
    else:
        w, h = export_png(texture, args.out, args.mip)
        print(f"{args.texture} -> {args.out} (mip {args.mip}, {w}x{h} RGBA)")
    return 0


def cmd_import(args):
    template = load(args.template)
    container, note = build_container(template, args.image)
    with open(args.out, "wb") as handle:
        handle.write(container)
    print(f"{args.image} -> {args.out} ({template.describe()}){note}")
    return 0


def cmd_swap(args):
    import hmx_ark

    if bool(args.out) == bool(args.patch_dir):
        raise TextureError("pass exactly one of --out (full archive copy) or --patch-dir "
                           "(small overlay archive)")

    template = load(args.template)
    container, note = build_container(template, args.image)
    entry_path = args.entry or entry_path_for(args.template, args.root)

    header = hmx_ark.load_header(args.hdr)
    entry = next((e for e in header.files if e.path == entry_path), None)
    if entry is None:
        raise TextureError(f"no entry {entry_path!r} in {args.hdr}; pass --entry with the "
                           "path the archive uses")
    if entry.size != len(container):
        raise TextureError(f"entry {entry.path} is {entry.size} bytes, the rebuilt texture "
                           f"is {len(container)}")

    if args.patch_dir:
        # A tiny overlay archive: the title loads gen/patch_xbox.hdr/ark over the base
        # archive at boot, which is how title updates and the Ultimate payload work.
        root = args.patch_dir
        part = "gen/patch_xbox_0.ark"
        size = hmx_ark.write_ark(
            os.path.join(root, "gen", "patch_xbox.hdr"),
            os.path.join(root, *part.split("/")),
            {entry.path: container}, header, part_name=part,
            extra=struct.pack("<I", 1) + b"\xff" * 16)
        print(f"{args.image} -> {root}{os.sep}gen{os.sep}patch_xbox{{.hdr,_0.ark}}{note}")
        print(f"  {entry.path} carries {entry.size} bytes in a {size}-byte overlay archive")
        print(f"  point the game at it with --ultimate_payload_root={root}")
        return 0

    source = header.ark_parts(args.hdr)[0]
    size = hmx_ark.write_patched_ark(header, args.hdr, source, args.out,
                                     {entry.path: container}, overwrite=args.overwrite)
    print(f"{args.image} -> {args.out}{note}")
    print(f"  {entry.path} replaced at offset {entry.offset}, {entry.size} bytes")
    print(f"  {size} bytes written, the other {len(header.files) - 1} entries untouched")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description="Read, convert and swap Harmonix console textures.")
    sub = parser.add_subparsers(dest="command", required=True)

    info = sub.add_parser("info", help="describe a texture envelope")
    info.add_argument("texture", nargs="+")
    info.set_defaults(func=cmd_info)

    export = sub.add_parser("export", help="write a texture as PNG or DDS")
    export.add_argument("texture")
    export.add_argument("--out", required=True, help=".png (largest mip) or .dds (all mips)")
    export.add_argument("--mip", type=int, default=0)
    export.set_defaults(func=cmd_export)

    imp = sub.add_parser("import", help="build a texture file from an image")
    imp.add_argument("image")
    imp.add_argument("--template", required=True, help="texture whose envelope to reuse")
    imp.add_argument("--out", required=True)
    imp.set_defaults(func=cmd_import)

    swap = sub.add_parser("swap", help="build an image into the game's archive")
    swap.add_argument("image")
    swap.add_argument("--template", required=True, help="the extracted texture being replaced")
    swap.add_argument("--hdr", required=True)
    swap.add_argument("--out", help="patched copy of the whole .ark (full-size route)")
    swap.add_argument("--patch-dir", help="write a small overlay archive here instead "
                                          "(<dir>/gen/patch_xbox.hdr and _0.ark)")
    swap.add_argument("--entry", help="in-archive path (default: --template under --root)")
    swap.add_argument("--root", default="extracted", help="extraction root (default: extracted)")
    swap.add_argument("--overwrite", action="store_true")
    swap.set_defaults(func=cmd_swap)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (OSError, TextureError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
