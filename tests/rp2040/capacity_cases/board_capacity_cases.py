#!/usr/bin/env python3
"""Board test for three capacity cases of tests/rp2040/capacity-corpus.json.

usage: DISCOBSD_BOARD_CAPACITY_CASES=run $PYTHON board_capacity_cases.py \
           LOG [unix.uf2,flash.uf2]

The script touches the board only when DISCOBSD_BOARD_CAPACITY_CASES is
exactly "run"; unset, empty and every other value exit without opening a port
or calling discobsd-flash. With the variable set it flashes the named images
with discobsd-flash when given, reaches a root shell on the USB console and
stages tests/rp2040/swapram_evacuate/evactest and tests/rp2040/sigtest/sigtest
with the staging routines of tests/rp2040/user_window/board_user_window.py.
It then runs, in order:

1. evactest twice (swapram-evacuation): each run reports at least one image
   in the compressed pool before the request, the evacuation result "done",
   and four children with no bad data or stack byte and no bad signal
   disposition, the pattern each child keeps in the u area. The second run
   shows that the pool accepts work after an evacuation;
2. sigtest (signal-frame-recovery): its success marker, exit status 0 and no
   check marked FAIL, then a kern.capacity snapshot;
3. the storage workload (buffer-cache-board-pressure): tar of /etc to a file,
   find over /usr and /etc, and du over the same trees, first one after the
   other as the reference and then all three at once; the three outputs of
   the parallel run equal the reference by cksum after a sync, and no row of
   kern.capacity counts a failure the snapshot before the workload lacks;
4. recovery: the workload's files are removed, the board reboots, a root
   shell returns, the boot's own fsck -p printed its summary of the root
   and went on to the daemons with no repair or failure line (/etc/rc
   prints one beginning "---" for every other fsck status; securelevel 1
   refuses a second fsck of the mounted root), df reports the free space it
   reported before the workload, and a final command answers.

Every console byte goes to LOG. The exit status is 0 when every check passed,
1 when one failed and 2 when the console stopped answering. The script
records no result; the operator retains LOG as the receipt.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time

OPT_IN_VARIABLE = "DISCOBSD_BOARD_CAPACITY_CASES"
OPT_IN_VALUE = "run"
HERE = os.path.dirname(os.path.abspath(__file__))
WINDOW_DRIVER = os.path.join(HERE, "..", "user_window", "board_user_window.py")
STAGED = (
    ("evactest", "tests/rp2040/swapram_evacuate/evactest"),
    ("sigtest", "tests/rp2040/sigtest/sigtest"),
)
ROW = re.compile(r"^(proc|inode|file|clist|buffer)\s+limit (\d+) .* failures (\d+)", re.M)
TREES = "/usr /etc"
REFERENCE = (
    ("tar", "cd /; tar cf /tmp/r.t etc"),
    ("find", "find %s -print > /tmp/r.f" % TREES),
    ("du", "du -s %s > /tmp/r.d" % TREES),
)
PARALLEL = (
    "cd /; tar cf /tmp/p.t etc & find %s -print > /tmp/p.f & "
    "du -s %s > /tmp/p.d & wait; sync" % (TREES, TREES)
)
OUTPUTS = (("tar", "t"), ("find", "f"), ("du", "d"))
FSCK_SUMMARY = re.compile(r"^/dev/fl0a: \d+ files, \d+ used, \d+ free\s*$", re.M)


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


def failures(console):
    """Return {table: failure count} from kern.capacity, or None."""
    text = "\n".join(console.run("sysctl kern.capacity", 30))
    rows = {found.group(1): int(found.group(3)) for found in ROW.finditer(text)}
    return rows if len(rows) == 5 else None


def run_program(console, name, seconds):
    """Run one staged program; return its output lines and exit status."""
    out = console.run("/tmp/%s; echo rc=$?" % name, seconds)
    print("\n".join(out))
    status = None
    for line in out:
        if line.startswith("rc=") and line[3:].isdigit():
            status = int(line[3:])
    return out, status


def evacuation_holds(out, status):
    """Return True when one evactest run meets the swapram-evacuation case."""
    text = "\n".join(out)
    before = re.search(r"^pool images before: (\d+)$", text, re.M)
    children = re.findall(r"^child \d: 0 bad bytes, 0 bad dispositions$", text, re.M)
    return (
        "EVACTEST OK" in out
        and status == 0
        and before is not None
        and int(before.group(1)) > 0
        and "evacuation result: 2 " in text
        and len(children) == 4
    )


def cksum(console, path):
    """Return the checksum and length cksum prints for path on the board."""
    out = console.run("cksum < %s" % path, 60)
    return out[-1].split()[:2] if out else []


def free_space(console):
    """Return the root file system's used and free counts as df prints them."""
    out = console.run("df /", 30)
    return out[-1].split()[1:4] if out else []


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
    if len(sys.argv) == 3:
        subprocess.check_call(["discobsd-flash", *sys.argv[2].split(",")])
        time.sleep(3)
    console.open_port()
    if len(sys.argv) == 3:
        console.read_for(40, re.compile(r"login: ?$", re.M))
    try:
        console.check("a root shell answers", reach_root(console), results)
        print("\n".join(console.run("cat /etc/release; sysctl kern.capacity", 30)))
        space = free_space(console)
        print("df before: %s" % " ".join(space))
        for name, path in STAGED:
            console.check("staged %s" % name, console.stage(name, path), results)

        for attempt in ("first", "second"):
            out, status = run_program(console, "evactest", 120)
            console.check(
                "evactest, %s run: patterns survive evacuation to flash" % attempt,
                evacuation_holds(out, status),
                results,
            )
        console.check("shell answers after evactest", "alive" in console.run("echo alive"), results)

        out, status = run_program(console, "sigtest", 120)
        console.check(
            "sigtest: frame, mask, EINTR and registers",
            "SIGTEST OK" in out and status == 0 and not any("FAIL" in line for line in out),
            results,
        )
        before = failures(console)
        console.check("kern.capacity snapshot after sigtest", before is not None, results)
        console.run("rm -f /tmp/evactest /tmp/sigtest")

        for _label, command in REFERENCE:
            console.run(command, 300)
        console.run("sync", 60)
        console.run(PARALLEL, 600)
        for label, suffix in OUTPUTS:
            want = cksum(console, "/tmp/r.%s" % suffix)
            got = cksum(console, "/tmp/p.%s" % suffix)
            console.check(
                "parallel %s equals its reference (cksum %s, %s)"
                % (label, " ".join(want), " ".join(got)),
                len(want) == 2 and want == got,
                results,
            )
        after = failures(console)
        print("\n".join(console.run("sysctl kern.capacity", 30)))
        console.check(
            "the workload adds no capacity failure (%s then %s)" % (before, after),
            before is not None and before == after,
            results,
        )

        console.run("rm -f /tmp/r.t /tmp/r.f /tmp/r.d /tmp/p.t /tmp/p.f /tmp/p.d", 60)
        console.run("sync", 60)
        console.port.write(b"sync; reboot\r")
        time.sleep(12)
        console.port.close()
        console.open_port()
        boot = console.read_for(40, re.compile(r"login: ?$", re.M))
        console.check("a root shell answers after the reboot", reach_root(console), results)
        summary = FSCK_SUMMARY.search(boot)
        print("boot fsck: %s" % (summary.group(0).strip() if summary else "no summary line"))
        console.check(
            "the boot's fsck -p reports a clean root and the boot goes on",
            summary is not None
            and "Starting daemons" in boot[summary.end() :]
            and "\n---" not in boot,
            results,
        )
        again = free_space(console)
        console.check(
            "df reports the free space it reported before (%s then %s)"
            % (" ".join(space), " ".join(again)),
            len(space) == 3 and space == again,
            results,
        )
        console.check("final command answers", "alive" in console.run("echo alive"), results)
    except console.ConsoleSilent as silent:
        tail = console.read_for(5)
        print("console silent after %r; last output:\n%s" % (silent.args[0], tail[-600:]))
        return 2
    print("%d of %d checks passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
