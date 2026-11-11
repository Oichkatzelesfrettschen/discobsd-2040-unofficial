"""Summarize source-identified paired packed/raw exec timing observations."""

import argparse
import json
import math
import re
import statistics
import sys
from pathlib import Path

# Two-sided 95% Student t quantiles for 1..30 degrees of freedom. For larger
# samples the 30-degree value gives a conservative interval.
T95 = (12.706205, 4.302653, 3.182446, 2.776445, 2.570582, 2.446912,
       2.364624, 2.306004, 2.262157, 2.228139, 2.200985, 2.178813,
       2.160369, 2.144787, 2.131450, 2.119905, 2.109816, 2.100922,
       2.093024, 2.085963, 2.079614, 2.073873, 2.068658, 2.063899,
       2.059539, 2.055529, 2.051831, 2.048407, 2.045230, 2.042272)
HASH_KEYS = ("kernel_sha256", "packed_root_sha256", "raw_root_sha256",
             "packed_executable_sha256", "raw_executable_sha256")


def finite_number(value, label, *, positive=False):
    try:
        valid = (not isinstance(value, bool) and isinstance(value, (int, float)) and
                 math.isfinite(value) and value >= 0 and (not positive or value > 0))
    except OverflowError:
        valid = False
    if not valid:
        description = "positive" if positive else "nonnegative"
        raise ValueError(f"{label} must be a finite {description} number")
    return float(value)


def summarize(record):
    if (not isinstance(record, dict) or type(record.get("schema")) is not int or
            record["schema"] != 1):
        raise ValueError("schema must be 1")
    for name in HASH_KEYS:
        if not isinstance(record.get(name), str) or not re.fullmatch(r"[0-9a-f]{64}", record[name]):
            raise ValueError(f"{name} must be a lowercase SHA-256")
    for name in ("boot_id", "workload"):
        if not isinstance(record.get(name), str) or not record[name].strip():
            raise ValueError(f"{name} must identify the capture")
    resolution = finite_number(record.get("timer_resolution_seconds"),
                               "timer_resolution_seconds", positive=True)
    iterations = record.get("iterations")
    if isinstance(iterations, bool) or not isinstance(iterations, int) or iterations < 1:
        raise ValueError("iterations must be a positive integer")
    pairs = record.get("pairs")
    if not isinstance(pairs, list) or len(pairs) < 2:
        raise ValueError("at least two completed pairs are required")
    differences = []
    identifiers = set()
    order_counts = {"packed-raw": 0, "raw-packed": 0}
    for pair in pairs:
        if not isinstance(pair, dict):
            raise ValueError("each pair must be an object")
        identifier = pair.get("id")
        if not isinstance(identifier, str) or not identifier.strip() or identifier in identifiers:
            raise ValueError("pair identifiers must be nonempty and unique")
        identifiers.add(identifier)
        if pair.get("order") not in ("packed-raw", "raw-packed"):
            raise ValueError(f"{identifier}: order must name both variants")
        order_counts[pair["order"]] += 1
        if pair.get("packed_success") is not True or pair.get("raw_success") is not True:
            raise ValueError(f"{identifier}: both workloads must complete successfully")
        packed = finite_number(pair.get("packed_seconds"), f"{identifier} packed_seconds")
        raw = finite_number(pair.get("raw_seconds"), f"{identifier} raw_seconds")
        differences.append((packed - raw) / iterations)
    count = len(differences)
    mean = statistics.mean(differences)
    try:
        deviation = statistics.stdev(differences)
    except OverflowError as error:
        raise ValueError("timing range exceeds numeric precision") from error
    error = deviation / math.sqrt(count)
    quantile = T95[min(count - 1, 30) - 1] + 0.000001
    half_width = quantile * error
    quantization = (resolution / iterations) * 2
    if not all(math.isfinite(value) for value in (mean, deviation, error, half_width,
                                                   quantization, mean - half_width,
                                                   mean + half_width)):
        raise ValueError("timing range exceeds numeric precision")
    return {"identity": {key: record[key] for key in (*HASH_KEYS, "boot_id", "workload")},
            "pairs": count, "order_counts": order_counts, "iterations_per_pair": iterations,
            "mean_difference_seconds_per_exec": mean,
            "sample_sd_seconds_per_exec": deviation,
            "standard_error_seconds_per_exec": error,
            "paired_t95_sampling_interval_seconds_per_exec": [mean - half_width, mean + half_width],
            "t_quantile": quantile,
            "timer_quantization_bound_seconds_per_exec": quantization,
            "quantization_assumption": "each duration has at most one timer resolution of "
                                       "quantization error; excludes clock and scheduling error",
            "interval_assumption": "independent paired differences; t model; "
                                   "excludes other systematic error"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    try:
        record = json.loads(args.capture.read_text(encoding="utf-8"))
        print(json.dumps(summarize(record), indent=2, sort_keys=True))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"paired exec summary: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
