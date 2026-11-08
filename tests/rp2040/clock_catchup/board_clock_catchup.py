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
each line stamped with the host time of its arrival (host_now(), the monotonic
clock anchored once to the wall clock). A line was produced
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
A measurement whose per-line bounds have no common point has two causes: a
line delayed past SETTLE_MARGIN, which is inconclusive, or a board clock that
fell behind during the loop, which is a failure. Arrival times alone cannot
tell them apart, because a console that holds lines back can give a steady
clock any arrival pattern. The check therefore separates them with a probe
whose bounds rest on no latency assumption: the host sends each "date" itself,
so the line was made between the send and the arrival, and a steady clock's
offset lies in every sample's interval (send - value - 1, arrival - value].
probe_bounds() intersects PROBE_SAMPLES such intervals; when they have no
common point, no steady clock can have produced them and the check fails,
reporting the gap as the least time the clock lost. When they share a point,
the loop's empty bounds came from a delayed line and the measurement is
inconclusive. Any of the three measurements can show the loss, because the
date loop is itself flash-backed work: every date forks, the fork swaps
through the raw swap partition, and a kernel without catch-up loses the
masked ticks of that swap; the probe forks one date per sample and loses the
same way. The idle wait is not free of flash work for the same reason, so the
control carries that residue.

The decision logic is exercised on synthetic input by test_board_scripts.py.
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
# Request-bounded samples taken when a loop's bounds have no common point.
PROBE_SAMPLES = 40
# Exit status of a run whose measurements cannot decide (0 passes, 1 fails).
INCONCLUSIVE = 2

# Arrival stamps come from the monotonic clock placed on the wall clock once,
# so a host clock step during the run moves no stamp relative to another.
WALL_ANCHOR = time.time()
MONOTONIC_ANCHOR = time.monotonic()


def host_now():
    """Host time in seconds since the epoch, immune to wall-clock steps."""
    return WALL_ANCHOR + (time.monotonic() - MONOTONIC_ANCHOR)


class Console:
    def __init__(self, serial_module, port):
        self.serial = serial_module
        self.port = serial_module.Serial(port, 115200, timeout=0)
        self.requests = 0

    def reopen(self):
        """Reopen the console after the board re-enumerates, which it can do twice."""
        start = time.monotonic()
        while True:
            time.sleep(2)
            try:
                self.port = self.serial.Serial(glob.glob(PORT_PATTERN)[0], 115200, timeout=0)
                return
            except (IndexError, self.serial.SerialException):
                if time.monotonic() - start > 60:
                    raise

    def read(self, seconds, stop=None):
        """Read for up to SECONDS; return (text, [(arrival, line)...])."""
        buf = b""
        stamped = []
        pending = b""
        start = time.monotonic()
        while time.monotonic() - start < seconds:
            try:
                data = self.port.read(4096)
            except self.serial.SerialException:
                self.reopen()
                continue
            if not data:
                time.sleep(0.005)
                continue
            now = host_now()
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

    def probe(self, samples, wait=10):
        """Send "date" SAMPLES times; return [(sent, arrival, line)...].

        Each request is "date; echo PROBE<n>", N counting every request this
        console has sent. Its date line is the one line between the echo of
        that command and the line PROBE<n>, so neither a late reply to an
        earlier request nor any other stale date line is taken for it. A
        request whose marker does not arrive within WAIT seconds, or whose
        window holds no date line or more than one, raises MeasurementError,
        because its reply cannot be attributed to its send."""
        probes = []
        for _ in range(samples):
            index = self.requests
            self.requests += 1
            marker = "PROBE%d" % index
            command = 'date; echo PRO""BE%d' % index
            sent = host_now()
            _, stamped = self.run(command, wait, stop=re.compile(r"^%s\n" % marker, re.M))
            lines = [line for _, line in stamped]
            if marker not in lines:
                raise MeasurementError("probe request %d unanswered in %g s" % (index, wait))
            end = lines.index(marker)
            echoes = [i for i, line in enumerate(lines[:end]) if line.endswith(command)]
            if not echoes:
                raise MeasurementError("probe request %d was not echoed" % index)
            window = stamped[echoes[-1] + 1 : end]
            dated = [(arrival, line) for arrival, line in window if board_seconds(line) is not None]
            if len(dated) != 1:
                raise MeasurementError(
                    "probe request %d returned %d date lines, want one" % (index, len(dated))
                )
            arrival, line = dated[0]
            probes.append((sent, arrival, line))
        return probes


def flash(images):
    subprocess.check_call(
        ["discobsd-flash", *images.split(",")],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(4)


def open_console(serial_module, boot_wait):
    start = time.monotonic()
    while time.monotonic() - start < 30 and not glob.glob(PORT_PATTERN):
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


SHELL_READY = re.compile(r"^SHELL\n", re.M)
ANY_PROMPT = re.compile(r"(login:|assword:|\$) *$")
# The shell prompt is left out of SHELL_STOP because every echoed command
# line starts with it; a login or password prompt ends the text while the
# console waits for input.
SHELL_STOP = re.compile(r"^SHELL\n|(login:|assword:) *$", re.M)


def reach_shell(console, seconds=60, wait=4):
    """Return once a shell answers on CONSOLE, logging in as operator with an
    empty password when the console waits at login: or Password:.

    A board still booting or sitting at login: turns every date request of
    the baseline into a reply without a date line, so a console with no shell
    after SECONDS, each reply awaited up to WAIT seconds, raises
    MeasurementError naming that cause."""
    start = time.monotonic()
    text = ""
    while time.monotonic() - start < seconds:
        tail = text.rstrip()
        if tail.endswith("login:"):
            text, _ = console.run("operator", wait=wait, stop=ANY_PROMPT)
        elif tail.endswith("assword:"):
            text, _ = console.run("", wait=wait, stop=ANY_PROMPT)
        else:
            text, _ = console.run('echo SH""ELL', wait=wait, stop=SHELL_STOP)
            if SHELL_READY.search(text):
                return
    raise MeasurementError("no shell answered on the console in %g s" % seconds)


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


class EmptyBounds(MeasurementError):
    """The per-line bounds of a date loop have no common point."""


class ClockUnsteady(MeasurementError):
    """The board clock fell behind or ran ahead during a measurement; the
    check fails, since a catch-up that drops ticks or counts them twice both
    break the claim.

    LOSS is the least time in seconds the clock must have moved off the host
    for the request-bounded samples to have no common offset, BEHIND tells a
    clock that fell behind from one that ran ahead, LINES is the number of
    parsed samples and MEASUREMENT the name of the measurement that showed it
    (set by measure_offset).
    """

    def __init__(self, loss, lines, behind=True, measurement=None):
        super().__init__(
            "the board clock %s by at least %.3f s across %d date lines"
            % ("fell behind" if behind else "ran ahead", loss, lines)
        )
        self.loss = loss
        self.lines = lines
        self.behind = behind
        self.measurement = measurement


def offset_bounds(stamped, samples=SAMPLES):
    """Return (low, high) bounds on host time minus board time, in seconds.

    STAMPED holds (arrival time, console line) pairs. The constant that
    converts the board's local zone to UTC stays in the value; only differences
    between measurements are meaningful. Raises MeasurementError when too few
    lines parse and EmptyBounds when the per-line bounds have no common point,
    which a line delayed beyond SETTLE_MARGIN or a clock that fell behind
    during the loop produces; probe_bounds() tells the two apart.
    """
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
    if used < samples // 2:
        raise MeasurementError("only %d of %d date lines parsed" % (used, samples))
    if low > high:
        raise EmptyBounds(
            "the bounds of %d date lines have no common point (%.3f .. %.3f s)" % (used, low, high)
        )
    return low, high


def probe_bounds(probes, samples=PROBE_SAMPLES):
    """Return (low, high) offset bounds from request-bounded samples.

    PROBES holds (sent, arrival, console line) triples, SENT the host time the
    command left and ARRIVAL the time its date line came back. The line was
    made at x in [sent, arrival], so a steady clock's offset lies in
    (sent - value - 1, arrival - value] for every sample, however long the
    console held the line. Raises ClockUnsteady with the gap between the
    bounds when they have no common point, BEHIND telling a clock that fell
    behind from one that ran ahead, and MeasurementError when too few
    lines parse.
    """
    low = -1e18
    high = 1e18
    low_at = high_at = 0
    used = 0
    for sent, arrival, line in probes:
        value = board_seconds(line)
        if value is None:
            continue
        used += 1
        if sent - value - 1 > low:
            low, low_at = sent - value - 1, used
        if arrival - value < high:
            high, high_at = arrival - value, used
    if used < samples // 2:
        raise MeasurementError("only %d of %d probe lines parsed" % (used, samples))
    if low > high:
        # host - board rises across the probe when the board falls behind, so
        # the binding lower bound then comes from the later sample.
        raise ClockUnsteady(low - high, used, behind=low_at > high_at)
    return low, high


def measure_offset(console, name="measurement"):
    """Run the stamped date loop on the board and bound the offset.

    A ClockUnsteady raised by the loop carries NAME as its measurement."""
    _, stamped = console.run(
        'for i in `seq 1 %d`; do date; done; echo OFF""DONE' % SAMPLES,
        wait=300,
        stop="\nOFFDONE\n",
    )
    try:
        return offset_bounds(stamped)
    except EmptyBounds as empty:
        try:
            probe_bounds(console.probe(PROBE_SAMPLES))
        except ClockUnsteady as error:
            error.measurement = name
            raise
        raise MeasurementError(
            "%s; the request-bounded probe shares an offset, so a line was delayed" % empty
        ) from empty


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
        reach_shell(console)
        return run_check(console)
    except MeasurementError as error:
        print("clock catch-up: INCONCLUSIVE (%s)" % error)
        return INCONCLUSIVE


def run_check(console):
    """Run the three measurements and the workload; return the exit status.

    A clock that lost time during any date loop ends the run as a failure."""
    try:
        return run_measurements(console)
    except ClockUnsteady as error:
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

    start = time.monotonic()
    text, _ = console.run('sh /tmp/cc.sh; echo WORK""STATUS$?', wait=600, stop=WORKLOAD_STATUS)
    workload_s = time.monotonic() - start
    reason = check_workload(text)
    if reason:
        print("workload failed: %s: %r" % (reason, text[-300:]))
        return INCONCLUSIVE
    console.run("rm -f /tmp/cc.sh /tmp/cc.dat", wait=2)
    after_work = measure_offset(console, "after-workload")
    print("offset 2 (after workload, %.1f s): %.3f .. %.3f s" % ((workload_s, *after_work)))

    idle_start = time.monotonic()
    time.sleep(workload_s)
    after_idle = measure_offset(console, "after-idle")
    idle_s = time.monotonic() - idle_start
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
