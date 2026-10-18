#!/usr/bin/env python3
"""Enforce a bounded RP2040 stack frame for recovery path resolution."""

import argparse
from pathlib import Path


def read_stack_usage(path):
    records = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        fields = line.rsplit("\t", 2)
        if len(fields) != 3:
            raise ValueError(f"{path}:{line_number}: malformed stack-usage row")
        location, byte_text, classification = fields
        function = location.rsplit(":", 1)[-1]
        try:
            byte_count = int(byte_text)
        except ValueError as error:
            raise ValueError(
                f"{path}:{line_number}: invalid stack byte count {byte_text!r}"
            ) from error
        records.append((function, byte_count, classification))
    if not records:
        raise ValueError(f"{path}: stack-usage report is empty")
    return records


def describe_largest(records):
    function, byte_count, classification = max(records, key=lambda row: row[1])
    return function, byte_count, classification


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--maximum-bytes", type=int, required=True)
    parser.add_argument("--production", type=Path, required=True)
    parser.add_argument("--known-bad", type=Path, required=True)
    arguments = parser.parse_args()

    if arguments.maximum_bytes <= 0:
        parser.error("--maximum-bytes must be positive")

    known_bad = describe_largest(read_stack_usage(arguments.known_bad))
    if known_bad[1] <= arguments.maximum_bytes:
        raise SystemExit(
            "realpath stack calibration failed: known-bad maximum "
            f"{known_bad[0]}={known_bad[1]} does not exceed "
            f"{arguments.maximum_bytes} bytes"
        )

    production_records = read_stack_usage(arguments.production)
    non_static = [
        (function, classification)
        for function, _byte_count, classification in production_records
        if classification != "static"
    ]
    if non_static:
        function, classification = non_static[0]
        raise SystemExit(
            "realpath stack limit exceeded: non-static production frame "
            f"{function} is classified {classification}"
        )

    production = describe_largest(production_records)
    if production[1] > arguments.maximum_bytes:
        raise SystemExit(
            "realpath stack limit exceeded: production maximum "
            f"{production[0]}={production[1]} exceeds "
            f"{arguments.maximum_bytes} bytes"
        )

    print(
        "realpath stack usage: production maximum "
        f"{production[0]}={production[1]} bytes; calibrated known-bad "
        f"{known_bad[0]}={known_bad[1]} bytes"
    )


if __name__ == "__main__":
    main()
