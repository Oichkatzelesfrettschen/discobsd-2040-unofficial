"""Produce a path-safe, deterministic derivative of the pinned hosted report."""

import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path, PurePosixPath

REPOSITORY = "discobsd-2040-unofficial"
HOSTED_ROOT = f"/home/runner/work/{REPOSITORY}/{REPOSITORY}"
REPORT_TEMP = "/home/runner/work/_temp/ilp32-execution.json"
EXECUTION_TEMP = re.compile(r"/tmp/discobsd-execution-[A-Za-z0-9_-]+\Z")
GETLINE_TEMP = re.compile(r"/tmp/tmp\.[A-Za-z0-9]+/getline_test32\Z")
TRANSFORMATION_VERSION = 1
RECEIPT_FIELDS = {"capability", "cwd", "executable", "expected_exit",
                  "invocation", "outcome", "owner", "reason", "returncode",
                  "variant", "width"}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate report key: {key}")
        result[key] = value
    return result


def canonical_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("ascii")


def repository_path(value):
    if value == HOSTED_ROOT:
        return "."
    prefix = HOSTED_ROOT + "/"
    if not value.startswith(prefix):
        raise ValueError(f"unexpected hosted path: {value}")
    relative = value[len(prefix):]
    if (not relative or str(PurePosixPath(relative)) != relative or
            ".." in PurePosixPath(relative).parts):
        raise ValueError(f"noncanonical hosted path: {value}")
    return relative


def reject_absolute_strings(value):
    if isinstance(value, dict):
        for member in value.values():
            reject_absolute_strings(member)
    elif isinstance(value, list):
        for member in value:
            reject_absolute_strings(member)
    elif isinstance(value, str) and re.search(r"(?<![A-Za-z0-9._<>/-])/", value):
        raise ValueError("absolute path survived report transformation")


def sanitize_report(original, expected_hash):
    if hashlib.sha256(original).hexdigest() != expected_hash:
        raise ValueError("original hosted report SHA-256 mismatch")
    report = json.loads(original, object_pairs_hook=unique_object)
    if set(report) != {"inventory_sha256", "invocation", "returncode",
                       "revision", "tracked_diff_sha256",
                       "tracked_files_modified", "variants"}:
        raise ValueError("unexpected hosted report fields")
    if not isinstance(report["variants"], list):
        raise ValueError("hosted variants must be a list")
    invocation = report["invocation"]
    if invocation.count(f"TEST_EXECUTION_REPORT={REPORT_TEMP}") != 1:
        raise ValueError("unexpected hosted report output path")
    execution_directories = [entry.split("=", 1)[1] for entry in invocation
                             if entry.startswith("TEST_EXECUTION_DIR=")]
    if (len(execution_directories) != 1 or
            not EXECUTION_TEMP.fullmatch(execution_directories[0])):
        raise ValueError("unexpected hosted execution directory")
    report["invocation"] = [
        "TEST_EXECUTION_REPORT=<ci-temp>/ilp32-execution.json"
        if entry == f"TEST_EXECUTION_REPORT={REPORT_TEMP}" else
        "TEST_EXECUTION_DIR=<execution-temp>"
        if entry == f"TEST_EXECUTION_DIR={execution_directories[0]}" else entry
        for entry in invocation
    ]
    seen = set()
    for receipt in report["variants"]:
        if set(receipt) != RECEIPT_FIELDS or receipt["reason"] is not None:
            raise ValueError("unexpected hosted receipt fields or reason")
        identifier = receipt["variant"]
        if identifier in seen:
            raise ValueError(f"duplicate hosted variant: {identifier}")
        seen.add(identifier)
        receipt["cwd"] = repository_path(receipt["cwd"])
        executable = receipt["executable"]["path"]
        if identifier == "textbox.getline.ilp32":
            if not GETLINE_TEMP.fullmatch(executable):
                raise ValueError("unexpected getline temporary executable")
            if receipt["invocation"] != [executable]:
                raise ValueError("getline invocation differs from executable")
            receipt["executable"]["path"] = "<test-temp>/getline_test32"
            receipt["invocation"] = ["<test-temp>/getline_test32"]
        else:
            receipt["executable"]["path"] = repository_path(executable)
            if identifier == "dd.filesystem.ilp32":
                expected = ["sh", HOSTED_ROOT + "/tests/dd_contracts/filesystem_test.sh",
                            HOSTED_ROOT + "/tests/dd_contracts/dd_host"]
                if receipt["invocation"] != expected:
                    raise ValueError("unexpected dd filesystem harness")
                receipt["invocation"] = ["sh", "tests/dd_contracts/filesystem_test.sh",
                                         "tests/dd_contracts/dd_host"]
            elif identifier == "shell.posix.ilp32":
                if receipt["invocation"] != ["sh", "bin/sh/tests/posix-sh.sh"]:
                    raise ValueError("unexpected POSIX shell harness")
            elif (len(receipt["invocation"]) != 1 or
                  receipt["invocation"][0] != "./" + PurePosixPath(executable).name):
                raise ValueError(f"unexpected direct invocation: {identifier}")
    report["sanitization"] = {"version": TRANSFORMATION_VERSION,
                              "original_report_sha256": expected_hash}
    reject_absolute_strings(report)
    serialized = canonical_bytes(report)
    return serialized


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--original-sha256", required=True)
    arguments = parser.parse_args()
    original = arguments.source.read_bytes()
    if arguments.source.suffix == ".gz":
        original = gzip.decompress(original)
    derivative = sanitize_report(original, arguments.original_sha256)
    with arguments.output.open("xb") as destination:
        destination.write(derivative)
    print(f"sanitized report sha256={hashlib.sha256(derivative).hexdigest()}")


if __name__ == "__main__":
    main()
