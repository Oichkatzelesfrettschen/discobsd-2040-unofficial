"""Create one-defect copies of subr_capacity.c for gate calibration."""

import argparse
from pathlib import Path

MUTATIONS = {
    "process-state": ("proc[index].p_stat != 0", "proc[index].p_stat == SRUN"),
    "inode-references": (
        "live += inode[index].i_count != 0;",
        "live += inode[index].i_count;",
    ),
    "clist-object-size": (
        "live = NCLIST - cfreecount / CBSIZE;",
        "live = NCLIST - cfreecount / sizeof(struct cblock);",
    ),
    "buffer-any-flag": (
        "live += (buf[index].b_flags & B_BUSY) != 0;",
        "live += buf[index].b_flags != 0;",
    ),
    "peak-follows-down": (
        "if (metric->current > metric->peak)",
        "if (metric->current < metric->peak)",
    ),
    "saturation-inverted": (
        "if (remaining == 0)\n        capacity_state.stack_saturated = 1;",
        "if (remaining == capacity_state.stack_bytes)\n        capacity_state.stack_saturated = 1;",
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
        (output_directory / f"capacity_{name}.mutant.c").write_text(
            source.replace(before, after, 1), encoding="utf-8"
        )
    print(f"generated {len(MUTATIONS)} capacity mutations")


if __name__ == "__main__":
    main()
