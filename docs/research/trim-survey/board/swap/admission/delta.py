"""Print the counter deltas of the two retained admission captures.

usage: delta.py main-sh.txt fixed-sh.txt

Each capture holds `sysctl machdep.storage_stats` before and after the 24
compiles. They were collected with the probe before its layout became
version 2, so their header reads version 1 with the expanded metric set and
tools/summarize_rp2040_storage_delta.py, which ties version 1 to the
original 26 metrics, rejects them. This reader takes every `name value`
line between the two commands as it stands.
"""
import re
import sys

MARK = "### sysctl machdep.storage_stats"


def snapshot(text):
    values = {}
    for line in text.split("###")[0].splitlines():
        match = re.fullmatch(r"\s*([a-z_0-9]+)[ =:]+(\d+)\s*", line)
        if match:
            values[match.group(1)] = int(match.group(2))
    return values


def deltas(path):
    with open(path, encoding="utf-8") as capture:
        parts = capture.read().split(MARK)
    if len(parts) != 3:
        raise SystemExit(f"{path}: expected two storage_stats snapshots")
    before, after = snapshot(parts[1]), snapshot(parts[2])
    if not before or before.keys() != after.keys():
        raise SystemExit(f"{path}: the snapshots name different metrics")
    return {name: after[name] - before[name] for name in after}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    first, second = deltas(sys.argv[1]), deltas(sys.argv[2])
    for name, value in first.items():
        other = second.get(name, 0)
        if value or other:
            print(f"{name:32s} {value:>10d} {other:>10d}")


if __name__ == "__main__":
    main()
