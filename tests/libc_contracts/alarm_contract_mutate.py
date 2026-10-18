#!/usr/bin/env python3
"""Prove the alarm contract rejects truncation of a partial prior second."""

import pathlib
import shlex
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run(command):
    subprocess.run(command, check=True)


def main():
    require(len(sys.argv) == 4, "usage: alarm_contract_mutate.py ROOT CC TEST")
    root = pathlib.Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    test_source = pathlib.Path(sys.argv[3]).resolve()
    source = (root / "lib/libc/gen/alarm.c").read_text(encoding="utf-8")
    anchor = "if (oitv.it_value.tv_usec != 0)"
    require(source.count(anchor) == 1, "alarm rounding mutation anchor differs")
    mutated = source.replace(anchor, "if (0)")

    with tempfile.TemporaryDirectory(prefix="alarm-contract-") as name:
        directory = pathlib.Path(name)
        mutated_source = directory / "alarm_mutated.c"
        mutated_object = directory / "alarm_mutated.o"
        executable = directory / "alarm_rounding_bad"
        mutated_source.write_text(mutated, encoding="utf-8")
        common = [
            "-std=c17",
            "-pedantic-errors",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wstrict-prototypes",
            "-Wold-style-definition",
            "-Wmissing-prototypes",
        ]
        run(
            compiler
            + common
            + [
                "-nostdinc",
                "-isystem",
                str(root / "include"),
                "-Dalarm=db_alarm",
                "-Dsetitimer=test_setitimer",
                "-c",
                str(mutated_source),
                "-o",
                str(mutated_object),
            ]
        )
        run(
            compiler
            + common
            + [
                "-D_DEFAULT_SOURCE",
                str(test_source),
                str(mutated_object),
                "-o",
                str(executable),
            ]
        )
        result = subprocess.run(executable, capture_output=True, text=True)
        require(result.returncode == 1, "alarm rounding mutant was accepted")
        require(
            "rounds a nonzero prior microsecond upward" in result.stderr,
            "alarm rounding mutant failed for an unexpected reason",
        )
    print("PASS alarm contract: partial-second mutant rejected")


if __name__ == "__main__":
    main()
