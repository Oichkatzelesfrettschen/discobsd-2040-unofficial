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

TEST_DIRECTORY = "tests/libc_contracts"
TIMEOUT_SECONDS = 20


class MutationError(RuntimeError):
    """Report a missing mutation site or an incorrectly accepted mutant."""


def replace_once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise MutationError(f"{label}: expected one mutation site")
    return source.replace(old, new, 1)


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
    try:
        return subprocess.run(
            [str(executable)],
            check=False,
            stdout=subprocess.DEVNULL,
            timeout=TIMEOUT_SECONDS,
        ).returncode
    except subprocess.TimeoutExpired:
        return -1


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
