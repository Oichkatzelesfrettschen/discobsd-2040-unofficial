"""Independent FAT12/16/32 image reader for the usr.sbin/msdosfs gate.

usage: fat_reader.py IMAGE check [EXPECT]

Reads IMAGE from the on-disk layout the Microsoft FAT specification
(FAT: General Overview of On-Disk Format, version 1.03) defines, with no
code shared with sys/fs/msdosfs, and checks its consistency:

- the boot sector signature and a BPB whose geometry fits the image;
- every FAT copy holds the same bytes, and FAT[0] carries the media byte;
- every directory parses: each long-name chain has consecutive ordinals
  and the checksum of the short entry that ends it, "." and ".." of a
  subdirectory point at itself and at its parent (0 for the root);
- every cluster chain stays in range, ends in an end-of-chain mark, and
  has exactly the clusters its file size needs (a directory any number);
- no cluster belongs to two chains, and every allocated FAT entry belongs
  to some chain (no lost clusters);
- on FAT32, the FSInfo free count, when known, equals the free entries.

check prints one "path size" line per file and "path/" per directory,
depth first in directory order. With EXPECT, a file of "path=local" lines,
each named file's bytes must equal the local file's. A failure prints
"FAIL reason" and exits 1.
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path


class Bad(Exception):
    """The image violates the FAT on-disk format."""


@dataclass
class Volume:
    data: bytes
    bps: int
    spc: int
    reserved: int
    nfats: int
    root_entries: int
    total: int
    fat_sectors: int
    fat_type: int
    root_cluster: int
    fsinfo: int
    first_data: int
    clusters: int

    @property
    def cluster_bytes(self) -> int:
        return self.bps * self.spc

    def fat(self, copy: int = 0) -> bytes:
        start = (self.reserved + copy * self.fat_sectors) * self.bps
        return self.data[start : start + self.fat_sectors * self.bps]

    def entry(self, n: int) -> int:
        fat = self.fat()
        if self.fat_type == 12:
            pair = fat[n + n // 2] | fat[n + n // 2 + 1] << 8
            return pair >> 4 if n & 1 else pair & 0xFFF
        if self.fat_type == 16:
            return struct.unpack_from("<H", fat, 2 * n)[0]
        return struct.unpack_from("<I", fat, 4 * n)[0] & 0x0FFFFFFF

    def is_eoc(self, value: int) -> bool:
        return value >= {12: 0xFF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[self.fat_type]

    def cluster(self, n: int) -> bytes:
        start = (self.first_data + (n - 2) * self.spc) * self.bps
        return self.data[start : start + self.cluster_bytes]


def parse(data: bytes) -> Volume:
    if len(data) < 512 or data[510:512] != b"\x55\xaa":
        raise Bad("boot sector signature")
    bps, spc, reserved, nfats, root_entries, total16, _media, fat16 = struct.unpack_from(
        "<HBHBHHBH", data, 11
    )
    total32 = struct.unpack_from("<I", data, 32)[0]
    if bps not in (512, 1024, 2048, 4096) or spc == 0 or spc & (spc - 1):
        raise Bad(f"bytes per sector {bps}, sectors per cluster {spc}")
    if reserved == 0 or nfats == 0:
        raise Bad("reserved sectors or FAT count is 0")
    total = total16 or total32
    fat_sectors = fat16 or struct.unpack_from("<I", data, 36)[0]
    root_sectors = (root_entries * 32 + bps - 1) // bps
    first_data = reserved + nfats * fat_sectors + root_sectors
    if total * bps > len(data) or first_data >= total:
        raise Bad(f"geometry: {total} sectors in a {len(data)}-byte image")
    clusters = (total - first_data) // spc
    # The specification's rule: the cluster count alone decides the type.
    fat_type = 12 if clusters < 4085 else 16 if clusters < 65525 else 32
    root_cluster = fsinfo = 0
    if fat_type == 32:
        if root_entries != 0 or fat16 != 0:
            raise Bad("FAT32 with a fixed root or a 16-bit FAT size")
        root_cluster, fsinfo = struct.unpack_from("<IH", data, 44)
    elif root_entries == 0:
        raise Bad("FAT12/16 without root entries")
    need = (clusters + 2) * {12: 1.5, 16: 2, 32: 4}[fat_type]
    if fat_sectors * bps < need:
        raise Bad(f"FAT of {fat_sectors} sectors cannot map {clusters} clusters")
    return Volume(
        data, bps, spc, reserved, nfats, root_entries, total, fat_sectors,
        fat_type, root_cluster, fsinfo, first_data, clusters,
    )


def chain(vol: Volume, start: int, owner: str, owners: dict[int, str]) -> list[int]:
    out: list[int] = []
    n = start
    while True:
        if not 2 <= n < vol.clusters + 2:
            raise Bad(f"{owner}: cluster {n} out of range")
        if n in owners:
            raise Bad(f"{owner}: cluster {n} also belongs to {owners[n]}")
        owners[n] = owner
        out.append(n)
        nxt = vol.entry(n)
        if vol.is_eoc(nxt):
            return out
        # Free, reserved and bad-cluster values all fall outside 2..clusters+1.
        if not 2 <= nxt < vol.clusters + 2:
            raise Bad(f"{owner}: cluster {n} links to {nxt:#x}")
        n = nxt


def lfn_checksum(short: bytes) -> int:
    total = 0
    for byte in short:
        total = (((total & 1) << 7) | (total >> 1)) + byte & 0xFF
    return total


def short_name(raw: bytes, case: int) -> str:
    base = raw[:8].rstrip(b" ").decode("latin-1")
    ext = raw[8:11].rstrip(b" ").decode("latin-1")
    if raw[0] == 0x05:
        base = "\xe5" + base[1:]
    if case & 0x08:
        base = base.lower()
    if case & 0x10:
        ext = ext.lower()
    return base + ("." + ext if ext else "")


def entries(raw: bytes, where: str):
    """Yield (name, short, attributes, start, size) per live entry."""
    pending: list[tuple[int, int, str]] = []
    for off in range(0, len(raw), 32):
        e = raw[off : off + 32]
        if e[0] == 0x00:
            if pending:
                raise Bad(f"{where}: long name without a short entry")
            return
        if e[0] == 0xE5:
            pending = []
            continue
        attr = e[11]
        if attr & 0x3F == 0x0F:
            ordinal, csum = e[0], e[13]
            text = (e[1:11] + e[14:26] + e[28:32]).decode("utf-16-le")
            text = text.split("\x00", 1)[0]
            pending.append((ordinal, csum, text))
            continue
        if attr & 0x08:
            pending = []
            continue
        short = e[0:11]
        name = short_name(short, e[12])
        if pending:
            ordinals = [o & 0x3F for o, _, _ in pending]
            if not pending[0][0] & 0x40 or ordinals != list(range(len(pending), 0, -1)):
                raise Bad(f"{where}: long name ordinals {ordinals}")
            if any(c != lfn_checksum(short) for _, c, _ in pending):
                raise Bad(f"{where}: long name checksum for {name!r}")
            name = "".join(t for _, _, t in reversed(pending))
            pending = []
        start = struct.unpack_from("<H", e, 26)[0] | struct.unpack_from("<H", e, 20)[0] << 16
        size = struct.unpack_from("<I", e, 28)[0]
        yield name, short, attr, start, size


def walk(vol: Volume, out: list[str], files: dict[str, bytes]) -> dict[int, str]:
    owners: dict[int, str] = {}

    def directory(path: str, start: int, parent: int) -> None:
        if start == 0:
            if vol.fat_type == 32:
                raise Bad("FAT32 root at cluster 0")
            first = (vol.reserved + vol.nfats * vol.fat_sectors) * vol.bps
            raw = vol.data[first : first + vol.root_entries * 32]
        else:
            raw = b"".join(vol.cluster(n) for n in chain(vol, start, path or "/", owners))
        names: set[str] = set()
        for name, short, attr, first_cluster, size in entries(raw, path or "/"):
            if short in (b".          ", b"..         "):
                if path == "":
                    raise Bad("root holds a dot entry")
                want = start if short[1] == 0x20 else parent
                if first_cluster != want:
                    raise Bad(f"{path}: {short.decode().strip()} -> {first_cluster}, not {want}")
                continue
            if name.lower() in names:
                raise Bad(f"{path}: duplicate name {name!r}")
            names.add(name.lower())
            child = f"{path}/{name}"
            if attr & 0x10:
                if first_cluster == 0:
                    raise Bad(f"{child}: directory without a cluster")
                out.append(child + "/")
                directory(child, first_cluster, 0 if start == vol.root_cluster else start)
                continue
            out.append(f"{child} {size}")
            if first_cluster == 0:
                if size != 0:
                    raise Bad(f"{child}: {size} bytes and no cluster")
                files[child] = b""
                continue
            clusters = chain(vol, first_cluster, child, owners)
            if len(clusters) != -(-size // vol.cluster_bytes):
                raise Bad(f"{child}: {len(clusters)} clusters for {size} bytes")
            files[child] = b"".join(vol.cluster(n) for n in clusters)[:size]

    directory("", vol.root_cluster if vol.fat_type == 32 else 0, 0)
    return owners


def check(vol: Volume, out: list[str], files: dict[str, bytes]) -> None:
    for copy in range(1, vol.nfats):
        if vol.fat(copy) != vol.fat():
            raise Bad(f"FAT copy {copy} differs from FAT 0")
    if vol.fat()[0] != vol.data[21]:
        raise Bad("FAT[0] does not carry the media byte")
    owners = walk(vol, out, files)
    allocated = {n for n in range(2, vol.clusters + 2) if vol.entry(n) != 0}
    lost = sorted(allocated - set(owners))
    if lost:
        raise Bad(f"lost clusters {lost[:8]}")
    free = vol.clusters - len(allocated)
    if vol.fat_type == 32 and vol.fsinfo not in (0, 0xFFFF):
        info = vol.data[vol.fsinfo * vol.bps : (vol.fsinfo + 1) * vol.bps]
        if info[0:4] != b"RRaA" or info[484:488] != b"rrAa":
            raise Bad("FSInfo signatures")
        recorded = struct.unpack_from("<I", info, 488)[0]
        if recorded != 0xFFFFFFFF and recorded != free:
            raise Bad(f"FSInfo free count {recorded}, FAT has {free}")
    out.append(f"fat {vol.fat_type} clusters {vol.clusters} free {free}")


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3) or argv[1] != "check":
        raise SystemExit(__doc__)
    out: list[str] = []
    files: dict[str, bytes] = {}
    try:
        vol = parse(Path(argv[0]).read_bytes())
        check(vol, out, files)
        if len(argv) == 3:
            for line in Path(argv[2]).read_text(encoding="utf-8").splitlines():
                path, local = line.split("=", 1)
                if files.get(path) != Path(local).read_bytes():
                    raise Bad(f"{path}: contents differ from {local}")
    except Bad as error:
        print(f"FAIL {error}")
        return 1
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
