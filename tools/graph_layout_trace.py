"""Attribute PC traces, replay cache hypotheses, and export layout candidates."""

import argparse
import csv
import hashlib
import json
import random
import re
import sys
from bisect import bisect_right
from pathlib import Path


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_symbols(path):
    """Input is nm -S -n --defined-only, with one unique text extent per row."""
    records = []
    for line in path.read_text().splitlines():
        words = line.split()
        if len(words) != 4 or words[2] not in ("t", "T"):
            continue
        address, size = int(words[0], 16), int(words[1], 16)
        name = words[3]
        require(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.$]*", name), "unsafe symbol name")
        require(size > 0 and address % 2 == 0, "zero-sized or odd text extent")
        require(
            0x10000000 <= address < address + size <= 0x11000000,
            "text extent outside allocating XIP alias",
        )
        records.append({"name": name, "address": address, "bytes": size})
    records.sort(key=lambda r: r["address"])
    require(2 <= len(records) <= 512, "need 2..512 text extents")
    require(len({r["name"] for r in records}) == len(records), "duplicate text name")
    for a, b in zip(records, records[1:]):
        require(a["address"] + a["bytes"] <= b["address"], "overlapping text extents/aliases")
    return records


def read_trace(path, symbols):
    starts = [s["address"] for s in symbols]
    result = []
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        require(reader.fieldnames == ["pc", "width"], "trace header must be pc,width")
        for row in reader:
            require(set(row) == {"pc", "width"}, "malformed trace row")
            pc, width = int(row["pc"], 0), int(row["width"])
            require(width in (2, 4) and pc % 2 == 0, "invalid instruction fetch")
            index = bisect_right(starts, pc) - 1
            require(
                index >= 0 and pc + width <= starts[index] + symbols[index]["bytes"],
                "unmapped or crossing instruction fetch",
            )
            result.append((index, pc - starts[index], width))
            require(len(result) <= 1_000_000, "trace exceeds one million events")
    require(result, "empty trace")
    return result


def replay(trace, addresses, capacity=16384, line_bytes=8, ways=2, policy="lru", warm=False):
    require(
        capacity > 0 and line_bytes > 0 and ways > 0 and capacity % (line_bytes * ways) == 0,
        "invalid cache geometry",
    )
    require(policy in ("lru", "fifo"), "invalid cache policy")
    sets = [[] for _ in range(capacity // (line_bytes * ways))]

    def pass_once():
        accesses = misses = 0
        for index, offset, width in trace:
            address = addresses[index] + offset
            for line in range(address // line_bytes, (address + width - 1) // line_bytes + 1):
                accesses += 1
                bucket = sets[line % len(sets)]
                if line in bucket:
                    if policy == "lru":
                        bucket.remove(line)
                        bucket.append(line)
                else:
                    misses += 1
                    if len(bucket) == ways:
                        bucket.pop(0)
                    bucket.append(line)
        return {"line_accesses": accesses, "line_misses": misses}

    if warm:
        pass_once()
    return pass_once()


def place(symbols, order, base, alignment, limit):
    require(alignment >= 2 and alignment & (alignment - 1) == 0, "alignment must be power of two")
    addresses = [0] * len(symbols)
    cursor = base
    for i in order:
        cursor = (cursor + alignment - 1) & -alignment
        addresses[i] = cursor
        cursor += symbols[i]["bytes"]
    require(cursor <= limit, "candidate exceeds text budget")
    return addresses, cursor - base


def study(symbols, training, heldout, trials, seed, alignment):
    require(alignment >= 2 and alignment & (alignment - 1) == 0, "alignment must be power of two")
    base = symbols[0]["address"]
    limit = max(s["address"] + s["bytes"] for s in symbols)
    order = list(range(len(symbols)))

    def score(candidate):
        return sum(replay(training, candidate, policy=p)["line_misses"] for p in ("lru", "fifo"))

    original = [s["address"] for s in symbols]
    best_score, best_addresses, best_order = score(original), original, None
    rng = random.Random(seed)
    accepted = 0
    # Always evaluate packed original order, then bounded random swap search.
    for step in range(trials + 1):
        candidate = order.copy()
        if step:
            i, j = rng.sample(range(len(symbols)), 2)
            candidate[i], candidate[j] = candidate[j], candidate[i]
        try:
            addresses, _ = place(symbols, candidate, base, alignment, limit)
        except ValueError:
            continue
        value = score(addresses)
        if value < best_score:
            best_score, best_addresses, best_order = value, addresses, candidate
            order = candidate
            accepted += 1
    metrics = {}
    for name, trace in (("training", training), ("heldout", heldout)):
        metrics[name] = {}
        for policy in ("lru", "fifo"):
            for warm in (False, True):
                key = policy + ("_warm" if warm else "_cold")
                metrics[name][key] = {
                    "original": replay(trace, original, policy=policy, warm=warm),
                    "candidate": replay(trace, best_addresses, policy=policy, warm=warm),
                }
    transitions = {}
    for a, b in zip(training, training[1:]):
        if a[0] != b[0]:
            edge = (symbols[a[0]]["name"], symbols[b[0]]["name"])
            transitions[edge] = transitions.get(edge, 0) + 1
    return {
        "schema": "discobsd-cache-layout-v1",
        "seed": seed,
        "trials": trials,
        "accepted_improvements": accepted,
        "cache": {"bytes": 16384, "ways": 2, "line_bytes": 8},
        "evidence": "instruction-trace cache hypothesis; no measured board speedup",
        "selected_by": "training cold LRU misses + training cold FIFO misses",
        "metrics": metrics,
        "order": best_order,
        "symbols": [dict(s, candidate_address=best_addresses[i]) for i, s in enumerate(symbols)],
        "transitions": [[a, b, count] for (a, b), count in sorted(transitions.items())],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--training", type=Path, required=True)
    parser.add_argument("--heldout", type=Path, required=True)
    parser.add_argument("--trials", type=int, default=100)
    parser.add_argument("--seed", type=int, default=1729)
    parser.add_argument("--alignment", type=int, default=8)
    parser.add_argument(
        "--emit", choices=("report", "linker", "symbols", "transitions"), default="report"
    )
    args = parser.parse_args()
    try:
        require(0 <= args.trials <= 10000 and 0 <= args.seed < 2**32, "invalid search budget/seed")
        symbols = read_symbols(args.nm)
        training, heldout = read_trace(args.training, symbols), read_trace(args.heldout, symbols)
        require(
            args.training.read_bytes() != args.heldout.read_bytes(),
            "held-out trace equals training",
        )
        report = study(symbols, training, heldout, args.trials, args.seed, args.alignment)
        report["sha256"] = {
            name: hashlib.sha256(path.read_bytes()).hexdigest()
            for name, path in (
                ("nm", args.nm),
                ("training", args.training),
                ("heldout", args.heldout),
                ("tool", Path(__file__)),
            )
        }
        if args.emit == "linker":
            require(report["order"] is not None, "no improving layout; no fragment emitted")
            print("/* Candidate inside .text after vector table; requires -ffunction-sections. */")
            for i in report["order"]:
                print(f". = ALIGN({args.alignment}); *(.text.{symbols[i]['name']})")
        elif args.emit == "symbols":
            json.dump(
                [{"name": s["name"], "bytes": s["bytes"]} for s in symbols], sys.stdout, indent=2
            )
            print()
        elif args.emit == "transitions":
            writer = csv.writer(sys.stdout)
            writer.writerow(["source", "target", "count"])
            writer.writerows(report["transitions"])
        else:
            json.dump(report, sys.stdout, indent=2, allow_nan=False)
            print()
    except (ValueError, OSError, TypeError) as error:
        parser.exit(2, f"graph-layout-trace: {error}\n")


if __name__ == "__main__":
    main()
