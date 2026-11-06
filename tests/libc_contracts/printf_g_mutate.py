#!/usr/bin/env python3
"""Rebuild the tree's float formatter with one defect at a time and require
printf_g_contract_test to reject each.

usage: printf_g_mutate.py ROOT HOSTCC TREE_CFLAGS TEST_SOURCE OBJECT...

OBJECT names the compiled tree formatter members other than
lib/libc/stdio/doprnt_float.c, which this script recompiles from each mutated
copy of its source with TREE_CFLAGS.
"""

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


def build_and_run(
    compiler: list[str],
    tree_flags: list[str],
    test_source: str,
    objects: list[str],
    root: Path,
    source: str,
    work: Path,
    label: str,
) -> int:
    mutated_source = work / f"doprnt_float-{label}.c"
    mutated_object = work / f"doprnt_float-{label}.o"
    executable = work / f"printf-g-{label}"
    mutated_source.write_text(source, encoding="utf-8")
    subprocess.run(
        compiler + tree_flags + ["-c", "-o", str(mutated_object), str(mutated_source)],
        check=True,
    )
    subprocess.run(
        compiler
        + ["-std=c17", "-Wall", "-Wextra", "-Werror", "-o", str(executable), test_source]
        + objects
        + [str(mutated_object), "-lm"],
        check=True,
    )
    return subprocess.run([str(executable)], check=False, stdout=subprocess.DEVNULL).returncode


def main() -> int:
    if len(sys.argv) < 6:
        print(
            "usage: printf_g_mutate.py ROOT HOSTCC TREE_CFLAGS TEST_SOURCE OBJECT...",
            file=sys.stderr,
        )
        return 2
    root = Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    tree_flags = shlex.split(sys.argv[3])
    test_source = sys.argv[4]
    objects = sys.argv[5:]
    source = (root / "lib/libc/stdio/doprnt_float.c").read_text(encoding="utf-8")
    mutations = {
        # Digits after the leading zeros of a value below 1 are counted
        # against the precision, so fewer than P significant digits print.
        "leading-zeros-significant": replace_once(
            source,
            "\t\tfor (i = -ex - 1; i > 0; --i)\n\t\t\t*t++ = '0';\n\t\tfor (i = 0; i < nd; ++i)",
            "\t\tfor (i = -ex - 1; i > 0; --i)\n\t\t\t*t++ = '0';\n"
            "\t\tfor (i = 0; i < nd + ex + 1; ++i)",
            "leading-zeros-significant",
        ),
        "style-e-below-minus-4": replace_once(
            source,
            "if (ex < -4 || ex >= prec)",
            "if (ex < -3 || ex >= prec)",
            "style-e-below-minus-4",
        ),
        "style-e-at-precision": replace_once(
            source,
            "if (ex < -4 || ex >= prec)",
            "if (ex < -4 || ex > prec)",
            "style-e-at-precision",
        ),
        "trailing-zeros-kept": replace_once(
            source,
            "\tif (! sharpflag)\n\t\twhile (nd > 1",
            "\tif (0)\n\t\twhile (nd > 1",
            "trailing-zeros-kept",
        ),
        "precision-zero": replace_once(
            source,
            "\tif (prec < 1)\n\t\tprec = 1;\n",
            "\tif (prec < 0)\n\t\tprec = 1;\n\tif (prec == 0)\n\t\tprec = 6;\n",
            "precision-zero",
        ),
        "sharp-point-dropped": replace_once(
            source,
            "\t\tif (nd > 1 || sharpflag)\n\t\t\t*t++ = '.';\n\t\tfor (i = 1;",
            "\t\tif (nd > 1)\n\t\t\t*t++ = '.';\n\t\tfor (i = 1;",
            "sharp-point-dropped",
        ),
        "sharp-zeros-dropped": replace_once(
            source,
            "\t\tif (sharpflag)\n\t\t\t*extrap = prec - nd;\n\t\tt = exponent",
            "\t\tt = exponent",
            "sharp-zeros-dropped",
        ),
        # A carry into a new decade leaves the exponent where it was, so
        # the style is chosen for 9.96 as though it were 9.96.
        "carry-exponent-not-advanced": replace_once(
            source,
            "\t\t\t\t\t*end = '1';\n\t\t\t\t\t++*exp;\n",
            "\t\t\t\t\t*end = '1';\n",
            "carry-exponent-not-advanced",
        ),
    }

    with tempfile.TemporaryDirectory(prefix="printf-g-") as temporary:
        work = Path(temporary)
        if (
            build_and_run(
                compiler, tree_flags, test_source, objects, root, source, work, "known-good"
            )
            != 0
        ):
            raise MutationError("known-good doprnt_float.c failed the %g contract")
        for label, mutated in mutations.items():
            if (
                build_and_run(
                    compiler, tree_flags, test_source, objects, root, mutated, work, label
                )
                == 0
            ):
                raise MutationError(f"{label}: known-bad mutation passed")
            print(f"PASS printf-g mutation {label} rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
