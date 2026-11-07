#!/usr/bin/env python3
"""Rebuild the tree's math library with one defect at a time and require
huge_val_contract_test to reject each.

usage: huge_val_mutate.py ROOT HOSTCC TREE_CFLAGS HEADER_CFLAGS OBJECT... -- SOURCE...

SOURCE names the tree files under ROOT that the test links, relative to ROOT;
this script compiles each from a scratch copy of the tree files it mutates.
OBJECT names the formatter objects the Makefile already built. A mutant that
hangs counts as rejected.
"""

from __future__ import annotations

import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import test_execution  # noqa: E402

TEST_DIRECTORY = "tests/libc_contracts"
TIMEOUT_SECONDS = 20


class MutationError(RuntimeError):
    """Report a missing mutation site or an incorrectly accepted mutant."""


def replace_once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise MutationError(f"{label}: expected one mutation site")
    return source.replace(old, new, 1)


def run_executable(executable: Path) -> int:
    """Run the contract test as an owned process group (tools/test_execution.py
    run_owned), so a hang is stopped whole; a run past the deadline is reported
    as -1, which counts as rejected. A SIGINT or SIGTERM of this script stops
    the group before the script exits."""
    returncode, timed_out = test_execution.run_owned(
        [str(executable)], TIMEOUT_SECONDS, stdout=subprocess.DEVNULL
    )
    return -1 if timed_out else returncode


def build_and_run(
    root: Path,
    compiler: list[str],
    tree_flags: list[str],
    header_flags: list[str],
    objects: list[str],
    sources: list[str],
    overrides: dict[str, str],
    work: Path,
    label: str,
) -> int:
    """Compile with `overrides` (relative path -> text) replacing tree files."""
    include = work / "include"
    include.mkdir(exist_ok=True)
    test_directory = root / TEST_DIRECTORY
    built: list[str] = []
    for relative in sources:
        text = overrides.get(relative)
        path = root / relative
        if text is not None:
            path = work / f"{label}-{Path(relative).name}"
            path.write_text(text, encoding="utf-8")
        output = work / f"{label}-{Path(relative).stem}.o"
        subprocess.run(
            compiler + tree_flags + [f"-I{include}", "-c", "-o", str(output), str(path)],
            check=True,
            cwd=test_directory,
        )
        built.append(str(output))
    header_object = work / f"{label}-header.o"
    subprocess.run(
        compiler
        + [f"-I{include}"]
        + header_flags
        + ["-c", "-o", str(header_object), str(test_directory / "huge_val_header.c")],
        check=True,
        cwd=test_directory,
    )
    executable = work / f"huge-val-{label}"
    subprocess.run(
        compiler
        + [
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-o",
            str(executable),
            str(test_directory / "huge_val_contract_test.c"),
            str(test_directory / "huge_val_stubs.c"),
            str(header_object),
        ]
        + built
        + [str(test_directory / name) for name in objects]
        + ["-lm"],
        check=True,
        cwd=test_directory,
    )
    return run_executable(executable)


def main() -> int:
    separator = sys.argv.index("--") if "--" in sys.argv else -1
    if len(sys.argv) < 7 or separator < 0:
        print(__doc__, file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    tree_flags = shlex.split(sys.argv[3])
    header_flags = shlex.split(sys.argv[4])
    objects = sys.argv[5:separator]
    sources = sys.argv[separator + 1 :]

    def text(relative: str) -> str:
        return (root / relative).read_text(encoding="utf-8")

    math_h = text("include/math.h")
    ldexp_c = text("lib/libc/gen/ldexp.c")
    frexp_c = text("lib/libc/gen/frexp.c")
    log_c = text("lib/libm/log.c")
    atof_c = text("lib/libc/gen/atof.c")
    strtod_c = text("lib/libc/stdlib/strtod.c")
    exp_c = text("lib/libm/exp.c")
    pow_c = text("lib/libm/pow.c")
    j0_c = text("lib/libm/j0.c")
    j1_c = text("lib/libm/j1.c")
    jn_c = text("lib/libm/jn.c")
    mutations = {
        "ldexp-overflow-finite": {
            "lib/libc/gen/ldexp.c": replace_once(
                ldexp_c,
                "\t\t\treturn(HUGE_VAL);",
                "\t\t\treturn(DBL_MAX);",
                "ldexp-overflow-finite",
            )
        },
        "ldexp-overflow-without-erange": {
            "lib/libc/gen/ldexp.c": replace_once(
                ldexp_c,
                "\t\terrno = ERANGE;\n",
                "",
                "ldexp-overflow-without-erange",
            )
        },
        "frexp-loops-on-infinity": {
            "lib/libc/gen/frexp.c": replace_once(
                frexp_c,
                "\tif (x != x || x - x != 0) {\n\t\t*i = 0;\n\t\treturn(x);\n\t}\n",
                "",
                "frexp-loops-on-infinity",
            )
        },
        "log-zero-positive": {
            "lib/libm/log.c": replace_once(
                log_c,
                "return(-HUGE_VAL);",
                "return(HUGE_VAL);",
                "log-zero-positive",
            )
        },
        "atof-zero-mantissa-nan": {
            "lib/libc/gen/atof.c": replace_once(
                atof_c,
                "\tif (fl == 0)\n\t\treturn(neg < 0 ? -fl : fl);\n",
                "",
                "atof-zero-mantissa-nan",
            )
        },
        "strtod-overflow-without-erange": {
            "lib/libc/stdlib/strtod.c": replace_once(
                strtod_c,
                "\t\t\terrno = ERANGE;\n",
                "\t\t\t;\n",
                "strtod-overflow-without-erange",
            )
        },
        "exp-overflow-finite": {
            "lib/libm/exp.c": replace_once(
                exp_c,
                "\t\treturn(HUGE_VAL);",
                "\t\treturn(DBL_MAX);",
                "exp-overflow-finite",
            )
        },
        # C17 7.12.1p4: a pole is a range error. Each mutant reports the
        # pole as a domain error.
        "log-pole-domain": {
            "lib/libm/log.c": replace_once(
                log_c,
                "errno = arg == 0. ? ERANGE : EDOM;",
                "errno = EDOM;",
                "log-pole-domain",
            )
        },
        "y0-pole-domain": {
            "lib/libm/j0.c": replace_once(
                j0_c,
                "errno = arg == 0. ? ERANGE : EDOM;",
                "errno = EDOM;",
                "y0-pole-domain",
            )
        },
        "y1-pole-domain": {
            "lib/libm/j1.c": replace_once(
                j1_c,
                "errno = x == 0. ? ERANGE : EDOM;",
                "errno = EDOM;",
                "y1-pole-domain",
            )
        },
        "yn-pole-domain": {
            "lib/libm/jn.c": replace_once(
                jn_c,
                "errno = x == 0 ? ERANGE : EDOM;",
                "errno = EDOM;",
                "yn-pole-domain",
            )
        },
        "pow-pole-domain": {
            "lib/libm/pow.c": replace_once(
                pow_c,
                "\t\t\tif(arg2 < 0.) {\n\t\t\t\terrno = ERANGE;\n"
                "\t\t\t\treturn(HUGE_VAL);\n\t\t\t}\n",
                "\t\t\tif(arg2 < 0.)\n\t\t\t\tgoto domain;\n",
                "pow-pole-domain",
            )
        },
        "pow-zero-to-zero": {
            "lib/libm/pow.c": replace_once(
                pow_c,
                "\t\t\tif(arg2 == 0.)\n\t\t\t\treturn(1.);\n",
                "\t\t\tif(arg2 == 0.)\n\t\t\t\tgoto domain;\n",
                "pow-zero-to-zero",
            )
        },
        # exp: the reduction through arg * log2e alone, 5e-14 off near 706
        # and an infinity one argument early at ln(DBL_MAX); an overflow
        # guard under ln(DBL_MAX); an underflow guard above the results
        # ldexp still represents. An overflow guard at 770 is not a mutant:
        # ldexp returns the same HUGE_VAL and ERANGE for every argument
        # between ln(DBL_MAX) and 770, so no observation separates the two.
        "exp-imprecise-reduction": {
            "lib/libm/exp.c": replace_once(
                exp_c,
                "\tent = floor(arg*log2e);\n\tfract = ((arg - ent*ln2hi) - ent*ln2lo)*log2e - 0.5;",
                "\targ *= log2e;\n\tent = floor(arg);\n\tfract = (arg-ent) - 0.5;",
                "exp-imprecise-reduction",
            )
        },
        "exp-overflow-guard-low": {
            "lib/libm/exp.c": replace_once(
                exp_c,
                "maxf	= 709.782712893384;",
                "maxf	= 709.7;",
                "exp-overflow-guard-low",
            )
        },
        "exp-underflow-guard-high": {
            "lib/libm/exp.c": replace_once(
                exp_c,
                "minf	= -745.14;",
                "minf	= -709.0;",
                "exp-underflow-guard-high",
            )
        },
        "finite-huge-val": {},
    }

    with tempfile.TemporaryDirectory(prefix="huge-val-") as temporary:
        work = Path(temporary)

        def run(label: str, overrides: dict[str, str]) -> int:
            return build_and_run(
                root,
                compiler,
                tree_flags,
                header_flags,
                objects,
                sources,
                overrides,
                work,
                label,
            )

        if run("known-good", {}) != 0:
            raise MutationError("known-good tree sources failed the huge-val contract")
        for label, overrides in mutations.items():
            if label == "finite-huge-val":
                # A math.h earlier on the include path that defines the
                # finite value, as the tree header did, for the libm sources
                # and for the header constants alike.
                (work / "include").mkdir(exist_ok=True)
                mutated = math_h
                start = mutated.index("#define HUGE_VAL")
                end = mutated.index("\n", start) + 1
                (work / "include" / "math.h").write_text(
                    mutated[:start] + "#define HUGE_VAL 3.40282347e+38\n" + mutated[end:],
                    encoding="utf-8",
                )
                overrides = {}
            status = run(label, overrides)
            (work / "include" / "math.h").unlink(missing_ok=True)
            if status == 0:
                raise MutationError(f"{label}: known-bad mutation passed")
            print(f"PASS huge-val mutation {label} rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
