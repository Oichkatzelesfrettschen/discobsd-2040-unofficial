#!/usr/bin/env python3
"""Calibrate the recovery-utility stack-usage verifier."""

import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    checker = Path(__file__).with_name("check_stack_usage.py")
    with tempfile.TemporaryDirectory(prefix="realpath-stack-verifier-") as temporary:
        temporary_directory = Path(temporary)
        production = temporary_directory / "production.su"
        known_bad = temporary_directory / "known-bad.su"
        production.write_text("realpath.c:1:1:main\t64\tdynamic\n", encoding="utf-8")
        known_bad.write_text("known_bad.c:1:1:main\t1024\tstatic\n", encoding="utf-8")
        result = subprocess.run(
            [
                sys.executable,
                str(checker),
                "--maximum-bytes",
                "704",
                "--production",
                str(production),
                "--known-bad",
                str(known_bad),
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        if result.returncode == 0:
            raise SystemExit("dynamic production stack frame passed calibration")
        if "non-static production frame" not in result.stderr:
            raise SystemExit("dynamic production rejection lacks its deciding diagnostic")
    print("realpath stack verifier calibration: dynamic frame rejected")


if __name__ == "__main__":
    main()
