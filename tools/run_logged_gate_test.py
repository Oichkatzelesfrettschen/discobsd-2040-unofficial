"""Calibrate logged gate progress, verdicts, and process-group timeouts."""

import contextlib
import io
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import run_logged_gate as runner

ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / "tools/run_logged_gate.py"


class LoggedGateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-logged-gate-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.log = self.directory / "gate.log"
        self.status = self.directory / "gate.status.json"

    def run_gate(self, program, *, heartbeat="0.05", idle="1", timeout="2",
                 program_arguments=()):
        return subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--label", "fixture",
                "--log", str(self.log),
                "--status", str(self.status),
                "--heartbeat-seconds", heartbeat,
                "--idle-timeout-seconds", idle,
                "--timeout-seconds", timeout,
                "--",
                sys.executable,
                "-c", program,
                *map(str, program_arguments),
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    def read_status(self):
        return json.loads(self.status.read_text(encoding="utf-8"))

    def test_success_streams_combined_output_and_records_completion(self):
        result = self.run_gate(
            "import sys, time; print('fixture stdout', flush=True); "
            "time.sleep(0.1); "
            "print('fixture stderr', file=sys.stderr, flush=True)"
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("fixture stdout", result.stdout)
        self.assertIn("fixture stderr", result.stdout)
        status = self.read_status()
        self.assertEqual(status["state"], "completed")
        self.assertEqual(status["exit_code"], 0)
        self.assertIsNone(status["termination"])
        self.assertGreater(status["output_bytes"], 0)
        self.assertEqual(status["last_output"], "fixture stderr")
        log_text = self.log.read_text(encoding="utf-8")
        self.assertIn("fixture stdout", log_text)
        self.assertIn("fixture stderr", log_text)
        self.assertIn("[logged-gate] completed", log_text)

    def test_child_failure_status_is_preserved(self):
        result = self.run_gate("raise SystemExit(7)")

        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        status = self.read_status()
        self.assertEqual(status["state"], "failed")
        self.assertEqual(status["exit_code"], 7)
        self.assertEqual(status["child_exit_code"], 7)
        self.assertIsNone(status["termination"])

    def test_idle_timeout_emits_heartbeats_and_stops_the_process_group(self):
        terminated = self.directory / "descendant-terminated"
        descendant = (
            "import pathlib, signal, sys, time; marker = pathlib.Path(sys.argv[1]); "
            "signal.signal(signal.SIGTERM, lambda *_: "
            "(marker.write_text('terminated', encoding='ascii'), sys.exit(0))); "
            "print('descendant ready', flush=True); time.sleep(60)"
        )
        parent = (
            "import subprocess, sys, time; "
            "subprocess.Popen([sys.executable, '-c', sys.argv[1], sys.argv[2]]); "
            "time.sleep(60)"
        )
        result = self.run_gate(
            parent,
            heartbeat="0.1",
            idle="0.5",
            timeout="2",
            program_arguments=(descendant, terminated),
        )

        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        self.assertIn("[logged-gate] heartbeat", result.stdout)
        self.assertTrue(
            terminated.is_file(),
            result.stdout + result.stderr + self.log.read_text(encoding="utf-8"),
        )
        status = self.read_status()
        self.assertEqual(status["state"], "timed_out")
        self.assertEqual(status["termination"], "idle_timeout")
        self.assertLess(status["elapsed_seconds"], 2)
        control_lines = [
            line for line in result.stdout.splitlines()
            if line.startswith("[logged-gate]")
        ]
        self.assertGreaterEqual(len(control_lines), 3)
        for line in control_lines:
            self.assertRegex(line, r"\btime=\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z\b")

    def test_hard_timeout_fires_while_output_remains_active(self):
        program = (
            "import time; value = 0\n"
            "while True:\n"
            " print(value, flush=True); value += 1; time.sleep(0.02)\n"
        )
        result = self.run_gate(program, heartbeat="0.04", idle="1", timeout="0.18")

        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        status = self.read_status()
        self.assertEqual(status["state"], "timed_out")
        self.assertEqual(status["termination"], "hard_timeout")
        self.assertGreater(status["output_bytes"], 0)

    def test_timeout_retains_shutdown_output(self):
        program = (
            "import signal, sys, time; "
            "signal.signal(signal.SIGTERM, lambda *_: "
            "(print('SHUTDOWN-MARKER', flush=True), sys.exit(0))); "
            "print('ready', flush=True); time.sleep(60)"
        )
        result = self.run_gate(program, heartbeat="0.05", idle="0.2", timeout="2")

        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        self.assertIn("SHUTDOWN-MARKER", self.log.read_text(encoding="utf-8"))
        status = self.read_status()
        self.assertEqual(status["last_output"], "SHUTDOWN-MARKER")

    def test_signal_wakes_a_quiet_runner_promptly(self):
        process = subprocess.Popen(
            [
                sys.executable,
                str(RUNNER),
                "--label", "signal-fixture",
                "--log", str(self.log),
                "--status", str(self.status),
                "--heartbeat-seconds", "30",
                "--idle-timeout-seconds", "0",
                "--timeout-seconds", "60",
                "--",
                sys.executable,
                "-c", "import time; print('ready', flush=True); time.sleep(60)",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.addCleanup(self.stop_runner, process)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if self.status.is_file() and "ready" in self.log.read_text(encoding="utf-8"):
                break
            time.sleep(0.01)
        else:
            self.fail("runner did not reach the running state")

        time.sleep(0.1)
        started = time.monotonic()
        process.send_signal(signal.SIGTERM)
        output, _ = process.communicate(timeout=2)

        self.assertEqual(process.returncode, 128 + signal.SIGTERM, output)
        self.assertLess(time.monotonic() - started, 1)
        self.assertEqual(self.read_status()["termination"], "signal_15")

    def test_receipt_failure_after_child_ready_stops_the_process_group(self):
        marker = self.directory / "receipt-child-terminated"
        arguments = runner.parse_arguments([
            "--label", "receipt-failure",
            "--log", str(self.log),
            "--status", str(self.status),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "2",
            "--timeout-seconds", "3",
            "--",
            sys.executable,
            "-c",
            "import pathlib, signal, sys, time; marker=pathlib.Path(sys.argv[1]); "
            "signal.signal(signal.SIGTERM, lambda *_: "
            "(marker.write_text('terminated', encoding='ascii'), sys.exit(0))); "
            "print('ready', flush=True); time.sleep(60)",
            str(marker),
        ])
        original_status = runner.atomic_status
        injected = False

        def fail_after_launch(path, record):
            nonlocal injected
            if not injected and record["last_output"] == "ready":
                injected = True
                raise OSError("injected receipt failure")
            original_status(path, record)

        with mock.patch.object(runner, "atomic_status", side_effect=fail_after_launch):
            result = runner.run(arguments)

        self.assertEqual(result, runner.RUNNER_ERROR_EXIT)
        self.assertTrue(injected)
        self.assertTrue(marker.is_file())

    def test_log_failure_stops_the_launched_process_group(self):
        marker = self.directory / "log-child-terminated"
        arguments = runner.parse_arguments([
            "--label", "log-failure",
            "--log", str(self.log),
            "--status", str(self.status),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "2",
            "--timeout-seconds", "3",
            "--",
            sys.executable,
            "-c",
            "import pathlib, signal, sys, time; marker=pathlib.Path(sys.argv[1]); "
            "signal.signal(signal.SIGTERM, lambda *_: "
            "(marker.write_text('terminated', encoding='ascii'), sys.exit(0))); "
            "print('ready', flush=True); time.sleep(60)",
            str(marker),
        ])
        injected = False

        def fail_after_launch(stream, data):
            nonlocal injected
            if not injected and data.strip() == b"ready":
                injected = True
                raise OSError("injected log failure")
            stream.write(data)
            stream.flush()

        with mock.patch.object(
                runner, "write_log", create=True, side_effect=fail_after_launch
        ):
            result = runner.run(arguments)

        self.assertEqual(result, runner.RUNNER_ERROR_EXIT)
        self.assertTrue(injected)
        self.assertTrue(marker.is_file())

    def test_terminal_backpressure_cannot_suspend_the_hard_deadline(self):
        process = subprocess.Popen(
            [
                sys.executable,
                str(RUNNER),
                "--label", "backpressure-fixture",
                "--log", str(self.log),
                "--status", str(self.status),
                "--heartbeat-seconds", "0.05",
                "--idle-timeout-seconds", "1",
                "--timeout-seconds", "0.2",
                "--",
                sys.executable,
                "-c",
                "import sys, time; "
                "[sys.stdout.write('x' * 65536) for _ in range(64)]; "
                "sys.stdout.flush(); time.sleep(60)",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.addCleanup(self.stop_runner, process)

        try:
            return_code = process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.stop_runner(process)
            self.fail("terminal backpressure suspended the runner deadline")

        self.assertEqual(return_code, 124)
        self.assertEqual(self.read_status()["termination"], "hard_timeout")

    def test_invalid_deadline_is_rejected_before_launch(self):
        result = self.run_gate("print('must not run')", heartbeat="0")

        self.assertEqual(result.returncode, 2)
        self.assertIn("heartbeat", result.stderr)
        self.assertFalse(self.log.exists())
        self.assertFalse(self.status.exists())

    def test_nonfinite_deadlines_are_rejected_before_launch(self):
        for option in (
            "--heartbeat-seconds",
            "--idle-timeout-seconds",
            "--timeout-seconds",
            "--terminate-grace-seconds",
        ):
            for value in ("nan", "inf", "-inf"):
                with self.subTest(option=option, value=value):
                    arguments = [
                        "--label", "fixture",
                        "--log", str(self.log),
                        "--status", str(self.status),
                        f"{option}={value}",
                        "--",
                        sys.executable,
                        "-c", "print('must not run')",
                    ]

                    with contextlib.redirect_stderr(io.StringIO()):
                        with self.assertRaises(SystemExit) as error:
                            runner.parse_arguments(arguments)

                    self.assertEqual(error.exception.code, 2)
                    self.assertFalse(self.log.exists())
                    self.assertFalse(self.status.exists())

    def stop_runner(self, process):
        if process.poll() is not None:
            if process.stdout is not None:
                process.stdout.close()
            return
        process.kill()
        process.wait()
        if self.status.is_file():
            process_group = self.read_status().get("pid")
            if process_group is not None:
                try:
                    os.killpg(process_group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        if process.stdout is not None:
            process.stdout.close()


if __name__ == "__main__":
    unittest.main()
