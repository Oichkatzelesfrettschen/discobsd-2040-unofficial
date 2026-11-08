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


def buffered_lines(cadence=0.14, held=10, late=2.0, spread=0.0):
    """Steady board clock equal to host time: the first lines arrive 5 ms after
    they are made, the last HELD from LATE seconds after the last one, SPREAD
    seconds apart (together when zero)."""
    made = [BASE + cadence * index for index in range(clock.SAMPLES)]
    first_held = clock.SAMPLES - held
    burst = made[-1] + late
    return [
        (
            burst + spread * (index - first_held) if index >= first_held else when + 0.005,
            date_line(when),
        )
        for index, when in enumerate(made)
    ]


def delayed_index():
    return clock.SAMPLES // 2


def probe_lines(offset, seed=1, loss=0.0, delay=None):
    """Request-bounded samples: each "date" is sent, made after a random lag
    and returned after a random delay; DELAY(index, rng) replaces the return
    delay. LOSS adds that many seconds of offset per sample."""
    rng = random.Random(seed)
    sent = BASE
    out = []
    for index in range(clock.PROBE_SAMPLES):
        sent += rng.uniform(0.5, 1.5)
        made = sent + rng.uniform(0.05, 0.6)
        back = delay(index, rng) if delay else rng.uniform(0.0, 0.05)
        out.append((sent, made + back, date_line(made - offset - loss * index)))
        sent = made + back
    return out


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

    def test_a_delayed_or_losing_loop_gives_empty_bounds_but_no_verdict(self):
        # Arrival times alone cannot separate the causes: a held-back tail of
        # a steady clock and a clock losing half a second per line both empty
        # the loop's bounds, and the loop raises neither as a lost clock.
        for stamped in (
            stamped_lines(0.4321, delayed=2.0),
            buffered_lines(),
            buffered_lines(spread=0.005),
            buffered_lines(spread=0.04),
            buffered_lines(held=25),
            stamped_lines(0.4321, loss=0.5),
        ):
            with self.assertRaises(clock.EmptyBounds) as caught:
                clock.offset_bounds(stamped)
            self.assertNotIsInstance(caught.exception, clock.ClockUnsteady)

    def test_probe_of_a_steady_clock_holds_the_offset_whatever_the_delays(self):
        # Delays of up to five seconds, bursts of shared arrivals and slow
        # drains: the true offset lies in every request-bounded interval.
        shapes = (
            None,
            lambda index, rng: rng.uniform(0.0, 5.0),
            lambda index, rng: 3.0 if index >= 30 else 0.01,
            lambda index, rng: 0.0,
            lambda index, rng: 4.0 - 0.1 * index if index < 40 else 0.0,
        )
        for seed in range(25):
            for shape in shapes:
                low, high = clock.probe_bounds(probe_lines(0.4321, seed=seed, delay=shape))
                self.assertLessEqual(low, 0.4321)
                self.assertGreaterEqual(high, 0.4321)

    def test_probe_of_a_losing_clock_fails_with_a_lower_bound_on_the_loss(self):
        with self.assertRaises(clock.ClockUnsteady) as caught:
            clock.probe_bounds(probe_lines(0.4321, loss=0.5))
        lost = 0.5 * (clock.PROBE_SAMPLES - 1)
        self.assertGreater(caught.exception.loss, 1.0)
        self.assertLessEqual(caught.exception.loss, lost)
        self.assertEqual(caught.exception.lines, clock.PROBE_SAMPLES)

    def test_probe_of_a_gaining_clock_fails_as_running_ahead(self):
        with self.assertRaises(clock.ClockUnsteady) as caught:
            clock.probe_bounds(probe_lines(0.4321, loss=-0.5))
        self.assertFalse(caught.exception.behind)
        self.assertIn("ran ahead", str(caught.exception))

    def test_probe_of_a_losing_clock_reports_falling_behind(self):
        with self.assertRaises(clock.ClockUnsteady) as caught:
            clock.probe_bounds(probe_lines(0.4321, loss=0.5))
        self.assertTrue(caught.exception.behind)

    def test_the_settle_margin_cannot_bound_a_probe(self):
        # Calibration: bounding the probe samples by arrival and SETTLE_MARGIN
        # alone, as the loop does, empties the bounds of a steady clock whose
        # replies were delayed.
        probes = probe_lines(0.4321, delay=lambda index, rng: rng.uniform(0.0, 5.0))
        with self.assertRaises(clock.EmptyBounds):
            clock.offset_bounds([(arrival, line) for _, arrival, line in probes])

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

    def __init__(self, offsets, workload, probe_loss=0.0):
        self.offsets = list(offsets)
        self.workload = workload
        self.commands = []
        self.probe_loss = probe_loss
        self.probes = 0

    def probe(self, samples):
        self.probes += 1
        return probe_lines(5.0, loss=self.probe_loss)[:samples]

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
        # Each of the three measurements can show the loss; the probe that
        # follows the loop's empty bounds decides it.
        for slot in range(3):
            offsets = [5.0, 5.0, 5.0]
            offsets[slot] = (5.0, 0.5)
            console = StubConsole(offsets, self.good, probe_loss=0.5)
            status, out = self.quiet(console)
            self.assertEqual(status, 1, slot)
            self.assertIn("FAIL", out)
            self.assertIn("fell behind by at least", out)
            self.assertIn("across %d date lines" % clock.PROBE_SAMPLES, out)
            self.assertEqual(console.probes, 1)

    def test_empty_loop_bounds_with_a_steady_probe_are_inconclusive(self):
        console = StubConsole([5.0, (5.0, 0.5), 5.0], self.good)
        with self.assertRaises(clock.MeasurementError) as caught:
            self.quiet(console)
        self.assertNotIsInstance(caught.exception, clock.ClockUnsteady)
        self.assertIn("delayed", str(caught.exception))

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
        self.assertNotIsInstance(caught.exception, clock.ClockUnsteady)


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


class ArrivalStamps(unittest.TestCase):
    def test_a_wall_clock_step_moves_no_arrival_stamp(self):
        # The host wall clock jumps 3 s between two console reads; the stamps
        # still differ by the monotonic time between them. Stamps taken from
        # time.time() fail this check by the size of the step.
        StubSerialModule.Serial.chunks = [b"a\n", b"b\n"]
        console = clock.Console(StubSerialModule, "stub")
        real = clock.time.time
        first = console.read(1.0, stop="a")[1]
        clock.time.time = lambda: real() + 3.0
        try:
            second = console.read(0.05)[1]
        finally:
            clock.time.time = real
        self.assertEqual([line for _, line in first + second], ["a", "b"])
        self.assertLess(second[0][0] - first[0][0], 1.0)


class TimedSerialModule:
    """A serial port that answers each probe request after a scripted delay.

    DELAYS[n] is the seconds until the reply to request n is readable; a
    reply carries a steady board clock's date line and the request's marker.
    """

    class SerialException(OSError):
        pass

    delays = []

    class Serial:
        def __init__(self, _port, _baud, timeout=0):
            self.pending = []
            self.requests = 0

        def write(self, data):
            text = data.decode()
            if not text.startswith("date; echo"):
                return
            index = int(text.split("BE")[-1])
            delay = TimedSerialModule.delays[self.requests]
            self.requests += 1
            ready = clock.time.monotonic() + delay
            made = clock.host_now()
            echo = "operator@pico:~$ %s\r\n" % text.rstrip("\r")
            reply = echo + "%s\r\nPROBE%d\r\n" % (date_line(made), index)
            # Replies leave in order: one held back holds the later ones.
            if self.pending:
                ready = max(ready, self.pending[-1][0])
            self.pending.append((ready, reply.encode()))

        def read(self, _size):
            now = clock.time.monotonic()
            out = b""
            while self.pending and self.pending[0][0] <= now:
                out += self.pending.pop(0)[1]
            return out


class Probe(unittest.TestCase):
    def console(self, delays):
        TimedSerialModule.delays = list(delays)
        return clock.Console(TimedSerialModule, "stub")

    def test_prompt_replies_give_one_sample_each(self):
        probes = self.console([0.01] * 5).probe(5, wait=1.0)
        self.assertEqual(len(probes), 5)
        for sent, arrival, line in probes:
            self.assertLessEqual(sent, arrival)
            self.assertIsNotNone(clock.board_seconds(line))

    def test_an_unanswered_request_stops_the_probe(self):
        # The first reply is held past the wait and the rest queue behind it;
        # attributing them to later sends could fail a steady clock.
        with self.assertRaises(clock.MeasurementError) as caught:
            self.console([0.6] + [0.01] * 4).probe(5, wait=0.3)
        self.assertNotIsInstance(caught.exception, clock.ClockUnsteady)
        self.assertIn("unanswered", str(caught.exception))

    def test_a_late_reply_is_not_taken_for_the_next_request(self):
        # A reply to a request of an earlier probe, with its marker, arrives
        # ahead of the new request's own; the numbered marker skips it.
        console = self.console([0.01])
        console.requests = 7
        stale = b"Mon Jan  1 00:00:00 PST 2001\r\nPROBE0\r\n"
        console.port.pending.append((0.0, stale))
        probes = console.probe(1, wait=0.5)
        self.assertNotEqual(probes[0][2], "Mon Jan  1 00:00:00 PST 2001")
        self.assertEqual(console.requests, 8)

    def test_a_stale_date_line_ahead_of_the_echo_is_ignored(self):
        # A date line left over from earlier output, with no marker, arrives
        # first; only the line after this request's echo counts.
        console = self.console([0.01])
        console.port.pending.append((0.0, b"Mon Jan  1 00:00:00 PST 2001\r\n"))
        probes = console.probe(1, wait=0.5)
        self.assertNotEqual(probes[0][2], "Mon Jan  1 00:00:00 PST 2001")

    def test_two_date_lines_in_one_window_stop_the_probe(self):
        console = self.console([0.2])
        original = console.port.write

        def doubled(data):
            original(data)
            ready, reply = console.port.pending[-1]
            extra = b"Mon Jan  1 00:00:00 PST 2001\r\n"
            head, tail = reply.split(b"\r\n", 1)
            console.port.pending[-1] = (ready, head + b"\r\n" + extra + tail)

        console.port.write = doubled
        with self.assertRaises(clock.MeasurementError) as caught:
            console.probe(1, wait=0.5)
        self.assertIn("want one", str(caught.exception))

    def test_markers_number_every_request_of_the_console(self):
        console = self.console([0.01] * 4)
        console.probe(2, wait=0.5)
        console.probe(2, wait=0.5)
        self.assertEqual(console.requests, 4)


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
