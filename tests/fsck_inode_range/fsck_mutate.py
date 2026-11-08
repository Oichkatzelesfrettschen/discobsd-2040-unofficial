#!/usr/bin/env python3
"""Restore the pre-change code of the gate's programs, one site at a time.

The programs are sbin/fsck, tools/fsutil, bin/df and sbin/mkfs.

Each mutation rewrites a site back to the form it had before its fix, in a
copy of the source file: a comparison against imax to the form that excluded
the highest inode, or a transfer of the inode window to the full NINOBLK
blocks. The gate builds the copy and requires the scenarios named for that
mutation to fail. A mutation that matches no line, or more than one, stops
the run, so a source edit that moves a site cannot leave a mutant that
silently equals the program.
"""

from __future__ import annotations

import argparse
from pathlib import Path

Edit = tuple[str, str, str]  # (file name, text in the program, mutant text)

SITES: dict[str, Edit] = {
    "pass1-loop": (
        "pass1.c",
        "for (inumber = ROOTINO; inumber <= imax; inumber++) {",
        "for (inumber = ROOTINO; inumber < imax; inumber++) {",
    ),
    "direntry": (
        "dir.c",
        "if (dp->d_ino <= imax &&",
        "if (dp->d_ino < imax &&",
    ),
    "makeentry-ino": (
        "dir.c",
        "|| ino < ROOTINO || ino > imax)\n        return (0);",
        "|| ino < ROOTINO || ino >= imax)\n        return (0);",
    ),
    "makeentry-parent": (
        "dir.c",
        "if (parent < ROOTINO || parent > imax ||",
        "if (parent < ROOTINO || parent >= imax ||",
    ),
    "lfdir-lookup": (
        "dir.c",
        "idesc.id_parent >= ROOTINO && idesc.id_parent <= imax",
        "idesc.id_parent >= ROOTINO && idesc.id_parent < imax",
    ),
    "alloc-test": (
        "inode.c",
        "    if (ino > imax)\n        return (0);",
        "    if (ino == imax)\n        return (0);",
    ),
    "alloc-loop": (
        "inode.c",
        "for (ino = request; ino <= imax; ino++)",
        "for (ino = request; ino < imax; ino++)",
    ),
    "fsutil-find-inode": (
        "check.c",
        "dirp->ino <= (fs->isize-1) * BSDFS_INODES_PER_BLOCK)",
        "dirp->ino < (fs->isize-1) * BSDFS_INODES_PER_BLOCK)",
    ),
    "fsutil-lost-entry-old": (
        "check.c",
        "static int make_lost_entry (fs_t *fs, fs_dirent_t *dirp)\n{\n"
        "    char name [16];\n"
        "    unsigned namlen, used;\n"
        "\n"
        '    snprintf (name, sizeof(name), "#%05u", lost_inode);\n'
        "    namlen = strlen (name);\n"
        "    /* A record that runs past its block or breaks 4-byte alignment is\n"
        "     * damaged, and neither it nor its slack takes the new entry. */\n"
        "    if (dirp->reclen > scan_room || (dirp->reclen & 3))\n"
        "        return KEEPON;\n"
        "    if (dirp->ino == 0) {\n"
        "        if (dirp->reclen < DIRENT_SIZE (namlen))\n"
        "            return KEEPON;\n"
        "        dirp->ino = lost_inode;\n"
        "        dirp->namlen = namlen;\n"
        "        strlcpy (dirp->name, name, sizeof(dirp->name));\n"
        "        return ALTERD | STOP;\n"
        "    }\n"
        "    used = DIRENT_SIZE (dirp->namlen);\n"
        "    if (dirp->reclen < used + DIRENT_SIZE (namlen))\n"
        "        return KEEPON;\n"
        "    memset (&inserted, 0, sizeof(inserted));\n"
        "    inserted.ino = lost_inode;\n"
        "    inserted.reclen = dirp->reclen - used;\n"
        "    inserted.namlen = namlen;\n"
        "    strlcpy (inserted.name, name, sizeof(inserted.name));\n"
        "    dirp->reclen = used;\n"
        "    return ALTERD | INSERT | STOP;\n"
        "}",
        "static int make_lost_entry (fs_t *fs, fs_dirent_t *dirp)\n{\n"
        "    if (dirp->ino)\n"
        "        return KEEPON;\n"
        "    dirp->ino = lost_inode;\n"
        '    snprintf (dirp->name, sizeof(dirp->name), "#%05d", dirp->ino);\n'
        "    return ALTERD | STOP;\n"
        "}",
    ),
    "fsutil-namlen-empty": (
        "check.c",
        "        dirp->namlen = namlen;\n",
        "",
    ),
    "fsutil-namlen-split": (
        "check.c",
        "    inserted.namlen = namlen;\n",
        "",
    ),
    "fsutil-split-off": (
        "check.c",
        "    if (dirp->reclen < used + DIRENT_SIZE (namlen))\n",
        "    if (dirp->ino)\n",
    ),
    "fsutil-reclen-bound": (
        "check.c",
        "    if (dirp->reclen > scan_room || (dirp->reclen & 3))\n        return KEEPON;\n",
        "",
    ),
    "fsutil-split-trim": (
        "check.c",
        "    dirp->reclen = used;\n    return ALTERD | INSERT | STOP;",
        "    return ALTERD | INSERT | STOP;",
    ),
    "fsutil-reach-skip": (
        "check.c",
        "            if (orphan == 0 || ! move_to_lost_found (&inode))\n",
        "            if (orphan == 0 || parent != 0 || ! move_to_lost_found (&inode))\n",
    ),
    "fsutil-no-rescan": (
        "check.c",
        "            scan_pass2 (fs, orphan);\n",
        "            (void) orphan;\n            break;\n",
    ),
    "fsutil-dotdot-keep": (
        "check.c",
        "        scan_inode (inode, DATA, scan_directory, dotdot_to_lost_found);\n",
        "        (void) dotdot_to_lost_found;\n",
    ),
    "fsutil-chain-flat": (
        "check.c",
        "                if (parent == 0 || inode_state (parent) != DSTATE)\n",
        "                if (parent == 0 || inode_state (parent) != DSTATE || 1)\n",
    ),
    "fsutil-clear-leak": (
        "check.c",
        "scan_inode (&inode, ADDR, pass4, &used_blocks);",
        "scan_inode (&inode, ADDR, pass4, 0);",
    ),
    "fsutil-tfree-keep": (
        "check.c",
        "    fs->tfree = 0;      /* fs_block_free() counts every block it links */\n",
        "",
    ),
    "fsutil-tfree-count": (
        "check.c",
        "        } else if (free_blocks != fs->tfree) {\n",
        "        } else if (0) {\n",
    ),
    "fsutil-tinode-count": (
        "check.c",
        "    if (fs->tinode != free_inodes) {\n",
        "    if (0) {\n",
    ),
    "fsutil-cycle-unbound": (
        "check.c",
        "if (++hops > last_allocated_inode)",
        "if (++hops == 0)",
    ),
    "fsutil-cycle-noclose": (
        "check.c",
        "                if (parent == inum) {\n",
        "                if (parent == inum && 0) {\n",
    ),
    "fsutil-cycle-noretry": (
        "check.c",
        "            scan_pass2 (fs, orphan);\n",
        "            scan_pass2 (fs, orphan);\n            break;\n",
    ),
    "df-isize": (
        "df.c",
        "sfsp->f_files = (sblock.fs_isize - (SUPERB + 1)) * INOPB;",
        "sfsp->f_files = (sblock.fs_isize - 2) * INOPB;",
    ),
    "mkfs-ratio": (
        "mkfs.c",
        "filsys.fs.fs_fsize,\n                        (unsigned)(filsys.fs.fs_isize-(SUPERB+1)));",
        "filsys.fs.fs_fsize,\n                        filsys.fs.fs_isize-2);",
    ),
    "dirhead": (
        "dir.c",
        "struct  dirtemplate dirhead = { 0, 12, 1, {'.'}, 0, DIRBLKSIZ - 12, 2,",
        "struct  dirtemplate dirhead = { 0, 8, 1, {'.'}, 0, DIRBLKSIZ - 8, 2,",
    ),
    "allocdir-zero": (
        "dir.c",
        "    if (ino == 0)\n        return (0);\n    dirhead.dot_ino = ino;",
        "    dirhead.dot_ino = ino;",
    ),
    "touch-superblock": (
        "utilities.c",
        "ckfini()\n{\n    flush(&dfile, &fileblk);",
        "ckfini()\n{\n    sblock.fs_tinode++;\n    sbdirty();\n    flush(&dfile, &fileblk);",
    ),
    "adjust-noop": (
        "dir.c",
        "dp->di_nlink -= lcnt;",
        "(void)lcnt;",
    ),
    "window-flush": (
        "utilities.c",
        "bwrite(fcp, inobuf, startib, INOWINDOW(startib) * DEV_BSIZE);",
        "bwrite(fcp, inobuf, startib, NINOBLK * DEV_BSIZE);",
    ),
    "window-write": (
        "inode.c",
        "bwrite(&dfile, inobuf, startib, INOWINDOW(startib) * DEV_BSIZE);",
        "bwrite(&dfile, inobuf, startib, NINOBLK * DEV_BSIZE);",
    ),
    "window-read": (
        "inode.c",
        "if (bread(&dfile, inobuf, iblk, INOWINDOW(iblk) * DEV_BSIZE))",
        "if (bread(&dfile, inobuf, iblk, NINOBLK * DEV_BSIZE))",
    ),
}

RANGE_SITES = (
    "pass1-loop",
    "direntry",
    "makeentry-ino",
    "makeentry-parent",
    "lfdir-lookup",
    "alloc-test",
    "alloc-loop",
)

# Several sites restored at once: the allocation test and loop together, the
# inode window transfers together, and the inode range tests as they stood
# before the highest inode was admitted.
GROUPS: dict[str, tuple[str, ...]] = {
    "alloc-range": ("alloc-loop", "alloc-test"),
    "inode-window": ("window-flush", "window-write", "window-read"),
    "fsutil-cycle-open": ("fsutil-cycle-noclose", "fsutil-cycle-noretry"),
    "fsutil-cycle-unclosed": ("fsutil-cycle-noclose", "fsutil-cycle-unbound"),
    "all-old": RANGE_SITES,
}


def sites_of(mutation: str) -> tuple[str, ...]:
    if mutation in GROUPS:
        return GROUPS[mutation]
    if mutation in SITES:
        return (mutation,)
    raise SystemExit(f"unknown mutation: {mutation}")


def replace_once(text: str, old: str, new: str, site: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{site}: expected one source match, found {count}")
    return text.replace(old, new)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--mutation", required=True)
    parser.add_argument(
        "--list-files",
        action="store_true",
        help="print the file names the mutation rewrites and stop",
    )
    arguments = parser.parse_args()

    chosen = sites_of(arguments.mutation)
    names = sorted({SITES[site][0] for site in chosen})
    if arguments.list_files:
        print(" ".join(names))
        return
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    for name in names:
        text = (arguments.source_dir / name).read_text(encoding="utf-8")
        for site in chosen:
            file_name, old, new = SITES[site]
            if file_name == name:
                text = replace_once(text, old, new, site)
        (arguments.output_dir / name).write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
