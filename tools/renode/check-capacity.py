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
clistbound
    run between proctab and the counters: output stopped, a blocked
    writer, and 600 typed bytes past TTYHOG; "CLISTBOUND OK" (no clist
    refusal), exit status 0, and the writer's 60 numbered lines once each
    in order.
clist and buffer bounds
    the clist and buffer failure counters do not move across the run: no
    clist refusal and no getblk() wait for a buffer; their peaks are
    printed.
record readers
    as root, pstat -i, -f and -t print the inode table, file table and
    console terminal; then as operator, outside the kmem group, pstat -T
    and -p, vmstat, vmstat -s and -f, and iostat print their records, and
    pstat -i is refused with EPERM.

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
TABLES = {"proc", "inode", "file", "clist", "buffer"}
ROW = re.compile(
    r"^(proc|inode|file|clist|buffer)\s+limit (\d+) current (\d+) peak (\d+) min-free (\d+) "
    r"failures (\d+)",
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


# clistbound (tests/rp2040/capacity_exhaustion/clistbound.c) stops its
# terminal's output and holds a blocked writer while this driver types
# CB_INPUT_BYTES into the stopped terminal, past TTYHOG (255, sys/kern/tty.c),
# in chunks one emulation slice apart so the UART model delivers each. The
# writer's CB_LINES numbered lines must then arrive once each and in order.
CB_LINES = 60
CB_INPUT_BYTES = 600
CB_CHUNK = 60
CB_DRAIN_SECONDS = 3.0
# Echoed input can land on the same console line as a writer line, so a
# line is matched wherever it sits.
CB_LINE = re.compile(r"CB(\d{4}) abcdefghijkl")


def run_clistbound(console, boot) -> None:
    """Run /tmp/clistbound with typed input and require its verdict."""
    start = console.cursor
    console.type_line("/tmp/clistbound; echo rc=$?")
    console.wait_for("\nCLISTBOUND STOP", "clistbound start", boot.COMMAND_BUDGET_SECONDS)
    for _ in range(int(CB_DRAIN_SECONDS / boot.SLICE_SECONDS)):
        console.advance()
    # The input begins with '#', so the shell reads what the terminal kept
    # of it as a comment once the program exits.
    console.renode.command("sysbus.uart0 WriteChar 0x23")
    for _ in range(CB_INPUT_BYTES // CB_CHUNK):
        for _ in range(CB_CHUNK):
            console.renode.command("sysbus.uart0 WriteChar 0x78")
        console.advance()
    console.wait_for("\nrc=", "clistbound exit status", PROGRAM_BUDGET_SECONDS)
    console.type_line("")
    console.wait_for("#", "prompt after clistbound", boot.COMMAND_BUDGET_SECONDS)
    text = output_since(console, start)
    lines = text.split("\n")
    for line in lines:
        if line.startswith(("INFO", "FAIL", "CLISTBOUND")):
            print(f"check-capacity: clistbound: {line}")
    numbers = [int(found) for found in CB_LINE.findall(text)]
    if numbers != list(range(CB_LINES)):
        console.fail(f"clistbound: {len(numbers)} writer lines, not 0..{CB_LINES - 1} once each")
    if "CLISTBOUND OK" not in lines:
        refused = [line for line in lines if line.startswith("CLISTBOUND REFUSED")]
        console.fail(f"clistbound: the clist pool refused a block: {refused}")
    if "rc=0" not in lines or any(line.startswith("FAIL") for line in lines):
        console.fail("clistbound: exit status is not 0 or a check printed FAIL")


# The kernel-record readers, which check-capacity.sh adds under /tmp. root
# reads pstat's system tables; operator, outside the kmem
# group, runs everything else, which the readers that opened /dev/kmem
# could not, and must be refused the inode table.
ROOT_READER_CHECKS = (
    ("/tmp/pstat -i", (r"^\d+/24 active inodes$", r"^FLAGS +CNT +DEVICE")),
    ("/tmp/pstat -f", (r"^\d+/18 open files$", r"^TYPE +FLG +CNT")),
    ("/tmp/pstat -t", (r"^cn line$", r"^ +\d+ +\d+ +\d+ ")),
)
OPERATOR_READER_CHECKS = (
    ("/tmp/pstat -T", (r"^ *\d+/ *18 files$", r"^ *\d+/ *24 inodes$", r"^ *\d+/ *16 processes$",
                  r"^\d+ kbytes swap used, \d+ kbytes free$")),
    ("/tmp/pstat -p", (r"^ +S +F +PRI +UID", r"^ +\d+ +[0-9a-f]+ +\d+ +0 ")),
    ("/tmp/vmstat", (r"^ *r +b +w +avm", r"^ *\d+ +\d+ +\d+ +\d+ ")),
    ("/tmp/vmstat -s", (r"^ *\d+ system calls$", r"^ *\d+ cpu context switches$")),
    ("/tmp/vmstat -f", (r"^\d+ forks, \d+ kbytes",)),
    ("/tmp/iostat", (r"^ *tin +tout", r"^ *\d+ +\d+ +\d+ +\d+ +\d+ +\d+$")),
    ("/tmp/pstat -i", (r"^pstat: inodes: Operation not permitted$",)),
)


def run_reader_checks(console, boot, checks, prompt: str) -> None:
    """Run each command and require a line for each of its patterns."""
    for command, patterns in checks:
        start = console.cursor
        console.type_line(f"{command}; echo rd-end")
        console.wait_for("\nrd-end", command, boot.COMMAND_BUDGET_SECONDS)
        console.wait_for(prompt, f"prompt after {command}", boot.COMMAND_BUDGET_SECONDS)
        text = output_since(console, start)
        for pattern in patterns:
            if not re.search(pattern, text, re.M):
                console.fail(f"{command}: no line matching {pattern!r} in {text!r}")


def run_record_readers(console, boot) -> None:
    """Check the readers as root, then as operator after leaving su."""
    run_reader_checks(console, boot, ROOT_READER_CHECKS, "#")
    console.type_line("exit")
    console.wait_lines(["$"], "operator shell", boot.COMMAND_BUDGET_SECONDS)
    run_reader_checks(console, boot, OPERATOR_READER_CHECKS, "$")
    print("check-capacity: record readers checked")


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
    run_clistbound(console, boot)
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
    # The buffer bound: the table-exhaustion programs and clistbound drive
    # the file system and swap hardest of any tier, and no getblk() may
    # wait for a buffer meanwhile (sys/kern/ufs_bio.c counts each wait as a
    # buffer failure); the peak is recorded, and the clist row must show no
    # refusal.
    for table in ("clist", "buffer"):
        limit, current, peak, min_free, failures = after[table]
        print(
            f"check-capacity: {table} limit {limit} current {current} peak {peak} "
            f"min-free {min_free} failures {failures}"
        )
        if failures != before[table][4]:
            console.fail(f"{table}: failures moved from {before[table][4]} to {failures}")
    run_record_readers(console, boot)


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
