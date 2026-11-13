"""Read bounded V7 images independently of the production allocation helpers.

The parser uses byte offsets from the Seventh Edition layouts. It walks all
allocated data/index trees and free-list entries, then traverses directory
reachability and counts every directory reference, including dot entries.
The reserved bad-block inode is the sole reachability exception. Sparse file
data reads as zero; directories require every byte to have an allocated block.
"""

import hashlib
from collections import Counter
from dataclasses import dataclass


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


@dataclass(frozen=True)
class Report:
    free_blocks: int
    free_inodes: int
    allocated_inodes: int
    namespace: dict
    link_counts: dict
    sizes: dict
    payload_hashes: dict
    inode_metadata: dict


class Reader:
    def __init__(self, image, order):
        require(order in ("little", "pdp"), "unsupported V7 byte order")
        require(len(image) >= 3 * 512 and len(image) % 512 == 0, "whole-block image required")
        self.image = image
        self.order = order
        self.start = self.number(512, 2)
        self.volume = self.number(514, 4)
        require(2 < self.start < self.volume <= len(image) // 512, "superblock bounds")
        self.inode_count = (self.start - 2) * 8
        require(self.inode_count <= 65535, "inode table exceeds 16-bit namespace")
        self.owners = {}
        self.inodes = {}
        self.maps = {}
        self.attributes = {}

    def number(self, offset, width):
        require(0 <= offset <= len(self.image) and width <= len(self.image) - offset,
                "field bounds")
        raw = self.image[offset:offset + width]
        if self.order == "pdp" and width == 3:
            return (raw[0] << 16) | raw[1] | (raw[2] << 8)
        if self.order == "pdp" and width == 4:
            return int.from_bytes(raw[:2], "little") * 65536 + int.from_bytes(raw[2:], "little")
        return int.from_bytes(raw, "little")

    def claim(self, block, owner):
        require(self.start <= block < self.volume, f"block bounds: {block}")
        require(block not in self.owners,
                f"multiply owned block {block}: {self.owners.get(block)} and {owner}")
        self.owners[block] = owner

    def indirect(self, inode, block, level, logical_start, required, mapping):
        self.claim(block, (inode, "index", level))
        require(logical_start < required, f"inode {inode}: index exceeds file size")
        span = 128 ** (level - 1)
        for index in range(128):
            child = self.number(block * 512 + index * 4, 4)
            if child == 0:
                continue
            logical = logical_start + index * span
            if level == 1:
                self.claim(child, (inode, "data", logical))
                require(logical < required, f"inode {inode}: data address exceeds file size")
                mapping[logical] = child
            else:
                self.indirect(inode, child, level - 1, logical, required, mapping)

    def read_inodes(self):
        for inode in range(1, self.inode_count + 1):
            offset = 1024 + (inode - 1) * 64
            mode = self.number(offset, 2)
            if mode == 0:
                continue
            links = self.number(offset + 2, 2)
            links = links - 65536 if links >= 32768 else links
            size = self.number(offset + 8, 4)
            require(size <= len(self.image), "file size exceeds bounded oracle range")
            file_type = mode & 0o170000
            require(file_type in (0o100000, 0o040000, 0o120000, 0o020000, 0o060000),
                    f"inode {inode}: unsupported type in bounded oracle")
            self.inodes[inode] = (file_type, links, size)
            self.attributes[inode] = (
                mode, self.number(offset + 4, 2), self.number(offset + 6, 2),
                tuple(self.number(offset + 12 + index * 3, 3) for index in range(13)),
                tuple(self.number(offset + time_offset, 4) for time_offset in (52, 56, 60)),
            )
            mapping = {}
            self.maps[inode] = mapping
            if file_type in (0o020000, 0o060000):
                continue
            required = (size + 511) // 512
            for index in range(10):
                block = self.number(offset + 12 + index * 3, 3)
                if block:
                    self.claim(block, (inode, "data", index))
                    require(index < required, f"inode {inode}: data address exceeds file size")
                    mapping[index] = block
            logical = 10
            for level in range(1, 4):
                block = self.number(offset + 12 + (9 + level) * 3, 3)
                if block:
                    self.indirect(inode, block, level, logical, required, mapping)
                logical += 128 ** level
            if file_type == 0o040000:
                require(size >= 32 and size % 16 == 0, f"inode {inode}: directory size")
                require(set(mapping) == set(range(required)), f"inode {inode}: directory hole")

    def read_free_list(self):
        cached = self.number(512 + 6, 2)
        require(cached <= 50, "free block cache count")
        entries = [self.number(512 + 8 + index * 4, 4) for index in range(cached)]
        free = 0
        while entries:
            link = entries[0]
            for block in entries[1:]:
                self.claim(block, ("free",))
                free += 1
            if link == 0:
                break
            self.claim(link, ("free-list",))
            free += 1
            cached = self.number(link * 512, 2)
            require(1 <= cached <= 50, "free-list block count")
            entries = [self.number(link * 512 + 2 + index * 4, 4) for index in range(cached)]
        require(free == self.number(512 + 418, 4), "free block total differs")
        for block in range(self.start, self.volume):
            require(block in self.owners, f"unowned data block {block}")
        return free

    def read_free_inodes(self):
        free = self.inode_count - len(self.inodes)
        require(free == self.number(512 + 422, 2), "free inode total differs")
        cached = self.number(512 + 208, 2)
        require(cached <= 100, "free inode cache count")
        entries = [self.number(512 + 210 + index * 2, 2) for index in range(cached)]
        require(len(set(entries)) == len(entries), "duplicate free inode cache entry")
        require(all(1 <= inode <= self.inode_count and inode not in self.inodes
                    for inode in entries), "free inode cache names an allocated or invalid inode")
        return free

    def payload(self, inode):
        size = self.inodes[inode][2]
        output = bytearray(size)
        for logical, block in self.maps[inode].items():
            length = min(512, size - logical * 512)
            output[logical * 512:logical * 512 + length] = self.image[
                block * 512:block * 512 + length]
        return bytes(output)

    def directories(self):
        entries = {}
        references = Counter()
        for inode, (file_type, _links, _size) in self.inodes.items():
            if file_type != 0o040000:
                continue
            raw = self.payload(inode)
            directory = {}
            for offset in range(0, len(raw), 16):
                child = int.from_bytes(raw[offset:offset + 2], "little")
                if child == 0:
                    continue
                name = raw[offset + 2:offset + 16].split(b"\0", 1)[0]
                require(child in self.inodes, f"directory reaches unallocated inode {child}")
                require(bool(name) and b"/" not in name and name not in directory,
                        "invalid or duplicate directory name")
                directory[name] = child
                references[child] += 1
            require(directory.get(b".") == inode, f"inode {inode}: dot entry differs")
            require(b".." in directory, f"inode {inode}: parent entry missing")
            entries[inode] = directory
        require(2 in entries, "root directory missing")
        namespace = {"/": 2}
        reached = {2}
        parents = {2: 2}
        queue = [(2, "/")]
        for inode, path in queue:
            require(entries[inode][b".."] == parents[inode], f"inode {inode}: parent entry differs")
            for name, child in entries[inode].items():
                if name in (b".", b".."):
                    continue
                child_path = path.rstrip("/") + "/" + name.decode("latin1")
                namespace[child_path] = child
                reached.add(child)
                if child in entries:
                    require(child not in parents, "directory cycle or multiple parents")
                    parents[child] = inode
                    queue.append((child, child_path))
        require(set(self.inodes) - {1} == reached, "unreachable inode")
        for inode, (_file_type, links, _size) in self.inodes.items():
            require(links == references[inode], f"inode {inode}: link count differs")
        return namespace


def verify(image, order="little"):
    reader = Reader(image, order)
    reader.read_inodes()
    free_blocks = reader.read_free_list()
    free_inodes = reader.read_free_inodes()
    namespace = reader.directories()
    return Report(free_blocks, free_inodes, len(reader.inodes), namespace,
                  {inode: values[1] for inode, values in reader.inodes.items()},
                  {inode: values[2] for inode, values in reader.inodes.items()},
                  {path: hashlib.sha256(reader.payload(inode)).hexdigest()
                   for path, inode in namespace.items()}, reader.attributes)
