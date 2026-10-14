"""Validate an RP2040 kernel and root UF2 pair before it is flashed.

Usage: check_rp2040_uf2.py --self-test | check_rp2040_uf2.py KERNEL ROOT

Every block must carry both UF2 magic words and the end magic, the RP2040
family ID with its flag, one 256-byte aligned payload, and a block number
and total consistent with the file. The kernel must be contiguous inside
[0x10000000, 0x10020000); the root must cover [0x10020000, 0x101a0000)
exactly, the region dev/flash.h assigns to the Dhara root. The report prints
each image's block count, covered interval and SHA-256. --self-test accepts
a valid two-block image and rejects one mutation of each checked field.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY = 0x00002000
RP2040_FAMILY = 0xE48BFF56
KERNEL = (0x10000000, 0x10020000)
ROOT = (0x10020000, 0x101A0000)


def parse(data: bytes, region: tuple[int, int], contiguous: bool) -> dict[str, object]:
    if not data or len(data) % 512:
        raise ValueError("size is not a positive multiple of 512")
    count = len(data) // 512
    addresses = []
    for index in range(count):
        block = data[index * 512:(index + 1) * 512]
        (m0, m1, flags, address, size, number, total,
         family) = struct.unpack_from("<8I", block)
        (end,) = struct.unpack_from("<I", block, 508)
        if (m0, m1, end) != (MAGIC0, MAGIC1, MAGIC_END):
            raise ValueError(f"block {index}: bad magic")
        if not flags & FLAG_FAMILY or family != RP2040_FAMILY:
            raise ValueError(f"block {index}: family is not RP2040")
        if size != 256 or address % 256:
            raise ValueError(f"block {index}: payload size or alignment")
        if number != index or total != count:
            raise ValueError(f"block {index}: numbering {number}/{total}")
        if not region[0] <= address < region[1] - 255:
            raise ValueError(f"block {index}: address {address:#x} outside")
        addresses.append(address)
    if len(set(addresses)) != count:
        raise ValueError("duplicate block address")
    low, high = min(addresses), max(addresses) + 256
    if contiguous and (low, high, count) != (region[0], region[1],
                                            (region[1] - region[0]) // 256):
        raise ValueError("root does not cover its region exactly")
    if not contiguous and addresses != list(range(low, high, 256)):
        raise ValueError("kernel blocks are not contiguous")
    return {"blocks": count, "interval": f"[{low:#010x}, {high:#010x})"}


def block(address: int, number: int, total: int) -> bytearray:
    raw = bytearray(512)
    struct.pack_into("<8I", raw, 0, MAGIC0, MAGIC1, FLAG_FAMILY, address,
                     256, number, total, RP2040_FAMILY)
    struct.pack_into("<I", raw, 508, MAGIC_END)
    return raw


def self_test() -> None:
    good = b"".join(block(KERNEL[0] + 256 * i, i, 2) for i in range(2))
    parse(good, KERNEL, False)
    mutations = {
        "magic": (0, b"\0\0\0\0"),
        "end magic": (508, b"\0\0\0\0"),
        "family flag": (8, struct.pack("<I", 0)),
        "family": (28, struct.pack("<I", 0x12345678)),
        "payload size": (16, struct.pack("<I", 255)),
        "block number": (20, struct.pack("<I", 1)),
        "block total": (24, struct.pack("<I", 3)),
        "address": (12, struct.pack("<I", ROOT[0])),
    }
    for name, (offset, value) in mutations.items():
        bad = bytearray(good)
        bad[offset:offset + len(value)] = value
        try:
            parse(bytes(bad), KERNEL, False)
        except ValueError:
            continue
        raise SystemExit(f"self-test accepted the {name} mutation")
    print(f"uf2 self-test: valid accepted, {len(mutations)} mutations rejected")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("kernel", nargs="?", type=Path)
    parser.add_argument("root", nargs="?", type=Path)
    arguments = parser.parse_args()
    if arguments.self_test:
        if arguments.kernel or arguments.root:
            parser.error("--self-test takes no image operands")
        self_test()
        return
    if arguments.root is None:
        parser.error("KERNEL and ROOT images are required")
    report: dict[str, dict[str, object]] = {}
    for label, path, region, contiguous in (
            ("kernel", arguments.kernel, KERNEL, False),
            ("root", arguments.root, ROOT, True)):
        data = path.read_bytes()
        try:
            report[label] = parse(data, region, contiguous)
        except ValueError as error:
            raise SystemExit(f"{path}: {error}") from None
        report[label]["path"] = str(path)
        report[label]["sha256"] = hashlib.sha256(data).hexdigest()
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
