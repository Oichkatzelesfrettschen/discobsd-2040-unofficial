"""Calibrate the repository instruction graph against contents and index modes."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import check_agent_instructions as policy


class InstructionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-instructions-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout"
        self.root.mkdir()
        self.run_git("init", "-q")
        self.run_git("config", "core.excludesFile", "/dev/null")
        self.write("AGENTS.md", "# Canonical policy\n"
                   "Read `docs/research/STYLE-GUIDE.md` as a proposal.\n")
        self.write("CLAUDE.md", "@AGENTS.md\n")
        self.run_git("add", "AGENTS.md", "CLAUDE.md")

    def write(self, name, contents):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)

    def run_git(self, *arguments, input=None):
        return subprocess.run(["git", "-C", str(self.root), *arguments],
                              input=input, capture_output=True, check=True).stdout

    def test_exact_regular_pair(self):
        policy.check(self.root)
        policy.check(self.root, staged=True)

    def test_wrapper_contents(self):
        for contents in ("", "@missing.md\n", "@AGENTS.md", "@AGENTS.md\n\n",
                         "@AGENTS.md\nMore instructions\n"):
            with self.subTest(contents=contents):
                self.write("CLAUDE.md", contents)
                with self.assertRaisesRegex(policy.PolicyError, "expected exactly"):
                    policy.check(self.root)

    def test_missing_files(self):
        for name in policy.POLICY_FILES:
            path = self.root / name
            contents = path.read_bytes()
            path.unlink()
            with self.assertRaisesRegex(policy.PolicyError, "missing instruction file"):
                policy.check(self.root)
            path.write_bytes(contents)

    def test_symlink_modes_on_both_files(self):
        for name in sorted(policy.POLICY_FILES):
            with self.subTest(name=name):
                path = self.root / name
                contents = path.read_bytes()
                self.write("saved-policy", contents.decode())
                path.unlink()
                path.symlink_to("saved-policy")
                with self.assertRaisesRegex(policy.PolicyError, "regular non-executable"):
                    policy.check(self.root)
                self.run_git("add", name)
                with self.assertRaisesRegex(policy.PolicyError, "regular mode 100644"):
                    policy.check(self.root, staged=True)
                path.unlink()
                path.write_bytes(contents)
                self.run_git("add", name)

    def test_executable_instruction_is_rejected(self):
        (self.root / "CLAUDE.md").chmod(0o755)
        with self.assertRaisesRegex(policy.PolicyError, "regular non-executable"):
            policy.check(self.root)
        self.run_git("add", "CLAUDE.md")
        with self.assertRaisesRegex(policy.PolicyError, "regular mode 100644"):
            policy.check(self.root, staged=True)

    def test_broken_cyclic_escaping_private_and_proposal_edges(self):
        self.write("helper.md", "@AGENTS.md\n")
        for target in ("missing.md", "AGENTS.md", "CLAUDE.md", "helper.md",
                       "../outside.md", "~/.private-policy.md", "/home/example/policy.md",
                       "./docs/research/STYLE-GUIDE.md"):
            with self.subTest(target=target):
                self.write("AGENTS.md", f"Read @{target} before editing.\n")
                with self.assertRaisesRegex(policy.PolicyError, "forbidden active import"):
                    policy.check(self.root)

    def test_literal_references_and_fenced_examples(self):
        self.write("AGENTS.md", "# Policy\n`@docs/research/STYLE-GUIDE.md`\n"
                   "``literal ` @~/private.md``\n"
                   "```text\n@missing.md\n```\n"
                   "~~~text\n@AGENTS.md\n~~~~\n"
                   "Contact maintainer@example.invalid.\n")
        policy.check(self.root)

    def test_multiline_spans_and_import_after_fence(self):
        self.write("AGENTS.md", "# Policy\n`literal\n@~/private.md`\n")
        policy.check(self.root)
        self.write("AGENTS.md", "# Policy\n```\n@literal.md\n```\n@missing.md\n")
        with self.assertRaisesRegex(policy.PolicyError, "AGENTS.md:5:"):
            policy.check(self.root)

    def test_invalid_backtick_fence_cannot_hide_import(self):
        self.write("AGENTS.md", "```bad`\n@~/private.md\n")
        with self.assertRaisesRegex(policy.PolicyError, "fence info string"):
            policy.check(self.root)

    def test_escaped_ticks_and_separate_blocks_cannot_hide_imports(self):
        for contents in (
            "Escaped \\` delimiter @~/private.md \\` end.\n",
            "An unmatched ` delimiter.\n\n@~/private.md\n\nA later ` delimiter.\n",
            "An unmatched ` delimiter.\n# @~/private.md ` heading\n",
            "An unmatched ` delimiter.\n- @~/private.md ` list item\n",
            "prefix`literal`@~/private.md\n",
        ):
            with self.subTest(contents=contents):
                self.write("AGENTS.md", contents)
                with self.assertRaisesRegex(policy.PolicyError, "forbidden active import"):
                    policy.check(self.root)

    def test_valid_index_invalid_working_copy(self):
        self.write("AGENTS.md", "@~/private.md\n")
        self.write("CLAUDE.md", "wrong wrapper\n")
        policy.check(self.root, staged=True)
        with self.assertRaises(policy.PolicyError):
            policy.check(self.root)

    def test_thematic_breaks_and_setext_headings_end_spans(self):
        for separator in ("***", "---", "___", "* * *", "  - - -  ", "_\t_\t_", "==="):
            with self.subTest(separator=separator):
                self.write("AGENTS.md", "An unmatched ` delimiter.\n"
                           f"{separator}\n@~/private.md\nA later ` delimiter.\n")
                self.run_git("add", "AGENTS.md")
                for staged in (False, True):
                    with self.assertRaisesRegex(policy.PolicyError, "forbidden active import"):
                        policy.check(self.root, staged=staged)

    def test_tracked_rule_directory_links_are_rejected(self):
        for name in (".claude", ".claude/rules", ".claude/rules/shared",
                     "nested/.claude/rules"):
            with self.subTest(name=name):
                path = self.root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to("/outside-policy")
                self.run_git("add", name)
                for staged in (False, True):
                    with self.assertRaisesRegex(policy.PolicyError, "outside the allowed graph"):
                        policy.check(self.root, staged=staged)
                self.run_git("update-index", "--force-remove", name)
                path.unlink()

    def test_indented_and_html_blocks_end_inline_spans(self):
        for opener in ("    An unmatched `", "\tAn unmatched `", " \tAn unmatched `",
                       "   \tAn unmatched `", "<div>An unmatched `"):
            with self.subTest(opener=opener):
                self.write("AGENTS.md", f"{opener}\n@~/private.md\nA later ` delimiter.\n")
                self.run_git("add", "AGENTS.md")
                for staged in (False, True):
                    with self.assertRaisesRegex(policy.PolicyError, "forbidden active import"):
                        policy.check(self.root, staged=staged)

    def test_case_folded_instruction_entries_are_rejected(self):
        for name in (".CLAUDE/rules/policy.md", ".claude/RULES/policy.md",
                     "nested/agents.MD", "nested/Claude.Local.md"):
            with self.subTest(name=name):
                self.write(name, "@~/private.md\n")
                self.run_git("add", name)
                for staged in (False, True):
                    with self.assertRaisesRegex(policy.PolicyError, "outside the allowed graph"):
                        policy.check(self.root, staged=staged)
                self.run_git("update-index", "--force-remove", name)

    def test_autocrlf_checkout_keeps_wrapper_lf(self):
        attributes = (Path(policy.__file__).resolve().parent.parent / ".gitattributes").read_text()
        self.write(".gitattributes", attributes)
        self.run_git("add", ".gitattributes")
        self.run_git("config", "core.autocrlf", "true")
        (self.root / "CLAUDE.md").unlink()
        self.run_git("checkout-index", "-f", "CLAUDE.md")
        self.assertEqual((self.root / "CLAUDE.md").read_bytes(), policy.WRAPPER)
        policy.check(self.root)
        policy.check(self.root, staged=True)

    def test_invalid_index_valid_working_copy(self):
        for name, invalid in (("AGENTS.md", "@~/private.md\n"),
                              ("CLAUDE.md", "wrong wrapper\n")):
            with self.subTest(name=name):
                original = (self.root / name).read_text()
                self.write(name, invalid)
                self.run_git("add", name)
                self.write(name, original)
                policy.check(self.root)
                with self.assertRaises(policy.PolicyError):
                    policy.check(self.root, staged=True)
                self.run_git("add", name)

    def test_staged_mode_ignores_working_symlink(self):
        (self.root / "CLAUDE.md").unlink()
        (self.root / "CLAUDE.md").symlink_to("AGENTS.md")
        policy.check(self.root, staged=True)

    def test_missing_index_entry(self):
        self.run_git("update-index", "--force-remove", "CLAUDE.md")
        policy.check(self.root)
        with self.assertRaisesRegex(policy.PolicyError, "missing staged"):
            policy.check(self.root, staged=True)

    def test_unresolved_index_is_infrastructure_error(self):
        object_id = self.run_git("rev-parse", ":AGENTS.md").decode().strip()
        self.run_git("update-index", "--force-remove", "AGENTS.md")
        self.run_git("update-index", "--index-info",
                     input=f"100644 {object_id} 1\tAGENTS.md\n".encode())
        with self.assertRaisesRegex(policy.InfrastructureError, "unresolved index stage"):
            policy.check(self.root, staged=True)

    def test_missing_git_blob_is_infrastructure_error(self):
        object_length = len(self.run_git("rev-parse", ":AGENTS.md").strip())
        self.run_git("update-index", "--info-only", "--cacheinfo",
                     "100644," + "1" * object_length + ",AGENTS.md")
        with self.assertRaises(policy.InfrastructureError):
            policy.check(self.root, staged=True)

    def test_sha256_index_and_unreadable_blob(self):
        self.root = Path(self.temporary.name) / "sha256-checkout"
        self.root.mkdir()
        self.run_git("init", "-q", "--object-format=sha256")
        self.run_git("config", "core.excludesFile", "/dev/null")
        self.write("AGENTS.md", "# Canonical policy\n")
        self.write("CLAUDE.md", "@AGENTS.md\n")
        self.run_git("add", "AGENTS.md", "CLAUDE.md")
        policy.check(self.root, staged=True)
        self.test_missing_git_blob_is_infrastructure_error()

    def test_additional_tracked_instruction_entry_is_rejected(self):
        for name in ("sub/AGENTS.md", ".claude/rules/policy.md", "AGENTS.override.md"):
            with self.subTest(name=name):
                self.write(name, "@~/private.md\n")
                self.run_git("add", name)
                with self.assertRaisesRegex(policy.PolicyError, "outside the allowed graph"):
                    policy.check(self.root, staged=True)
                self.run_git("update-index", "--force-remove", name)

    def test_empty_or_invalid_utf8_canonical_policy(self):
        for contents in (b" \n", b"\xff"):
            (self.root / "AGENTS.md").write_bytes(contents)
            with self.assertRaises(policy.PolicyError):
                policy.check(self.root)

    def test_subdirectory_and_linked_worktree(self):
        self.run_git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                     "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null",
                     "commit", "-qm", "fixture")
        linked = Path(self.temporary.name) / "linked"
        self.run_git("worktree", "add", "--detach", str(linked), "HEAD")
        policy.check(linked, staged=True)
        subdirectory = linked / "subdirectory"
        subdirectory.mkdir()
        result = subprocess.run(
            [sys.executable, str(Path(policy.__file__)), "--root", str(linked), "--staged"],
            cwd=subdirectory, capture_output=True, text=True, check=True,
        )
        self.assertIn("PASS agent-instructions: index modes and blobs", result.stdout)

    def test_cli_distinguishes_policy_and_infrastructure_failures(self):
        for directory, expected in ((self.root, 1), (Path(self.temporary.name), 2)):
            self.write("CLAUDE.md", "invalid\n")
            environment = os.environ.copy()
            environment["GIT_CEILING_DIRECTORIES"] = self.temporary.name
            result = subprocess.run(
                [sys.executable, str(Path(policy.__file__)), "--root", str(directory)],
                env=environment, capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, expected, result.stderr)


if __name__ == "__main__":
    unittest.main()
