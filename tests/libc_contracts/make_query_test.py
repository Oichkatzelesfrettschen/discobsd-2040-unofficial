"""Require detached compiler-flag queries to own their make environment."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import qsort_callers

ROOT = Path(__file__).resolve().parents[2]
FLAGS = "-j 2 -J 987,988"


class MakeQueryTest(unittest.TestCase):
    def environment(self, variables):
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        environment.update({name: FLAGS for name in variables})
        return environment

    def test_closed_jobserver_control(self):
        environment = self.environment(("MAKEFLAGS", "MFLAGS"))
        result = subprocess.run(
            ["bmake", "MACHINE=rp2040", "-V", "${CC}"],
            cwd=ROOT / "bin/ls",
            env=environment,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # A bmake that warns about inherited job-server descriptors it cannot
        # use is the hazard the queries below are isolated from. An older
        # bmake (Ubuntu 24.04's) takes them without a word, so there the
        # hazard is absent and the control has nothing to show.
        if result.stderr == "" and result.stdout.strip():
            self.skipTest("this bmake accepts closed job-server descriptors silently")
        self.assertIn('Invalid internal option "-J"', result.stderr)

    def test_sort_caller_query(self):
        query = ["bmake", "MACHINE=rp2040", "-V", "${CC}", "-V", "${CFLAGS}"]
        with mock.patch.dict(os.environ, self.environment(()), clear=True):
            expected = qsort_callers.run(query, cwd=ROOT / "bin/ls")
        for variables in (("MAKEFLAGS",), ("MFLAGS",), ("MAKEFLAGS", "MFLAGS")):
            with (
                self.subTest(variables=variables),
                mock.patch.dict(os.environ, self.environment(variables), clear=True),
            ):
                try:
                    output = qsort_callers.run(query, cwd=ROOT / "bin/ls")
                except RuntimeError as error:
                    self.fail(str(error))
                self.assertEqual(len(output.splitlines()), 2)
                self.assertIn("-std=gnu17", output)
                self.assertEqual(output, expected)

    def test_usercopy_query(self):
        with tempfile.TemporaryDirectory(prefix="discobsd-make-query-") as temporary:
            for variables in (("MAKEFLAGS",), ("MFLAGS",), ("MAKEFLAGS", "MFLAGS")):
                with self.subTest(variables=variables):
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(ROOT / "tests/rp2040/usercopy_span/check.py"),
                            "--root",
                            str(ROOT),
                            "--output",
                            str(Path(temporary) / "result.json"),
                        ],
                        env=self.environment(variables),
                        capture_output=True,
                        text=True,
                        timeout=90,
                        check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stderr, "")
                    self.assertIn("13 calibrated cases", result.stdout)


if __name__ == "__main__":
    unittest.main()
