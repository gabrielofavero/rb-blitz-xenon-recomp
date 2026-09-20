"""Decrypt/de-block a XEX2 image into a raw guest image, or diff two of them.

Mirrors rexglue-sdk XexModule::ReadImage for XEX_ENCRYPTION_NORMAL and
XEX_COMPRESSION_BASIC (the retail 360 layout). Handles uncompressed (devkit or
rebuilt) images too.

usage:
  python decrypt_xex.py <in.xex> <out.bin> [--key HEX] [--sdk PATH]
  python decrypt_xex.py --diff <left.bin> <right.bin> [--image-base 0x82000000]

The retail key is never stored here: it is read from the pinned SDK source, the
same file the runtime uses (docs/rb3-references.md §9 asks for one copy of key
material, not two), or passed in with --key.

The decrypted images are what make a content variant reviewable: decrypt the
dumped default.xex and the variant's default.xex, diff them, and every code and
data edit is a byte range with a guest address. That is how the Rock Band Blitz
Ultimate delta was established (docs/ultimate-compat.md): 12 bytes in 3 places,
which is why this project reproduces the edits in src/hooks/ultimate.cpp instead
of treating the variant's executable as a second compilation input.
"""
import re
import struct
import sys
from pathlib import Path

from Crypto.Cipher import AES

SEC_AES_KEY = 0x150
SEC_IMAGE_SIZE = 0x04
SEC_PAGE_DESC_COUNT = 0x180
SEC_PAGE_DESCRIPTORS = 0x184

FF_OFFSET_KEY = 0x000003FF

DEFAULT_SDK = Path(__file__).resolve().parent.parent / "rexglue-sdk"
SDK_KEY_FILE = "src/system/xex_module.cpp"
SDK_KEY_NAME = "xe_xex2_retail_key"


def be32(b, off):
    return struct.unpack_from(">I", b, off)[0]


def be16(b, off):
    return struct.unpack_from(">H", b, off)[0]


def read_retail_key(sdk_dir):
    """Reads the retail key out of the SDK source instead of copying it here."""
    source = Path(sdk_dir) / SDK_KEY_FILE
    try:
        text = source.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise SystemExit(
            f"cannot read the retail key from {source} ({error}); pass --key HEX instead"
        ) from error

    at = text.find(SDK_KEY_NAME)
    if at < 0:
        raise SystemExit(f"no {SDK_KEY_NAME} in {source}; pass --key HEX instead")
    values = re.findall(r"0x([0-9A-Fa-f]{2})\b", text[at:])
    if len(values) < 16:
        raise SystemExit(f"{SDK_KEY_NAME} has fewer than 16 bytes in {source}; pass --key HEX")
    return bytes(int(v, 16) for v in values[:16])


def decrypt_image(xex_path, out_path, retail_key):
    with open(xex_path, "rb") as f:
        data = f.read()

    if data[0:4] not in (b"XEX2", b"XEX1"):
        print("ERROR: not a XEX image")
        return 1

    header_size = be32(data, 0x08)
    security_offset = be32(data, 0x10)
    header_count = be32(data, 0x14)

    opt = {}
    for i in range(header_count):
        off = 0x18 + i * 8
        opt[be32(data, off)] = be32(data, off + 4)

    ff = opt[FF_OFFSET_KEY]
    info_size = be32(data, ff)
    enc = be16(data, ff + 4)
    comp = be16(data, ff + 6)

    image_size = be32(data, security_offset + SEC_IMAGE_SIZE)
    page_desc_count = be32(data, security_offset + SEC_PAGE_DESC_COUNT)
    total_size = 0
    for i in range(page_desc_count):
        desc = be32(data, security_offset + SEC_PAGE_DESCRIPTORS + i * 0x18)
        pages = desc >> 4
        total_size += pages * 0x1000
    aes_key = data[security_offset + SEC_AES_KEY : security_offset + SEC_AES_KEY + 16]

    print(f"file           : {xex_path}")
    print(f"header_size    : 0x{header_size:X}")
    print(f"security_off   : 0x{security_offset:X}")
    print(f"file_format    : info_size={info_size} encryption={enc} compression={comp}")
    print(f"image_size     : 0x{image_size:X} ({image_size} bytes)")
    print(f"page_desc      : {page_desc_count} -> mapped size 0x{total_size:X}")

    session_key = AES.new(retail_key, AES.MODE_ECB).decrypt(aes_key) if enc else aes_key
    print(f"aes_key        : {aes_key.hex()}")
    print(f"session_key    : {session_key.hex()}")

    src = header_size
    if comp == 0:
        # Uncompressed: single contiguous image.
        exe = data[src : src + image_size]
        if enc:
            exe = AES.new(session_key, AES.MODE_CBC, b"\0" * 16).decrypt(
                exe + b"\0" * ((-len(exe)) % 16)
            )
        image = bytearray(exe)
    elif comp == 1:
        block_count = (info_size - 8) // 8
        out = bytearray(image_size)
        d = 0
        # ivec persists across blocks: the ciphertext stream is continuous even
        # though zero_size gaps are inserted into the plaintext image.
        ivec = b"\0" * 16
        cipher = AES.new(session_key, AES.MODE_ECB)
        for n in range(block_count):
            e = ff + 8 + n * 8
            data_size = be32(data, e)
            zero_size = be32(data, e + 4)
            chunk = data[src : src + data_size]
            if enc:
                blocks = []
                for i in range(0, data_size, 16):
                    ct = chunk[i : i + 16]
                    pt = bytes(a ^ b for a, b in zip(cipher.decrypt(ct), ivec))
                    ivec = ct
                    blocks.append(pt)
                chunk = b"".join(blocks)
            out[d : d + data_size] = chunk
            src += data_size
            d += data_size + zero_size
        image = out
    else:
        print(f"ERROR: unsupported compression_type {comp} (delta/LZX not implemented)")
        return 2

    out_path = Path(out_path)
    with open(out_path, "wb") as f:
        f.write(image)
    print(f"wrote          : {out_path} ({len(image)} bytes)")
    if image[0:2] == b"MZ":
        print("image starts with : MZ (PE header present)")
    else:
        print(f"image starts with : {image[0:16].hex()}")
    return 0


def printable(data):
    text = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in data)
    return f"  '{text}'" if any(0x20 <= b < 0x7F for b in data) else ""


def diff_images(left_path, right_path, image_base):
    left = Path(left_path).read_bytes()
    right = Path(right_path).read_bytes()
    if len(left) != len(right):
        print(f"ERROR: sizes differ ({len(left)} vs {len(right)}); align them first")
        return 2

    print(f"left           : {left_path} ({len(left)} bytes)")
    print(f"right          : {right_path} ({len(right)} bytes)")
    print(f"image base     : 0x{image_base:08X}")

    runs = []
    index = 0
    while index < len(left):
        if left[index] == right[index]:
            index += 1
            continue
        start = index
        while index < len(left) and left[index] != right[index]:
            index += 1
        runs.append((start, index))

    total = sum(end - start for start, end in runs)
    for start, end in runs:
        span = end - start
        print(
            f"0x{image_base + start:08X}  {span} byte(s)  "
            f"left={left[start:end].hex()}{printable(left[start:end])}  "
            f"right={right[start:end].hex()}{printable(right[start:end])}"
        )
    print(f"{total} differing byte(s) in {len(runs)} run(s)")
    return 0


def main(argv):
    if "--diff" in argv:
        argv = [a for a in argv if a != "--diff"]
        image_base = 0x82000000
        if "--image-base" in argv:
            at = argv.index("--image-base")
            image_base = int(argv[at + 1], 0)
            del argv[at : at + 2]
        if len(argv) != 2:
            print(__doc__)
            return 64
        return diff_images(argv[0], argv[1], image_base)

    key = None
    sdk_dir = DEFAULT_SDK
    positional = []
    index = 0
    while index < len(argv):
        if argv[index] == "--key":
            key = bytes.fromhex(argv[index + 1])
            index += 2
        elif argv[index] == "--sdk":
            sdk_dir = argv[index + 1]
            index += 2
        else:
            positional.append(argv[index])
            index += 1

    if len(positional) != 2 or (key is not None and len(key) != 16):
        print(__doc__)
        return 64
    return decrypt_image(positional[0], positional[1], key or read_retail_key(sdk_dir))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
