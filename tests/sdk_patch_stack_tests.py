"""Exercise stacked SDK patch application and rejection in an isolated checkout."""
from pathlib import Path
import difflib
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/setup/apply_sdk_patches.ps1"


class PatchStackTests(unittest.TestCase):
    def test_replacement_is_idempotent_but_partial_or_dirty_stack_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="sdk-stack-") as directory:
            root = Path(directory)
            sdk = root / "rexglue-sdk"
            patches = root / "patches/rexglue-sdk"
            sdk.mkdir()
            patches.mkdir(parents=True)

            def git(*args):
                return subprocess.run(["git", "-C", str(sdk), *args], check=True,
                                      capture_output=True, text=True)

            def write(name, text):
                (sdk / name).write_text(text, newline="\n")

            def diff(name, before, after):
                return (f"diff --git a/{name} b/{name}\n" + "".join(
                    difflib.unified_diff(before.splitlines(keepends=True),
                                         after.splitlines(keepends=True),
                                         fromfile="a/" + name, tofile="b/" + name)))

            def run(*args):
                return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                       "-File", str(SCRIPT), "-RepoRoot", str(root), *args],
                                      capture_output=True, text=True)

            git("init", "--quiet")
            write("a.txt", "original a\n")
            write("b.txt", "original b\n")
            git("add", ".")
            git("-c", "user.name=Patch fixture", "-c", "user.email=fixture@example.invalid",
                "commit", "--quiet", "-m", "fixture")
            (patches / "0001.patch").write_text(
                diff("a.txt", "original a\n", "CPU conversion\n") +
                diff("b.txt", "original b\n", "first patch b\n"), newline="\n")
            (patches / "0002.patch").write_text(
                diff("a.txt", "CPU conversion\n", "GPU conversion\n"), newline="\n")

            applied = run()
            self.assertEqual(applied.returncode, 0, applied.stdout + applied.stderr)
            self.assertEqual((sdk / "a.txt").read_text(), "GPU conversion\n")
            for args in [("-Check",), ()]:
                repeated = run(*args)
                self.assertEqual(repeated.returncode, 0, repeated.stdout + repeated.stderr)
                self.assertIn("verified in final patch stack", repeated.stdout)

            write("b.txt", "unrelated edit\n")
            dirty = run("-Check")
            self.assertNotEqual(dirty.returncode, 0, dirty.stdout)
            self.assertEqual((sdk / "b.txt").read_text(), "unrelated edit\n")
            # This path disappears from git status; every failed patch path must
            # still be compared with the final replay rather than status alone.
            git("restore", "b.txt")
            partial = run("-Check")
            self.assertNotEqual(partial.returncode, 0, partial.stdout)
            self.assertNotIn("verified in final patch stack", partial.stdout)


if __name__ == "__main__":
    unittest.main()
