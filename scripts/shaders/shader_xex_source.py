"""Bounded, quiet extraction of the supported installed XEX's local image.

No key or decoded bytes are logged or distributed. Address-based shader/declaration
recipes are allowed only after the installed XEX matches the project fingerprint.
"""
import hashlib
import struct
import tomllib
from pathlib import Path

MAX_IMAGE = 32 * 1024 * 1024


def decode_image(data, retail_key):
    from Crypto.Cipher import AES

    def u32(offset):
        if offset < 0 or offset + 4 > len(data):
            raise ValueError("truncated XEX field")
        return struct.unpack_from(">I", data, offset)[0]

    if not 24 <= len(data) <= MAX_IMAGE or data[:4] != b"XEX2":
        raise ValueError("invalid XEX size/magic")
    image_start, security, count = u32(8), u32(16), u32(20)
    if count > 128 or 24 + count * 8 > image_start or image_start > len(data):
        raise ValueError("invalid XEX optional-header table")
    if security < 24 or security + 0x184 > image_start:
        raise ValueError("invalid XEX security header")
    fields = {u32(24 + i * 8): u32(28 + i * 8) for i in range(count)}
    file_format = fields.get(0x3ff)
    if file_format is None or file_format < 24 or file_format + 8 > image_start:
        raise ValueError("missing XEX file-format header")
    info_size = u32(file_format)
    if info_size < 8 or info_size > image_start - file_format:
        raise ValueError("invalid XEX file-format extent")
    encryption, compression = struct.unpack_from(">HH", data, file_format + 4)
    if encryption not in (0, 1) or compression not in (0, 1):
        raise ValueError("unsupported XEX encryption/compression")
    size = u32(security + 4)
    if not 0 < size <= MAX_IMAGE:
        raise ValueError("invalid decoded XEX image length")
    if len(retail_key) != 16:
        raise ValueError("invalid XEX key length")
    session = AES.new(retail_key, AES.MODE_ECB).decrypt(data[security + 0x150:security + 0x160])
    cipher = AES.new(session, AES.MODE_CBC, bytes(16)) if encryption else None
    if compression == 0:
        if size > len(data) - image_start or (encryption and size % 16):
            raise ValueError("truncated/unaligned XEX image")
        raw = data[image_start:image_start + size]
        return cipher.decrypt(raw) if cipher else raw
    if (info_size - 8) % 8 or (info_size - 8) // 8 > 4096:
        raise ValueError("invalid XEX basic-compression table")
    result = bytearray(size)
    source, destination = image_start, 0
    for offset in range(file_format + 8, file_format + info_size, 8):
        stored, zeros = u32(offset), u32(offset + 4)
        if stored > len(data) - source or stored + zeros > size - destination:
            raise ValueError("XEX block outside source/image")
        if encryption and stored % 16:
            raise ValueError("unaligned encrypted XEX block")
        raw = data[source:source + stored]
        result[destination:destination + stored] = cipher.decrypt(raw) if cipher else raw
        source += stored
        destination += stored + zeros
    return bytes(result)


def load_supported_image(game_root, project_root):
    project_root = Path(project_root)
    with (project_root / "config/game_fingerprints.toml").open("rb") as stream:
        fingerprints = tomllib.load(stream)
    expected = next(f for f in fingerprints["files"] if f["role"] == "entrypoint")
    path = Path(game_root) / expected["path"]
    with path.open("rb") as stream:
        data = stream.read(MAX_IMAGE + 1)
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != expected["size"] or digest != expected["sha256"]:
        raise ValueError("unsupported default.xex fingerprint; vertex source recipes refused")
    # Reuse the key owned by the pinned SDK; never make a second stored copy.
    from decrypt_xex import read_retail_key
    try:
        key = read_retail_key(project_root / "rexglue-sdk")
    except SystemExit as error:
        raise ValueError("cannot read the SDK's XEX key") from error
    image = decode_image(data, key)
    return image, {"name": "default.xex", "bytes": len(data), "sha256": digest,
                   "path": str(path.resolve()), "image_sha256": hashlib.sha256(image).hexdigest()}
