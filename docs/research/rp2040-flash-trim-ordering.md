# RP2040 flash trim: ordering against the write that releases a block

Source survey of the BSD lineages in a local BSD workspace (paths below are
relative to each project's tree), fetched sources of Dhara, Linux and five
embedded file systems, the closed form for FIFO cleaning, and storage
counters of the measured Pico, reconciled into the trim design of this port.

## Outcome

The ordering result stands: a discard is safe only after the write that
removes the block's last reference, and on Dhara that reduces to the order
of two calls. The design below was built with a reset test at every request
boundary and passed it. It is not merged, because it costs wear on this
medium: a trim runs Dhara's collector as a write does and the free list
hands the block out again at once, so eight compiles on the measured Pico
erased 1,592 root sectors with trim at free against 1,374 without
(`trim-cost-model.md`). The code stays on the branches
`archive/rp2040-trim-freed-blocks` and `archive/rp2040-free-list-sweep`. The ordering is
the prerequisite for an erase or discard at free on an SD card
(`log-structured-store-scope.md`), where the 2.11BSD order this tree
inherited would let a reset leave a file naming erased blocks.

## Evidence base

- BSD trees read on 2026-11-14: 2.11BSD, RetroBSD, 4.4BSD-Lite2, LiteBSD,
  FreeBSD, NetBSD, OpenBSD, DragonFly. Each claim names a file and line of
  that tree.
- Dhara upstream (`dlbeer/dhara`, clone `0e083ded`) beside the vendored copy
  in `sys/arch/rp2040/dhara`; Linux `fs/ext4`, `fs/f2fs`, `fs/ubifs`,
  `fs/jffs2`, `drivers/mtd/ftl.c`; LittleFS, SPIFFS, Zephyr NVS and ZMS,
  ChaN FatFs, NuttX SmartFS, FileX with LevelX. Fetched 2026-11-14.
- Desnoyers, "Analytic Modeling of SSD Write Performance", SYSTOR 2012,
  equations 1 to 4; Rosenblum and Ousterhout, SOSP 1991.
- Board: the measured Pico, PICO kernels built with `STORAGE_STATS=yes`,
  `sysctl machdep.storage_stats` deltas
  (`sys/arch/rp2040/doc/research/flash-traffic-per-workload.md`).

## The rule every lineage states

Every FFS lineage carries one comment in its truncate routine: "Update file
and block pointers on disk before we start freeing blocks. If we crash
before free'ing blocks below, the blocks will be returned to the free
list." 4.4BSD-Lite2 (`sys/ufs/ffs/ffs_inode.c:250`, waited write at `:264`),
LiteBSD (`:246`, `:260`), OpenBSD (`:245`, `:265`), DragonFly
(`sys/vfs/ufs/ffs_inode.c:318`, `:336`) and NetBSD (`:415`, `:438`)
implement it with a waited inode write before the first block is released;
FreeBSD does on a mount that is not async (`:279`, `:540`).

2.11BSD states the rule and frees first: `sys/sys/ufs_inode.c:513-519`
carries the comment, `:543` and `:558` free through a copy of the inode, and
`:583` writes the inode last. RetroBSD (`sys/kernel/ufs_inode.c:545-607`)
and this port (`sys/kern/ufs_inode.c`, `itrunc()`) inherited that order.
Without discard the difference is invisible after a reset: a block freed
ahead of its inode write still holds its data. With discard it decides
whether a file reads back its contents.

## What the systems that discard do

| System | Where the discard is issued | State it keeps |
| --- | --- | --- |
| FreeBSD FFS (`ffs_alloc.c:2570-2671`) | after the inode write, before the cylinder-group bit is set; with soft updates after the `freeblks` item, which runs once the zeroed inode is on disk | a request per range, a 1024-bucket hash, a task queue |
| NetBSD FFS (`ffs_alloc.c:1596-1816`) | from a work queue; the group bits are set after the discard returns; off under WAPBL | one pending record, a thread |
| DragonFly UFS (`ffs_alloc.c:1661-1692`) | "we TRIM the blocks first then free them ... so that we don't reuse a block before we actually trim it" | a buffer per free |
| OpenBSD FFS | none | none |
| ext4 `discard` | from the journal commit callback; a freed extent is withheld from reuse until commit | the transaction's freed-extent list |
| F2FS | after the checkpoint succeeds | a prefree bitmap, a thread |
| ChaN FatFs `FF_USE_TRIM`, NuttX SmartFS, FileX with LevelX | at the moment the cached allocation entry changes, before that metadata is written | none; a reset between the two leaves a file that names discarded sectors |
| LittleFS, SPIFFS, Zephyr NVS and ZMS | no discard: free space is derived from committed metadata | none |

The systems that are safe share one invariant: a block is discarded after
the durable write that removes its last reference and before the allocator
can hand the block out again. The small systems that discard without that
order are the ones a reset can damage.

## What Dhara changes

`dhara_map_trim()` is journaled like a write: `try_delete()` copies a cousin
page to the journal head with a path that omits the sector (one 1 KiB page
for a mapped sector, nothing for an unmapped one), and a reset restores the
journal to its last checkpoint, a prefix of the order in which requests were
enqueued (`map.c`, `journal.c`; `map.h`: after `dhara_map_sync()` "all
changes to date are persistent and durable"). In a totally ordered,
prefix-durable log, "durable before" reduces to "submitted before": any
restored prefix that contains a trim contains every request submitted
ahead of it. The BSD designs need queues, hashes and threads because their
devices give no such order. Here the order of two calls is the whole
mechanism, and it costs no memory.

Two facts of this tree decide where the calls go:

- The root is mounted async, and `iupdat()` leaves the inode write delayed
  under `MNT_ASYNC`. A delayed write has not been submitted, so the truncate
  path submits the inode itself when the device trims.
- `free()` writes the free-list chain node into the freed block when the
  in-core list is full (`sys/kern/ufs_alloc.c`). A trim issued first is
  followed in request order by that write, which maps the block again; a
  trim issued after it would destroy the chain. Trimming at the head of
  `free()` therefore needs no test for chain blocks.

## Design that was built and measured

1. `itrunc()` submits the inode with its released pointers zeroed before it
   frees a block, and `indirtrunc()` submits an indirect block before it
   frees that block's children: the 4.4BSD order.
2. `free()` trims the block, then pushes it on the free list.
3. A trim error leaves the block mapped; the free proceeds.

Falsifier: the host test resets the flash model at every request boundary
of a truncate, with the root sync and async, and requires every block the
restored inode references to read back its data.

## Rejected and deferred

- A queue of freed blocks applied at the checkpoint keeps the file system
  code untouched and needs a bitmap or list in kernel RAM, a cancel on
  reallocation, and still admits a checkpoint taken while a truncate holds
  its inode locked.
- A sweep of the free list (the `fstrim` form) trims only blocks the
  allocator calls free, and the free list of this file system lags the
  inodes in both directions, so it is safe only directly after a completed
  sync with the list locked. It remains the one way to unmap blocks that
  fsck returned to the list after a reset and blocks freed before a kernel
  that trims. It was built on `archive/rp2040-free-list-sweep` and never won in the
  host model (`trim-cost-model.md`).

## Cleaning cost against the live share

Desnoyers gives FIFO cleaning under uniform random writes as
A = 1 / (1 - exp(-alpha / A)), alpha the ratio of physical to logical pages.
The vendored `auto_gc()` runs at most `FLASH_GC_RATIO + 1` = 5 tail steps
per write, so it keeps pace only while the live share of the tail stays at
or under 0.8; above that every write also consumes reserve.

| Live share | FIFO closed form | with one checkpoint page per eight | host model of this tree |
| ---: | ---: | ---: | ---: |
| 0.75 | 2.20 | 2.51 | 1.20 |
| 0.80 | 2.69 | 3.08 | 2.34 |
| 0.85 | 3.52 | 4.02 | 4.45 |
| 0.90 | 5.18 | 5.92 | 6.28 |
| 1.00 | bounded by the cleaner at 6 | 6.86 | 6.28 to 8.00 |

The model sits under the closed form at 0.75 and 0.80, where a burst has not
yet wrapped the journal. From 0.85 up it exceeds the bare closed form by 21
to 26 percent and the column that adds the checkpoint page by 6 to 11
percent; the remaining excess is not explained. Both put
the knee at a live share of about 0.8: 791 of the 989 sectors. The shipped
`full` image maps 758 sectors once free blocks stay out of the map, 33 below
the knee.

## Board measurements

| Kernel and image | Root program units per 1 KiB write |
| --- | --- |
| checkpoint per request, every sector live | 8.00 in every workload |
| checkpoint on sync, every sector live | 5.65 to 6.86 |
| checkpoint on sync, free blocks unmapped, first compile after flashing | 1.20 |
| the same, second and third compile, a 24 KB write | 5.55, 3.18, 6.05 |

The first compile erases 50 root sectors where the first row's kernel
erases 346, and takes 12.4 seconds where it took 25. The saving is gone by
the second compile because a block the file system has written once stays
live in the map after the file is deleted. A trim returns the live share to
where the image starts, at the collector cost the Outcome section states;
the merged change instead starts collection by the live sector count
(`sys/arch/rp2040/doc/research/dhara-gc-trigger.md`).

## Open

- Erases per physical sector for swap.
- The inode-before-free order as its own change, ahead of any SD erase.
- The excess of the model over the cleaner's bound at live shares of 0.9
  and above.
