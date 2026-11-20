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
    compiler: list[str], root: Path, source: str, work: Path, label: str,
    path_source: Path | None = None,
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
        "-Dstatfs=db_statfs",
        "-Dfcntl=db_fcntl",
        "-Dfstatfs=db_fstatfs",
        "-Dpathconf=db_pathconf",
        "-Dfpathconf=db_fpathconf",
        str(path_source or root / "lib/libc/gen/pathconf.c"),
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
        "symlink-limit": replace_once(
            source, "return MAXSYMLINKS;", "return MAXSYMLINKS - 1;", "symlink-limit"
        ),
        "device-errno": replace_once(
            source, "return _POSIX_DEVICE_CONTROL;",
            "errno = EINVAL; return _POSIX_DEVICE_CONTROL;", "device-errno",
        ),
        "invalid-errno": replace_once(source, "errno = EINVAL;", "", "invalid-errno"),
        "processor-count": replace_once(source, "return 1;", "return 2;", "processor-count"),
        "signal-count": replace_once(source, "return NSIG;", "return NSIG - 1;", "signal-count"),
        "page-size": replace_once(
            source, "return DEV_BSIZE;", "return DEV_BSIZE / 2;", "page-size",
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
        path_source = (root / "lib/libc/gen/pathconf.c").read_text(encoding="utf-8")
        for original, replacement, label in (
            ("? V7_NAMELEN : MAXNAMLEN", "? MAXNAMLEN : V7_NAMELEN", "mount-format"),
            ("fstatfs(fd,", "fstatfs(fd < 0 ? 7 : fd,", "descriptor-forwarding"),
        ):
            mutant = work / f"pathconf-{label}.c"
            modified = replace_once(path_source, original, replacement, label)
            mutant.write_text(modified, encoding="utf-8")
            if compile_and_run(compiler, root, source, work, label, mutant) == 0:
                raise MutationError(f"{label}: known-bad mutation passed")
            print(f"PASS runtime-limits mutation {label} rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
