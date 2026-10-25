"""Exercise the real PowerShell packaging contract without retail assets."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = shutil.which('powershell.exe') or shutil.which('pwsh')


@unittest.skipUnless(SHELL, 'PowerShell is required')
class PreparationPayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.catalogue = json.loads((ROOT / 'config/graphics_preparation.json').read_text())

    def check(self, embedded=None, accepted=True):
        text = json.dumps(self.catalogue, indent=2) + '\n'
        (self.directory / 'graphics-preparation.json').write_bytes(text.encode())
        (self.directory / 'rb_blitz.exe').write_bytes(b'MZ' + (text if embedded is None else embedded).encode())
        # Pass paths through environment variables, never shell interpolation.
        import os
        env = dict(os.environ, P9_MODULE=str(ROOT / 'installer/tools/preparation_payload.ps1'),
                   P9_DIRECTORY=str(self.directory))
        result = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-Command',
            "$ErrorActionPreference='Stop'; . $env:P9_MODULE; "
            'Get-PreparationPayload -Directory $env:P9_DIRECTORY | ConvertTo-Json -Depth 4'],
            env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)
        return json.loads(result.stdout) if accepted else None

    def test_complete_embedded_catalogue_has_release_identity(self):
        report = self.check()
        self.assertEqual(report['Sources'], len(self.catalogue['sources']))
        self.assertEqual(report['Counts'], {k: len(v) for k, v in self.catalogue['paths'].items()})
        self.assertEqual(len(report['Sha256']), 64)

    def test_stale_executable_is_rejected(self):
        self.check(embedded='older build', accepted=False)

    def test_shader_instruction_field_is_rejected(self):
        self.catalogue['sources'][0]['microcode'] = '01020304'
        self.check(accepted=False)

    def test_native_cache_field_is_rejected(self):
        self.catalogue['native_blob'] = 'driver bytes'
        self.check(accepted=False)

    def test_incomplete_backend_catalogue_is_rejected(self):
        self.catalogue['paths']['vulkan/fbo'] = []
        self.check(accepted=False)

    def test_oversized_recipe_is_rejected(self):
        self.catalogue['paths']['d3d12/rtv'][0]['raw'] += '00'
        self.check(accepted=False)


if __name__ == '__main__':
    unittest.main(verbosity=2)
