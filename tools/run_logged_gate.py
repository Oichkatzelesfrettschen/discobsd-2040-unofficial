"""Run one build or gate with streaming logs, heartbeats, and deadlines."""

import argparse
import datetime
import json
import math
import os
import queue
import selectors
import shlex
import signal
import stat
import subprocess
import sys
import threading
import time
from pathlib import Path

TIMEOUT_EXIT = 124
RUNNER_ERROR_EXIT = 125
READ_SIZE = 65536
TERMINAL_QUEUE_CHUNKS = 32
TERMINAL_CLOSE_SECONDS = 0.2


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
        child_exit = process.wait()
    if pump is not None and output_open is not None:
        drain_deadline = time.monotonic() + TERMINAL_CLOSE_SECONDS
        while output_open() and time.monotonic() < drain_deadline:
            pump(min(0.02, drain_deadline - time.monotonic()))
    return child_exit


def output_state(previous_tail, chunk):
    text = (previous_tail + chunk.decode("utf-8", errors="replace")).replace(
        "\r", "\n"
    )
    tail = text[-8192:]
    lines = tail.splitlines()
    return tail, lines[-1][-400:] if lines else ""


def run(args):
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        raise ValueError("a command is required after --")
    if os.name != "posix":
        raise ValueError("logged gate process-group control requires a POSIX host")

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
        "quiet_seconds": 0.0,
        "output_bytes": 0,
        "last_output": "",
        "child_exit_code": None,
        "exit_code": None,
        "termination": None,
        "terminal_queue_dropped_bytes": 0,
    }
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
            atomic_status(args.status, record)
        except OSError:
            pass
        print(f"logged-gate: {error}", file=sys.stderr)
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
                    atomic_status(args.status, record)
                except OSError:
                    pass

            emit(
                f"[logged-gate] start label={args.label} time={start_wall} "
                f"command={shlex.join(command)}\n"
            )
            try:
                process = subprocess.Popen(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                    bufsize=0,
                    pass_fds=make_jobserver_descriptors(),
                )
            except OSError as error:
                record.update(
                    state="error",
                    updated_at=utc_timestamp(),
                    elapsed_seconds=round(time.monotonic() - start, 3),
                    exit_code=RUNNER_ERROR_EXIT,
                    termination="launch_error",
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
                atomic_status(args.status, record)
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
                        atomic_status(args.status, record)
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
                    child_exit = stop_process_group(
                        process,
                        args.terminate_grace_seconds,
                        pump=pump,
                        output_open=lambda: output_open,
                    )
                    exit_code = (
                        128 + signal_received
                        if signal_received is not None
                        else TIMEOUT_EXIT
                    )
                    state = (
                        "interrupted" if signal_received is not None else "timed_out"
                    )
                else:
                    child_exit = process.wait()
                    exit_code = child_exit if child_exit >= 0 else 128 - child_exit
                    state = "completed" if exit_code == 0 else "failed"

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
                emit(
                    f"[logged-gate] {state} label={args.label} exit={exit_code} "
                    f"time={utc_timestamp()} elapsed={elapsed:.1f}s "
                    f"bytes={record['output_bytes']}\n"
                )
                atomic_status(args.status, record)
                return exit_code
            except Exception as error:
                if group_exists(process.pid):
                    child_exit = stop_process_group(
                        process,
                        args.terminate_grace_seconds,
                        pump=pump_callback,
                        output_open=(lambda: output_open) if selector is not None else None,
                    )
                else:
                    child_exit = process.wait()
                record.update(
                    state="error",
                    updated_at=utc_timestamp(),
                    elapsed_seconds=round(time.monotonic() - start, 3),
                    child_exit_code=child_exit,
                    exit_code=RUNNER_ERROR_EXIT,
                    termination="runner_error",
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


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--status", type=Path, required=True)
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
    return parser.parse_args(argv)


def main(argv=None):
    try:
        return run(parse_arguments(argv))
    except ValueError as error:
        print(f"logged-gate: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
