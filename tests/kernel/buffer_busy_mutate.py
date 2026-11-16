"""Create one-defect copies of ufs_bio.c for the buffer-pressure gate."""

import argparse
from pathlib import Path

MUTATIONS = {
    # The all-busy path stops counting a buffer capacity failure.
    "no-capacity-note": (
        "        capacity_note(CAPACITY_BUFFER, 1);\n"
        "        storage_note(STORAGE_BUFFER_WAITS, 1);\n",
        "        storage_note(STORAGE_BUFFER_WAITS, 1);\n",
    ),
    # The delayed-write eviction path counts a capacity failure as well.
    "eviction-capacity-note": (
        "        storage_note(STORAGE_EVICTION_WRITES, 1);\n        bawrite(bp);\n",
        "        storage_note(STORAGE_EVICTION_WRITES, 1);\n"
        "        capacity_note(CAPACITY_BUFFER, 1);\n"
        "        bawrite(bp);\n",
    ),
    # brelse() stops waking the process sleeping on the free-list head.
    "no-freelist-wakeup": (
        "        wakeup((caddr_t)bfreelist);\n",
        "",
    ),
    # The eviction releases the delayed-write buffer without writing it.
    "eviction-drops-write": (
        "        storage_note(STORAGE_EVICTION_WRITES, 1);\n        bawrite(bp);\n",
        "        storage_note(STORAGE_EVICTION_WRITES, 1);\n"
        "        bp->b_flags &= ~B_DELWRI;\n"
        "        brelse(bp);\n",
    ),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("output_directory")
    args = parser.parse_args()
    source = Path(args.source).read_text(encoding="utf-8")
    output_directory = Path(args.output_directory)
    for name, (before, after) in MUTATIONS.items():
        if source.count(before) != 1:
            raise SystemExit(f"mutation anchor {name!r} occurs {source.count(before)} times")
        (output_directory / f"buffer_busy_{name}.mutant.c").write_text(
            source.replace(before, after, 1), encoding="utf-8"
        )
    print(f"generated {len(MUTATIONS)} buffer-busy mutations")


if __name__ == "__main__":
    main()
