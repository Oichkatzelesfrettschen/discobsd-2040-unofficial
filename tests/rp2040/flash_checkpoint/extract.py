"""Extract the flash driver code that flash_checkpoint_test.c runs.

flash.c reaches the chip through boot ROM entry points found at fixed
addresses, so it does not link into a host binary whole. The gate takes the
text of everything above that layer, in source order, and the test supplies
flash_erase(), flash_program() and flash_rom_init() over a NOR model:

  map     the Dhara map state, the shared scratch page owner and flnand
  nand    the dhara_nand_* driver callbacks
  disk    the partition state, fl_checkpoint, fl_setup, flopen, flclose
          and flsize
  strat   flstrategy and flioctl

A missing or repeated anchor stops the extraction, so a renamed routine
fails the gate instead of leaving it testing a stale copy.
"""

import argparse
import hashlib
from pathlib import Path

# (label, first line of the range, first line after it); None ends at EOF.
RANGES = (
    ("map", "static struct dhara_map flmap;\n", "/*\n * The two reads below"),
    ("nand", "/*\n * Dhara's driver interface.", "/*\n * Block device.\n"),
    ("disk", "#define\tNPARTITIONS\tFLASH_PARTITION_COUNT\n", "/*\n * The raw swap region"),
    ("strat", "void\nflstrategy(struct buf *bp)\n", None),
)


def unique_index(source: str, anchor: str, label: str) -> int:
    count = source.count(anchor)
    if count != 1:
        raise SystemExit(f"{label}: anchor {anchor!r} occurs {count} times")
    return source.index(anchor)


def extract(source: str) -> str:
    pieces = []
    for label, first, after in RANGES:
        start = unique_index(source, first, label)
        end = len(source) if after is None else unique_index(source, after, label)
        if end <= start:
            raise SystemExit(f"{label}: range ends before it starts")
        pieces.append(source[start:end])
    return "".join(pieces)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    data = arguments.source.read_bytes()
    banner = (
        "/* Extracted from "
        + arguments.source.name
        + " SHA-256 "
        + hashlib.sha256(data).hexdigest()
        + ". */\n"
    )
    arguments.output.write_text(banner + extract(data.decode()))


if __name__ == "__main__":
    main()
