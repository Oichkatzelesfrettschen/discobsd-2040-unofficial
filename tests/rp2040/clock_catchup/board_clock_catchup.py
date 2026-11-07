#!/usr/bin/env python3
"""Board check of the tick catch-up across masked flash operations.

usage: DISCOBSD_BOARD_CLOCK_CATCHUP=run PYTHON board_clock_catchup.py [unix.uf2,flash.uf2]

The script touches the board only when DISCOBSD_BOARD_CLOCK_CATCHUP is exactly
"run"; unset, empty and every other value exit without opening a port or
calling discobsd-flash. With the variable set it flashes the named images with
discobsd-flash when given and otherwise leaves the board running, then logs in
as operator on the USB console.

Claim under test (sys/arch/rp2040/rp2040/clock_catchup.h, called from
flash_erase, flash_program, fl_raw and flstrategy in sys/arch/rp2040/dev/flash.c):
the kernel counts back the SysTick ticks that a masked flash operation drops,
so the time of day does not fall behind the host while the root file system
erases sectors. A sector erase masks interrupts for about 44 ms.

Method. The board's time of day is read as the output of "date" in a loop,
each line stamped with the host time of its arrival. A line was produced
between its arrival and a short settle margin earlier, and the board's integer
second at that instant is the line's value, so every line bounds the
host-minus-board offset to an interval. The intersection over the loop is the
offset estimate, one interval per measurement. The offset starts from the
image's last-update time, so the check compares offsets and never reads one
absolute value. Three measurements run in order: a baseline; one after a
dd-and-sync loop that rewrites a file on the flash root; and one after an idle
wait as long as the loop, which gives the clock's drift without the loop. The
workload's excess drift is the second offset change minus the third. A kernel
without catch-up loses at least 44 ms per sector erased (the floor used here;
a kernel with the catch-up call disabled lost 96 s to a 96-sector workload, 23
times the floor); the check passes when the excess stays under a tenth of the
floor, and reports itself inconclusive when
the floor is under a second or the tenth is narrower than the bound width. The
idle wait is not free of flash work: each fork under the single-process window
swaps through the raw swap partition, so the control carries that residue.
"""

import calendar
import glob
import os
import re
import subprocess
import sys
import time

ESC = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\r")
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
OPT_IN_VARIABLE = "DISCOBSD_BOARD_CLOCK_CATCHUP"
OPT_IN_VALUE = "run"

DATE_LINE = re.compile(
    r"^(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)"
    r" (\d\d) (\d\d):(\d\d):(\d\d) [A-Z]{2,5} (\d{4})$"
)
MONTHS = ("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec").split()

# Samples per offset measurement, and the time between a line's creation by
# date(1) and its arrival on the host that the bound allows.
SAMPLES = 40
SETTLE_MARGIN = 0.060

# The workload: 32 KB written per pass; 24 passes lose 8 s without catch-up.
PASSES = 24
WORKLOAD_BLOCKS = 32
SECTOR_BYTES = 4096
ERASE_MS = 44
# The check passes when the excess drift stays under this fraction of the loss
# a kernel without catch-up would show. The catch-up counts back the ticks
# from the free-running timer and can lose at most one tick per masked window,
# so a residue that grows with the flash work is expected; a missing catch-up
# loses the whole ERASE_MS per sector.
TOLERANCE_FRACTION = 0.10
MIN_LOST_S = 1.0


class Console:
    def __init__(self, serial_module, port):
        self.serial = serial_module
        self.port = serial_module.Serial(port, 115200, timeout=0)

    def reopen(self):
        """Reopen the console after the board re-enumerates, which it can do twice."""
        start = time.time()
        while True:
            time.sleep(2)
            try:
                self.port = self.serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0)
                return
            except (IndexError, self.serial.SerialException):
                if time.time() - start > 60:
                    raise

    def read(self, seconds, stop=None):
        """Read for up to SECONDS; return (text, [(arrival, line)...])."""
        buf = b""
        stamped = []
        pending = b""
        start = time.time()
        while time.time() - start < seconds:
            try:
                data = self.port.read(4096)
            except self.serial.SerialException:
                self.reopen()
                continue
            if not data:
                time.sleep(0.005)
                continue
            now = time.time()
            buf += data
            pending += data
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                stamped.append((now, ESC.sub(b"", line).decode("latin1").strip()))
            if stop and stop in ESC.sub(b"", buf).decode("latin1"):
                break
        return ESC.sub(b"", buf).decode("latin1"), stamped

    def run(self, command, wait=8, stop=None):
        self.port.write(command.encode() + b"\r")
        return self.read(wait, stop)


def flash(images):
    subprocess.check_call(
        ["discobsd-flash", *images.split(",")],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(4)


def open_console(serial_module, boot_wait):
    start = time.time()
    while time.time() - start < 30 and not glob.glob(PORT_PATTERN):
        time.sleep(0.5)
    time.sleep(1)
    ports = glob.glob(PORT_PATTERN)
    if not ports:
        sys.exit("no DiscoBSD console under /dev/serial/by-id")
    console = Console(serial_module, ports[0])
    console.port.write(b"\r")
    text, _ = console.read(boot_wait, stop="login:")
    if "login:" in text:
        console.port.write(b"operator\r")
        console.read(4)
    return console


def board_seconds(line):
    """Seconds of the board's local date and time, read as if it were UTC."""
    match = DATE_LINE.match(line)
    if not match:
        return None
    month, day, hour, minute, second, year = match.groups()
    return calendar.timegm(
        (int(year), MONTHS.index(month) + 1, int(day), int(hour), int(minute), int(second))
    )


def measure_offset(console):
    """Return (low, high) bounds on host time minus board time, in seconds.

    The constant that converts the board's local zone to UTC stays in the value;
    only differences between measurements are meaningful.
    """
    _, stamped = console.run(
        'for i in `seq 1 %d`; do date; done; echo OFF""DONE' % SAMPLES,
        wait=300,
        stop="\nOFFDONE\n",
    )
    low = -1e18
    high = 1e18
    used = 0
    for arrival, line in stamped:
        value = board_seconds(line)
        if value is None:
            continue
        used += 1
        # The line was made at x in [arrival - margin, arrival]; the board's
        # time b has floor(b) == value, so b in [value, value + 1) and the
        # offset x - b lies in (x_low - value - 1, x_high - value].
        low = max(low, arrival - SETTLE_MARGIN - value - 1)
        high = min(high, arrival - value)
    if used < SAMPLES // 2:
        raise RuntimeError("only %d of %d date lines parsed" % (used, SAMPLES))
    return low, high


def midpoint(bounds):
    return (bounds[0] + bounds[1]) / 2


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    import serial

    images = sys.argv[1] if len(sys.argv) > 1 else ""
    if images:
        flash(images)
    console = open_console(serial, 25 if images else 3)

    baseline = measure_offset(console)
    print("offset 1 (baseline): %.3f .. %.3f s" % baseline)

    # The workload script travels as echo lines; the board's sh aborts a
    # script on a failed redirection, so each line is one short command.
    console.run("rm -f /tmp/cc.sh /tmp/cc.dat", wait=2)
    script = (
        "i=0",
        "while [ $i -lt %d ]; do" % PASSES,
        "dd if=/dev/zero of=/tmp/cc.dat bs=1k count=%d" % WORKLOAD_BLOCKS,
        "sync",
        "rm /tmp/cc.dat",
        "sync",
        "i=`expr $i + 1`",
        "done",
        "echo WORKDONE",
    )
    for line in script:
        console.run("echo '%s' >> /tmp/cc.sh" % line, wait=1.0)
    # The board's sh runs a script under "-n" instead of only parsing it, so
    # the stored script is read back and compared line for line.
    text, _ = console.run('cat /tmp/cc.sh; echo SCRIPT""END', wait=10, stop="\nSCRIPTEND\n")
    stored = [line.strip() for line in text.split("\n")]
    if any(line not in stored for line in script):
        print("workload script was not stored intact: %r" % text[-300:])
        return 1

    start = time.time()
    text, _ = console.run("sh /tmp/cc.sh 2>/dev/null", wait=600, stop="WORKDONE\n")
    workload_s = time.time() - start
    if "WORKDONE" not in text:
        print("workload did not finish in 600 s: %r" % text[-200:])
        return 1
    console.run("rm -f /tmp/cc.sh /tmp/cc.dat", wait=2)
    after_work = measure_offset(console)
    print("offset 2 (after workload, %.1f s): %.3f .. %.3f s" % ((workload_s, *after_work)))

    idle_start = time.time()
    time.sleep(workload_s)
    after_idle = measure_offset(console)
    idle_s = time.time() - idle_start
    print("offset 3 (after idle, %.1f s): %.3f .. %.3f s" % ((idle_s, *after_idle)))

    # Between the first and the second measurement the workload ran; between
    # the second and the third the board idled for the same time.
    work_change = midpoint(after_work) - midpoint(baseline)
    idle_change = midpoint(after_idle) - midpoint(after_work)
    excess = work_change - idle_change
    width = max(
        after_work[1] - after_work[0], baseline[1] - baseline[0], after_idle[1] - after_idle[0]
    )
    bytes_written = PASSES * WORKLOAD_BLOCKS * 1024
    sectors = bytes_written // SECTOR_BYTES
    lost = sectors * ERASE_MS / 1000.0
    print(
        "workload: %d bytes written in %.1f s, at least %d sectors erased"
        " (%.2f s lost without catch-up)" % (bytes_written, workload_s, sectors, lost)
    )
    print(
        "offset change: workload %+.3f s, idle control %+.3f s over %.1f s,"
        " excess %+.3f s, bound width %.3f s" % (work_change, idle_change, idle_s, excess, width)
    )
    if lost < MIN_LOST_S or lost * TOLERANCE_FRACTION < width:
        print("clock catch-up: INCONCLUSIVE (workload too small to separate a missing catch-up)")
        return 1
    tolerance = lost * TOLERANCE_FRACTION
    ok = abs(excess) <= tolerance
    print(
        "clock catch-up: %s (excess %.1f%% of the loss without catch-up, tolerance %.3f s)"
        % ("PASS" if ok else "FAIL", 100.0 * excess / lost, tolerance)
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
