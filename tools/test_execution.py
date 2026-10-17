"""Record actual test invocations and require every designated CI variant."""

import argparse
import dataclasses
import hashlib
import json
import math
import os
import re
import shlex
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import run_logged_gate as execution_ownership

ROOT = Path(__file__).resolve().parent.parent
INVENTORY = ROOT / "tools/test-execution-inventory.json"
TIMEOUT_EXIT = 124
VARIANT_TIMEOUT_SECONDS = 600.0
AGGREGATE_TIMEOUT_SECONDS = 3600.0
TERMINATE_GRACE_SECONDS = 1.0
CLEANUP_ALLOWANCE_ENV = "_DISCOBSD_TEST_EXECUTION_CLEANUP_ALLOWANCE_SECONDS"
ROOT_CLEANUP_ALLOWANCE_SECONDS = 2.0 * TERMINATE_GRACE_SECONDS
MINIMUM_CHILD_CLEANUP_SECONDS = 0.02


@dataclasses.dataclass(frozen=True)
class StopResult:
    child_exit_code: object
    group_stopped: bool


def positive_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("deadline must be positive")
    return seconds


def process_group_exists(process_group):
    try:
        os.killpg(process_group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def stop_process_group(process, grace_seconds=TERMINATE_GRACE_SECONDS):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline and process_group_exists(process.pid):
        process.poll()
        time.sleep(min(0.02, max(0, deadline - time.monotonic())))
    if process_group_exists(process.pid):
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
    while time.monotonic() < group_deadline and process_group_exists(process.pid):
        time.sleep(min(0.02, max(0, group_deadline - time.monotonic())))
    return StopResult(child_exit, not process_group_exists(process.pid))


def cleanup_allowances():
    value = os.environ.get(
        CLEANUP_ALLOWANCE_ENV, str(ROOT_CLEANUP_ALLOWANCE_SECONDS)
    )
    allowance = float(value)
    if not math.isfinite(allowance) or allowance <= 0:
        raise ValueError(f"invalid inherited cleanup allowance: {value}")
    child_allowance = max(MINIMUM_CHILD_CLEANUP_SECONDS, allowance / 2.0)
    return allowance, child_allowance


def run_bounded(command, timeout_seconds, *, cwd=None, env=None):
    previous_handlers = {}
    cleanup_seconds, child_cleanup_seconds = cleanup_allowances()
    child_environment = os.environ.copy() if env is None else env.copy()
    child_environment[CLEANUP_ALLOWANCE_ENV] = str(child_cleanup_seconds)
    signal_received = None

    def interrupt(number, _frame):
        nonlocal signal_received
        signal_received = number

    for number in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[number] = signal.signal(number, interrupt)

    def release_registered_group(process_group):
        try:
            execution_ownership.unregister_inherited_execution_group(
                process_group
            )
        except (OSError, ValueError, TimeoutError) as error:
            print(
                f"test-execution: ownership release failed: {error}",
                file=sys.stderr,
                flush=True,
            )
            return False
        return True

    def retain_incomplete_group(process_group):
        try:
            execution_ownership.mark_inherited_execution_group_cleanup_incomplete(
                process_group
            )
        except (OSError, ValueError, TimeoutError) as error:
            print(
                f"test-execution: incomplete ownership retention failed: {error}",
                file=sys.stderr,
                flush=True,
            )
            return False
        return True

    def cancel_launch_reservation(reservation):
        try:
            execution_ownership.cancel_inherited_execution_launch(reservation)
        except (OSError, ValueError, TimeoutError) as error:
            print(
                f"test-execution: launch reservation release failed: {error}",
                file=sys.stderr,
                flush=True,
            )
            return False
        return True

    try:
        try:
            reservation = execution_ownership.reserve_inherited_execution_launch()
        except (OSError, ValueError, TimeoutError) as error:
            print(
                f"test-execution: ownership reservation failed: {error}",
                file=sys.stderr,
                flush=True,
            )
            return 125, "cleanup_incomplete"
        if signal_received is not None:
            if not cancel_launch_reservation(reservation):
                return 125, "cleanup_incomplete"
            return 128 + signal_received, f"signal_{signal_received}"
        try:
            process = subprocess.Popen(
                command, cwd=cwd, env=child_environment, start_new_session=True
            )
        except BaseException:
            cancel_launch_reservation(reservation)
            raise
        try:
            execution_ownership.complete_inherited_execution_launch(
                reservation, process.pid
            )
        except (OSError, ValueError, TimeoutError) as error:
            print(
                f"test-execution: ownership registration failed: {error}",
                file=sys.stderr,
                flush=True,
            )
            result = stop_process_group(process, cleanup_seconds)
            if result.group_stopped:
                cancel_launch_reservation(reservation)
            else:
                retain_incomplete_group(process.pid)
            return 125, "cleanup_incomplete"
        if signal_received is not None:
            result = stop_process_group(process, cleanup_seconds)
            if result.group_stopped and not release_registered_group(process.pid):
                return 125, "cleanup_incomplete"
            if not result.group_stopped:
                retain_incomplete_group(process.pid)
            return (
                (128 + signal_received, f"signal_{signal_received}")
                if result.group_stopped
                else (125, "cleanup_incomplete")
            )
        deadline = time.monotonic() + timeout_seconds
        while True:
            if signal_received is not None:
                result = stop_process_group(process, cleanup_seconds)
                if result.group_stopped and not release_registered_group(process.pid):
                    return 125, "cleanup_incomplete"
                if not result.group_stopped:
                    retain_incomplete_group(process.pid)
                return (
                    (128 + signal_received, f"signal_{signal_received}")
                    if result.group_stopped
                    else (125, "cleanup_incomplete")
                )
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                result = stop_process_group(process, cleanup_seconds)
                if result.group_stopped and not release_registered_group(process.pid):
                    return 125, "cleanup_incomplete"
                if not result.group_stopped:
                    retain_incomplete_group(process.pid)
                return (
                    (TIMEOUT_EXIT, "hard_timeout")
                    if result.group_stopped
                    else (125, "cleanup_incomplete")
                )
            try:
                returncode = process.wait(timeout=min(0.1, remaining))
                break
            except subprocess.TimeoutExpired:
                continue
            except BaseException:
                stop_process_group(process, cleanup_seconds)
                raise
        if process_group_exists(process.pid):
            result = stop_process_group(process, cleanup_seconds)
            if not result.group_stopped:
                retain_incomplete_group(process.pid)
                return 125, "cleanup_incomplete"
        if not release_registered_group(process.pid):
            return 125, "cleanup_incomplete"
        return returncode, None
    finally:
        for number, handler in previous_handlers.items():
            signal.signal(number, handler)


def inventory(path=INVENTORY):
    data = json.loads(path.read_text())
    variants = data["variants"]
    identifiers = [variant["id"] for variant in variants]
    if len(identifiers) != len(set(identifiers)):
        raise ValueError("duplicate variant identifiers")
    for variant in variants:
        if not re.fullmatch(r"[a-z0-9_.-]+", variant["id"]):
            raise ValueError("invalid variant identifier")
        if variant["width"] not in {"native", "ilp32"}:
            raise ValueError("invalid compiler width")
    return {variant["id"]: variant for variant in variants}


def required(variant):
    for name in ("TEST_EXECUTION_REQUIRED", "REQUIRE_ILP32"):
        if os.environ.get(name, "no") not in {"yes", "no"}:
            raise ValueError(f"{name} must be yes or no")
    return os.environ.get("TEST_EXECUTION_REQUIRED") == "yes" or (
        variant["width"] == "ilp32" and os.environ.get("REQUIRE_ILP32") == "yes"
    )


def receipt(variant, outcome, command=None, returncode=None, reason=None,
            executable=None, termination=None, timeout_seconds=None):
    record = {
        "variant": variant["id"],
        "owner": variant["owner"],
        "width": variant["width"],
        "capability": variant["capability"],
        "outcome": outcome,
        "cwd": str(Path.cwd()),
        "invocation": command,
        "returncode": returncode,
        "expected_exit": variant.get("expected_exit", 0),
        "reason": reason,
        "executable": executable,
        "termination": termination,
        "timeout_seconds": timeout_seconds,
    }
    destination = os.environ.get("TEST_EXECUTION_DIR")
    if destination:
        # The aggregate creates a fresh directory for every invocation. An
        # exclusive file prevents a repeated recipe from hiding its first result.
        output = Path(destination) / (variant["id"] + ".json")
        with output.open("x") as stream:
            json.dump(record, stream, sort_keys=True)
            stream.write("\n")
    detail = shlex.join(command) if command else reason
    print(f"{outcome} {variant['id']} owner={variant['owner']} "
          f"width={variant['width']}: {detail}", flush=True)
    return record


def executable_evidence(path, width):
    contents = path.read_bytes()
    if width == "ilp32" and contents[:5] != b"\x7fELF\x01":
        raise ValueError(f"{path}: required ILP32 ELF executable is absent")
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(contents).hexdigest()}


def run_variant(variant, command, timeout_seconds=VARIANT_TIMEOUT_SECONDS):
    if not command:
        raise ValueError("an executable invocation is required")
    expected = variant["program"]
    if Path(command[0]).name != expected:
        raise ValueError(f"{variant['id']}: expected executable {expected}, got {command[0]}")
    if expected == "sh" and (
        len(command) < 2 or Path(command[1]).resolve() != ROOT / variant["script"]
    ):
        raise ValueError(f"{variant['id']}: invocation names a different harness")
    if expected != "sh" and "script" not in variant:
        if Path(command[0]).resolve() != (ROOT / variant["directory"] / expected).resolve():
            raise ValueError(f"{variant['id']}: invocation names a different executable")
    evidence = None
    try:
        if expected != "sh":
            evidence = executable_evidence(Path(command[0]), variant["width"])
        returncode, termination = run_bounded(command, timeout_seconds)
        # The shell harness builds its subject before executing its cases.
        if "subject" in variant:
            evidence = executable_evidence(ROOT / variant["subject"], variant["width"])
    except (OSError, ValueError) as error:
        receipt(variant, "ERROR", command, reason=str(error), executable=evidence)
        return 2
    if termination is not None:
        outcome = "ERROR"
    else:
        outcome = "PASS" if returncode == variant.get("expected_exit", 0) else "FAIL"
    if returncode in {126, 127} or (expected == "sh" and returncode == 2):
        outcome = "ERROR"
    if outcome == "PASS":
        reason = None
    elif termination == "hard_timeout":
        reason = f"deadline exceeded after {timeout_seconds:g} seconds"
    elif termination is not None:
        reason = f"execution interrupted by {termination}"
    else:
        reason = (
            f"expected status {variant.get('expected_exit', 0)}, "
            f"observed {returncode}"
        )
    receipt(
        variant, outcome, command, returncode, reason=reason,
        executable=evidence, termination=termination,
        timeout_seconds=timeout_seconds,
    )
    if termination == "cleanup_incomplete":
        return 125
    return {"PASS": 0, "FAIL": 1, "ERROR": 2}[outcome]


def skip_variant(variant, reason):
    outcome = "ERROR" if required(variant) else "SKIP"
    receipt(variant, outcome, reason=reason)
    return 2 if outcome == "ERROR" else 0


def collect(variants, directory):
    records = []
    for variant in variants.values():
        path = directory / (variant["id"] + ".json")
        if path.exists():
            record = json.loads(path.read_text())
            if record["outcome"] == "PASS" and (
                not record["invocation"] or record["returncode"] != variant.get("expected_exit", 0)
            ):
                record["outcome"] = "ERROR"
                record["reason"] = "PASS lacks a completed expected invocation"
        else:
            record = {
                "variant": variant["id"], "owner": variant["owner"],
                "width": variant["width"], "outcome": "ERROR",
                "invocation": None, "returncode": None,
                "reason": "required executable recipe produced no receipt",
            }
        records.append(record)
    return records


def aggregate(make, report, make_variables=(),
              aggregate_timeout_seconds=AGGREGATE_TIMEOUT_SECONDS):
    variants = inventory()
    environment = os.environ.copy()
    # Python closes inherited jobserver descriptors; the detached child owns
    # its own scheduling. Ordinary recursive make recipes keep their jobserver.
    environment.pop("MAKEFLAGS", None)
    environment.pop("MFLAGS", None)
    environment["REQUIRE_ILP32"] = "yes"
    environment["TEST_EXECUTION_REQUIRED"] = "yes"
    for variable in make_variables:
        if not re.match(r"^[A-Za-z_.][A-Za-z0-9_.]*=", variable):
            raise ValueError(f"invalid make variable assignment: {variable}")
    command = [make, *make_variables, "MACHINE=rp2040",
               "REQUIRE_ILP32=yes", "TEST_EXECUTION_REQUIRED=yes",
               "check-ilp32-execution-recipes"]
    with tempfile.TemporaryDirectory(prefix="discobsd-execution-") as temporary:
        environment["TEST_EXECUTION_DIR"] = temporary
        command.append(f"TEST_EXECUTION_DIR={temporary}")
        try:
            status, termination = run_bounded(
                command, aggregate_timeout_seconds, cwd=ROOT, env=environment
            )
        except OSError as error:
            print(f"ERROR aggregate: {error}", file=sys.stderr)
            status = 2
            termination = "launch_error"
        records = collect(variants, Path(temporary))
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=True
    ).stdout.strip()
    tracked_diff = subprocess.run(
        ["git", "diff", "HEAD", "--"], cwd=ROOT, capture_output=True, check=True
    ).stdout
    payload = {
        "revision": revision, "invocation": command, "returncode": status,
        "tracked_diff_sha256": hashlib.sha256(tracked_diff).hexdigest(),
        "tracked_files_modified": bool(tracked_diff),
        "inventory_sha256": hashlib.sha256(INVENTORY.read_bytes()).hexdigest(),
        "termination": termination,
        "timeout_seconds": aggregate_timeout_seconds,
        "variants": records,
    }
    if report:
        report.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    failures = [record for record in records if record["outcome"] != "PASS"]
    for record in failures:
        print(f"{record['outcome']} {record['variant']}: {record.get('reason')}", file=sys.stderr)
    print(f"execution inventory: {len(records) - len(failures)}/{len(records)} PASS", flush=True)
    return 1 if failures or status else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    run = subparsers.add_parser("run")
    run.add_argument(
        "--timeout-seconds", type=positive_seconds,
        default=VARIANT_TIMEOUT_SECONDS,
    )
    run.add_argument("variant")
    run.add_argument("command", nargs=argparse.REMAINDER)
    skip = subparsers.add_parser("skip")
    skip.add_argument("variant")
    skip.add_argument("reason")
    suite = subparsers.add_parser("aggregate")
    suite.add_argument("--make", default="bmake")
    suite.add_argument("--report", type=Path)
    suite.add_argument("--make-variable", action="append", default=[])
    suite.add_argument(
        "--timeout-seconds", type=positive_seconds,
        default=AGGREGATE_TIMEOUT_SECONDS,
    )
    args = parser.parse_args()
    try:
        if args.operation == "aggregate":
            return aggregate(
                args.make, args.report, args.make_variable,
                args.timeout_seconds,
            )
        variant = inventory()[args.variant]
        required(variant)
        if args.operation == "skip":
            return skip_variant(variant, args.reason)
        command = args.command[1:] if args.command[:1] == ["--"] else args.command
        return run_variant(variant, command, args.timeout_seconds)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ERROR test-execution: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
