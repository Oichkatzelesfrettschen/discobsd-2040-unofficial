# Storage on the Pico: what costs what, and the layout chosen

Measured on the built DiscoBSD tree for the RP2040 on 2026-09-11. Sizes
are bytes of the a.out as installed unless marked.

## The three costs, in order

**Static libc, one copy per program.** No MMU means no shared library,
so every a.out carries what it uses of libc. `cat` has 1.4 KB of its own
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

**The kernel region.** 512 KB was the STM32F407XE figure; the kernel is
90 KB. The region is 128 KB, leaving 38 KB for growth, and the Dhara
region is 1536 KB, 989 KB of logical blocks; the 64 KB moved from the
kernel region bought 39 KB of root because Dhara reserves its share of
every erase block.

## The multicall executables

Seven multicall a.outs hold most of the root's commands, each dispatched
on the name it is invoked by and hard-linked under every name by the
manifest: `sbin/box` (cat, cp, ls, rm and the other /bin tools; `[` is
an alias of test), `sbin/sysbox` (date, dd, df, stty, mknod, mount,
umount), `sbin/adminbox` (shutdown, reboot, halt, sysctl, true, false,
nohup), `sbin/textbox` (the sbase text tools), `sbin/utilbox` (expr, id,
sort, more, uniq, head, tr, wc and the rest of usr.bin), `sbin/grepbox`
(grep, fgrep), and `games/gamebox`. Each tool's objects are combined with
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

Executing userland in place from flash would free RAM rather than
flash. An a.out linked at 0x20000000 cannot do it; a binary linked for
a fixed flash address with its data in the user window could, at the
cost of a new executable format, a loader that validates it, and a
swapper that never restores flash text into the data window. It is
not done.

The kernel itself compresses 35 percent, but it executes in place from
flash and a compressed kernel would have to be copied to RAM, which
costs 90 KB of the 264.

## The layout

| Region | Address | Size | Content |
|---|---|---|---|
| boot2 and kernel | 0x10000000 | 128 KB | 90 KB used |
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
(`FLASH_UNIT_BYTES`) followed by `dhara_map_sync()`, and that unit programs
four 256-byte program pages. Buffer coalescing can combine several inode changes,
and Dhara garbage collection decides the physical program and erase count.
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
errors count dhara_map_sync calls, including a successful no-op on an already
clean map. They are not counts of checkpoint pages written. NOR program
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
next call; a failed write latches in `m_write_error`. update(8) supplies a
30-second cadence for these calls, not a completion bound: a skipped object
waits for a later call, and scheduling and flash latency add to the
interval. The host gate establishes the selection and error handling;
whether the overwrite reaches flash at the next tick on the board is a
separate hardware observation.
