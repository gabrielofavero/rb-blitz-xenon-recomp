"""Synthetic source initializers and output checks; never reads a real SDK key."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("decrypt_xex", ROOT / "scripts/xex/decrypt_xex.py")
decrypt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(decrypt)


def array(values):
    return "{"+", ".join(f"0x{value:02X}" for value in values)+"}"


def table(retail=None):
    rows = [list(range(16)) if retail is None else retail, [], list(range(16,32)), list(range(32,48))]
    return "constexpr std::array<Key, 4> kMasterKeys = {{"+", ".join(array(row) for row in rows)+"}};"


class KeySourceTests(unittest.TestCase):
    def test_exact_current_table_and_legacy_initializer(self):
        expected = bytes(range(16))
        self.assertEqual(decrypt.parse_retail_key(table(), table=True), expected)
        old = "const uint8_t xe_xex2_retail_key[16] = "+array(expected)+";"
        self.assertEqual(decrypt.parse_retail_key(old, table=False), expected)
        for source in (table(),old):
            self.assertEqual(decrypt.parse_retail_key("// 0xFF\n"+source+"\nint unrelated=0xFF;",table=source.startswith("constexpr")),expected)

    def test_rejects_short_long_ambiguous_and_unbounded_initializers(self):
        for source in (table(list(range(15))), table(list(range(17))), table()+table(),
                       table().replace("0x00", "0x100",1), table().replace("0x00", "expression()",1),
                       table().replace("0x00,", "0x00",1), table()[:-2], table().replace("= {{", "= {")):
            with self.subTest(shape=len(source)), self.assertRaises(ValueError):
                decrypt.parse_retail_key(source,table=True)
        short = "uint8_t xe_xex2_retail_key[16] = "+array(range(15))+";\nint other[]={0xAB};"
        with self.assertRaises(ValueError): decrypt.parse_retail_key(short,table=False)
        oversized = "uint8_t xe_xex2_retail_key[16] = {"+" "*4096+"};"
        with self.assertRaises(ValueError): decrypt.parse_retail_key(oversized,table=False)

    def test_prefers_current_runtime_path_and_never_falls_back_after_invalid_table(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / decrypt.SDK_KEY_FILE; old.parent.mkdir(parents=True)
            old.write_text("uint8_t xe_xex2_retail_key[16] = "+array(range(16))+";")
            self.assertEqual(decrypt.read_retail_key(root),bytes(range(16)))
            current = root / decrypt.SDK_KEY_TABLE_FILE; current.parent.mkdir(parents=True)
            current.write_text(table(list(range(16,32))))
            self.assertEqual(decrypt.read_retail_key(root),bytes(range(16,32)))
            current.write_text("unsupported declaration")
            with self.assertRaises(SystemExit) as error: decrypt.read_retail_key(root)
            self.assertNotIn(bytes(range(16,32)).hex(),str(error.exception))

    def test_decryption_does_not_print_wrapped_or_session_keys(self):
        class Cipher:
            def decrypt(self, data): return data
        fake = SimpleNamespace(MODE_ECB=1, MODE_CBC=2, new=lambda *args: Cipher())
        module = ModuleType("Crypto.Cipher"); module.AES = fake
        with tempfile.TemporaryDirectory() as temporary, patch.dict(sys.modules,{"Crypto":ModuleType("Crypto"),"Crypto.Cipher":module}):
            source = Path(temporary) / "synthetic.xex"
            data = bytearray(0x310); data[:4]=b"XEX2"
            for offset,value in [(8,0x300),(0x10,0x60),(0x14,1),(0x18,decrypt.FF_OFFSET_KEY),(0x1C,0x30),(0x30,8),(0x64,16)]:
                struct.pack_into(">I",data,offset,value)
            struct.pack_into(">HH",data,0x34,1,0)
            wrapped = bytes(range(160,176)); data[0x1B0:0x1C0]=wrapped
            data[0x300:]=b"MZ"+b"x"*14; source.write_bytes(data)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(decrypt.decrypt_image(source,Path(temporary)/"image.bin",bytes(range(16))),0)
            self.assertNotIn("session_key",output.getvalue())
            self.assertNotIn("aes_key",output.getvalue())
            self.assertNotIn(wrapped.hex(),output.getvalue())
            self.assertNotIn(bytes(range(16)).hex(),output.getvalue())


if __name__ == "__main__": unittest.main()
