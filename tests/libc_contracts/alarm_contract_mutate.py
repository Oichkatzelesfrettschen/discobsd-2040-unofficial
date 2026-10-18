#!/usr/bin/env python3
"""Prove the alarm contract rejects rounding and errno mutations."""

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


def build_and_run(root, compiler, test_source, directory, source, name):
    mutated_source = directory / f"alarm_{name}.c"
    mutated_object = directory / f"alarm_{name}.o"
    executable = directory / f"alarm_{name}_bad"
    mutated_source.write_text(source, encoding="utf-8")
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
            "-Derrno=alarm_test_errno",
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
    return subprocess.run(executable, capture_output=True, text=True)


def main():
    require(len(sys.argv) == 4, "usage: alarm_contract_mutate.py ROOT CC TEST")
    root = pathlib.Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    test_source = pathlib.Path(sys.argv[3]).resolve()
    source = (root / "lib/libc/gen/alarm.c").read_text(encoding="utf-8")
    rounding_anchor = "if (oitv.it_value.tv_usec != 0)"
    failure_anchor = (
        "if (setitimer(ITIMER_REAL, itp, &oitv) < 0)\n"
        "\t\treturn ((unsigned int)-1);"
    )
    require(
        source.count(rounding_anchor) == 1,
        "alarm rounding mutation anchor differs",
    )
    require(source.count(failure_anchor) == 1, "alarm errno mutation anchor differs")
    rounding_mutant = source.replace(rounding_anchor, "if (0)")
    errno_mutant = source.replace(
        failure_anchor,
        "if (setitimer(ITIMER_REAL, itp, &oitv) < 0) {\n"
        "\t\textern int errno;\n"
        "\n"
        "\t\terrno = 0;\n"
        "\t\treturn ((unsigned int)-1);\n"
        "\t}",
    )

    with tempfile.TemporaryDirectory(prefix="alarm-contract-") as name:
        directory = pathlib.Path(name)
        result = build_and_run(
            root,
            compiler,
            test_source,
            directory,
            rounding_mutant,
            "rounding",
        )
        require(result.returncode == 1, "alarm rounding mutant was accepted")
        require(
            "rounds a nonzero prior microsecond upward" in result.stderr,
            "alarm rounding mutant failed for an unexpected reason",
        )
        result = build_and_run(
            root,
            compiler,
            test_source,
            directory,
            errno_mutant,
            "errno",
        )
        require(result.returncode == 1, "alarm errno mutant was accepted")
        require(
            "preserves errno from setitimer" in result.stderr,
            "alarm errno mutant failed for an unexpected reason",
        )
    print("PASS alarm contract: rounding and errno mutants rejected")


if __name__ == "__main__":
    main()
