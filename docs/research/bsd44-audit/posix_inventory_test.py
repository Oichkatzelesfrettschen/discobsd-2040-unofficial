#!/usr/bin/env python3
"""Calibrate the bounded POSIX API candidate inventory."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "posix_inventory.py"
FIXTURES = HERE / "fixtures" / "posix-inventory"


def run_inventory(discobsd_root, bsd44_root):
    python = os.environ.get("PYTHON")
    if not python:
        raise RuntimeError("PYTHON names the selected interpreter")
    return subprocess.run(
        [
            python,
            str(SCRIPT),
            "--discobsd-root",
            str(discobsd_root),
            "--bsd44-root",
            str(bsd44_root),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


class InventoryTest(unittest.TestCase):
    def setUp(self):
        self.declared = FIXTURES / "declared-only"
        self.implemented = FIXTURES / "implemented"

    def test_rejects_missing_root(self):
        result = run_inventory(self.declared / "missing", self.implemented)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("discobsd root is not a directory", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_rejects_incomplete_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = run_inventory(temporary, self.implemented)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("discobsd root lacks required subtree", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_distinguishes_evidence_layers(self):
        result = run_inventory(self.declared, self.implemented)
        self.assertEqual(result.returncode, 0, result.stderr)
        document = json.loads(result.stdout)
        self.assertEqual(document["schema_version"], 1)

        records = {record["name"]: record for record in document["apis"]}
        declared = records["declared_api"]
        self.assertEqual(declared["discobsd"]["declaration"]["state"], "present")
        self.assertEqual(
            declared["discobsd"]["symbol"]["state"],
            "absent_from_scanned_scope",
        )
        self.assertEqual(declared["discobsd"]["linkage"]["state"], "not_assessed")
        self.assertEqual(declared["discobsd"]["behavior"]["state"], "not_assessed")

        self.assertEqual(
            records["comma_first"]["discobsd"]["declaration"]["state"],
            "present",
        )
        self.assertEqual(
            records["comma_second"]["discobsd"]["declaration"]["state"],
            "present",
        )
        self.assertEqual(
            records["pointer_first"]["discobsd"]["declaration"]["state"],
            "present",
        )
        self.assertEqual(
            records["pointer_second"]["discobsd"]["declaration"]["state"],
            "present",
        )

        implemented = records["implemented_api"]
        self.assertEqual(implemented["bsd44"]["declaration"]["state"], "present")
        self.assertEqual(implemented["bsd44"]["symbol"]["state"], "present")
        self.assertEqual(
            implemented["bsd44"]["symbol"]["evidence"],
            [{"line": 2, "path": "lib/libc/different_filename.c"}],
        )
        for name in (
            "conditional_first",
            "conditional_second",
            "conditional_after",
        ):
            self.assertEqual(records[name]["bsd44"]["symbol"]["state"], "present")
        self.assertNotIn("comment_api", records)
        self.assertNotIn("string_api", records)

    def test_output_is_path_independent_and_deterministic(self):
        first = run_inventory(self.declared, self.implemented)
        second = run_inventory(self.declared, self.implemented)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertNotIn(str(FIXTURES), first.stdout)


if __name__ == "__main__":
    unittest.main()
