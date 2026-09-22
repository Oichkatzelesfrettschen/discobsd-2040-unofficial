"""Record actual test invocations and require every designated CI variant."""

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INVENTORY = ROOT / "tools/test-execution-inventory.json"


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


def receipt(variant, outcome, command=None, returncode=None, reason=None, executable=None):
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


def run_variant(variant, command):
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
        process = subprocess.run(command, check=False)
        # The shell harness builds its subject before executing its cases.
        if "subject" in variant:
            evidence = executable_evidence(ROOT / variant["subject"], variant["width"])
    except (OSError, ValueError) as error:
        receipt(variant, "ERROR", command, reason=str(error), executable=evidence)
        return 2
    outcome = "PASS" if process.returncode == variant.get("expected_exit", 0) else "FAIL"
    if process.returncode in {126, 127} or (expected == "sh" and process.returncode == 2):
        outcome = "ERROR"
    reason = None if outcome == "PASS" else (
        f"expected status {variant.get('expected_exit', 0)}, observed {process.returncode}"
    )
    receipt(variant, outcome, command, process.returncode, reason=reason, executable=evidence)
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


def aggregate(make, report):
    variants = inventory()
    environment = os.environ.copy()
    # Python closes inherited jobserver descriptors; the detached child owns
    # its own scheduling. Ordinary recursive make recipes keep their jobserver.
    environment.pop("MAKEFLAGS", None)
    environment.pop("MFLAGS", None)
    environment["REQUIRE_ILP32"] = "yes"
    environment["TEST_EXECUTION_REQUIRED"] = "yes"
    command = [make, "MACHINE=rp2040", "check-ilp32-execution-recipes"]
    with tempfile.TemporaryDirectory(prefix="discobsd-execution-") as temporary:
        environment["TEST_EXECUTION_DIR"] = temporary
        try:
            status = subprocess.run(command, cwd=ROOT, env=environment, check=False).returncode
        except OSError as error:
            print(f"ERROR aggregate: {error}", file=sys.stderr)
            status = 2
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
    run.add_argument("variant")
    run.add_argument("command", nargs=argparse.REMAINDER)
    skip = subparsers.add_parser("skip")
    skip.add_argument("variant")
    skip.add_argument("reason")
    suite = subparsers.add_parser("aggregate")
    suite.add_argument("--make", default="bmake")
    suite.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        if args.operation == "aggregate":
            return aggregate(args.make, args.report)
        variant = inventory()[args.variant]
        required(variant)
        if args.operation == "skip":
            return skip_variant(variant, args.reason)
        command = args.command[1:] if args.command[:1] == ["--"] else args.command
        return run_variant(variant, command)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ERROR test-execution: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
