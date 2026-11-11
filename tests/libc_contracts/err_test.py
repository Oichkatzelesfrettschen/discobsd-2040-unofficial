"""Exercise the production err family with native va_list and subprocess exits."""

import errno
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CC = shlex.split(os.environ.get("HOST_CC", "cc"))
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-Wold-style-definition", "-Wstrict-prototypes"]
NAMES = ["err", "errx", "warn", "warnx", "verr", "verrx", "vwarn", "vwarnx"]


def main():
    with tempfile.TemporaryDirectory(prefix="err-contract-") as directory:
        work = Path(directory)
        shutil.copy(ROOT / "include/err.h", work / "err.h")
        shim = work / "native.h"
        shim.write_text("extern char *__progname;\n")
        command = CC + FLAGS + ["-I" + str(work), "-include", str(shim),
                                "-D__progname=tree_progname", "-DERR_PROBE_HOST_PROGNAME"]
        command += ["-D" + name + "=tree_" + name for name in NAMES]
        source = ROOT / "lib/libc/gen/err.c"
        probe = ROOT / "tests/libc_contracts/err_probe.c"
        binary = work / "probe"
        subprocess.run(command + [str(source), str(probe), "-o", str(binary)], check=True)
        for mode in range(8):
            for null in (False, True):
                result = subprocess.run([str(binary), str(mode)] + (["null"] if null else []),
                                        capture_output=True, text=True, check=False)
                message = "err-probe: " + ("" if null else "37 word 1.25")
                if mode % 2 == 0:
                    message += ("" if null else ": ") + os.strerror(errno.ENOENT)
                status = {0: 21, 1: 22, 4: 23, 5: 24}.get(mode, 19)
                actual = (result.returncode, result.stdout, result.stderr)
                assert actual == (status, "", message + "\n"), (mode, null, actual)
        # Calibrate both syntax and owning-header type rejection.
        for name, text in {
            "old-style": "int old(x) int x; { return x; }\n",
            "wrong-va-list": '#include <err.h>\nvoid (*bad)(int, const char *, int) = verr;\n',
        }.items():
            bad = work / (name + ".c")
            bad.write_text(text)
            result = subprocess.run(command + ["-fsyntax-only", str(bad)], capture_output=True)
            assert result.returncode != 0, name
    print("err family: 16 stderr/status/forwarding cases and 2 compiler controls passed")


if __name__ == "__main__":
    main()
