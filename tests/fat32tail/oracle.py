"""Independent oracle for the tail locator gate (tests/fat32tail/check.sh).

usage:
    oracle.py check IMAGE VOLUME_LBA
    oracle.py classify IMAGE VOLUME_LBA
    oracle.py extent IMAGE VOLUME_LBA ROOT_IMAGE

The oracle reads the volume through tests/msdosfs_core/fat_reader.py, which
follows the Microsoft FAT specification and shares zero code with the locator
(sys/arch/rp2040/dev/fat32tail.c) or with the msdosfs tool that wrote the
images. Images are mapped, not read, so a 2 GiB sparse volume costs only
the sectors the oracle touches.

check     the whole volume is consistent (fat_reader's checks): FAT copies,
          chains, long names, lost and cross-linked clusters, file bytes.
classify  where DISCOBSD.UF2 is found (the root directory first, then each
          subdirectory of it in directory order, by long or short name in
          any case) and how its cluster chain lies: prints "absent",
          "contiguous" or "fragmented".
extent    prints "LBA NSECTORS" for the tail of that file as the layout
          defines it (head = N * 512 rounded up to 32768 bytes, N from the
          first UF2 block) and fails unless the bytes of those sectors equal
          ROOT_IMAGE and the tail ends at the file's last byte.
"""

from __future__ import annotations

import mmap
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "msdosfs_core"))
import fat_reader  # noqa: E402

TARGET = "discobsd.uf2"
HEAD_ALIGN = 32768


def open_volume(path: str, lba: int):
    handle = open(path, "rb")  # noqa: SIM115 - mapped for the life of the process
    mapped = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
    data = memoryview(mapped)
    vol = fat_reader.parse(data[lba * 512 :])
    if vol.fat_type != 32:
        # FAT12 and FAT16 volumes in the gate are small, and their fixed root
        # region needs bytes, not a view.
        vol.data = bytes(vol.data[: vol.total * vol.bps])
    return data, vol


def directory_entries(vol, start: int, label: str):
    """Yield (name, short, attr, first cluster, size) of the live entries."""
    owners: dict[int, str] = {}
    clusters = fat_reader.chain(vol, start, label, owners)
    raw = b"".join(vol.cluster(n) for n in clusters)
    yield from fat_reader.entries(raw, label)


def find_target(vol):
    """The (path, first cluster, size) of DISCOBSD.UF2, or None."""
    subdirs = []
    for name, _short, attr, first, size in directory_entries(vol, vol.root_cluster, "/"):
        if attr & 0x10:
            subdirs.append((name, first))
        elif name.lower() == TARGET:
            return "/" + name, first, size
    for dirname, first in subdirs:
        for name, short, attr, start, size in directory_entries(vol, first, dirname):
            if short in (b".          ", b"..         "):
                continue
            if not attr & 0x10 and name.lower() == TARGET:
                return f"/{dirname}/{name}", start, size
    return None


def file_chain(vol, first: int) -> list[int]:
    out = [first]
    while True:
        nxt = vol.entry(out[-1])
        if vol.is_eoc(nxt):
            return out
        if not 2 <= nxt < vol.clusters + 2 or len(out) > vol.clusters:
            raise fat_reader.Bad(f"chain from {first} leaves the volume or loops")
        out.append(nxt)


def cmd_check(path: str, lba: int) -> int:
    _data, vol = open_volume(path, lba)
    fat_reader.check(vol, [], {})
    return 0


def cmd_classify(path: str, lba: int) -> int:
    _data, vol = open_volume(path, lba)
    found = find_target(vol)
    if found is None:
        print("absent")
        return 0
    _path, first, size = found
    chain = file_chain(vol, first)
    needed = -(-size // vol.cluster_bytes)
    contiguous = chain[:needed] == list(range(first, first + needed))
    print("contiguous" if contiguous else "fragmented")
    return 0


def cmd_extent(path: str, lba: int, root_path: str) -> int:
    data, vol = open_volume(path, lba)
    found = find_target(vol)
    if found is None:
        raise fat_reader.Bad("no DISCOBSD.UF2")
    _path, first, size = found
    needed = -(-size // vol.cluster_bytes)
    if file_chain(vol, first)[:needed] != list(range(first, first + needed)):
        raise fat_reader.Bad("DISCOBSD.UF2 is fragmented")
    file_sector = lba + vol.first_data + (first - 2) * vol.spc
    (blocks,) = struct.unpack_from("<I", data, file_sector * 512 + 24)
    head = -(-(blocks * 512) // HEAD_ALIGN) * HEAD_ALIGN
    root = Path(root_path).read_bytes()
    if size - head != len(root):
        raise fat_reader.Bad(f"tail holds {size - head} bytes, root image {len(root)}")
    tail_sector = file_sector + head // 512
    if bytes(data[tail_sector * 512 : tail_sector * 512 + len(root)]) != root:
        raise fat_reader.Bad("tail bytes differ from the root image")
    print(tail_sector, len(root) // 512)
    return 0


def main(argv: list[str]) -> int:
    commands = {"check": (cmd_check, 2), "classify": (cmd_classify, 2), "extent": (cmd_extent, 3)}
    if not argv or argv[0] not in commands or len(argv) != commands[argv[0]][1] + 1:
        raise SystemExit(__doc__)
    function, _count = commands[argv[0]]
    args: list = [argv[1], int(argv[2])] + argv[3:]
    try:
        return function(*args)
    except fat_reader.Bad as error:
        print(f"FAIL {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
