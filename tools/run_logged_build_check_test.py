"""Calibrate one bounded build-then-check invocation."""

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

ROOT = Path(__file__).resolve().parent.parent
SEQUENCE = ROOT / "tools/run_logged_build_check.py"
TEST_HELPER_TIMEOUT_SECONDS = 5

sys.path.insert(0, str(ROOT / "tools"))
import run_logged_build_check as sequence  # noqa: E402


class LoggedBuildCheckTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="discobsd-logged-build-check-"
        )
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.evidence_root = self.directory / "evidence"
        self.execution_lock = self.directory / "products.lock"

    def run_sequence(self, make, timeout_seconds="2"):
        return subprocess.run(
            self.sequence_command(make, timeout_seconds),
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )

    def sequence_command(self, make, timeout_seconds="2"):
        return [
            sys.executable,
            str(SEQUENCE),
            "--label", "rp2040-check-local",
            "--evidence-root", str(self.evidence_root),
            "--execution-lock", str(self.execution_lock),
            "--heartbeat-seconds", "0.05",
            "--idle-timeout-seconds", "1",
            "--timeout-seconds", timeout_seconds,
            "--",
            str(make),
            "MACHINE=rp2040",
        ]

    def wait_for_running_check(self):
        deadline = time.monotonic() + 2
        latest_path = self.evidence_root / "rp2040-check-local.latest.json"
        while time.monotonic() < deadline:
            if latest_path.exists():
                summary = json.loads(latest_path.read_text(encoding="utf-8"))
                check_path = Path(summary["evidence_directory"]) / "check.status.json"
                if check_path.exists():
                    check = json.loads(check_path.read_text(encoding="utf-8"))
                    if check["state"] == "running":
                        return summary
            time.sleep(0.01)
        self.fail("check stage did not reach running state")

    def wait_for_running_build(self):
        deadline = time.monotonic() + 2
        latest_path = self.evidence_root / "rp2040-check-local.latest.json"
        while time.monotonic() < deadline:
            if latest_path.exists():
                summary = json.loads(latest_path.read_text(encoding="utf-8"))
                build = summary["stages"]["build"]
                if build["state"] == "running":
                    return summary
            time.sleep(0.01)
        self.fail("build stage did not reach running state")

    def write_stage_record(self, args, state="completed"):
        record = {
            "state": state,
            "exit_code": 0,
            "termination": None,
        }
        path = args.status
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(record), encoding="utf-8")

    def run_interstage_signal_probe(self, signal_number, signal_stage):
        harness = self.directory / f"interstage-{signal_stage}-{signal_number}.py"
        harness.write_text(
            "import json, os, signal, sys\n"
            "from pathlib import Path\n"
            f"sys.path.insert(0, {str(ROOT / 'tools')!r})\n"
            "import run_logged_build_check as sequence\n"
            f"signal_number = {signal_number}\n"
            f"signal_stage = {signal_stage!r}\n"
            "def stage_run(args):\n"
            "    record = {'state': 'completed', 'exit_code': 0, "
            "'termination': None}\n"
            "    args.status.parent.mkdir(parents=True, exist_ok=True)\n"
            "    args.status.write_text(json.dumps(record), encoding='utf-8')\n"
            "    stage = args.label.rsplit('-', 1)[1]\n"
            "    if stage == signal_stage:\n"
            "        os.kill(os.getpid(), signal_number)\n"
            "    return 0\n"
            "sequence.gate.run = stage_run\n"
            "raise SystemExit(sequence.run(sequence.parse_arguments(sys.argv[1:])))\n",
            encoding="ascii",
        )
        return subprocess.run(
            [sys.executable, str(harness), *self.sequence_command("fixture-make")[2:]],
            text=True,
            capture_output=True,
            check=False,
            timeout=TEST_HELPER_TIMEOUT_SECONDS,
        )

    def test_failed_build_publishes_blocked_check_receipt(self):
        make = self.directory / "failing-make"
        make.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            "last=\n"
            "for argument do last=$argument; done\n"
            "if [ \"$last\" = build ]; then exit 7; fi\n"
            "exit 0\n",
            encoding="ascii",
        )
        make.chmod(0o755)

        result = self.run_sequence(make)

        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        summary = json.loads(
            (self.evidence_root / "rp2040-check-local.latest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(summary["state"], "failed")
        self.assertEqual(summary["exit_code"], 7)
        invocation = Path(summary["evidence_directory"])
        self.assertEqual(summary["invocation_id"], invocation.name)
        build = json.loads((invocation / "build.status.json").read_text())
        check = json.loads((invocation / "check.status.json").read_text())
        self.assertEqual(build["sequence_invocation_id"], invocation.name)
        self.assertEqual(build["state"], "failed")
        self.assertEqual(build["exit_code"], 7)
        self.assertEqual(check["sequence_invocation_id"], invocation.name)
        self.assertEqual(check["state"], "blocked")
        self.assertEqual(check["termination"], "prerequisite_failed")
        self.assertEqual(check["blocked_by"], "build")

    def test_build_runner_error_remains_error_in_sequence_summary(self):
        missing_make = self.directory / "missing-make"

        result = self.run_sequence(missing_make)

        self.assertEqual(
            result.returncode,
            sequence.gate.RUNNER_ERROR_EXIT,
            result.stdout + result.stderr,
        )
        summary = json.loads(
            (self.evidence_root / "rp2040-check-local.latest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(summary["state"], "error")
        self.assertEqual(summary["stages"]["build"]["state"], "error")
        self.assertEqual(summary["stages"]["check"]["state"], "blocked")

    def test_total_deadline_includes_build_and_check(self):
        make = self.directory / "slow-make"
        make.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            "last=\n"
            "for argument do last=$argument; done\n"
            "if [ \"$last\" = build ]; then sleep 0.35; exit 0; fi\n"
            "printf '%s\\n' CHECK-START\n"
            "sleep 60\n",
            encoding="ascii",
        )
        make.chmod(0o755)

        result = self.run_sequence(make, timeout_seconds="0.55")

        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        summary = json.loads(
            (self.evidence_root / "rp2040-check-local.latest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(summary["state"], "timed_out")
        self.assertEqual(summary["termination"], "hard_timeout")
        self.assertEqual(summary["stages"]["build"]["state"], "completed")
        self.assertEqual(summary["stages"]["check"]["state"], "timed_out")
        invocation = Path(summary["evidence_directory"])
        check = json.loads((invocation / "check.status.json").read_text())
        self.assertGreater(check["timeout_seconds"], 0)
        self.assertLess(check["timeout_seconds"], 0.3)

    def test_exhausted_deadline_does_not_launch_check(self):
        args = sequence.parse_arguments(
            [
                "--label", "rp2040-check-local",
                "--evidence-root", str(self.evidence_root),
                "--execution-lock", str(self.execution_lock),
                "--timeout-seconds", "1",
                "--", "fixture-make", "MACHINE=rp2040",
            ]
        )
        launched = []

        def stage_run(stage_args):
            launched.append(stage_args.label.rsplit("-", 1)[1])
            self.write_stage_record(stage_args)
            return 0

        monotonic_values = iter([0, 0, 1.1])
        with mock.patch.object(sequence.gate, "run", side_effect=stage_run), \
             mock.patch.object(
                 sequence.time,
                 "monotonic",
                 side_effect=lambda: next(monotonic_values, 1.1),
             ):
            result = sequence.run(args)

        self.assertEqual(result, sequence.gate.TIMEOUT_EXIT)
        self.assertEqual(launched, ["build"])
        summary = json.loads(
            (self.evidence_root / "rp2040-check-local.latest.json").read_text()
        )
        self.assertEqual(summary["state"], "timed_out")
        self.assertEqual(summary["termination"], "deadline_exhausted")
        self.assertEqual(summary["stages"]["check"]["state"], "blocked")

    def test_latest_summary_reports_completed_build_during_check(self):
        make = self.directory / "blocking-check-make"
        make.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            "last=\n"
            "for argument do last=$argument; done\n"
            "if [ \"$last\" = build ]; then exit 0; fi\n"
            "printf '%s\\n' CHECK-START\n"
            "sleep 60\n",
            encoding="ascii",
        )
        make.chmod(0o755)
        process = subprocess.Popen(
            self.sequence_command(make),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            summary = self.wait_for_running_check()
            self.assertEqual(summary["state"], "running")
            self.assertEqual(summary["stages"]["build"]["state"], "completed")
            self.assertEqual(summary["stages"]["check"]["state"], "running")
        finally:
            process.terminate()
            process.communicate(timeout=TEST_HELPER_TIMEOUT_SECONDS)

    def test_latest_summary_reports_running_build(self):
        make = self.directory / "blocking-build-make"
        make.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            "last=\n"
            "for argument do last=$argument; done\n"
            "if [ \"$last\" = build ]; then sleep 60; fi\n"
            "exit 0\n",
            encoding="ascii",
        )
        make.chmod(0o755)
        process = subprocess.Popen(
            self.sequence_command(make),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            summary = self.wait_for_running_build()
            self.assertEqual(summary["state"], "running")
            self.assertEqual(summary["stages"]["build"]["state"], "running")
            self.assertEqual(summary["stages"]["check"]["state"], "pending")
            self.assertTrue(summary["stages"]["build"]["status_path"].endswith(
                "/build.status.json"
            ))
        finally:
            process.terminate()
            process.communicate(timeout=TEST_HELPER_TIMEOUT_SECONDS)

    def test_interstage_signals_publish_terminal_receipts_and_release_lock(self):
        for signal_number in (signal.SIGINT, signal.SIGTERM):
            for signal_stage in ("build", "check"):
                with self.subTest(signal=signal_number, stage=signal_stage):
                    result = self.run_interstage_signal_probe(
                        signal_number, signal_stage
                    )
                    self.assertEqual(
                        result.returncode,
                        128 + signal_number,
                        result.stdout + result.stderr,
                    )
                    summary = json.loads(
                        (self.evidence_root / "rp2040-check-local.latest.json")
                        .read_text(encoding="utf-8")
                    )
                    self.assertEqual(summary["state"], "interrupted")
                    self.assertEqual(
                        summary["termination"], f"signal_{signal_number}"
                    )
                    self.assertEqual(
                        summary["stages"][signal_stage]["state"], "completed"
                    )
                    if signal_stage == "build":
                        self.assertEqual(
                            summary["stages"]["check"]["state"], "blocked"
                        )
                    lock_descriptor = sequence.gate.acquire_execution_lock(
                        self.execution_lock
                    )
                    os.close(lock_descriptor)

    def test_stage_exception_publishes_error_and_releases_product_lock(self):
        args = sequence.parse_arguments(
            [
                "--label", "rp2040-check-local",
                "--evidence-root", str(self.evidence_root),
                "--execution-lock", str(self.execution_lock),
                "--",
                "fixture-make",
                "MACHINE=rp2040",
            ]
        )

        with mock.patch.object(
            sequence.gate, "run", side_effect=RuntimeError("fixture stage failure")
        ):
            result = sequence.run(args)

        self.assertEqual(result, sequence.gate.RUNNER_ERROR_EXIT)
        summary = json.loads(
            (self.evidence_root / "rp2040-check-local.latest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(summary["state"], "error")
        self.assertEqual(summary["termination"], "stage_runner_error")
        self.assertEqual(summary["stages"]["build"]["state"], "error")
        self.assertEqual(summary["stages"]["check"]["state"], "blocked")
        lock_descriptor = sequence.gate.acquire_execution_lock(self.execution_lock)
        os.close(lock_descriptor)

    def test_unusable_evidence_root_fails_without_launch_or_traceback(self):
        self.evidence_root.write_text("regular file\n", encoding="ascii")
        make = self.directory / "marker-make"
        child_marker = Path(f"{make}.launched")
        make.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            ': > "$0.launched"\n',
            encoding="ascii",
        )
        make.chmod(0o755)

        result = self.run_sequence(make)

        self.assertEqual(result.returncode, sequence.gate.RUNNER_ERROR_EXIT)
        self.assertIn("logged-build-check:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(child_marker.exists())


if __name__ == "__main__":
    unittest.main()
