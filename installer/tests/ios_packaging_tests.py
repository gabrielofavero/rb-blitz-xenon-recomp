"""Headless IPA regressions; native cases compile tiny iPhoneOS fixtures only."""
from __future__ import annotations

import datetime as dt
import hashlib
import importlib.util
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ios_package", ROOT / "installer/ios/package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackagingTests(unittest.TestCase):
    def test_documents_are_shared_with_files(self):
        with tempfile.TemporaryDirectory() as name:
            app = Path(name)
            with patch.object(package, "run"):
                package.write_info(app, "example.blitz", "0.1.0", "17.0")
            info = plistlib.loads((app / "Info.plist").read_bytes())
            self.assertTrue(info["UIFileSharingEnabled"])
            self.assertTrue(info["LSSupportsOpeningDocumentsInPlace"])

    def test_game_contract_verifies_all_files_and_rejects_links(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            records = []
            for path in sorted(package.GAME_FILES):
                target = root / "game" / path
                target.parent.mkdir(parents=True, exist_ok=True)
                data = path.encode()
                target.write_bytes(data)
                records.append(f'[[files]]\npath = "{path}"\nsize = {len(data)}\n'
                               f'sha256 = "{hashlib.sha256(data).hexdigest()}"\n')
            manifest = root / "fingerprints.toml"
            manifest.write_text("\n".join(records))
            self.assertEqual(set(package.verify_game(root / "game", manifest)), package.GAME_FILES)
            target = root / "game/default.xex"
            target.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "corrupt game input"):
                package.verify_game(root / "game", manifest)
            target.unlink()
            target.symlink_to(manifest)
            with self.assertRaisesRegex(ValueError, "linked source"):
                package.verify_game(root / "game", manifest)

    def test_ultimate_is_bundled_without_mod_executable_or_private_settings(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            game = root / "game"
            app = root / "app"
            records = []
            for role, relative in (("ultimate-archive-header", "gen/patch_xbox.hdr"),
                                   ("ultimate-archive-data", "gen/patch_xbox_0.ark")):
                target = game / "ultimate" / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                data = relative.encode()
                target.write_bytes(data)
                records.append(f'[[files]]\nrole = "{role}"\npath = "{relative}"\n'
                               f'size = {len(data)}\nsha256 = "{hashlib.sha256(data).hexdigest()}"\n')
            (game / "ultimate/default.xex").write_bytes(b"mod executable")
            (game / "ultimate/private-settings.txt").write_bytes(b"private preferences")
            fingerprints = root / "fingerprints.toml"
            fingerprints.write_text("\n".join(records))
            package.stage_ultimate(app, game, fingerprints)
            self.assertEqual({p.relative_to(app / "game/ultimate").as_posix()
                              for p in (app / "game/ultimate").rglob("*") if p.is_file()},
                             package.ULTIMATE_FILES | {"screenshots/_keepme"})
            self.assertEqual((app / "game/ultimate/screenshots/_keepme").read_bytes(), b"zxc")
            target = game / "ultimate/gen/patch_xbox_0.ark"
            target.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "corrupt Ultimate"):
                package.verify_ultimate(game / "ultimate", fingerprints)
            target.unlink()
            target.symlink_to(fingerprints)
            with self.assertRaisesRegex(ValueError, "linked source"):
                package.verify_ultimate(game / "ultimate", fingerprints)
            target.unlink()
            with self.assertRaisesRegex(ValueError, "missing or escaping source"):
                package.verify_ultimate(game / "ultimate", fingerprints)

    def test_parent_link_is_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "outside").mkdir()
            (root / "outside/file").touch()
            (root / "game").mkdir()
            (root / "game/gen").symlink_to(root / "outside", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "linked source"):
                package.safe_file(root / "game", "gen/file")
            with self.assertRaisesRegex(ValueError, "unsafe source"):
                package.safe_file(root / "game", "../outside/file")

    def profile(self):
        return {"Platform": ["iOS"], "ExpirationDate": dt.datetime(2099, 1, 1),
                "ProvisionedDevices": ["fixture-device"], "TeamIdentifier": ["TEAM"],
                "Entitlements": {"application-identifier": "TEAM.example.*",
                                 "get-task-allow": True, package.VA_ENTITLEMENT: True}}

    def test_signing_requires_device_profile_extended_va_and_matching_bundle(self):
        profile = self.profile()
        entitlements = package.signing_entitlements(profile, "example.blitz")
        self.assertEqual(entitlements["application-identifier"], "TEAM.example.blitz")
        self.assertTrue(entitlements[package.VA_ENTITLEMENT])
        with self.assertRaisesRegex(ValueError, "bundle identifier"):
            package.signing_entitlements(profile, "another.blitz")
        profile["Entitlements"][package.VA_ENTITLEMENT] = False
        with self.assertRaisesRegex(ValueError, "extended-virtual-addressing"):
            package.signing_entitlements(profile, "example.blitz")
        profile = self.profile()
        profile["ExpirationDate"] = dt.datetime(2000, 1, 1)
        with self.assertRaisesRegex(ValueError, "expired"):
            package.signing_entitlements(profile, "example.blitz")
        profile = self.profile()
        del profile["ProvisionedDevices"]
        with self.assertRaisesRegex(ValueError, "cannot sideload"):
            package.signing_entitlements(profile, "example.blitz")

    def test_device_audit_rejects_simulator_and_newer_minimum(self):
        with patch.object(package, "run", side_effect=["arm64", "platform IOSSIMULATOR\nminos 17.0"]):
            with self.assertRaisesRegex(ValueError, "not macOS or a simulator"):
                package.audit_device_binary(Path("fixture"), "17.0")
        with patch.object(package, "run", side_effect=["arm64", "platform IOS\nminos 18.0"]):
            with self.assertRaisesRegex(ValueError, "newer than declared"):
                package.audit_device_binary(Path("fixture"), "17.0")

    def test_signing_preserves_legacy_application_prefix(self):
        profile = self.profile()
        profile["ApplicationIdentifierPrefix"] = ["LEGACY"]
        profile["Entitlements"]["application-identifier"] = "LEGACY.example.*"
        entitlements = package.signing_entitlements(profile, "example.blitz")
        self.assertEqual(entitlements["application-identifier"], "LEGACY.example.blitz")
        self.assertEqual(entitlements["com.apple.developer.team-identifier"], "TEAM")

    def test_dependency_resolution_rejects_colliding_or_escaping_libraries(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "a").mkdir()
            (root / "b").mkdir()
            (root / "a/libfixture.dylib").write_bytes(b"one")
            (root / "b/libfixture.dylib").write_bytes(b"two")
            with self.assertRaisesRegex(ValueError, "ambiguous"):
                package.find_library("libfixture.dylib", [root])
            (root / "outside.dylib").write_bytes(b"one")
            (root / "a/libfixture.dylib").unlink()
            (root / "a/libfixture.dylib").symlink_to(root / "outside.dylib")
            with self.assertRaisesRegex(ValueError, "escapes"):
                package.find_library("libfixture.dylib", [root / "a"])

    def test_archive_layout_preserves_executable_mode_and_rejects_links(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            payload = root / "Payload"
            app = payload / "Rock Band Blitz.app"
            app.mkdir(parents=True)
            executable = app / "rb_blitz"
            executable.write_bytes(b"fixture")
            executable.chmod(0o755)
            archive = root / "fixture.ipa"
            package.archive_ipa(payload, archive)
            with zipfile.ZipFile(archive) as stream:
                info = stream.getinfo("Payload/Rock Band Blitz.app/rb_blitz")
                self.assertEqual((info.external_attr >> 16) & 0o777, 0o755)
                self.assertIsNone(stream.testzip())
            (app / "link").symlink_to(executable)
            with self.assertRaisesRegex(ValueError, "bundle symlink"):
                package.archive_ipa(payload, root / "bad.ipa")

    def test_cli_refuses_to_overwrite_an_existing_ipa(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            with (ROOT / "installer/config/pins.toml").open("rb") as stream:
                version = tomllib.load(stream)["installer"]["version"]
            existing = root / f"RockBandBlitz-{version}-ios-arm64-unsigned.ipa"
            existing.write_bytes(b"previous artifact")
            arguments = ["package.py", "--build-dir", name, "--controller-validator", "unused",
                         "--output", name]
            with patch.object(sys, "argv", arguments), self.assertRaisesRegex(ValueError, "archive the existing IPA"):
                package.main()
            self.assertEqual(existing.read_bytes(), b"previous artifact")

    def test_graphics_catalogue_requires_a_current_game_build(self):
        with tempfile.TemporaryDirectory() as name:
            app = Path(name)
            catalogue = b'{"schema":1,"title_id":"5841122D"}'
            (app / "graphics-preparation.json").write_bytes(catalogue)
            (app / "rb_blitz").write_bytes(b"fixture" + catalogue)
            package.audit_preparation(app)
            (app / "rb_blitz").write_bytes(b"stale fixture")
            with self.assertRaisesRegex(ValueError, "rebuild before packaging"):
                package.audit_preparation(app)


def iphone_sdk_available() -> bool:
    if not shutil.which("xcrun"):
        return False
    return subprocess.run(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"],
                          capture_output=True).returncode == 0


@unittest.skipUnless(iphone_sdk_available(), "requires an Xcode iPhoneOS SDK selected by DEVELOPER_DIR")
class NativePackagingTests(unittest.TestCase):
    def test_real_device_libraries_relocate_into_frameworks(self):
        with tempfile.TemporaryDirectory(prefix="rb-blitz-ios-fixture-") as name:
            root = Path(name)
            build = root / "build"
            build.mkdir()
            sdk = package.run("xcrun", "--sdk", "iphoneos", "--show-sdk-path")
            source = root / "fixture.c"
            source.write_text("int fixture(void) { return 1; }\n")
            flags = ["xcrun", "--sdk", "iphoneos", "clang", "-arch", "arm64", "-isysroot", sdk,
                     "-miphoneos-version-min=17.0", "-Wl,-headerpad_max_install_names"]
            for library in ("librexruntime.dylib", "librexgpu-xenos.dylib", "libMoltenVK.dylib"):
                subprocess.run([*flags, "-dynamiclib", str(source), "-o", str(build / library),
                                "-Wl,-install_name," + str(build / library)],
                               check=True, capture_output=True)
            source.write_text("extern int fixture(void); int main(void) { return fixture(); }\n")
            subprocess.run([*flags, str(source), str(build / "librexruntime.dylib"),
                            "-Wl,-rpath," + str(build), "-o", str(build / "rb_blitz")],
                           check=True, capture_output=True)
            libraries = package.collect_libraries(build, [build], "17.0")
            app = root / "Payload/Rock Band Blitz.app"
            package.copy_file(build / "rb_blitz", app / "rb_blitz", executable=True)
            # Actual device builds already carry the intended rpath. Preserve it;
            # install_name_tool rejects deleting and adding the same path at once.
            package.run("xcrun", "install_name_tool", "-add_rpath", "@executable_path/Frameworks",
                        str(app / "rb_blitz"))
            for library, path in libraries.items():
                package.stage_framework(app, path, library, "example.blitz", "0.1.0", "17.0")
            binaries = package.prepare_binaries(app, "17.0", list(libraries))
            self.assertEqual(len(binaries), 4)
            for binary in binaries:
                commands = package.load_commands(binary)
                self.assertIn(("LC_RPATH", "@executable_path/Frameworks"), commands)
                self.assertNotIn(str(build), repr(commands))
            self.assertIn(("LC_LOAD_DYLIB", "@rpath/rexruntime.framework/rexruntime"),
                          package.load_commands(app / "rb_blitz"))
            package.write_info(app, "example.blitz", "0.1.0", "17.0")
            info = plistlib.loads((app / "Info.plist").read_bytes())
            self.assertEqual(info["CFBundleExecutable"], "rb_blitz")
            self.assertEqual(info["UIDeviceFamily"], [1, 2])
            self.assertEqual(info["CFBundleSupportedPlatforms"], ["iPhoneOS"])
            # Ad hoc signatures validate the framework structure headlessly;
            # they do not claim that this fixture can install on an iPhone.
            for binary in binaries[1:]:
                package.run("codesign", "--force", "--sign", "-", "--timestamp=none", str(binary.parent))
            package.run("codesign", "--force", "--sign", "-", "--timestamp=none", str(app))
            package.run("codesign", "--verify", "--deep", "--strict", str(app))
            package.archive_ipa(root / "Payload", root / "fixture.ipa")
            with zipfile.ZipFile(root / "fixture.ipa") as stream:
                self.assertIn("Payload/Rock Band Blitz.app/Frameworks/MoltenVK.framework/MoltenVK", stream.namelist())
                self.assertFalse(any(name.endswith(".dylib") for name in stream.namelist()))


if __name__ == "__main__":
    unittest.main()
