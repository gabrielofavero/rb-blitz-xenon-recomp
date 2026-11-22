"""Host-independent checks for the modular SDK package inventories and staging."""
from pathlib import Path
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer/tools"))
import runtime_inventory as runtime

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

mac = load("desktop_mac_package", "installer/macos/package.py")
linux = load("desktop_linux_package", "installer/linux/package.py")
direct = load("desktop_direct_stage", "scripts/macos/stage_direct.py")

class DesktopInventoryTests(unittest.TestCase):
    def test_cmake_cache_comments_and_blank_lines_preserve_disabled_features(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "CMakeCache.txt").write_bytes(
                b"# CMake cache\r\n\r\n//Build the Vulkan graphics provider\r\n"
                b"REXGLUE_GPU_VULKAN:BOOL=OFF\r\n\r\n//Enable Tracy\r\n"
                b"REXGLUE_ENABLE_TRACY:BOOL=OFF\r\n\r\n//Prepared sources\r\n"
                b"REXGLUE_VULKAN_SOURCE_ROOT:PATH=deliberately-missing\r\n")
            self.assertFalse(runtime.enabled(build, "vulkan"))
            self.assertFalse(runtime.enabled(build, "tracy"))
            self.assertEqual(runtime.cmake_cache(build)["REXGLUE_VULKAN_SOURCE_ROOT"], "deliberately-missing")
            names = runtime.runtime_targets(build, "windows")
            self.assertIn("rexgpu-xenos-d3d12", names)
            self.assertNotIn("rexgpu-xenos-vulkan", names)
            self.assertNotIn("TracyClient", names)

    def test_reviewed_manifests_cover_all_runtime_loaded_modules(self):
        config = json.loads((ROOT / "installer/config/platform-packaging.json").read_text())
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            for platform in ("macos", "linux"):
                entries = runtime.manifest_entries(config[platform], build)
                staged = {entry["fromTarget"] for entry in entries if "fromTarget" in entry}
                self.assertTrue(set(runtime.runtime_targets(build, platform)) <= staged)
                self.assertNotIn("rexruntime", staged)
                self.assertNotIn("rexgpu-xenos", staged)
            (build / "CMakeCache.txt").write_text("REXGLUE_USE_VULKAN:BOOL=OFF\nREXGLUE_ENABLE_TRACY:BOOL=OFF\n")
            for platform in ("macos", "linux"):
                entries = runtime.manifest_entries(config[platform], build)
                self.assertFalse(any(entry.get("when") in {"vulkan", "tracy"} for entry in entries))
                self.assertNotIn("rexgpu-xenos-vulkan", runtime.runtime_targets(build, platform))
                self.assertNotIn("TracyClient", runtime.runtime_targets(build, platform))
            self.assertEqual(runtime.target_filename("SDL3", "linux"), "libSDL3.so.0")

    def test_desktop_stage_refuses_a_missing_runtime_provider(self):
        config = json.loads((ROOT / "installer/config/platform-packaging.json").read_text())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            for platform, packager in (("macos", mac), ("linux", linux)):
                minimal = dict(config[platform])
                minimal["stageManifest"] = [{"fromTarget": "rexui-sdl3", "to": "app/librexui-sdl3.dylib"}]
                with self.assertRaisesRegex(SystemExit, "missing"):
                    if platform == "macos": packager.stage(build, root / platform, minimal, "0.1.0", root)
                    else: packager.stage(build, root / platform, minimal)

    def test_direct_stage_checks_module_presence_and_transitive_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "CMakeCache.txt").write_text("REXGLUE_USE_VULKAN:BOOL=OFF\nREXGLUE_ENABLE_TRACY:BOOL=OFF\n")
            paths = [build / "rb_blitz", *(build / runtime.target_filename(name, "macos")
                     for name in runtime.runtime_targets(build, "macos"))]
            for path in paths: path.write_bytes(b"fixture")
            def dependencies(command, **kwargs):
                dependency = "@rpath/librexbase.dylib" if Path(command[-1]).name == "rb_blitz" else "/usr/lib/libSystem.B.dylib"
                return subprocess.CompletedProcess(command, 0, stdout=f"fixture:\n\t{dependency} (compatibility version 1.0.0)\n")
            with patch.object(direct.subprocess, "run", side_effect=dependencies):
                self.assertEqual(set(direct.verify_runtime(build)), set(paths))
            with patch.object(direct.subprocess, "run", return_value=subprocess.CompletedProcess([], 0,
                    stdout="fixture:\n\t@rpath/libmissing.dylib (compatibility version 1.0.0)\n")):
                with self.assertRaisesRegex(ValueError, "unstaged dependency"): direct.verify_runtime(build)
            (build / "librexui-sdl3.dylib").unlink()
            with self.assertRaisesRegex(ValueError, "runtime module"): direct.verify_runtime(build)

    def test_licenses_use_prepared_sources_and_reject_ambiguous_nested_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            sources = build / "prepared"
            license = sources / "vulkan-headers/LICENSE.md"
            license.parent.mkdir(parents=True)
            license.write_text("fixture Vulkan license")
            cache = build / "cache"
            cereal = cache / "cereal/pin/LICENSE"
            cereal.parent.mkdir(parents=True)
            cereal.write_text("fixture cereal license")
            (build / "CMakeCache.txt").write_text(f"REXGLUE_VULKAN_SOURCE_ROOT:PATH={sources}\nCPM_SOURCE_CACHE:PATH={cache}\n")
            self.assertEqual(runtime.vulkan_source(build, "vulkan-headers/LICENSE.md"), license)
            with self.assertRaisesRegex(ValueError, "missing prepared"): runtime.vulkan_source(build, "../outside")
            self.assertEqual(runtime.molten_dependency_license(build, "cereal"), cereal)
            other = cache / "cereal/old-pin/LICENSE"
            other.parent.mkdir(parents=True)
            other.write_text("different source version")
            with self.assertRaisesRegex(ValueError, "ambiguous"): runtime.molten_dependency_license(build, "cereal")
            nested = cereal.parent / "include/cereal/external/LICENSE"
            nested.parent.mkdir(parents=True)
            nested.write_text("embedded dependency license")
            with (build / "CMakeCache.txt").open("a") as stream:
                stream.write(f"cereal_SOURCE_DIR:STATIC={cereal.parent}\n")
            self.assertEqual(runtime.molten_dependency_license(build, "cereal"), cereal)
            cereal.unlink()
            with self.assertRaisesRegex(ValueError, "missing pinned"):
                runtime.molten_dependency_license(build, "cereal")
            if sys.platform != "win32":
                outside = build / "outside-license"
                outside.write_text("unrelated license")
                cereal.symlink_to(outside)
                with self.assertRaisesRegex(ValueError, "missing pinned"):
                    runtime.molten_dependency_license(build, "cereal")
            spirv = cache / "SPIRV-Cross/LICENSE"
            spirv.parent.mkdir()
            spirv.write_text("fixture SPIRV-Cross license")
            with (build / "CMakeCache.txt").open("a") as stream:
                stream.write(f"CPM_PACKAGE_SPIRV-Cross_SOURCE_DIR:INTERNAL={spirv.parent}\n")
            self.assertEqual(runtime.molten_dependency_license(build, "spirv-cross"), spirv)

    @unittest.skipUnless(shutil.which("pwsh") or shutil.which("powershell"), "requires PowerShell")
    def test_windows_payload_requires_configured_provider_modules(self):
        shell = shutil.which("pwsh") or shutil.which("powershell")
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "CMakeCache.txt").write_text("REXGLUE_USE_VULKAN:BOOL=OFF\nREXGLUE_ENABLE_TRACY:BOOL=OFF\nREXGLUE_USE_D3D12:BOOL=ON\n")
            script = build / "probe.ps1"
            script.write_text("param([string] $Module, [string] $BuildDir)\n. $Module\n@(Get-PayloadRuntimeFiles -BuildDir $BuildDir) | ConvertTo-Json -Compress\n")
            result = subprocess.run([shell, "-NoProfile", "-File", str(script),
                str(ROOT / "installer/tools/payload_provenance.ps1"), str(build)], check=True, text=True, capture_output=True)
            files = set(json.loads(result.stdout))
            self.assertEqual(files, {runtime.target_filename(name, "windows") for name in runtime.runtime_targets(build, "windows")})
            self.assertIn("rexhid-xinput.dll", files)
            self.assertIn("rexgpu-xenos-d3d12.dll", files)
            self.assertIn("rexui-sdl3.dll", files)
            self.assertNotIn("rexgpu-xenos-vulkan.dll", files)
            self.assertNotIn("rexgpu-xenos-base.dll", files)


    @unittest.skipUnless(shutil.which("pwsh") or shutil.which("powershell"), "requires PowerShell")
    def test_windows_snapshot_detects_missing_provider_and_changed_module_bytes(self):
        shell = shutil.which("pwsh") or shutil.which("powershell")
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "CMakeCache.txt").write_text("REXGLUE_USE_VULKAN:BOOL=ON\nREXGLUE_ENABLE_TRACY:BOOL=OFF\nREXGLUE_USE_D3D12:BOOL=ON\n")
            for relative in ("vulkan-headers/LICENSE.md", "vulkan-loader/LICENSE.txt", "vulkan-memory-allocator/LICENSE.txt"):
                license = build / "_deps/rex-vulkan" / relative
                license.parent.mkdir(parents=True, exist_ok=True)
                license.write_text("fixture prepared dependency license")
            script = build / "snapshot.ps1"
            script.write_text(r"""param([string] $Module, [string] $BuildDir, [string] $RepoRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. $Module
$payload = Join-Path $BuildDir 'payload'
New-Item -ItemType Directory -Path $payload | Out-Null
$records = @()
foreach ($name in (Get-PayloadTreeFiles -BuildDir $BuildDir)) {
    $source = Join-Path $BuildDir $name
    [IO.File]::WriteAllText($source, 'fixture module ' + $name)
    $records += [pscustomobject]@{ Name = $name; Source = $source }
}
$loader = Join-Path $BuildDir 'vulkan-1.dll'
[IO.File]::WriteAllText($loader, 'fixture pinned loader')
$records += [pscustomobject]@{ Name = 'vulkan-1.dll'; Source = $loader }
$records += @(Get-PayloadLicenseFiles -BuildDir $BuildDir -RepoRoot $RepoRoot)
foreach ($name in (Get-PayloadRepoFiles)) {
    $records += [pscustomobject]@{ Name = $name; Source = (Join-Path $RepoRoot $name) }
}
function Write-Manifest($Entries) {
    $lines = @('[payload]', 'backends = "d3d12,vulkan"', 'commit = "fixture"', 'vulkan_loader_origin = "staged"')
    foreach ($entry in $Entries) {
        $lines += @('[[files]]', ('path = "' + $entry.Name + '"'),
            ('size = ' + (Get-Item -LiteralPath $entry.Source).Length),
            ('sha256 = "' + (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToLowerInvariant() + '"'))
    }
    [IO.File]::WriteAllLines((Join-Path $payload 'payload-manifest.toml'), $lines)
}
function Reasons {
    return @(Test-PayloadSnapshotCurrent -PayloadDir $payload -SourceDir $BuildDir -ExpectedBackends @('d3d12', 'vulkan') -ExpectedCommit 'fixture' -RepoRoot $RepoRoot)
}
Write-Manifest $records
$initial = @(Reasons)
Write-Manifest @($records | Where-Object { $_.Name -ne 'rexui-sdl3.dll' })
$missing = @(Reasons)
Write-Manifest $records
[IO.File]::WriteAllText((Join-Path $BuildDir 'rexui-sdl3.dll'), 'mutated module bytes')
$changed = @(Reasons)
[IO.File]::WriteAllText($loader, 'mutated loader bytes')
$loaderChanged = @(Reasons)
@{ initial = $initial; missing = $missing; changed = $changed; loaderChanged = $loaderChanged } | ConvertTo-Json -Compress
""")
            result = subprocess.run([shell, "-NoProfile", "-File", str(script),
                str(ROOT / "installer/tools/payload_provenance.ps1"), str(build), str(ROOT)],
                check=True, text=True, capture_output=True)
            outcomes = json.loads(result.stdout)
            self.assertEqual(outcomes["initial"], [])
            self.assertTrue(any("does not list rexui-sdl3.dll" in reason for reason in outcomes["missing"]))
            self.assertTrue(any("rexui-sdl3.dll" in reason for reason in outcomes["changed"]))
            self.assertTrue(any("source-built staged loader" in reason for reason in outcomes["loaderChanged"]))

if __name__ == "__main__": unittest.main()
