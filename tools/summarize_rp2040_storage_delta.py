"""Compare two GizmoBSD machdep.storage_stats snapshots of one ABI version."""

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
    "swapram_refused_closed", "swapram_refused_codec", "swapram_refused_encode",
    "swapram_refused_nospace", "swapram_refused_fragment", "swapram_refused_want_bytes",
    "swapram_refused_free_bytes", "swapram_refused_largest_bytes",
    "swapram_refused_data_fits", "swapram_short_1k", "swapram_short_2k",
    "swapram_short_3k", "swapram_short_4k", "swapram_short_5k", "swapram_short_6k",
    "swapram_short_7k", "swapram_short_more", "swapram_admitted_data_bytes",
    "swapram_admitted_u_bytes", "swapram_flash_swapouts", "swapram_exchange_swapouts",
    "swapram_sched_refusals", "swapram_admit_scan_bytes", "swapram_admit_zero_bytes",
    "swapram_refuse_scan_bytes", "swapram_refuse_zero_bytes",
    "swapram_refuse_data_chunks", "swapram_refuse_zero_chunks", "swapram_ram_span_01",
    "swapram_ram_span_02", "swapram_ram_span_03", "swapram_ram_span_04",
    "swapram_ram_span_05", "swapram_ram_span_06", "swapram_ram_span_07",
    "swapram_ram_span_08", "swapram_ram_span_09", "swapram_ram_span_10",
    "swapram_ram_span_11", "swapram_ram_span_12", "swapram_flash_span_01",
    "swapram_flash_span_02", "swapram_flash_span_03", "swapram_flash_span_04",
    "swapram_flash_span_05", "swapram_flash_span_06", "swapram_flash_span_07",
    "swapram_flash_span_08", "swapram_flash_span_09", "swapram_flash_span_10",
    "swapram_flash_span_11", "swapram_flash_span_12",
)
# Version 1 carried the first 26 metrics (through swapram_admissions); version
# 2 appends the admission probe counters. METRICS is the version 2 order and
# matches enum storage_metric.
METRICS_V1 = METRICS[:METRICS.index("swapram_admissions") + 1]
METRICS_BY_VERSION = {1: METRICS_V1, 2: METRICS}
UINT32_MAX = 0xFFFFFFFF
HEADER = re.compile(r"machdep\.storage_stats=version ([0-9]+) saturated ([0-9]+) "
                    r"dirty ([0-9]+) peak ([0-9]+)")
METRIC = re.compile(r"([a-z0-9_]+) ([0-9]+)")


def parse_snapshot(text):
    lines = text.splitlines()
    header = HEADER.fullmatch(lines[0]) if lines else None
    if header is None:
        raise ValueError("invalid snapshot header")
    version, saturated, dirty, peak = (int(value) for value in header.groups())
    if (version not in METRICS_BY_VERSION or saturated != 0 or dirty > peak or
            peak > UINT32_MAX):
        raise ValueError("unsupported, saturated, or inconsistent snapshot")
    metrics = METRICS_BY_VERSION[version]
    if len(lines) != len(metrics) + 1:
        raise ValueError("snapshot must contain one header and every metric")
    values = {}
    for line in lines[1:]:
        match = METRIC.fullmatch(line)
        if match is None:
            raise ValueError("invalid metric line")
        name, decimal = match.groups()
        value = int(decimal)
        if name not in metrics or name in values or value > UINT32_MAX:
            raise ValueError("unknown, duplicate, or out-of-range metric")
        values[name] = value
    if set(values) != set(metrics):
        raise ValueError(f"snapshot metric set differs from ABI version {version}")
    return {"version": version, "dirty": dirty, "peak": peak, "values": values}


def summarize(before_text, after_text, before_boot, after_boot, known_work):
    if (not isinstance(before_boot, str) or not before_boot.strip() or
            before_boot != after_boot):
        raise ValueError("matching nonempty boot identifiers are required")
    before = parse_snapshot(before_text)
    after = parse_snapshot(after_text)
    if before["version"] != after["version"]:
        raise ValueError("snapshots use different ABI versions")
    if after["peak"] < before["peak"]:
        raise ValueError("dirty peak decreased")
    deltas = {}
    for name in METRICS_BY_VERSION[before["version"]]:
        if after["values"][name] < before["values"][name]:
            raise ValueError(f"counter decreased: {name}")
        deltas[name] = after["values"][name] - before["values"][name]
    for name in known_work:
        if name not in deltas or deltas[name] == 0:
            raise ValueError(f"known-work counter did not increase: {name}")
    return {"schema": before["version"], "boot_id_declared": before_boot,
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
