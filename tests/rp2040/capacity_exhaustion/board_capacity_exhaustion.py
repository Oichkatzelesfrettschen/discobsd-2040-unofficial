#!/usr/bin/env python3
"""Board test for fixed-table exhaustion and recovery (sys/arch/rp2040/doc/CAPACITY.md).

usage: DISCOBSD_BOARD_CAPACITY_EXHAUSTION=run $PYTHON board_capacity_exhaustion.py \
           LOG [unix.uf2,flash.uf2]

The script touches the board only when DISCOBSD_BOARD_CAPACITY_EXHAUSTION is
exactly "run"; unset, empty and every other value exit without opening a port
or calling discobsd-flash. With the variable set it flashes the named images
with discobsd-flash when given, reaches a root shell on the USB console and
stages tests/rp2040/capacity_exhaustion/filetab and proctab as uuencoded text
with the staging routines of tests/rp2040/user_window/board_user_window.py.
It then runs, in order:

1. filetab: a dup to the per-process limit refuses with EMFILE and leaves the
   file counter alone; holder processes fill the system file table; the next
   open refuses with ENFILE while the parent holds far fewer than NOFILE
   descriptors; the file failure counter rises by the refusals and no other
   counter moves; closing every holder restores occupancy and a later open
   succeeds (system-file-table-exhaustion in capacity-corpus.json);
2. proctab: children park until the process table is full; fork refuses with
   EAGAIN, not ENOMEM; the process failure counter rises by the refusals and
   no other counter moves; every child exits and is reaped; a later fork
   succeeds (process-table-exhaustion). A fork refused with ENOMEM before
   the table filled means swap ran out first; proctab then prints
   PROCTABTEST INCONCLUSIVE and this script reports the case as unreached;
3. a final kern.capacity snapshot whose process occupancy equals the starting
   value, so no child outlived its program.

Every console byte goes to LOG. The exit status is 0 when every check passed,
1 when one failed, 2 when the console stopped answering and 3 when only the
process-table case was unreached. The script records no result; the operator
retains LOG as the receipt.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time

OPT_IN_VARIABLE = "DISCOBSD_BOARD_CAPACITY_EXHAUSTION"
OPT_IN_VALUE = "run"
HERE = os.path.dirname(os.path.abspath(__file__))
WINDOW_DRIVER = os.path.join(HERE, "..", "user_window", "board_user_window.py")
STAGED = (
    ("filetab", "tests/rp2040/capacity_exhaustion/filetab"),
    ("proctab", "tests/rp2040/capacity_exhaustion/proctab"),
)
PROC_LINE = re.compile(
    r"^proc\s+limit (\d+) current (\d+) peak (\d+) min-free (\d+) failures (\d+)", re.M
)


def load_console():
    """Import the staging and console routines the user-window driver owns."""
    spec = importlib.util.spec_from_file_location("board_user_window", WINDOW_DRIVER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def reach_root(console):
    """Reach a root prompt from whatever the console shows."""
    prompt = re.compile(r"(login: ?|[$#] ?)$", re.M)
    for _ in range(8):
        console.port.write(b"\r")
        tail = console.read_for(8, prompt).rstrip()[-40:]
        if tail.endswith("#"):
            return console.run("id -u")[-1:] == ["0"]
        if tail.endswith("$"):
            console.send("su\r")
            console.read_for(4)
        elif tail.endswith("login:"):
            console.send("operator\r")
            console.read_for(8)
    return False


def process_row(console):
    """Return (limit, current, peak, min_free, failures) of the process row."""
    text = "\n".join(console.run("sysctl kern.capacity", 30))
    found = PROC_LINE.search(text)
    return tuple(int(value) for value in found.groups()) if found else None


def run_program(console, name, seconds):
    """Run one staged program; return its output lines and exit status."""
    out = console.run("/tmp/%s; echo rc=$?" % name, seconds)
    print("\n".join(out))
    status = None
    for line in out:
        if line.startswith("rc=") and line[3:].isdigit():
            status = int(line[3:])
    return out, status


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    if len(sys.argv) not in (2, 3):
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    console = load_console()
    console.log = open(sys.argv[1], "ab")
    results = []
    unreached = False
    if len(sys.argv) == 3:
        subprocess.check_call(["discobsd-flash", *sys.argv[2].split(",")])
        time.sleep(3)
    console.open_port()
    if len(sys.argv) == 3:
        console.read_for(40, re.compile(r"login: ?$", re.M))
    try:
        console.check("a root shell answers", reach_root(console), results)
        for name, path in STAGED:
            console.check("staged %s" % name, console.stage(name, path), results)
        start = process_row(console)
        console.check("kern.capacity reports the process row", start is not None, results)
        if start is None:
            return 1
        print("process row before: limit %d current %d peak %d min-free %d failures %d" % start)

        out, status = run_program(console, "filetab", 300)
        console.check(
            "filetab: file table exhaustion and recovery",
            "FILETABTEST OK" in out and status == 0 and not any(x.startswith("FAIL") for x in out),
            results,
        )
        console.check("shell answers after filetab", "alive" in console.run("echo alive"), results)

        out, status = run_program(console, "proctab", 600)
        if "PROCTABTEST INCONCLUSIVE" in out and status == 3:
            unreached = True
            print("UNREACHED process table: swap ran out first; see the INFO line above")
        else:
            console.check(
                "proctab: process table exhaustion and recovery",
                "PROCTABTEST OK" in out
                and status == 0
                and not any(x.startswith("FAIL") for x in out),
                results,
            )
        console.check("shell answers after proctab", "alive" in console.run("echo alive"), results)

        end = process_row(console)
        console.check(
            "no process outlives its program (occupancy %s then %s)"
            % (start[1], end[1] if end else None),
            end is not None and end[1] == start[1],
            results,
        )
        if end is not None:
            print("process row after: limit %d current %d peak %d min-free %d failures %d" % end)
    except console.ConsoleSilent as silent:
        tail = console.read_for(5)
        print("console silent after %r; last output:\n%s" % (silent.args[0], tail[-600:]))
        return 2
    print("%d of %d checks passed" % (sum(results), len(results)))
    if not all(results):
        return 1
    return 3 if unreached else 0


if __name__ == "__main__":
    sys.exit(main())
