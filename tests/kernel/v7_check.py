"""Check a little-endian Seventh Edition volume image for structural consistency.

The checker shares no code with the kernel's inode layer or with the NetBSD
core in usr.sbin/v7fs. It reads the superblock, every inode and the free
chain from the image bytes and holds these claims:

- every block an inode or an indirect block names lies in the data area and
  is named once;
- an inode names no block past the last one its size needs;
- the free chain is well formed, repeats no block and names no block in use;
- used and free blocks together are exactly the data area, and the
  superblock's free count equals the chain's length;
- every directory entry names an allocated inode, every directory holds "."
  and "..", and each inode's link count equals the entries naming it.

Usage: v7_check.py IMAGE [--expect-free N]
       v7_check.py --self-test IMAGE
"""

import struct
import sys

BSIZE = 512
NICFREE = 50
IFMT, IFDIR, IFREG, IFCHR, IFBLK = 0o170000, 0o040000, 0o100000, 0o020000, 0o060000
IFLNK = 0o120000  # a symbolic link, stored as a file (2BSD and NetBSD extension)


class Damaged(Exception):
    """The image breaks a structural claim."""


def block(image, number):
    start = number * BSIZE
    if number < 0 or start + BSIZE > len(image):
        raise Damaged(f"block {number} lies outside the image")
    return image[start : start + BSIZE]


def addr24(raw):
    return raw[0] | raw[1] << 8 | raw[2] << 16


def inode_blocks(image, addrs, size, isize, fsize, claim):
    """Return the data blocks of one inode in file order, claiming every block."""
    need = (size + BSIZE - 1) // BSIZE
    data = []

    def take(number, what):
        if not isize <= number < fsize:
            raise Damaged(f"{what} names block {number} outside the data area")
        claim(number, what)

    def walk(number, depth, what):
        take(number, what)
        if depth == 0:
            data.append(number)
            return
        entries = struct.unpack("<128I", block(image, number))
        for entry in entries:
            if entry == 0:
                if depth == 1:
                    data.append(0)
                else:
                    data.extend([0] * 128 ** (depth - 1))
            else:
                walk(entry, depth - 1, what)

    for index in range(10):
        if addrs[index]:
            take(addrs[index], "direct")
            data.append(addrs[index])
        else:
            data.append(0)
    for level in range(3):
        root = addrs[10 + level]
        if root:
            walk(root, level + 1, f"indirect level {level + 1}")
        else:
            data.extend([0] * 128 ** (level + 1))
        if len(data) >= need and not any(addrs[11 + level :]):
            break
    for index, number in enumerate(data):
        if number and index >= need:
            raise Damaged(f"block {number} lies past the file's size")
    return data[:need]


def check(image, expect_free=None):
    superblock = block(image, 1)
    isize, fsize, nfree = struct.unpack_from("<HIh", superblock, 0)
    free_cache = struct.unpack_from("<50I", superblock, 8)
    ninode = struct.unpack_from("<h", superblock, 208)[0]
    tfree = struct.unpack_from("<I", superblock, 418)[0]
    if not (2 < isize < fsize and fsize * BSIZE <= len(image)):
        raise Damaged(f"superblock sizes {isize}, {fsize} do not describe the image")
    if not (0 <= nfree <= NICFREE and 0 <= ninode <= 100):
        raise Damaged(f"superblock counts {nfree}, {ninode} out of range")

    owner = {}

    def claim(number, what):
        if number in owner:
            raise Damaged(f"block {number} is named twice ({owner[number]}, {what})")
        owner[number] = what

    inodes = {}
    for ino in range(1, (isize - 2) * 8 + 1):
        raw = block(image, 2 + (ino - 1) // 8)[(ino - 1) % 8 * 64 :][:64]
        mode, nlink, _uid, _gid, size = struct.unpack_from("<HhHHI", raw, 0)
        if mode == 0:
            continue
        kind = mode & IFMT
        addrs = [addr24(raw[12 + 3 * i :]) for i in range(13)]
        blocks = []
        if kind in (IFDIR, IFREG, IFLNK):
            blocks = inode_blocks(
                image, addrs, size, isize, fsize, lambda n, w, i=ino: claim(n, f"inode {i} {w}")
            )
        elif kind not in (IFCHR, IFBLK):
            raise Damaged(f"inode {ino} has unknown type {kind:o}")
        inodes[ino] = (kind, nlink, size, blocks)

    free = []
    count, cache = nfree, list(free_cache[:nfree])
    while count:
        link = cache[0]
        for number in cache[1:count]:
            if not isize <= number < fsize:
                raise Damaged(f"free chain names block {number} outside the data area")
            free.append(number)
        if link == 0:
            break
        if not isize <= link < fsize:
            raise Damaged(f"free chain links to block {link} outside the data area")
        free.append(link)
        raw = block(image, link)
        count = struct.unpack_from("<h", raw, 0)[0]
        if not 0 < count <= NICFREE:
            raise Damaged(f"free-chain block {link} holds count {count}")
        cache = list(struct.unpack_from("<50I", raw, 2)[:count])
        if len(free) > fsize:
            raise Damaged("free chain does not end")
    if len(set(free)) != len(free):
        raise Damaged("free chain repeats a block")
    both = set(free) & set(owner)
    if both:
        raise Damaged(f"block {min(both)} is free and in use ({owner[min(both)]})")
    missing = fsize - isize - len(free) - len(owner)
    if missing:
        raise Damaged(f"{missing} data blocks are neither free nor in use")
    if tfree != len(free):
        raise Damaged(f"superblock counts {tfree} free blocks, the chain holds {len(free)}")
    if expect_free is not None and len(free) != expect_free:
        raise Damaged(f"{len(free)} blocks free, expected {expect_free}")

    links = dict.fromkeys(inodes, 0)
    for ino, (kind, _nlink, size, blocks) in inodes.items():
        if kind != IFDIR:
            continue
        if size % 16:
            raise Damaged(f"directory {ino} has size {size}")
        names = set()
        for index in range(size // 16):
            number = blocks[index // 32]
            if number == 0:
                raise Damaged(f"directory {ino} has a hole")
            entry = block(image, number)[index % 32 * 16 :][:16]
            target = struct.unpack_from("<H", entry, 0)[0]
            if target == 0:
                continue
            name = entry[2:].split(b"\0", 1)[0]
            if target not in inodes:
                raise Damaged(f"directory {ino} entry {name!r} names free inode {target}")
            if name in names:
                raise Damaged(f"directory {ino} holds {name!r} twice")
            names.add(name)
            links[target] += 1
        if not {b".", b".."} <= names:
            raise Damaged(f"directory {ino} lacks . or ..")
    for ino, (_kind, nlink, _size, _blocks) in inodes.items():
        if ino == 1:
            continue
        if links[ino] != nlink:
            raise Damaged(f"inode {ino} has link count {nlink} and {links[ino]} entries")
    return len(inodes), len(owner), len(free)


def self_test(image):
    """Damage a sound image five ways; each must be reported."""
    check(image)
    superblock = BSIZE
    isize = struct.unpack_from("<H", image, superblock)[0]
    root = 2 * BSIZE + 64
    first = addr24(image[root + 12 :])
    damages = {
        "free count": lambda b: struct.pack_into(
            "<I", b, superblock + 418, struct.unpack_from("<I", b, superblock + 418)[0] + 1
        ),
        "free block in use": lambda b: struct.pack_into("<I", b, superblock + 12, first),
        "link count": lambda b: struct.pack_into(
            "<h", b, root + 2, struct.unpack_from("<h", b, root + 2)[0] + 1
        ),
        "address outside the data area": lambda b: b.__setitem__(
            slice(root + 12, root + 15), bytes([isize - 1, 0, 0])
        ),
        "entry naming a free inode": lambda b: struct.pack_into(
            "<H", b, first * BSIZE + 32, (isize - 2) * 8
        ),
    }
    for label, damage in damages.items():
        copy = bytearray(image)
        damage(copy)
        try:
            check(bytes(copy))
        except Damaged:
            print(f"PASS v7-check rejects: {label}")
            continue
        raise Damaged(f"self-test: damaged image accepted ({label})")


def main(argv):
    if len(argv) == 3 and argv[1] == "--self-test":
        self_test(open(argv[2], "rb").read())
        return 0
    if len(argv) not in (2, 4) or (len(argv) == 4 and argv[2] != "--expect-free"):
        print(__doc__, file=sys.stderr)
        return 2
    expect_free = int(argv[3]) if len(argv) == 4 else None
    inodes, used, free = check(open(argv[1], "rb").read(), expect_free)
    print(f"PASS v7-check {argv[1]}: {inodes} inodes, {used} blocks in use, {free} free")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (OSError, Damaged, struct.error) as error:
        print(f"v7-check: {error}", file=sys.stderr)
        sys.exit(1)
