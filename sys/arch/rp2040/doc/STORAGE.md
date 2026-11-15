# Storage on the Pico: what costs what, and the layout chosen

Measured on the built DiscoBSD tree for the RP2040 on 2026-09-11. Sizes
are bytes of the a.out as installed unless marked.

The current SRAM layout gives every process a fixed 192 KB window at
`0x20000000..0x20030000`. The optional 16 KB SwapRAM compression pool is
kernel memory after ordinary data and BSS; pool evacuation changes the image's
storage tier without changing the process ceiling. Historical measurements
below remain bound to the revisions and images they name.

## The three costs, in order

**Static libc, one copy per program.** Every a.out links static libc and
carries what it uses of it. The missing MMU does not force this: every
process loads at the same window address, so a libc linked once at a fixed
flash address could serve all of them. `docs/research/xip-shared-libc/`
measures that design and the roadmap toward it. `cat` has 1.4 KB of its own
code in a 17 KB file; `id` has 1.5 KB in 20 KB. Across the 29 programs
in `/bin` the tree's own code is a fraction of the 528 KB installed.

**Floating point in printf.** `_doprnt` is 5.8 KB and its `cvt` path
pulls `__aeabi_dadd`, `dmul`, `ddiv`, `fsub` and the comparisons, about
10 KB in every program whether it prints a float or not. The fix is a
weak reference: `cvt` and `cvtround` now live in `doprnt_float.o`, and
on rp2040 only a Makefile that says `PRINTF_FLOAT=yes` links it. Twenty
one programs do (awk, bc, dc, basic, forth, picoc, scm, tclsh, vmstat,
iostat and friends). Every other machine links it unconditionally, as
before.

| Program | Before | After |
|---|---|---|
| cat | 16,984 | 10,220 |
| ls | 26,455 | 19,839 |
| grep | 17,976 | 11,212 |
| sed | 23,400 | 16,636 |
| ps | 22,290 | 15,694 |
| init | 23,968 | 17,372 |

**The kernel region.** 512 KB was the STM32F407XE figure. The region is
128 KB, of which boot2 takes 256 bytes; `check-kernel-headroom` measures
the flash image of each kernel against the remaining 130,816 (PICO: 95,248
bytes, 35,568 free, at `7a270276` with GCC 16.2.0), and the Dhara
region is 1536 KB, 989 KB of logical blocks; the 64 KB moved from the
kernel region bought 39 KB of root because Dhara reserves its share of
every erase block.

## The multicall executables

Eight multicall a.outs hold most of the root's commands, each dispatched
on the name it is invoked by and hard-linked under every name by the
manifest: `sbin/box` (cat, cp, ls, rm and the other /bin tools; `[` is
an alias of test), `sbin/sysbox` (date, dd, df, stty, mknod, mount,
umount), `sbin/adminbox` (shutdown, reboot, halt, sysctl, true, false,
nohup), `sbin/textbox` (the sbase text tools), `sbin/utilbox` (expr, id,
sort, more, uniq, head, tr, wc and the rest of usr.bin), `sbin/grepbox`
(grep, fgrep), `games/gamebox` and `games/puzzlebox`. Each tool's objects are combined with
`ld -r`, `main` becomes `<tool>_main`, and every other global goes local,
so the sources build on their own as well. A box's applet-private bss
shares one overlaid extent (MULTICALL-BSS-OVERLAY.md), and every box is a
packed a.out that the kernel expands at exec.

The limit on a box is the swap map before the process window: the
largest image must find a contiguous run beside the shell and init after
fragmentation, so images stay near 40 KB; awk, sed, the editors, the
set-id programs, and the native toolchain stay separate.

## What compression does and does not do

gzip -9 takes 30 to 35 percent off an a.out and xz 36 to 44 percent.
The heatshrink expander in the kernel recovers that across the whole
root: every installed executable is a packed a.out that exec expands, at
a cost of tens of milliseconds per exec on the M0+ (see the packed a.out
notes above).

Executing userland in place from flash frees RAM and, for a shared
library, flash. An a.out linked at 0x20000000 cannot do it; a binary
linked for a fixed flash address with its data in the user window could,
at the cost of a new executable format, a loader that validates it, and a
swapper that never restores flash text into the data window. For libc
alone, `docs/research/xip-shared-libc/` measures 502 packed blocks falling
to 288 over 28 executables against a 28-block partition cost, with libc data
relocated by crt0 so the loader and swapper stay unchanged. It is not
built.

The kernel itself compresses 35 percent, but it executes in place from
flash and a compressed kernel would have to be copied to RAM, which
costs the whole kernel, about 106 KB, of the 264.

## The layout

| Region | Address | Size | Content |
|---|---|---|---|
| boot2 and kernel | 0x10000000 | 128 KB | 106 KB used |
| root, Dhara journal | 0x10020000 | 1536 KB | 989 KB of logical blocks |
| swap, raw | 0x101a0000 | 384 KB | erase and program in place |

Dhara's overhead on the root is fixed by its metadata: one checkpoint
page per eight, a reserve of one fifth for garbage collection, and a
64 KB safety margin; 1536 KB of flash yields 989 KB of blocks. The
`flashimg -c` tool prints the figure for any geometry.

## Root read-write policy

Both RP2040 kernel configurations set `ROOT_MOUNT_FLAGS=MNT_NOATIME`.
The initial `mountfs()` call applies the flag before init reads the root,
and `/etc/fstab` repeats `noaccesstime` because `mount -a` updates the root
mount and replaces its mutable flags. Other architectures retain a zero
default for `ROOT_MOUNT_FLAGS`.

The policy prevents ordinary `read(2)` and `readlink(2)` operations from
marking an inode for an access-time write. An explicit `utimes(2)` request
still sets `IACC`, so `touch -a` and timestamp-preserving copies retain their
requested behavior. Read-derived access times therefore remain historical:
`ls -u`, `find -atime`, tty-idle reporting, and mailbox newness cannot treat a
read as a timestamp update on the RP2040 root.

The eliminated write trigger has a bounded software path but no fixed erase
count. One dirty inode causes its inode block to reach `flstrategy()`; a
one-kilobyte request becomes one `dhara_map_write()` of a 1 KiB Dhara unit
(`FLASH_UNIT_BYTES`), four 256-byte program pages, and its share of the
checkpoint that later covers it (see "Checkpoint cost" below). Buffer
coalescing can combine several inode changes, and Dhara garbage collection
decides the physical program and erase count.
Claims about saved flash operations therefore require workload counters or a
board trace; the mount policy alone proves only that reads stop creating the
dirty-atime input.

## What ships

The manifest installs 19 directories, 50 files, 18 device nodes, 78 hard
links, and 1 symlink; df on the board reports 979 blocks, 665 used, 314
free. The root holds sh, ed, ps, tar, init, fsck, getty, login, passwd,
su, update, awk, sed, find, tail, tee, du, compress, cpio, the seven
multicall boxes above, the stevie screen editor as vi, the menu shell,
and the native build chain: /usr/bin/cc driving /usr/libexec/smlrc,
/usr/bin/as and /usr/bin/ld against /usr/lib/libc.a and crt0.o. On the
board `cc -o h h.c` compiles, assembles and links a program with integer
division and printf, and `./h` runs. picoc is not on the root; the native
chain replaces it.

## Optional storage counters

Build a diagnostic kernel with `STORAGE_STATS=yes` and read
`sysctl machdep.storage_stats`. The default is `STORAGE_STATS=no`; the node
returns EOPNOTSUPP in that build. Rebuild sysctl and adminbox after changing
cpu.h so their machine node tables match the kernel. The versioned read-only
snapshot contains cumulative unsigned 32-bit counters since boot, delayed
buffer occupancy at capture, its peak, and a sticky saturation flag. Take two
captures around a workload and subtract each field only if neither capture
is saturated. There is no reset operation. Capture from the same resident
process where possible: invoking a utility can itself cause swapping.

The logical root and raw-swap request/byte counters count valid requests
submitted to the driver, including requests that subsequently fail. Map
writes count Dhara mapping attempts. Checkpoint attempts, successes and
errors count forced checkpoints, the `dhara_map_sync()` calls of
`fl_checkpoint()` that `DIOCCHECKPOINT` and the driver's last close make,
including a successful no-op on an already clean map. The checkpoint Dhara
writes itself when a write fills a group is not counted. They are not counts
of checkpoint pages written. NOR program
pages and erase sectors count requested 256-byte page and 4096-byte sector
equivalents, with requested bytes alongside them; the ROM may use a larger
erase opcode. Root and swap are counted separately at the NOR boundary,
including Dhara garbage collection and copy traffic. These are software
observations, not electrical confirmation of completed operations or wear.
Instrumentation runs before disabling XIP and never executes from flash
while the flash interface is unavailable.

Buffer counters distinguish new delayed writes, rewrites of already dirty
buffers, dirty eviction writes and waits for buffer capacity. Occupancy is
sampled when a delayed buffer is marked and when the snapshot is read.
SwapRAM attempts and admissions identify compression-path use separately
from raw-swap transfers. Admission is counted after reservation succeeds;
it is not a measurement of compression time or bytes saved.

Use paired captures for a clean idle interval, partial-block overwrites,
full-block rewrites, sequential and random reads, linker scratch traffic,
and explicit synchronous writes. Keep buffer configuration, image contents
and initial free space fixed and report logical requests, map operations,
checkpoint calls, NOR programs, NOR erases and swap separately. This tree
uses 1024-byte Dhara pages: one logical 1 KiB write attempts one map write,
although programming its data can require four physical 256-byte pages.
Counter instrumentation alone changes no coalescing or read-ahead policy.

Paired GCC 13.2.1 Cortex-M0+ builds measured 488 additional text bytes and
128 additional data bytes for counters in both PICO and PICO_UART, with
unchanged BSS. These are linked ELF differences for this implementation,
not a promise for other toolchains or configurations.

Under the earlier policy of a checkpoint after every write request, the
retained C17/SUS stdio board workload takes unsaturated snapshots around
43 checked commands and an explicit `sync`. The interval reports 38 logical
root writes, 38 map writes, 38 successful checkpoints with zero errors, 1,216
root program pages and 76 root erase sectors. Swapping reports 240 reads, 240
writes, 5,894 program pages and 400 erase sectors; SwapRAM records 83 attempts
and 3 admissions. Dirty-buffer traffic adds two transitions, two rewrites,
zero evictions and zero waits. The capture establishes software traffic for
that image and workload. It neither assigns traffic to one stdio call nor
converts requested NOR operations into electrical wear.

## Synchronous write contract

Explicit IO_SYNC now survives regular-file writes and MNT_ASYNC. The
indirect-block allocator honors B_SYNC even on an async mount. A successful
synchronous write waits for its data, then syncs the inode and the matching
mount's allocation and superblock state using the existing fsync paths.
Data, inode, buffer flush and latched mount errors propagate to the caller.
The mount flush can also write unrelated dirty data, so synchronous writes
may cost more than a targeted per-file flush. This correctness repair does
not change ordinary full-block bawrite policy, Dhara checkpoint placement,
raw swap or the periodic update daemon. Host fixtures establish sequencing
and error handling; power-cut persistence still requires separate hardware
evidence.

## Periodic sync coverage

`sync()` visits every mounted filesystem whose superblock lists are
unlocked and calls `ufs_sync()`, which writes modified inodes and delayed
buffers and then the superblock only when `fs_fmod` is set. `fs_fmod`
records superblock changes alone, so an in-place overwrite of an allocated
block, which dirties a buffer and the inode without allocating, is written
by the next `sync()` rather than waiting for eviction, a later allocation,
`fsync`, unmount or shutdown. A clean filesystem costs an inode-table and
free-list scan and no device request. Inodes found locked, buffers found
busy and a mount whose `fs_ilock` or `fs_flock` is held stay dirty for the
next call; a failed write latches in `m_write_error`. Each `ufs_sync()` ends
with the device checkpoint described under "Checkpoint cost", whether or not
it wrote anything. update(8) supplies a
30-second cadence for these calls, not a completion bound: a skipped object
waits for a later call, and scheduling and flash latency add to the
interval. The host gate establishes the selection and error handling;
whether the overwrite reaches flash at the next tick on the board is a
separate hardware observation.

## Checkpoint cost

`dhara-geometry.txt` records the geometry of the root region: 1 KiB Dhara
pages, 8 KiB erase blocks, checkpoint groups of eight pages (seven user pages
and one metadata page), garbage-collection ratio 4 and a capacity of 989
sectors. `check-dhara-amplification` derives these values from
`dev/flash.h` and the vendored journal and fails when the table disagrees.

Dhara makes a write durable only at a checkpoint, the metadata page that
closes a group. `dhara_map_write()` writes it when a write fills the group;
`dhara_map_sync()` pads the open group with copies of live pages up to it.
A checkpoint after every write request, the earlier policy of
`flstrategy()`, therefore made each request consume a whole group: in the
host model eight Dhara pages, or 32 requested 256-byte program-page
equivalents, at every map occupancy measured. The board shows the same
cost: a diagnostic `STORAGE_STATS=yes` kernel measured 32 root program
pages and two erase sectors per root write request (2 writes: 64 pages and
4 sectors; 66 writes: 2,112 and 132; one `cc` compile, 175 writes: 5,600
and 350).

`flstrategy()` now returns after `dhara_map_write()` and forces no
checkpoint. `ufs_sync()` ends with `DIOCCHECKPOINT`, which runs
`fl_checkpoint()`, so sync(2), the 30-second update(8) call, fsync(2),
`IO_SYNC` writes, unmount and shutdown each force one, and so does the
driver's last close; a checkpoint with nothing pending programs nothing. A
reset or power loss discards the writes since the last checkpoint, forced
or written by a group fill, and `dhara_map_resume()` returns the device to
the state that checkpoint recorded. Because a filled group checkpoints
itself, the completed requests a reset can lose are those in the open group:
at most six 1 KiB requests, fewer when garbage-collection copies share the
group, written since the last forced checkpoint or group fill. A forced
checkpoint follows each sync(2), about one update(8) interval plus scheduling
delay apart in ordinary operation. The dirty buffers and inodes not yet
written remain lost to a reset as before. A sync after one to six pending
writes still pads a whole group, so writes spaced wider than the sync
interval cost what they did under the per-request policy.

sync(2) has two stated limits. `sync()` returns at once while another
`sync()` holds `updlock`, and it passes over a mount whose superblock lists
(`fs_ilock`, `fs_flock`) are locked; neither call then reaches that mount's
`ufs_sync()` or its checkpoint. The writes it would have covered stay
uncheckpointed until the next pass reaches the mount, normally the next
update(8) call 30 seconds later, and a reset in between can lose them up to
the open-group bound above. Shutdown does not depend on that pass:
`rp2040_shutdown_sync()` forces the root device's checkpoint through
`bdev_checkpoint()` after its `sync()` and buffer drain, whatever `sync()`
did. `reboot -n` (`RB_NOSYNC`) skips both, as it skips the buffer flush.

A checkpoint failure reaches fsync(2), synchronous writes and unmount as
`EIO` and latches in the mount's `m_write_error`, under the rule that
already governs a failed buffer write: the first error is kept, every later
`ufs_sync()` of that mount returns it, and nothing clears it while the
mount exists. The slot is reset only when a mount is made (`mountfs()` loads
the device's recorded error) and by an unmount over media that
`DIOCMEDIAGONE` reports removed; the flash driver reports no removal, so a
flash mount with a latched error refuses unmount, and the root keeps the
error until reboot. sync(2) returns no status, so its failure surfaces
through that latch, a later fsync(2) and the shutdown report, which also
prints a failed shutdown checkpoint.

A process forces a checkpoint with `ioctl(fd, DIOCCHECKPOINT, &status)` on a
flash block node it opened for writing; `flioctl()` refuses a descriptor
opened read-only with `EBADF`, and opening the node requires the node's
permissions. `check-flash-checkpoint` holds the driver to these properties
over a NOR model, `kernel.ufs-disk.ilp32` holds the filesystem's sync paths
to issuing the checkpoint, and `check-rp2040-shutdown` holds shutdown to
forcing it. A board reset test of the new policy has not been run. Production
`dhara_nand_prog()` passes one 1 KiB extent to `flash_program()` while
`dhara_nand_copy()` passes four 256-byte extents, so the equivalent count is
neither a wrapper-call count nor a ROM-command count. N writes followed by one checkpoint
program `8 * ceil(N / 7)` pages while the journal stays below the
`auto_gc()` threshold. The shipped image writes all 989 sectors, which puts
the journal at that threshold: rewriting recently written sectors in
batches then costs about 6.3 pages per write, because each write first
copies live tail pages. On the shipped image, checkpointing on sync rather
than per request therefore saves about a fifth of the programs (8 to about
6.3 pages per write in that trace, 6.0 in `check-flash-checkpoint`'s), not
the six sevenths a journal below the threshold gives. With 75 percent or less of the capacity live, the
same traces stay below the threshold and cost 1.2 pages per write. These are
host-model counts for the exercised traces; electrical completion, command
execution inside the ROM and physical-sector wear require separate board
measurements.

## Trim on free

A block the filesystem frees stays live in the Dhara map until it is
written again, and every live sector is copied by garbage collection, so
`free()` in `sys/kern/ufs_alloc.c` tells the device the block is dead:
`free_trim()` issues `bdev_trim()` (`DIOCTRIM` in `sys/sys/disk.h`), which
`flioctl()` serves with `dhara_map_trim()` under `flstrategy()`'s bracket.
A driver without the command keeps the block; a failed trim leaves it
mapped, which costs wear and nothing else, and is not counted (the storage
counters' version 1 metric set is fixed, and
`tools/summarize_rp2040_storage_delta.py` rejects any other).

Every BSD that discards keeps one rule: a block's discard follows the
durable write that removes its last reference and precedes the allocator
handing the block out again (FreeBSD and NetBSD `ffs_blkfree`, DragonFly's
`BUF_CMD_FREEBLKS`; 4.4BSD-Lite2 `ffs_truncate` writes the inode first).
Dhara keeps a single request order and a reset restores a prefix of it, so
"durable before" becomes "submitted before". The port meets it as follows.

| Path that drops a block reference | Write that removes the last reference | Trim |
| --- | --- | --- |
| `itrunc()` direct blocks and the top indirect block (truncate, unlink at last close via `irele()`, `O_TRUNC`, `ufs_setattr()` size, directory shrink in `ufs_namei.c`, `rmdir`/`rename`, core dump, the `rwip()` rollback) | the inode, written by `iupdat(oip, ..., 1)` before the first `free()`; with waitfor set `iupdat()` calls `bwrite()` whatever `MNT_ASYNC` says | yes |
| `indirtrunc()` children (`trsingle()` and the recursive levels) | the indirect block with the dropped pointers zeroed, `bwrite()` or `bawrite()` before its children are freed; the flash strategy completes either before it returns | yes |
| `bmap()` rollback after a failed initialization write (`write_failed`) | none needed: no pointer to the block was published | no in practice: the failed write latched `m_write_error` |
| any `free()` after a latched write error | a releasing write may have failed | no |
| V7FS filesystems | the device block holds two V7 blocks | no |

`itrunc()` wrote the inode last, after the frees, as 2.11BSD and RetroBSD
do although their comment states the opposite; it now writes it between
zeroing the pointers and the first free, where 4.4BSD-Lite2 does, and a
failure of that write latches in the mount. The trim precedes the push and
the chain write in `free()`, so a block that becomes a free-list chain node
is mapped again by that write. `alloc()` cannot return the block before
`free()` pushes it: the trim does not sleep, and `free()`'s waits on
`fs_flock`, `getblk()` and the chain write all come before the push. On an
`MNT_ASYNC` mount the chain write is delayed, as before, and reaches flash
with the next sync.

`check-trim-order` runs the production `ufs_inode.c`, `ufs_alloc.c`,
`ufs_bmap.c`, `ufs_bio.c`, `ufs_subr.c` and `ufs_syscalls2.c` over the
vendored Dhara map and rebuilds every prefix of a truncate's requests as a
reset would leave it; no restored inode names a block that reads 0xff. In
the same host model each trim journals one Dhara page (`try_delete()`
rewrites a neighbor's page). With a static file holding 732 of 989 sectors
live, create-and-delete rounds cost 10.38 Dhara pages per file block with
trim against 5.99 without (one 16-block file per round), and 9.04 against
6.48 (six 30-block files per round): the free list hands the freed blocks
out again first, so trim lowers the live count between rounds but not
while the next round writes. The board decides whether its workloads
behave like these rounds; board evidence for trim has not been run.
