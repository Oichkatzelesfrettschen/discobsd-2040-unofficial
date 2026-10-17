"""Calibrate execution verdicts with real processes and absent invocations."""

import contextlib
import io
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

import test_execution as execution


class ExecutionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="discobsd-execution-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.receipts = self.directory / "receipts"
        self.receipts.mkdir()
        self.environment = patch.dict(os.environ, {
            "TEST_EXECUTION_DIR": str(self.receipts),
            "TEST_EXECUTION_REQUIRED": "no", "REQUIRE_ILP32": "no",
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)
        os.environ.pop(
            execution.execution_ownership.OWNERSHIP_PATH_ENV, None
        )
        os.environ.pop(
            execution.execution_ownership.OWNERSHIP_INVOCATION_ENV, None
        )
        self.output = io.StringIO()
        self.stdout = contextlib.redirect_stdout(self.output)
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)
        self.stderr = contextlib.redirect_stderr(self.output)
        self.stderr.__enter__()
        self.addCleanup(self.stderr.__exit__, None, None, None)

    def variant(self, name="fixture", width="native", expected_exit=0):
        return {"id": name, "owner": "fixture/job", "width": width,
                "capability": "fixture runtime", "program": name,
                "directory": str(self.directory), "expected_exit": expected_exit}

    def compile(self, variant, returncode, flags=()):
        source = self.directory / (variant["program"] + ".c")
        source.write_text(f"int main(void) {{ return {returncode}; }}\n")
        executable = self.directory / variant["program"]
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        subprocess.run([*command, "-std=c17", "-Wall", "-Wextra", "-Werror", *flags,
                        str(source), "-o", str(executable)], check=True)
        return [str(executable)]

    def record(self, variant):
        return json.loads((self.receipts / (variant["id"] + ".json")).read_text())

    def stop_recorded_group(self, group_file):
        if not group_file.is_file():
            return
        try:
            os.killpg(int(group_file.read_text(encoding="ascii")), signal.SIGKILL)
        except ProcessLookupError:
            pass

    def wait_for_file(self, path, timeout_seconds=2):
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            if path.is_file():
                return True
            time.sleep(0.01)
        return False

    def test_executed_pass_and_failure(self):
        for name, returncode, outcome in [("good", 0, "PASS"), ("bad", 1, "FAIL")]:
            variant = self.variant(name)
            command = self.compile(variant, returncode)
            self.assertEqual(execution.run_variant(variant, command), returncode)
            record = self.record(variant)
            self.assertEqual(record["outcome"], outcome)
            self.assertEqual(record["invocation"], command)
            self.assertEqual(record["returncode"], returncode)
            self.assertEqual(len(record["executable"]["sha256"]), 64)

    def test_negative_control_requires_exact_rejection(self):
        for name, returncode, outcome in [("rejected", 1, "PASS"), ("accepted", 0, "FAIL"),
                                         ("infrastructure", 127, "ERROR")]:
            variant = self.variant(name, expected_exit=1)
            execution.run_variant(variant, self.compile(variant, returncode))
            self.assertEqual(self.record(variant)["outcome"], outcome)

    def test_optional_skip_and_required_error(self):
        optional = self.variant("optional", "ilp32")
        self.assertEqual(execution.skip_variant(optional, "capability absent"), 0)
        self.assertEqual(self.record(optional)["outcome"], "SKIP")
        for setting in ("REQUIRE_ILP32", "TEST_EXECUTION_REQUIRED"):
            required = self.variant(setting.lower(), "ilp32")
            with patch.dict(os.environ, {setting: "yes"}):
                self.assertEqual(execution.skip_variant(required, "capability absent"), 2)
            record = self.record(required)
            self.assertEqual(record["outcome"], "ERROR")
            self.assertIsNone(record["invocation"])

    def test_missing_executable_is_error(self):
        variant = self.variant()
        self.assertEqual(execution.run_variant(variant, [str(self.directory / "fixture")]), 2)
        self.assertEqual(self.record(variant)["outcome"], "ERROR")

    def test_cleanup_allowance_reserves_current_budget_before_child_share(self):
        with patch.dict(os.environ, {}, clear=False):
            os.environ.pop(execution.CLEANUP_ALLOWANCE_ENV, None)
            self.assertEqual(
                execution.cleanup_allowances(),
                (execution.ROOT_CLEANUP_ALLOWANCE_SECONDS, 1.0),
            )

        with patch.dict(
            os.environ, {execution.CLEANUP_ALLOWANCE_ENV: "0.5"}, clear=False
        ):
            self.assertEqual(execution.cleanup_allowances(), (0.5, 0.25))

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
        with patch.object(execution.os, "killpg"):
            with patch.object(
                execution, "process_group_exists", return_value=True
            ):
                result = execution.stop_process_group(process, 0.01)

        self.assertLess(time.monotonic() - started, 0.5)
        self.assertIsNone(result.child_exit_code)
        self.assertFalse(result.group_stopped)
        self.assertGreaterEqual(len(process.wait_timeouts), 1)
        self.assertNotIn(None, process.wait_timeouts)
        self.assertEqual(process.kill_calls, 1)

    def test_stop_process_group_treats_signal_refusal_as_incomplete(self):
        class RefusedProcess:
            pid = 4243

            def poll(self):
                return -signal.SIGTERM

            def wait(self, timeout=None):
                return -signal.SIGTERM

        def deny_signals(_process_group, number):
            if number == 0:
                return
            raise PermissionError("injected signal refusal")

        with patch.object(execution.os, "killpg", side_effect=deny_signals):
            result = execution.stop_process_group(RefusedProcess(), 0)

        self.assertEqual(result.child_exit_code, -signal.SIGTERM)
        self.assertFalse(result.group_stopped)

    def test_stop_process_group_bounds_refused_leader_signal(self):
        class RefusedProcess:
            pid = 4244

            def poll(self):
                return None

            def kill(self):
                raise PermissionError("injected leader refusal")

            def wait(self, timeout=None):
                raise subprocess.TimeoutExpired("fixture", timeout)

        def deny_signals(_process_group, number):
            if number == 0:
                return
            raise PermissionError("injected group refusal")

        with patch.object(execution.os, "killpg", side_effect=deny_signals):
            result = execution.stop_process_group(RefusedProcess(), 0)

        self.assertIsNone(result.child_exit_code)
        self.assertFalse(result.group_stopped)

    def test_variant_timeout_records_error_and_stops_process_group(self):
        variant = self.variant()
        marker = self.directory / "variant-descendant-terminated"
        ready = self.directory / "variant-descendant-ready"
        executable = self.directory / variant["program"]
        executable.write_text(
            f"#!{sys.executable}\n"
            "import os, pathlib, signal, sys, time\n"
            "child = (\"import pathlib, signal, sys, time; \""
            "         \"marker, ready=map(pathlib.Path, sys.argv[1:]); \""
            "         \"signal.signal(signal.SIGTERM, lambda *_: \""
            "         \"(marker.write_text('terminated', encoding='ascii'), \""
            "         \"sys.exit(0))); ready.write_text('ready', encoding='ascii'); \""
            "         \"time.sleep(60)\")\n"
            "child_pid = os.fork()\n"
            "if child_pid == 0:\n"
            "    os.execv(sys.executable, [sys.executable, '-c', child, *sys.argv[1:]])\n"
            "time.sleep(60)\n"
        )
        executable.chmod(0o755)

        self.assertEqual(
            execution.run_variant(
                variant,
                [str(executable), str(marker), str(ready)],
                timeout_seconds=2.0,
            ),
            2,
        )

        record = self.record(variant)
        self.assertEqual(record["outcome"], "ERROR")
        self.assertEqual(record["returncode"], execution.TIMEOUT_EXIT)
        self.assertEqual(record["termination"], "hard_timeout")
        self.assertEqual(record["timeout_seconds"], 2.0)
        self.assertTrue(ready.is_file())
        self.assertTrue(marker.is_file())

    def test_successful_variant_stops_its_surviving_descendant(self):
        variant = self.variant()
        marker = self.directory / "successful-descendant-terminated"
        ready = self.directory / "successful-descendant-ready"
        group_file = self.directory / "successful-descendant-group"
        self.addCleanup(self.stop_recorded_group, group_file)
        executable = self.directory / variant["program"]
        executable.write_text(
            f"#!{sys.executable}\n"
            "import os, pathlib, signal, sys, time\n"
            "marker, ready, group_file = map(pathlib.Path, sys.argv[1:])\n"
            "child = (\"import pathlib, signal, sys, time; \"\n"
            "         \"marker, ready = map(pathlib.Path, sys.argv[1:]); \"\n"
            "         \"signal.signal(signal.SIGTERM, lambda *_: \"\n"
            "         \"(marker.write_text('terminated', encoding='ascii'), \"\n"
            "         \"sys.exit(0))); ready.write_text('ready', encoding='ascii'); \"\n"
            "         \"time.sleep(60)\")\n"
            "child_pid = os.fork()\n"
            "if child_pid == 0:\n"
            "    os.execv(\n"
            "        sys.executable,\n"
            "        [sys.executable, '-c', child, str(marker), str(ready)],\n"
            "    )\n"
            "group_file.write_text(str(os.getpgrp()), encoding='ascii')\n"
            "deadline = time.monotonic() + 2\n"
            "while not ready.exists() and time.monotonic() < deadline:\n"
            "    time.sleep(0.01)\n"
            "raise SystemExit(0 if ready.exists() else 3)\n",
            encoding="ascii",
        )
        executable.chmod(0o755)

        self.assertEqual(
            execution.run_variant(
                variant, [str(executable), str(marker), str(ready), str(group_file)]
            ),
            0,
        )

        self.assertTrue(self.wait_for_file(marker, 1))

    def test_completed_group_leaves_sequence_ownership(self):
        variant = self.variant("owned-success")
        command = self.compile(variant, 0)
        ownership_path = self.directory / "products.lock.ownership.json"
        invocation_id = "sequence-fixture"
        execution.execution_ownership.begin_execution_ownership(
            ownership_path,
            {
                "label": "sequence-fixture",
                "invocation_id": invocation_id,
                "status_path": str(self.directory / "status.json"),
            },
        )

        with patch.dict(
            os.environ,
            {
                execution.execution_ownership.OWNERSHIP_PATH_ENV:
                    str(ownership_path),
                execution.execution_ownership.OWNERSHIP_INVOCATION_ENV:
                    invocation_id,
            },
        ):
            self.assertEqual(execution.run_variant(variant, command), 0)

        ownership = execution.execution_ownership.read_execution_ownership(
            ownership_path
        )
        self.assertEqual(ownership["state"], "active")
        self.assertEqual(ownership["process_groups"], [])

    def test_registration_failure_names_the_ownership_error(self):
        missing_ownership = self.directory / "missing-ownership.json"
        with patch.dict(
            os.environ,
            {
                execution.execution_ownership.OWNERSHIP_PATH_ENV:
                    str(missing_ownership),
                execution.execution_ownership.OWNERSHIP_INVOCATION_ENV:
                    "sequence-fixture",
            },
        ):
            returncode, termination = execution.run_bounded(
                [sys.executable, "-c", "import time; time.sleep(60)"], 2.0
            )

        self.assertEqual(returncode, 125)
        self.assertEqual(termination, "cleanup_incomplete")
        self.assertIn(
            f"execution ownership invocation mismatch: {missing_ownership}",
            self.output.getvalue(),
        )

    def test_incomplete_registration_cleanup_retains_sequence_ownership(self):
        execution_lock = self.directory / "products.lock"
        ownership_path = execution.execution_ownership.execution_ownership_path(
            execution_lock
        )
        invocation_id = "sequence-fixture"
        execution.execution_ownership.begin_execution_ownership(
            ownership_path,
            {
                "label": "sequence-fixture",
                "invocation_id": invocation_id,
                "status_path": str(self.directory / "status.json"),
            },
        )
        launched = []
        original_launch = execution.subprocess.Popen

        def capture_launch(*arguments, **keywords):
            process = original_launch(*arguments, **keywords)
            launched.append(process)
            return process

        def leave_group_running(process, _grace_seconds):
            return execution.StopResult(process.poll(), False)

        try:
            with patch.dict(
                os.environ,
                {
                    execution.execution_ownership.OWNERSHIP_PATH_ENV:
                        str(ownership_path),
                    execution.execution_ownership.OWNERSHIP_INVOCATION_ENV:
                        invocation_id,
                },
            ), patch.object(
                execution.subprocess, "Popen", side_effect=capture_launch
            ), patch.object(
                execution.execution_ownership,
                "complete_inherited_execution_launch",
                side_effect=OSError("injected registration failure"),
            ) as registration, patch.object(
                execution, "stop_process_group", side_effect=leave_group_running
            ):
                returncode, termination = execution.run_bounded(
                    [sys.executable, "-c", "import time; time.sleep(60)"], 2.0
                )

            self.assertEqual(registration.call_count, 1)
            self.assertEqual(returncode, 125)
            self.assertEqual(termination, "cleanup_incomplete")
            process_group = launched[0].pid
            ownership = execution.execution_ownership.read_execution_ownership(
                ownership_path
            )
            self.assertEqual(ownership["state"], "cleanup_incomplete")
            self.assertEqual(ownership["process_groups"], [process_group])
            execution.execution_ownership.finish_execution_ownership(
                ownership_path, invocation_id
            )
            with self.assertRaises(
                execution.execution_ownership.ExecutionQuarantined
            ):
                execution.execution_ownership.acquire_execution_lock(
                    execution_lock
                )
        finally:
            if launched:
                try:
                    os.killpg(launched[0].pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                launched[0].wait(timeout=5)

    def test_busy_ownership_update_lock_prevents_detached_launch(self):
        execution_lock = self.directory / "products.lock"
        ownership_path = execution.execution_ownership.execution_ownership_path(
            execution_lock
        )
        invocation_id = "sequence-fixture"
        execution.execution_ownership.begin_execution_ownership(
            ownership_path,
            {
                "label": "sequence-fixture",
                "invocation_id": invocation_id,
                "status_path": str(self.directory / "status.json"),
            },
        )
        update_lock = execution.execution_ownership._acquire_ownership_update_lock(
            ownership_path
        )
        launched = []
        original_launch = execution.subprocess.Popen

        def capture_launch(*arguments, **keywords):
            process = original_launch(*arguments, **keywords)
            launched.append(process)
            return process

        try:
            with patch.dict(
                os.environ,
                {
                    execution.execution_ownership.OWNERSHIP_PATH_ENV:
                        str(ownership_path),
                    execution.execution_ownership.OWNERSHIP_INVOCATION_ENV:
                        invocation_id,
                },
            ), patch.object(
                execution.subprocess, "Popen", side_effect=capture_launch
            ), patch.object(
                execution.execution_ownership,
                "OWNERSHIP_LOCK_SECONDS",
                0.02,
            ):
                returncode, termination = execution.run_bounded(
                    [sys.executable, "-c", "import time; time.sleep(60)"], 2.0
                )

            self.assertEqual(returncode, 125)
            self.assertEqual(termination, "cleanup_incomplete")
            self.assertEqual(launched, [])
        finally:
            os.close(update_lock)
            for process in launched:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_failed_registration_write_retains_pending_launch_barrier(self):
        execution_lock = self.directory / "products.lock"
        ownership_path = execution.execution_ownership.execution_ownership_path(
            execution_lock
        )
        invocation_id = "sequence-fixture"
        execution.execution_ownership.begin_execution_ownership(
            ownership_path,
            {
                "label": "sequence-fixture",
                "invocation_id": invocation_id,
                "status_path": str(self.directory / "status.json"),
            },
        )
        launched = []
        original_launch = execution.subprocess.Popen
        original_atomic_status = execution.execution_ownership.atomic_status

        def capture_launch(*arguments, **keywords):
            process = original_launch(*arguments, **keywords)
            launched.append(process)
            return process

        def reject_group_registration(path, record):
            if record.get("process_groups"):
                raise OSError("injected registration write failure")
            original_atomic_status(path, record)

        def leave_group_running(process, _grace_seconds):
            return execution.StopResult(process.poll(), False)

        try:
            with patch.dict(
                os.environ,
                {
                    execution.execution_ownership.OWNERSHIP_PATH_ENV:
                        str(ownership_path),
                    execution.execution_ownership.OWNERSHIP_INVOCATION_ENV:
                        invocation_id,
                },
            ), patch.object(
                execution.subprocess, "Popen", side_effect=capture_launch
            ), patch.object(
                execution.execution_ownership,
                "atomic_status",
                side_effect=reject_group_registration,
            ), patch.object(
                execution, "stop_process_group", side_effect=leave_group_running
            ):
                returncode, termination = execution.run_bounded(
                    [sys.executable, "-c", "import time; time.sleep(60)"], 2.0
                )

            self.assertEqual(returncode, 125)
            self.assertEqual(termination, "cleanup_incomplete")
            ownership = execution.execution_ownership.read_execution_ownership(
                ownership_path
            )
            self.assertEqual(ownership["process_groups"], [])
            self.assertEqual(len(ownership["pending_launches"]), 1)
            execution.execution_ownership.finish_execution_ownership(
                ownership_path, invocation_id
            )
            ownership = execution.execution_ownership.read_execution_ownership(
                ownership_path
            )
            self.assertEqual(ownership["state"], "cleanup_incomplete")
            with self.assertRaises(
                execution.execution_ownership.ExecutionQuarantined
            ):
                execution.execution_ownership.acquire_execution_lock(execution_lock)
        finally:
            for process in launched:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_signal_during_registration_stops_launched_group(self):
        ownership_path = self.directory / "products.lock.ownership.json"
        invocation_id = "sequence-fixture"
        execution.execution_ownership.begin_execution_ownership(
            ownership_path,
            {
                "label": "sequence-fixture",
                "invocation_id": invocation_id,
                "status_path": str(self.directory / "status.json"),
            },
        )
        launched = []
        original_launch = execution.subprocess.Popen
        original_complete = (
            execution.execution_ownership.complete_inherited_execution_launch
        )

        def capture_launch(*arguments, **keywords):
            process = original_launch(*arguments, **keywords)
            launched.append(process)
            return process

        def interrupt_registration(reservation, process_group):
            os.kill(os.getpid(), signal.SIGTERM)
            original_complete(reservation, process_group)

        try:
            with patch.dict(
                os.environ,
                {
                    execution.execution_ownership.OWNERSHIP_PATH_ENV:
                        str(ownership_path),
                    execution.execution_ownership.OWNERSHIP_INVOCATION_ENV:
                        invocation_id,
                },
            ), patch.object(
                execution.subprocess, "Popen", side_effect=capture_launch
            ), patch.object(
                execution.execution_ownership,
                "complete_inherited_execution_launch",
                side_effect=interrupt_registration,
            ):
                returncode, termination = execution.run_bounded(
                    [sys.executable, "-c", "import time; time.sleep(60)"], 2.0
                )

            self.assertEqual(returncode, 128 + signal.SIGTERM)
            self.assertEqual(termination, f"signal_{signal.SIGTERM}")
            self.assertFalse(execution.process_group_exists(launched[0].pid))
            ownership = execution.execution_ownership.read_execution_ownership(
                ownership_path
            )
            self.assertEqual(ownership["process_groups"], [])
            self.assertEqual(ownership["pending_launches"], [])
        finally:
            if launched:
                try:
                    os.killpg(launched[0].pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_signal_before_launch_returns_stops_launched_group(self):
        launched = []
        original_launch = execution.subprocess.Popen

        def interrupt_before_return(*arguments, **keywords):
            process = original_launch(*arguments, **keywords)
            launched.append(process)
            os.kill(os.getpid(), signal.SIGTERM)
            return process

        interruption = None
        result = None
        try:
            with patch.object(
                execution.subprocess, "Popen", side_effect=interrupt_before_return
            ):
                try:
                    result = execution.run_bounded(
                        [sys.executable, "-c", "import time; time.sleep(60)"], 2.0
                    )
                except BaseException as error:
                    interruption = error

            self.assertIsNone(interruption)
            self.assertEqual(
                result,
                (128 + signal.SIGTERM, f"signal_{signal.SIGTERM}"),
            )
            self.assertFalse(execution.process_group_exists(launched[0].pid))
        finally:
            for process in launched:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_outer_timeout_stops_nested_detached_process_group(self):
        marker = self.directory / "nested-descendant-terminated"
        ready = self.directory / "nested-descendant-ready"
        group_file = self.directory / "nested-descendant-group"
        self.addCleanup(self.stop_recorded_group, group_file)
        middle = self.directory / "nested-supervisor.py"
        middle.write_text(
            "import pathlib, sys\n"
            f"sys.path.insert(0, {str(execution.ROOT / 'tools')!r})\n"
            "import test_execution\n"
            "marker, ready, group_file = map(pathlib.Path, sys.argv[1:])\n"
            "child = (\"import os, pathlib, signal, sys, time; \"\n"
            "         \"marker, ready, group_file = map(pathlib.Path, sys.argv[1:]); \"\n"
            "         \"group_file.write_text(str(os.getpgrp()), encoding='ascii'); \"\n"
            "         \"signal.signal(signal.SIGTERM, lambda *_: \"\n"
            "         \"(marker.write_text('terminated', encoding='ascii'), \"\n"
            "         \"sys.exit(0))); ready.write_text('ready', encoding='ascii'); \"\n"
            "         \"time.sleep(60)\")\n"
            "test_execution.run_bounded(\n"
            "    [sys.executable, '-c', child, marker, ready, group_file], 60\n"
            ")\n",
            encoding="ascii",
        )

        returncode, termination = execution.run_bounded(
            [sys.executable, str(middle), str(marker), str(ready), str(group_file)],
            0.3,
        )

        self.assertEqual(returncode, execution.TIMEOUT_EXIT)
        self.assertEqual(termination, "hard_timeout")
        self.assertTrue(self.wait_for_file(marker, 1))

    def test_three_nested_supervisors_stop_resistant_leaf_before_return(self):
        for probe in range(3):
            ready = self.directory / f"nested-resistant-ready-{probe}"
            group_file = self.directory / f"nested-resistant-group-{probe}"
            self.addCleanup(self.stop_recorded_group, group_file)
            leaf = self.directory / f"nested-resistant-leaf-{probe}.py"
            leaf.write_text(
                "import os, pathlib, signal, sys, time\n"
                "ready, group_file = map(pathlib.Path, sys.argv[1:])\n"
                "group_file.write_text(str(os.getpgrp()), encoding='ascii')\n"
                "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                "ready.write_text('ready', encoding='ascii')\n"
                "time.sleep(60)\n",
                encoding="ascii",
            )
            child = [sys.executable, str(leaf), str(ready), str(group_file)]
            for level in range(3):
                supervisor = self.directory / f"nested-supervisor-{probe}-{level}.py"
                delayed_cleanup = (
                    "original_stop = test_execution.stop_process_group\n"
                    "def delayed_stop(*args, **kwargs):\n"
                    "    time.sleep(0.05)\n"
                    "    return original_stop(*args, **kwargs)\n"
                    "test_execution.stop_process_group = delayed_stop\n"
                    if level == 0 else ""
                )
                supervisor_source = (
                    "import sys, time\n"
                    f"sys.path.insert(0, {str(execution.ROOT / 'tools')!r})\n"
                    "import test_execution\n"
                    + delayed_cleanup
                    + f"command = {child!r}\n"
                    "test_execution.run_bounded(command, 60)\n"
                )
                supervisor.write_text(
                    supervisor_source,
                    encoding="ascii",
                )
                child = [sys.executable, str(supervisor)]

            returncode, termination = execution.run_bounded(child, 2.0)

            self.assertEqual(returncode, execution.TIMEOUT_EXIT)
            self.assertEqual(termination, "hard_timeout")
            self.assertTrue(self.wait_for_file(ready, 1))
            process_group = int(group_file.read_text(encoding="ascii"))
            self.assertFalse(
                execution.process_group_exists(process_group),
                f"probe {probe} returned while process group {process_group} survived",
            )

    def test_symlinked_directory_resolves_both_executable_paths(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        alias = self.directory / "directory-alias"
        alias.symlink_to(self.directory, target_is_directory=True)
        variant["directory"] = str(alias)
        self.assertEqual(execution.run_variant(variant, command), 0)
        self.assertEqual(self.record(variant)["outcome"], "PASS")

    def test_same_named_executable_in_another_directory_is_rejected(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        variant["directory"] = str(self.directory / "different")
        with self.assertRaisesRegex(ValueError, "different executable"):
            execution.run_variant(variant, command)

    def test_width_mismatch_is_error(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        # Force a non-ILP32 header independently of the calibration host's width.
        Path(command[0]).write_bytes(b"\x7fELF\x02")
        variant["width"] = "ilp32"
        self.assertEqual(execution.run_variant(variant, command), 2)
        self.assertEqual(self.record(variant)["outcome"], "ERROR")

    def test_duplicate_receipt_cannot_overwrite(self):
        variant = self.variant()
        command = self.compile(variant, 0)
        self.assertEqual(execution.run_variant(variant, command), 0)
        with self.assertRaises(FileExistsError):
            execution.run_variant(variant, command)

    def test_missing_and_unexecuted_receipts_fail_collection(self):
        variant = self.variant()
        variants = {variant["id"]: variant}
        self.assertEqual(execution.collect(variants, self.receipts)[0]["outcome"], "ERROR")
        execution.receipt(variant, "PASS")
        self.assertEqual(execution.collect(variants, self.receipts)[0]["outcome"], "ERROR")

    def test_exit_zero_aggregate_without_binary_execution_fails(self):
        report = self.directory / "report.json"
        self.assertEqual(execution.aggregate(shutil.which("true"), report), 1)
        records = json.loads(report.read_text())["variants"]
        self.assertTrue(records)
        self.assertTrue(all(record["outcome"] == "ERROR" for record in records))

    def test_detached_aggregate_timeout_stops_process_group_and_is_reported(self):
        report = self.directory / "aggregate-timeout.json"
        marker = self.directory / "aggregate-descendant-terminated"
        observer = self.directory / "make-timeout-observer"
        observer.write_text(
            f"#!{sys.executable}\n"
            "import os, pathlib, signal, sys, time\n"
            "child = (\"import pathlib, signal, sys, time; \""
            "         \"marker=pathlib.Path(sys.argv[1]); \""
            "         \"signal.signal(signal.SIGTERM, lambda *_: \""
            "         \"(marker.write_text('terminated', encoding='ascii'), \""
            "         \"sys.exit(0))); time.sleep(60)\")\n"
            "child_pid = os.fork()\n"
            "if child_pid == 0:\n"
            f"    os.execv(sys.executable, [sys.executable, '-c', child, {str(marker)!r}])\n"
            "time.sleep(60)\n"
        )
        observer.chmod(0o755)

        self.assertEqual(
            execution.aggregate(
                str(observer), report, aggregate_timeout_seconds=0.2
            ),
            1,
        )

        payload = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(payload["returncode"], execution.TIMEOUT_EXIT)
        self.assertEqual(payload["termination"], "hard_timeout")
        self.assertEqual(payload["timeout_seconds"], 0.2)
        self.assertTrue(marker.is_file())

    def test_probe_executes_and_checks_width(self):
        probe = execution.ROOT / "tools/compiler-probe.sh"
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        good = subprocess.run(["sh", str(probe), "native", *compiler],
                              text=True, capture_output=True, check=True)
        self.assertEqual(good.stdout.strip(), "yes")
        # A successful compiler command that produces no executable is insufficient.
        absent = subprocess.run(["sh", str(probe), "ilp32", shutil.which("true")],
                                text=True, capture_output=True, check=True)
        self.assertEqual(absent.stdout.strip(), "no")
        unsupported = subprocess.run(["sh", str(probe), "ilp32", shutil.which("false")],
                                     text=True, capture_output=True, check=True)
        self.assertEqual(unsupported.stdout.strip(), "no")

    def test_detached_make_preserves_command_line_overrides(self):
        observer = self.directory / "make-observer"
        observed = self.directory / "make-arguments.json"
        observer.write_text(
            f"#!{sys.executable}\nimport json, os, sys\n"
            f"with open({str(observed)!r}, 'w') as output:\n"
            "    json.dump({'args': sys.argv[1:], 'flags': "
            "[os.getenv('MAKEFLAGS'), os.getenv('MFLAGS')]}, output)\n"
        )
        observer.chmod(0o755)
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        overrides = ["HOSTCC=cc -O1", "HOST_CC=cc -O2", "CC=cc -O3",
                     "HOST_CFLAGS=-DOVERRIDE=1 -g", ".MAKE.LEVEL.ENV=MAKELEVEL"]
        result = subprocess.run(
            ["bmake", "-j4", "-C", str(execution.ROOT), "MACHINE=rp2040",
             f"MAKE={observer}", f"PYTHON={sys.executable}", *overrides,
             "REQUIRE_ILP32=no", "TEST_EXECUTION_REQUIRED=no",
             "TEST_EXECUTION_DIR=wrong-directory", "check-ilp32-execution"],
            env=environment, text=True, capture_output=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("required executable recipe produced no receipt",
                      result.stdout + result.stderr)
        record = json.loads(observed.read_text())
        for override in overrides:
            self.assertIn(override, record["args"])
        effective = dict(argument.split("=", 1) for argument in record["args"]
                         if "=" in argument)
        self.assertEqual(effective["REQUIRE_ILP32"], "yes")
        self.assertEqual(effective["TEST_EXECUTION_REQUIRED"], "yes")
        self.assertNotEqual(effective["TEST_EXECUTION_DIR"], "wrong-directory")
        self.assertEqual(record["flags"], [None, None])

    def test_runtime_failure_is_unavailable(self):
        failing = self.compile(self.variant("runtime-failure"), 1)[0]
        compiler = self.directory / "compiler"
        compiler.write_text(
            "#!/bin/sh\nset -eu\n"
            'while [ "$1" != -o ]; do shift; done\n'
            f'cp {shlex.quote(failing)} "$2"\n'
        )
        compiler.chmod(0o755)
        result = subprocess.run(
            ["sh", str(execution.ROOT / "tools/compiler-probe.sh"), "native", str(compiler)],
            text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "no")

    def test_actual_make_recipe_optional_and_required_capability(self):
        # A failing compiler drives the real capability branch without rebuilding
        # or deleting any product in the checkout that runs the calibration.
        command = ["bmake", "-C", str(execution.ROOT / "tests/backgammon_contracts"),
                   "MACHINE=rp2040", "HOSTCC=false", "check"]
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        environment.pop("TEST_EXECUTION_DIR", None)
        environment["PYTHON"] = os.environ["PYTHON"]
        optional = subprocess.run(command, env=environment, text=True,
                                  capture_output=True, check=False)
        self.assertEqual(optional.returncode, 0, optional.stdout + optional.stderr)
        self.assertIn("SKIP backgammon.ilp32", optional.stdout)
        environment["REQUIRE_ILP32"] = "yes"
        required = subprocess.run(command, env=environment, text=True,
                                  capture_output=True, check=False)
        self.assertNotEqual(required.returncode, 0)
        self.assertIn("ERROR backgammon.ilp32", required.stdout)

    def test_native_output_cannot_satisfy_width_probe(self):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        result = subprocess.run(
            ["sh", str(execution.ROOT / "tools/compiler-probe.sh"), "ilp32", *compiler,
             "-Dint=short"], text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "no")

    def test_stdio_bounds_recipe_preserves_compiler_argument_boundary(self):
        environment = os.environ.copy()
        environment.pop("MAKEFLAGS", None)
        environment.pop("MFLAGS", None)
        result = subprocess.run(
            ["bmake", "-n", "-C", str(execution.ROOT / "tests/libc_contracts"),
             "MACHINE=rp2040", "HOSTCC=cc -O1", "check-stdio-bounds"],
            env=environment, text=True, capture_output=True, check=True,
        )
        recipe = next(line for line in result.stdout.splitlines()
                      if line.startswith("sh ") and "stdio_bounds.sh" in line)
        self.assertEqual(shlex.split(recipe)[-2:], ["cc -O1", "stdio_bounds_work"])

    def test_required_undefined_sanitizer_rejects_recoverable_fault(self):
        source = self.directory / "undefined.c"
        source.write_text(
            "#include <limits.h>\n"
            "int main(void) { volatile int value = INT_MAX; "
            "volatile int result = value + 1; (void)result; return 0; }\n"
        )
        executable = self.directory / "undefined"
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        process = subprocess.run(
            [*compiler, "-std=c17", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=undefined", "-fno-sanitize-recover=all", str(source),
             "-o", str(executable)], text=True, capture_output=True, check=False,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        result = subprocess.run([str(executable)], text=True, capture_output=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("runtime error", result.stderr)
        for path, variable in [("tests/libc_contracts/Makefile", "ZONE_SAN"),
                               ("tests/libc_sysctl/Makefile", "SAN")]:
            contents = (execution.ROOT / path).read_text().replace("\\\n", " ")
            flags = re.search(rf"^{variable}=([^\n]+)", contents, re.MULTILINE).group(1)
            self.assertIn("-fno-sanitize-recover=all", flags)

    def test_inventory_has_unique_owners_and_executable_recipes(self):
        variants = execution.inventory()
        for variant in variants.values():
            self.assertEqual(variant["owner"], "firmware/posix-sh")
            self.assertIn(variant["id"], variant["recipe"])
            self.assertTrue(variant["condition"])
            self.assertTrue(variant["prerequisites"])
            self.assertTrue((execution.ROOT / variant["makefile"]).is_file())

    def test_inventory_matches_executable_recipe_identifiers(self):
        variants = execution.inventory()
        paths = {variant["makefile"] for variant in variants.values()}
        paths.update(variant["script"] for variant in variants.values() if "script" in variant)
        observed = []
        for path in paths:
            contents = (execution.ROOT / path).read_text()
            observed.extend(re.findall(r"\brun ([a-z0-9_.-]+) --", contents))
        self.assertCountEqual(observed, variants)

    def test_array_memory_cross_gate_is_aggregate_owned(self):
        result = subprocess.run(
            ["bmake", "-C", str(execution.ROOT), "-V", "CROSS_COMPOSITION_GATES"],
            text=True, capture_output=True, check=True,
        )
        self.assertEqual(
            result.stdout.split().count("check-libc-array-memory-line-cross"), 1
        )

    def test_line_reading_has_unconditional_native_recipe(self):
        result = subprocess.run(
            ["bmake", "-n", "-C", str(execution.ROOT / "tests/libc_contracts"),
             "MACHINE=rp2040", "ARRAY_MEMORY_SAN_OK=no",
             "ARRAY_MEMORY_SAN32_OK=no", "check-array-memory-line"],
            text=True, capture_output=True, check=True,
        )
        self.assertIn("run libc.line-reading.native --", result.stdout)


if __name__ == "__main__":
    unittest.main()
