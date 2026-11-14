"""Calibrate check_time_binding.py on objects the cross compiler builds.

A unit at the default view that calls time() must pass; the same unit
built with _TIME_BITS defined as 32, and a unit that declares time() by
hand without the header, must each be rejected for the narrow `time`
reference. A host object is skipped rather than scanned, and an empty
directory, a header tree without declarations and an nm that fails each
end in an error instead of a pass.

ARM_GCC and ARM_NM select the cross tools; HOST_CC the host compiler.
"""

import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_time_binding as binding  # noqa: E402

ROOT = binding.ROOT
ARM_GCC = os.environ.get("ARM_GCC", "arm-none-eabi-gcc")
ARM_NM = os.environ.get("ARM_NM", "arm-none-eabi-nm")
HOST_CC = os.environ.get("HOST_CC", "cc")
TARGET_FLAGS = ["-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Os",
                "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft",
                "-nostdinc", "-I%s" % (ROOT / "include")]
HEADER_CALL = """#include <time.h>
long long stamp(void) { return (long long)time((time_t *)0); }
"""
HAND_DECLARED = """long time();
long stamp(void) { return time(0); }
"""


def compile_unit(compiler, flags, source, output):
    unit = output.with_suffix(".c")
    unit.write_text(source)
    subprocess.run([compiler] + flags + ["-c", "-o", str(output), str(unit)],
                   check=True)
    unit.unlink()


class TimeBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="time-binding-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)

    def directory(self, name):
        path = self.work / name
        path.mkdir()
        return path

    def run_check(self, *directories):
        return binding.check(ROOT, ARM_NM, list(directories))

    def test_default_view_passes(self):
        good = self.directory("good")
        compile_unit(ARM_GCC, TARGET_FLAGS, HEADER_CALL, good / "wide.o")
        violations, scanned, skipped, names = self.run_check(good)
        self.assertEqual(violations, [])
        self.assertEqual((scanned, skipped), (1, 0))
        self.assertIn("time", names)
        self.assertIn("stat", names)

    def test_narrow_view_is_rejected(self):
        bad = self.directory("narrow")
        compile_unit(ARM_GCC, TARGET_FLAGS + ["-D_TIME_BITS=32"], HEADER_CALL,
                     bad / "narrow.o")
        violations, _, _, _ = self.run_check(bad)
        self.assertEqual([symbol for _, symbol in violations], ["time"])

    def test_hand_declaration_is_rejected(self):
        bad = self.directory("hand")
        compile_unit(ARM_GCC, TARGET_FLAGS + ["-Wno-deprecated-non-prototype"],
                     HAND_DECLARED, bad / "hand.o")
        violations, _, _, _ = self.run_check(bad)
        self.assertEqual([symbol for _, symbol in violations], ["time"])

    def test_host_object_is_skipped(self):
        if shutil.which(HOST_CC) is None:
            self.skipTest("no host compiler %s" % HOST_CC)
        mixed = self.directory("mixed")
        compile_unit(HOST_CC, ["-std=gnu17", "-Wno-deprecated-non-prototype"],
                     HAND_DECLARED, mixed / "host.o")
        compile_unit(ARM_GCC, TARGET_FLAGS, HEADER_CALL, mixed / "wide.o")
        violations, scanned, skipped, _ = self.run_check(mixed)
        self.assertEqual((violations, scanned, skipped), ([], 1, 1))

    def test_empty_directory_is_an_error(self):
        with self.assertRaises(binding.BindingError):
            self.run_check(self.directory("empty"))

    def test_headers_without_declarations_are_an_error(self):
        tree = self.directory("bare")
        (tree / "include").mkdir()
        (tree / "include" / "time.h").write_text("long time();\n")
        with self.assertRaises(binding.BindingError):
            binding.narrow_names(tree)

    def test_failing_nm_is_an_error(self):
        good = self.directory("nmfail")
        compile_unit(ARM_GCC, TARGET_FLAGS, HEADER_CALL, good / "wide.o")
        with self.assertRaises(binding.BindingError):
            binding.check(ROOT, "false", [good])

    def test_main_exit_status(self):
        bad = self.directory("exit")
        compile_unit(ARM_GCC, TARGET_FLAGS + ["-D_TIME_BITS=32"], HEADER_CALL,
                     bad / "narrow.o")
        report = io.StringIO()
        with contextlib.redirect_stdout(report), contextlib.redirect_stderr(report):
            self.assertEqual(binding.main(["--nm", ARM_NM, str(bad)]), 1)
            self.assertEqual(binding.main(["--nm", ARM_NM,
                                           str(self.directory("none"))]), 2)
        self.assertIn("references the 32-bit entry time", report.getvalue())
        self.assertIn("ERROR: no ARM relocatable object", report.getvalue())


if __name__ == "__main__":
    unittest.main()
