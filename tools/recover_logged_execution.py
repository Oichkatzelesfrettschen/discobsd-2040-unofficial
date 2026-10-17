"""Recover a fail-closed logged execution owner after operator inspection."""

import argparse
import fcntl
import hashlib
import json
import os
import stat
import sys
import uuid
from pathlib import Path

import run_logged_gate as gate

TERMINAL_STATES = {
    "blocked",
    "completed",
    "error",
    "failed",
    "interrupted",
    "timed_out",
}


def acquire_recovery_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_RDWR | os.O_CREAT
    flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o666)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(f"execution lock must name a regular file: {path}")
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError(f"active execution owner holds {path}") from error
        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def read_terminal_status(path, invocation_id):
    status = gate._read_regular_json(path, "execution status receipt")
    if not isinstance(status, dict) or status.get("schema") != 1:
        raise ValueError(f"invalid execution status receipt: {path}")
    if status.get("invocation_id") != invocation_id:
        raise ValueError(f"execution status invocation mismatch: {path}")
    if status.get("state") not in TERMINAL_STATES:
        raise ValueError(f"execution status is not terminal: {path}")
    exit_code = status.get("exit_code")
    if not isinstance(exit_code, int) or isinstance(exit_code, bool):
        raise ValueError(f"terminal execution status lacks an exit code: {path}")
    return status


def recovery_receipt_path(execution_lock):
    timestamp = gate.utc_timestamp().replace(":", "").replace("-", "")
    identifier = uuid.uuid4().hex[:8]
    return execution_lock.with_name(
        f"{execution_lock.name}.recovery-{timestamp}-{identifier}.json"
    )


def recover(args):
    execution_lock = args.execution_lock.resolve()
    status_path = args.status.resolve()
    lock_descriptor = acquire_recovery_lock(execution_lock)
    ownership_path = gate.execution_ownership_path(execution_lock)
    ownership_descriptor = None
    try:
        ownership_descriptor = gate._acquire_ownership_update_lock(
            ownership_path
        )
        ownership = gate.read_execution_ownership(ownership_path)
        if ownership is None:
            raise ValueError(f"execution ownership receipt is absent: {ownership_path}")
        if ownership["invocation_id"] != args.invocation_id:
            raise ValueError(
                f"execution ownership invocation mismatch: {ownership_path}"
            )
        if not gate.execution_ownership_blocks(ownership):
            raise ValueError(
                f"execution ownership is already released: {ownership_path}"
            )
        live_groups = [
            group for group in ownership["process_groups"]
            if gate.group_exists(group)
        ]
        if live_groups:
            joined = ", ".join(map(str, live_groups))
            raise ValueError(f"recorded process group remains live: {joined}")
        recorded_status_path = Path(ownership["status_path"]).resolve()
        if recorded_status_path != status_path:
            raise ValueError(
                f"execution status path mismatch: {recorded_status_path}"
            )
        status = read_terminal_status(status_path, args.invocation_id)
        status_record = json.dumps(
            status, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")

        recovery_path = recovery_receipt_path(execution_lock)
        recovery = {
            "schema": 1,
            "state": "ownership_recovered",
            "created_at": gate.utc_timestamp(),
            "execution_lock": str(execution_lock),
            "invocation_id": args.invocation_id,
            "operator_uid": os.getuid(),
            "reason": args.reason,
            "confirmation": "unrecorded_process_groups_absent",
            "previous_ownership": ownership,
            "status": status,
            "status_path": str(status_path),
            "status_record_sha256": hashlib.sha256(status_record).hexdigest(),
        }
        gate.atomic_status(recovery_path, recovery)

        released = dict(ownership)
        released.update(
            state="released",
            process_groups=[],
            pending_launches=[],
            recovered_at=gate.utc_timestamp(),
            recovery_receipt=str(recovery_path),
        )
        gate.atomic_status(ownership_path, released)
    finally:
        if ownership_descriptor is not None:
            os.close(ownership_descriptor)
        os.close(lock_descriptor)

    print(
        f"logged-execution recovery: released {args.invocation_id}; "
        f"evidence={recovery_path}"
    )
    return 0


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execution-lock", type=Path, required=True)
    parser.add_argument("--invocation-id", required=True)
    parser.add_argument("--status", type=Path, required=True)
    parser.add_argument("--reason", required=True)
    parser.add_argument(
        "--confirm-unrecorded-groups-absent",
        action="store_true",
        help="confirm that host inspection found no unrecorded surviving group",
    )
    args = parser.parse_args(argv)
    if not args.reason.strip():
        parser.error("--reason must describe the deciding inspection")
    if not args.confirm_unrecorded_groups_absent:
        parser.error("--confirm-unrecorded-groups-absent is required")
    return args


def main(argv=None):
    try:
        return recover(parse_arguments(argv))
    except (OSError, ValueError, TimeoutError) as error:
        print(f"logged-execution recovery: {error}", file=sys.stderr)
        return gate.RUNNER_ERROR_EXIT


if __name__ == "__main__":
    sys.exit(main())
