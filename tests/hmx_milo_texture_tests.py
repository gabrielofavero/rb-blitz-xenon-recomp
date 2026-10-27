"""Hermetic scene-texture tests; no retail files or game UI needed."""

from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import hmx_milo
import hmx_tex


def scene(platform):
    # Three opaque BC3 blocks plus transparent white with junk RGB.
    blocks = b"".join(
        bytes([alpha, alpha]) + bytes(6) + struct.pack("<HHI", color, color, 0)
        for color, alpha in ((0xF800, 255), (0x07E0, 255),
                             (0x001F, 255), (0xFFFF, 0))
    )
    stored = hmx_tex.unswap(blocks) if platform == "xbox" else blocks
    header = (b"\x12\x34\x56\x78\x08\x00\x00\x00\x18\x00" +
              struct.pack(">HHH", 8, 8, 8) + bytes(7))
    prefix = b"fixture-path"

    class Scene:
        path = f"fixture.milo_{platform}"

        def stream(self):
            return prefix + header + bytes(8) + stored + b"trailing object"

        def texture_records(self):
            return [{"path": "fixture.png", "width": 8, "height": 8,
                     "row_bytes": 8, "bpp": 8, "at": len(prefix)}]

        scene_textures = hmx_milo.Milo.scene_textures

    return Scene()


class SceneTextureTests(unittest.TestCase):
    def test_preamble_and_platform_byte_order(self):
        for platform in ("xbox", "ps3"):
            with self.subTest(platform=platform):
                milo = scene(platform)
                item, = milo.scene_textures()
                self.assertEqual(item["payload_at"], item["record_at"] + 31)
                with tempfile.TemporaryDirectory() as directory:
                    output = Path(directory) / "texture.png"
                    args = types.SimpleNamespace(milo=milo.path, list=False,
                                                 asset="fixture.png", out=output)
                    with patch.object(hmx_milo, "load", return_value=milo):
                        self.assertEqual(hmx_milo.cmd_texture(args), 0)
                    image = Image.open(output).convert("RGBA")
                    self.assertEqual(image.size, (8, 8))
                    self.assertEqual(image.getpixel((0, 0)), (255, 0, 0, 255))
                    self.assertEqual(image.getpixel((5, 0)), (0, 255, 0, 255))
                    self.assertEqual(image.getpixel((0, 5)), (0, 0, 255, 255))
                    self.assertEqual(image.getpixel((5, 5)), (0, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
