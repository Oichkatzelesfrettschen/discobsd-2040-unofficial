#!/usr/bin/env python3
"""Rebuild the tree's float formatter with one defect at a time and require
printf_g_contract_test to reject each.

usage: printf_g_mutate.py ROOT HOSTCC TREE_CFLAGS TEST_SOURCE OBJECT...

OBJECT names the compiled tree formatter members other than
lib/libc/stdio/doprnt.c and lib/libc/stdio/doprnt_float.c, which this script
recompiles from each mutated copy of its source with TREE_CFLAGS. A mutant
that hangs counts as rejected.
"""

from __future__ import annotations

import os
import shlex
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

TIMEOUT_SECONDS = 120


class MutationError(RuntimeError):
    """Report a missing mutation site or an incorrectly accepted mutant."""


def replace_once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise MutationError(f"{label}: expected one mutation site")
    return source.replace(old, new, 1)


def run_in_group(command: list[str], output: Path) -> int:
    """Run command in its own process group with its output in `output`; a run
    past the deadline is killed with its group and reported as -1, which counts
    as rejected."""
    with output.open("wb") as stream:
        process = subprocess.Popen(command, stdout=stream, start_new_session=True)
        try:
            return process.wait(timeout=TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            return -1


def build_and_run(
    compiler: list[str],
    tree_flags: list[str],
    test_source: str,
    objects: list[str],
    sources: dict[str, str],
    work: Path,
    label: str,
) -> int:
    """Compile the formatter members in `sources` (name -> text) and run the test."""
    built: list[str] = []
    for name, text in sources.items():
        mutated_source = work / f"{name}-{label}.c"
        mutated_object = work / f"{name}-{label}.o"
        mutated_source.write_text(text, encoding="utf-8")
        subprocess.run(
            compiler + tree_flags + ["-c", "-o", str(mutated_object), str(mutated_source)],
            check=True,
        )
        built.append(str(mutated_object))
    executable = work / f"printf-g-{label}"
    subprocess.run(
        compiler
        + ["-std=c17", "-Wall", "-Wextra", "-Werror", "-o", str(executable), test_source]
        + objects
        + built
        + ["-lm"],
        check=True,
    )
    return run_in_group([str(executable)], work / f"printf-g-{label}.out")


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
    doprnt = (root / "lib/libc/stdio/doprnt.c").read_text(encoding="utf-8")
    source = (root / "lib/libc/stdio/doprnt_float.c").read_text(encoding="utf-8")

    def float_member(text: str) -> dict[str, str]:
        return {"doprnt": doprnt, "doprnt_float": text}

    def format_member(text: str) -> dict[str, str]:
        return {"doprnt": text, "doprnt_float": source}

    mutations = {
        # %g: digits after the leading zeros of a value below 1 are counted
        # against the precision, so fewer than P significant digits print.
        "leading-zeros-significant": float_member(
            replace_once(
                source,
                "\t\tt = s + 1 - ex + nd;",
                "\t\tt = s + nd + 2;",
                "leading-zeros-significant",
            )
        ),
        "style-e-below-minus-4": float_member(
            replace_once(
                source,
                "if (ex < -4 || ex >= prec)",
                "if (ex < -3 || ex >= prec)",
                "style-e-below-minus-4",
            )
        ),
        "style-e-at-precision": float_member(
            replace_once(
                source,
                "if (ex < -4 || ex >= prec)",
                "if (ex < -4 || ex > prec)",
                "style-e-at-precision",
            )
        ),
        "trailing-zeros-kept": float_member(
            replace_once(
                source,
                "\tif (! sharpflag)\n\t\twhile (nd > 1",
                "\tif (0)\n\t\twhile (nd > 1",
                "trailing-zeros-kept",
            )
        ),
        "precision-zero": float_member(
            replace_once(
                source,
                "\tif (prec < 1)\n\t\tprec = 1;\n",
                "\tif (prec < 0)\n\t\tprec = 1;\n\tif (prec == 0)\n\t\tprec = 6;\n",
                "precision-zero",
            )
        ),
        "sharp-point-dropped": float_member(
            replace_once(
                source,
                "\t\tif (nd > 1 || sharpflag) {\n\t\t\tmemmove (s + 2",
                "\t\tif (nd > 1) {\n\t\t\tmemmove (s + 2",
                "sharp-point-dropped",
            )
        ),
        "sharp-zeros-dropped": float_member(
            replace_once(
                source,
                "\t\tif (sharpflag)\n\t\t\t*extrap = prec - nd;\n\t\tt = exponent",
                "\t\tt = exponent",
                "sharp-zeros-dropped",
            )
        ),
        # A carry into a new decade leaves the exponent where it was, so
        # the style is chosen for 9.96 as though it were 9.96.
        "carry-exponent-not-advanced": float_member(
            replace_once(
                source,
                "\t\t\t*first = '1';\n\t\t\t++ex;\n",
                "\t\t\t*first = '1';\n",
                "carry-exponent-not-advanced",
            )
        ),
        # Rounding, C17 7.21.6.1p13 and F.3: an exact tie goes to the even
        # digit. A five rounded up always, a tie that ignores the parity of
        # the kept digit, and a five taken for a tie whatever the remainder
        # of the exact value are the three ways a tie rule goes wrong.
        "tie-rounds-up": float_member(
            replace_once(
                source,
                "if (next < '5' || (next == '5' && ! drest (d) && ! ((*last - '0') & 1)))",
                "if (next < '5')",
                "tie-rounds-up",
            )
        ),
        "tie-ignores-parity": float_member(
            replace_once(
                source,
                "! ((*last - '0') & 1)))",
                "1))",
                "tie-ignores-parity",
            )
        ),
        "tie-ignores-remainder": float_member(
            replace_once(
                source,
                "next == '5' && ! drest (d)",
                "next == '5'",
                "tie-ignores-remainder",
            )
        ),
        # The digits stop at 11 significant digits and zeros follow, the
        # symptom of generating digits in double arithmetic.
        "digits-capped-at-eleven": float_member(
            replace_once(
                replace_once(
                    source,
                    "\t\troom = (int) (endp - first) - ndig - 1;\n",
                    "\t\troom = (int) (endp - first) - ndig - 1;\n"
                    "\t\tif (room > 11)\n\t\t\troom = 11;\n",
                    "digits-capped-at-eleven",
                ),
                "\t\troom = (int) (endp - first) - 7;\n",
                "\t\troom = (int) (endp - first) - 7;\n\t\tif (room > 11)\n\t\t\troom = 11;\n",
                "digits-capped-at-eleven",
            )
        ),
        "capital-f-is-style-e": float_member(
            replace_once(
                source,
                "if (fmtch == 'f' || fmtch == 'F')",
                "if (fmtch == 'f')",
                "capital-f-is-style-e",
            )
        ),
        # doprnt.c: the 0 flag survives a precision for a floating
        # conversion, and the sign of a negative zero prints.
        "zero-flag-dropped-by-precision": format_member(
            replace_once(
                doprnt,
                "\t\t\tpadding = zeroflag ? '0' : ' ';\n\t\t\t/*\n\t\t\t * cvt may",
                "\t\t\t/*\n\t\t\t * cvt may",
                "zero-flag-dropped-by-precision",
            )
        ),
        "negative-zero-unsigned": format_member(
            replace_once(
                doprnt,
                "\t\t\tif (sbits >> 63) {",
                "\t\t\tif (d < 0) {",
                "negative-zero-unsigned",
            )
        ),
        "long-modifier-widens-default-precision": format_member(
            replace_once(
                doprnt,
                "\t\t\t\tdwidth = 6;",
                "\t\t\t\tdwidth = (sz == SZ_LONG ? DBL_DIG : FLT_DIG);",
                "long-modifier-widens-default-precision",
            )
        ),
        "zero-flag-pads-nan": format_member(
            replace_once(
                doprnt,
                "\t\t\t\textrazeros = 0;\n\t\t\t\tpadding = ' ';\n\t\t\t\tif (isnan (d))",
                "\t\t\t\textrazeros = 0;\n\t\t\t\tif (isnan (d))",
                "zero-flag-pads-nan",
            )
        ),
    }

    with tempfile.TemporaryDirectory(prefix="printf-g-") as temporary:
        work = Path(temporary)
        status = build_and_run(
            compiler, tree_flags, test_source, objects, float_member(source), work, "known-good"
        )
        if status != 0:
            # The first disagreeing cases, each with format, value bits, the
            # tree result and the expected text, are what the test printed.
            report = (work / "printf-g-known-good.out").read_text(
                encoding="utf-8", errors="replace"
            )
            print(report, file=sys.stderr)
            raise MutationError(
                f"known-good doprnt.c and doprnt_float.c failed the contract (status {status})"
            )
        for label, mutated in mutations.items():
            if build_and_run(compiler, tree_flags, test_source, objects, mutated, work, label) == 0:
                raise MutationError(f"{label}: known-bad mutation passed")
            print(f"PASS printf-g mutation {label} rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
