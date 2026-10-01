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

What it does **not** do yet: locate the pixels of those textures. A `RndTex` whose
stream is cached carries its bitmap inline, but the shape this tool looked for - the
RB3 `RndBitmap::LoadHeader` (revision byte, bits per pixel, byte order, mip count,
dimensions, row bytes, reserved bytes, then the mip chain) - matches nothing in the
10.4 MB ``splash.milo_xbox`` decompresses to, and neither does any descriptor that
just puts a power-of-two width and height next to matching row bytes. Blitz is a
later engine than the RB3 decompilation whose format this was modelled on, so its
inline texture record is still unknown; see docs/assets.md, "Texture swapping", and
the backlog entry for what a next attempt should start from. The scene's textures
also carry builder-side source paths (``img/list_neon_outer.png``), so the record may
be a file reference plus a cached copy rather than an RB3-shaped bitmap.

The `.tex` names and the corpus are here to work against:

    python scripts/hmx_milo.py info  extracted/ui/splash/gen/splash.milo_xbox
    python scripts/hmx_milo.py list  extracted/ui/splash/gen/splash.milo_xbox --assets
    python scripts/hmx_milo.py dump  extracted/ui/splash/gen/splash.milo_xbox --chunk 3

References: the chunk table and the deflate framing come from the RB3 decompilation
(`freeqaz/rb3-xenon`, ``utl/ChunkStream``); every claim here was re-verified against
the dump rather than assumed.
"""
import argparse
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

    def describe(self):
        lines = [
            f"{self.path}: {len(self.data)} bytes, {self.codec} chunk stream, "
            f"{self.chunk_count} chunk(s), largest decompressed {self.max_chunk_size} B",
        ]
        for chunk in self.chunks:
            lines.append(chunk.describe(self.codec))
        lines.append(f"  {self.trailing} bytes past the last chunk")
        return "\n".join(lines)


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

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (OSError, MiloError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
