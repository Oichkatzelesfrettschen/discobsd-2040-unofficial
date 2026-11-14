"""Validate a crash dump on an SD card image, independently of the kernel.

The format is sys/arch/rp2040/include/dump.h: sector 0 of the card's first
MBR partition of type 0xb9 holds a 24-byte header (magic "DUMP", version 1,
segment count, image CRC32, time, total bytes, header CRC32) and 16-byte
segment entries (kind, address, bytes, CRC32), zeros after them; each
segment follows from sector 1 on a sector boundary, padded with zeros.
Every CRC is zlib's crc32(); the header's covers the header and table with
its own field read as zero. This reader shares no code with the kernel's
writer (rp2040/dump.c) or savecore(8), so a format slip in both shows here.

Usage: check_crash_dump.py CARD_IMAGE [--message TEXT] [--absent]
       check_crash_dump.py --self-test
--message requires the message segment to equal TEXT; --absent requires the
partition to hold no dump (sector 0 without the magic). Exit status: 0
pass, 1 the dump disagrees, 2 the image has no dump partition.
"""

import argparse
import struct
import sys
import zlib

SECTOR = 512
MAGIC = 0x504D5544
VERSION = 1
PART_TYPE = 0xB9
HEADER = struct.Struct("<IHHIIII")
SEGMENT = struct.Struct("<IIII")
MAXSEGMENTS = (SECTOR - HEADER.size) // SEGMENT.size
KINDS = {1: "message", 2: "kernel data", 3: "u area"}


class NoPartition(Exception):
    """The card image has no MBR entry of the dump type."""


def dump_partition(card):
    """Byte offset and length of the first type 0xb9 entry inside the card."""
    if len(card) < SECTOR or card[510:512] != b"\x55\xaa":
        raise NoPartition("no MBR signature")
    for index in range(4):
        entry = card[446 + 16 * index:446 + 16 * (index + 1)]
        kind = entry[4]
        start, count = struct.unpack_from("<II", entry, 8)
        if kind == PART_TYPE and count and (start + count) * SECTOR <= len(card):
            return start * SECTOR, count * SECTOR
    raise NoPartition("no partition of type 0xb9")


def check_dump(part, message=None):
    """Failures of the dump in partition bytes part, and its segments."""
    if len(part) < SECTOR:
        return ["partition shorter than a sector"], []
    magic, version, count, _image, _time, total, crc = HEADER.unpack_from(part)
    if magic != MAGIC:
        return ["no dump: sector 0 lacks the magic"], []
    failures = []
    if version != VERSION:
        failures.append(f"version {version}, want {VERSION}")
    if not 1 <= count <= MAXSEGMENTS:
        return failures + [f"segment count {count}"], []
    table_end = HEADER.size + count * SEGMENT.size
    sealed = bytearray(part[:table_end])
    sealed[20:24] = b"\0\0\0\0"
    if zlib.crc32(bytes(sealed)) != crc:
        failures.append("header checksum mismatch")
    if any(part[table_end:SECTOR]):
        failures.append("sector 0 is not zero after the table")
    segments = []
    offset = SECTOR
    for index in range(count):
        kind, address, length, seg_crc = SEGMENT.unpack_from(
            part, HEADER.size + index * SEGMENT.size)
        data = part[offset:offset + length]
        padded = -(-length // SECTOR) * SECTOR
        if len(data) != length:
            failures.append(f"segment {index} runs past the partition")
            break
        if zlib.crc32(data) != seg_crc:
            failures.append(f"segment {index} ({KINDS.get(kind, kind)}) checksum mismatch")
        if any(part[offset + length:offset + padded]):
            failures.append(f"segment {index} padding is not zero")
        segments.append((kind, address, length, data))
        offset += padded
    if offset != total:
        failures.append(f"length {total}, the table spans {offset}")
    if message is not None:
        found = [data for kind, _a, _l, data in segments if kind == 1]
        if found != [message.encode()]:
            failures.append(f"message {found!r}, want {message!r}")
    return failures, segments


def build_dump(segments, image_crc=0x1234ABCD, when=1700000000):
    """A dump in the format above, written by this module for calibration."""
    body = bytearray()
    table = bytearray()
    for kind, address, data in segments:
        table += SEGMENT.pack(kind, address, len(data), zlib.crc32(data))
        body += data + bytes(-len(data) % SECTOR)
    total = SECTOR + len(body)
    head = HEADER.pack(MAGIC, VERSION, len(segments), image_crc, when, total, 0)
    crc = zlib.crc32(head + table)
    head = HEADER.pack(MAGIC, VERSION, len(segments), image_crc, when, total, crc)
    sector0 = head + table
    return bytes(sector0 + bytes(SECTOR - len(sector0)) + body)


def self_test():
    segments = [(1, 0x10001000, b"panic: test"),
                (2, 0x20030000, bytes(range(256)) * 9),
                (3, 0x2003F400, bytes(3072))]
    good = build_dump(segments) + bytes(4 * SECTOR)
    failures, found = check_dump(good, "panic: test")
    if failures or len(found) != 3:
        print(f"self-test: the good dump failed: {failures}", file=sys.stderr)
        return 1

    def flip(data, offset, value):
        changed = bytearray(data)
        changed[offset] = value
        return bytes(changed)

    def reseal(data):
        count = struct.unpack_from("<H", data, 6)[0]
        sealed = bytearray(data[:HEADER.size + count * SEGMENT.size])
        sealed[20:24] = b"\0\0\0\0"
        changed = bytearray(data)
        struct.pack_into("<I", changed, 20, zlib.crc32(bytes(sealed)))
        return bytes(changed)

    mutants = {
        "segment byte": (flip(good, 2 * SECTOR + 5, 0xEE), "checksum mismatch"),
        "header checksum": (flip(good, 21, good[21] ^ 1), "header checksum"),
        "version": (reseal(flip(good, 4, 2)), "version"),
        "padding": (flip(good, SECTOR + 20, 1), "padding"),
        "table tail": (flip(good, 100, 1), "after the table"),
        "length": (reseal(flip(good, 17, good[17] + 2)), "length"),
        "message": (good, "message"),
        "no magic": (flip(good, 0, 0), "no dump"),
    }
    for name, (data, word) in mutants.items():
        failures, _found = check_dump(data, "other" if name == "message" else None)
        if not any(word in failure for failure in failures):
            print(f"self-test: the {name} mutant was not rejected: {failures}",
                  file=sys.stderr)
            return 1
    card = bytearray(64 * SECTOR)
    struct.pack_into("<II", card, 446 + 16 + 8, 8, 40)
    card[446 + 16 + 4] = PART_TYPE
    card[510:512] = b"\x55\xaa"
    if dump_partition(bytes(card)) != (8 * SECTOR, 40 * SECTOR):
        print("self-test: the dump partition was not found", file=sys.stderr)
        return 1
    card[446 + 16 + 4] = 0xB7
    try:
        dump_partition(bytes(card))
    except NoPartition:
        pass
    else:
        print("self-test: a card without a 0xb9 entry was accepted", file=sys.stderr)
        return 1
    print("check_crash_dump self-test: ok (a good dump, eight mutants, the partition rule)")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("card", nargs="?")
    parser.add_argument("--message")
    parser.add_argument("--absent", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.card is None:
        parser.error("name a card image")
    with open(args.card, "rb") as stream:
        card = stream.read()
    try:
        start, length = dump_partition(card)
    except NoPartition as error:
        print(f"check_crash_dump: {args.card}: {error}", file=sys.stderr)
        return 2
    failures, segments = check_dump(card[start:start + length], args.message)
    if args.absent:
        if card[start:start + 4] == struct.pack("<I", MAGIC):
            print(f"FAIL crash-dump {args.card}: a dump is still on the card")
            return 1
        print(f"PASS crash-dump {args.card}: no dump on the partition")
        return 0
    for failure in failures:
        print(f"FAIL crash-dump {args.card}: {failure}")
    if failures:
        return 1
    print(f"PASS crash-dump {args.card}: " + ", ".join(
        f"{KINDS.get(kind, kind)} {length} bytes at {address:#010x}"
        for kind, address, length, _data in segments))
    return 0


if __name__ == "__main__":
    sys.exit(main())
