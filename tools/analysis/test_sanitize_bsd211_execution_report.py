"""Calibrate the retained hosted-report path transformation."""

import copy
import hashlib
import json
import unittest

import sanitize_bsd211_execution_report as sanitizer


class SanitizerTest(unittest.TestCase):
    def setUp(self):
        root = sanitizer.HOSTED_ROOT
        self.report = {
            "inventory_sha256": "a" * 64,
            "invocation": ["bmake", f"TEST_EXECUTION_REPORT={sanitizer.REPORT_TEMP}",
                           "TEST_EXECUTION_DIR=/tmp/discobsd-execution-fixture"],
            "returncode": 0,
            "revision": "b" * 40,
            "tracked_diff_sha256": "c" * 64,
            "tracked_files_modified": False,
            "variants": [
                self.receipt("libc.base.native", root + "/tests/libc_contracts",
                             root + "/tests/libc_contracts/libc_contracts_test",
                             ["./libc_contracts_test"]),
                self.receipt("dd.filesystem.ilp32", root + "/tests/dd_contracts",
                             root + "/tests/dd_contracts/dd_host",
                             ["sh", root + "/tests/dd_contracts/filesystem_test.sh",
                              root + "/tests/dd_contracts/dd_host"]),
                self.receipt("shell.posix.ilp32", root,
                             root + "/bin/sh/tests/out/sh",
                             ["sh", "bin/sh/tests/posix-sh.sh"]),
                self.receipt("textbox.getline.ilp32", root,
                             "/tmp/tmp.fixture/getline_test32",
                             ["/tmp/tmp.fixture/getline_test32"]),
            ],
        }

    @staticmethod
    def receipt(identifier, cwd, executable, invocation):
        return {"variant": identifier, "cwd": cwd,
                "executable": {"path": executable, "sha256": "d" * 64},
                "invocation": invocation}

    def transform(self):
        original = sanitizer.canonical_bytes(self.report)
        digest = hashlib.sha256(original).hexdigest()
        return sanitizer.sanitize_report(original, digest)

    def test_known_paths_are_replaced_reproducibly(self):
        first = self.transform()
        self.assertEqual(first, self.transform())
        result = json.loads(first)
        receipts = {item["variant"]: item for item in result["variants"]}
        self.assertEqual(receipts["libc.base.native"]["cwd"], "tests/libc_contracts")
        self.assertEqual(receipts["dd.filesystem.ilp32"]["invocation"],
                         ["sh", "tests/dd_contracts/filesystem_test.sh",
                          "tests/dd_contracts/dd_host"])
        self.assertEqual(receipts["shell.posix.ilp32"]["executable"]["path"],
                         "bin/sh/tests/out/sh")
        self.assertEqual(receipts["textbox.getline.ilp32"]["invocation"],
                         ["<test-temp>/getline_test32"])
        self.assertNotIn(b"/home/runner/", first)
        self.assertNotIn(b"/tmp/", first)

    def test_wrong_original_hash_and_duplicate_keys_fail(self):
        original = sanitizer.canonical_bytes(self.report)
        with self.assertRaisesRegex(ValueError, "original hosted report SHA"):
            sanitizer.sanitize_report(original, "0" * 64)
        duplicate = original.replace(b'"revision":', b'"revision":"x","revision":')
        with self.assertRaisesRegex(ValueError, "duplicate report key"):
            sanitizer.sanitize_report(duplicate, hashlib.sha256(duplicate).hexdigest())

    def test_unexpected_absolute_paths_and_invocations_fail(self):
        for mutation in ("foreign-cwd", "foreign-executable", "foreign-reason",
                         "digit-path", "underscore-path", "dot-path",
                         "wrong-harness", "wrong-getline"):
            with self.subTest(mutation=mutation):
                self.setUp()
                if mutation == "foreign-cwd":
                    self.report["variants"][0]["cwd"] = "/home/private/build"
                elif mutation == "foreign-executable":
                    self.report["variants"][0]["executable"]["path"] = "/tmp/x"
                elif mutation == "foreign-reason":
                    self.report["variants"][0]["reason"] = "trace:/etc/passwd"
                elif mutation in {"digit-path", "underscore-path", "dot-path"}:
                    path = {"digit-path": "/1/private", "underscore-path": "/_private/x",
                            "dot-path": "/.private/x"}[mutation]
                    self.report["variants"][0]["reason"] = path
                elif mutation == "wrong-harness":
                    self.report["variants"][1]["invocation"][1] = "/tmp/wrong.sh"
                else:
                    self.report["variants"][3]["invocation"][0] = "/tmp/other"
                with self.assertRaises(ValueError):
                    self.transform()

    def test_unexpected_root_invocation_and_duplicate_variants_fail(self):
        self.report["invocation"][1] = "TEST_EXECUTION_REPORT=/tmp/other.json"
        with self.assertRaisesRegex(ValueError, "report output path"):
            self.transform()
        self.setUp()
        self.report["variants"].append(copy.deepcopy(self.report["variants"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate hosted variant"):
            self.transform()


if __name__ == "__main__":
    unittest.main()
