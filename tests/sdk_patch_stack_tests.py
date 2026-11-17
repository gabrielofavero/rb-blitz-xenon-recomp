"""Exercise production SDK appliers against disposable, game-data-free gitlinks."""
from pathlib import Path
import difflib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StackCases:
    shell = None

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="sdk-stack-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sdk = self.root / "rexglue-sdk"
        self.patches = self.root / "patches/rexglue-sdk"
        self.sdk.mkdir()
        self.patches.mkdir(parents=True)
        self.git(self.sdk, "init", "-q")
        self.git(self.sdk, "config", "core.autocrlf", "false")
        self.write("a.txt", b"original a\n")
        self.write("b.txt", b"original b\n")
        self.write("untouched.txt", b"original untouched\n")
        self.git(self.sdk, "add", ".")
        self.commit(self.sdk)
        pin = self.git(self.sdk, "rev-parse", "HEAD").stdout.strip()
        self.git(self.root, "init", "-q")
        self.git(self.root, "update-index", "--add", "--cacheinfo",
                 "160000", pin, "rexglue-sdk")
        self.commit(self.root)
        (self.patches / "0001.patch").write_text(
            self.diff("a.txt", "original a\n", "CPU conversion\n") +
            self.diff("b.txt", "original b\n", "first patch b\n"), newline="\n")
        (self.patches / "0002.patch").write_text(
            self.diff("a.txt", "CPU conversion\n", "GPU conversion\n") +
            self.diff("new.txt", "", "new original\n", new=True), newline="\n")
        (self.patches / "0003.patch").write_text(
            self.diff("new.txt", "new original\n", "new final\n"), newline="\n")

    @staticmethod
    def git(root, *args):
        return subprocess.run(["git", "-C", str(root), *args], check=True,
                              capture_output=True, text=True)

    def commit(self, root):
        self.git(root, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "fixture")

    def write(self, name, data):
        (self.sdk / name).write_bytes(data)

    @staticmethod
    def diff(name, before, after, new=False):
        return (f"diff --git a/{name} b/{name}\n" + ("new file mode 100644\n" if new else "") +
                "".join(difflib.unified_diff(
            before.splitlines(True), after.splitlines(True),
            fromfile="/dev/null" if new else "a/" + name, tofile="b/" + name)))

    def run_applier(self, check=False):
        if self.shell:
            command = [self.shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                       str(ROOT / "scripts/setup/apply_sdk_patches.ps1"),
                       "-RepoRoot", str(self.root)]
            if check:
                command.append("-Check")
        else:
            command = [sys.executable, str(ROOT / "scripts/setup/apply_sdk_patches.py"),
                       "--repo-root", str(self.root)]
            if check:
                command.append("--check")
        return subprocess.run(command, capture_output=True, text=True)

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_failure(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_replacement_new_file_and_idempotence(self):
        self.assert_failure(self.run_applier(check=True))
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"original a\n")
        self.assertFalse((self.sdk / "new.txt").exists())
        self.assert_success(self.run_applier())
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"GPU conversion\n")
        self.assertEqual((self.sdk / "new.txt").read_bytes(), b"new final\n")
        self.assert_success(self.run_applier(check=True))
        self.assert_success(self.run_applier())

    def test_conflicting_edit_prevents_all_writes(self):
        self.write("b.txt", b"local b\n")
        self.assert_failure(self.run_applier())
        self.assertEqual((self.sdk / "b.txt").read_bytes(), b"local b\n")
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"original a\n")
        self.assertFalse((self.sdk / "new.txt").exists())

    def test_incomplete_path_is_rejected_whole_pristine_file_is_repairable(self):
        self.assert_success(self.run_applier())
        self.write("a.txt", b"CPU conversion\n")
        self.assert_failure(self.run_applier(check=True))
        self.assert_failure(self.run_applier())
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"CPU conversion\n")
        self.write("a.txt", b"original a\n")
        self.assert_failure(self.run_applier(check=True))
        self.assert_success(self.run_applier())

    def test_new_file_conflict_is_preserved(self):
        self.write("new.txt", b"user file\n")
        self.assert_failure(self.run_applier())
        self.assertEqual((self.sdk / "new.txt").read_bytes(), b"user file\n")

    def test_deleted_file_replay_and_repeated_audit(self):
        (self.patches / "0004.patch").write_text(
            "diff --git a/b.txt b/b.txt\ndeleted file mode 100644\n" +
            "".join(difflib.unified_diff(["first patch b\n"], [],
                                        fromfile="a/b.txt", tofile="/dev/null")), newline="\n")
        self.assert_success(self.run_applier())
        self.assertFalse((self.sdk / "b.txt").exists())
        self.assert_success(self.run_applier(check=True))

    def test_tracked_edits_outside_patch_coverage_are_preserved(self):
        self.write("untouched.txt", b"user edit\n")
        self.assert_failure(self.run_applier())
        self.assertEqual((self.sdk / "untouched.txt").read_bytes(), b"user edit\n")
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"original a\n")

    def test_untracked_policy(self):
        self.write("unrelated.txt", b"keep\n")
        result = self.run_applier()
        (self.assert_success if self.shell else self.assert_failure)(result)
        self.assertEqual((self.sdk / "unrelated.txt").read_bytes(), b"keep\n")

    def test_crlf_policy(self):
        self.assert_success(self.run_applier())
        for name in ("a.txt", "b.txt", "new.txt"):
            self.write(name, (self.sdk / name).read_bytes().replace(b"\n", b"\r\n"))
        result = self.run_applier(check=True)
        (self.assert_success if self.shell else self.assert_failure)(result)
        self.assertEqual((self.sdk / "new.txt").read_bytes(), b"new final\r\n")

    def test_pristine_crlf_policy_with_checkout_conversion_enabled(self):
        self.git(self.sdk, "config", "core.autocrlf", "true")
        self.write("a.txt", b"original a\r\n")
        self.write("b.txt", b"original b\r\n")
        result = self.run_applier()
        (self.assert_success if self.shell else self.assert_failure)(result)
        if self.shell:
            self.assertEqual((self.sdk / "a.txt").read_bytes(), b"GPU conversion\n")
            self.assert_success(self.run_applier(check=True))
        else:
            self.assertEqual((self.sdk / "a.txt").read_bytes(), b"original a\r\n")

    def test_pin_mismatch_is_rejected(self):
        self.write("untouched.txt", b"different commit\n")
        self.git(self.sdk, "add", ".")
        self.commit(self.sdk)
        self.assert_failure(self.run_applier())
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"original a\n")

    def test_replay_failure_is_detected_before_writes(self):
        (self.patches / "0003.patch").write_text(
            self.diff("new.txt", "wrong base\n", "replacement\n"), newline="\n")
        self.assert_failure(self.run_applier())
        self.assertEqual((self.sdk / "a.txt").read_bytes(), b"original a\n")


class PythonStackTests(StackCases, unittest.TestCase):
    pass


@unittest.skipUnless(shutil.which("powershell"), "Windows PowerShell unavailable")
class WindowsPowerShellStackTests(StackCases, unittest.TestCase):
    shell = "powershell"


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell Core unavailable")
class PowerShellCoreStackTests(StackCases, unittest.TestCase):
    shell = "pwsh"


if __name__ == "__main__":
    unittest.main()
