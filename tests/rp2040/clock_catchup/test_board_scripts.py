#!/usr/bin/env python3
"""Host checks for the decision logic of two board scripts.

board_clock_catchup.py and board_dcbc_smoke.py drive a board over its USB
console. This file imports both without a serial module and exercises their
parsing, interval arithmetic, verdicts and console framing on synthetic input
and a stub console, so it opens no port and calls no flasher. Each check that
guards a defect also runs against the defective form and requires the
rejection to disappear, so a passing run shows the check can fail.

usage: PYTHON test_board_scripts.py
"""

import calendar
import importlib.util
import os
import random
import re
import statistics
import sys
import unittest

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


clock = load("board_clock_catchup", os.path.join(HERE, "board_clock_catchup.py"))
dcbc = load("board_dcbc_smoke", os.path.join(TESTS, "dcbc_smoke", "board_dcbc_smoke.py"))

BASE = 1_800_000_000.123  # a host time in seconds


def date_line(board_time):
    """The line asctime() in lib/libc/gen/asctime.c prints: "%.3s %.3s%3d ..."."""
    tm = __import__("time").gmtime(int(board_time))
    return "%s %s%3d %02d:%02d:%02d PST %d" % (
        ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")[tm.tm_wday],
        clock.MONTHS[tm.tm_mon - 1],
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        tm.tm_year,
    )


def stamped_lines(offset, delayed=None, seed=1, start=BASE, loss=0.0, profile=None):
    """Synthetic date loop: lines made at host time x carry floor(x - offset).

    LOSS adds that many seconds of offset per line, a clock falling behind;
    PROFILE, when given, maps the line index to the added offset instead."""
    rng = random.Random(seed)
    arrival = start
    out = []
    for index in range(clock.SAMPLES):
        arrival += rng.uniform(0.08, 0.2)
        made = arrival - rng.uniform(0.0, 0.03)
        shown = arrival + (delayed if delayed and index == delayed_index() else 0.0)
        extra = profile(index) if profile else loss * index
        out.append((shown, date_line(made - offset - extra)))
    return out


def cadence_lines(cadence, phase, lag=0.01, late=0.5):
    """Steady board clock equal to host time: dates CADENCE seconds apart from
    BASE rounded down plus PHASE, each arriving LAG late and line
    delayed_index() LATE seconds later still."""
    out = []
    for index in range(clock.SAMPLES):
        made = int(BASE) + phase + cadence * index
        shown = made + lag + (late if index == delayed_index() else 0.0)
        out.append((shown, date_line(made)))
    return out


def delayed_index():
    return clock.SAMPLES // 2


class DateParsing(unittest.TestCase):
    def test_space_padded_and_two_digit_days(self):
        cases = {
            "Sat Nov  7 09:47:21 PST 2026": (2026, 11, 7, 9, 47, 21),
            "Fri Nov 06 20:16:31 PST 2026": (2026, 11, 6, 20, 16, 31),
            "Sun Nov 15 00:00:00 PST 2026": (2026, 11, 15, 0, 0, 0),
        }
        for line, fields in cases.items():
            self.assertEqual(clock.board_seconds(line), calendar.timegm(fields))

    def test_garbage_is_not_a_date(self):
        for line in ("", "operator@pico:~$ date", "Sat Nov  7 09:47 PST 2026", "Sat Nov 100 1:2:3"):
            self.assertIsNone(clock.board_seconds(line))

    def test_single_digit_days_bound_the_offset(self):
        # A board clock on the 7th prints the day with two leading spaces.
        start = calendar.timegm((2026, 11, 7, 9, 0, 0, 0, 0, 0)) + 0.1
        lines = stamped_lines(0.4321, start=start)
        self.assertTrue(all("Nov  7" in text for _, text in lines))
        low, high = clock.offset_bounds(lines)
        self.assertLessEqual(low, 0.4321)
        self.assertLessEqual(0.4321, high)

    def test_the_old_pattern_rejects_a_single_digit_day(self):
        old = re.compile(
            r"^(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)"
            r" (\d\d) (\d\d):(\d\d):(\d\d) [A-Z]{2,5} (\d{4})$"
        )
        self.assertIsNone(old.match("Sat Nov  7 09:47:21 PST 2026"))
        self.assertIsNotNone(old.match("Fri Nov 06 20:16:31 PST 2026"))


class OffsetBounds(unittest.TestCase):
    def test_known_good_contains_the_offset(self):
        for offset in (0.4321, 0.9321, 35080.7):
            low, high = clock.offset_bounds(stamped_lines(offset))
            self.assertLessEqual(low, offset)
            self.assertLessEqual(offset, high)
            self.assertLess(high - low, 0.2)

    def test_detects_a_shift(self):
        a = clock.offset_bounds(stamped_lines(0.4321))
        b = clock.offset_bounds(stamped_lines(0.9321))
        shift = sum(b) / 2 - sum(a) / 2
        self.assertAlmostEqual(shift, 0.5, delta=0.15)

    def test_delayed_sample_makes_the_measurement_invalid(self):
        with self.assertRaises(clock.MeasurementError):
            clock.offset_bounds(stamped_lines(0.4321, delayed=0.5))

    def test_the_old_arithmetic_returned_an_empty_interval(self):
        # Calibration: the same per-line bounds without the emptiness test give
        # a low bound above the high bound, which the old code returned as valid.
        low, high = -1e18, 1e18
        for arrival, line in stamped_lines(0.4321, delayed=0.5):
            value = clock.board_seconds(line)
            low = max(low, arrival - clock.SETTLE_MARGIN - value - 1)
            high = min(high, arrival - value)
        self.assertGreater(low, high)

    def test_a_single_two_second_delay_is_not_a_lost_clock(self):
        with self.assertRaises(clock.MeasurementError) as caught:
            clock.offset_bounds(stamped_lines(0.4321, delayed=2.0))
        self.assertNotIsInstance(caught.exception, clock.ClockLostTime)

    def test_a_clock_losing_half_a_second_per_line_is_reported(self):
        with self.assertRaises(clock.ClockLostTime) as caught:
            clock.offset_bounds(stamped_lines(0.4321, loss=0.5))
        self.assertGreaterEqual(caught.exception.loss, 18.0)
        self.assertLess(caught.exception.loss, 21.0)
        self.assertEqual(caught.exception.lines, clock.SAMPLES)

    def test_a_steady_clock_with_a_slow_cadence_and_one_delay_is_not_lost(self):
        # Dates 1.03 s apart walk the estimates up the quantization sawtooth;
        # one line arrives 500 ms late. The board clock matches the host.
        with self.assertRaises(clock.MeasurementError) as caught:
            clock.offset_bounds(cadence_lines(1.03, 0.01))
        self.assertNotIsInstance(caught.exception, clock.ClockLostTime)

    def test_the_slope_rule_alone_called_the_sawtooth_a_lost_clock(self):
        # Calibration: without the quarter-minimum bound the median slope of
        # the same steady input reads as more than MIN_LOST_S lost.
        estimates = [a - clock.board_seconds(line) for a, line in cadence_lines(1.03, 0.01)]
        count = len(estimates)
        slope = statistics.median(
            (estimates[j] - estimates[i]) / (j - i)
            for i in range(count)
            for j in range(i + 1, count)
        )
        self.assertGreater(slope * (count - 1), clock.MIN_LOST_S)

    def test_no_steady_cadence_or_phase_reads_as_a_lost_clock(self):
        for step in range(0, 61, 3):
            for phase in (0.01, 0.25, 0.5, 0.75, 0.97):
                cadence = 1.0 + step / 1000.0
                try:
                    clock.offset_bounds(cadence_lines(cadence, phase))
                except clock.ClockLostTime:
                    self.fail("cadence %.3f phase %.2f" % (cadence, phase))
                except clock.MeasurementError:
                    pass

    def test_a_clock_that_loses_time_and_regains_it_is_inconclusive(self):
        # Rising then falling by the same amount fails the non-decreasing test.
        def triangle(index):
            half = clock.SAMPLES // 2
            return 0.5 * min(index, 2 * half - index)

        with self.assertRaises(clock.MeasurementError) as caught:
            clock.offset_bounds(stamped_lines(0.4321, profile=triangle))
        self.assertNotIsInstance(caught.exception, clock.ClockLostTime)

    def test_the_old_rule_called_a_lost_clock_inconclusive(self):
        # Calibration: the old rule raised MeasurementError for every empty
        # interval. The losing clock's bounds are empty, so the old rule gave
        # exit status 2 where the check now fails.
        low, high = -1e18, 1e18
        for arrival, line in stamped_lines(0.4321, loss=0.5):
            value = clock.board_seconds(line)
            low = max(low, arrival - clock.SETTLE_MARGIN - value - 1)
            high = min(high, arrival - value)
        self.assertGreater(low, high)
        self.assertGreater(low - high, 18.0)

    def test_too_few_parsed_lines(self):
        with self.assertRaises(clock.MeasurementError):
            clock.offset_bounds([(BASE, "noise")] * clock.SAMPLES)


class Verdicts(unittest.TestCase):
    LOST = 8.448  # 24 passes of 32 KB: 192 sectors at 44 ms

    def test_excess_interval_propagates_all_three_bounds(self):
        baseline, work, idle = (0.0, 0.1), (1.0, 1.1), (1.0, 1.1)
        low, high = clock.excess_interval(baseline, work, idle)
        self.assertAlmostEqual(low, 2 * 1.0 - 0.1 - 1.1)
        self.assertAlmostEqual(high, 2 * 1.1 - 0.0 - 1.0)
        self.assertAlmostEqual(high - low, 2 * 0.1 + 0.1 + 0.1)

    def test_three_outcomes(self):
        tolerance = self.LOST * clock.TOLERANCE_FRACTION
        self.assertEqual(clock.verdict((-0.1, 0.2), self.LOST)[0], "PASS")
        self.assertEqual(clock.verdict((tolerance + 0.1, tolerance + 0.5), self.LOST)[0], "FAIL")
        self.assertEqual(clock.verdict((-tolerance - 0.5, -tolerance - 0.1), self.LOST)[0], "FAIL")
        self.assertEqual(
            clock.verdict((tolerance - 0.2, tolerance + 0.2), self.LOST)[0], "INCONCLUSIVE"
        )
        self.assertEqual(clock.verdict((-0.1, 0.1), 0.5)[0], "INCONCLUSIVE")

    def test_midpoint_rule_would_have_passed_an_inconclusive_interval(self):
        # Calibration: the interval straddles the tolerance, its midpoint lies
        # inside it, and the old rule (midpoint only) called that a pass.
        tolerance = self.LOST * clock.TOLERANCE_FRACTION
        interval = (tolerance - 0.5, tolerance + 0.3)
        midpoint = sum(interval) / 2
        self.assertLessEqual(abs(midpoint), tolerance)
        self.assertEqual(clock.verdict(interval, self.LOST)[0], "INCONCLUSIVE")

    def test_recorded_board_runs(self):
        # Offsets printed by the 2026-11-07 runs (docs/research/picocalc-session-handoff.md).
        runs = (
            # shipped kernel, 24 passes
            ((35080.864, 35080.989), (35081.263, 35081.325), (35081.305, 35081.377), 8.448, "PASS"),
            # shipped kernel, 12 passes
            ((35081.338, 35081.419), (35081.527, 35081.617), (35081.580, 35081.678), 4.224, "PASS"),
        )
        for baseline, work, idle, lost, expected in runs:
            result = clock.verdict(clock.excess_interval(baseline, work, idle), lost)[0]
            self.assertEqual(result, expected)


class Workload(unittest.TestCase):
    PASSES = 3

    def good(self, passes=None):
        passes = passes or self.PASSES
        records = "32+0 records in\n32+0 records out\n" * passes
        return records + "WORKDONE\nWORKSTATUS0\n"

    def test_complete_run_is_accepted(self):
        self.assertIsNone(clock.check_workload(self.good(), self.PASSES))

    def test_script_stops_on_the_first_failure(self):
        self.assertEqual(clock.workload_script()[0], "set -e")
        self.assertEqual(clock.workload_script()[-1], "echo WORKDONE")

    def test_failed_dd_is_rejected(self):
        text = (
            "32+0 records in\n32+0 records out\n"
            "write: No space left on device\n0+0 records in\n0+0 records out\n"
            "WORKSTATUS1\n"
        )
        self.assertIn("status 1", clock.check_workload(text, self.PASSES))

    def test_marker_without_all_writes_is_rejected(self):
        # The old script printed the marker whatever dd did and discarded the
        # diagnostics; the byte count catches it even with status 0.
        text = "32+0 records out\n" * (self.PASSES - 1) + "WORKDONE\nWORKSTATUS0\n"
        self.assertIn("full", clock.check_workload(text, self.PASSES))
        short = (
            "32+0 records out\n" * (self.PASSES - 1) + "10+1 records out\nWORKDONE\nWORKSTATUS0\n"
        )
        self.assertIn("partial", clock.check_workload(short, self.PASSES))

    def test_missing_marker_or_status(self):
        self.assertIn("marker", clock.check_workload("32+0 records out\nWORKSTATUS0\n", 1))
        self.assertIn("status", clock.check_workload("WORKDONE\n", 1))


class StubConsole:
    """A console whose answers come from a table; no serial port exists."""

    def __init__(self, offsets, workload):
        self.offsets = list(offsets)
        self.workload = workload
        self.commands = []

    def run(self, command, wait=8, stop=None):
        self.commands.append(command)
        if command.startswith("for i in"):
            entry = self.offsets.pop(0)
            offset, loss = entry if isinstance(entry, tuple) else (entry, 0.0)
            return "", stamped_lines(offset, loss=loss)
        if command.startswith("cat /tmp/cc.sh"):
            return "\n".join(clock.workload_script()) + "\nSCRIPTEND\n", []
        if command.startswith("sh /tmp/cc.sh"):
            return self.workload, []
        return "", []


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.passes = clock.PASSES
        self.good = "32+0 records in\n32+0 records out\n" * self.passes + "WORKDONE\nWORKSTATUS0\n"
        self.slept = clock.time.sleep
        clock.time.sleep = lambda _seconds: None

    def tearDown(self):
        clock.time.sleep = self.slept

    def quiet(self, console):
        import contextlib
        import io

        with contextlib.redirect_stdout(io.StringIO()) as out:
            status = clock.run_check(console)
        return status, out.getvalue()

    def test_steady_clock_passes(self):
        status, _ = self.quiet(StubConsole([5.0, 5.0, 5.0], self.good))
        self.assertEqual(status, 0)

    def test_losing_clock_fails(self):
        status, _ = self.quiet(StubConsole([5.0, 15.0, 15.0], self.good))
        self.assertEqual(status, 1)

    def test_clock_losing_time_inside_a_date_loop_fails(self):
        # Each of the three measurements can show the loss.
        for slot in range(3):
            offsets = [5.0, 5.0, 5.0]
            offsets[slot] = (5.0, 0.5)
            console = StubConsole(offsets, self.good)
            status, out = self.quiet(console)
            self.assertEqual(status, 1, slot)
            self.assertIn("FAIL", out)
            self.assertIn("fell behind by", out)
            self.assertIn("across %d date lines" % clock.SAMPLES, out)

    def test_borderline_clock_is_inconclusive(self):
        tolerance = clock.PASSES * clock.WORKLOAD_BLOCKS * 1024 // clock.SECTOR_BYTES
        tolerance *= clock.ERASE_MS / 1000.0 * clock.TOLERANCE_FRACTION
        status, _ = self.quiet(StubConsole([5.0, 5.0 + tolerance, 5.0 + tolerance], self.good))
        self.assertEqual(status, clock.INCONCLUSIVE)

    def test_failed_dd_stops_the_check(self):
        bad = "dd: write error\nWORKSTATUS1\n"
        console = StubConsole([5.0, 5.0, 5.0], bad)
        status, out = self.quiet(console)
        self.assertEqual(status, clock.INCONCLUSIVE)
        self.assertIn("workload failed", out)
        self.assertEqual(sum(c.startswith("for i in") for c in console.commands), 1)

    def test_invalid_measurement_is_inconclusive(self):
        class Delayed(StubConsole):
            def run(self, command, wait=8, stop=None):
                if command.startswith("for i in"):
                    return "", stamped_lines(5.0, delayed=0.5)
                return super().run(command, wait, stop)

        with self.assertRaises(clock.MeasurementError) as caught:
            self.quiet(Delayed([], self.good))
        self.assertNotIsInstance(caught.exception, clock.ClockLostTime)


class StubSerialModule:
    class SerialException(OSError):
        pass

    class Serial:
        chunks = []

        def __init__(self, _port, _baud, timeout=0):
            pass

        def write(self, data):
            self.written = data

        def read(self, _size):
            return StubSerialModule.Serial.chunks.pop(0) if StubSerialModule.Serial.chunks else b""


class SentinelFraming(unittest.TestCase):
    OLD = re.compile(r"^ZD(\d+)$", re.M)

    def script(self):
        # Echo of the typed line, the output, a blank line, then the sentinel
        # split inside its status digits.
        StubSerialModule.Serial.chunks = [
            b"operator@pico:~$ cmd; echo; echo ZD$?\r\n",
            b"7\r\n\r\nZD1",
            b"2\r\n",
            b"operator@pico:~$ ",
        ]

    def test_whole_sentinel_is_read_before_returning(self):
        self.script()
        console = dcbc.Console(StubSerialModule, "stub")
        lines = console.run("cmd", wait=2)
        self.assertEqual(lines, ["7"])
        self.assertEqual(StubSerialModule.Serial.chunks, [b"operator@pico:~$ "])

    def test_old_regex_stops_mid_sentinel(self):
        original = dcbc.SENTINEL
        dcbc.SENTINEL = self.OLD
        try:
            self.script()
            console = dcbc.Console(StubSerialModule, "stub")
            console.run("cmd", wait=2)
            # The old pattern matched "ZD1" at the end of the second chunk and
            # left the digit and the newline unread.
            self.assertEqual(len(StubSerialModule.Serial.chunks), 2)
        finally:
            dcbc.SENTINEL = original

    def test_sentinel_split_before_the_newline_is_awaited(self):
        StubSerialModule.Serial.chunks = [b"cmd\r\nx\r\n\r\nZD0", b"\r\n"]
        console = dcbc.Console(StubSerialModule, "stub")
        self.assertEqual(console.run("cmd", wait=2), ["x"])
        self.assertEqual(StubSerialModule.Serial.chunks, [])

    def test_missing_sentinel_times_out(self):
        StubSerialModule.Serial.chunks = [b"cmd\r\nx\r\n"]
        console = dcbc.Console(StubSerialModule, "stub")
        self.assertTrue(console.run("cmd", wait=0.3)[0].startswith("TIMEOUT"))


if __name__ == "__main__":
    unittest.main()
