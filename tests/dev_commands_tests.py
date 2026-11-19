"""Headless development command routing and settings-copy regressions."""
import importlib.util
import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("dev_commands", Path(__file__).resolve().parents[1] / "scripts/dev/dev.py")
dev = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dev)
REPOSITORY = Path(__file__).resolve().parents[1]
GIT_BASH = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe"
BASH = str(GIT_BASH) if GIT_BASH.is_file() else shutil.which("bash")
SHELL_ENV = dict(os.environ)


class DevCommandsTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="rb-blitz-dev-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.root_patch = patch.object(dev, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.env_patch = patch.dict(os.environ, {"XDG_CONFIG_HOME": str(self.root / "config")}, clear=True)
        self.env_patch.start()
        self.addCleanup(self.env_patch.stop)

    def test_native_architectures(self):
        for target in ("linux", "mac"):
            for machine, arch in (("AMD64", "amd64"), ("x86_64", "amd64"), ("aarch64", "arm64"), ("arm64", "arm64")):
                self.assertEqual(dev.preset(target, machine), f"{target}-{arch}-release")
        self.assertEqual(dev.preset("ios"), "ios-arm64-release")
        with self.assertRaises(ValueError):
            dev.preset("linux", "riscv64")

    def test_portrait_preserves_source_and_forces_resolution_profile(self):
        source = self.root / "custom.toml"
        original = "# profile\n[launch]\ntarget = 'ultimate'\n"
        source.write_text(original)
        settings = source.with_name("custom.game-settings.toml")
        text = "schema_version = 1\n# retain this\n[graphics]\nforce_16_by_9 = false\naspect_ratio = 'auto'\nrenderer = 'vulkan'\n[provenance]\n'graphics.force_16_by_9' = 'automatic'\n[controller]\nscheme = 'xbox-series'\n"
        settings.write_text(text)
        copied = dev.portrait_profile(source)
        values = tomllib.loads(copied.with_name("settings.toml").read_text())
        self.assertEqual(values["graphics"]["orientation"], "portrait")
        self.assertEqual(values["graphics"]["aspect_ratio"], "16:9")
        self.assertIs(values["graphics"]["force_16_by_9"], True)
        self.assertEqual(values["graphics"]["renderer"], "vulkan")
        self.assertEqual(values["provenance"]["graphics.force_16_by_9"], "manual")
        self.assertEqual(values["controller"]["scheme"], "xbox-series")
        self.assertEqual(settings.read_text(), text)
        self.assertEqual(source.read_text(), original)
        self.assertEqual(copied.read_text(), original)
        self.assertIn("# retain this", copied.with_name("settings.toml").read_text())

    def test_profile_precedence(self):
        build = self.root / "build"
        build.mkdir()
        config = dev.native_config("linux")
        config.mkdir(parents=True)
        pointer = config / "settings_dir.txt"
        pointer.write_text(str(self.root / "chosen"))
        (build / "rb_blitz_launcher.portable").touch()
        self.assertEqual(dev.profile_path("linux", build), self.root / "chosen/launcher.toml")
        with patch.dict(os.environ, {"RBBLITZ_CONTROLLER_SETTINGS_PROFILE": "environment.toml"}):
            self.assertEqual(dev.profile_path("linux", build), build / "environment.toml")
            self.assertEqual(dev.profile_path("linux", build, "explicit.toml"), build / "explicit.toml")
        pointer.unlink()
        self.assertEqual(dev.profile_path("linux", build), build / "launcher.toml")

    def test_dlc_native_precedence_and_relative_legacy(self):
        profile = self.root / "launcher.toml"
        library = self.root / "music with spaces"
        library.mkdir()
        profile.write_text("[settings]\ndlc_root = 'music with spaces'\n")
        expected = [f"--blitz.runtime.dlc_root={library}"]
        self.assertEqual(dev.dlc_arguments("linux", self.root, profile, []), expected)
        self.assertEqual(dev.dlc_arguments("linux", self.root, profile, ["--dlc_root=empty"]), [])
        with patch.dict(os.environ, {"REX_BLITZ_RUNTIME_DLC_LIBRARY": "explicit"}):
            self.assertEqual(dev.dlc_arguments("linux", self.root, profile, []), [])
        config = dev.native_config("linux")
        config.mkdir(parents=True)
        (config / "paths.toml").write_text("schema_version = 1\n[paths]\ndlc_root = '/unavailable-drive'\n")
        self.assertEqual(dev.dlc_arguments("linux", self.root, profile, []), [])

    def test_desktop_run_preserves_arguments_and_avoids_ui(self):
        build = self.root / "out/build/linux-amd64-release"
        build.mkdir(parents=True)
        (build / "rb_blitz").touch()
        with patch.object(dev.platform, "machine", return_value="x86_64"), patch.object(dev, "execute", return_value=0) as run:
            dev.run_game("linux", ["--portrait", "--no-dlc", "--normal", "--log.file_name=path with spaces;$(echo nope)"])
        args, cwd = run.call_args.args
        self.assertEqual(cwd, build)
        self.assertIn("--ui.fullscreen=false", args)
        self.assertIn("--blitz.compatibility.ultimate_mode=0", args)
        self.assertIn("--log.file_name=path with spaces;$(echo nope)", args)
        self.assertIn(f"--blitz.runtime.dlc_root={self.root / 'out/no-dlc'}", args)
        copied = Path(next(arg.split("=", 1)[1] for arg in args if arg.startswith("--blitz.runtime.launcher_profile=")))
        self.assertTrue(tomllib.loads(copied.with_name("settings.toml").read_text())["graphics"]["force_16_by_9"])

    def test_build_failure_prevents_packaging(self):
        with patch.object(dev.platform, "system", return_value="Linux"), patch.object(dev, "build_game", return_value=17), patch.object(dev, "execute") as execute:
            self.assertEqual(dev.main(["linux", "build-installer", "--version", "1.0.0"]), 17)
            execute.assert_not_called()

    def test_ios_uses_device_paths_and_never_host_game_directory(self):
        with patch.object(dev, "execute", return_value=0) as execute:
            dev.run_game("ios", ["--device", "device with spaces", "--normal", "--bundle-id", "test.app"])
        command = execute.call_args.args[0]
        self.assertEqual(command[:7], ["xcrun", "devicectl", "device", "process", "launch", "--device", "device with spaces"])
        self.assertIn("test.app", command)
        self.assertFalse(any("game_root" in arg or "launcher_profile" in arg for arg in command))
        with self.assertRaises(ValueError):
            dev.run_game("ios", [])

    def test_help_has_no_filesystem_or_process_effects(self):
        with patch.object(dev, "execute") as execute, contextlib.redirect_stdout(io.StringIO()):
            for target in ("linux", "mac", "ios"):
                self.assertEqual(dev.main([target, "run", "--help"]), 0)
            execute.assert_not_called()
        self.assertEqual(list(self.root.iterdir()), [])

    @unittest.skipUnless(BASH, "Bash is unavailable")
    def test_shell_detects_hosts_and_preserves_sourced_arguments(self):
        argument = '--log.file_name=space ; $(echo should-stay-literal)'
        script = '''
uname() { if [ "$1" = -s ]; then printf '%s' "$TEST_HOST"; else printf arm64; fi; }
source "$1"
printf '%s\\0' "$RBBLITZ_DEV_PLATFORM"
_rbblitz_dev() { printf '%s\\0' "$@"; }
run-game-portrait "$2"
'''
        for host, target in (("MINGW64_NT", "windows"), ("Linux", "linux"), ("Darwin", "mac")):
            env = dict(SHELL_ENV, TEST_HOST=host)
            env.pop("RBBLITZ_DEV_PLATFORM", None)
            result = subprocess.run([BASH, "-c", script, "dev-test", (REPOSITORY / "dev.sh").as_posix(), argument],
                                    env=env, capture_output=True, check=True)
            self.assertEqual(result.stdout.split(b"\0")[:-1], [target.encode(), b"run", b"--portrait", argument.encode()])

    @unittest.skipUnless(BASH, "Bash is unavailable")
    def test_shell_dispatch_propagates_exit_and_preserves_arguments(self):
        # Mock the platform executable; never launch a game or a build.
        script = '''
source "$1"
pwsh() { printf '%s\\0' "$@"; return 23; }
_rbblitz_dev run '--log.file_name=space ; $literal'
'''
        env = dict(SHELL_ENV, RBBLITZ_DEV_PLATFORM="windows")
        result = subprocess.run([BASH, "-c", script, "dev-test", (REPOSITORY / "dev.sh").as_posix()],
                                env=env, capture_output=True)
        self.assertEqual(result.returncode, 23)
        arguments = result.stdout.split(b"\0")[:-1]
        self.assertEqual(arguments[-2:], [b"run", b"--log.file_name=space ; $literal"])


if __name__ == "__main__":
    unittest.main()
