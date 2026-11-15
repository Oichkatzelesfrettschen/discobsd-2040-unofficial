# BSD discard ordering versus the metadata write that releases a block

Paths marked `W/` are in a local collection of BSD source checkouts, one directory per tree (`4.4BSD-Lite2`, `LiteBSD`, `freebsd-src`, `netbsd-src`, `openbsd-src` and the others named below), whose revisions were not recorded; paths marked `DISCO/` are in the port tree. Searches: `grep -n` over each tree's `ffs_inode.c`, `ffs_alloc.c`, `ffs_softdep.c`, `ffs_vfsops.c`, `softdep.h`; `grep -rniE "discard|trim|BIO_DELETE"` over `openbsd-src/sys/ufs`; `grep -rn "dotrim\|trim_onfree"` over DragonFlyBSD.

## 1. Truncate ordering

Every FFS lineage states the same rule and, except the 2.11BSD family, implements it with a synchronous inode write before any block is freed.

- Rule text (identical in all): "Update file and block pointers on disk before we start freeing blocks.  If we crash before free'ing blocks below, the blocks will be returned to the free list." Locations: `W/4.4BSD-Lite2/sys/ufs/ffs/ffs_inode.c:250`, `W/LiteBSD/sys/ufs/ffs/ffs_inode.c:246`, `W/freebsd-src/sys/ufs/ffs/ffs_inode.c:522`, `W/netbsd-src/sys/ufs/ffs/ffs_inode.c:415`, `W/openbsd-src/sys/ufs/ffs/ffs_inode.c:245`, `W/DragonFlyBSD/sys/vfs/ufs/ffs_inode.c:318`.
- Implementation, inode first, waited:
  - 4.4BSD-Lite2 `ffs_inode.c:264` `VOP_UPDATE(ovp, &tv, &tv, MNT_WAIT)`; LiteBSD `:260` same.
  - OpenBSD `:265` `UFS_UPDATE(oip, 1)`; DragonFly `:336` `ffs_update(ovp, 1)`.
  - NetBSD `:438` `ffs_update(ovp, NULL, NULL, UPDATE_WAIT)`, only if a pointer changed (`sync = 1`, `:425-434`).
  - FreeBSD `:540` `ffs_update(vp, waitforupdate)` with `:279` `waitforupdate = (flags & IO_SYNC) != 0 || !DOINGASYNC(vp)`. An async mount skips the wait, so FreeBSD's ordering is sync-mount only.
- 2.11BSD states the rule and does not implement it. `W/211bsd/sys/sys/ufs_inode.c:513-519` carries the same comment. The code then edits the in-core inode (`:520` `tip = *oip;`, pointers zeroed `:524-531`), frees through `tip` (`:543` `free(ip, bn)`, `:558`, and `:676` inside `indirtrunc`), and writes the inode last: `:583` `iupdat(oip, &time, &time, 1);`. `iupdat` also skips the wait when `MNT_ASYNC` is set (`:414`). The block-pointer zeroing inside `indirtrunc` is written first (`:646-648`, `bawrite` unless `B_SYNC`), but the inode pointer is not.
- RetroBSD is the same as 2.11BSD: comment `W/retrobsd/sys/kernel/ufs_inode.c:545-548`, frees `:572,:587`, `iupdat` last at `:607`. The port itself matches: `DISCO/sys/kern/ufs_inode.c:660` (comment), `:687,:702` (frees), `:722` (`iupdat`).
- LiteBSD differs from RetroBSD: it carries the 4.4BSD `ffs_truncate`, inode first.
- Soft updates (FreeBSD `softdep.h:566-573`, DragonFly `softdep.h:392`): a `freeblks` item records "the blocks of a file after its zeroed inode has been written to disk" and frees run only after that write completes. With SUJ, `ffs_softdep.c:8135` asserts "Journal entries not written." before freeing.

## 2. Discard implementations

- FreeBSD (`ffs_alloc.c`, flag `FS_TRIM` `fs.h:516`, set by `tunefs -t` `tunefs.c:545-555`; mount checks `GEOM::candelete` `ffs_vfsops.c:1076-1082`). `ffs_blkfree` (`:2671`) either frees immediately or builds a trim request and calls `ffs_blkfree_sendtrim` (`:2570`), which sends BIO_DELETE via `g_vfs_strategy` (`:2598`). The comment at `:2577-2580` says: "Postpone the set of the free bit in the cg bitmap until the BIO_DELETE is completed." `ffs_blkfree_trim_completed` (`:2469`) queues `ffs_blkfree_trim_task` (`:2483`), which calls `ffs_blkfree_cg` and so marks the block free. Order: inode write (section 1) → BIO_DELETE → cylinder-group bit set. It never waits for the cg write. A crash after the trim leaves the block marked allocated in the cg bitmap with no owner: leaked, recoverable by fsck.
  - Consolidation, `vfs.ffs.dotrimcons` (`:502-504`): `ffs_blkrelease_start/finish` (`:2605,:2625`) bracket one file's release; contiguous blocks merge into one `ffs_blkfree_trim_params` plus a `trim_blkreq` list (`:2436-2457`).
  - State: one malloc'd request per range and per block element, a 1024-bucket hash (`MAXTRIMIO` `ffs_extern.h:150`, `hashinit` `ffs_vfsops.c:1096`), a taskqueue thread.
  - Soft updates/SUJ: frees come from `handle_workitem_freeblocks` (`ffs_softdep.c:8125-8137`, `ffs_blkrelease_start` at `:8137`), so trims inherit the "after inode and journal written" order.
- NetBSD (`ffs_alloc.c`, mount option `MNT_DISCARD` `sys/fstypes.h:98`, set up in `ffs_vfsops.c:1612`). `ffs_blkfree` (`:1733`) buffers a single pending `struct discardopdata` in `ts->entry` (`:1596-1612`, `:1806`), prepend-merging adjacent frees up to `maxsize = 100*1024` (`:1691`), then enqueues to a workqueue (`:1788,:1816`). `ffs_discardcb` runs `VOP_FDISCARD` (`:1660`) and only then `ffs_blkfree_td` (`:1666`) sets the cg bits. It does not wait for the cg write; it defers the cg bit until after the discard returns. Inode ordering comes from `ffs_truncate` alone. State: one `discardopdata`, workqueue thread, two mutexes, one condvar. With WAPBL, discard is turned off: `ffs_wapbl.c:390-401` "XXX discard interferes with block deallocation registration and hence log consistency".
- OpenBSD: no FFS discard. `grep -rniE "discard|trim|BIO_DELETE|DIOCGDISCARD" openbsd-src/sys/ufs` finds only comments on discarding vnodes; no `FS_TRIM`/`MNT_DISCARD` in `sys` or `sbin/tunefs`. `sd.c:1594,1596` assigns `sc->sc_delete = sd_unmap` / `sd_write_same_16`, and `grep -rn sc_delete openbsd-src/sys` finds no caller.
- DragonFly UFS: mount flag `MNT_TRIM` (`sys/sys/mount.h:284`). `ffs_blkfree` (`ffs_alloc.c:1668`) issues `BUF_CMD_FREEBLKS` (`:1692`) and the completion task calls `ffs_blkfree_cg`. Comment `:1661-1665`: "we TRIM the blocks first then free them... so that we don't reuse a block before we actually trim it". State: one `kmalloc` params struct and one `getnewbuf` buffer per free, no merging. `vfs.ffs.dotrim` and `trim_onfree`: not found. HAMMER/HAMMER2 do not issue BUF_CMD_FREEBLKS (only a comment match, `hammer2_vnops.c:571`).

## 3. Driver-facing discard interface

- FreeBSD: `BIO_DELETE` (`sys/sys/bio.h:47`) with `bio_offset`/`bio_length`; a disk advertises `DISKFLAG_CANDELETE` and `d_delmaxsize` (`geom_disk.h:110`, `geom_disk.c:446-449`); userland `DIOCGDELETE _IOW('d',136,off_t[2])` (`sys/sys/disk.h:60`). Smallest form: one bio.
- NetBSD: `VOP_FDISCARD` → `spec_fdiscard` (`spec_vnops.c:1292-1318`) → `bdev_discard` → `d_discard(dev_t, off_t, off_t)` (`sys/conf.h:80`; `disk.h:522`). `DIOCGDISCARDPARAMS` is gone ("129 was DIOCGDISCARDPARAMS", `dkio.h:123`). Smallest form: one synchronous `(dev, offset, length)` call.
- DragonFly: `BUF_CMD_FREEBLKS` (`sys/sys/buf.h:91`), handled in e.g. `mmcsd.c:541`. OpenBSD: `sc_delete` exists in SCSI `sd` only, uncalled. 2.11BSD, RetroBSD, LiteBSD: not found (`grep -rniE "discard|BIO_DELETE|d_discard"` over `211bsd/sys/h`, `retrobsd/sys/include`, `LiteBSD/sys/sys` matches only `unp_discard` and map comments).

## 4. Reallocation hazard

All three trimming systems close it the same way: the block stays allocated in the allocator's map until its discard has been issued or completed, so it cannot be handed out and rewritten first (FreeBSD `:2577-2580`; NetBSD `:1660-1666`; DragonFly `:1661-1665`). The FreeBSD `trim_lookup` hash is keyed by consolidation key, not by block, and does not detect reuse; it only merges ranges.

## 5. Invariant and ranking

Invariant (one sentence): a block's discard is ordered after the durable write that removes its last reference and before the allocator can hand that block out again.

Consequence in this port (source-verified, `W/211bsd/sys/sys/ufs_alloc.c:263-276`): `free()` writes the free-list chain node into the freed block itself when the in-core list is full (`*fbp = *((FBLKP)&fs->fs_nfree)` at `:267`, `bwrite` `:273`), and `alloc()` reads it back with `bread` (`:61`). A trim applied after that write destroys the chain; a trim issued before it is followed by the chain write, which then stands.

Ranking for a totally ordered, prefix-durable journal with about 40 KB RAM (the ranking and the Dhara behavior below are hypothesis: derived from the user-stated Dhara contract, not from these trees):

1. Write the inode (`iupdat(..., 1)`, submitted to the FTL) before the free loops, then trim at free, with the trim submitted before `free()` pushes the block. Zero extra state. Because Dhara orders writes and trims in one sequence and restores a prefix, any restored prefix that contains the trim also contains the earlier inode write. The chain-node write follows the trim in the same order, so the chain survives. Obligations: move `iupdat` between the pointer zeroing (`:531`) and the first free (`:543`), as 4.4BSD does; submit `indirtrunc`'s zeroed pointer block before trimming its children; skip trim under `MNT_ASYNC`, where `iupdat` does not wait (`:414`), as FreeBSD's async mount skips its wait (`ffs_inode.c:279`). Residual: a block freed but missing from the durable superblock free list is leaked until fsck, the same outcome the lineage comment accepts.
2. Queue trims and apply at the checkpoint. The checkpoint is durable after the inode write, so ordering against the inode holds. It fails the second clause unless every queued block is withheld from the free list or cancelled on `alloc()`, and a queued block that later became a chain node would be trimmed after the chain write. Cost: 4 bytes per block (or an extent list) plus a membership test per allocation.
3. No trim. Satisfies the invariant vacuously and costs nothing in state, but leaves Dhara with no reclaim hint. It ranks below (1) on function and above (2) on risk.

State cost: FreeBSD tens of bytes per freed block plus hash and thread; NetBSD one pending record plus thread; DragonFly one params struct and buffer per free; OpenBSD and the 2.11 family none.
