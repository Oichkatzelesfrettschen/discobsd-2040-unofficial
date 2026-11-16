"""Calibrate explicit recovery of an unresolved logged execution owner."""

import json
import os
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import run_logged_gate as gate

ROOT = Path(__file__).resolve().parent.parent
RECOVERY = ROOT / "tools/recover_logged_execution.py"


class RecoverLoggedExecutionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="discobsd-logged-recovery-"
        )
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.execution_lock = self.directory / "products.lock"
        self.ownership_path = gate.execution_ownership_path(self.execution_lock)
        self.status_path = self.directory / "status.json"
        self.invocation_id = "stalled-invocation"
        gate.begin_execution_ownership(
            self.ownership_path,
            {
                "label": "recovery-fixture",
                "invocation_id": self.invocation_id,
                "status_path": str(self.status_path),
            },
        )
        ownership = gate.read_execution_ownership(self.ownership_path)
        ownership["state"] = "cleanup_incomplete"
        ownership["pending_launches"] = ["0123456789abcdef0123456789abcdef"]
        ownership["updated_at"] = gate.utc_timestamp()
        gate.atomic_status(self.ownership_path, ownership)
        gate.atomic_status(
            self.status_path,
            {
                "schema": 1,
                "label": "recovery-fixture",
                "invocation_id": self.invocation_id,
                "state": "completed",
                "exit_code": 0,
                "termination": None,
            },
        )

    def recover(self, *extra_arguments, cwd=None):
        return subprocess.run(
            [
                sys.executable,
                str(RECOVERY),
                "--execution-lock", str(self.execution_lock),
                "--invocation-id", self.invocation_id,
                "--status", str(self.status_path),
                "--reason", "fixture verified the former owner is absent",
                *extra_arguments,
            ],
            text=True,
            capture_output=True,
            check=False,
            timeout=5,
            cwd=cwd,
        )

    def test_recovery_requires_explicit_unrecorded_group_confirmation(self):
        result = self.recover()

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "cleanup_incomplete")
        self.assertEqual(
            ownership["pending_launches"],
            ["0123456789abcdef0123456789abcdef"],
        )

    def test_recovery_retains_evidence_before_releasing_ownership(self):
        result = self.recover("--confirm-unrecorded-groups-absent")

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "released")
        self.assertEqual(ownership["pending_launches"], [])
        self.assertEqual(ownership["process_groups"], [])
        recovery_path = Path(ownership["recovery_receipt"])
        recovery = json.loads(recovery_path.read_text(encoding="utf-8"))
        self.assertEqual(
            recovery["previous_ownership"]["pending_launches"],
            ["0123456789abcdef0123456789abcdef"],
        )
        self.assertEqual(recovery["status"]["state"], "completed")
        descriptor = gate.acquire_execution_lock(self.execution_lock)
        os.close(descriptor)

    def test_recovery_rejects_a_live_recorded_group(self):
        sleeper = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(60)"],
            start_new_session=True,
        )
        self.addCleanup(self.stop_group, sleeper)
        ownership = gate.read_execution_ownership(self.ownership_path)
        ownership["process_groups"] = [sleeper.pid]
        gate.atomic_status(self.ownership_path, ownership)

        result = self.recover("--confirm-unrecorded-groups-absent")

        self.assertEqual(result.returncode, gate.RUNNER_ERROR_EXIT)
        self.assertIn("remains live", result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "cleanup_incomplete")

    def test_recovery_rejects_a_nonterminal_status(self):
        status = json.loads(self.status_path.read_text(encoding="utf-8"))
        status["state"] = "running"
        status["exit_code"] = None
        gate.atomic_status(self.status_path, status)

        result = self.recover("--confirm-unrecorded-groups-absent")

        self.assertEqual(result.returncode, gate.RUNNER_ERROR_EXIT)
        self.assertIn("terminal", result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "cleanup_incomplete")

    def mark_lost_supervisor(self, pid):
        self.invocation_id = f"recovery-fixture-20261110T021709Z-{pid}-0123abcd"
        ownership = gate.read_execution_ownership(self.ownership_path)
        ownership["invocation_id"] = self.invocation_id
        gate.atomic_status(self.ownership_path, ownership)
        status = json.loads(self.status_path.read_text(encoding="utf-8"))
        status.update(
            invocation_id=self.invocation_id, state="running", exit_code=None
        )
        gate.atomic_status(self.status_path, status)

    def exited_pid(self):
        child = subprocess.Popen([sys.executable, "-c", "pass"])
        child.wait(timeout=5)
        return child.pid

    def test_lost_supervisor_recovery_releases_an_exited_supervisor(self):
        pid = self.exited_pid()
        self.mark_lost_supervisor(pid)

        result = self.recover(
            "--confirm-unrecorded-groups-absent", "--supervisor-lost"
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "released")
        recovery = json.loads(
            Path(ownership["recovery_receipt"]).read_text(encoding="utf-8")
        )
        self.assertEqual(recovery["lost_supervisor_pid"], pid)
        self.assertEqual(recovery["status"]["state"], "running")
        self.assertEqual(
            recovery["confirmation"],
            "supervisor_and_unrecorded_process_groups_absent",
        )

    def test_lost_supervisor_recovery_rejects_a_live_supervisor(self):
        self.mark_lost_supervisor(os.getpid())

        result = self.recover(
            "--confirm-unrecorded-groups-absent", "--supervisor-lost"
        )

        self.assertEqual(result.returncode, gate.RUNNER_ERROR_EXIT)
        self.assertIn("supervisor remains live", result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "cleanup_incomplete")

    def test_lost_supervisor_recovery_rejects_a_terminal_status(self):
        self.mark_lost_supervisor(self.exited_pid())
        status = json.loads(self.status_path.read_text(encoding="utf-8"))
        status.update(state="completed", exit_code=0)
        gate.atomic_status(self.status_path, status)

        result = self.recover(
            "--confirm-unrecorded-groups-absent", "--supervisor-lost"
        )

        self.assertEqual(result.returncode, gate.RUNNER_ERROR_EXIT)
        self.assertIn("nonterminal", result.stderr)

    def test_lost_supervisor_recovery_rejects_an_unparsable_invocation(self):
        status = json.loads(self.status_path.read_text(encoding="utf-8"))
        status.update(state="running", exit_code=None)
        gate.atomic_status(self.status_path, status)

        result = self.recover(
            "--confirm-unrecorded-groups-absent", "--supervisor-lost"
        )

        self.assertEqual(result.returncode, gate.RUNNER_ERROR_EXIT)
        self.assertIn("names no supervisor", result.stderr)

    def test_recovery_resolves_recorded_status_from_execution_directory(self):
        ownership = gate.read_execution_ownership(self.ownership_path)
        ownership["status_path"] = self.status_path.name
        gate.atomic_status(self.ownership_path, ownership)
        status = json.loads(self.status_path.read_text(encoding="utf-8"))
        status["cwd"] = str(self.directory)
        gate.atomic_status(self.status_path, status)
        operator_directory = self.directory / "operator"
        operator_directory.mkdir()

        result = self.recover(
            "--confirm-unrecorded-groups-absent",
            cwd=operator_directory,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ownership = gate.read_execution_ownership(self.ownership_path)
        self.assertEqual(ownership["state"], "released")

    @staticmethod
    def stop_group(process):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
