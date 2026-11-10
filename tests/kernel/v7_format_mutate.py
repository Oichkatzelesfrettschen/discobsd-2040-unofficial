"""Write mutated copies of the kernel sources for v7_format_check.sh.

Usage: v7_format_mutate.py SYS OUT

Each mutant is a copy of the sys/ files the gate compiles, under OUT/NAME,
with one exact replacement that breaks one claim of the V7 format design.
A replacement whose text is absent or repeated stops the run, so a mutant
never silently equals the original.
"""

import sys
from pathlib import Path

MUTANTS = {
    # A V7 block is one half of a buffer; this reads the first half always.
    "half_buffer_address": (
        "sys/fsgeom.h",
        "#define FSBADDR(fs, bp, b)      ((bp)->b_addr + \\\n"
        "        (((int)(b) & ((1 << FS_DBSHIFT(fs)) - 1)) << FS_BSHIFT(fs)))",
        "#define FSBADDR(fs, bp, b)      ((bp)->b_addr)",
    ),
    # rwip()'s copy of the same view.
    "file_data_address": (
        "sys/fsgeom.h",
        "#define IN_BADDR(ip, bp, b)     ((bp)->b_addr + (((int)(b) & \\\n"
        "        ((1 << (DEV_BSHIFT - IN_BSHIFT(ip))) - 1)) << IN_BSHIFT(ip)))",
        "#define IN_BADDR(ip, bp, b)     ((bp)->b_addr + (0 & (int)(b)))",
    ),
    # Ten direct addresses, not the native four.
    "direct_count": (
        "kern/ufs_v7.c",
        "[FS_FORMAT_V7]     = { 9, 10, 1, 50, FS_FORMAT_V7 },",
        "[FS_FORMAT_V7]     = { 9, 4, 1, 50, FS_FORMAT_V7 },",
    ),
    # A free-chain block holds 50 addresses.
    "free_chunk": (
        "kern/ufs_v7.c",
        "[FS_FORMAT_V7]     = { 9, 10, 1, 50, FS_FORMAT_V7 },",
        "[FS_FORMAT_V7]     = { 9, 10, 1, 49, FS_FORMAT_V7 },",
    ),
    # Addresses are little-endian on the volume.
    "address_byte_order": (
        "kern/ufs_v7.c",
        "    return (p[0] | (u_long)p[1] << 8 | (u_long)p[2] << 16);",
        "    return (p[2] | (u_long)p[1] << 8 | (u_long)p[0] << 16);",
    ),
    # A wide address must come back from the disk inode.
    "escape_not_stored": (
        "kern/ufs_v7.c",
        "    put24(raw + DI_ADDR + 3 * i, bn);\n    bdwrite(bp);",
        "    brelse(bp);",
    ),
    # A directory's size is doubled in core and halved on the volume.
    "directory_size": (
        "kern/ufs_v7.c",
        "    put32(raw + DI_SIZE, type == IFDIR ? ip->i_size >> 1 : ip->i_size);",
        "    put32(raw + DI_SIZE, ip->i_size);",
    ),
    # A live V7 record has no free space for a second name.
    "record_footprint": (
        "sys/fsgeom.h",
        "#define DIRFOOT(ip, ep)         (FS_ISV7(INODE_FILESYSTEM(ip)) ? \\\n"
        "        (int)(ep)->d_reclen : (int)DIRSIZ(ep))",
        "#define DIRFOOT(ip, ep)         ((int)DIRSIZ(ep))",
    ),
    # bmap() learns the block balloc() returned from the mount record.
    "allocation_block_lost": (
        "kern/ufs_alloc.c",
        "    fs_mount(fs)->m_lastalloc = bno;",
        "    fs_mount(fs)->m_lastalloc = bno ^ 1;",
    ),
    # An address outside the data area is refused where bmap() reads it.
    "address_unchecked": (
        "sys/fsgeom.h",
        "#define FS_BADADDR(fs, bn)      (FS_ISV7(fs) && \\\n"
        "        ((u_long)(bn) < (fs)->fs_isize || (u_long)(bn) >= (fs)->fs_fsize))",
        "#define FS_BADADDR(fs, bn)      (0 & (int)(bn))",
    ),
    # A wide address that cannot be read back is not a hole.
    "escape_failure_is_hole": (
        "kern/ufs_v7.c",
        "    if (raw == NULL)\n        return ((daddr_t)-1);",
        "    if (raw == NULL)\n        return (0);",
    ),
    # A short transfer is judged by the part the caller selected.
    "whole_buffer_residual": (
        "sys/fsgeom.h",
        "#define FSRESID(fs, bp, b)      \\\n"
        "        fs_part_resid((bp)->b_resid, FS_BSHIFT(fs), (b))",
        "#define FSRESID(fs, bp, b)      ((bp)->b_resid + (0 & (int)(b)))",
    ),
    # An inode number is checked against the inode table.
    "inode_number_unchecked": (
        "sys/fsgeom.h",
        "#define FS_BADINO(fs, ino)      (FS_ISV7(fs) && v7_badino((fs), (ino)))",
        "#define FS_BADINO(fs, ino)      (0 & (int)(ino))",
    ),
    # ialloc() refuses an owner the format cannot hold.
    "wide_owner_admitted": (
        "sys/fsgeom.h",
        "#define FS_WIDE16(fs, v)        (FS_ISV7(fs) && (u_long)(v) > 0xffff)",
        "#define FS_WIDE16(fs, v)        (0 & (int)(v))",
    ),
}

FILES = (
    "sys/fsgeom.h",
    "kern/ufs_v7.c",
    "kern/ufs_alloc.c",
)


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    source, out = Path(argv[1]), Path(argv[2])
    out.mkdir(parents=True)
    for name, (path, old, new) in MUTANTS.items():
        root = out / name
        # The gate compiles with -I ROOT, so the whole sys/ tree is needed;
        # symlink everything and replace only the mutated file.
        for part in ("kern", "sys"):
            (root / part).mkdir(parents=True)
            for entry in (source / part).iterdir():
                (root / part / entry.name).symlink_to(entry)
        for entry in source.iterdir():
            if entry.name not in ("kern", "sys"):
                (root / entry.name).symlink_to(entry)
        text = (source / path).read_text()
        if text.count(old) != 1:
            raise SystemExit(f"mutant {name}: {text.count(old)} matches in {path}")
        target = root / path
        target.unlink()
        target.write_text(text.replace(old, new))
        if path not in FILES:
            raise SystemExit(f"mutant {name}: {path} is not a file the gate tracks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
