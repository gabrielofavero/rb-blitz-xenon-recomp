"""Exercise the shipped collector against isolated files; never launches the game."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SHELL = shutil.which('powershell.exe') or shutil.which('pwsh')


@unittest.skipUnless(SHELL, 'PowerShell is required')
class CollectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='rb diagnostics ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'Saved Games'
        self.source.mkdir()
        self.output = self.root / 'report.zip'

    def collect(self):
        return subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-File', str(ROOT / 'scripts/diagnostics/collect_diagnostics.ps1'),
            '-DiagnosticsDirectory', str(self.source), '-GameDirectory', str(self.root),
            '-OutputPath', str(self.output), '-Note', 'Bounce Man, first play'],
            capture_output=True, text=True, errors='replace', timeout=30)

    def test_exact_evidence_and_identity_without_saves_or_binary_bytes(self):
        payload = b'partial final line\r\nshader_trace: file_read\x00\xff'
        (self.source / 'game.log').write_bytes(payload)
        (self.source / 'unmapped-shaders.jsonl').write_bytes(b'{"schema":1}\n')
        (self.source / 'session-info.txt').write_text('build=fixture')
        (self.source / 'player-save.bin').write_bytes(b'private fixture')
        binary = b'MZ fixture, hash only'
        (self.root / 'rb_blitz.exe').write_bytes(binary)
        module = b'MZ module fixture, hash only'
        for name in ('rexkernel.dll', 'rexapu-sdl3.dll', 'unrelated.dll'):
            (self.root / name).write_bytes(module)
        result = self.collect()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(set(archive.namelist()), {'manifest.json', 'diagnostics/game.log',
                'diagnostics/unmapped-shaders.jsonl', 'diagnostics/session-info.txt'})
            self.assertEqual(archive.read('diagnostics/game.log'), payload)
            manifest = json.loads(archive.read('manifest.json'))
            row = next(r for r in manifest['files'] if r['name'] == 'game.log')
            self.assertEqual(row['sha256'], hashlib.sha256(payload).hexdigest())
            self.assertEqual(row['bytes'], len(payload))
            self.assertTrue(row['complete_snapshot'])
            self.assertEqual(manifest['user_note'], 'Bounce Man, first play')
            self.assertEqual(manifest['build_files_at_collection'][0]['sha256'],
                             hashlib.sha256(binary).hexdigest())
            identities = {r['name']: r for r in manifest['build_files_at_collection']}
            self.assertEqual(set(identities), {'rb_blitz.exe', 'rexkernel.dll', 'rexapu-sdl3.dll'})
            for name in ('rexkernel.dll', 'rexapu-sdl3.dll'):
                self.assertEqual(identities[name]['sha256'], hashlib.sha256(module).hexdigest())
                self.assertEqual(identities[name]['bytes'], len(module))
            self.assertIn('previous.log', manifest['missing'])

    def test_existing_archive_is_preserved(self):
        self.output.write_bytes(b'existing report')
        result = self.collect()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), b'existing report')

    def test_oversized_evidence_is_reported_without_reading_it(self):
        (self.source / 'game.log').write_text('normal log')
        with (self.source / 'unmapped-shaders.jsonl').open('wb') as stream:
            stream.truncate(64 * 1024 * 1024 + 1)
        result = self.collect()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            manifest = json.loads(archive.read('manifest.json'))
            self.assertEqual(manifest['skipped'][0]['name'], 'unmapped-shaders.jsonl')
            self.assertNotIn('diagnostics/unmapped-shaders.jsonl', archive.namelist())


if __name__ == '__main__':
    unittest.main()
