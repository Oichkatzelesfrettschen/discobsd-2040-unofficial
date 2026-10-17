"""Run one build and its aggregate check under one retained deadline."""

import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

import run_logged_gate as gate


def stage_arguments(args, invocation_id, directory, stage, timeout_seconds):
    return argparse.Namespace(
        label=f"{args.label}-{stage}",
        log=directory / f"{stage}.log",
        status=directory / f"{stage}.status.json",
        evidence_root=None,
        execution_lock=None,
        sequence_invocation_id=invocation_id,
        heartbeat_seconds=args.heartbeat_seconds,
        idle_timeout_seconds=args.idle_timeout_seconds,
        timeout_seconds=timeout_seconds,
        terminate_grace_seconds=args.terminate_grace_seconds,
        command=[*args.command, stage],
    )


def blocked_check_record(args, invocation_id, directory):
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
        "termination": "prerequisite_failed",
        "terminal_queue_dropped_bytes": 0,
        "invocation_id": f"{args.label}-check-{os.getpid()}",
        "sequence_invocation_id": invocation_id,
        "evidence_directory": str(directory),
        "log_path": str(directory / "check.log"),
        "status_path": str(directory / "check.status.json"),
        "blocked_by": "build",
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
    try:
        lock_descriptor = gate.acquire_execution_lock(args.execution_lock)
    except (OSError, ValueError) as error:
        summary.update(
            state="error",
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
            exit_code=gate.RUNNER_ERROR_EXIT,
            termination="execution_lock_busy",
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        print(f"logged-build-check: {error}", file=sys.stderr)
        return gate.RUNNER_ERROR_EXIT

    active_stage = "build"
    try:
        summary["state"] = "running"
        publish_summary(summary_path, evidence.latest_status, summary)
        remaining = args.timeout_seconds - (time.monotonic() - started)
        if remaining <= 0:
            remaining = 0.001
        build_args = stage_arguments(
            args, evidence.invocation_id, directory, "build", remaining
        )
        build_exit = gate.run(build_args)
        build_record = json.loads(
            (directory / "build.status.json").read_text(encoding="utf-8")
        )
        summary["stages"]["build"] = {
            "state": build_record["state"],
            "exit_code": build_record["exit_code"],
            "termination": build_record["termination"],
            "status_path": str(directory / "build.status.json"),
        }
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
            remaining = 0.001
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
        summary["stages"]["check"] = {
            "state": check_record["state"],
            "exit_code": check_record["exit_code"],
            "termination": check_record["termination"],
            "status_path": str(directory / "check.status.json"),
        }
        summary.update(
            state=check_record["state"],
            updated_at=gate.utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - started, 3),
            exit_code=check_exit,
            termination=check_record["termination"],
        )
        publish_summary(summary_path, evidence.latest_status, summary)
        return check_exit
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
