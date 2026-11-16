#!/usr/bin/env python3
# Extraction gate for the process-name choice of usr.bin/fstat/fstat.c. The
# gate copies proc_comm() out of the production source, compiles the copy
# under proc_comm_test.c and runs that test. A copy with the pid-0 branch
# removed must fail the test, and the gate fails when that mutant passes or
# when the branch text is not found exactly once.
#
# usage: proc_comm_gate.py TOPSRC      (HOSTCC selects the compiler)

import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

# HOSTCC may carry arguments ("cc -O1"), as a make variable does.
CC = shlex.split(os.environ.get("HOSTCC", "cc"))
FLAGS = ["-std=c17", "-pedantic-errors", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror"]
HEAD = "static const char *\nproc_comm("
END = "\n}\n"
SWAPPER = ("\tif (kp->kp_pid == 0 && kp->kp_comm[0] == '\\0')\n"
           "\t\treturn (\"swapper\");\n")
# Each pattern must match in sys/sys/sysctl.h, the header the test mirrors.
MIRROR = [
    (r"#define\s+KI_COMMSZ\s+64\b", "KI_COMMSZ"),
    (r"#define\s+KP_VALID_COMM\s+0x20\b", "KP_VALID_COMM"),
    (r"uint16_t\s+kp_valid;", "kp_valid"),
    (r"int16_t\s+kp_pid;", "kp_pid"),
    (r"char\s+kp_comm\[KI_COMMSZ\];", "kp_comm"),
]


def extract(text):
    if text.count(HEAD) != 1:
        sys.exit("proc_comm gate: fstat.c does not hold proc_comm() exactly once")
    start = text.index(HEAD)
    return text[start:text.index(END, start) + len(END)]


def build_and_run(work, name, body, test):
    source = work / (name + ".c")
    source.write_text(body)
    exe = work / name
    built = subprocess.run([*CC, *FLAGS, f'-DPROC_COMM_SRC="{source}"', "-o", str(exe),
                            str(test)], capture_output=True, text=True)
    if built.returncode != 0:
        sys.exit(f"proc_comm gate: {name} did not build\n{built.stderr}")
    return subprocess.run([str(exe)], capture_output=True, text=True)


def main():
    top = Path(sys.argv[1]).resolve()
    sysctl = (top / "sys" / "sys" / "sysctl.h").read_text()
    for pattern, what in MIRROR:
        if re.search(pattern, sysctl) is None:
            sys.exit(f"proc_comm gate: sys/sys/sysctl.h no longer matches the mirror of {what}")
    good = extract((top / "usr.bin" / "fstat" / "fstat.c").read_text())
    if good.count(SWAPPER) != 1:
        sys.exit("proc_comm gate: the pid-0 branch is not found exactly once")
    bad = good.replace(SWAPPER, "", 1)
    test = Path(__file__).resolve().parent / "proc_comm_test.c"
    with tempfile.TemporaryDirectory(prefix="fstat-proc-comm-") as tmp:
        work = Path(tmp)
        run = build_and_run(work, "proc_comm_good", good, test)
        if run.returncode != 0:
            sys.exit(f"proc_comm gate: the production copy fails its test\n{run.stderr}")
        print(run.stdout.strip())
        run = build_and_run(work, "proc_comm_bad", bad, test)
        if run.returncode == 0:
            sys.exit("proc_comm gate: the pid-0 mutant passed; the test cannot see that fault")
        if "swapper" not in run.stderr:
            sys.exit(f"proc_comm gate: the mutant failed on another case\n{run.stderr}")
        print("proc_comm: the pid-0 mutant is rejected: " + run.stderr.splitlines()[0])


if __name__ == "__main__":
    main()
