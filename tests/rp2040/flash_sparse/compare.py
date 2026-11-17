"""Compare a disk image with its readback through the Dhara map.

The free list of every 2.11BSD UFS partition of ORIGINAL is walked here
independently of tools/flashimg: the superblock's nfree and free[200], then
each chain block (the list's entry 0) holding the next nfree and 200
entries, until entry 0 is block 0. Expected readback:

  used-readback   a unit outside the free list reads back unchanged
  chain-readback  a chain block reads back unchanged
  free-unwritten  any other free block reads as 0xff bytes
  live-share      the map holds exactly the units that are not free blocks

Usage: compare.py ORIGINAL READBACK LIVE CAPACITY
Prints one "FAIL <label>: ..." line per unmet expectation (the first ten of
a label) and exits 1 when any failed.
"""

import struct
import sys
from pathlib import Path

UNIT = 1024
UFS_TYPE = 0xB7
MAGIC1 = 0x3C3C5346
MAGIC2 = 0x53463E3E


def word(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def free_lists(image: bytes) -> tuple[set[int], set[int]]:
    """Return (free units, chain units) over every UFS partition."""
    free: set[int] = set()
    chain: set[int] = set()
    if len(image) < UNIT or image[510:512] != b"\x55\xaa":
        return free, chain
    for index in range(4):
        entry = image[446 + 16 * index : 462 + 16 * index]
        offset = word(entry, 8)
        if entry[4] != UFS_TYPE or offset % 2:
            continue
        base = offset // 2
        sb = image[base * UNIT : (base + 1) * UNIT]
        if len(sb) < UNIT or word(sb, 0) != MAGIC1 or word(sb, 1020) != MAGIC2:
            continue
        nfree = word(sb, 16)
        entries = [word(sb, 20 + 4 * i) for i in range(200)]
        while nfree:
            if nfree > 200:
                raise SystemExit(f"compare: free count {nfree} at unit {base}")
            for block in entries[1:nfree]:
                free.add(base + block)
            if entries[0] == 0:
                break
            link = base + entries[0]
            chain.add(link)
            block = image[link * UNIT : (link + 1) * UNIT]
            nfree = word(block, 0)
            entries = [word(block, 4 + 4 * i) for i in range(200)]
    return free, chain


def main() -> int:
    original = Path(sys.argv[1]).read_bytes()
    readback = Path(sys.argv[2]).read_bytes()
    live = int(sys.argv[3])
    capacity = int(sys.argv[4])
    units = (len(original) + UNIT - 1) // UNIT
    original = original.ljust(units * UNIT, b"\0")
    free, chain = free_lists(original)
    failures: dict[str, int] = {}

    def fail(label: str, text: str) -> None:
        failures[label] = failures.get(label, 0) + 1
        if failures[label] <= 10:
            print(f"FAIL {label}: {text}")

    if len(readback) != units * UNIT:
        fail("used-readback", f"readback holds {len(readback)} bytes, not {units * UNIT}")
        return 1
    for unit in range(units):
        got = readback[unit * UNIT : (unit + 1) * UNIT]
        want = original[unit * UNIT : (unit + 1) * UNIT]
        if unit in free:
            if got != b"\xff" * UNIT:
                fail("free-unwritten", f"free unit {unit} was written")
        elif got != want:
            label = "chain-readback" if unit in chain else "used-readback"
            fail(label, f"unit {unit} differs from the image")
    expected = units - len(free)
    if live != expected:
        fail("live-share", f"{live} units live, expected {expected} of {units}")
    print(
        f"compare: {units} units, {len(chain)} chain blocks, {len(free)} free "
        f"blocks unwritten, {live} of {capacity} capacity sectors live "
        f"({100.0 * live / capacity:.1f} percent)"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
