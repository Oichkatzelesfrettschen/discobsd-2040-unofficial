"""Calibrate shared-libc provider closure with compiler-produced ARM objects."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import libc_shared_closure as closure

TARGET_COMPILER = os.environ.get("SHARED_CLOSURE_CC", "arm-none-eabi-gcc")
ARCHIVER = os.environ.get("SHARED_CLOSURE_AR", "arm-none-eabi-ar")


def contract(*names, external=()):
    return closure.read_contract({
        "schema": 1,
        "roots": [{"symbol": name, "kind": "function", "reason": "public test API"}
                  for name in names],
        "external_symbols": [{"symbol": name, "owner": "startup",
                              "reason": "startup-owned test state"} for name in external],
    })


class ClosureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="libc-closure-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        sources = {
            "api": "extern int helper(void); int api(void) { return helper(); }",
            "helper": "extern int state; int helper(void) { return state; }",
            "state": "int state = 7;",
            "dormant": "int dormant(void) { return 42; }",
            "broken": "extern int absent(void); int broken(void) { return absent(); }",
            "weak_api": "extern int optional(void) __attribute__((weak)); "
                        "int weak_api(void) { return optional ? optional() : 0; }",
            "optional": "int optional(void) { return 23; }",
            "alpha": "extern int beta(void); int alpha(void) { return beta(); }",
            "beta": "extern int alpha(void); int beta(void) { return alpha(); }",
            "collision": "int api(void) { return 99; }",
        }
        for name, source in sources.items():
            path = cls.work / (name + ".c")
            path.write_text(source + "\n")
            cls.command([TARGET_COMPILER, "-std=c17", "-pedantic-errors", "-Wall",
                         "-Wextra", "-Werror", "-ffreestanding", "-fno-common", "-Os",
                         "-mcpu=cortex-m0plus", "-mthumb", "-c", path,
                         "-o", cls.work / (name + ".o")])
        cls.archive = cls.work / "lib.a"
        cls.command([ARCHIVER, "rcs", cls.archive,
                     *(cls.work / (name + ".o") for name in sources if name != "collision")])
        cls.graph = closure.read_archive(cls.archive, ARCHIVER)

    @staticmethod
    def command(argv):
        result = subprocess.run([str(value) for value in argv], capture_output=True,
                                text=True, timeout=30, check=False)
        if result.returncode or result.stderr:
            raise RuntimeError(result.stdout + result.stderr)

    def test_transitive_dependencies_and_unused_member(self):
        result = closure.resolve(self.graph, contract("api"))
        self.assertEqual(result["members"], ["api.o", "helper.o", "state.o"])
        self.assertEqual(result["external_imports"], [])

    def test_explicit_api_survives_absent_program_callers(self):
        specification = contract("api", "dormant")
        result = closure.resolve(self.graph, specification)
        self.assertEqual(result["members"], ["api.o", "dormant.o", "helper.o", "state.o"])
        with self.assertRaisesRegex(closure.ClosureError, "required API dormant"):
            closure.verify(self.graph, specification, ["api.o", "helper.o", "state.o"])

    def test_missing_dependency_rejects_selected_members(self):
        with self.assertRaisesRegex(closure.ClosureError, "unresolved strong import state"):
            closure.verify(self.graph, contract("api"), ["api.o", "helper.o"])

    def test_cycle_reaches_a_finite_fixed_point(self):
        result = closure.resolve(self.graph, contract("alpha"))
        self.assertEqual(result["members"], ["alpha.o", "beta.o"])

    def test_optional_weak_import_does_not_force_a_provider(self):
        result = closure.resolve(self.graph, contract("weak_api"))
        self.assertEqual(result["members"], ["weak_api.o"])
        self.assertEqual(result["unresolved_weak"], ["optional"])
        result = closure.resolve(self.graph, contract("weak_api", "optional"))
        self.assertEqual(result["members"], ["optional.o", "weak_api.o"])
        self.assertEqual(result["unresolved_weak"], [])

    def test_strong_import_requires_a_provider_or_named_external_owner(self):
        with self.assertRaisesRegex(closure.ClosureError, "unresolved strong import absent"):
            closure.resolve(self.graph, contract("broken"))
        result = closure.resolve(self.graph, contract("broken", external=("absent",)))
        self.assertEqual(result["external_imports"], ["absent"])

    def test_public_root_cannot_be_satisfied_by_an_external_owner(self):
        with self.assertRaisesRegex(closure.ClosureError, "external.*API|API.*external"):
            contract("absent", external=("absent",))

    def test_kind_confusion_rejects_a_data_object_as_a_function(self):
        with self.assertRaisesRegex(closure.ClosureError, "kind.*state|state.*kind"):
            closure.resolve(self.graph, contract("state"))

    def test_conflicting_providers_reject_instead_of_following_archive_order(self):
        path = self.work / "collision.a"
        self.command([ARCHIVER, "rcs", path,
                      self.work / "api.o", self.work / "collision.o"])
        graph = closure.read_archive(path, ARCHIVER)
        with self.assertRaisesRegex(closure.ClosureError, "conflicting providers.*api"):
            closure.resolve(graph, contract("api"))

    def test_duplicate_member_names_reject_before_symbol_inspection(self):
        path = self.work / "duplicate.a"
        self.command([ARCHIVER, "qc", path,
                      self.work / "api.o", self.work / "api.o"])
        with self.assertRaisesRegex(closure.ClosureError, "duplicate.*member"):
            closure.read_archive(path, ARCHIVER)

    def test_case_aliases_reject_on_every_host_filesystem(self):
        objects = []
        for directory, name in (("upper", "_Exit.o"), ("lower", "_exit.o")):
            parent = self.work / directory
            parent.mkdir()
            path = parent / name
            path.write_bytes((self.work / "api.o").read_bytes())
            objects.append(path)
        archive = self.work / "case-aliases.a"
        self.command([ARCHIVER, "rcs", archive, *objects])
        with self.assertRaisesRegex(closure.ClosureError, "case-insensitive"):
            closure.read_archive(archive, ARCHIVER)

    def test_archive_paths_reject_without_extraction(self):
        payload = (self.work / "api.o").read_bytes()
        header = (f"{'../bad.o/':<16}{0:<12}{0:<6}{0:<6}{'100644':<8}"
                  f"{len(payload):<10}`\n").encode("ascii")
        path = self.work / "traversal.a"
        path.write_bytes(b"!<arch>\n" + header + payload + (b"\n" if len(payload) % 2 else b""))
        with self.assertRaisesRegex(closure.ClosureError, "unsafe archive member"):
            closure.read_archive(path, ARCHIVER)

    def test_object_in_executable_section_remains_an_object(self):
        source = self.work / "text_object.S"
        source.write_text(".text\n.global text_object\n.type text_object,%object\n"
                          ".size text_object,4\ntext_object:\n.word 7\n")
        target = self.work / "text_object.o"
        self.command([TARGET_COMPILER, "-mcpu=cortex-m0plus", "-mthumb",
                      "-c", source, "-o", target])
        member = closure.read_member("text_object.o", target.read_bytes())
        self.assertEqual(member["definitions"]["text_object"]["kind"], "object")

    def test_strong_provider_overrides_optional_weak_definition(self):
        source = self.work / "weak_dormant.c"
        source.write_text("__attribute__((weak)) int dormant(void) { return 0; }\n")
        target = self.work / "weak_dormant.o"
        self.command([TARGET_COMPILER, "-std=c17", "-Wall", "-Wextra", "-Werror",
                      "-mcpu=cortex-m0plus", "-mthumb", "-c", source, "-o", target])
        archive = self.work / "strong-weak.a"
        self.command([ARCHIVER, "rcs", archive, target, self.work / "dormant.o"])
        result = closure.resolve(closure.read_archive(archive, ARCHIVER),
                                 contract("dormant"))
        self.assertEqual(result["members"], ["dormant.o"])

    def test_archive_and_contract_boundaries(self):
        bad = self.work / "bad.a"
        bad.write_bytes(b"not an archive")
        with self.assertRaises(closure.ClosureError):
            closure.read_archive(bad, ARCHIVER)
        specification = {"schema": 1, "roots": [], "external_symbols": []}
        with self.assertRaisesRegex(closure.ClosureError, "root"):
            closure.read_contract(specification)
        specification["roots"] = [{"symbol": "../api", "kind": "function", "reason": "API"}]
        with self.assertRaisesRegex(closure.ClosureError, "symbol"):
            closure.read_contract(specification)
        specification["roots"] = [{"symbol": "api", "kind": "function", "reason": "API"}] * 2
        with self.assertRaisesRegex(closure.ClosureError, "duplicate"):
            closure.read_contract(specification)

    def test_thin_archive_requires_embedded_member_identity(self):
        archive = self.work / "thin.a"
        subprocess.run([ARCHIVER, "rcsT", archive.name, "api.o"], cwd=self.work,
                       capture_output=True, check=True, timeout=30)
        with self.assertRaisesRegex(closure.ClosureError, "regular embedded archive"):
            closure.read_archive(archive, ARCHIVER)

    def test_common_storage_requires_canonical_owner(self):
        source = self.work / "common.c"
        source.write_text("int shared_state;\n")
        target = self.work / "common.o"
        self.command([TARGET_COMPILER, "-std=c17", "-pedantic-errors", "-Wall",
                      "-Wextra", "-Werror", "-mcpu=cortex-m0plus", "-mthumb",
                      "-fcommon", "-c", source, "-o", target])
        with self.assertRaisesRegex(closure.ClosureError, "common symbol lacks sole owner"):
            closure.read_member("common.o", target.read_bytes())

    def test_ambiguous_weak_providers_require_explicit_ownership(self):
        objects = []
        for name in ("weak_first", "weak_second"):
            source = self.work / (name + ".c")
            source.write_text("__attribute__((weak)) int weak_root(void) { return 7; }\n")
            target = source.with_suffix(".o")
            self.command([TARGET_COMPILER, "-std=c17", "-pedantic-errors", "-Wall",
                          "-Wextra", "-Werror", "-mcpu=cortex-m0plus", "-mthumb",
                          "-c", source, "-o", target])
            objects.append(target)
        archive = self.work / "ambiguous.a"
        self.command([ARCHIVER, "rcs", archive, *objects])
        with self.assertRaisesRegex(closure.ClosureError, "conflicting providers.*weak_root"):
            closure.resolve(closure.read_archive(archive, ARCHIVER), contract("weak_root"))

    def test_report_is_deterministic_and_counts_a_member_once(self):
        specification = contract("api", "helper")
        first = closure.resolve(self.graph, specification)
        second = closure.resolve(self.graph, specification)
        self.assertEqual(json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True))
        self.assertEqual(first["members"], ["api.o", "helper.o", "state.o"])
        self.assertEqual(first["input_section_bytes"][".data"], 4)

    def test_cli_preserves_contract_and_rejects_duplicate_json_fields(self):
        specification = self.work / "contract.json"
        specification.write_text(json.dumps(contract("api")))
        argv = [sys.executable, str(Path(closure.__file__)), str(self.archive),
                "--ar", ARCHIVER, "--contract", str(specification)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["members"],
                         ["api.o", "helper.o", "state.o"])
        specification.write_text('{"schema":1,"schema":2,"roots":[],"external_symbols":[]}')
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("duplicate JSON field", result.stderr)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
