"""Run one build and its aggregate check under one retained deadline."""

import argparse
import json
import os
import re
import signal
import sys
import time
from pathlib import Path

import run_logged_gate as gate


class SequenceSignal(BaseException):
    def __init__(self, number):
        super().__init__(f"signal {number}")
        self.number = number


def stage_arguments(args, invocation_id, directory, stage, timeout_seconds):
    return argparse.Namespace(
        label=f"{args.label}-{stage}",
        log=directory / f"{stage}.log",
        status=directory / f"{stage}.status.json",
        evidence_root=None,
        execution_lock=None,
        execution_quarantine=gate.execution_quarantine_path(args.execution_lock),
        sequence_invocation_id=invocation_id,
        heartbeat_seconds=args.heartbeat_seconds,
        idle_timeout_seconds=args.idle_timeout_seconds,
        timeout_seconds=timeout_seconds,
        terminate_grace_seconds=args.terminate_grace_seconds,
        command=[*args.command, stage],
    )


def blocked_check_record(
    args,
    invocation_id,
    directory,
    *,
    termination="prerequisite_failed",
    blocked_by="build",
):
    now = gate.utc_timestamp()
    return {
        "schema": 1,
        "label": f"{args.label}-check",
        "state": "blocked",
        "command": [*args.command, "check"],
        "cwd": str(Path.cwd()),
        "pid": None,
        "started_at": now,
        "updated_at": now,
        "elapsed_seconds": 0.0,
        "quiet_seconds": 0.0,
        "output_bytes": 0,
        "last_output": "",
        "child_exit_code": None,
        "exit_code": None,
        "termination": termination,
        "terminal_queue_dropped_bytes": 0,
        "invocation_id": f"{args.label}-check-{os.getpid()}",
        "sequence_invocation_id": invocation_id,
        "evidence_directory": str(directory),
        "log_path": str(directory / "check.log"),
        "status_path": str(directory / "check.status.json"),
        "blocked_by": blocked_by,
    }


def stage_error_record(args, invocation_id, directory, stage, error):
    now = gate.utc_timestamp()
    return {
        "schema": 1,
        "label": f"{args.label}-{stage}",
        "state": "error",
        "command": [*args.command, stage],
        "cwd": str(Path.cwd()),
        "pid": None,
        "started_at": now,
        "updated_at": now,
        "elapsed_seconds": 0.0,
        "quiet_seconds": 0.0,
        "output_bytes": 0,
        "last_output": "",
        "child_exit_code": None,
        "exit_code": gate.RUNNER_ERROR_EXIT,
        "termination": "stage_runner_error",
        "terminal_queue_dropped_bytes": 0,
        "invocation_id": f"{args.label}-{stage}-{os.getpid()}",
        "sequence_invocation_id": invocation_id,
        "evidence_directory": str(directory),
        "log_path": str(directory / f"{stage}.log"),
        "status_path": str(directory / f"{stage}.status.json"),
        "error_type": type(error).__name__,
        "diagnostic": str(error),
    }


def publish_summary(summary_path, latest_path, summary):
    gate.atomic_status(summary_path, summary)
    gate.atomic_status(latest_path, summary)


def stage_summary(record, status_path):
    return {
        "state": record["state"],
        "exit_code": record["exit_code"],
        "termination": record["termination"],
        "status_path": str(status_path),
    }


def read_stage_record(directory, stage):
    status_path = directory / f"{stage}.status.json"
    try:
        return json.loads(status_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def interrupted_stage_record(args, invocation_id, directory, stage, number):
    now = gate.utc_timestamp()
    return {
        "schema": 1,
        "label": f"{args.label}-{stage}",
        "state": "interrupted",
        "command": [*args.command, stage],
        "cwd": str(Path.cwd()),
        "pid": None,
        "started_at": now,
        "updated_at": now,
        "elapsed_seconds": 0.0,
        "quiet_seconds": 0.0,
        "output_bytes": 0,
        "last_output": "",
        "child_exit_code": None,
        "exit_code": 128 + number,
        "termination": f"signal_{number}",
        "terminal_queue_dropped_bytes": 0,
        "invocation_id": f"{args.label}-{stage}-{os.getpid()}",
        "sequence_invocation_id": invocation_id,
        "evidence_directory": str(directory),
        "log_path": str(directory / f"{stage}.log"),
        "status_path": str(directory / f"{stage}.status.json"),
    }


def deadline_blocked_check(args, invocation_id, directory):
    return blocked_check_record(
        args,
        invocation_id,
        directory,
        termination="deadline_exhausted",
        blocked_by="sequence_deadline",
    )


def run(args):
    evidence = argparse.Namespace(
        label=args.label,
        evidence_root=args.evidence_root,
        log=None,
        status=None,
    )
    gate.prepare_evidence(evidence)
    directory = evidence.evidence_directory
    summary_path = directory / "status.json"
    started = time.monotonic()
    started_at = gate.utc_timestamp()
    summary = {
        "schema": 1,
        "label": args.label,
        "state": "starting",
        "command": args.command,
        "cwd": str(Path.cwd()),
        "started_at": started_at,
        "updated_at": started_at,
        "elapsed_seconds": 0.0,
        "exit_code": None,
        "termination": None,
        "timeout_seconds": args.timeout_seconds,
        "invocation_id": evidence.invocation_id,
        "evidence_directory": str(directory),
        "status_path": str(summary_path),
        "stages": {
            "build": {"state": "pending"},
            "check": {"state": "pending"},
        },
    }
    lock_descriptor = None
    previous_handlers = {}
    try:
        lock_descriptor = gate.acquire_execution_lock(args.execution_lock)
    except (OSError, ValueError) as error:
        summary.update(
            state="error",
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
            exit_code=gate.RUNNER_ERROR_EXIT,
            termination=(
                "execution_quarantined"
                if isinstance(error, gate.ExecutionQuarantined)
                else "execution_lock_busy"
            ),
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        print(f"logged-build-check: {error}", file=sys.stderr)
        return gate.RUNNER_ERROR_EXIT

    def interrupt(number, _frame):
        raise SequenceSignal(number)

    for number in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[number] = signal.signal(number, interrupt)

    active_stage = "build"
    try:
        summary["state"] = "running"
        publish_summary(summary_path, evidence.latest_status, summary)
        remaining = args.timeout_seconds - (time.monotonic() - started)
        if remaining <= 0:
            blocked = deadline_blocked_check(
                args, evidence.invocation_id, directory
            )
            gate.atomic_status(directory / "check.status.json", blocked)
            summary["stages"]["check"] = stage_summary(
                blocked, directory / "check.status.json"
            )
            summary.update(
                state="timed_out",
                updated_at=gate.utc_timestamp(),
                elapsed_seconds=round(time.monotonic() - started, 3),
                exit_code=gate.TIMEOUT_EXIT,
                termination="deadline_exhausted",
            )
            publish_summary(summary_path, evidence.latest_status, summary)
            return gate.TIMEOUT_EXIT
        build_args = stage_arguments(
            args, evidence.invocation_id, directory, "build", remaining
        )
        summary["stages"]["build"] = {
            "state": "running",
            "status_path": str(directory / "build.status.json"),
        }
        summary.update(
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        build_exit = gate.run(build_args)
        build_record = json.loads(
            (directory / "build.status.json").read_text(encoding="utf-8")
        )
        summary["stages"]["build"] = stage_summary(
            build_record, directory / "build.status.json"
        )
        if build_exit != 0:
            blocked = blocked_check_record(args, evidence.invocation_id, directory)
            gate.atomic_status(directory / "check.status.json", blocked)
            summary["stages"]["check"] = {
                "state": "blocked",
                "exit_code": None,
                "termination": "prerequisite_failed",
                "status_path": str(directory / "check.status.json"),
            }
            summary.update(
                state=build_record["state"],
                updated_at=gate.utc_timestamp(),
                elapsed_seconds=round(time.monotonic() - started, 3),
                exit_code=build_exit,
                termination=build_record["termination"],
            )
            publish_summary(summary_path, evidence.latest_status, summary)
            return build_exit

        remaining = args.timeout_seconds - (time.monotonic() - started)
        if remaining <= 0:
            blocked = deadline_blocked_check(
                args, evidence.invocation_id, directory
            )
            gate.atomic_status(directory / "check.status.json", blocked)
            summary["stages"]["check"] = stage_summary(
                blocked, directory / "check.status.json"
            )
            summary.update(
                state="timed_out",
                updated_at=gate.utc_timestamp(),
                elapsed_seconds=round(time.monotonic() - started, 3),
                exit_code=gate.TIMEOUT_EXIT,
                termination="deadline_exhausted",
            )
            publish_summary(summary_path, evidence.latest_status, summary)
            return gate.TIMEOUT_EXIT
        active_stage = "check"
        summary["stages"]["check"] = {
            "state": "running",
            "status_path": str(directory / "check.status.json"),
        }
        summary.update(
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        check_args = stage_arguments(
            args, evidence.invocation_id, directory, "check", remaining
        )
        check_exit = gate.run(check_args)
        check_record = json.loads(
            (directory / "check.status.json").read_text(encoding="utf-8")
        )
        summary["stages"]["check"] = stage_summary(
            check_record, directory / "check.status.json"
        )
        summary.update(
            state=check_record["state"],
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
            exit_code=check_exit,
            termination=check_record["termination"],
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        return check_exit
    except SequenceSignal as interruption:
        for stage in ("build", "check"):
            record = read_stage_record(directory, stage)
            if record is not None:
                summary["stages"][stage] = stage_summary(
                    record, directory / f"{stage}.status.json"
                )
        if summary["stages"][active_stage]["state"] in {"pending", "running"}:
            interrupted = interrupted_stage_record(
                args,
                evidence.invocation_id,
                directory,
                active_stage,
                interruption.number,
            )
            gate.atomic_status(
                directory / f"{active_stage}.status.json", interrupted
            )
            summary["stages"][active_stage] = stage_summary(
                interrupted, directory / f"{active_stage}.status.json"
            )
        if summary["stages"]["check"]["state"] == "pending":
            blocked = blocked_check_record(
                args,
                evidence.invocation_id,
                directory,
                termination=f"signal_{interruption.number}",
                blocked_by="sequence",
            )
            gate.atomic_status(directory / "check.status.json", blocked)
            summary["stages"]["check"] = stage_summary(
                blocked, directory / "check.status.json"
            )
        summary.update(
            state="interrupted",
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
            exit_code=128 + interruption.number,
            termination=f"signal_{interruption.number}",
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        return 128 + interruption.number
    except Exception as error:
        error_record = stage_error_record(
            args, evidence.invocation_id, directory, active_stage, error
        )
        gate.atomic_status(directory / f"{active_stage}.status.json", error_record)
        summary["stages"][active_stage] = {
            "state": "error",
            "exit_code": gate.RUNNER_ERROR_EXIT,
            "termination": "stage_runner_error",
            "status_path": str(directory / f"{active_stage}.status.json"),
        }
        if active_stage == "build":
            blocked = blocked_check_record(args, evidence.invocation_id, directory)
            gate.atomic_status(directory / "check.status.json", blocked)
            summary["stages"]["check"] = {
                "state": "blocked",
                "exit_code": None,
                "termination": "prerequisite_failed",
                "status_path": str(directory / "check.status.json"),
            }
        summary.update(
            state="error",
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
            exit_code=gate.RUNNER_ERROR_EXIT,
            termination="stage_runner_error",
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        print(
            f"logged-build-check: {active_stage} stage runner error: {error}",
            file=sys.stderr,
        )
        return gate.RUNNER_ERROR_EXIT
    finally:
        for number, handler in previous_handlers.items():
            signal.signal(number, handler)
        os.close(lock_descriptor)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--execution-lock", type=Path, required=True)
    parser.add_argument(
        "--heartbeat-seconds",
        type=lambda value: gate.positive_seconds(value, "heartbeat"),
        default=30.0,
    )
    parser.add_argument(
        "--idle-timeout-seconds",
        type=lambda value: gate.positive_seconds(value, "idle timeout", True),
        default=600.0,
    )
    parser.add_argument(
        "--timeout-seconds",
        type=lambda value: gate.positive_seconds(value, "hard timeout"),
        default=3600.0,
    )
    parser.add_argument(
        "--terminate-grace-seconds",
        type=lambda value: gate.positive_seconds(value, "termination grace"),
        default=2.0,
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    args.command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not args.command:
        parser.error("a make command prefix is required after --")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", args.label):
        parser.error("label must contain only letters, digits, dot, underscore or dash")
    return args


def main():
    try:
        return run(parse_arguments())
    except OSError as error:
        print(
            f"logged-build-check: evidence setup failed: {error}",
            file=sys.stderr,
        )
        return gate.RUNNER_ERROR_EXIT


if __name__ == "__main__":
    sys.exit(main())
