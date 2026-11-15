"""Non-UI packaging regressions. Native tests use tiny, non-game Mach-O fixtures.

Run: python3 -m unittest discover -s installer/tests -p '*packaging_tests.py' -v
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("macos_package", ROOT / "installer/macos/package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class MetadataTests(unittest.TestCase):
    def test_metadata(self):
        subprocess.run([sys.executable, str(ROOT / "installer/tools/check_platform_packaging.py")],
                       check=True, capture_output=True)

    def test_controller_rejects_parent_traversal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "assets/controller/example"
            folder.mkdir(parents=True)
            (folder / "scheme.toml").write_text("")
            validator = root / "validator"
            validator.write_text("#!/bin/sh\nprintf 'scheme.toml\\n../outside.png\\n'\n")
            validator.chmod(0o755)
            with self.assertRaisesRegex(ValueError, "unsafe"):
                package.controller_entries(root, validator, "assets/controller")

    def test_preparation_requires_exact_embedded_catalogue(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp)
            macos = app / "Contents/MacOS"
            macos.mkdir(parents=True)
            raw = (ROOT / "config/graphics_preparation.json").read_bytes().replace(b"\r\n", b"\n")
            (macos / "graphics-preparation.json").write_bytes(raw)
            (macos / "rb_blitz").write_bytes(b"fixture" + raw)
            package.audit_preparation(app)
            (macos / "rb_blitz").write_bytes(b"stale fixture")
            with self.assertRaisesRegex(SystemExit, "rebuild"):
                package.audit_preparation(app)

    def test_preparation_rejects_shader_blob_field(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp)
            macos = app / "Contents/MacOS"
            macos.mkdir(parents=True)
            data = json.loads((ROOT / "config/graphics_preparation.json").read_text())
            data["sources"][0]["bytecode"] = "do not ship"
            (macos / "graphics-preparation.json").write_text(json.dumps(data))
            with self.assertRaisesRegex(SystemExit, "unexpected"):
                package.audit_preparation(app)


@unittest.skipUnless(sys.platform == "darwin", "requires Apple packaging tools")
class NativePackagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="rb-blitz-package-fixtures-")
        cls.root = Path(cls.temporary.name)
        cls.build = cls.root / "build"
        cls.build.mkdir()
        source = cls.build / "empty.c"
        source.write_text("int main(void) { return 0; }\n")
        for name in ("librexruntime.dylib", "librexgpu-xenos.dylib", "libTracyClient.dylib",
                     "libvulkan.1.dylib", "libMoltenVK.dylib"):
            subprocess.run(["xcrun", "clang", "-dynamiclib", str(source), "-o", str(cls.build / name),
                            "-mmacosx-version-min=13.0",
                            "-Wl,-headerpad_max_install_names", "-Wl,-install_name," + str(cls.build / name)],
                           check=True, capture_output=True)
        for name in ("rb_blitz", "rb_blitz_macos_launcher"):
            subprocess.run(["xcrun", "clang", str(source), "-o", str(cls.build / name),
                            "-mmacosx-version-min=13.0",
                            "-Wl,-headerpad_max_install_names", "-Wl,-rpath," + str(cls.build),
                            str(cls.build / "librexruntime.dylib")], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.temporary_stage = tempfile.TemporaryDirectory(dir=self.root)
        self.stage = Path(self.temporary_stage.name) / "stage"
        self.app = self.stage / "Rock Band Blitz.app"
        self.macos = self.app / "Contents/MacOS"
        self.macos.mkdir(parents=True)
        self.config = json.loads(package.CONFIG.read_text())["macos"]
        entries = []
        for source in self.build.iterdir():
            if source.suffix == ".c":
                continue
            relative = ("vulkan/lib/" if source.name in ("libvulkan.1.dylib", "libMoltenVK.dylib") else "") + source.name
            destination = self.macos / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            entries.append({"to": destination.relative_to(self.stage).as_posix()})
        info = self.app / "Contents/Info.plist"
        info.write_text((ROOT / "installer/macos/Info.plist.in").read_text().replace("@RBBLITZ_GAME_VERSION@", "0.1.0"))
        entries.append({"to": info.relative_to(self.stage).as_posix()})
        self.config["stageManifest"] = entries

    def tearDown(self):
        self.temporary_stage.cleanup()

    def prepare(self):
        package.prepare_binaries(self.app)

    def audit(self):
        package.audit_app(self.app, self.stage, platform.machine(), self.config)

    def test_relocation_signatures_and_pkg_round_trip(self):
        self.prepare()
        self.audit()
        output = Path(self.temporary_stage.name) / "fixture.pkg"
        package.run("pkgbuild", "--root", str(self.stage), "--install-location", "/Applications",
                    "--component-plist", str(ROOT / self.config["componentProperties"]),
                    "--identifier", self.config["packageId"], "--version", "0.1.0", str(output))
        package.compare_expanded_pkg(output, self.stage, Path(self.temporary_stage.name), self.config, "0.1.0")
        commands = package.load_commands(self.macos / "rb_blitz")
        self.assertIn(("LC_LOAD_DYLIB", "@rpath/librexruntime.dylib"), commands)
        self.assertFalse(any(str(self.build) in value for _, value in commands))

    def test_audit_rejects_unresolved_real_rpath(self):
        self.prepare()
        binary = self.macos / "rb_blitz"
        package.run("install_name_tool", "-delete_rpath", "@executable_path", "-delete_rpath",
                    "@loader_path", str(binary))
        package.run("codesign", "--force", "--sign", "-", str(binary))
        with self.assertRaisesRegex(SystemExit, "unresolved @rpath"):
            self.audit()

    def test_rejects_symlink(self):
        self.prepare()
        (self.macos / "linked").symlink_to("rb_blitz")
        with self.assertRaisesRegex(SystemExit, "symlink"):
            self.audit()

    def test_rejects_renamed_game_package(self):
        self.prepare()
        (self.macos / "innocent.dat").write_bytes(b"LIVE" + b"\0" * 100)
        with self.assertRaisesRegex(SystemExit, "retail or PE"):
            self.audit()

    def test_rejects_renamed_windows_executable(self):
        self.prepare()
        (self.macos / "innocent.dat").write_bytes(b"MZ\0\0" + b"\0" * 100)
        with self.assertRaisesRegex(SystemExit, "retail or PE"):
            self.audit()

    def test_rejects_minimum_os_above_bundle(self):
        self.prepare()
        self.config["deploymentTargetCandidate"] = "12.0"
        with self.assertRaisesRegex(SystemExit, "newer OS"):
            self.audit()

    def test_rejects_machine_path_across_chunk_boundary(self):
        self.prepare()
        marker = str(ROOT).encode()
        (self.macos / "large.dat").write_bytes(b"a" * (1024 * 1024 - 2) + marker)
        with self.assertRaisesRegex(SystemExit, "build-machine path"):
            self.audit()

    def test_rejects_wrong_architecture(self):
        self.prepare()
        other = "x86_64" if platform.machine() == "arm64" else "arm64"
        with self.assertRaisesRegex(SystemExit, "requested"):
            package.audit_app(self.app, self.stage, other, self.config)

    def test_expanded_package_rejects_outside_app_payload(self):
        self.prepare()
        (self.stage / "unrelated-file").write_text("not part of the app")
        output = Path(self.temporary_stage.name) / "extra.pkg"
        package.run("pkgbuild", "--root", str(self.stage), "--install-location", "/Applications",
                    "--identifier", self.config["packageId"], "--version", "0.1.0", str(output))
        with self.assertRaisesRegex(SystemExit, "outside the application"):
            package.compare_expanded_pkg(output, self.stage, Path(self.temporary_stage.name), self.config, "0.1.0")


if __name__ == "__main__":
    unittest.main()
