"""Parse the XEX2 header of game/default.xex (plaintext header, no image
decryption needed) and dump header-level metadata: entry point, image base,
file format, resources (sections), and import libraries.

All XEX2 header fields are big-endian.
"""
import struct
import sys


def be32(b, off):
    return struct.unpack_from(">I", b, off)[0]


def be16(b, off):
    return struct.unpack_from(">H", b, off)[0]


def main(path):
    with open(path, "rb") as f:
        data = f.read()

    magic = data[0:4]
    print(f"magic          : {magic!r}")
    if magic not in (b"XEX2", b"XEX1"):
        print("ERROR: not a XEX2/XEX1 image")
        return 1

    module_flags = be32(data, 0x04)
    header_size = be32(data, 0x08)
    security_offset = be32(data, 0x10)
    header_count = be32(data, 0x14)
    print(f"module_flags   : 0x{module_flags:08X}")
    print(f"header_size    : 0x{header_size:X}")
    print(f"security_off   : 0x{security_offset:X}")
    print(f"header_count   : {header_count}")

    entry_point = None
    image_base = None
    opt = {}  # key -> (value, offset_resolved)

    for i in range(header_count):
        off = 0x18 + i * 8
        key = be32(data, off)
        val = be32(data, off + 4)
        opt[key] = val

    if 0x00010100 in opt:
        entry_point = opt[0x00010100]
    if 0x00010201 in opt:
        image_base = opt[0x00010201]
    print(f"entry_point    : 0x{entry_point:08X}" if entry_point is not None else "entry_point    : (none)")
    print(f"image_base     : 0x{image_base:08X}" if image_base is not None else "image_base     : (none)")

    # File format info (0x000003FF) -> offset from header start
    if 0x000003FF in opt:
        off = opt[0x000003FF]
        ff = off  # offset is relative to header start == file start for XEX2
        info_size = be32(data, ff)
        enc = be16(data, ff + 4)
        comp = be16(data, ff + 6)
        print(f"file_format    : info_size={info_size} encryption={enc} compression={comp}")

    # Resources (0x000002FF) -> named sections with guest ranges
    if 0x000002FF in opt:
        off = opt[0x000002FF]
        size = be32(data, off)
        count = (size - 4) // 16
        print(f"resources      : {count} entries (size={size})")
        for i in range(count):
            e = off + 4 + i * 16
            name = data[e:e+8].rstrip(b"\0").decode("ascii", "replace")
            addr = be32(data, e + 8)
            rsize = be32(data, e + 12)
            print(f"  {name:<12} addr=0x{addr:08X} size=0x{rsize:X}")

    # Import libraries (0x000103FF)
    if 0x000103FF in opt:
        off = opt[0x000103FF]
        total = be32(data, off)
        str_size = be32(data, off + 4)
        str_count = be32(data, off + 8)
        str_data = data[off + 12 : off + 12 + str_size]

        # Split the string table (count NUL-terminated strings)
        strings = []
        cur = b""
        for ch in str_data:
            if ch == 0:
                if cur:
                    strings.append(cur.decode("ascii", "replace"))
                cur = b""
            else:
                cur += bytes([ch])
        if cur:
            strings.append(cur.decode("ascii", "replace"))

        print(f"imports        : total={total} string_table(size={str_size}, count={str_count})")
        print(f"  libraries ({len(strings)}): {', '.join(strings)}")

        # Parse import_library entries following the string table
        cursor = off + 12 + str_size
        end = off + total
        lib_idx = 0
        while cursor < end and lib_idx < 200:
            lib_size = be32(data, cursor)
            if lib_size == 0 or cursor + lib_size > end:
                break
            name_index = be16(data, cursor + 0x24)
            count = be16(data, cursor + 0x26)
            name = strings[name_index & 0xFF] if (name_index & 0xFF) < len(strings) else "?"
            print(f"  lib[{lib_idx}] {name}: {count} imports (size={lib_size})")
            cursor += lib_size
            lib_idx += 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "game/default.xex"))
