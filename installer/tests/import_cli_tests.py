"""Exercise the local-build CLI with tiny synthetic inputs, including paths with spaces."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

IMPORTER = Path(sys.argv.pop(1)).resolve()


class ImportCliTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="blitz CLI ")
        self.root = Path(self.temporary.name)
        self.source = self.root / "source folder"
        self.destination = self.root / "game folder"
        self.fingerprints = self.root / "fingerprints.toml"
        text = 'schema_version = 1\n[game]\nname = "CLI fixture"\n'
        for role, name in (("entrypoint", "default.xex"), ("archive-header", "gen/main_xbox.hdr"),
                           ("archive-data", "gen/main_xbox_0.ark")):
            data = ("synthetic " + role).encode()
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            text += (f'[[files]]\nrole = "{role}"\npath = "{name}"\nsize = {len(data)}\n'
                     f'sha256 = "{hashlib.sha256(data).hexdigest()}"\n')
        self.fingerprints.write_text(text)

    def tearDown(self):
        self.temporary.cleanup()

    def run_cli(self, *args, expected=0):
        result = subprocess.run([str(IMPORTER), "--fingerprints", str(self.fingerprints),
                                 "--destination", str(self.destination), *map(str, args)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_import_verify_and_preserve_existing_dlc(self):
        self.run_cli("--source", self.source)
        self.run_cli("--verify")
        sentinel = self.destination / "dlc/personal-package"
        sentinel.parent.mkdir()
        sentinel.write_text("private fixture")
        self.run_cli("--source", self.source)
        self.assertEqual(sentinel.read_text(), "private fixture")

    def test_bad_source_keeps_valid_destination(self):
        self.run_cli("--source", self.source)
        (self.source / "default.xex").write_bytes(b"unsupported")
        self.run_cli("--source", self.source, expected=1)
        self.run_cli("--verify")

    def test_missing_and_corrupt_input_fail(self):
        self.run_cli("--verify", expected=1)
        (self.source / "default.xex").write_bytes(b"unsupported")
        self.run_cli("--source", self.source, expected=1)
        self.assertFalse(self.destination.exists())

    def test_requires_exactly_one_mode(self):
        self.run_cli(expected=2)
        self.run_cli("--verify", "--source", self.source, expected=2)


if __name__ == "__main__":
    unittest.main()
