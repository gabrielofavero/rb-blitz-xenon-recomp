"""Verify exact replay, pin enforcement and edit preservation in nested dependencies."""
import difflib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/setup/apply_sdk_patches.py"
spec = importlib.util.spec_from_file_location("sdk_patches", SCRIPT)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class NestedPatchTests(unittest.TestCase):
    def test_nested_replay_and_audit(self):
        with tempfile.TemporaryDirectory(prefix="sdk-nested-patches-") as temporary:
            root = Path(temporary)
            sdk = root / "sdk"
            child = sdk / "thirdparty/dependency"
            child.mkdir(parents=True)
            patches = root / "patches"
            nested = patches / "thirdparty/dependency"
            nested.mkdir(parents=True)

            def git(directory, *args):
                return subprocess.run(["git", "-C", str(directory), *args], check=True,
                                      capture_output=True, text=True).stdout.strip()

            def commit(directory):
                git(directory, "add", ".")
                git(directory, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                    "commit", "-qm", "fixture")
                return git(directory, "rev-parse", "HEAD")

            def patch(path, before, after):
                return f"diff --git a/{path} b/{path}\n" + "".join(difflib.unified_diff(
                    before.splitlines(True), after.splitlines(True),
                    fromfile="a/" + path, tofile="b/" + path))

            git(child, "init", "-q")
            (child / "a.txt").write_text("original\n")
            (child / "untouched.txt").write_text("keep\n")
            commit(child)
            git(sdk, "init", "-q")
            (sdk / "a.txt").write_text("parent\n")
            pin = commit(sdk)
            (patches / "0001.patch").write_text(patch("a.txt", "parent\n", "patched parent\n"))
            (nested / "0001.patch").write_text(patch("a.txt", "original\n", "first\n"))
            (nested / "0002.patch").write_text(patch("a.txt", "first\n", "final\n"))

            with self.assertRaises(SystemExit):
                patcher.apply_patch_set(sdk, patches, pin, True)
            self.assertEqual((child / "a.txt").read_text(), "original\n")
            patcher.apply_patch_set(sdk, patches, pin, False)
            patcher.apply_patch_set(sdk, patches, pin, True)
            patcher.apply_patch_set(sdk, patches, pin, False)
            self.assertEqual((child / "a.txt").read_text(), "final\n")

            (child / "a.txt").write_text("user edit\n")
            with self.assertRaises(SystemExit):
                patcher.apply_patch_set(sdk, patches, pin, False)
            self.assertEqual((child / "a.txt").read_text(), "user edit\n")
            (child / "a.txt").write_text("final\n")
            (child / "untouched.txt").write_text("user edit\n")
            with self.assertRaises(SystemExit):
                patcher.apply_patch_set(sdk, patches, pin, True)
            (child / "untouched.txt").write_text("keep\n")

            commit(child)
            with self.assertRaisesRegex(SystemExit, "pinned commit"):
                patcher.apply_patch_set(sdk, patches, pin, True)


if __name__ == "__main__":
    unittest.main()
