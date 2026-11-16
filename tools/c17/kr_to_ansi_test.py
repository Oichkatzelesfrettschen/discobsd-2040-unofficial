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
"""Calibrate kr_to_ansi.py on a known-good file and a known-bad file.

The known-good fixture holds five definitions the converter must rewrite
(identifier lists, a register parameter, an array parameter, an empty list and
an implicit-int parameter). Its output must equal the expected text exactly,
a second run must change nothing, the host compiler must accept the output
under -Werror=old-style-definition and -Werror=strict-prototypes, and the
.text section of the output must equal the .text section of the input
compiled as K&R, which shows the rewrite preserves the object code.

The known-bad fixture holds five definitions the converter must leave
byte-identical and report as refused: a typedef of unknown width (u_char), a
char parameter, a float parameter, a function-pointer parameter and a comment
inside the declaration list. A
mutant converter that ignores promotion must change the float definition's
object code, which shows the refusal protects the program, and every mutant
below must fail at least one check:

  - a converter without the promotion refusal;
  - a converter that emits parameters in reverse order;
  - a converter that leaves the declaration list in place.

Run: python3 tools/c17/kr_to_ansi_test.py
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = (HERE / "kr_to_ansi.py").read_text()

GOOD = """\
static int
add(a, b)
	int a;
	int b;
{
	return a + b;
}

char *
advance(p, n)
	register char *p;
	unsigned n;
{
	return p + n;
}

int
many(argc, argv, q, r)
	int argc;
	char *argv[], **q;
	long r;
{
	return argc + (int)r + (q != 0) + (argv != 0);
}

static int
zero()
{
	return 0;
}

int
first(argc, argv)
	char **argv;
{
	return argc + (argv != 0);
}
"""

GOOD_EXPECTED = """\
static int
add(int a, int b)
{
	return a + b;
}

char *
advance(register char *p, unsigned n)
{
	return p + n;
}

int
many(int argc, char *argv[], char **q, long r)
{
	return argc + (int)r + (q != 0) + (argv != 0);
}

static int
zero(void)
{
	return 0;
}

int
first(int argc, char **argv)
{
	return argc + (argv != 0);
}
"""

BAD = """\
typedef unsigned char u_char;

int
tiny(c)
	u_char c;
{
	return c;
}

int
narrow(c)
	char c;
{
	return c;
}

double
widen(x)
	float x;
{
	return x;
}

int
call(f)
	int (*f)();
{
	return f();
}

int
annotated(a)
	int a;	/* kept as written */
{
	return a;
}
"""

BAD_REASONS = [
    "tiny: refused: promoted type",
    "narrow: refused: promoted type",
    "widen: refused: promoted type",
    "call: refused: function or function-pointer parameter",
    "annotated: refused: comment, literal or directive",
]

BASE = ["-O1", "-w", "-fpermissive", "-c"]


def load(source):
    namespace = {"__name__": "kr_to_ansi_under_test"}
    exec(compile(source, "kr_to_ansi.py", "exec"), namespace)  # noqa: S102
    return namespace


def text_section(work, name, source, std):
    c_file = work / f"{name}.c"
    obj = work / f"{name}.o"
    raw = work / f"{name}.bin"
    c_file.write_text(source)
    built = subprocess.run(
        ["cc", f"-std={std}", *BASE, str(c_file), "-o", str(obj)], capture_output=True
    )
    if built.returncode != 0:
        return None
    subprocess.run(
        ["objcopy", "-O", "binary", "--only-section=.text", str(obj), str(raw)], check=True
    )
    return raw.read_bytes()


def strict_compile(work, source):
    c_file = work / "strict.c"
    c_file.write_text(source)
    result = subprocess.run(
        [
            "cc",
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-Werror=old-style-definition",
            "-Werror=strict-prototypes",
            "-c",
            str(c_file),
            "-o",
            str(work / "strict.o"),
        ],
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def checks(module, work):
    """Return the names of the checks the converter fails."""
    failed = []
    good_out, good_findings = module["convert"](GOOD)
    if good_out != GOOD_EXPECTED:
        failed.append("good output text")
    if [s for _, _, s in good_findings] != ["converted"] * 5:
        failed.append("good findings")
    if module["convert"](good_out)[0] != good_out:
        failed.append("good idempotence")
    if not strict_compile(work, good_out):
        failed.append("good strict compile")
    before = text_section(work, "before", GOOD, "gnu17")
    after = text_section(work, "after", good_out, "c17")
    if before is None or before != after:
        failed.append("good object identity")
    bad_out, bad_findings = module["convert"](BAD)
    if bad_out != BAD:
        failed.append("bad left unchanged")
    report = [f"{n}: {s}" for _, n, s in bad_findings]
    if not all(any(r.startswith(want) for r in report) for want in BAD_REASONS):
        failed.append("bad reasons")
    return failed


def main():
    for tool in ("cc", "objcopy"):
        if shutil.which(tool) is None:
            print(f"kr_to_ansi_test: not run: {tool} is missing", file=sys.stderr)
            return 2
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        failed = checks(load(SOURCE), work)
        if failed:
            print(f"kr_to_ansi_test: FAIL: the converter fails {failed}", file=sys.stderr)
            return 1
        forced = SOURCE.replace(
            "def promoted(base, declarator):\n",
            "def promoted(base, declarator):\n    return None\n",
        )
        mutants = {
            "no promotion refusal": forced,
            "reversed parameters": SOURCE.replace(
                "new_params.append(format_param(base, declarator))",
                "new_params.insert(0, format_param(base, declarator))",
            ),
            "declaration list kept": SOURCE.replace(
                "edits.append((m.start(1), brace, header", "edits.append((m.start(1), after, header"
            ),
        }
        for label, mutated in mutants.items():
            if mutated == SOURCE:
                print(f"kr_to_ansi_test: FAIL: mutant '{label}' did not apply", file=sys.stderr)
                return 1
            if not checks(load(mutated), work):
                print(f"kr_to_ansi_test: FAIL: mutant '{label}' survived", file=sys.stderr)
                return 1
        # The promotion refusal protects the program: forcing the float rewrite changes the code.
        out, _ = load(forced)["convert"](BAD)
        if out == BAD:
            print("kr_to_ansi_test: FAIL: the forced rewrite changed nothing", file=sys.stderr)
            return 1
        bad_before = text_section(work, "bad_before", BAD, "gnu17")
        if bad_before is None or bad_before == text_section(work, "bad_after", out, "c17"):
            print("kr_to_ansi_test: FAIL: promotion rewrite kept the object code", file=sys.stderr)
            return 1
    print("kr_to_ansi_test: ok: 1 known-good, 1 known-bad, 3 mutants killed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
