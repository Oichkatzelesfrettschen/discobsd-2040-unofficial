"""Calibrate execution verdicts with real processes and absent invocations."""

import contextlib
import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import test_execution as execution


class ExecutionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-execution-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.receipts = self.directory / "receipts"
        self.receipts.mkdir()
        self.environment = patch.dict(os.environ, {
            "TEST_EXECUTION_DIR": str(self.receipts),
            "TEST_EXECUTION_REQUIRED": "no", "REQUIRE_ILP32": "no",
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.output = io.StringIO()
        self.stdout = contextlib.redirect_stdout(self.output)
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)
        self.stderr = contextlib.redirect_stderr(self.output)
        self.stderr.__enter__()
        self.addCleanup(self.stderr.__exit__, None, None, None)

    def variant(self, name="fixture", width="native", expected_exit=0):
        return {"id": name, "owner": "fixture/job", "width": width,
                "capability": "fixture runtime", "program": name,
                "directory": str(self.directory), "expected_exit": expected_exit}

    def compile(self, variant, returncode, flags=()):
        source = self.directory / (variant["program"] + ".c")
        source.write_text(f"int main(void) {{ return {returncode}; }}\n")
        executable = self.directory / variant["program"]
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        subprocess.run([*command, "-std=c17", "-Wall", "-Wextra", "-Werror", *flags,
                        str(source), "-o", str(executable)], check=True)
        return [str(executable)]

    def record(self, variant):
        return json.loads((self.receipts / (variant["id"] + ".json")).read_text())

    def test_executed_pass_and_failure(self):
        for name, returncode, outcome in [("good", 0, "PASS"), ("bad", 1, "FAIL")]:
            variant = self.variant(name)
            command = self.compile(variant, returncode)
            self.assertEqual(execution.run_variant(variant, command), returncode)
            record = self.record(variant)
            self.assertEqual(record["outcome"], outcome)
            self.assertEqual(record["invocation"], command)
            self.assertEqual(record["returncode"], returncode)
            self.assertEqual(len(record["executable"]["sha256"]), 64)

    def test_negative_control_requires_exact_rejection(self):
        for name, returncode, outcome in [("rejected", 1, "PASS"), ("accepted", 0, "FAIL"),
                                         ("infrastructure", 127, "ERROR")]:
            variant = self.variant(name, expected_exit=1)
            execution.run_variant(variant, self.compile(variant, returncode))
            self.assertEqual(self.record(variant)["outcome"], outcome)

    def test_optional_skip_and_required_error(self):
        optional = self.variant("optional", "ilp32")
        self.assertEqual(execution.skip_variant(optional, "capability absent"), 0)
        self.assertEqual(self.record(optional)["outcome"], "SKIP")
        for setting in ("REQUIRE_ILP32", "TEST_EXECUTION_REQUIRED"):
            required = self.variant(setting.lower(), "ilp32")
            with patch.dict(os.environ, {setting: "yes"}):
                self.assertEqual(execution.skip_variant(required, "capability absent"), 2)
            record = self.record(required)
            self.assertEqual(record["outcome"], "ERROR")
            self.assertIsNone(record["invocation"])

    def test_missing_executable_is_error(self):
        variant = self.variant()
        self.assertEqual(execution.run_variant(variant, [str(self.directory / "fixture")]), 2)
        self.assertEqual(self.record(variant)["outcome"], "ERROR")

    def test_symlinked_directory_resolves_both_executable_paths(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        alias = self.directory / "directory-alias"
        alias.symlink_to(self.directory, target_is_directory=True)
        variant["directory"] = str(alias)
        self.assertEqual(execution.run_variant(variant, command), 0)
        self.assertEqual(self.record(variant)["outcome"], "PASS")

    def test_same_named_executable_in_another_directory_is_rejected(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        variant["directory"] = str(self.directory / "different")
        with self.assertRaisesRegex(ValueError, "different executable"):
            execution.run_variant(variant, command)

    def test_width_mismatch_is_error(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        # Force a non-ILP32 header independently of the calibration host's width.
        Path(command[0]).write_bytes(b"\x7fELF\x02")
        variant["width"] = "ilp32"
        self.assertEqual(execution.run_variant(variant, command), 2)
        self.assertEqual(self.record(variant)["outcome"], "ERROR")

    def test_duplicate_receipt_cannot_overwrite(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        self.assertEqual(execution.run_variant(variant, command), 0)
        with self.assertRaises(FileExistsError):
            execution.run_variant(variant, command)

    def test_missing_and_unexecuted_receipts_fail_collection(self):
        variant = self.variant()
        variants = {variant["id"]: variant}
        self.assertEqual(execution.collect(variants, self.receipts)[0]["outcome"], "ERROR")
        execution.receipt(variant, "PASS")
        self.assertEqual(execution.collect(variants, self.receipts)[0]["outcome"], "ERROR")

    def test_exit_zero_aggregate_without_binary_execution_fails(self):
        report = self.directory / "report.json"
        self.assertEqual(execution.aggregate(shutil.which("true"), report), 1)
        records = json.loads(report.read_text())["variants"]
        self.assertTrue(records)
        self.assertTrue(all(record["outcome"] == "ERROR" for record in records))

    def test_probe_executes_and_checks_width(self):
        probe = execution.ROOT / "tools/compiler-probe.sh"
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        good = subprocess.run(["sh", str(probe), "native", *compiler],
                              text=True, capture_output=True, check=True)
        self.assertEqual(good.stdout.strip(), "yes")
        # A successful compiler command that produces no executable is insufficient.
        absent = subprocess.run(["sh", str(probe), "ilp32", shutil.which("true")],
                                text=True, capture_output=True, check=True)
        self.assertEqual(absent.stdout.strip(), "no")
        unsupported = subprocess.run(["sh", str(probe), "ilp32", shutil.which("false")],
                                     text=True, capture_output=True, check=True)
        self.assertEqual(unsupported.stdout.strip(), "no")

    def test_detached_make_preserves_command_line_overrides(self):
        observer = self.directory / "make-observer"
        observed = self.directory / "make-arguments.json"
        observer.write_text(
            f"#!{sys.executable}\nimport json, os, sys\n"
            f"with open({str(observed)!r}, 'w') as output:\n"
            "    json.dump({'args': sys.argv[1:], 'flags': "
            "[os.getenv('MAKEFLAGS'), os.getenv('MFLAGS')]}, output)\n"
        )
        observer.chmod(0o755)
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        overrides = ["HOSTCC=cc -O1", "HOST_CC=cc -O2", "CC=cc -O3",
                     "HOST_CFLAGS=-DOVERRIDE=1 -g"]
        result = subprocess.run(
            ["bmake", "-j4", "-C", str(execution.ROOT), "MACHINE=rp2040",
             f"MAKE={observer}", f"PYTHON={sys.executable}", *overrides,
             "REQUIRE_ILP32=no", "TEST_EXECUTION_REQUIRED=no",
             "TEST_EXECUTION_DIR=wrong-directory", "check-ilp32-execution"],
            env=environment, text=True, capture_output=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("required executable recipe produced no receipt",
                      result.stdout + result.stderr)
        record = json.loads(observed.read_text())
        for override in overrides:
            self.assertIn(override, record["args"])
        effective = dict(argument.split("=", 1) for argument in record["args"]
                         if "=" in argument)
        self.assertEqual(effective["REQUIRE_ILP32"], "yes")
        self.assertEqual(effective["TEST_EXECUTION_REQUIRED"], "yes")
        self.assertNotEqual(effective["TEST_EXECUTION_DIR"], "wrong-directory")
        self.assertEqual(record["flags"], [None, None])

    def test_runtime_failure_is_unavailable(self):
        failing = self.compile(self.variant("runtime-failure"), 1)[0]
        compiler = self.directory / "compiler"
        compiler.write_text(
            "#!/bin/sh\nset -eu\n"
            'while [ "$1" != -o ]; do shift; done\n'
            f'cp {shlex.quote(failing)} "$2"\n'
        )
        compiler.chmod(0o755)
        result = subprocess.run(
            ["sh", str(execution.ROOT / "tools/compiler-probe.sh"), "native", str(compiler)],
            text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "no")

    def test_actual_make_recipe_optional_and_required_capability(self):
        # A failing compiler drives the real capability branch without rebuilding
        # or deleting any product in the checkout that runs the calibration.
        command = ["bmake", "-C", str(execution.ROOT / "tests/backgammon_contracts"),
                   "MACHINE=rp2040", "HOSTCC=false", "check"]
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        environment.pop("TEST_EXECUTION_DIR", None)
        environment["PYTHON"] = os.environ["PYTHON"]
        optional = subprocess.run(command, env=environment, text=True,
                                  capture_output=True, check=False)
        self.assertEqual(optional.returncode, 0, optional.stdout + optional.stderr)
        self.assertIn("SKIP backgammon.ilp32", optional.stdout)
        environment["REQUIRE_ILP32"] = "yes"
        required = subprocess.run(command, env=environment, text=True,
                                  capture_output=True, check=False)
        self.assertNotEqual(required.returncode, 0)
        self.assertIn("ERROR backgammon.ilp32", required.stdout)

    def test_native_output_cannot_satisfy_width_probe(self):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        result = subprocess.run(
            ["sh", str(execution.ROOT / "tools/compiler-probe.sh"), "ilp32", *compiler,
             "-Dint=short"], text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "no")

    def test_stdio_bounds_recipe_preserves_compiler_argument_boundary(self):
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        result = subprocess.run(
            ["bmake", "-n", "-C", str(execution.ROOT / "tests/libc_contracts"),
             "MACHINE=rp2040", "HOSTCC=cc -O1", "check-stdio-bounds"],
            env=environment, text=True, capture_output=True, check=True,
        )
        recipe = next(line for line in result.stdout.splitlines()
                      if line.startswith("sh ") and "stdio_bounds.sh" in line)
        self.assertEqual(shlex.split(recipe)[-2:], ["cc -O1", "stdio_bounds_work"])

    def test_required_undefined_sanitizer_rejects_recoverable_fault(self):
        source = self.directory / "undefined.c"
        source.write_text(
            "#include <limits.h>\n"
            "int main(void) { volatile int value = INT_MAX; "
            "volatile int result = value + 1; (void)result; return 0; }\n"
        )
        executable = self.directory / "undefined"
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        process = subprocess.run(
            [*compiler, "-std=c17", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=undefined", "-fno-sanitize-recover=all", str(source),
             "-o", str(executable)], text=True, capture_output=True, check=False,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        result = subprocess.run([str(executable)], text=True, capture_output=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("runtime error", result.stderr)
        for path, variable in [("tests/libc_contracts/Makefile", "ZONE_SAN"),
                               ("tests/libc_sysctl/Makefile", "SAN")]:
            contents = (execution.ROOT / path).read_text().replace("\\\n", " ")
            flags = re.search(rf"^{variable}=([^\n]+)", contents, re.MULTILINE).group(1)
            self.assertIn("-fno-sanitize-recover=all", flags)

    def test_inventory_has_unique_owners_and_executable_recipes(self):
        variants = execution.inventory()
        for variant in variants.values():
            self.assertEqual(variant["owner"], "firmware/posix-sh")
            self.assertIn(variant["id"], variant["recipe"])
            self.assertTrue(variant["condition"])
            self.assertTrue(variant["prerequisites"])
            self.assertTrue((execution.ROOT / variant["makefile"]).is_file())

    def test_inventory_matches_executable_recipe_identifiers(self):
        variants = execution.inventory()
        paths = {variant["makefile"] for variant in variants.values()}
        paths.update(variant["script"] for variant in variants.values() if "script" in variant)
        observed = []
        for path in paths:
            contents = (execution.ROOT / path).read_text()
            observed.extend(re.findall(r"\brun ([a-z0-9_.-]+) --", contents))
        self.assertCountEqual(observed, variants)


if __name__ == "__main__":
    unittest.main()
