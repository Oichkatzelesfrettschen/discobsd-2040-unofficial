#!/usr/bin/env python3
"""Fill the process, in-core inode and system file tables under Renode.

The flash image this driver boots is the shipped root with
tests/rp2040/capacity_exhaustion/filetab, inotab and proctab added under /tmp
(check-capacity.sh composes it). The driver boots it through the same
machine, console and virtual-time slicing as check-boot.py, reaches a root
shell and runs the three programs. Each program fills one table, checks that the
refusal carries the table's own errno and moves only that table's failure
counter in kern.capacity, releases the table and checks that the next
allocation succeeds; filetab.c, inotab.c and proctab.c state every check.

The driver asserts, in order:

filetab
    "FILETABTEST OK", exit status 0 and no line beginning "FAIL".
inotab
    "INOTABTEST OK", exit status 0 and no line beginning "FAIL". "INOTABTEST
    INCONCLUSIVE" means the free process or file slots could not carry
    enough holders: the case was not reached, and the gate fails.
proctab
    "PROCTABTEST OK", exit status 0 and no line beginning "FAIL". A fork
    refused with ENOMEM before the table filled prints "PROCTABTEST
    INCONCLUSIVE": swap ran out first, the case was not reached, and the
    gate fails.
counters
    kern.capacity after the runs, read by the driver and not by the
    programs: the process, inode and file rows each record a peak equal to the
    limit, a minimum free count of zero and at least one failure, and the
    process occupancy equals its value before the runs, so no child
    outlived its program.

The claim is the emulated kernel's table accounting and recovery. The
third-party peripheral models carry the boot; a Pico's result is the board
receipt, a separate evidence class (sys/arch/rp2040/doc/CAPACITY.md).
check-boot.sh owns the warning-class ceilings, measured for its shorter run.

Usage: check-capacity.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from renode_monitor import Renode, RenodeError  # noqa: E402

# The kernel configuration check-capacity.sh boots; it selects the probe lines.
CONFIG = "PICO_UART"
PROGRAM_BUDGET_SECONDS = 900.0
TABLES = {"proc", "inode", "file"}
ROW = re.compile(
    r"^(proc|inode|file)\s+limit (\d+) current (\d+) peak (\d+) min-free (\d+) failures (\d+)",
    re.M,
)


def load_boot():
    """Import check-boot.py, whose name is not an identifier."""
    spec = importlib.util.spec_from_file_location("check_boot", os.path.join(HERE, "check-boot.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def output_since(console, start: int) -> str:
    """Return the console text after offset start with escapes removed."""
    return console.text()[start:].replace("\r", "")


def run_program(console, boot, name: str, verdict: str) -> None:
    """Run /tmp/NAME and require its verdict line, status 0 and no FAIL line."""
    start = console.cursor
    console.type_line(f"/tmp/{name}; echo rc=$?")
    # The typed command echoes "rc=$?"; the status line is "rc=" and digits.
    try:
        console.wait_for("\nrc=", f"{name} exit status", PROGRAM_BUDGET_SECONDS)
        console.wait_for("#", f"prompt after {name}", boot.COMMAND_BUDGET_SECONDS)
    finally:
        text = output_since(console, start)
        lines = text.split("\n")
        for line in lines:
            if line.startswith(("PASS", "FAIL", "INFO", "fork refused", name.upper())):
                print(f"check-capacity: {name}: {line}")
    if f"{verdict} INCONCLUSIVE" in lines:
        console.fail(f"{name}: the table was not reached; see the INFO line")
    elif f"{verdict} OK" not in lines:
        console.fail(f"{name}: no {verdict} OK line")
    if "rc=0" not in lines:
        console.fail(f"{name}: exit status is not 0")
    if any(line.startswith("FAIL") for line in lines):
        console.fail(f"{name}: a check printed FAIL")


def capacity_rows(console, boot) -> dict:
    """Return {table: (limit, current, peak, min_free, failures)} from sysctl."""
    start = console.cursor
    console.type_line("sysctl kern.capacity; echo capacity-end")
    console.wait_for("\ncapacity-end", "capacity record", boot.COMMAND_BUDGET_SECONDS)
    console.wait_for("#", "prompt after the capacity record", boot.COMMAND_BUDGET_SECONDS)
    rows = {}
    for found in ROW.finditer(output_since(console, start)):
        rows[found.group(1)] = tuple(int(value) for value in found.groups()[1:])
    return rows


def run_stages(console, boot) -> None:
    console.wait_lines(
        [f"DiscoBSD 2.7 ({CONFIG})"]
        + boot.CPU_LINES
        + boot.BOARD_LINES[CONFIG]
        + boot.STORAGE_LINES,
        "device probe",
        boot.PROBE_BUDGET_SECONDS,
    )
    console.wait_lines(boot.LOGIN_LINES, "file system check and daemons", boot.LOGIN_BUDGET_SECONDS)
    console.wait_lines(["login:"], "login prompt", boot.LOGIN_BUDGET_SECONDS)
    console.type_line("operator")
    console.wait_lines(["$"], "shell prompt", boot.COMMAND_BUDGET_SECONDS)
    # kern.capacity is readable by every user; root keeps the run the board
    # driver's, whose programs are staged and started by root.
    console.type_line("su")
    console.wait_lines(["#"], "root shell", boot.COMMAND_BUDGET_SECONDS)

    before = capacity_rows(console, boot)
    if set(before) != TABLES:
        console.fail(f"kern.capacity rows before the runs: {sorted(before)}")
        raise boot.StageFailed("capacity record")
    run_program(console, boot, "filetab", "FILETABTEST")
    run_program(console, boot, "inotab", "INOTABTEST")
    run_program(console, boot, "proctab", "PROCTABTEST")
    after = capacity_rows(console, boot)
    if set(after) != TABLES:
        console.fail(f"kern.capacity rows after the runs: {sorted(after)}")
        raise boot.StageFailed("capacity record")

    for table in ("proc", "inode", "file"):
        limit, current, peak, min_free, failures = after[table]
        print(
            f"check-capacity: {table} limit {limit} current {current} peak {peak} "
            f"min-free {min_free} failures {failures}"
        )
        if peak != limit or min_free != 0:
            console.fail(f"{table}: peak {peak} and min-free {min_free} do not record a full table")
        if failures <= before[table][4]:
            console.fail(f"{table}: failures stayed at {failures}")
    if after["proc"][1] != before["proc"][1]:
        console.fail(f"process occupancy {before['proc'][1]} before, {after['proc'][1]} after")


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    boot = load_boot()
    renode_path, unix_bin, flash_bin, outdir = argv
    os.makedirs(outdir, exist_ok=True)
    transcript = os.path.join(outdir, "uart0.txt")
    if os.path.exists(transcript):
        os.remove(transcript)
    renode = Renode(renode_path, os.path.join(outdir, "renode.log"), boot.WALL_SECONDS)
    console = boot.Console(renode, transcript)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        run_stages(console, boot)
    except boot.StageFailed:
        print(f"check-capacity: transcript in {transcript}", file=sys.stderr)
    except RenodeError as error:
        print(f"check-capacity: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    if console.failures:
        return 1
    print("check-capacity: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
