#!/usr/bin/env python3
"""Compile and reject focused sysconf contract mutations."""

from __future__ import annotations

import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


class MutationError(RuntimeError):
    """Report a missing mutation site or an incorrectly accepted mutant."""


def replace_once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise MutationError(f"{label}: expected one mutation site")
    return source.replace(old, new, 1)


def compile_and_run(
    compiler: list[str], root: Path, source: str, work: Path, label: str
) -> int:
    mutated_source = work / f"sysconf-{label}.c"
    executable = work / f"runtime-limits-{label}"
    mutated_source.write_text(source, encoding="utf-8")
    command = compiler + [
        "-std=c17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Wold-style-definition",
        "-nostdinc",
        "-isystem",
        str(root / "include"),
        "-Dsysconf=db_sysconf",
        "-Dconfstr=db_confstr",
        "-Derrno=runtime_limits_errno",
        str(root / "tests/libc_contracts/runtime_limits_contract_test.c"),
        str(mutated_source),
        str(root / "lib/libc/gen/confstr.c"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True)
    return subprocess.run([str(executable)], check=False).returncode


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: runtime_limits_mutate.py ROOT HOSTCC", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    source = (root / "lib/libc/gen/sysconf.c").read_text(encoding="utf-8")
    mutations = {
        "device-errno": replace_once(
            source,
            "\t\tif (selector == _SC_DEVICE_CONTROL)\n"
            "\t\t\treturn _POSIX_DEVICE_CONTROL;",
            "\t\tif (selector == _SC_DEVICE_CONTROL) {\n"
            "\t\t\terrno = EINVAL;\n"
            "\t\t\treturn _POSIX_DEVICE_CONTROL;\n"
            "\t\t}",
            "device-errno",
        ),
        "invalid-errno": replace_once(
            source,
            "\terrno = EINVAL;\n\treturn -1;",
            "\treturn -1;",
            "invalid-errno",
        ),
        "processor-count": replace_once(
            source,
            "\t\tif (selector <= _SC_NPROCESSORS_ONLN)\n\t\t\treturn 1;",
            "\t\tif (selector <= _SC_NPROCESSORS_ONLN)\n\t\t\treturn 2;",
            "processor-count",
        ),
        "signal-count": replace_once(
            source,
            "\t\t\treturn 1L << ((selector - _SC_NPROCESSORS_ONLN) * 5);",
            "\t\t\treturn 1L << ((selector - _SC_NPROCESSORS_ONLN) * 4);",
            "signal-count",
        ),
        "page-size": replace_once(
            source,
            "\t\t\treturn 1L << ((selector - _SC_NPROCESSORS_ONLN) * 5);",
            "\t\t\treturn selector == _SC_PAGESIZE ? DEV_BSIZE / 2 : NSIG;",
            "page-size",
        ),
    }

    with tempfile.TemporaryDirectory(prefix="runtime-limits-") as temporary:
        work = Path(temporary)
        if compile_and_run(compiler, root, source, work, "known-good") != 0:
            raise MutationError("known-good runtime limit source failed")
        for label, mutated in mutations.items():
            if compile_and_run(compiler, root, mutated, work, label) == 0:
                raise MutationError(f"{label}: known-bad mutation passed")
            print(f"PASS runtime-limits mutation {label} rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
