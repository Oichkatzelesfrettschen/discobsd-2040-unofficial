"""Reject false compiler success even when the Python oracle is optimized."""

import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECK = Path(__file__).with_name("assert_check.py")


class AssertionOracleTest(unittest.TestCase):
    def run_check(self, compiler):
        environment = dict(os.environ, HOSTCC=compiler, PYTHONOPTIMIZE="1")
        return subprocess.run([sys.executable, str(CHECK)], env=environment,
                              capture_output=True, text=True)

    def test_optimized_python_accepts_correct_contract(self):
        result = self.run_check(os.environ.get("HOSTCC", "cc"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("assert contract: PASS", result.stdout)

    def test_optimized_python_rejects_false_compiler_success(self):
        with tempfile.TemporaryDirectory(prefix="assert-oracle-") as directory:
            wrapper = Path(directory) / "compiler.sh"
            wrapper.write_text(
                '#!/bin/sh\nset -eu\n'
                'for argument do\n'
                '  case "$argument" in\n'
                '    */static_assert_false.c)\n'
                '      echo "discobsd false constant" >&2\n'
                '      exit 0 ;;\n'
                '  esac\n'
                'done\n'
                'exec "$@"\n',
                encoding="utf-8",
            )
            compiler = shlex.split(os.environ.get("HOSTCC", "cc"))
            result = self.run_check(shlex.join(["sh", str(wrapper), *compiler]))
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("false static_assert compiled", result.stderr)


if __name__ == "__main__":
    unittest.main()
