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
"""Execute callable div, ldiv, lldiv and llabs with named faulty providers.

The fixture checks the three conditions that single out truncating division
and literal results at each width boundary. Five mutants edit a private copy
of one provider; each must fail under its own function name, status and
reason, so a compiler failure or an unrelated crash is not a rejection.
"""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

import netbsd_oracle

NAMES = ("div", "ldiv", "lldiv", "llabs", "imaxdiv")
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-O2"] + [
    "-D%s=tree_%s" % (name, name) for name in NAMES
]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
PASS = "integer division: 402471 checks passed\n"
REMAINDER = "result.rem = numerator % denominator;"
FLOORED = (
    REMAINDER + "\n\tif (result.rem != 0 && "
    "(result.rem < 0) != (denominator < 0)) {\n"
    "\t\tresult.rem += denominator;\n\t\tresult.quot--;\n\t}"
)
# provider, exit status, anchor, replacement, required reason, description
MUTANTS = (
    (
        "div",
        1,
        REMAINDER,
        "result.rem = numerator;",
        "identity",
        "div returns the numerator as its remainder",
    ),
    ("div", 1, REMAINDER, FLOORED, "sign", "div rounds the quotient toward negative infinity"),
    (
        "ldiv",
        2,
        REMAINDER,
        "result.rem = numerator;",
        "identity",
        "ldiv returns the numerator as its remainder",
    ),
    (
        "lldiv",
        3,
        "result.rem = wide.rem;",
        "result.rem = wide.quot;",
        "identity",
        "lldiv returns the quotient as its remainder",
    ),
    (
        "llabs",
        4,
        "return value < 0 ? -value : value;",
        "return value;",
        "llabs(-1)",
        "llabs preserves negative input",
    ),
)


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=90, check=False)
    require(
        result.returncode == 0 and not result.stderr,
        "integer-division compilation fails: " + result.stdout + result.stderr,
    )


def build(root, compiler, work, arm, replacement=None, foreign=None):
    machine = ARM if arm else []
    objects = list(foreign or [])
    for name in NAMES if foreign is None else ():
        source = (
            replacement[1]
            if replacement and replacement[0] == name
            else root / "lib/libc/stdlib" / (name + ".c")
        )
        obj = work / (name + ".o")
        command(compiler + FLAGS + machine + ["-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    fixture = work / "fixture.o"
    command(
        compiler
        + FLAGS
        + machine
        + ["-c", str(root / "tests/libc_contracts/integer_division_test.c"), "-o", str(fixture)]
    )
    objects.append(str(fixture))
    link = []
    if arm:
        support = work / "support.o"
        command(
            compiler
            + ARM
            + [
                "-std=gnu17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fomit-frame-pointer",
                "-c",
                str(root / "tests/libbn/qemu_support.c"),
                "-o",
                str(support),
            ]
        )
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    program = work / "integer_division"
    command(compiler + machine + objects + link + ["-o", str(program)])
    return program


def execute(program, arm):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if arm else []
    return subprocess.run(
        prefix + [str(program)], capture_output=True, text=True, timeout=90, check=False
    )


def netbsd(root, compiler, work, args):
    """Run the fixture over NetBSD's five members in place of the tree's.

    The fixture's conditions single out truncating division, so NetBSD's
    members pass exactly when they return the tree's results on every
    input the fixture draws.
    """
    try:
        _, report = netbsd_oracle.build(args.netbsd, compiler, args.arm, work, ["division"])
        objects = []
        for name in NAMES:
            obj = work / ("netbsd-%s.o" % name)
            netbsd_oracle.compile_member(
                args.netbsd, compiler, args.arm, work, "lib/libc/stdlib/%s.c" % name, obj
            )
            objects.append(obj)
    except netbsd_oracle.OracleError as error:
        raise SystemExit(str(error)) from error
    redefine = work / "netbsd-redefine"
    redefine.write_text("".join("%s tree_%s\n" % (name, name) for name in NAMES))
    objcopy = "arm-none-eabi-objcopy" if args.arm else "objcopy"
    for obj in objects:
        command([objcopy, "--redefine-syms", str(redefine), str(obj)])
    program = build(root, compiler, work, args.arm, foreign=[str(o) for o in objects])
    result = execute(program, args.arm)
    require(
        result.returncode == 0 and not result.stderr and result.stdout == PASS,
        "NetBSD's division members fail the contract: " + result.stdout + result.stderr,
    )
    print(report)
    print("integer division: NetBSD's div, ldiv, lldiv, llabs and imaxdiv pass the same contract")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument(
        "--netbsd", type=Path, help="src-netbsd checkout whose members must also pass"
    )
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    with tempfile.TemporaryDirectory(prefix="integer-division-") as temporary:
        work = Path(temporary)
        program = build(root, compiler, work, args.arm)
        result = execute(program, args.arm)
        require(
            result.returncode == 0 and not result.stderr and result.stdout == PASS,
            "integer-division contract fails: " + result.stdout + result.stderr,
        )
        print(result.stdout, end="")
        accepted = work / "accepted"
        shutil.copy2(program, accepted)
        for name, status, anchor, change, reason, description in MUTANTS:
            original = (root / "lib/libc/stdlib" / (name + ".c")).read_text()
            require(original.count(anchor) == 1, name + " mutation anchor differs")
            source = work / "mutant.c"
            source.write_text(original.replace(anchor, change))
            program = build(root, compiler, work, args.arm, (name, source))
            result = execute(program, args.arm)
            require(
                result.returncode == status
                and result.stdout.startswith("FAIL " + name + "(")
                and reason in result.stdout,
                name + " mutant lacks its named rejection: " + result.stdout + result.stderr,
            )
            print("integer-division mutant rejected: " + description)
        if args.netbsd is not None:
            netbsd(root, compiler, work, args)
        if args.output:
            shutil.copy2(accepted, args.output)


if __name__ == "__main__":
    main()
