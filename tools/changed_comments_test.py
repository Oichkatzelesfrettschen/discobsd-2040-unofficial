"""Calibrate changed-comment selection and narration diagnostics."""

import io
import os
import random
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import check_changed_comments as checker


class GitFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-comments-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout"
        self.root.mkdir()
        self.git("init", "-q")
        self.git("config", "core.excludesFile", "/dev/null")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "core.hooksPath", "/dev/null")
        self.git("config", "core.autocrlf", "false")
        self.write("sample.c", "int baseline;\n")
        self.git("add", "sample.c")
        self.git("commit", "-qm", "baseline")
        self.base = self.git("rev-parse", "HEAD").decode().strip()

    def git(self, *arguments, input=None, check=True):
        command = [b"git", b"-C", os.fsencode(self.root)]
        command.extend(os.fsencode(argument) if isinstance(argument, str) else argument
                       for argument in arguments)
        return subprocess.run(command, input=input, capture_output=True,
                              check=check).stdout

    def write(self, name, contents):
        raw_name = os.fsencode(name) if isinstance(name, str) else name
        path = os.path.join(os.fsencode(self.root), raw_name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        payload = contents.encode() if isinstance(contents, str) else contents
        with open(path, "wb") as output:
            output.write(payload)

    def remove(self, name):
        raw_name = os.fsencode(name) if isinstance(name, str) else name
        os.unlink(os.path.join(os.fsencode(self.root), raw_name))

    def stage(self, *names):
        self.git("add", "--", *names)

    def check_mode(self, mode, **arguments):
        return checker.check(self.root, mode, self.base, **arguments)


class SnapshotTests(GitFixture):
    def resolve_range(self, event_base, event_head, default_ref):
        result = subprocess.run(
            ["sh", str(checker.ROOT / "tools/resolve-changed-comment-range.sh"),
             str(self.root), event_base, event_head, default_ref],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip().split()

    def test_staged_bad_working_repaired(self):
        self.write("sample.c", "/* in this PR */\nint baseline;\n")
        self.stage("sample.c")
        self.write("sample.c", "/* execution ordering invariant */\nint baseline;\n")

        staged = self.check_mode("staged")
        working = self.check_mode("working")

        self.assertEqual([item.rule_id for item in staged.diagnostics],
                         ["CH001_IN_THIS_PR"])
        self.assertEqual(working.diagnostics, ())
        self.assertEqual(working.changed_comments, 1)

    def test_staged_good_working_bad(self):
        self.write("sample.c", "/* execution ordering invariant */\nint baseline;\n")
        self.stage("sample.c")
        self.write("sample.c", "/* the reviewer requested a special case */\nint baseline;\n")

        staged = self.check_mode("staged")
        working = self.check_mode("working")

        self.assertEqual(staged.diagnostics, ())
        self.assertEqual([item.rule_id for item in working.diagnostics],
                         ["CH004_REVIEWER_REQUEST"])

    def test_revision_uses_explicit_commits(self):
        self.write("sample.c", "/* after this commit the path changes */\n")
        self.stage("sample.c")
        self.git("commit", "-qm", "bad comment")
        revision = self.git("rev-parse", "HEAD").decode().strip()

        result = checker.check(self.root, "revision", self.base, revision)

        self.assertEqual(result.after, revision)
        self.assertEqual([item.rule_id for item in result.diagnostics],
                         ["CH003_AFTER_THIS_COMMIT"])

    def test_initial_branch_range_covers_every_commit_since_fork(self):
        default_branch = self.git("branch", "--show-current").decode().strip()
        self.git("checkout", "-qb", "feature", self.base)
        self.write("sample.c", "/* in this PR */\nint baseline;\n")
        self.stage("sample.c")
        self.git("commit", "-qm", "introduce bad comment")
        self.write("note.txt", "second commit\n")
        self.stage("note.txt")
        self.git("commit", "-qm", "follow-up")
        head = self.git("rev-parse", "HEAD").decode().strip()
        zero = "0" * len(head)

        for event_base in ("", zero):
            with self.subTest(event_base=event_base):
                base, resolved_head = self.resolve_range(
                    event_base, head, f"refs/heads/{default_branch}"
                )
                result = checker.check(self.root, "revision", base, resolved_head)
                self.assertEqual(base, self.base)
                self.assertEqual(resolved_head, head)
                self.assertEqual(result.diagnostics[0].rule_id, "CH001_IN_THIS_PR")

    def test_untracked_c_file_is_outside_working_scope(self):
        self.write("untracked.c", "/* before this patch */\n")

        result = self.check_mode("working")

        self.assertEqual(result.changed_paths, 0)
        self.assertEqual(result.changed_comments, 0)

    def test_unresolved_c_index_stage_is_error(self):
        object_id = self.git("rev-parse", "HEAD:sample.c").strip()
        self.git("update-index", "--force-remove", "sample.c")
        self.git("update-index", "--index-info",
                 input=b"100644 " + object_id + b" 1\tsample.c\n")

        with self.assertRaisesRegex(checker.InfrastructureError,
                                    "unresolved index stage"):
            self.check_mode("staged")

    def test_missing_index_blob_is_error(self):
        object_length = len(self.git("rev-parse", "HEAD:sample.c").strip())
        missing = "1" * object_length
        self.git("update-index", "--info-only", "--cacheinfo",
                 f"100644,{missing},sample.c")

        with self.assertRaises(checker.InfrastructureError):
            self.check_mode("staged")

    def test_invalid_revision_is_error(self):
        with self.assertRaises(checker.InfrastructureError):
            checker.check(self.root, "revision", self.base, "missing-revision")

    def test_pure_rename_does_not_select_unchanged_comment(self):
        self.write("sample.c", "/* before this patch */\nint baseline;\n")
        self.stage("sample.c")
        self.git("commit", "-qm", "existing comment")
        self.base = self.git("rev-parse", "HEAD").decode().strip()
        self.git("mv", "sample.c", "renamed.c")

        result = self.check_mode("staged")

        self.assertEqual(result.candidate_files, 1)
        self.assertEqual(result.changed_comments, 0)
        self.assertEqual(result.diagnostics, ())

    def test_rename_with_comment_edit_uses_destination(self):
        self.write("sample.c", "/* allocator invariant */\nint baseline;\n")
        self.stage("sample.c")
        self.git("commit", "-qm", "existing comment")
        self.base = self.git("rev-parse", "HEAD").decode().strip()
        self.git("mv", "sample.c", "renamed.c")
        self.write("renamed.c", "/* before this patch */\nint baseline;\n")
        self.stage("renamed.c")

        result = self.check_mode("staged")

        self.assertEqual(result.diagnostics[0].path, b"renamed.c")

    def test_rename_into_c_scope_checks_all_destination_comments(self):
        self.write("sample.txt", "/* before this patch */\n")
        self.stage("sample.txt")
        self.git("commit", "-qm", "non-C source")
        self.base = self.git("rev-parse", "HEAD").decode().strip()
        self.git("mv", "sample.txt", "renamed.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_unusual_filename_bytes_survive_inventory(self):
        name = b"odd-\xff.c"
        blob = self.git("hash-object", "-w", "--stdin",
                        input=b"/* in this PR */\n").strip()
        self.git("update-index", "--add", "--cacheinfo",
                 b"100644," + blob + b"," + name)

        result = self.check_mode("staged")

        self.assertEqual(result.diagnostics[0].path, name)
        self.assertIn("\\xff", checker.display_path(name))

    def test_non_c_and_deleted_files_are_counted_as_exclusions(self):
        self.write("note.txt", "in this PR\n")
        self.stage("note.txt")
        self.remove("sample.c")
        self.git("add", "-u", "sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.non_c_exclusions, 1)
        self.assertEqual(result.deleted_exclusions, 1)
        self.assertEqual(result.changed_comments, 0)

    def test_selected_c_symlink_is_error(self):
        self.remove("sample.c")
        os.symlink("target.c", self.root / "sample.c")
        self.git("add", "sample.c")

        with self.assertRaisesRegex(checker.InfrastructureError,
                                    "unsupported selected mode 120000"):
            self.check_mode("staged")


class AttributionTests(GitFixture):
    def replace_baseline(self, contents):
        self.write("sample.c", contents)
        self.stage("sample.c")
        self.git("commit", "-qm", "fixture baseline")
        self.base = self.git("rev-parse", "HEAD").decode().strip()

    def test_unchanged_comment_beside_code_edit_is_not_selected(self):
        self.replace_baseline("int value = 1; /* before this patch */\n")
        self.write("sample.c", "int value = 2; /* before this patch */\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 0)
        self.assertEqual(result.diagnostics, ())

    def test_unchanged_long_license_is_not_selected_or_advised(self):
        license_lines = ["/* license text"]
        license_lines.extend(f" * permission clause {number}" for number in range(15))
        license_lines.append(" */")
        license_text = "\n".join(license_lines) + "\n"
        self.replace_baseline(license_text + "int value = 1;\n")
        self.write("sample.c", license_text + "int value = 2;\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 0)
        self.assertEqual(result.advisories, ())

    def test_large_repetitive_file_keeps_unchanged_comment_unselected(self):
        repeated = "int repeated;\n" * 20000
        before = repeated + "int value = 1; /* before this patch */\n" + repeated
        after = repeated + "int value = 2; /* before this patch */\n" + repeated
        self.replace_baseline(before)
        self.write("sample.c", after)
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 0)
        self.assertEqual(result.diagnostics, ())

    def test_disjoint_large_edits_keep_middle_comments_unselected(self):
        middle = "int repeated; /* before this patch */\n" * 5000
        self.replace_baseline("int first = 1;\n" + middle + "int last = 1;\n")
        self.write("sample.c", "int first = 2;\n" + middle + "int last = 2;\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 0)
        self.assertEqual(result.diagnostics, ())

    def test_opener_outside_changed_line_selects_whole_comment(self):
        self.replace_baseline("/*\n * allocator invariant\n * remains bounded\n */\n")
        self.write("sample.c", "/*\n * allocator invariant\n * before this patch\n */\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].line, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_complete_comment_deletion_has_no_after_diagnostic(self):
        self.replace_baseline("/* before this patch */\nint value;\n")
        self.write("sample.c", "int value;\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 0)
        self.assertEqual(result.diagnostics, ())

    def test_interior_deletion_selects_surviving_comment(self):
        self.replace_baseline("/* before this very patch */\n")
        self.write("sample.c", "/* before this patch */\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_delimiter_deletion_changes_lexical_ownership(self):
        self.replace_baseline('const char *value = "/* before this patch */";\n')
        self.write("sample.c", "const char *value = /* before this patch */;\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_identical_deleted_comment_cannot_hide_new_lexical_ownership(self):
        self.replace_baseline(
            "/* before this patch */\n"
            "int first;\n"
            'const char *text = "/* before this patch */";\n'
            "int second;\n"
        )
        self.write(
            "sample.c",
            "int first;\n"
            "/* before this patch */\n"
            "int second;\n",
        )
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].line, 2)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_swapped_comments_select_violating_destination(self):
        self.replace_baseline(
            "void first(void) {\n"
            "/* before this patch */\n"
            "}\n"
            "void second(void) {\n"
            "/* allocator invariant */\n"
            "}\n"
        )
        self.write(
            "sample.c",
            "void first(void) {\n"
            "/* allocator invariant */\n"
            "}\n"
            "void second(void) {\n"
            "/* before this patch */\n"
            "}\n",
        )
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertIn(5, [diagnostic.line for diagnostic in result.diagnostics])

    def test_line_join_changes_comment_ownership(self):
        self.replace_baseline("// mechanism\nint before_this_patch;\n")
        self.write("sample.c", "// mechanism int before this patch;\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_diff_context_can_span_a_deleted_comment_newline(self):
        self.replace_baseline(b"/*\n\t*/\n")
        self.write("sample.c", b"/*\t*/\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics, ())

    def test_newline_marker_stops_at_each_side_of_an_asymmetric_hunk(self):
        self.replace_baseline(b"/*\n\nc\t*/\n")
        self.write("sample.c", b"/*c\n\n\nc\t*/\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics, ())

    def test_word_diff_can_omit_a_trailing_blank_line_from_a_hunk(self):
        self.replace_baseline(b"/*\n\n*/\n")
        self.write("sample.c", b"/*a\n*/\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics, ())

    def test_crlf_line_join_changes_comment_ownership(self):
        self.replace_baseline(b"// mechanism\r\nint before_this_patch;\r\n")
        self.write("sample.c", b"// mechanism int before this patch;\r\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_comment_line_ending_change_is_selected_without_parser_recovery(self):
        self.replace_baseline(b"/* alpha\r\n * beta\r\n */\r\n")
        self.write("sample.c", b"/* alpha\r\n * beta\n */\r\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics, ())

    def test_diff_payload_keeps_vertical_and_form_feed_bytes(self):
        self.replace_baseline(
            "int value = 1;\v\f/* allocator invariant */\n"
        )
        self.write(
            "sample.c",
            "int value = 2;\v\f/* before this patch */\n",
        )
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 1)
        self.assertEqual(result.diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")

    def test_new_identical_copy_is_not_hidden_by_existing_comment(self):
        self.replace_baseline("/* before this patch */\nint value;\n")
        self.write("sample.c", "/* before this patch */\n/* before this patch */\nint value;\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.changed_comments, 2)
        self.assertEqual(len(result.diagnostics), 2)
        self.assertEqual({item.line for item in result.diagnostics}, {1, 2})

    def test_long_comment_is_advisory_only(self):
        lines = ["/* allocator invariant"]
        lines.extend(f" * bounded detail {number}" for number in range(13))
        lines.append(" */")
        self.write("sample.c", "\n".join(lines) + "\n")
        self.stage("sample.c")

        result = self.check_mode("staged")

        self.assertEqual(result.diagnostics, ())
        self.assertEqual(len(result.advisories), 1)
        self.assertEqual(result.advisories[0].lines, 15)


class LexerAndRuleTests(unittest.TestCase):
    def changed(self, before, after):
        before_comments = checker.lex_comments(before)
        after_comments = checker.lex_comments(after)
        return checker.changed_after_comments(before, after,
                                              before_comments, after_comments)

    def diagnostics(self, source):
        comments = self.changed(b"", source)
        results = []
        for comment in comments:
            diagnostics, _ = checker.inspect_comment(b"fixture.c", comment, 12)
            results.extend(diagnostics)
        return results

    def test_strings_characters_and_escapes_do_not_create_comments(self):
        source = (b'char *a = "/* in this PR */";\n'
                  b"char slash = '/';\n"
                  b'char *b = "escaped \\\" // after this commit";\n'
                  b"/* mechanism remains bounded now */\n")

        self.assertEqual(self.diagnostics(source), [])
        self.assertEqual(len(checker.lex_comments(source)), 1)

    def test_seeded_newline_edits_keep_the_after_comment_selected(self):
        generator = random.Random(0xC017)
        separators = (b"", b" ", b"\t", b"\n", b"\r\n", b"\n\n", b"\r\n\r\n")
        for case in range(200):
            token_count = generator.randrange(2, 10)
            tokens = [f"field_{case}_{index}".encode("ascii")
                      for index in range(token_count)]
            before_separators = [generator.choice(separators)
                                 for _ in range(token_count - 1)]
            changed_index = generator.randrange(len(before_separators))
            alternatives = [separator for separator in separators
                            if separator != before_separators[changed_index]]
            after_separators = before_separators.copy()
            after_separators[changed_index] = generator.choice(alternatives)

            def source(selected_tokens, selected_separators):
                body = bytearray(b"/* ")
                for index, token in enumerate(selected_tokens):
                    body.extend(token)
                    if index < len(selected_separators):
                        body.extend(selected_separators[index])
                body.extend(b" */\n")
                return b"int before;\n" + bytes(body) + b"int after;\n"

            before = source(tokens, before_separators)
            after = source(tokens, after_separators)
            with self.subTest(case=case):
                before_comments = checker.lex_comments(before)
                after_comments = checker.lex_comments(after)
                selected = checker.changed_after_comments(
                    before, after, before_comments, after_comments
                )
                self.assertEqual(len(before_comments), 1)
                self.assertEqual(len(after_comments), 1)
                self.assertEqual(selected, after_comments)

    def test_spliced_block_delimiters_and_line_comment(self):
        block = b"/??/\n* before this patch *??/\n/\n"
        line = b"// after this \\\ncommit\n"

        block_diagnostics = self.diagnostics(block)
        line_diagnostics = self.diagnostics(line)

        self.assertEqual(block_diagnostics[0].rule_id, "CH002_BEFORE_THIS_PATCH")
        self.assertEqual(line_diagnostics[0].rule_id, "CH003_AFTER_THIS_COMMIT")
        self.assertEqual(checker.lex_comments(block)[0].start_line, 1)
        self.assertEqual(checker.lex_comments(block)[0].end_line, 3)

    def test_spliced_string_keeps_comment_markers_literal(self):
        source = b'char *value = "continued \\\n/* in this PR */";\n'

        self.assertEqual(checker.lex_comments(source), [])

    def test_malformed_selected_inputs_are_errors(self):
        for source, message in (
                (b"/* unterminated", "block comment"),
                (b'"unterminated\n', "string"),
                (b"'x\n", "character literal"),
                (b'"\\', "string"),
                (b"'\\", "character literal")):
            with self.subTest(source=source):
                with self.assertRaisesRegex(checker.LexicalError, message):
                    checker.lex_comments(source, b"bad.c")

    def test_only_explicit_editorial_history_rules_block(self):
        positives = {
            b"/* in this PR */": "CH001_IN_THIS_PR",
            b"/* in this pull request */": "CH001_IN_THIS_PR",
            b"/* before this patch, allocation leaked */": "CH002_BEFORE_THIS_PATCH",
            b"/* after this commit the ABI changes */": "CH003_AFTER_THIS_COMMIT",
            b"/* the reviewer requested another branch */": "CH004_REVIEWER_REQUEST",
        }
        negatives = (
            b"/* goes out now */",
            b"/* previously allocated storage is released */",
            b"/* this stream table remains bounded */",
            b"/* before a patch record is decoded */",
            b"/* after commit processing completes */",
            b"/* PR is a register name */",
            b"/* patch 475 bounds the copy */",
            b"/* see sys/kern/ufs_namei.c */",
        )

        for source, rule_id in positives.items():
            with self.subTest(source=source):
                self.assertEqual(self.diagnostics(source)[0].rule_id, rule_id)
        for source in negatives:
            with self.subTest(source=source):
                self.assertEqual(self.diagnostics(source), [])

    def test_block_decoration_cannot_split_blocking_phrases(self):
        fixtures = {
            b"/* in this\n * PR */": "CH001_IN_THIS_PR",
            b"/* before this\n * patch */": "CH002_BEFORE_THIS_PATCH",
            b"/* after this\n * commit */": "CH003_AFTER_THIS_COMMIT",
            b"/* the reviewer\n * requested another branch */":
                "CH004_REVIEWER_REQUEST",
        }

        for source, rule_id in fixtures.items():
            with self.subTest(source=source):
                self.assertEqual(self.diagnostics(source)[0].rule_id, rule_id)

    def test_raw_parser_handles_rename_and_unusual_bytes(self):
        old = b"old name-\xff.c"
        new = b"new name-\xfe.c"
        raw = (b":100644 100644 " + b"1" * 40 + b" " + b"2" * 40
               + b" R100\0" + old + b"\0" + new + b"\0")

        changes = checker.parse_raw_changes(raw)

        self.assertEqual(changes[0].old_path, old)
        self.assertEqual(changes[0].new_path, new)

    def test_omitted_hunk_tail_accepts_only_newline_bytes(self):
        self.assertEqual(
            checker.consume_omitted_newlines(b"\n\r\n", 0, 3, "fixture"),
            3,
        )
        with self.assertRaisesRegex(checker.InfrastructureError,
                                    "omitted non-newline fixture bytes"):
            checker.consume_omitted_newlines(b"\nX", 0, 2, "fixture")


class RepositoryCalibrationTests(unittest.TestCase):
    def test_historical_and_current_backgammon_comments_pass(self):
        historical = checker.run_git(
            checker.ROOT, "show", "4c13c7da^:games/backgammon/subs.c"
        )
        current = (checker.ROOT / "games/backgammon/subs.c").read_bytes()
        historical_comment = next(
            comment for comment in checker.lex_comments(historical)
            if b"RAW, which main" in comment.body
        )
        current_comment = next(
            comment for comment in checker.lex_comments(current)
            if b"RAW bypasses" in comment.body
        )

        for comment in (historical_comment, current_comment):
            diagnostics, _ = checker.inspect_comment(
                b"games/backgammon/subs.c", comment, checker.ADVISORY_LINE_THRESHOLD
            )
            self.assertEqual(diagnostics, [])

    def test_named_real_tree_comments_pass_rules(self):
        paths = (
            "sys/arch/rp2040/dev/usb.c",
            "sys/kern/subr_rmap.c",
            "sys/kern/ufs_namei.c",
        )
        for name in paths:
            with self.subTest(name=name):
                contents = (checker.ROOT / name).read_bytes()
                for comment in checker.lex_comments(contents, os.fsencode(name)):
                    diagnostics, _ = checker.inspect_comment(
                        os.fsencode(name), comment, checker.ADVISORY_LINE_THRESHOLD
                    )
                    self.assertEqual(diagnostics, [])

    def test_cli_exit_codes_and_empty_denominator(self):
        with tempfile.TemporaryDirectory(prefix="discobsd-comments-cli-") as temporary:
            root = Path(temporary)
            subprocess.run(["git", "-C", temporary, "init", "-q"], check=True)
            subprocess.run(["git", "-C", temporary, "config", "user.name", "Fixture"],
                           check=True)
            subprocess.run(["git", "-C", temporary, "config", "user.email",
                            "fixture@example.invalid"], check=True)
            subprocess.run(["git", "-C", temporary, "config", "commit.gpgsign",
                            "false"], check=True)
            subprocess.run(["git", "-C", temporary, "config", "core.hooksPath",
                            "/dev/null"], check=True)
            subprocess.run(["git", "-C", temporary, "config", "core.autocrlf",
                            "false"], check=True)
            (root / "sample.c").write_text("int value;\n")
            subprocess.run(["git", "-C", temporary, "add", "sample.c"], check=True)
            subprocess.run(["git", "-C", temporary, "commit", "-qm", "baseline"],
                           check=True)
            output = io.StringIO()
            with redirect_stdout(output):
                status = checker.main(["--root", temporary, "--working"])
            self.assertEqual(status, 0)
            self.assertIn("changed_comments=0", output.getvalue())

            (root / "sample.c").write_text("/* in this PR */\n")
            error = io.StringIO()
            with redirect_stderr(error):
                status = checker.main(["--root", temporary, "--working"])
            self.assertEqual(status, 1)
            self.assertIn("CH001_IN_THIS_PR", error.getvalue())

            error = io.StringIO()
            with redirect_stderr(error):
                status = checker.main([
                    "--root", temporary, "--revision", "missing", "--base", "HEAD"
                ])
            self.assertEqual(status, 2)
            self.assertIn("ERROR changed-comments", error.getvalue())


if __name__ == "__main__":
    unittest.main()
