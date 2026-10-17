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
TEST_HELPER_TIMEOUT_SECONDS = 5


class LoggedGateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-logged-gate-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.log = self.directory / "gate.log"
        self.status = self.directory / "gate.status.json"

    def run_gate(self, program, *, heartbeat="0.05", idle="1", timeout="2",
                 program_arguments=(), execution_lock=None):
        lock_arguments = (
            ["--execution-lock", str(execution_lock)]
            if execution_lock is not None else []
        )
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
                *lock_arguments,
                "--",
                sys.executable,
                "-c", program,
                *map(str, program_arguments),
            ],
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
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

    def test_stop_process_group_reports_an_unreaped_process_within_its_budget(self):
        class UnreapedProcess:
            pid = 4242

            def __init__(self):
                self.wait_timeouts = []
                self.kill_calls = 0

            def poll(self):
                return None

            def kill(self):
                self.kill_calls += 1

            def wait(self, timeout=None):
                self.wait_timeouts.append(timeout)
                raise subprocess.TimeoutExpired("fixture", timeout)

        process = UnreapedProcess()
        started = time.monotonic()
        with mock.patch.object(runner.os, "killpg"):
            with mock.patch.object(runner, "group_exists", return_value=True):
                result = runner.stop_process_group(process, 0.01)

        self.assertLess(time.monotonic() - started, 0.5)
        self.assertIsNone(result.child_exit_code)
        self.assertFalse(result.group_stopped)
        self.assertGreaterEqual(len(process.wait_timeouts), 1)
        self.assertNotIn(None, process.wait_timeouts)
        self.assertEqual(process.kill_calls, 1)

    def test_timeout_retains_process_tree_snapshot_before_termination(self):
        program = (
            "import subprocess, sys, time; "
            "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)']); "
            "print('snapshot-ready', flush=True); time.sleep(60)"
        )

        result = self.run_gate(
            program,
            heartbeat="0.05",
            idle="1",
            timeout="0.2",
        )

        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        log_text = self.log.read_text(encoding="utf-8")
        self.assertIn("[logged-gate] process-snapshot", log_text)
        self.assertIn("PID PPID PGID STATE ELAPSED CPU COMMAND", log_text)
        process_lines = [
            line for line in log_text.splitlines()
            if line.startswith("[logged-gate] process ")
        ]
        self.assertGreaterEqual(len(process_lines), 2)
        self.assertTrue(any("time.sleep(60)" in line for line in process_lines))

    def test_process_snapshot_uses_portable_ps_argument_vector(self):
        fake_ps = self.directory / "ps"
        fake_ps.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            "test \"$#\" -eq 3\n"
            "test \"$1\" = -A\n"
            "test \"$2\" = -o\n"
            "test \"$3\" = "
            "pid=,ppid=,pgid=,state=,etime=,time=,command=\n"
            "printf '%s\\n' "
            "'4242 1 4242 S 00:01 0:00 snapshot-parent' "
            "'4243 4242 4242 S 00:01 0:00 snapshot-child'\n",
            encoding="ascii",
        )
        fake_ps.chmod(0o755)
        environment = {"PATH": f"{self.directory}{os.pathsep}{os.environ['PATH']}"}

        with mock.patch.dict(os.environ, environment):
            snapshot = runner.process_tree_snapshot(4242)

        process_lines = [
            line for line in snapshot.splitlines()
            if line.startswith("[logged-gate] process ")
        ]
        self.assertEqual(len(process_lines), 2, snapshot)
        self.assertTrue(any("snapshot-parent" in line for line in process_lines))
        self.assertTrue(any("snapshot-child" in line for line in process_lines))

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

    def test_nonregular_log_sink_is_rejected_before_launch(self):
        marker = self.directory / "child-launched"
        os.mkfifo(self.log)

        result = self.run_gate(
            "import pathlib, sys; pathlib.Path(sys.argv[1]).write_text("
            "'launched', encoding='ascii')",
            program_arguments=(marker,),
        )

        self.assertEqual(result.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertIn("regular file", result.stderr)
        self.assertFalse(marker.exists())
        self.assertEqual(self.read_status()["termination"], "log_open_error")

    def test_concurrent_runner_cannot_truncate_active_log(self):
        first_command = [
            sys.executable,
            str(RUNNER),
            "--label", "first-owner",
            "--log", str(self.log),
            "--status", str(self.status),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "2",
            "--timeout-seconds", "3",
            "--",
            sys.executable,
            "-c",
            "import time; print('FIRST-START', flush=True); "
            "time.sleep(0.4); print('FIRST-END', flush=True)",
        ]
        first = subprocess.Popen(
            first_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.addCleanup(self.stop_runner, first)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if self.log.is_file() and "FIRST-START" in self.log.read_text(
                    encoding="utf-8"):
                break
            time.sleep(0.01)
        else:
            self.fail("first runner did not publish its start marker")

        second = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--label", "second-owner",
                "--log", str(self.log),
                "--status", str(self.status),
                "--heartbeat-seconds", "0.05",
                "--idle-timeout-seconds", "1",
                "--timeout-seconds", "2",
                "--",
                sys.executable,
                "-c", "print('SECOND-RAN', flush=True)",
            ],
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )
        first_output, _ = first.communicate(timeout=TEST_HELPER_TIMEOUT_SECONDS)

        self.assertEqual(first.returncode, 0, first_output)
        self.assertEqual(second.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertIn("active log owner", second.stderr)
        log_text = self.log.read_text(encoding="utf-8")
        self.assertIn("FIRST-START", log_text)
        self.assertIn("FIRST-END", log_text)
        self.assertNotIn("SECOND-RAN", log_text)
        self.assertEqual(self.read_status()["label"], "first-owner")

    def test_evidence_root_retains_sequential_invocations(self):
        evidence_root = self.directory / "evidence"
        for marker in ("FIRST-EVIDENCE", "SECOND-EVIDENCE"):
            result = subprocess.run(
                [
                    sys.executable,
                    str(RUNNER),
                    "--label", "retained-run",
                    "--evidence-root", str(evidence_root),
                    "--heartbeat-seconds", "0.05",
                    "--idle-timeout-seconds", "1",
                    "--timeout-seconds", "2",
                    "--",
                    sys.executable,
                    "-c", f"print({marker!r}, flush=True)",
                ],
                text=True,
                capture_output=True,
                check=False,
                timeout=TEST_HELPER_TIMEOUT_SECONDS,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        invocation_directories = sorted(
            path for path in evidence_root.glob("retained-run-*")
            if path.is_dir()
        )
        self.assertEqual(len(invocation_directories), 2)
        retained_logs = [
            (path / "gate.log").read_text(encoding="utf-8")
            for path in invocation_directories
        ]
        self.assertTrue(any("FIRST-EVIDENCE" in log for log in retained_logs))
        self.assertTrue(any("SECOND-EVIDENCE" in log for log in retained_logs))
        for path in invocation_directories:
            status = json.loads((path / "status.json").read_text(encoding="utf-8"))
            self.assertEqual(status["label"], "retained-run")
            self.assertEqual(status["evidence_directory"], str(path))
            self.assertEqual(status["invocation_id"], path.name)

        latest = json.loads(
            (evidence_root / "retained-run.latest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(latest["state"], "completed")
        self.assertIn(Path(latest["evidence_directory"]), invocation_directories)

    def test_unusable_evidence_root_fails_without_launch_or_traceback(self):
        evidence_root = self.directory / "occupied-evidence-root"
        evidence_root.write_text("regular file\n", encoding="ascii")
        child_marker = self.directory / "child-launched"

        result = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--label", "unusable-evidence",
                "--evidence-root", str(evidence_root),
                "--",
                sys.executable,
                "-c",
                "import pathlib, sys; pathlib.Path(sys.argv[1]).touch()",
                str(child_marker),
            ],
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )

        self.assertEqual(result.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertIn("logged-gate:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(child_marker.exists())

    def test_execution_lock_rejects_overlapping_product_mutation(self):
        execution_lock = self.directory / "product.lock"
        first_log = self.directory / "first.log"
        first_status = self.directory / "first.status.json"
        first = subprocess.Popen(
            [
                sys.executable,
                str(RUNNER),
                "--label", "first-product-owner",
                "--log", str(first_log),
                "--status", str(first_status),
                "--execution-lock", str(execution_lock),
                "--heartbeat-seconds", "0.05",
                "--idle-timeout-seconds", "2",
                "--timeout-seconds", "3",
                "--",
                sys.executable,
                "-c",
                "import time; print('PRODUCT-LOCKED', flush=True); time.sleep(0.4)",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.addCleanup(self.stop_runner, first)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if first_log.is_file() and "PRODUCT-LOCKED" in first_log.read_text(
                    encoding="utf-8"):
                break
            time.sleep(0.01)
        else:
            self.fail("first runner did not acquire the execution lock")

        second_log = self.directory / "second.log"
        second_status = self.directory / "second.status.json"
        second_marker = self.directory / "second-launched"
        second = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--label", "second-product-owner",
                "--log", str(second_log),
                "--status", str(second_status),
                "--execution-lock", str(execution_lock),
                "--heartbeat-seconds", "0.05",
                "--idle-timeout-seconds", "1",
                "--timeout-seconds", "2",
                "--",
                sys.executable,
                "-c",
                "import pathlib, sys; pathlib.Path(sys.argv[1]).write_text("
                "'launched', encoding='ascii')",
                str(second_marker),
            ],
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )
        first_output, _ = first.communicate(timeout=TEST_HELPER_TIMEOUT_SECONDS)

        self.assertEqual(first.returncode, 0, first_output)
        self.assertEqual(second.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertIn("active execution owner", second.stderr)
        self.assertFalse(second_marker.exists())
        self.assertFalse(second_log.exists())
        second_record = json.loads(second_status.read_text(encoding="utf-8"))
        self.assertEqual(second_record["state"], "error")
        self.assertEqual(second_record["termination"], "execution_lock_busy")

    def test_relative_execution_lock_survives_nested_working_directory(self):
        nested_directory = self.directory / "nested"
        nested_directory.mkdir()
        nested_program = (
            "import os, sys; "
            "sys.path.insert(0, sys.argv[1]); "
            "import test_execution; "
            "os.chdir(sys.argv[2]); "
            "result, _termination = test_execution.run_bounded("
            "[sys.executable, '-c', 'raise SystemExit(0)'], 1.0); "
            "raise SystemExit(result)"
        )
        result = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--label", "relative-ownership",
                "--log", str(self.log),
                "--status", str(self.status),
                "--execution-lock", "products.lock",
                "--heartbeat-seconds", "0.05",
                "--idle-timeout-seconds", "1",
                "--timeout-seconds", "2",
                "--",
                sys.executable,
                "-c", nested_program, str(ROOT / "tools"), str(nested_directory),
            ],
            cwd=self.directory,
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ownership = json.loads(
            (self.directory / "products.lock.ownership.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(ownership["state"], "released")
        self.assertEqual(ownership["process_groups"], [])

    def test_registration_error_with_incomplete_cleanup_blocks_successor(self):
        execution_lock = self.directory / "products.lock"
        marker = self.directory / "successor-launched"
        arguments = runner.parse_arguments([
            "--label", "registration-cleanup-incomplete",
            "--log", str(self.log),
            "--status", str(self.status),
            "--execution-lock", str(execution_lock),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "1",
            "--timeout-seconds", "2",
            "--terminate-grace-seconds", "0.1",
            "--", sys.executable, "-c", "import time; time.sleep(60)",
        ])
        original_stop = runner.stop_process_group

        def report_incomplete(process, grace_seconds, **kwargs):
            result = original_stop(process, grace_seconds, **kwargs)
            return runner.StopResult(result.child_exit_code, False)

        with mock.patch.object(
            runner, "register_execution_group",
            side_effect=OSError("injected registration failure"),
        ), mock.patch.object(
            runner, "stop_process_group", side_effect=report_incomplete
        ):
            result = runner.run(arguments)

        self.assertEqual(result, runner.RUNNER_ERROR_EXIT)
        first_status = self.read_status()
        self.assertEqual(first_status["termination"], "cleanup_incomplete")
        ownership = json.loads(
            Path(f"{execution_lock}.ownership.json").read_text(encoding="utf-8")
        )
        self.assertEqual(ownership["state"], "cleanup_incomplete")

        successor = self.run_gate(
            "import pathlib, sys; pathlib.Path(sys.argv[1]).write_text("
            "'launched', encoding='ascii')",
            program_arguments=(marker,),
            execution_lock=execution_lock,
        )
        self.assertEqual(successor.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertFalse(marker.exists())
        self.assertEqual(
            self.read_status()["termination"], "execution_quarantined"
        )

    def test_cleanup_incomplete_publishes_product_quarantine(self):
        execution_lock = self.directory / "products.lock"
        quarantine = Path(f"{execution_lock}.quarantine.json")
        arguments = runner.parse_arguments([
            "--label", "cleanup-incomplete",
            "--log", str(self.log),
            "--status", str(self.status),
            "--execution-lock", str(execution_lock),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "0",
            "--timeout-seconds", "0.1",
            "--terminate-grace-seconds", "0.1",
            "--",
            sys.executable,
            "-c", "import time; time.sleep(60)",
        ])
        original_stop = runner.stop_process_group

        def report_incomplete(process, grace_seconds, **kwargs):
            result = original_stop(process, grace_seconds, **kwargs)
            return runner.StopResult(result.child_exit_code, False)

        with mock.patch.object(
            runner, "stop_process_group", side_effect=report_incomplete
        ):
            result = runner.run(arguments)

        self.assertEqual(result, runner.RUNNER_ERROR_EXIT)
        status = self.read_status()
        self.assertEqual(status["state"], "error")
        self.assertEqual(status["termination"], "cleanup_incomplete")
        quarantine_record = json.loads(quarantine.read_text(encoding="utf-8"))
        self.assertEqual(quarantine_record["state"], "cleanup_incomplete")
        self.assertEqual(quarantine_record["process_group"], status["pid"])
        self.assertEqual(quarantine_record["invocation_id"], status["invocation_id"])
        self.assertEqual(quarantine_record["status_path"], str(self.status))

    def test_live_product_quarantine_rejects_mutation_before_launch(self):
        execution_lock = self.directory / "products.lock"
        quarantine = Path(f"{execution_lock}.quarantine.json")
        marker = self.directory / "launched"
        sleeper = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(60)"],
            start_new_session=True,
        )
        self.addCleanup(self.stop_runner, sleeper)
        quarantine.write_text(
            json.dumps({
                "schema": 1,
                "state": "cleanup_incomplete",
                "process_group": sleeper.pid,
                "label": "previous-owner",
                "invocation_id": "previous-invocation",
            }) + "\n",
            encoding="utf-8",
        )

        result = self.run_gate(
            "import pathlib, sys; pathlib.Path(sys.argv[1]).write_text("
            "'launched', encoding='ascii')",
            program_arguments=(marker,),
            execution_lock=execution_lock,
        )

        self.assertEqual(result.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertFalse(marker.exists())
        self.assertEqual(self.read_status()["termination"], "execution_quarantined")

    def test_expired_product_quarantine_permits_mutation_and_remains_evidence(self):
        execution_lock = self.directory / "products.lock"
        quarantine = Path(f"{execution_lock}.quarantine.json")
        quarantine_text = json.dumps({
            "schema": 1,
            "state": "cleanup_incomplete",
            "process_group": 2147483647,
            "label": "previous-owner",
            "invocation_id": "previous-invocation",
        }) + "\n"
        quarantine.write_text(quarantine_text, encoding="utf-8")

        result = self.run_gate(
            "print('replacement ran', flush=True)",
            execution_lock=execution_lock,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(quarantine.read_text(encoding="utf-8"), quarantine_text)

    def test_nested_cleanup_incomplete_quarantines_successor_before_launch(self):
        execution_lock = self.directory / "products.lock"
        group_file = self.directory / "nested-group"
        marker = self.directory / "successor-launched"
        nested = self.directory / "nested-cleanup.py"
        nested_child = (
            "import os, pathlib, sys, time; "
            "pathlib.Path(sys.argv[1]).write_text("
            "str(os.getpgrp()), encoding='ascii'); time.sleep(60)"
        )
        nested.write_text(
            "import os, pathlib, sys\n"
            f"sys.path.insert(0, {str(ROOT / 'tools')!r})\n"
            "import test_execution\n"
            "group_file = pathlib.Path(sys.argv[1])\n"
            f"child = {nested_child!r}\n"
            "def leave_group_running(process, _grace_seconds):\n"
            "    return test_execution.StopResult(process.poll(), False)\n"
            "test_execution.stop_process_group = leave_group_running\n"
            "returncode, _termination = test_execution.run_bounded(\n"
            "    [sys.executable, '-c', child, str(group_file)], 0.1\n"
            ")\n"
            "raise SystemExit(returncode)\n",
            encoding="ascii",
        )

        first = self.run_gate(
            f"exec(compile(open({str(nested)!r}, encoding='ascii').read(), "
            f"{str(nested)!r}, 'exec'))",
            timeout="2",
            program_arguments=(group_file,),
            execution_lock=execution_lock,
        )
        self.assertEqual(first.returncode, runner.RUNNER_ERROR_EXIT)
        self.assertTrue(group_file.is_file(), first.stdout + first.stderr)
        process_group = int(group_file.read_text(encoding="ascii"))
        try:
            second = self.run_gate(
                "import pathlib, sys; pathlib.Path(sys.argv[1]).write_text("
                "'launched', encoding='ascii')",
                program_arguments=(marker,),
                execution_lock=execution_lock,
            )
            self.assertEqual(second.returncode, runner.RUNNER_ERROR_EXIT)
            self.assertFalse(marker.exists())
            self.assertEqual(
                self.read_status()["termination"], "execution_quarantined"
            )
        finally:
            try:
                os.killpg(process_group, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def test_quarantine_write_failure_retains_prelaunch_ownership_barrier(self):
        execution_lock = self.directory / "products.lock"
        quarantine = Path(f"{execution_lock}.quarantine.json")
        marker = self.directory / "successor-launched"
        arguments = runner.parse_arguments([
            "--label", "quarantine-write-failure",
            "--log", str(self.log),
            "--status", str(self.status),
            "--execution-lock", str(execution_lock),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "0",
            "--timeout-seconds", "0.1",
            "--terminate-grace-seconds", "0.1",
            "--", sys.executable, "-c", "import time; time.sleep(60)",
        ])

        def leave_group_running(process, _grace_seconds, **_kwargs):
            return runner.StopResult(process.poll(), False)

        original_atomic_status = runner.atomic_status

        def reject_cleanup_record(path, record):
            if path == quarantine and record.get("state") == "cleanup_incomplete":
                raise OSError("injected quarantine write failure")
            original_atomic_status(path, record)

        process_group = None
        try:
            with mock.patch.object(
                runner, "stop_process_group", side_effect=leave_group_running
            ), mock.patch.object(
                runner, "atomic_status", side_effect=reject_cleanup_record
            ):
                self.assertEqual(runner.run(arguments), runner.RUNNER_ERROR_EXIT)
            process_group = self.read_status()["pid"]

            second = self.run_gate(
                "import pathlib, sys; pathlib.Path(sys.argv[1]).write_text("
                "'launched', encoding='ascii')",
                program_arguments=(marker,),
                execution_lock=execution_lock,
            )
            self.assertEqual(second.returncode, runner.RUNNER_ERROR_EXIT)
            self.assertFalse(marker.exists())
            self.assertEqual(
                self.read_status()["termination"], "execution_quarantined"
            )
        finally:
            if process_group is not None:
                try:
                    os.killpg(process_group, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_common_helper_has_an_independent_outer_deadline(self):
        completed = subprocess.CompletedProcess([], 0, "", "")
        with mock.patch.object(subprocess, "run", return_value=completed) as run:
            self.run_gate("print('fixture')")

        self.assertEqual(
            run.call_args.kwargs["timeout"], TEST_HELPER_TIMEOUT_SECONDS
        )

    def test_recursive_bmake_keeps_jobserver_descriptors(self):
        child_makefile = self.directory / "child.mk"
        child_makefile.write_text(
            "all:\n\t@echo child-jobserver-ok\n", encoding="ascii"
        )
        parent_makefile = self.directory / "parent.mk"
        parent_makefile.write_text(
            "all:\n"
            f"\t@{sys.executable} {RUNNER} "
            f"--label jobserver --log {self.log} --status {self.status} "
            "--heartbeat-seconds 0.05 --idle-timeout-seconds 1 "
            "--timeout-seconds 2 -- "
            f"bmake -f {child_makefile} all\n",
            encoding="ascii",
        )

        fixture_environment = os.environ.copy()
        fixture_environment.pop("MAKEFLAGS", None)
        fixture_environment.pop("MFLAGS", None)
        result = subprocess.run(
            ["bmake", "-j2", "-f", str(parent_makefile), "all"],
            text=True,
            capture_output=True,
            check=False,
            env=fixture_environment,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )

        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("child-jobserver-ok", output)
        self.assertNotIn("Invalid internal option", output)

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
