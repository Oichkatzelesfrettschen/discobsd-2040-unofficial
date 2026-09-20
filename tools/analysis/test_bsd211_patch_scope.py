"""Regression tests for the 2.11BSD patch-scope analyzer."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class PatchScopeTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name)
        self.repository = self.root / "upstream"
        self.repository.mkdir()
        self.run_git(self.repository, "init", "-q")
        self.run_git(self.repository, "config", "user.name", "Patch Scope Test")
        self.run_git(self.repository, "config", "user.email", "test@example.invalid")

        source = self.repository / "common.txt"
        source.write_text("before\n", encoding="utf-8")
        self.run_git(self.repository, "add", "common.txt")
        self.run_git(self.repository, "commit", "-q", "-m", "base")
        self.base = self.run_git(
            self.repository, "rev-parse", "HEAD"
        ).stdout.strip()

        source.write_text("after\n", encoding="utf-8")
        self.run_git(self.repository, "commit", "-q", "-am", "patch 432 repair")

        self.linked = self.root / "linked"
        self.run_git(
            self.repository,
            "worktree",
            "add",
            "-q",
            "--detach",
            str(self.linked),
            "HEAD",
        )
        self.tree = self.root / "tree"
        self.tree.mkdir()
        (self.tree / "common.txt").write_text("after\n", encoding="utf-8")
        self.script = Path(__file__).with_name("bsd211_patch_scope.py")

    @staticmethod
    def run_git(repository, *arguments):
        return subprocess.run(
            ["git", "-C", str(repository), *arguments],
            capture_output=True,
            text=True,
            check=True,
        )

    def run_analyzer(self, base):
        return subprocess.run(
            [
                sys.executable,
                str(self.script),
                "--bsd",
                str(self.linked),
                "--tree",
                str(self.tree),
                "--base",
                base,
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_linked_worktree_is_a_checkout(self):
        self.assertTrue((self.linked / ".git").is_file())
        result = self.run_analyzer(self.base)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = json.loads(result.stdout)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["patch"], 432)
        self.assertEqual(rows[0]["carried"][0][0], "common.txt")

    def test_invalid_base_reports_git_failure(self):
        invalid_base = "definitely-not-a-revision"
        result = self.run_analyzer(invalid_base)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn(
            f"cannot enumerate commits after {invalid_base}", result.stderr
        )
        self.assertIn(invalid_base, result.stderr)


if __name__ == "__main__":
    unittest.main()
