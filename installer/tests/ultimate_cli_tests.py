"""Non-UI native Ultimate import regressions; no network or game content."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

IMPORTER = Path(sys.argv.pop(1)).resolve()


class UltimateCliTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="blitz Ultimate ")
        self.root = Path(self.temporary.name)
        self.game = self.root / "game"
        self.game.mkdir()
        self.fingerprints = self.root / "base.toml"
        text = 'schema_version = 1\n'
        for role, name in (("entrypoint", "default.xex"), ("archive-header", "gen/main_xbox.hdr"),
                           ("archive-data", "gen/main_xbox_0.ark")):
            data = ("base fixture " + role).encode()
            file = self.game / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
            text += (f'[[files]]\nrole = "{role}"\npath = "{name}"\nsize = {len(data)}\n'
                     f'sha256 = "{hashlib.sha256(data).hexdigest()}"\n')
        self.fingerprints.write_text(text)
        self.source = self.root / "Ultimate folder"
        for name in ("gen/patch_xbox.hdr", "gen/patch_xbox_0.ark", "support/info.txt", "default.xex"):
            file = self.source / "Xbox" / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("Ultimate fixture " + name)

    def tearDown(self):
        self.temporary.cleanup()

    def run_cli(self, source, expected=0):
        result = subprocess.run([str(IMPORTER), "--fingerprints", str(self.fingerprints),
            "--destination", str(self.game), "--verify", "--ultimate-source", str(source)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def zip_source(self, name="Ultimate.zip"):
        archive = self.root / name
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for file in self.source.rglob("*"):
                if file.is_file():
                    output.write(file, file.relative_to(self.source))
        return archive

    def test_zip_import_excludes_xex_and_preserves_base(self):
        before = (self.game / "default.xex").read_bytes()
        result = self.run_cli(self.zip_source())
        self.assertIn("differs from", result.stderr)
        self.assertEqual((self.game / "default.xex").read_bytes(), before)
        self.assertFalse((self.game / "ultimate/default.xex").exists())
        self.assertTrue((self.game / "ultimate/support/info.txt").is_file())
        self.assertTrue((self.game / "ultimate/gen/patch_xbox_0.ark").is_file())

    def test_folder_import_and_failure_preserve_previous_overlay(self):
        self.run_cli(self.source)
        before = (self.game / "ultimate/gen/patch_xbox.hdr").read_bytes()
        (self.source / "Xbox/gen/patch_xbox_0.ark").unlink()
        self.run_cli(self.source, expected=1)
        self.assertEqual((self.game / "ultimate/gen/patch_xbox.hdr").read_bytes(), before)

    def test_corrupt_archive_preserves_previous_overlay(self):
        self.run_cli(self.source)
        invalid = self.root / "bad.zip"
        invalid.write_bytes(b"not a zip archive")
        self.run_cli(invalid, expected=1)
        self.assertTrue((self.game / "ultimate/gen/patch_xbox_0.ark").is_file())

    def test_zip_traversal_is_rejected(self):
        archive = self.zip_source()
        with zipfile.ZipFile(archive, "a") as output:
            output.writestr("../escape.txt", "never write this")
        self.run_cli(archive, expected=1)
        self.assertFalse((self.root / "escape.txt").exists())
        self.assertFalse((self.game / "ultimate").exists())


if __name__ == "__main__":
    unittest.main()
