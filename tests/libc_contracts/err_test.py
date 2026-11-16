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
"""Exercise the production err family with native va_list and subprocess exits.

The fixture compiles lib/libc/gen/err.c against the owning include/err.h as
strict C17 and runs eight entry points with formatted and null messages. Each
mutant below edits a private copy of err.c and must fail either the compile or
one of the sixteen stderr/status cases, which shows the cases and the
-Wold-style-definition and -Wstrict-prototypes flags can reject.
"""

import errno
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CC = shlex.split(os.environ.get("HOST_CC", "cc"))
FLAGS = [
    "-std=c17",
    "-pedantic-errors",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wold-style-definition",
    "-Wstrict-prototypes",
]
NAMES = ["err", "errx", "warn", "warnx", "verr", "verrx", "vwarn", "vwarnx"]

# Each anchor occurs once in err.c; the mutation breaks one entry's contract.
MUTANTS = {
    "verr-exit-status-ignored": (
        "exit(eval);\n}\n\nvoid\nerr (int",
        "exit(eval + 1);\n}\n\nvoid\nerr (int",
    ),
    "verrx-exit-status-ignored": (
        "exit(eval);\n}\n\nvoid\nerrx",
        "exit(eval + 1);\n}\n\nvoid\nerrx",
    ),
    "verr-loses-saved-errno": (
        "(void)fputs(strerror(sverrno), stderr);\n\t(void)fputc('\\n', stderr);\n\texit(eval);",
        "(void)fputs(strerror(sverrno + 1), stderr);\n\t(void)fputc('\\n', stderr);\n\texit(eval);",
    ),
    "vwarn-drops-separator": (
        "putcolsp();\n\t}\n\t(void)fputs(strerror(sverrno), stderr);\n"
        "\t(void)fputc('\\n', stderr);\n}\n\nvoid\nwarn (",
        "}\n\t(void)fputs(strerror(sverrno), stderr);\n"
        "\t(void)fputc('\\n', stderr);\n}\n\nvoid\nwarn (",
    ),
    "vwarnx-drops-newline": (
        "(void)fputc('\\n', stderr);\n}\n\nvoid\nwarnx",
        "\n}\n\nvoid\nwarnx",
    ),
    "vwarnx-old-style-definition": (
        "vwarnx(const char *fmt, va_list ap)",
        "vwarnx(fmt, ap)\n\tconst char *fmt;\n\tva_list ap;",
    ),
}


def compile_probe(work, command, source, name):
    """Return the probe binary, or None when the compiler rejects the source."""
    binary = work / name
    probe = ROOT / "tests/libc_contracts/err_probe.c"
    result = subprocess.run(
        command + [str(source), str(probe), "-o", str(binary)], capture_output=True, check=False
    )
    return binary if result.returncode == 0 else None


def mismatches(binary):
    """Run every entry with a formatted and a null message; list the failures."""
    failures = []
    for mode in range(8):
        for null in (False, True):
            result = subprocess.run(
                [str(binary), str(mode)] + (["null"] if null else []),
                capture_output=True,
                text=True,
                check=False,
            )
            message = "err-probe: " + ("" if null else "37 word 1.25")
            if mode % 2 == 0:
                message += ("" if null else ": ") + os.strerror(errno.ENOENT)
            status = {0: 21, 1: 22, 4: 23, 5: 24}.get(mode, 19)
            actual = (result.returncode, result.stdout, result.stderr)
            if actual != (status, "", message + "\n"):
                failures.append((mode, null, actual))
    return failures


def main():
    with tempfile.TemporaryDirectory(prefix="err-contract-") as directory:
        work = Path(directory)
        shutil.copy(ROOT / "include/err.h", work / "err.h")
        shim = work / "native.h"
        shim.write_text("extern char *__progname;\n")
        command = CC + FLAGS + ["-I" + str(work), "-include", str(shim)]
        command += ["-D" + name + "=tree_" + name for name in NAMES]
        source = ROOT / "lib/libc/gen/err.c"
        binary = compile_probe(work, command, source, "probe")
        if binary is None:
            raise RuntimeError("err.c does not compile as strict C17 against err.h")
        failures = mismatches(binary)
        if failures:
            raise RuntimeError("err family cases failed: " + repr(failures))
        # Calibrate: each mutant must fail the compile or a case.
        text = source.read_text()
        for label, (old, new) in MUTANTS.items():
            if text.count(old) != 1:
                raise RuntimeError("mutation anchor is not unique: " + label)
            mutated = work / (label + ".c")
            mutated.write_text(text.replace(old, new))
            built = compile_probe(work, command, mutated, label)
            if built is not None and not mismatches(built):
                raise RuntimeError("mutant survived: " + label)
        # Calibrate both syntax and owning-header type rejection.
        for name, body in {
            "old-style": "int old(x) int x; { return x; }\n",
            "wrong-va-list": "#include <err.h>\nvoid (*bad)(int, const char *, int) = verr;\n",
        }.items():
            bad = work / (name + ".c")
            bad.write_text(body)
            result = subprocess.run(command + ["-fsyntax-only", str(bad)], capture_output=True)
            if result.returncode == 0:
                raise RuntimeError("compiler control accepted: " + name)
    print(
        "err family: 16 stderr/status/forwarding cases, "
        + str(len(MUTANTS))
        + " mutants and 2 compiler controls passed"
    )


if __name__ == "__main__":
    main()
