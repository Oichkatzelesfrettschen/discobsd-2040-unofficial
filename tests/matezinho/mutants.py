#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Mutants of the dirty-column mechanism that the matezinho gates must reject.

Each mutant edits a copy of the sources, builds it, and must fail for the
reason named in its entry. The unmutated copy runs first and must pass, so a
failure of a mutant is attributable to its edit.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CC = "cc"
FILES = [
    "usr.bin/matezinho/core.c",
    "usr.bin/matezinho/main.c",
    "usr.bin/matezinho/matezinho.h",
    "sys/arch/rp2040/dev/lcdfont.c",
    "sys/arch/rp2040/dev/lcdfont.h",
    "tests/matezinho/core_test.c",
]

# (name, file, [(old, new)], gate, stderr text that names the failing check)
MUTANTS = [
    (
        "mark ignores x and reports one unit",
        "usr.bin/matezinho/core.c",
        [
            ("lo=x/MZ_XUNIT;", "lo=0;"),
            ("hi=(x+w+MZ_XUNIT-1)/MZ_XUNIT;", "hi=1;"),
        ],
        "core_test",
        "diff ",
    ),
    (
        "mark replaces the range instead of widening it",
        "usr.bin/matezinho/core.c",
        [("if (s->xlo[i]>=s->xhi[i]) {", "if (1) {")],
        "core_test",
        "diff ",
    ),
    (
        "mark reports every band as full width",
        "usr.bin/matezinho/core.c",
        [("lo=x/MZ_XUNIT;", "lo=0;"), ("hi=(x+w+MZ_XUNIT-1)/MZ_XUNIT;", "hi=MZ_COLS;")],
        "core_test",
        "reported_units<",
    ),
    (
        "mz_render leaves the range dirty",
        "usr.bin/matezinho/core.c",
        [("s->xlo[i]=s->xhi[i]=0;", "(void)0;")],
        "core_test",
        "emitted<40",
    ),
    (
        "the PPM sink writes the range at column 0",
        "usr.bin/matezinho/main.c",
        [("(y+j)*960L+x0*3L", "(y+j)*960L")],
        "frames",
        "dirty-column frame differs",
    ),
]


def stage(work, mutation):
    """Copy the sources into work and apply (file, replacements) when given."""
    for name in FILES:
        target = work / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / name, target)
    if mutation:
        name, edits = mutation
        path = work / name
        text = path.read_text(encoding="utf-8")
        for old, new in edits:
            if text.count(old) != 1:
                raise SystemExit(f"mutants: {name} lacks exactly one {old!r}")
            text = text.replace(old, new)
        path.write_text(text, encoding="utf-8")


def build_and_run(work, gate, python):
    """Return (exit code, stderr) of the gate named for a staged tree."""
    # A mutant that leaves a variable unused still compiles; only a run may reject it.
    flags = ["-std=gnu17", "-g", "-D_DEFAULT_SOURCE"]
    if gate == "core_test":
        binary = work / "core_test"
        subprocess.run(
            [CC, *flags, "-fsanitize=address,undefined", "-o", binary]
            + [work / n for n in ("tests/matezinho/core_test.c", "usr.bin/matezinho/core.c")]
            + [work / "sys/arch/rp2040/dev/lcdfont.c"],
            check=True,
        )
        run = subprocess.run([binary], capture_output=True, text=True, timeout=120)
        return run.returncode, run.stderr
    binary = work / "matezinho"
    subprocess.run(
        [CC, *flags, "-o", binary]
        + [work / n for n in ("usr.bin/matezinho/main.c", "usr.bin/matezinho/core.c")]
        + [work / "sys/arch/rp2040/dev/lcdfont.c"],
        check=True,
    )
    check = ROOT / "tests/matezinho/check.py"
    run = subprocess.run(
        [python, check, "--binary", binary], capture_output=True, text=True, timeout=300
    )
    return run.returncode, run.stderr


def main():
    python = sys.executable
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory) / "base"
        stage(base, None)
        for gate in ("core_test", "frames"):
            code, err = build_and_run(base, gate, python)
            if code != 0:
                raise SystemExit(f"mutants: unmutated {gate} fails: {err[-400:]}")
        for index, (name, file, edits, gate, reason) in enumerate(MUTANTS):
            work = Path(directory) / f"m{index}"
            stage(work, (file, edits))
            code, err = build_and_run(work, gate, python)
            if code == 0:
                failures.append(f"survived: {name}")
            elif reason not in err:
                failures.append(f"failed for another reason: {name}: {err[-300:]}")
            else:
                print(f"rejected: {name}")
    if failures:
        raise SystemExit("matezinho mutants: " + "; ".join(failures))
    print(f"matezinho mutants: {len(MUTANTS)} rejected, each for its named reason")


if __name__ == "__main__":
    main()
