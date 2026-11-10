"""Compare two version-1 DiscoBSD machdep.storage_stats snapshots."""

import argparse
import json
import re
import sys
from pathlib import Path

METRICS = (
    "root_reads", "root_read_bytes", "root_writes", "root_write_bytes",
    "swap_reads", "swap_read_bytes", "swap_writes", "swap_write_bytes",
    "map_writes", "checkpoint_attempts", "checkpoint_successes", "checkpoint_errors",
    "root_program_pages", "root_program_bytes", "root_erase_sectors", "root_erase_bytes",
    "swap_program_pages", "swap_program_bytes", "swap_erase_sectors", "swap_erase_bytes",
    "dirty_transitions", "dirty_rewrites", "eviction_writes", "buffer_waits",
    "swapram_attempts", "swapram_admissions",
)
UINT32_MAX = 0xFFFFFFFF
HEADER = re.compile(r"machdep\.storage_stats=version ([0-9]+) saturated ([0-9]+) "
                    r"dirty ([0-9]+) peak ([0-9]+)")
METRIC = re.compile(r"([a-z_]+) ([0-9]+)")


def parse_snapshot(text):
    lines = text.splitlines()
    if len(lines) != len(METRICS) + 1:
        raise ValueError("snapshot must contain one header and every metric")
    header = HEADER.fullmatch(lines[0])
    if header is None:
        raise ValueError("invalid snapshot header")
    version, saturated, dirty, peak = (int(value) for value in header.groups())
    if version != 1 or saturated != 0 or dirty > peak or peak > UINT32_MAX:
        raise ValueError("unsupported, saturated, or inconsistent snapshot")
    values = {}
    for line in lines[1:]:
        match = METRIC.fullmatch(line)
        if match is None:
            raise ValueError("invalid metric line")
        name, decimal = match.groups()
        value = int(decimal)
        if name not in METRICS or name in values or value > UINT32_MAX:
            raise ValueError("unknown, duplicate, or out-of-range metric")
        values[name] = value
    if set(values) != set(METRICS):
        raise ValueError("snapshot metric set differs from ABI version 1")
    return {"dirty": dirty, "peak": peak, "values": values}


def summarize(before_text, after_text, before_boot, after_boot, known_work):
    if (not isinstance(before_boot, str) or not before_boot.strip() or
            before_boot != after_boot):
        raise ValueError("matching nonempty boot identifiers are required")
    before = parse_snapshot(before_text)
    after = parse_snapshot(after_text)
    if after["peak"] < before["peak"]:
        raise ValueError("dirty peak decreased")
    deltas = {}
    for name in METRICS:
        if after["values"][name] < before["values"][name]:
            raise ValueError(f"counter decreased: {name}")
        deltas[name] = after["values"][name] - before["values"][name]
    for name in known_work:
        if name not in deltas or deltas[name] == 0:
            raise ValueError(f"known-work counter did not increase: {name}")
    return {"schema": 1, "boot_id_declared": before_boot,
            "counter_deltas": deltas, "dirty_current": [before["dirty"], after["dirty"]],
            "dirty_peak": [before["peak"], after["peak"]],
            "scope": "global snapshot deltas; flash program and erase counters record "
                     "issued traffic, not independent media verification",
            "limitations": "boot identity and workload isolation are caller assertions; "
                           "dirty_peak is a boot-lifetime high-water mark"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--before-boot", required=True)
    parser.add_argument("--after-boot", required=True)
    parser.add_argument("--known-work", action="append", default=[])
    args = parser.parse_args()
    try:
        result = summarize(args.before.read_text(encoding="utf-8"),
                           args.after.read_text(encoding="utf-8"),
                           args.before_boot, args.after_boot, args.known_work)
    except (OSError, ValueError) as error:
        print(f"storage snapshot: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
