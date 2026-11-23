"""Fixture maker for the tail locator gate (tests/fat32tail/check.sh).

usage:
    fixtures.py uf2 BLOCKS OUT
    fixtures.py composite KIND BLOCKS ROOT_IMAGE OUT
    fixtures.py root KIND IN OUT
    fixtures.py patch IMAGE VOLUME_LBA KIND [ARG...]
    fixtures.py shift IMAGE OUT SECTORS

uf2 writes a synthetic kernel UF2 of BLOCKS valid RP2040 blocks. composite
assembles the layout the builder writes, or, for a damaged KIND, a file the
builder would refuse (so the locator's refusals are exercised, not the
builder's). root damages a copy of a root image. patch edits one volume in
place, through the byte offsets the FAT specification gives, to produce the
damage the msdosfs tool cannot write. shift copies an image behind SECTORS
zero sectors, keeping it sparse, so a volume starts at a nonzero sector.
"""

from __future__ import annotations

import mmap
import struct
import sys
from pathlib import Path

HEAD_ALIGN = 32768
UF2 = struct.Struct("<8I")
START0, START1, END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FAMILY_FLAG, RP2040 = 0x2000, 0xE48BFF56
EOC = 0x0FFFFFFF
DAMAGED_TAIL = (
    "stray-bytes",
    "no-tail",
    "one-sector",
    "short-tail",
    "bad-magic1",
    "bad-magic2",
    "zero-tail",
)


def uf2_block(number: int, total: int, seed: int) -> bytes:
    block = bytearray(512)
    UF2.pack_into(
        block, 0, START0, START1, FAMILY_FLAG, 0x10000000 + 256 * number, 256, number, total, RP2040
    )
    block[32:288] = bytes((seed + number * 7 + i * 13) & 0xFF for i in range(256))
    struct.pack_into("<I", block, 508, END)
    return bytes(block)


def make_uf2(blocks: int) -> bytes:
    return b"".join(uf2_block(n, blocks, 0x5A) for n in range(blocks))


def head_size(blocks: int) -> int:
    return -(-(blocks * 512) // HEAD_ALIGN) * HEAD_ALIGN


def make_composite(kind: str, blocks: int, root: bytes) -> bytes:
    kernel = bytearray(make_uf2(blocks))
    first, last = 0, (blocks - 1) * 512
    if kind == "good":
        pass
    elif kind == "bad-start-magic":
        kernel[0:4] = bytes(4)
    elif kind == "bad-end-magic":
        kernel[508:512] = bytes(4)
    elif kind == "zero-blocks":
        struct.pack_into("<I", kernel, first + 24, 0)
    elif kind == "huge-blocks":
        struct.pack_into("<I", kernel, first + 24, 0x00400000)
    elif kind == "first-number":
        struct.pack_into("<I", kernel, first + 20, 1)
    elif kind == "last-number":
        struct.pack_into("<I", kernel, last + 20, blocks - 2)
    elif kind == "last-total":
        struct.pack_into("<I", kernel, last + 24, blocks + 1)
    elif kind == "last-magic":
        kernel[last : last + 4] = bytes(4)
    elif kind == "lies-short":
        # The first block claims one block fewer than the kernel holds.
        struct.pack_into("<I", kernel, first + 24, blocks - 1)
    elif kind in DAMAGED_TAIL:
        pass
    else:
        raise SystemExit(f"unknown composite kind {kind}")
    body = root
    if kind in ("bad-magic1", "bad-magic2"):
        body = make_root(kind, root)
    elif kind == "zero-tail":
        body = bytes(len(root))
    elif kind == "no-tail":
        body = b""
    elif kind == "one-sector":
        body = root[:512]
    elif kind == "short-tail":
        body = root[: 1024 - 2]
    elif kind == "stray-bytes":
        body = root + bytes(100)
    return bytes(kernel) + bytes(head_size(blocks) - len(kernel)) + body


def make_root(kind: str, data: bytes) -> bytes:
    if kind == "bad-magic1":
        return bytes(4) + data[4:]
    if kind == "bad-magic2":
        return data[:1020] + bytes(4) + data[1024:]
    raise SystemExit(f"unknown root kind {kind}")


class Volume:
    """Byte offsets of one FAT32 volume inside a mapped image."""

    def __init__(self, path: str, lba: int):
        self.handle = open(path, "r+b")  # noqa: SIM115
        self.map = mmap.mmap(self.handle.fileno(), 0)
        self.base = lba * 512
        b = self.map
        o = self.base
        self.bps, self.spc, self.reserved, self.nfats = struct.unpack_from("<HBHB", b, o + 11)
        (self.fatsz,) = struct.unpack_from("<I", b, o + 36)
        (self.root,) = struct.unpack_from("<I", b, o + 44)
        self.first_data = self.reserved + self.nfats * self.fatsz

    def fat_entry_offsets(self, cluster: int):
        for copy in range(self.nfats):
            yield self.base + (self.reserved + copy * self.fatsz) * 512 + 4 * cluster

    def get_fat(self, cluster: int) -> int:
        (value,) = struct.unpack_from("<I", self.map, next(self.fat_entry_offsets(cluster)))
        return value & 0x0FFFFFFF

    def set_fat(self, cluster: int, value: int, copies: str = "all") -> None:
        for index, offset in enumerate(self.fat_entry_offsets(cluster)):
            if copies == "all" or (copies == "first" and index == 0):
                struct.pack_into("<I", self.map, offset, value)

    def cluster_offset(self, cluster: int) -> int:
        return self.base + (self.first_data + (cluster - 2) * self.spc) * 512

    def chain(self, first: int) -> list[int]:
        out = [first]
        while self.get_fat(out[-1]) < 0x0FFFFFF8:
            out.append(self.get_fat(out[-1]))
        return out

    def slots(self, first: int):
        """Offsets of every 32-byte slot of the directory chain from first."""
        for cluster in self.chain(first):
            start = self.cluster_offset(cluster)
            for slot in range(start, start + self.spc * 512, 32):
                if self.map[slot] == 0:
                    return
                yield slot

    def find(self, short: bytes, first: int | None = None) -> int:
        """Offset of the live short entry named short, in a directory or its subdirectories."""
        first = self.root if first is None else first
        subdirs = []
        for slot in self.slots(first):
            attr = self.map[slot + 11]
            if self.map[slot] == 0xE5 or attr & 0x3F == 0x0F or attr & 0x08:
                continue
            if bytes(self.map[slot : slot + 11]).upper() == short:
                return slot
            if attr & 0x10 and self.map[slot] != 0x2E:
                subdirs.append(slot)
        for slot in subdirs:
            try:
                return self.find(short, self.entry_cluster(slot))
            except KeyError:
                continue
        raise KeyError(short)

    def entry_cluster(self, slot: int) -> int:
        (hi,) = struct.unpack_from("<H", self.map, slot + 20)
        (lo,) = struct.unpack_from("<H", self.map, slot + 26)
        return hi << 16 | lo

    def set_entry_cluster(self, slot: int, cluster: int) -> None:
        struct.pack_into("<H", self.map, slot + 20, cluster >> 16)
        struct.pack_into("<H", self.map, slot + 26, cluster & 0xFFFF)

    def close(self) -> None:
        self.map.flush()
        self.map.close()
        self.handle.close()


TARGET = b"GIZMOBSDUF2"


def patch(path: str, lba: int, kind: str, args: list[str]) -> None:
    vol = Volume(path, lba)
    try:
        apply_patch(vol, kind, args)
    finally:
        vol.close()


def apply_patch(vol: Volume, kind: str, args: list[str]) -> None:
    m, o = vol.map, vol.base
    if kind == "lowercase-short":
        slot = vol.find(TARGET)
        m[slot : slot + 11] = TARGET.lower()
    elif kind == "label-before":
        # The first live decoy entry of the root becomes a volume label that
        # carries the target's name; the file itself follows it.
        for slot in vol.slots(vol.root):
            live = m[slot] != 0xE5 and m[slot + 11] & 0x3F != 0x0F
            if live and bytes(m[slot : slot + 11]) != TARGET:
                m[slot : slot + 11] = TARGET
                m[slot + 11] = 0x08
                return
        raise SystemExit("no decoy entry")
    elif kind == "delete-before":
        # Mark the first live short entry deleted: a 0xE5 entry sits before the file.
        for slot in vol.slots(vol.root):
            if m[slot] != 0xE5 and m[slot + 11] & 0x3F != 0x0F:
                m[slot] = 0xE5
                return
        raise SystemExit("no entry to delete")
    elif kind == "delete-entry":
        short = args[0].encode("ascii").ljust(11)
        slot = vol.find(short)
        m[slot] = 0xE5
    elif kind == "active-fat1":
        struct.pack_into("<H", m, o + 40, 0x0081)
        slot = vol.find(TARGET)
        for cluster in vol.chain(vol.entry_cluster(slot)):
            vol.set_fat(cluster, 0, copies="first")
    elif kind == "bpb":
        offset, size, value = int(args[0]), int(args[1]), int(args[2], 0)
        m[o + offset : o + offset + size] = value.to_bytes(size, "little")
    elif kind == "exfat-like":
        m[o + 3 : o + 11] = b"EXFAT   "
        m[o + 11 : o + 64] = bytes(53)
    elif kind == "root-loop":
        chain = vol.chain(vol.root)
        for cluster in chain:
            start = vol.cluster_offset(cluster)
            for slot in range(start, start + vol.spc * 512, 32):
                m[slot] = 0xE5
        vol.set_fat(chain[-1], chain[0])
    elif kind == "file-fat":
        # file-fat INDEX VALUE: FAT entry INDEX clusters into the target's chain.
        slot = vol.find(TARGET)
        first = vol.entry_cluster(slot)
        vol.set_fat(first + int(args[0]), int(args[1], 0))
    elif kind == "alias-chain":
        # alias-chain COUNT: every subdirectory entry of the root names one
        # chain of COUNT clusters whose entries are all deleted, so each scan
        # of a sibling runs the whole chain and finds zero entries.
        count = int(args[0])
        start = 20000
        for cluster in range(start, start + count):
            vol.set_fat(cluster, cluster + 1 if cluster + 1 < start + count else EOC)
            base = vol.cluster_offset(cluster)
            for slot in range(base, base + vol.spc * 512, 32):
                m[slot] = 0xE5
        for slot in vol.slots(vol.root):
            live = m[slot] != 0xE5 and m[slot + 11] & 0x3F != 0x0F
            if live and m[slot + 11] & 0x10 and m[slot] != 0x2E:
                vol.set_entry_cluster(slot, start)
    elif kind == "file-attr":
        slot = vol.find(TARGET)
        m[slot + 11] = int(args[0], 0)
    elif kind == "file-first":
        slot = vol.find(TARGET)
        vol.set_entry_cluster(slot, int(args[0], 0))
    elif kind == "file-size":
        slot = vol.find(TARGET)
        struct.pack_into("<I", m, slot + 28, int(args[0], 0))
    elif kind == "subdir-first":
        # subdir-first VALUE: the first subdirectory entry of the root gets cluster VALUE.
        for slot in vol.slots(vol.root):
            if m[slot] != 0xE5 and m[slot + 11] & 0x3F != 0x0F and m[slot + 11] & 0x10:
                vol.set_entry_cluster(slot, int(args[0], 0))
                return
        raise SystemExit("no subdirectory")
    else:
        raise SystemExit(f"unknown patch kind {kind}")


def shift(source: str, out: str, sectors: int) -> None:
    """Copy source behind sectors zero sectors, skipping its zero blocks."""
    step = 1 << 20
    with open(source, "rb") as src, open(out, "wb") as dst:
        dst.seek(sectors * 512)
        position = sectors * 512
        while chunk := src.read(step):
            if chunk.count(0) == len(chunk):
                position += len(chunk)
                dst.seek(position)
            else:
                dst.write(chunk)
                position += len(chunk)
        dst.truncate(position)


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[0] == "uf2":
        Path(argv[2]).write_bytes(make_uf2(int(argv[1])))
    elif len(argv) == 5 and argv[0] == "composite":
        root = Path(argv[3]).read_bytes()
        Path(argv[4]).write_bytes(make_composite(argv[1], int(argv[2]), root))
    elif len(argv) == 4 and argv[0] == "root":
        Path(argv[3]).write_bytes(make_root(argv[1], Path(argv[2]).read_bytes()))
    elif len(argv) >= 4 and argv[0] == "patch":
        patch(argv[1], int(argv[2]), argv[3], argv[4:])
    elif len(argv) == 4 and argv[0] == "shift":
        shift(argv[1], argv[2], int(argv[3]))
    else:
        raise SystemExit(__doc__)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
