"""Bounded vertex declaration linking recovered from sub_82521908.

All shader instructions come from the local asset. Recipes supply declarations
and stream strides, never replacement instruction words.
"""
import struct


def source_layouts(image, manifest):
    if not isinstance(manifest, dict) or manifest.get("schema") != 1 or manifest.get("title_id") != "5841122D" or manifest.get("linker") != "d3d9-sub_82521908-v1":
        raise ValueError("unsupported vertex linkage recipe version")

    def slice_at(offset, length):
        if type(offset) is not int or type(length) is not int or offset < 0 or length < 0 or offset > len(image) or length > len(image) - offset:
            raise ValueError("vertex source outside executable image")
        return image[offset:offset + length]

    masks = struct.unpack(">8H", slice_at(manifest.get("swizzle_masks_offset"), 16))
    sizes = slice_at(manifest.get("format_sizes_offset"), 64)
    if any(s > 4 for s in sizes):
        raise ValueError("invalid vertex format-size table")
    rows = manifest.get("layouts")
    if not isinstance(rows, list) or not 0 < len(rows) <= 16:
        raise ValueError("invalid vertex layout table")
    layouts, names = [], set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("invalid vertex layout row")
        name, strides = row.get("name"), row.get("strides")
        if not isinstance(name, str) or not 0 < len(name) <= 64 or name in names:
            raise ValueError("invalid/duplicate vertex layout name")
        names.add(name)
        if not isinstance(strides, list) or not 0 < len(strides) <= 16 or any(type(s) is not int or not 0 <= s <= 255 for s in strides):
            raise ValueError("invalid layout stream strides")
        strides = strides + [0] * (16 - len(strides))
        if "fvf" in row:
            if "declaration_offset" in row:
                raise ValueError("ambiguous vertex declaration recipe")
            elements = fvf_declaration(row["fvf"])
        else:
            offset = row.get("declaration_offset")
            slice_at(offset, 12)
            elements = []
            for count in range(33):
                e = struct.unpack(">HHIBBBB", slice_at(offset + count * 12, 12))
                if e[0] == 255:
                    if e[2] != 0xffffffff or not elements:
                        raise ValueError("invalid declaration terminator")
                    break
                if count == 32 or e[0] >= 16 or e[1] >= 32768 or e[3] or e[4] >= 16 or e[5] >= 16:
                    raise ValueError("invalid vertex declaration element")
                elements.append(e)
        if len({e[4:6] for e in elements}) != len(elements):
            raise ValueError("ambiguous vertex declaration usage/index")
        if any(not sizes[e[2] & 63] for e in elements):
            raise ValueError("unsupported vertex declaration format")
        layouts.append((name, elements, strides))
    bootstraps = manifest.get("bootstrap_shaders")
    if not isinstance(bootstraps, list) or len(bootstraps) > 16:
        raise ValueError("invalid bootstrap shader list")
    programs = []
    for row in bootstraps:
        if not isinstance(row, dict):
            raise ValueError("invalid bootstrap shader row")
        length = row.get("bytes")
        if type(length) is not int or not 0 < length <= 65536 or length % 12:
            raise ValueError("invalid bootstrap microcode length")
        programs.append((row, slice_at(row.get("offset"), length)))
    return layouts, masks, sizes, programs


def fvf_declaration(fvf):
    # sub_823EAB58 selects FVF 450 (XYZ, diffuse, specular, one float2 UV)
    # before its 28-byte full-screen draw. sub_82512730 constructs this layout.
    if fvf != 450:
        raise ValueError("unsupported vertex FVF")
    return [(0, 0, 0x2a23b9, 0, 0, 0, 0),
            (0, 12, 0x182886, 0, 10, 0, 0),
            (0, 16, 0x182886, 0, 10, 1, 0),
            (0, 20, 0x2c23a5, 0, 5, 0, 0)]


def mask(start, end):
    value = (0xffffffff >> start) & (0xffffffff << (31 - end))
    return value if start <= end else ((0xffffffff >> start) | (0xffffffff << (31 - end))) & 0xffffffff


def rol(value, count):
    return ((value << count) | (value >> ((32 - count) & 31))) & 0xffffffff


def rm(value, count, start, end):
    return rol(value, count) & mask(start, end)


def ri(dest, value, count, start, end):
    m = mask(start, end)
    return (dest & ~m) | (rol(value, count) & m)


def signed(value):
    value &= 0xffffffff
    return value - 0x100000000 if value & 0x80000000 else value


def link_fetch(words, element, strides, swizzle_masks):
    a, b, c = words
    swizzle = 0
    if element is None:
        a = ri(a, 95, 20, 2, 11)
        b = (b & 0xbfc6cfff) | 0x60000
        c = (c & 0x80000000) | strides[0]
        swizzle = 37456
    else:
        stream, offset, fmt, _, _, _, _ = element
        swizzle = rm(fmt, 16, 29, 31) | rm(fmt, 22, 26, 28)
        swizzle = rm(swizzle, 3, 0, 28) | rm(fmt, 13, 29, 31)
        swizzle = rm(swizzle, 1, 0, 30) | rm(fmt, 0, 16, 21)
        swizzle = rm(swizzle, 3, 0, 28) & 65535
        if rm(fmt, 0, 24, 25) == 64:
            for bits, shift, toggle in ((7 << 13, 13, 8192), (7 << 10, 10, 1024),
                                        (7 << 7, 7, 128), (7 << 4, 4, 16)):
                if (swizzle & bits) <= (3 << shift):
                    swizzle ^= toggle
        encoded = ((95 - stream) * 0x1560056) & 0xffffffff
        encoded = ri(encoded, encoded, 11, 8, 12)
        a = ri(a, encoded, 1, 5, 11)
        a = rm(a, 0, 5, 1)
        flags = rm(fmt, 12, 14, 19) | rm(fmt, 0, 22, 23)
        b = rm(flags, 4, 0, 27) | (b & 0xbfc0cfff)
        c = (c & 0x80000000) | rm(offset, 6, 1, 23) | strides[stream]
    # The original destination selectors index the declaration's component map.
    # Keep the SDK's exact selector reduction, including constant/keep selectors.
    swizzle |= 14
    v = swizzle_masks[rm(b, 1, 28, 30) // 2] & swizzle
    low = v & 65535
    x = rm(v, 29, 19, 31) | low
    for _ in range(3):
        x = rm(x, 29, 3, 31) | low
    b = ri(b, x, 31, 29, 31)
    for component, (selection_rotation, pack_shift, begin, end) in enumerate(
            ((30, 2, 29, 25), (27, 3, 26, 22), (24, 3, 23, 19))):
        v = swizzle_masks[rm(b, selection_rotation, 28, 30) // 2] & swizzle
        low = v & 65535
        y = rm(v, 29, 19, 31) | low
        if component == 0:
            x = rm(v, 2, 14, 29)
            for _ in range(2):
                y = rm(y, 29, 3, 31) | low
            x |= rm(y, 31, 1, 31)
        elif component == 1:
            x = rm(v, 3, 13, 28) | low
            y = rm(y, 29, 3, 31) | low
            x = rm(x, 2, 0, 29) | rm(y, 31, 1, 31)
        else:
            x = rm(v, 3, 13, 28) | low
            y = rm(v, 29, 19, 31) | low
            x = rm(rm(x, 3, 0, 28) | low, 2, 0, 29) | rm(y, 31, 1, 31)
        b = ri(x, b, 0, begin, end)
    return [a, b, c]


def link(program, elements, strides, swizzle_masks, format_sizes):
    if len(elements) > 32 or len(strides) != 16 or any(not 0 <= s <= 255 for s in strides):
        raise ValueError("invalid vertex declaration/strides")
    if len(swizzle_masks) != 8 or len(format_sizes) != 64 or len(program.fetches) > 32:
        raise ValueError("invalid vertex linkage tables")
    if any(len(e) != 7 or any(type(v) is not int for v in e) or not 0 <= e[0] < 16 or
           not 0 <= e[1] < 32768 or not 0 <= e[2] <= 0xffffffff or e[3] or
           not 0 <= e[4] < 16 or not 0 <= e[5] < 16 for e in elements):
        raise ValueError("invalid vertex declaration element")
    code = bytearray(program.code)
    fetches = []
    positions = set()
    for entry in program.fetches:
        usage, index = (entry >> 12) & 15, (entry >> 16) & 15
        element = next((e for e in elements if e[4:6] == (usage, index)), None)
        if element and (element[0] >= 16 or element[1] >= 32768):
            raise ValueError("vertex declaration element out of range")
        position = (entry & 4095) * 12
        if position + 12 > len(code):
            raise ValueError("vertex fetch outside code")
        if position in positions:
            raise ValueError("duplicate vertex fetch slot")
        positions.add(position)
        words = struct.unpack_from(">III", code, position)
        if words[0] & 31:
            raise ValueError("vertex metadata references a non-vertex-fetch instruction")
        fetches.append(link_fetch(words, element, strides, swizzle_masks))

    def key(words):
        return rm(words[0], 14, 25, 29) | rm(words[0], 7, 30, 31)

    def offset(words):
        return signed(rm(words[2], 1, 0, 30)) >> 9

    def width(words):
        return format_sizes[(words[1] >> 16) & 63]

    cursor = 0
    while cursor < len(fetches):
        while cursor < len(fetches) and not program.fetches[cursor] & 0x300000:
            cursor += 1
        end = cursor + 1
        while end < len(fetches) and not program.fetches[end] & 0x300000:
            end += 1
        if end >= len(fetches):
            break
        start = cursor
        cursor = end + 1
        special = (program.fetches[end] & 0x300000) == 0x300000
        anchor = key(fetches[end]) if special else -1
        sort_end = end if special else end + 1
        fetches[start:sort_end] = sorted(fetches[start:sort_end],
            key=lambda w: (key(w) == anchor, -key(w), offset(w), width(w)))
        previous = start
        for current in range(start + 1, end + 1):
            a, b = fetches[previous], fetches[current]
            ak = rm(ri(a[0], a[0], 25, 12, 13), 0, 7, 13)
            bk = rm(ri(b[0], b[0], 25, 12, 13), 0, 7, 13)
            span = offset(b) - offset(a) + width(b)
            if ak == bk and offset(a) <= offset(b) and span <= 8:
                count = max(rm(a[0], 5, 29, 31), span - 1)
                a[0] = ri(a[0], count, 27, 2, 4)
                b[1] |= 0x40000000
            else:
                previous = current
    for entry, words in zip(program.fetches, fetches):
        struct.pack_into(">III", code, (entry & 4095) * 12, *words)
    return bytes(code)
