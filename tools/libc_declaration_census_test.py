"""Calibrate declaration exposure and linkage-name provider accounting."""

import copy
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import libc_declaration_census as census


def function(name, linkage=None, **extra):
    return {"kind": "FunctionDecl", "name": name, "mangledName": linkage or name,
            "type": {"qualType": "int (int)"}, **extra}


def tree(*nodes):
    return {"kind": "TranslationUnitDecl", "inner": list(nodes)}


def graph(name, kind="function"):
    return {"providers": {name: ["member.o"]}, "members": {
        "member.o": {"definitions": {name: {"kind": kind,
                                             "size": 4, "mutable": kind == "object",
                                             "binding": "STB_GLOBAL"}}}}}


class DeclarationTests(unittest.TestCase):
    def test_archive_query_expands_and_closes_the_owner_denominator(self):
        names = sorted(set(census.ARCHIVES) | {"startup-arm", "libc_aout"})
        result = SimpleNamespace(returncode=0, stderr="", stdout=" ".join(names))
        with patch.object(census.subprocess, "run", return_value=result) as query:
            paths = census.archive_paths(Path("fixture"))
            self.assertEqual(query.call_args.args[0][-1], "${SUBDIR}")
            self.assertNotIn("MAKEFLAGS", query.call_args.kwargs["env"])
            self.assertEqual(set(paths), set(census.ARCHIVES.values()))
        result.stdout += " libnew"
        with patch.object(census.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "owner denominator changes"):
                census.archive_paths(Path("fixture"))

    def test_full_report_denominators_reject_omissions(self):
        profile = {"headers": {"a.h": {"symbols": []}}, "joint": {"symbols": []},
                   "installed_headers": ["a.h"], "installed_joint": {"symbols": []},
                   "declarations": {}, "provider_counts": {}}
        report = {"headers": ["a.h"], "installed_headers": ["a.h"],
                  "archives": dict.fromkeys(census.ARCHIVES.values(), "fixture"),
                  "profiles": {f"{m}.time{w}": {**copy.deepcopy(profile),
                                                "selectors": [*census.MODES[m],
                                                              f"-D_TIME_BITS={w}"]}
                               for m in census.MODES for w in (32, 64)}}
        census.verify_report(report)
        bad = copy.deepcopy(report)
        del bad["profiles"]["issue8.time64"]
        with self.assertRaisesRegex(ValueError, "profile/time denominator"):
            census.verify_report(bad)
        bad = copy.deepcopy(report)
        bad["profiles"]["issue8.time64"]["selectors"] = ["-D_TIME_BITS=32"]
        with self.assertRaisesRegex(ValueError, "profile/time selectors"):
            census.verify_report(bad)
        del report["archives"]["libc"]
        with self.assertRaisesRegex(ValueError, "archive denominator"):
            census.verify_report(report)

    def test_filters_static_implicit_and_nested_declarations(self):
        nodes = tree(function("public"), function("private", storageClass="static"),
                     function("implicit", isImplicit=True),
                     {"kind": "RecordDecl", "inner": [function("nested")]})
        self.assertEqual(set(census.declarations(nodes)), {"public"})

    def test_retains_linkage_name_and_parameter_types(self):
        node = function("public", "actual", inner=[
            {"kind": "ParmVarDecl", "type": {"qualType": "int"}},
            {"kind": "NoReturnAttr"}])
        observed = census.declarations(tree(node))["public"]
        self.assertEqual(observed["linkage"], "actual")
        self.assertEqual(observed["parameters"], [["int"]])
        self.assertEqual(observed["attributes"], ["NoReturnAttr"])

    def test_unprovided_alias_does_not_bind_to_its_c_name(self):
        observed = census.declarations(tree(function("public", "actual")))["public"]
        classified = census.provider_observation(observed, {"libc": graph("public")})
        self.assertEqual(classified["state"], "unprovided")
        self.assertEqual(classified["providers"], [])

    def test_matching_linkage_provider(self):
        observed = census.declarations(tree(function("public", "actual")))["public"]
        result = census.provider_observation(observed, {"libc": graph("actual")})
        self.assertEqual(result["state"], "matched")
        self.assertEqual(result["providers"][0]["member"], "member.o")

    def test_provider_kind_mismatch_remains_visible(self):
        observed = census.declarations(tree(function("public")))["public"]
        result = census.provider_observation(observed, {"libc": graph("public", "object")})
        self.assertEqual(result["state"], "kind-mismatch")

    def test_multiple_archive_owners_require_disposition(self):
        observed = census.declarations(tree(function("public")))["public"]
        result = census.provider_observation(observed, {"libc": graph("public"),
                                                       "libm": graph("public")})
        self.assertEqual(result["state"], "multiple-providers")

    def test_missing_header_exposure_rejects_reconciliation(self):
        observed = census.declarations(tree(function("public")))
        with self.assertRaisesRegex(ValueError, "header exposure"):
            census.reconcile(["a.h", "b.h"], {"a.h": observed}, observed)

    def test_joint_parse_preserves_standalone_only_declaration(self):
        observed = census.declarations(tree(function("public")))
        rows = census.reconcile(["a.h"], {"a.h": observed}, {})
        self.assertEqual(rows["public"]["exposure"], "standalone-only")
        self.assertEqual(rows["public"]["exposed_by"], ["a.h"])

    def test_omitted_required_declaration_row_rejects(self):
        profile = {"headers": {"a.h": {"symbols": ["public"]}},
                   "installed_headers": ["a.h"], "installed_joint": {"symbols": []},
                   "joint": {"symbols": []}, "declarations": {}}
        with self.assertRaisesRegex(ValueError, "declaration witness denominator"):
            census.verify_profile(["a.h"], profile)

    def test_exposure_and_review_state_controls(self):
        rows = census.reconcile(["a.h"], {"a.h": census.declarations(
            tree(function("public")))}, {})
        row = rows["public"]
        row["provider"] = census.provider_observation(row, {"libc": graph("public")})
        row["next_action"] = row["provider"]["next_action"]
        profile = {"headers": {"a.h": {"symbols": ["public"]}},
                   "installed_headers": ["a.h"], "installed_joint": {"symbols": []},
                   "joint": {"symbols": []}, "declarations": rows,
                   "provider_counts": {"matched": 1}}
        census.verify_profile(["a.h"], profile)
        row["exposed_by"] = []
        with self.assertRaisesRegex(ValueError, "exposure differs"):
            census.verify_profile(["a.h"], profile)
        row["exposed_by"] = ["a.h"]
        row["abi_selection"] = "accepted"
        with self.assertRaisesRegex(ValueError, "unreviewed disposition"):
            census.verify_profile(["a.h"], profile)
        row["abi_selection"] = "unreviewed"
        row["installed_joint_observed"] = True
        with self.assertRaisesRegex(ValueError, "installed declaration exposure"):
            census.verify_profile(["a.h"], profile)

    def test_provider_summary_cannot_hide_a_gap(self):
        rows = census.reconcile(["a.h"], {"a.h": census.declarations(
            tree(function("public")))}, {})
        row = rows["public"]
        row["provider"] = census.provider_observation(row, {})
        row["next_action"] = row["provider"]["next_action"]
        profile = {"headers": {"a.h": {"symbols": ["public"]}},
                   "installed_headers": ["a.h"], "installed_joint": {"symbols": []},
                   "joint": {"symbols": []}, "declarations": rows,
                   "provider_counts": {"matched": 1}}
        with self.assertRaisesRegex(ValueError, "provider counts"):
            census.verify_profile(["a.h"], profile)
        row["provider"]["state"] = "matched"
        with self.assertRaisesRegex(ValueError, "provider disposition differs"):
            census.verify_profile(["a.h"], profile)

    def test_conflicting_linkage_redeclarations_reject(self):
        with self.assertRaisesRegex(ValueError, "linkage"):
            census.declarations(tree(function("public", "one"),
                                     function("public", "two")))

    def test_source_only_header_retains_its_uninstalled_disposition(self):
        observed = census.declarations(tree(function("future")))
        rows = census.reconcile(["public.h", "todo.h"],
                                {"public.h": {}, "todo.h": observed}, observed,
                                ["public.h"], {})
        self.assertEqual(rows["future"]["exposed_by"], ["todo.h"])
        self.assertEqual(rows["future"]["installed_exposed_by"], [])
        self.assertFalse(rows["future"]["installed_joint_observed"])

    def test_installed_query_distinguishes_source_only_header(self):
        result = SimpleNamespace(returncode=0, stderr="", stdout="public.h public.h")
        with patch.object(census.subprocess, "run", return_value=result):
            self.assertEqual(census.installed_headers(Path("fixture"),
                                                      ["public.h", "todo.h"]), ["public.h"])
        result.stdout += " absent.h"
        with patch.object(census.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "installed header denominator"):
                census.installed_headers(Path("fixture"), ["public.h", "todo.h"])

    def test_malformed_ast_rejects(self):
        for value in [{}, {"kind": "TranslationUnitDecl", "inner": {}}, []]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                census.declarations(value)


class CompilerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="declaration-census-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.work = self.root / "work"
        self.work.mkdir()
        (self.root / "include").mkdir()
        self.compiler = shutil.which("clang")
        if not self.compiler:
            raise RuntimeError("Clang is required for declaration census calibration")

    def compile(self, source, header="probe.h"):
        (self.root / "include" / header).write_text(source)
        return census.compile_headers(self.root, self.work, self.compiler, [],
                                      [header], "probe")

    def test_selected_compiler_passes_its_own_controls(self):
        controls = census.calibrate_compiler(self.root, self.work, self.compiler)
        self.assertEqual(controls["accept"]["exit_code"], 0)
        self.assertNotEqual(controls["reject"]["exit_code"], 0)

    def test_always_accepting_compiler_is_rejected(self):
        compiler = self.work / "false-compiler"
        response = json.dumps(tree(function("declaration_census_control")))
        compiler.write_text("#!/bin/sh\ncat <<'EOF'\n" + response + "\nEOF\n")
        compiler.chmod(0o700)
        with self.assertRaisesRegex(ValueError, "compiler rejection control fails"):
            census.calibrate_compiler(self.root, self.work, str(compiler))

    def test_real_compiler_retains_assembly_alias_and_filters_static(self):
        observed, witness = self.compile(
            'extern int public(int) __asm__("actual");\n'
            'static inline int private(int n) { return n; }\n'
            'extern int object;\n')
        self.assertEqual(set(observed), {"public", "object"})
        self.assertEqual(observed["public"]["linkage"], "actual")
        self.assertEqual(observed["object"]["kind"], "object")
        self.assertEqual(set(witness["dependencies"]), {"include/probe.h"})
        result = census.provider_observation(observed["public"], {"libc": graph("public")})
        self.assertEqual(result["state"], "unprovided")

    def test_alias_object_import_agrees_with_ast_linkage(self):
        source = ('extern int public(int) __asm__("actual");\n'
                  'int (*pointer)(int) = public;\n')
        observed, _ = self.compile(source)
        object_path = self.work / "alias.o"
        result = subprocess.run([self.compiler, *census.FLAGS, "-c", "-x", "c", "-",
                                 "-o", str(object_path)], input=source.encode(),
                                capture_output=True, timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        nm = os.environ.get("DECLARATION_CENSUS_NM", "arm-none-eabi-nm")
        output = subprocess.check_output([nm, "--undefined-only", str(object_path)], text=True)
        self.assertEqual({line.split()[-1] for line in output.splitlines()},
                         {observed["public"]["linkage"]})

    def test_invalid_header_fails_through_compiler_diagnostic(self):
        with self.assertRaisesRegex(ValueError, "census rejection control"):
            self.compile('#error "census rejection control"\n')

    def test_dotted_header_name_is_valid(self):
        observed, _ = self.compile("extern int public(int);\n", "a.out.h")
        self.assertEqual(set(observed), {"public"})

    def test_header_traversal_fails_before_compilation(self):
        with self.assertRaisesRegex(ValueError, "header spelling"):
            census.compile_headers(self.root, self.work, self.compiler, [],
                                   ["../probe.h"], "probe")

    def test_symlinked_root_spelling_is_accepted(self):
        link = Path(self.work) / "root-link"
        link.symlink_to(self.root, target_is_directory=True)
        (self.root / "include" / "probe.h").write_text("extern int public(int);\n")
        observed, witness = census.compile_headers(link, self.work, self.compiler, [],
                                                   ["probe.h"], "probe")
        self.assertEqual(set(observed), {"public"})
        self.assertEqual(set(witness["dependencies"]), {"include/probe.h"})

    def test_header_symlink_to_external_file_fails(self):
        temporary = tempfile.TemporaryDirectory(prefix="declaration-census-external-")
        self.addCleanup(temporary.cleanup)
        outside = Path(temporary.name) / "outside.h"
        outside.write_text("extern int external(void);\n")
        (self.root / "include" / "probe.h").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "dependency leaves repository"):
            census.compile_headers(self.root, self.work, self.compiler, [],
                                   ["probe.h"], "probe")

    def test_external_dependency_fails_after_compilation(self):
        temporary = tempfile.TemporaryDirectory(prefix="declaration-census-external-")
        self.addCleanup(temporary.cleanup)
        outside = Path(temporary.name) / "outside.h"
        outside.write_text("extern int external(void);\n")
        with self.assertRaisesRegex(ValueError, "dependency leaves repository"):
            self.compile(f'#include "{outside}"\n')


if __name__ == "__main__":
    unittest.main()
