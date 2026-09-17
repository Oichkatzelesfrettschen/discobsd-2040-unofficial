#!/usr/bin/env python3
"""Build a small RK05 pack for the pdp11 emulator from a stock V6 root pack.

usage: mkv6pack.py [-o OUT] [-f FSIZE] [-b BOOT] [-s SWPLO NSWAP] SRC LIST

SRC is a V6 root pack such as TUHS's Dennis_v6/v6root (block 1 is the
superblock; block 0 may or may not hold a bootstrap). LIST names the files
to carry over, one path per line, or `SRC DST` to carry SRC under the name
DST; a directory line carries the directory itself, `b PATH MAJOR MINOR`
or `c PATH MAJOR MINOR` makes a device node, and `#` starts a comment. The output pack holds:

  block 0        the bootstrap text of SRC's /usr/mdec/rkuboot (or BOOT)
  block 1        superblock for FSIZE blocks
  inode list     as many blocks as the file count needs
  data           the listed files, then the free list chained through
                 the remaining blocks

Every block the output never writes stays a hole in the output file, so a
pack of FSIZE logical blocks costs the host only its content. With -s the
kernel's swplo and nswap words are patched so the guest swaps inside a
small range just above the file system instead of at block 4000.
"""
import struct
import sys

BS = 512
IALLOC = 0o100000
IFMT = 0o60000
IFDIR = 0o40000
IFCHR = 0o20000
IFBLK = 0o60000
ILARG = 0o10000
NADDR = 8
NICINOD = 100
NICFREE = 100


class SrcFS:
    def __init__(self, data):
        self.d = data
        self.isize, self.fsize = struct.unpack_from("<HH", data, BS)

    def block(self, n):
        return self.d[n * BS:(n + 1) * BS]

    def inode(self, i):
        off = 2 * BS + (i - 1) * 32
        mode, nlink, uid, gid, s0, s1 = struct.unpack_from("<HBBBBH", self.d, off)
        addr = struct.unpack_from("<8H", self.d, off + 8)
        return mode, nlink, uid, gid, (s0 << 16) | s1, addr

    def blocks_of(self, mode, size, addr):
        nb = (size + BS - 1) // BS
        if not mode & ILARG:
            return list(addr)[:nb]
        out = []
        for k, a in enumerate(addr):
            if len(out) >= nb:
                break
            if a == 0:
                out += [0] * 256
            elif k == 7:
                for b2 in struct.unpack("<256H", self.block(a)):
                    out += [0] * 256 if b2 == 0 else list(struct.unpack("<256H", self.block(b2)))
            else:
                out += list(struct.unpack("<256H", self.block(a)))
        return out[:nb]

    def read(self, i):
        mode, _, _, _, size, addr = self.inode(i)
        data = b"".join(bytes(self.block(b)) if b else b"\0" * BS
                        for b in self.blocks_of(mode, size, addr))
        return data[:size]

    def listdir(self, i):
        data = self.read(i)
        for off in range(0, len(data), 16):
            (n,) = struct.unpack_from("<H", data, off)
            name = data[off + 2:off + 16].split(b"\0")[0].decode("latin1")
            if n:
                yield n, name

    def lookup(self, path):
        i = 1
        for part in path.strip("/").split("/"):
            if not part:
                continue
            for n, name in self.listdir(i):
                if name == part:
                    i = n
                    break
            else:
                raise KeyError(path)
        return i


class Node:
    def __init__(self, mode, uid, gid, data=b"", major=0, minor=0):
        self.mode = mode
        self.uid = uid
        self.gid = gid
        self.data = data
        self.major = major
        self.minor = minor
        self.ino = 0
        self.children = {}      # directories: name -> Node
        self.nlink = 0


def load_list(src, listfile):
    root = Node(IALLOC | IFDIR | 0o777, 0, 0)
    root.ino = 1

    def ensure_dir(path):
        node = root
        cur = ""
        for part in path.strip("/").split("/"):
            if not part:
                continue
            cur += "/" + part
            if part not in node.children:
                try:
                    i = src.lookup(cur)
                    mode, _, uid, gid, _, _ = src.inode(i)
                except KeyError:
                    mode, uid, gid = IALLOC | IFDIR | 0o777, 0, 0
                node.children[part] = Node(mode | IALLOC, uid, gid)
            node = node.children[part]
        return node

    for line in open(listfile):
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.startswith("b ") or line.startswith("c "):
            kind, path, major, minor = line.split()
            parent = ensure_dir("/".join(path.split("/")[:-1]))
            parent.children[path.split("/")[-1]] = Node(
                IALLOC | (IFBLK if kind == "b" else IFCHR) | 0o644, 0, 0,
                major=int(major), minor=int(minor))
            continue
        srcpath, _, dst = line.partition(" ")
        dst = dst.strip() or srcpath
        i = src.lookup(srcpath)
        mode, _, uid, gid, size, addr = src.inode(i)
        kind = mode & IFMT
        if kind == IFDIR:
            ensure_dir(dst)
            continue
        parent = ensure_dir("/".join(dst.split("/")[:-1]))
        name = dst.split("/")[-1]
        if kind in (IFCHR, IFBLK):
            node = Node(mode, uid, gid, major=addr[0] >> 8, minor=addr[0] & 0xFF)
        else:
            node = Node(mode, uid, gid, data=src.read(i))
        parent.children[name] = node
    return root


class Builder:
    def __init__(self, fsize):
        self.fsize = fsize
        self.blocks = {}        # block number -> bytes
        self.nodes = []
        self.next_data = 0

    def assign_inodes(self, root):
        order = []

        def visit(node):
            node.ino = len(order) + 1
            order.append(node)
            for name in sorted(node.children):
                visit(node.children[name])
        visit(root)
        self.nodes = order
        self.isize = (len(order) * 32 + BS - 1) // BS
        self.next_data = 2 + self.isize

    def alloc(self):
        b = self.next_data
        self.next_data += 1
        if b >= self.fsize:
            raise SystemExit("mkv6pack: pack of %d blocks is full" % self.fsize)
        return b

    def put_data(self, data):
        """Store data, return the addr[] and mode flag for the inode."""
        nb = (len(data) + BS - 1) // BS
        blks = []
        for k in range(nb):
            chunk = data[k * BS:(k + 1) * BS]
            if chunk.strip(b"\0") == b"":
                blks.append(0)      # hole in the guest file too
                continue
            b = self.alloc()
            self.blocks[b] = chunk.ljust(BS, b"\0")
            blks.append(b)
        if nb <= NADDR:
            return blks + [0] * (NADDR - nb), 0
        addr = []
        for k in range(0, nb, 256):
            group = blks[k:k + 256]
            b = self.alloc()
            self.blocks[b] = struct.pack("<256H", *(group + [0] * (256 - len(group))))
            addr.append(b)
        if len(addr) > NADDR:
            raise SystemExit("mkv6pack: file needs double indirection")
        return addr + [0] * (NADDR - len(addr)), ILARG

    def build(self, root):
        self.assign_inodes(root)
        inodes = bytearray(self.isize * BS)
        for node in self.nodes:
            kind = node.mode & IFMT
            if kind == IFDIR:
                ents = [(node.ino, "."), (self.parent_ino(node, root), "..")]
                ents += [(c.ino, n) for n, c in sorted(node.children.items())]
                data = b"".join(struct.pack("<H", i) + n.encode("latin1").ljust(14, b"\0")
                                for i, n in ents)
                node.nlink = 2 + sum(1 for c in node.children.values()
                                     if (c.mode & IFMT) == IFDIR)
            elif kind in (IFCHR, IFBLK):
                data = b""
                node.nlink = 1
            else:
                data = node.data
                node.nlink = 1
            if kind in (IFCHR, IFBLK):
                addr, flag = [node.major << 8 | node.minor] + [0] * 7, 0
                size = 0
            else:
                addr, flag = self.put_data(data)
                size = len(data)
            off = (node.ino - 1) * 32
            struct.pack_into("<HBBBBH", inodes, off, (node.mode | flag) & 0xFFFF,
                             node.nlink, node.uid, node.gid, size >> 16, size & 0xFFFF)
            struct.pack_into("<8H", inodes, off + 8, *addr)
            struct.pack_into("<HHHH", inodes, off + 24, 0, 0, 0, 0)
        for k in range(self.isize):
            self.blocks[2 + k] = bytes(inodes[k * BS:(k + 1) * BS])
        self.free_list()
        self.superblock()

    def parent_ino(self, node, root):
        if node is root:
            return 1

        def find(cur):
            for c in cur.children.values():
                if c is node:
                    return cur.ino
                r = find(c)
                if r:
                    return r
            return 0
        return find(root)

    def free_list(self):
        """V6 free(): chain blocks of 100 numbers, highest block first, so
        the guest allocates low blocks first and the chain heads stay at
        the top of the pack."""
        self.nfree = 1
        self.free = [0] * NICFREE
        for b in range(self.fsize - 1, self.next_data - 1, -1):
            if self.nfree >= NICFREE:
                self.blocks[b] = struct.pack("<H100H", self.nfree, *self.free)
                self.nfree = 0
            self.free[self.nfree] = b
            self.nfree += 1

    def superblock(self):
        sb = bytearray(BS)
        struct.pack_into("<HHH", sb, 0, self.isize, self.fsize, self.nfree)
        struct.pack_into("<100H", sb, 6, *self.free)
        free_inodes = [i for i in range(len(self.nodes) + 1, self.isize * 16 + 1)][:NICINOD]
        struct.pack_into("<H", sb, 206, len(free_inodes))
        struct.pack_into("<%dH" % len(free_inodes), sb, 208, *free_inodes)
        self.blocks[1] = bytes(sb)


def patch_swap(data, swplo, nswap):
    """Find `int swplo 4000; int nswap 872;` adjacent in the kernel image and
    rewrite them; the pair is unique in a V6 rk kernel."""
    old = struct.pack("<HH", 4000, 872)
    n = data.count(old)
    if n != 1:
        raise SystemExit("mkv6pack: swplo/nswap pair found %d times" % n)
    return data.replace(old, struct.pack("<HH", swplo, nswap))


def main():
    args = sys.argv[1:]
    out = "v6.rk"
    fsize = 1900
    boot = None
    swap = None
    while args and args[0].startswith("-"):
        if args[0] == "-o":
            out = args[1]
            args = args[2:]
        elif args[0] == "-f":
            fsize = int(args[1])
            args = args[2:]
        elif args[0] == "-b":
            boot = args[1]
            args = args[2:]
        elif args[0] == "-s":
            swap = (int(args[1]), int(args[2]))
            args = args[3:]
        else:
            raise SystemExit(__doc__)
    if len(args) != 2:
        raise SystemExit(__doc__)
    src = SrcFS(open(args[0], "rb").read())
    root = load_list(src, args[1])
    if swap:
        for _name, node in list(root.children.items()):
            if node.data[:2] == b"\x07\x01":
                node.data = patch_swap(node.data, *swap)
    b = Builder(fsize)
    b.build(root)
    if boot:
        bootdata = open(boot, "rb").read()
    else:
        a = src.read(src.lookup("/usr/mdec/rkuboot"))
        tsize = struct.unpack_from("<H", a, 2)[0]
        bootdata = a[16:16 + tsize]
    b.blocks[0] = bootdata.ljust(BS, b"\0")[:BS]
    with open(out, "wb") as fh:
        for n in sorted(b.blocks):
            fh.seek(n * BS)
            fh.write(b.blocks[n])
        fh.truncate(fsize * BS)
    written = len(b.blocks)
    print("%s: %d inodes in %d blocks, %d of %d blocks written (%d KB), %d free"
          % (out, len(b.nodes), b.isize, written, fsize, written * BS // 1024,
             fsize - b.next_data))


if __name__ == "__main__":
    main()
