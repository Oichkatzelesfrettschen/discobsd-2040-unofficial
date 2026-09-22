"""Calibrate pinned textual inventory without granting semantic verdicts."""

import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import bsd211_patch_scope as mapper


class PatchScopeTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-patch-scope-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.donor = self.root / "donor"
        self.recipient = self.root / "recipient"
        for repository in (self.donor, self.recipient):
            repository.mkdir()
            self.git(repository, "init", "-q")
            self.git(repository, "config", "user.name", "Fixture")
            self.git(repository, "config", "user.email", "fixture@example.invalid")
            self.git(repository, "config", "commit.gpgsign", "false")
            self.git(repository, "config", "core.hooksPath", "/dev/null")
            self.git(repository, "config", "core.excludesFile", "/dev/null")
        self.write(self.donor, "common.c", "before\n")
        self.base = self.commit(self.donor, "base")
        self.write(self.donor, "common.c", "after\n")
        self.child = self.commit(self.donor, "repair (Patch 475)")
        self.write(self.recipient, "common.c", "after\n")
        self.recipient_commit = self.commit(self.recipient, "recipient")
        self.mapping = self.root / "mapping.json"
        self.mapping.write_text("[]\n")
        mapper.tree_entries.cache_clear()
        mapper.blob.cache_clear()

    @staticmethod
    def git(repository, *arguments):
        return subprocess.run(["git", "-C", str(repository), *arguments],
                              capture_output=True, text=True, check=True).stdout.strip()

    @staticmethod
    def write(repository, name, contents):
        path = repository / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)

    def commit(self, repository, subject):
        self.git(repository, "add", "-A")
        self.git(repository, "commit", "-qm", subject)
        return self.git(repository, "rev-parse", "HEAD")

    @staticmethod
    def object_command(repository, arguments, contents):
        result = subprocess.run(["git", "-C", str(repository), *arguments],
                                input=contents, capture_output=True, check=True)
        return result.stdout.decode().strip()

    def tree_commit(self, repository, entries):
        records = b"".join(f"{mode} {kind} {object_id}\t".encode() + os.fsencode(name) + b"\0"
                           for name, (mode, kind, object_id) in entries.items())
        tree = self.object_command(repository, ["mktree", "-z"], records)
        commit = self.git(repository, "commit-tree", tree, "-p",
                          self.git(repository, "rev-parse", "HEAD"), "-m", "tree fixture")
        self.git(repository, "update-ref", "HEAD", commit)
        return commit

    def report(self, **options):
        return mapper.scope(self.donor, self.recipient, self.base,
                            relocations=self.mapping, **options)

    @staticmethod
    def candidates(report):
        return [row for commit in report["commits"] for comparison in commit["comparisons"]
                for row in comparison["textual_candidates"]]

    def test_linked_worktree_and_pinned_identity(self):
        linked = self.root / "linked"
        self.git(self.donor, "worktree", "add", "-q", "--detach", str(linked), self.child)
        report = mapper.scope(linked, self.recipient, self.base, relocations=self.mapping)
        self.assertEqual(report["donor_commit"], self.child)
        self.assertEqual(report["recipient_commit"], self.recipient_commit)
        self.assertEqual(report["commits"][0]["patch"], 475)
        self.assertEqual(self.candidates(report)[0]["textual_relation"], "closer-to-child")

    def test_dirty_recipient_and_donor_do_not_change_report(self):
        expected = self.report()
        self.write(self.recipient, "common.c", "arbitrary dirty content\n")
        self.write(self.donor, "common.c", "different dirty content\n")
        self.assertEqual(self.report(), expected)

    def test_refs_are_resolved_once_before_comparison(self):
        original = mapper.resolve

        def advance_after_resolving(repository, revision):
            resolved = original(repository, revision)
            if repository == str(self.recipient) and revision == "HEAD":
                self.write(self.recipient, "common.c", "later\n")
                self.commit(self.recipient, "later recipient")
                self.write(self.donor, "unrelated.c", "later donor\n")
                self.commit(self.donor, "patch 499 later")
            return resolved

        with patch.object(mapper, "resolve", side_effect=advance_after_resolving):
            report = self.report()
        self.assertEqual(report["recipient_commit"], self.recipient_commit)
        self.assertEqual(report["donor_commit"], self.child)
        self.assertEqual(len(report["commits"]), 1)
        self.assertEqual(self.candidates(report)[0]["textual_relation"], "closer-to-child")

    def test_relocated_path_and_symbol_hints(self):
        self.git(self.recipient, "mv", "common.c", "relocated.c")
        self.commit(self.recipient, "relocate")
        self.mapping.write_text(json.dumps([{
            "donor_path": "common.c", "recipient_path": "relocated.c",
            "symbols": [{"donor": "old_function", "recipient": "new_function"}],
        }]))
        report = self.report()
        row = self.candidates(report)[0]
        self.assertEqual(row["recipient_path"], "relocated.c")
        self.assertEqual(row["mapping"], "explicit")
        self.assertEqual(row["symbol_hints"][0]["recipient"], "new_function")
        self.assertEqual(len(report["relocations_sha256"]), 64)

    def test_absent_recipient_path_is_retained(self):
        self.write(self.donor, "absent.c", "new file\n")
        self.commit(self.donor, "patch 476 addition")
        rows = self.candidates(self.report())
        row = next(row for row in rows if row["donor_path"] == "absent.c")
        self.assertEqual(row["textual_relation"], "uncompared")
        self.assertIn("recipient path absent", row["reason"])

    def test_equidistant_text_remains_a_textual_candidate(self):
        self.write(self.recipient, "common.c", "different\n")
        self.commit(self.recipient, "equidistant")
        row = self.candidates(self.report())[0]
        self.assertEqual(row["textual_relation"], "equidistant")
        self.assertEqual(row["parent_distance"], row["child_distance"])

    def test_filename_bytes_survive_nul_delimited_inventory(self):
        names = ["name with spaces.c", "name\twith\ncontrols.c", os.fsdecode(b"non-utf8-\xff.c")]
        # Git trees retain filename bytes even when the host filesystem cannot
        # create those names. Pinned comparisons require no checkout of them.
        before = self.object_command(self.donor, ["hash-object", "-w", "--stdin"], b"before\n")
        after = self.object_command(self.donor, ["hash-object", "-w", "--stdin"], b"after\n")
        local = self.object_command(self.recipient, ["hash-object", "-w", "--stdin"], b"after\n")
        start = self.tree_commit(self.donor, {name: ("100644", "blob", before) for name in names})
        self.tree_commit(self.donor, {name: ("100644", "blob", after) for name in names})
        self.tree_commit(self.recipient, {name: ("100644", "blob", local) for name in names})
        report = mapper.scope(self.donor, self.recipient, start, relocations=self.mapping)
        roundtrip = json.loads(json.dumps(report))
        self.assertCountEqual([row["donor_path"] for row in self.candidates(roundtrip)], names)

    def test_ignored_gitlink_change_remains_in_inventory(self):
        start = self.tree_commit(self.donor, {"module": ("160000", "commit", self.base)})
        self.tree_commit(self.donor, {"module": ("160000", "commit", self.child)})
        self.tree_commit(self.recipient, {"module": ("160000", "commit", self.recipient_commit)})
        self.git(self.donor, "config", "diff.ignoreSubmodules", "all")
        report = mapper.scope(self.donor, self.recipient, start, relocations=self.mapping)
        rows = self.candidates(report)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["donor_path"], "module")
        self.assertEqual(rows[0]["reason"], "nonregular tree entry")

    def test_additions_deletions_binary_and_symlinks_are_explicit(self):
        self.write(self.donor, "deleted.c", "before\n")
        self.write(self.donor, "binary", "before\0bytes")
        (self.donor / "link").symlink_to("common.c")
        start = self.commit(self.donor, "special bases")
        (self.donor / "deleted.c").unlink()
        self.write(self.donor, "binary", "after\0bytes")
        (self.donor / "link").unlink()
        (self.donor / "link").symlink_to("binary")
        self.write(self.donor, "added.c", "added\n")
        self.commit(self.donor, "patch 478 special entries")
        for name in ("deleted.c", "binary", "link", "added.c"):
            self.write(self.recipient, name, "recipient\n")
        self.commit(self.recipient, "special recipient")
        rows = self.candidates(mapper.scope(self.donor, self.recipient, start,
                                           relocations=self.mapping))
        self.assertEqual(len(rows), 4)
        self.assertEqual({row["reason"] for row in rows},
                         {"donor addition or deletion", "binary or non-UTF-8 content",
                          "nonregular tree entry"})

    def test_git_failure_is_distinct_from_absence(self):
        original = mapper.git
        for operation in ("diff", "cat-file", "ls-tree"):
            mapper.tree_entries.cache_clear()
            mapper.blob.cache_clear()

            def failing_git(repository, *arguments, failing_operation=operation):
                if arguments[0] == failing_operation:
                    raise mapper.GitError("injected Git failure")
                return original(repository, *arguments)

            with patch.object(mapper, "git", side_effect=failing_git):
                with self.assertRaisesRegex(mapper.GitError, "injected Git failure"):
                    self.report()

    def test_patch_subject_formats(self):
        for subject in ("patch 475 fix", "fix (Patch 475)", "fix (#475)",
                        "475: fix", "475 - fix", "PATCH #475 fix"):
            self.assertEqual(mapper.patch_number(subject), 475)
        self.assertIsNone(mapper.patch_number("unidentified reconstruction"))

    def test_multiple_roots_and_merge_parents_are_explicit(self):
        self.git(self.donor, "checkout", "--orphan", "independent")
        self.write(self.donor, "other.c", "other\n")
        independent = self.commit(self.donor, "independent root")
        self.git(self.donor, "checkout", "--detach", self.child)
        self.git(self.donor, "merge", "--allow-unrelated-histories", "--no-edit", independent)
        with self.assertRaisesRegex(ValueError, "multiple roots"):
            mapper.scope(self.donor, self.recipient, relocations=self.mapping)
        report = self.report()
        merged = report["commits"][-1]
        self.assertTrue(merged["merge"])
        self.assertEqual(len(merged["comparisons"]), 2)
        root = next(item for item in report["commits"] if item["commit"] == independent)
        self.assertIsNone(root["comparisons"][0]["parent"])
        self.assertGreater(root["comparisons"][0]["touched_paths"], 0)

    def test_unrelated_ref_is_outside_pinned_inventory(self):
        self.git(self.donor, "checkout", "--orphan", "unrelated")
        self.write(self.donor, "unrelated.c", "other\n")
        unrelated = self.commit(self.donor, "patch 500 unrelated")
        self.git(self.donor, "checkout", "--detach", self.child)
        self.assertEqual(len(self.report()["commits"]), 1)
        with self.assertRaisesRegex(ValueError, "outside the pinned donor ancestry"):
            mapper.scope(self.donor, self.recipient, unrelated, relocations=self.mapping)

    def test_textual_distance_cannot_decide_semantic_coverage(self):
        parent = "/* original */\nint fix(void) { return 0; }\n"
        comments = "".join(f"/* comment {number} */\n" for number in range(20))
        child = comments + "int fix(void) { return 1; }\n"
        self.write(self.donor, "common.c", parent)
        start = self.commit(self.donor, "semantic baseline")
        self.write(self.donor, "common.c", child)
        self.commit(self.donor, "patch 479 correction")
        for local, relation in (
            (comments + "int fix(void) { return 0; }\n", "closer-to-child"),
            ("/* original */\nint fix(void) { return !!7; }\n", "closer-to-parent"),
        ):
            self.write(self.recipient, "common.c", local)
            self.commit(self.recipient, relation)
            report = mapper.scope(self.donor, self.recipient, start, relocations=self.mapping)
            row = self.candidates(report)[0]
            self.assertEqual(row["textual_relation"], relation)
            self.assertNotIn("disposition", row)
            rendered = io.StringIO()
            mapper.as_table(report, rendered)
            self.assertIn("textual candidates only", rendered.getvalue())
            for forbidden in ("carried", "applicable", "missing here", "needs no action"):
                self.assertNotIn(forbidden, rendered.getvalue())

    def test_invalid_revision_cli_reports_error(self):
        result = subprocess.run(
            [sys.executable, str(Path(mapper.__file__)), "--bsd", str(self.donor),
             "--tree", str(self.recipient), "--base", "invalid-base"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("ERROR patch-scope: git rev-parse", result.stderr)

    def test_invalid_relocation_is_rejected(self):
        for contents in ("{}", '[{"donor_path":"common.c"}]', json.dumps([{
            "donor_path": "common.c", "recipient_path": "../outside", "symbols": [],
        }])):
            self.mapping.write_text(contents)
            with self.assertRaises(ValueError):
                self.report()


if __name__ == "__main__":
    unittest.main()
