"""Run one build or gate with streaming logs, heartbeats, and deadlines."""

import argparse
import dataclasses
import datetime
import fcntl
import json
import math
import os
import queue
import re
import selectors
import shlex
import signal
import stat
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

TIMEOUT_EXIT = 124
RUNNER_ERROR_EXIT = 125
READ_SIZE = 65536
TERMINAL_QUEUE_CHUNKS = 32
TERMINAL_CLOSE_SECONDS = 0.2
PROCESS_SNAPSHOT_SECONDS = 1.0
OWNERSHIP_LOCK_SECONDS = 1.0
OWNERSHIP_PATH_ENV = "_DISCOBSD_EXECUTION_OWNERSHIP_PATH"
OWNERSHIP_INVOCATION_ENV = "_DISCOBSD_EXECUTION_OWNERSHIP_INVOCATION"


@dataclasses.dataclass(frozen=True)
class StopResult:
    child_exit_code: object
    group_stopped: bool


class ExecutionQuarantined(ValueError):
    pass


def utc_timestamp():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(
        timespec="seconds"
    ).replace("+00:00", "Z")


def positive_seconds(value, name, allow_zero=False):
    try:
        seconds = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"{name} must be numeric") from error
    if not math.isfinite(seconds) or seconds < 0 or (
        seconds == 0 and not allow_zero
    ):
        qualifier = "nonnegative" if allow_zero else "positive"
        raise argparse.ArgumentTypeError(f"{name} must be {qualifier}")
    return seconds


def atomic_status(path, record):
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    temporary.write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def publish_status(args, record):
    atomic_status(args.status, record)
    if args.latest_status is not None:
        atomic_status(args.latest_status, record)


def prepare_evidence(args):
    if args.evidence_root is None:
        timestamp = datetime.datetime.now(datetime.timezone.utc).strftime(
            "%Y%m%dT%H%M%SZ"
        )
        args.invocation_id = f"{args.label}-{timestamp}-{os.getpid()}"
        args.evidence_directory = args.log.parent
        args.latest_status = None
        return

    args.evidence_root.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y%m%dT%H%M%SZ"
    )
    args.invocation_id = (
        f"{args.label}-{timestamp}-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    )
    args.evidence_directory = args.evidence_root / args.invocation_id
    args.evidence_directory.mkdir()
    args.log = args.evidence_directory / "gate.log"
    args.status = args.evidence_directory / "status.json"
    args.latest_status = args.evidence_root / f"{args.label}.latest.json"


def write_log(stream, data):
    stream.write(data)
    stream.flush()


def open_regular_log(path):
    """Open a fresh regular-file log without following a final symlink."""
    try:
        existing = os.lstat(path)
    except FileNotFoundError:
        existing = None
    if existing is not None and not stat.S_ISREG(existing.st_mode):
        raise ValueError(f"log path must name a regular file: {path}")
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NONBLOCK
    flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o666)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(f"log path must name a regular file: {path}")
        return os.fdopen(descriptor, "wb")
    except Exception:
        os.close(descriptor)
        raise


def acquire_log_lock(path):
    """Own one log path without truncating another active invocation."""
    lock_path = path.with_name(f".{path.name}.lock")
    flags = os.O_RDWR | os.O_CREAT
    flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(lock_path, flags, 0o666)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(f"log lock must name a regular file: {lock_path}")
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError(f"active log owner holds {path}") from error
        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def execution_quarantine_path(path):
    return path.with_name(f"{path.name}.quarantine.json")


def execution_ownership_path(path):
    return path.with_name(f"{path.name}.ownership.json")


def _read_regular_json(path, description):
    try:
        existing = os.lstat(path)
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(existing.st_mode):
        raise ValueError(f"{description} must name a regular file: {path}")
    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(f"{description} must name a regular file: {path}")
        with os.fdopen(descriptor, encoding="utf-8") as stream:
            descriptor = None
            return json.load(stream)
    except Exception:
        if descriptor is not None:
            os.close(descriptor)
        raise


def read_execution_quarantine(path):
    record = _read_regular_json(path, "execution quarantine")
    if record is None:
        return None
    if not isinstance(record, dict) or record.get("schema") != 1:
        raise ValueError(f"invalid execution quarantine receipt: {path}")
    process_group = record.get("process_group")
    if (
        record.get("state") != "cleanup_incomplete"
        or not isinstance(process_group, int)
        or isinstance(process_group, bool)
        or process_group <= 0
    ):
        raise ValueError(f"invalid execution quarantine receipt: {path}")
    return record


def read_execution_ownership(path):
    record = _read_regular_json(path, "execution ownership receipt")
    if record is None:
        return None
    if not isinstance(record, dict) or record.get("schema") != 1:
        raise ValueError(f"invalid execution ownership receipt: {path}")
    process_groups = record.get("process_groups")
    if (
        record.get("state") not in {
            "active", "cleanup_incomplete", "released"
        }
        or not isinstance(record.get("invocation_id"), str)
        or not isinstance(process_groups, list)
        or any(
            not isinstance(group, int)
            or isinstance(group, bool)
            or group <= 0
            for group in process_groups
        )
        or len(process_groups) != len(set(process_groups))
    ):
        raise ValueError(f"invalid execution ownership receipt: {path}")
    return record


def execution_ownership_blocks(record):
    if record["state"] == "active":
        return True
    if record["state"] == "released":
        return False
    groups = record["process_groups"]
    return not groups or any(group_exists(group) for group in groups)


def _acquire_ownership_update_lock(path):
    lock_path = path.with_name(f".{path.name}.update.lock")
    flags = os.O_RDWR | os.O_CREAT
    flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(lock_path, flags, 0o666)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(
                f"execution ownership update lock must name a regular file: "
                f"{lock_path}"
            )
        deadline = time.monotonic() + OWNERSHIP_LOCK_SECONDS
        while True:
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return descriptor
            except BlockingIOError as error:
                if time.monotonic() >= deadline:
                    raise TimeoutError(
                        f"execution ownership update lock timed out: {lock_path}"
                    ) from error
                time.sleep(0.01)
    except Exception:
        os.close(descriptor)
        raise


def begin_execution_ownership(path, record):
    ownership = {
        "schema": 1,
        "state": "active",
        "process_groups": [],
        "label": record["label"],
        "invocation_id": record["invocation_id"],
        "created_at": utc_timestamp(),
        "status_path": record["status_path"],
    }
    atomic_status(path, ownership)


def register_execution_group(path, invocation_id, process_group):
    descriptor = _acquire_ownership_update_lock(path)
    try:
        record = read_execution_ownership(path)
        if record is None or record["invocation_id"] != invocation_id:
            raise ValueError(f"execution ownership invocation mismatch: {path}")
        if record["state"] not in {"active", "cleanup_incomplete"}:
            raise ValueError(f"execution ownership is already released: {path}")
        if process_group not in record["process_groups"]:
            record["process_groups"].append(process_group)
            record["process_groups"].sort()
            atomic_status(path, record)
    finally:
        os.close(descriptor)


def register_inherited_execution_group(process_group):
    path = os.environ.get(OWNERSHIP_PATH_ENV)
    invocation_id = os.environ.get(OWNERSHIP_INVOCATION_ENV)
    if path is None and invocation_id is None:
        return
    if path is None or invocation_id is None:
        raise ValueError("incomplete inherited execution ownership environment")
    register_execution_group(Path(path), invocation_id, process_group)


def unregister_execution_group(path, invocation_id, process_group):
    descriptor = _acquire_ownership_update_lock(path)
    try:
        record = read_execution_ownership(path)
        if record is None or record["invocation_id"] != invocation_id:
            raise ValueError(f"execution ownership invocation mismatch: {path}")
        if record["state"] not in {"active", "cleanup_incomplete"}:
            raise ValueError(f"execution ownership is already released: {path}")
        if process_group in record["process_groups"]:
            record["process_groups"].remove(process_group)
            atomic_status(path, record)
    finally:
        os.close(descriptor)


def unregister_inherited_execution_group(process_group):
    path = os.environ.get(OWNERSHIP_PATH_ENV)
    invocation_id = os.environ.get(OWNERSHIP_INVOCATION_ENV)
    if path is None and invocation_id is None:
        return
    if path is None or invocation_id is None:
        raise ValueError("incomplete inherited execution ownership environment")
    unregister_execution_group(Path(path), invocation_id, process_group)


def finish_execution_ownership(path, invocation_id, force_incomplete=False):
    descriptor = _acquire_ownership_update_lock(path)
    try:
        record = read_execution_ownership(path)
        if record is None or record["invocation_id"] != invocation_id:
            raise ValueError(f"execution ownership invocation mismatch: {path}")
        surviving = [
            group for group in record["process_groups"] if group_exists(group)
        ]
        record["process_groups"] = surviving
        record["state"] = (
            "cleanup_incomplete"
            if force_incomplete or surviving
            else "released"
        )
        record["updated_at"] = utc_timestamp()
        atomic_status(path, record)
        return surviving
    finally:
        os.close(descriptor)


def acquire_execution_lock(path):
    """Own the build products shared by logged targets in one worktree."""
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
        quarantine_path = execution_quarantine_path(path)
        quarantine = read_execution_quarantine(quarantine_path)
        if quarantine is not None and group_exists(quarantine["process_group"]):
            raise ExecutionQuarantined(
                "execution quarantine holds "
                f"{path}: process group {quarantine['process_group']} from "
                f"{quarantine.get('invocation_id', 'unknown')} remains live"
            )
        ownership_path = execution_ownership_path(path)
        ownership = read_execution_ownership(ownership_path)
        if ownership is not None and execution_ownership_blocks(ownership):
            raise ExecutionQuarantined(
                "execution ownership holds "
                f"{path}: invocation {ownership['invocation_id']} remains "
                f"{ownership['state']}"
            )
        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def execution_quarantine_for_args(args):
    path = getattr(args, "execution_quarantine", None)
    if path is None and args.execution_lock is not None:
        path = execution_quarantine_path(args.execution_lock)
    return path


def execution_ownership_for_args(args):
    path = getattr(args, "execution_ownership", None)
    if path is None and args.execution_lock is not None:
        path = execution_ownership_path(args.execution_lock)
    return path


def publish_execution_quarantine(args, record, process_groups=None):
    path = execution_quarantine_for_args(args)
    if path is None:
        return
    groups = sorted(set(process_groups or [record["pid"]]))
    quarantine = {
        "schema": 1,
        "state": "cleanup_incomplete",
        "process_group": groups[0],
        "process_groups": groups,
        "label": record["label"],
        "invocation_id": record["invocation_id"],
        "created_at": utc_timestamp(),
        "status_path": record["status_path"],
    }
    atomic_status(path, quarantine)


def make_jobserver_descriptors(environment=None):
    """Return live pipe descriptors named by inherited make jobserver flags."""
    environment = os.environ if environment is None else environment
    descriptor_pairs = []
    for variable in ("MAKEFLAGS", "MFLAGS"):
        try:
            tokens = shlex.split(environment.get(variable, ""))
        except ValueError:
            continue
        token_index = 0
        while token_index < len(tokens):
            token = tokens[token_index]
            value = None
            if token == "-J" and token_index + 1 < len(tokens):
                token_index += 1
                value = tokens[token_index]
            elif token.startswith("-J") and len(token) > 2:
                value = token[2:]
            elif token.startswith("--jobserver-auth="):
                value = token.split("=", 1)[1]
            elif token.startswith("--jobserver-fds="):
                value = token.split("=", 1)[1]
            if value is not None:
                descriptor_pairs.append(value)
            token_index += 1

    descriptors = set()
    for pair in descriptor_pairs:
        members = pair.split(",")
        if len(members) != 2:
            continue
        for member in members:
            try:
                descriptor = int(member, 10)
                mode = os.fstat(descriptor).st_mode
            except (OSError, ValueError):
                continue
            if stat.S_ISFIFO(mode) or stat.S_ISSOCK(mode):
                descriptors.add(descriptor)
    return tuple(sorted(descriptors))


class TerminalMirror:
    """Mirror output without letting terminal backpressure stop supervision."""

    def __init__(self, stream):
        self.file_descriptor = stream.fileno()
        self.queue = queue.Queue(maxsize=TERMINAL_QUEUE_CHUNKS)
        self.dropped_bytes = 0
        self.closed = False
        self.thread = threading.Thread(target=self._copy, daemon=True)
        self.thread.start()

    def _copy(self):
        while True:
            data = self.queue.get()
            if data is None:
                return
            view = memoryview(data)
            try:
                while view:
                    written = os.write(self.file_descriptor, view)
                    view = view[written:]
            except (BrokenPipeError, OSError):
                self.closed = True
                return

    def write(self, data):
        if self.closed:
            self.dropped_bytes += len(data)
            return
        try:
            self.queue.put_nowait(data)
        except queue.Full:
            self.dropped_bytes += len(data)

    def close(self):
        if self.closed:
            return
        try:
            self.queue.put_nowait(None)
        except queue.Full:
            return
        self.thread.join(TERMINAL_CLOSE_SECONDS)


def group_exists(process_group):
    try:
        os.killpg(process_group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def stop_process_group(process, grace_seconds, pump=None, output_open=None):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline and group_exists(process.pid):
        process.poll()
        wait_seconds = min(0.02, max(0, deadline - time.monotonic()))
        if pump is None:
            time.sleep(wait_seconds)
        else:
            pump(wait_seconds)
    if group_exists(process.pid):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        child_exit = process.wait(timeout=max(0.1, grace_seconds))
    except subprocess.TimeoutExpired:
        process.kill()
        try:
            child_exit = process.wait(timeout=max(0.1, grace_seconds))
        except subprocess.TimeoutExpired:
            child_exit = None
    group_deadline = time.monotonic() + grace_seconds
    while time.monotonic() < group_deadline and group_exists(process.pid):
        wait_seconds = min(0.02, max(0, group_deadline - time.monotonic()))
        if pump is None:
            time.sleep(wait_seconds)
        else:
            pump(wait_seconds)
    group_stopped = not group_exists(process.pid)
    if pump is not None and output_open is not None:
        drain_deadline = time.monotonic() + TERMINAL_CLOSE_SECONDS
        while output_open() and time.monotonic() < drain_deadline:
            pump(min(0.02, drain_deadline - time.monotonic()))
    return StopResult(child_exit, group_stopped)


def output_state(previous_tail, chunk):
    text = (previous_tail + chunk.decode("utf-8", errors="replace")).replace(
        "\r", "\n"
    )
    tail = text[-8192:]
    lines = tail.splitlines()
    return tail, lines[-1][-400:] if lines else ""


def process_tree_snapshot(root_pid):
    header = "PID PPID PGID STATE ELAPSED CPU COMMAND"
    timestamp = utc_timestamp()
    try:
        result = subprocess.run(
            [
                "ps", "-A", "-o",
                "pid=,ppid=,pgid=,state=,etime=,time=,command=",
            ],
            text=True,
            capture_output=True,
            check=False,
            timeout=PROCESS_SNAPSHOT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return (
            f"[logged-gate] process-snapshot time={timestamp} "
            f"root={root_pid} unavailable={error}\n"
            f"[logged-gate] process-snapshot-columns time={timestamp} {header}\n"
        )
    if result.returncode != 0:
        diagnostic = result.stderr.strip().replace("\r", " ").replace("\n", " ")
        return (
            f"[logged-gate] process-snapshot time={timestamp} root={root_pid} "
            f"ps-exit={result.returncode} diagnostic={diagnostic}\n"
            f"[logged-gate] process-snapshot-columns time={timestamp} {header}\n"
        )

    records = {}
    for line in result.stdout.splitlines():
        fields = line.strip().split(None, 6)
        if len(fields) != 7:
            continue
        try:
            pid, parent_pid, process_group = map(int, fields[:3])
        except ValueError:
            continue
        records[pid] = {
            "parent": parent_pid,
            "group": process_group,
            "fields": fields,
        }

    selected = {
        pid for pid, record in records.items()
        if pid == root_pid or record["group"] == root_pid
    }
    changed = True
    while changed:
        changed = False
        for pid, record in records.items():
            if pid not in selected and record["parent"] in selected:
                selected.add(pid)
                changed = True

    lines = [
        f"[logged-gate] process-snapshot time={timestamp} "
        f"root={root_pid} count={len(selected)}",
        f"[logged-gate] process-snapshot-columns time={timestamp} {header}",
    ]
    for pid in sorted(selected):
        lines.append(
            f"[logged-gate] process time={timestamp} "
            f"{' '.join(records[pid]['fields'])}"
        )
    return "\n".join(lines) + "\n"


def run(args):
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        raise ValueError("a command is required after --")
    if os.name != "posix":
        raise ValueError("logged gate process-group control requires a POSIX host")

    prepare_evidence(args)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.status.parent.mkdir(parents=True, exist_ok=True)
    start_wall = utc_timestamp()
    start = time.monotonic()
    record = {
        "schema": 1,
        "label": args.label,
        "state": "starting",
        "command": command,
        "cwd": str(Path.cwd()),
        "pid": None,
        "started_at": start_wall,
        "updated_at": start_wall,
        "elapsed_seconds": 0.0,
        "timeout_seconds": args.timeout_seconds,
        "quiet_seconds": 0.0,
        "output_bytes": 0,
        "last_output": "",
        "child_exit_code": None,
        "exit_code": None,
        "termination": None,
        "terminal_queue_dropped_bytes": 0,
        "invocation_id": args.invocation_id,
        "evidence_directory": str(args.evidence_directory),
        "log_path": str(args.log),
        "status_path": str(args.status),
        "sequence_invocation_id": getattr(args, "sequence_invocation_id", None),
    }
    execution_lock = None
    ownership_path = execution_ownership_for_args(args)
    owns_ownership = args.execution_lock is not None
    ownership_started = False
    ownership_finished = False
    if args.execution_lock is not None:
        try:
            execution_lock = acquire_execution_lock(args.execution_lock)
        except (OSError, ValueError) as error:
            record.update(
                state="error",
                updated_at=utc_timestamp(),
                elapsed_seconds=round(time.monotonic() - start, 3),
                exit_code=RUNNER_ERROR_EXIT,
                termination=(
                    "execution_quarantined"
                    if isinstance(error, ExecutionQuarantined)
                    else "execution_lock_busy"
                ),
            )
            try:
                publish_status(args, record)
            except OSError:
                pass
            print(f"logged-gate: {error}", file=sys.stderr)
            return RUNNER_ERROR_EXIT
    try:
        log_lock = acquire_log_lock(args.log)
    except (OSError, ValueError) as error:
        print(f"logged-gate: {error}", file=sys.stderr)
        if execution_lock is not None:
            os.close(execution_lock)
        return RUNNER_ERROR_EXIT
    try:
        log_stream = open_regular_log(args.log)
    except (OSError, ValueError) as error:
        record.update(
            state="error",
            updated_at=utc_timestamp(),
            elapsed_seconds=round(time.monotonic() - start, 3),
            exit_code=RUNNER_ERROR_EXIT,
            termination="log_open_error",
        )
        try:
            publish_status(args, record)
        except OSError:
            pass
        print(f"logged-gate: {error}", file=sys.stderr)
        os.close(log_lock)
        if execution_lock is not None:
            os.close(execution_lock)
        return RUNNER_ERROR_EXIT
    if owns_ownership:
        try:
            begin_execution_ownership(ownership_path, record)
            ownership_started = True
        except (OSError, ValueError) as error:
            record.update(
                state="error",
                updated_at=utc_timestamp(),
                elapsed_seconds=round(time.monotonic() - start, 3),
                exit_code=RUNNER_ERROR_EXIT,
                termination="execution_ownership_error",
            )
            try:
                publish_status(args, record)
            except OSError:
                pass
            print(f"logged-gate: {error}", file=sys.stderr)
            log_stream.close()
            os.close(log_lock)
            os.close(execution_lock)
            return RUNNER_ERROR_EXIT
    signal_received = None
    process = None
    selector = None
    pump_callback = None
    output_open = False
    wake_read = None
    wake_write = None
    previous_wakeup = None
    terminal_mirror = TerminalMirror(sys.stdout.buffer)

    def remember_signal(number, _frame):
        nonlocal signal_received
        signal_received = number

    previous_handlers = {}
    for number in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[number] = signal.signal(number, remember_signal)

    try:
        wake_read, wake_write = os.pipe()
        os.set_blocking(wake_read, False)
        os.set_blocking(wake_write, False)
        previous_wakeup = signal.set_wakeup_fd(wake_write)
        with log_stream:

            def emit(message):
                data = message if isinstance(message, bytes) else message.encode("utf-8")
                write_log(log_stream, data)
                terminal_mirror.write(data)
                record["terminal_queue_dropped_bytes"] = (
                    terminal_mirror.dropped_bytes
                )

            def safe_emit(message):
                try:
                    emit(message)
                except OSError:
                    pass

            def safe_status():
                try:
                    publish_status(args, record)
                except OSError:
                    pass

            emit(
                f"[logged-gate] start label={args.label} time={start_wall} "
                f"command={shlex.join(command)}\n"
            )
            try:
                child_environment = os.environ.copy()
                if ownership_path is not None:
                    child_environment[OWNERSHIP_PATH_ENV] = str(ownership_path)
                    child_environment[OWNERSHIP_INVOCATION_ENV] = (
                        record["sequence_invocation_id"] or record["invocation_id"]
                    )
                process = subprocess.Popen(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                    bufsize=0,
                    pass_fds=make_jobserver_descriptors(),
                    env=child_environment,
                )
                if ownership_path is not None:
                    register_execution_group(
                        ownership_path,
                        child_environment[OWNERSHIP_INVOCATION_ENV],
                        process.pid,
                    )
            except (OSError, ValueError, TimeoutError) as error:
                record.update(
                    state="error",
                    updated_at=utc_timestamp(),
                    elapsed_seconds=round(time.monotonic() - start, 3),
                    exit_code=RUNNER_ERROR_EXIT,
                    termination=(
                        "launch_error"
                        if process is None
                        else "execution_ownership_error"
                    ),
                )
                safe_emit(
                    f"[logged-gate] error label={args.label} "
                    f"time={utc_timestamp()}: {error}\n"
                )
                safe_status()
                return RUNNER_ERROR_EXIT

            try:
                record.update(
                    state="running", pid=process.pid, updated_at=utc_timestamp()
                )
                publish_status(args, record)
                selector = selectors.DefaultSelector()
                selector.register(process.stdout, selectors.EVENT_READ, "output")
                selector.register(wake_read, selectors.EVENT_READ, "signal")
                output_open = True
                last_output_time = start
                output_tail = ""
                next_heartbeat = start + args.heartbeat_seconds
                hard_deadline = (
                    start + args.timeout_seconds if args.timeout_seconds else None
                )
                idle_deadline = (
                    last_output_time + args.idle_timeout_seconds
                    if args.idle_timeout_seconds
                    else None
                )
                termination = None

                def pump(wait_seconds):
                    nonlocal idle_deadline, last_output_time, output_open, output_tail
                    received_output = False
                    for key, _mask in selector.select(max(0, wait_seconds)):
                        if key.data == "signal":
                            while True:
                                try:
                                    if not os.read(wake_read, READ_SIZE):
                                        break
                                except BlockingIOError:
                                    break
                            continue
                        chunk = os.read(key.fileobj.fileno(), READ_SIZE)
                        if not chunk:
                            selector.unregister(key.fileobj)
                            output_open = False
                            continue
                        received_output = True
                        emit(chunk)
                        record["output_bytes"] += len(chunk)
                        output_tail, record["last_output"] = output_state(
                            output_tail, chunk
                        )
                        last_output_time = time.monotonic()
                        idle_deadline = (
                            last_output_time + args.idle_timeout_seconds
                            if args.idle_timeout_seconds
                            else None
                        )
                    return received_output

                pump_callback = pump

                while True:
                    now = time.monotonic()
                    if signal_received is not None:
                        termination = f"signal_{signal_received}"
                        break
                    if hard_deadline is not None and now >= hard_deadline:
                        termination = "hard_timeout"
                        break
                    if idle_deadline is not None and now >= idle_deadline:
                        termination = "idle_timeout"
                        break

                    deadlines = [next_heartbeat]
                    if hard_deadline is not None:
                        deadlines.append(hard_deadline)
                    if idle_deadline is not None:
                        deadlines.append(idle_deadline)
                    received = pump(max(0, min(deadlines) - now))

                    now = time.monotonic()
                    if now >= next_heartbeat:
                        elapsed = now - start
                        quiet = now - last_output_time
                        emit(
                            f"[logged-gate] heartbeat label={args.label} "
                            f"time={utc_timestamp()} "
                            f"elapsed={elapsed:.1f}s quiet={quiet:.1f}s "
                            f"bytes={record['output_bytes']} pid={process.pid}\n"
                        )
                        record.update(
                            updated_at=utc_timestamp(),
                            elapsed_seconds=round(elapsed, 3),
                            quiet_seconds=round(quiet, 3),
                        )
                        publish_status(args, record)
                        next_heartbeat = now + args.heartbeat_seconds

                    if process.poll() is not None and not received:
                        pump(0)
                        if not output_open:
                            break

                if termination is not None:
                    emit(
                        f"[logged-gate] stopping label={args.label} "
                        f"time={utc_timestamp()} reason={termination}\n"
                    )
                    emit(process_tree_snapshot(process.pid))
                    stop_result = stop_process_group(
                        process,
                        args.terminate_grace_seconds,
                        pump=pump,
                        output_open=lambda: output_open,
                    )
                    child_exit = stop_result.child_exit_code
                    if not stop_result.group_stopped:
                        exit_code = RUNNER_ERROR_EXIT
                        state = "error"
                        termination = "cleanup_incomplete"
                    else:
                        exit_code = (
                            128 + signal_received
                            if signal_received is not None
                            else TIMEOUT_EXIT
                        )
                        state = (
                            "interrupted"
                            if signal_received is not None
                            else "timed_out"
                        )
                else:
                    child_exit = process.returncode
                    if child_exit is None:
                        child_exit = process.wait(
                            timeout=max(0.1, args.terminate_grace_seconds)
                        )
                    stop_result = None
                    if group_exists(process.pid):
                        stop_result = stop_process_group(
                            process,
                            args.terminate_grace_seconds,
                            pump=pump,
                            output_open=lambda: output_open,
                        )
                    if stop_result is not None and not stop_result.group_stopped:
                        exit_code = RUNNER_ERROR_EXIT
                        state = "error"
                        termination = "cleanup_incomplete"
                    else:
                        exit_code = child_exit if child_exit >= 0 else 128 - child_exit
                        state = "completed" if exit_code == 0 else "failed"

                if (
                    ownership_path is not None
                    and process is not None
                    and not group_exists(process.pid)
                ):
                    unregister_execution_group(
                        ownership_path,
                        record["sequence_invocation_id"]
                        or record["invocation_id"],
                        process.pid,
                    )
                ownership_groups = []
                ownership_survivors = []
                if ownership_path is not None:
                    ownership_record = read_execution_ownership(ownership_path)
                    if ownership_record is not None:
                        ownership_groups = ownership_record["process_groups"]
                        ownership_survivors = [
                            group for group in ownership_groups
                            if group_exists(group)
                        ]
                if ownership_survivors:
                    exit_code = RUNNER_ERROR_EXIT
                    state = "error"
                    termination = "cleanup_incomplete"

                elapsed = time.monotonic() - start
                quiet = time.monotonic() - last_output_time
                record.update(
                    state=state,
                    updated_at=utc_timestamp(),
                    elapsed_seconds=round(elapsed, 3),
                    quiet_seconds=round(quiet, 3),
                    child_exit_code=child_exit,
                    exit_code=exit_code,
                    termination=termination,
                )
                if ownership_path is not None and (
                    owns_ownership or termination == "cleanup_incomplete"
                ):
                    finish_execution_ownership(
                        ownership_path,
                        record["sequence_invocation_id"] or record["invocation_id"],
                        force_incomplete=(termination == "cleanup_incomplete"),
                    )
                    ownership_finished = owns_ownership
                if termination == "cleanup_incomplete":
                    try:
                        publish_execution_quarantine(
                            args,
                            record,
                            process_groups=(ownership_groups or [record["pid"]]),
                        )
                    except OSError as quarantine_error:
                        safe_emit(
                            f"[logged-gate] quarantine-write-error "
                            f"label={args.label} time={utc_timestamp()}: "
                            f"{quarantine_error}\n"
                        )
                emit(
                    f"[logged-gate] {state} label={args.label} exit={exit_code} "
                    f"time={utc_timestamp()} elapsed={elapsed:.1f}s "
                    f"bytes={record['output_bytes']}\n"
                )
                publish_status(args, record)
                return exit_code
            except Exception as error:
                cleanup_incomplete = False
                if group_exists(process.pid):
                    stop_result = stop_process_group(
                        process,
                        args.terminate_grace_seconds,
                        pump=pump_callback,
                        output_open=(lambda: output_open) if selector is not None else None,
                    )
                    child_exit = stop_result.child_exit_code
                    cleanup_incomplete = not stop_result.group_stopped
                else:
                    child_exit = process.poll()
                ownership_groups = []
                if ownership_path is not None:
                    ownership_record = read_execution_ownership(ownership_path)
                    if ownership_record is not None:
                        ownership_groups = ownership_record["process_groups"]
                        cleanup_incomplete = cleanup_incomplete or any(
                            group_exists(group) for group in ownership_groups
                        )
                record.update(
                    state="error",
                    updated_at=utc_timestamp(),
                    elapsed_seconds=round(time.monotonic() - start, 3),
                    child_exit_code=child_exit,
                    exit_code=RUNNER_ERROR_EXIT,
                    termination=(
                        "cleanup_incomplete" if cleanup_incomplete else "runner_error"
                    ),
                )
                if ownership_path is not None and (
                    owns_ownership or cleanup_incomplete
                ):
                    finish_execution_ownership(
                        ownership_path,
                        record["sequence_invocation_id"] or record["invocation_id"],
                        force_incomplete=cleanup_incomplete,
                    )
                    ownership_finished = owns_ownership
                if cleanup_incomplete:
                    try:
                        publish_execution_quarantine(
                            args,
                            record,
                            process_groups=(ownership_groups or [record["pid"]]),
                        )
                    except OSError as quarantine_error:
                        safe_emit(
                            f"[logged-gate] quarantine-write-error "
                            f"label={args.label} time={utc_timestamp()}: "
                            f"{quarantine_error}\n"
                        )
                safe_emit(
                    f"[logged-gate] error label={args.label} "
                    f"time={utc_timestamp()}: {error}\n"
                )
                safe_status()
                return RUNNER_ERROR_EXIT
    finally:
        if process is not None and group_exists(process.pid):
            stop_process_group(process, args.terminate_grace_seconds)
        if owns_ownership and ownership_started and not ownership_finished:
            try:
                finish_execution_ownership(
                    ownership_path,
                    record["invocation_id"],
                    force_incomplete=(
                        record.get("termination") == "cleanup_incomplete"
                        or (
                            process is not None
                            and group_exists(process.pid)
                        )
                    ),
                )
            except (OSError, ValueError, TimeoutError):
                pass
        if selector is not None:
            selector.close()
        if process is not None and process.stdout is not None:
            process.stdout.close()
        if previous_wakeup is not None:
            signal.set_wakeup_fd(previous_wakeup)
        for descriptor in (wake_read, wake_write):
            if descriptor is not None:
                os.close(descriptor)
        for number, handler in previous_handlers.items():
            signal.signal(number, handler)
        terminal_mirror.close()
        os.close(log_lock)
        if execution_lock is not None:
            os.close(execution_lock)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--status", type=Path)
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument("--execution-lock", type=Path)
    parser.add_argument("--sequence-invocation-id")
    parser.add_argument(
        "--heartbeat-seconds",
        type=lambda value: positive_seconds(value, "heartbeat"),
        default=30.0,
    )
    parser.add_argument(
        "--idle-timeout-seconds",
        type=lambda value: positive_seconds(value, "idle timeout", allow_zero=True),
        default=600.0,
    )
    parser.add_argument(
        "--timeout-seconds",
        type=lambda value: positive_seconds(value, "hard timeout", allow_zero=True),
        default=3600.0,
    )
    parser.add_argument(
        "--terminate-grace-seconds",
        type=lambda value: positive_seconds(value, "termination grace"),
        default=1.0,
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", args.label):
        parser.error("label must contain only letters, digits, dot, underscore or dash")
    explicit_paths = args.log is not None or args.status is not None
    if args.evidence_root is not None and explicit_paths:
        parser.error("--evidence-root cannot be combined with --log or --status")
    if args.evidence_root is None and (args.log is None or args.status is None):
        parser.error("either --evidence-root or both --log and --status are required")
    return args


def main(argv=None):
    try:
        return run(parse_arguments(argv))
    except OSError as error:
        print(f"logged-gate: evidence setup failed: {error}", file=sys.stderr)
        return RUNNER_ERROR_EXIT
    except ValueError as error:
        print(f"logged-gate: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
