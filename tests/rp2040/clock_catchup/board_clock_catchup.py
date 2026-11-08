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
workload's excess drift is the second offset change minus the third, that is
2 * second - first - third; the interval of each measurement is propagated, so
the excess is an interval. A kernel without catch-up loses at least 44 ms per
sector erased (the floor used here; a kernel with the catch-up call disabled
lost 96 s to a 96-sector workload, 23 times the floor). The check passes when
the whole excess interval lies within a tenth of the floor, fails when it lies
wholly outside, and is inconclusive otherwise (exit status 2). A workload that
did not write every block and a floor under a second are also inconclusive.
A measurement whose per-line bounds have no common point has two causes: one
sample delayed past SETTLE_MARGIN, which is inconclusive, or a board clock that
fell behind during the loop, which is a failure. Per line the offset estimate
is arrival - value, which lies in [offset, offset + 1 + SETTLE_MARGIN) for a
steady clock. A losing clock raises the estimate across the loop, so
loss_of_clock() reports a failure when at least LOSS_PAIR_FRACTION of the
estimate pairs LOSS_PAIR_LAG lines apart are non-decreasing within the
one-second quantization of date, the lowest estimate of the last quarter of
the loop exceeds the lowest of the first quarter by more than 1 + SETTLE_MARGIN,
and the median pairwise slope times the loop length exceeds MIN_LOST_S. The
quarter-minimum rise is the bound a steady clock cannot pass: a delay only
raises an estimate, so the minimum of a quarter holding one undelayed line lies
in the steady band, while the median slope of a steady clock follows the rising
edge of the quantization sawtooth and reaches a second within one loop. The
report carries the median-slope loss and the line count. Any
of the three measurements can show the loss, because the date loop is itself
flash-backed work: every date forks, the fork swaps through the raw swap
partition, and a kernel without catch-up loses the masked ticks of that swap.
A lone delayed sample leaves the median slope flat and stays inconclusive. The idle wait is not
free of flash work: each fork under the single-process window swaps through the
raw swap partition, so the control carries that residue.

The decision logic is exercised on synthetic input by test_board_scripts.py.
"""

import calendar
import glob
import os
import re
import statistics
import subprocess
import sys
import time

ESC = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\r")
PORT_PATTERN = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"
OPT_IN_VARIABLE = "DISCOBSD_BOARD_CLOCK_CATCHUP"
OPT_IN_VALUE = "run"

# asctime() in lib/libc/gen/asctime.c formats the day with "%3d", so a single
# digit day follows the month after two spaces ("Sat Nov  7 09:47:21 PST 2026").
DATE_LINE = re.compile(
    r"^(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)"
    r" +(\d{1,2}) (\d\d):(\d\d):(\d\d) [A-Z]{2,5} (\d{4})$"
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
# A board clock that fell behind across a date loop shows offset estimates that
# rise: at least this fraction of the estimate pairs LOSS_PAIR_LAG lines apart
# must not fall by more than the quantization of date (one second plus
# SETTLE_MARGIN). Adjacent pairs cannot test this, because a clock moving less
# than the quantization per line hides a fall inside the tolerance; four lines
# separate a slope of 0.25 s per line from the tolerance. The
# rise is the Theil-Sen slope times the loop length, which a minority of delayed
# lines cannot move; it must exceed MIN_LOST_S, the same floor below which the
# workload verdict is also inconclusive. The undelayed estimates of a steady
# clock lie in a band 1 + SETTLE_MARGIN wide, and a date cadence a little over
# one second walks them up that band in a sawtooth whose Theil-Sen rise reaches
# a second, so the rise also needs the lowest estimate of the last
# 1/LOSS_QUARTER of the loop to clear the lowest of the first by the band
# width, which no steady clock with an undelayed line in the first quarter does.
LOSS_PAIR_FRACTION = 0.90
LOSS_PAIR_LAG = 4
LOSS_QUARTER = 4
# Exit status of a run whose measurements cannot decide (0 passes, 1 fails).
INCONCLUSIVE = 2


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
            if stop:
                seen = ESC.sub(b"", buf).decode("latin1")
                if stop.search(seen) if hasattr(stop, "search") else stop in seen:
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


class MeasurementError(Exception):
    """A measurement that cannot bound the offset; the check is inconclusive."""


class ClockLostTime(MeasurementError):
    """The board clock fell behind during a date loop; the check fails.

    LOSS is the rise of the offset estimate across the loop in seconds, LINES
    the number of parsed date lines and MEASUREMENT the name of the measurement
    that showed it (set by measure_offset).
    """

    def __init__(self, loss, lines, measurement=None):
        super().__init__(
            "the board clock fell behind by %.3f s across %d date lines" % (loss, lines)
        )
        self.loss = loss
        self.lines = lines
        self.measurement = measurement


def loss_of_clock(estimates):
    """Return the seconds the clock fell behind across ESTIMATES, or None.

    ESTIMATES holds arrival - value per parsed line in order. A pair
    LOSS_PAIR_LAG lines apart is non-decreasing when the later estimate is no
    more than the quantization of date (1 + SETTLE_MARGIN) below the earlier.
    The lowest estimate of the last quarter must exceed the lowest of the first
    quarter by more than that quantization, which a steady clock's sawtooth
    cannot do. The loss is the median of the pairwise slopes times the number
    of intervals, which tolerates a minority of delayed lines. A single step in
    the clock smaller than half the loop moves the median little, so it reads
    as inconclusive.
    """
    count = len(estimates)
    if count <= LOSS_PAIR_LAG:
        return None
    quantum = 1 + SETTLE_MARGIN
    pairs = list(zip(estimates, estimates[LOSS_PAIR_LAG:]))
    steps = sum(1 for a, b in pairs if b >= a - quantum)
    if steps < LOSS_PAIR_FRACTION * len(pairs):
        return None
    window = max(LOSS_PAIR_LAG, count // LOSS_QUARTER)
    if min(estimates[-window:]) - min(estimates[:window]) <= quantum:
        return None
    slope = statistics.median(
        (estimates[j] - estimates[i]) / (j - i) for i in range(count) for j in range(i + 1, count)
    )
    loss = slope * (count - 1)
    return loss if loss > MIN_LOST_S else None


def offset_bounds(stamped, samples=SAMPLES):
    """Return (low, high) bounds on host time minus board time, in seconds.

    STAMPED holds (arrival time, console line) pairs. The constant that
    converts the board's local zone to UTC stays in the value; only differences
    between measurements are meaningful. Raises MeasurementError when too few
    lines parse. When the per-line bounds have no common point, which a sample
    delayed beyond SETTLE_MARGIN or a clock that fell behind during the loop
    produces, raises ClockLostTime for the second cause (see loss_of_clock)
    and MeasurementError for the first.
    """
    low = -1e18
    high = 1e18
    used = 0
    estimates = []
    for arrival, line in stamped:
        value = board_seconds(line)
        if value is None:
            continue
        used += 1
        estimates.append(arrival - value)
        # The line was made at x in [arrival - margin, arrival]; the board's
        # time b has floor(b) == value, so b in [value, value + 1) and the
        # offset x - b lies in (x_low - value - 1, x_high - value].
        low = max(low, arrival - SETTLE_MARGIN - value - 1)
        high = min(high, arrival - value)
    if used < samples // 2:
        raise MeasurementError("only %d of %d date lines parsed" % (used, samples))
    if low > high:
        loss = loss_of_clock(estimates)
        if loss is not None:
            raise ClockLostTime(loss, used)
        raise MeasurementError(
            "the bounds of %d date lines have no common point (%.3f .. %.3f s)" % (used, low, high)
        )
    return low, high


def measure_offset(console, name="measurement"):
    """Run the stamped date loop on the board and bound the offset.

    A ClockLostTime raised by the loop carries NAME as its measurement."""
    _, stamped = console.run(
        'for i in `seq 1 %d`; do date; done; echo OFF""DONE' % SAMPLES,
        wait=300,
        stop="\nOFFDONE\n",
    )
    try:
        return offset_bounds(stamped)
    except ClockLostTime as error:
        error.measurement = name
        raise


def excess_interval(baseline, work, idle):
    """Interval of work_change - idle_change = 2 * work - baseline - idle.

    Each argument is a (low, high) bound on the offset; the result carries the
    worst case of all three, so its width is twice the workload bound's width
    plus the other two.
    """
    low = 2 * work[0] - baseline[1] - idle[1]
    high = 2 * work[1] - baseline[0] - idle[0]
    return low, high


def verdict(excess, lost):
    """PASS when the whole excess interval lies within the tolerance, FAIL when
    it lies wholly outside, INCONCLUSIVE otherwise or when the floor is small."""
    tolerance = lost * TOLERANCE_FRACTION
    if lost < MIN_LOST_S:
        return "INCONCLUSIVE", tolerance
    if -tolerance <= excess[0] and excess[1] <= tolerance:
        return "PASS", tolerance
    if excess[1] < -tolerance or excess[0] > tolerance:
        return "FAIL", tolerance
    return "INCONCLUSIVE", tolerance


WORKLOAD_STATUS = re.compile(r"^WORKSTATUS(\d+)$", re.M)
RECORDS_OUT = re.compile(r"^(\d+)\+(\d+) records out$", re.M)


def workload_script(passes=PASSES):
    """Lines of the board script. errexit stops it at the first failed command,
    so the completion marker follows only a run in which every write succeeded."""
    return (
        "set -e",
        "i=0",
        "while [ $i -lt %d ]; do" % passes,
        "dd if=/dev/zero of=/tmp/cc.dat bs=1k count=%d" % WORKLOAD_BLOCKS,
        "sync",
        "rm /tmp/cc.dat",
        "sync",
        "i=`expr $i + 1`",
        "done",
        "echo WORKDONE",
    )


def check_workload(text, passes=PASSES):
    """Return None when the console text shows a complete workload, else a reason.

    The shell's status must be 0, the marker must be present, and dd must have
    reported exactly PASSES * WORKLOAD_BLOCKS full records out and no partial
    record, which is the byte count the loss floor is derived from.
    """
    match = WORKLOAD_STATUS.search(text)
    if not match:
        return "no exit status from the workload"
    if match.group(1) != "0":
        return "the workload exited with status %s" % match.group(1)
    if not re.search(r"^WORKDONE$", text, re.M):
        return "the completion marker is missing"
    records = RECORDS_OUT.findall(text)
    full = sum(int(a) for a, _ in records)
    partial = sum(int(b) for _, b in records)
    if full != passes * WORKLOAD_BLOCKS or partial != 0:
        return "dd reported %d full and %d partial records out, expected %d full" % (
            full,
            partial,
            passes * WORKLOAD_BLOCKS,
        )
    return None


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    import serial

    images = sys.argv[1] if len(sys.argv) > 1 else ""
    if images:
        flash(images)
    console = open_console(serial, 25 if images else 3)

    try:
        return run_check(console)
    except MeasurementError as error:
        print("clock catch-up: INCONCLUSIVE (%s)" % error)
        return INCONCLUSIVE


def run_check(console):
    """Run the three measurements and the workload; return the exit status.

    A clock that lost time during any date loop ends the run as a failure."""
    try:
        return run_measurements(console)
    except ClockLostTime as error:
        print("clock catch-up: FAIL (%s, in the %s measurement)" % (error, error.measurement))
        return 1


def run_measurements(console):
    baseline = measure_offset(console, "baseline")
    print("offset 1 (baseline): %.3f .. %.3f s" % baseline)

    # The workload script travels as echo lines; the board's sh aborts a
    # script on a failed redirection, so each line is one short command.
    console.run("rm -f /tmp/cc.sh /tmp/cc.dat", wait=2)
    script = workload_script()
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
    text, _ = console.run('sh /tmp/cc.sh; echo WORK""STATUS$?', wait=600, stop=WORKLOAD_STATUS)
    workload_s = time.time() - start
    reason = check_workload(text)
    if reason:
        print("workload failed: %s: %r" % (reason, text[-300:]))
        return INCONCLUSIVE
    console.run("rm -f /tmp/cc.sh /tmp/cc.dat", wait=2)
    after_work = measure_offset(console, "after-workload")
    print("offset 2 (after workload, %.1f s): %.3f .. %.3f s" % ((workload_s, *after_work)))

    idle_start = time.time()
    time.sleep(workload_s)
    after_idle = measure_offset(console, "after-idle")
    idle_s = time.time() - idle_start
    print("offset 3 (after idle, %.1f s): %.3f .. %.3f s" % ((idle_s, *after_idle)))

    # Between the first and the second measurement the workload ran; between
    # the second and the third the board idled for the same time.
    excess = excess_interval(baseline, after_work, after_idle)
    bytes_written = PASSES * WORKLOAD_BLOCKS * 1024
    sectors = bytes_written // SECTOR_BYTES
    lost = sectors * ERASE_MS / 1000.0
    print(
        "workload: %d bytes written in %.1f s, at least %d sectors erased"
        " (%.2f s lost without catch-up)" % (bytes_written, workload_s, sectors, lost)
    )
    print("excess over the idle control (%.1f s): %+.3f .. %+.3f s" % ((idle_s, *excess)))
    result, tolerance = verdict(excess, lost)
    print(
        "clock catch-up: %s (tolerance %.3f s, excess interval %+.3f .. %+.3f s)"
        % (result, tolerance, *excess)
    )
    return {"PASS": 0, "FAIL": 1}.get(result, INCONCLUSIVE)


if __name__ == "__main__":
    sys.exit(main())
