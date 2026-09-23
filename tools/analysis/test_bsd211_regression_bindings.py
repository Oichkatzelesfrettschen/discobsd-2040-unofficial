"""Calibrate source-to-object-to-executable joins in pinned make recipes."""

import copy
import hashlib
import unittest

import bsd211_regression_bindings as recipes
import bsd211_semantic_ledger as ledger


class RegressionBindingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = ledger.load(ledger.ROOT / ledger.LEDGER)
        cls.evidence = ledger.load(ledger.ROOT / cls.data["execution_evidence"])
        cls.bindings = ledger.load(ledger.ROOT / cls.data["regression_bindings"])
        cls.report = ledger.load(ledger.ROOT / cls.evidence["sanitized_report"])
        cls.inventory = ledger.load_blob(
            ledger.blob(ledger.ROOT, cls.data["recipient_commit"],
                        "tools/test-execution-inventory.json"), "fixture inventory")
        cls.sources = {
            (revision, path): ledger.blob(ledger.ROOT, revision, path)
            for revision in (cls.data["recipient_commit"],
                             cls.evidence["source_comparison"]["executed_tree"])
            for path in cls.bindings["makefiles"] | cls.bindings["supporting_inputs"]
        }

    def setUp(self):
        self.data = copy.deepcopy(type(self).data)
        self.evidence = copy.deepcopy(type(self).evidence)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.report = copy.deepcopy(type(self).report)
        self.inventory = copy.deepcopy(type(self).inventory)
        self.sources = copy.deepcopy(type(self).sources)

    def read_blob(self, revision, path):
        return self.sources[(revision, path)]

    def validate(self):
        recipes.validate(self.data, self.evidence, self.bindings,
                         self.inventory, self.report, self.read_blob)

    def replace_makefile(self, path, before, after):
        recipient = self.data["recipient_commit"]
        executed = self.evidence["source_comparison"]["executed_tree"]
        contents = self.sources[(recipient, path)]
        self.assertIn(before, contents)
        changed = contents.replace(before, after, 1)
        self.sources[(recipient, path)] = changed
        self.sources[(executed, path)] = changed
        self.bindings["makefiles"][path] = hashlib.sha256(changed).hexdigest()

    def test_all_executed_rows_have_pinned_recipe_bindings(self):
        self.validate()
        self.assertEqual(len(self.bindings["variants"]), 12)
        self.assertEqual(len(self.bindings["makefiles"]), 3)

    def test_missing_wrong_source_and_wrong_target_fail(self):
        del self.bindings["variants"]["kernel.inode.ilp32"]
        with self.assertRaisesRegex(ValueError, "variant set mismatch"):
            self.validate()
        self.bindings = copy.deepcopy(type(self).bindings)
        self.bindings["variants"]["libc.scanf.native"]["source"] = (
            "tests/libc_contracts/rwmode_contract_test.c")
        with self.assertRaisesRegex(ValueError, "regression source binding mismatch"):
            self.validate()
        self.bindings = copy.deepcopy(type(self).bindings)
        self.bindings["variants"]["libc.scanf.native"]["compile_target"] = (
            "rwmode_contract_test")
        with self.assertRaisesRegex(ValueError, "compiler target drift"):
            self.validate()

    def test_umount_gate_object_must_compile_and_link(self):
        path = "tests/umount_contracts/Makefile"
        self.replace_makefile(path,
                              b"${PROG}: gate.o umount.o shim.o",
                              b"${PROG}: umount.o shim.o")
        with self.assertRaisesRegex(ValueError, "gate.o is absent"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile(path,
                              b"-c -o $@ umount_contract_test.c",
                              b"-c -o $@ unrelated_test.c")
        with self.assertRaisesRegex(ValueError, "compiler recipe"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile(path,
                              b"${HOSTCC} ${CFLAGS} -o $@ gate.o umount.o shim.o",
                              b"echo ${CFLAGS} -o $@ gate.o umount.o shim.o")
        with self.assertRaisesRegex(ValueError, "gate.o is absent"):
            self.validate()

    def test_direct_recipe_cannot_lose_regression_source(self):
        path = "tests/kernel/Makefile"
        self.replace_makefile(path,
                              b"-o $@ sysctl_test.c \\\n",
                              b"-o $@ unrelated_test.c \\\n")
        with self.assertRaisesRegex(ValueError, "compiler recipe"):
            self.validate()

    def test_execution_target_must_build_its_program(self):
        path = "tests/libc_contracts/Makefile"
        self.replace_makefile(path, b"check-scanf: scanf_contract_test",
                              b"check-scanf:")
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile(path, b"@${MAKE} scanf_contract_test32",
                              b"@true")
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile(path, b"@${MAKE} scanf_contract_test32",
                              b"@${MAKE} -n scanf_contract_test32")
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile("tests/kernel/Makefile",
                              b"ialloc_test ialloc_test_pico ialloc_test_diagnostic",
                              b"ialloc_test_pico ialloc_test_diagnostic")
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile("tests/umount_contracts/Makefile",
                              b"@${MAKE} ${PROG}", b"@true")
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()

    def test_syntax_only_compiler_cannot_bind_an_executable(self):
        self.replace_makefile("tests/libc_contracts/Makefile",
                              b"-o $@ scanf_contract_test.c ${SCANF_OBJS}",
                              b"-fsyntax-only -o $@ scanf_contract_test.c ${SCANF_OBJS}")
        with self.assertRaisesRegex(ValueError, "compiler recipe"):
            self.validate()

    def test_shell_comment_cannot_supply_regression_source(self):
        self.replace_makefile(
            "tests/libc_contracts/Makefile",
            b"-o $@ scanf_contract_test.c ${SCANF_OBJS}",
            b"-o $@ unrelated.c # scanf_contract_test.c ${SCANF_OBJS}",
        )
        with self.assertRaisesRegex(ValueError, "compiler recipe"):
            self.validate()

    def test_recursive_build_must_share_the_run_branch(self):
        path = "tests/libc_contracts/Makefile"
        self.replace_makefile(
            path,
            b"\t@${MAKE} scanf_contract_test32\n"
            b"\t${TEST_EXECUTION} run libc.scanf.ilp32 -- ./scanf_contract_test32\n"
            b".else",
            b"\t${TEST_EXECUTION} run libc.scanf.ilp32 -- ./scanf_contract_test32\n"
            b".else\n\t@${MAKE} scanf_contract_test32",
        )
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile(
            path,
            b"\t@${MAKE} scanf_contract_test32\n"
            b"\t${TEST_EXECUTION} run libc.scanf.ilp32 -- ./scanf_contract_test32",
            b"\t@${MAKE} scanf_contract_test32\n"
            b".elif ${ILP32_OK} == \"other\"\n"
            b"\t${TEST_EXECUTION} run libc.scanf.ilp32 -- ./scanf_contract_test32",
        )
        with self.assertRaisesRegex(ValueError, "not built before execution"):
            self.validate()

    def test_width_assignment_and_native_compiler_width_are_bound(self):
        for path, assignment in (
                ("tests/kernel/Makefile", b"ILP32=\t\t-m32"),
                ("tests/libc_contracts/Makefile", b"ILP32=\t\t-m32"),
                ("tests/umount_contracts/Makefile", b"ILP32=\t-m32")):
            with self.subTest(path=path):
                self.sources = copy.deepcopy(type(self).sources)
                self.bindings = copy.deepcopy(type(self).bindings)
                self.replace_makefile(path, assignment, assignment.replace(b"-m32", b""))
                with self.assertRaisesRegex(ValueError, "ILP32 width assignment"):
                    self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile("tests/libc_contracts/Makefile",
                              b"ILP32=\t\t-m32", b"ILP32?=\t\t-m32")
        with self.assertRaisesRegex(ValueError, "ILP32 width assignment"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        self.bindings = copy.deepcopy(type(self).bindings)
        self.replace_makefile("tests/libc_contracts/Makefile",
                              b"-o $@ scanf_contract_test.c ${SCANF_OBJS}",
                              b"-m32 -o $@ scanf_contract_test.c ${SCANF_OBJS}")
        with self.assertRaisesRegex(ValueError, "compiler width mismatch"):
            self.validate()

    def test_pinned_makefile_and_inventory_recipe_are_required(self):
        path = "tests/libc_contracts/Makefile"
        self.bindings["makefiles"][path] = "0" * 64
        with self.assertRaisesRegex(ValueError, "pinned recipe input hash"):
            self.validate()
        self.bindings = copy.deepcopy(type(self).bindings)
        self.bindings["supporting_inputs"]["tools/test_execution.py"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "pinned recipe input hash"):
            self.validate()
        self.bindings = copy.deepcopy(type(self).bindings)
        variant = next(item for item in self.inventory["variants"]
                       if item["id"] == "libc.scanf.native")
        variant["recipe"] = "${TEST_EXECUTION} run libc.scanf.native -- ./wrong"
        with self.assertRaisesRegex(ValueError, "inventory execution recipe is absent"):
            self.validate()

    def test_same_basename_in_other_directory_and_receipt_drift_fail(self):
        variant = next(item for item in self.inventory["variants"]
                       if item["id"] == "libc.scanf.native")
        variant["makefile"] = "tests/kernel/Makefile"
        with self.assertRaisesRegex(ValueError, "build rule"):
            self.validate()
        self.inventory = copy.deepcopy(type(self).inventory)
        receipt = next(item for item in self.report["variants"]
                       if item["variant"] == "libc.scanf.native")
        receipt["invocation"] = ["./wrong"]
        with self.assertRaisesRegex(ValueError, "retained executable receipt"):
            self.validate()

    def test_pinned_source_hash_presence_and_revision_identity(self):
        del self.evidence["source_comparison"]["source_sha256"][
            "tests/kernel/ialloc_test.c"]
        with self.assertRaisesRegex(ValueError, "regression source lacks pinned hash"):
            self.validate()
        self.evidence = copy.deepcopy(type(self).evidence)
        self.bindings["executed_tree"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "revision mismatch"):
            self.validate()


if __name__ == "__main__":
    unittest.main()
