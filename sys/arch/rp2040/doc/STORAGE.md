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
is saturated. The counters reset only at boot. Capture from the same resident
process where possible: invoking a utility can itself cause swapping.

The logical root and raw-swap request/byte counters count valid requests
submitted to the driver, including requests that subsequently fail. Map
writes count Dhara mapping attempts. Checkpoint attempts, successes and
errors count forced checkpoints, the `dhara_map_sync()` calls of
`fl_checkpoint()` that `DIOCCHECKPOINT` and the last close of a root node
that an open held for writing make,
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

The admission probe counters (the `swapram_*` names after
`swapram_admissions`, snapshot version 2; version 1 ended at
`swapram_admissions`, and `tools/summarize_rp2040_storage_delta.py` reads each
version with its own metric set and rejects a pair of two versions) explain a refused attempt. Each `swapram_reserve()`
call ends in an admission or in one `swapram_refused_*` reason: admission
closed, packed-text decoder held, encoder error, fewer free pool bytes than
the reservation (`nospace`), or enough free bytes without one run
(`fragment`). The want, free and largest sums, the `swapram_short_*` bins and
`swapram_refused_data_fits` describe the pool refusals; the scan, zero and
chunk counters describe the data and stack bytes of every call, split by
outcome. `swapram_ram_span_NN` and `swapram_flash_span_NN` count completed
swap-outs by image length in 4 KiB sectors, the unit a flash swap-out erases.
Attempts equal admissions plus `swapram_flash_swapouts`,
`swapram_exchange_swapouts` and `swapram_sched_refusals`. A refused fork
counts its reservation reasons but no attempt. The counters add 208 bytes
of data and about 560 bytes of text to the PICO kernel, and the snapshot grows
from 124 to 332 bytes.

Use paired captures for a clean idle interval, partial-block overwrites,
full-block rewrites, sequential and random reads, linker scratch traffic,
and explicit synchronous writes. Keep buffer configuration, image contents
and initial free space fixed and report logical requests, map operations,
checkpoint calls, NOR programs, NOR erases and swap separately. This tree
uses 1024-byte Dhara pages: one logical 1 KiB write attempts one map write,
although programming its data can require four physical 256-byte pages.
Counter instrumentation alone leaves the coalescing and read-ahead policies unchanged.

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

### Erases per swap sector

The raw swap region (384 KiB, 96 sectors of 4 KiB) lacks wear leveling: an
image lands at the sector its reservation names, so a workload that swaps one
command in and out erases the same few sectors repeatedly while
`swap_erase_sectors` in `machdep.storage_stats` reports only the total. A
`STORAGE_STATS=yes` kernel keeps one 16-bit erase counter per swap sector
(`flash_swap_wear` in `dev/flash.c`, updated by `flash_wear_note()` in
`dev/flash_wear.h` from `flash_erase()`) and returns the array through the
read-only `machdep.flash_swap_wear` (id 53). Counters start at zero at boot,
count every erase of their sector since then, and saturate at 65535 instead of
wrapping, so 65535 reads as "at least 65535". A 64 KiB block erase counts each
of its sixteen sectors once, since the chip erases the whole block. Erases
below `FLASH_SWAP_OFFSET` (the Dhara root) are not counted. `sysctl
machdep.flash_swap_wear` prints `sectors N` and then the counters, sixteen
per row, sector 0 first; the call returns EOPNOTSUPP on kernels built without
`STORAGE_STATS`, which contain neither the array nor the update (their text,
data and bss sizes equal a tree without it, and the images differ only in the
build stamp). Each counter is one
halfword store under `splhigh()`, so a read sees every counter whole but the
array as it stood while the copy ran. The array costs 192 bytes of bss, no initialized
data, and 96 to 120 bytes of text on a diagnostic kernel (six configurations,
GCC 16.2). Take two reads around a
workload and subtract per sector, unless a counter reads 65535. The counters
are requested-erase counts, as `storage_stats` is, and measure requests, not electrical wear; `check-flash-wear` holds the counting rules over a model,
and the board has not been asked.

### Reset at the Nth flash command

`flash_program()` and `flash_erase()` hand the boot ROM a whole request with
interrupts masked, so a reset requested over USB lands before a request or
after it. A Dhara checkpoint page is one 1 KiB request of four 256-byte chip
program commands, and the host oracle of issue #619 found that a reset between
two of them leaves a page that recovery accepts as the newest root. A
`STORAGE_STATS=yes` kernel carries a hook that makes that cut reachable: while
armed it issues the request to the ROM one chip command at a time, counts the
commands of the armed kind, and resets the chip once the Nth has completed.
The hook is a diagnostic that destroys the root on purpose: the cut leaves a
torn flash page or sector, which is the state under test, and the board needs
its image flashed again to restore a known root. Kernels without
`STORAGE_STATS` do not contain it (their text is byte-identical to a tree
without the hook), and the two sysctl names answer `EOPNOTSUPP` there.

| Node | Read | Write (root only) |
| --- | --- | --- |
| `machdep.flash_reset_program` | program commands counted since arming | N > 0 resets after the Nth program command from now; 0 disarms |
| `machdep.flash_reset_erase` | erase commands counted since arming | the same for erase commands |

The two kinds arm independently, and a command of one kind neither counts
toward the other nor fires it. Arming restarts the count, and the count is
RAM state that a reset discards. A chip command is what the ROM's own loop
issues: a 256-byte program page, a 4096-byte sector erase, or a 65536-byte
block erase where the range holds an aligned whole block (the swap region's
large erases; the root's 8 KiB Dhara blocks are two sector commands). Armed
requests pass the ROM the same block size and block command an unarmed request
passes. The count includes raw swap programs and erases, which lie at
`FLASH_SWAP_OFFSET` and above, so a trial runs with no swapping process.

At the Nth command the hook calls the ROM's flush routine, re-enters
execute-in-place through the saved second stage, records the reset in the
watchdog scratch pair, parks core 1 where the kernel has one, and writes
`AIRCR.SYSRESETREQ` from RAM. Re-entering XIP first leaves the QSPI interface
as `usb_reset_to_flash()` and `boot()` leave it, and puts the flash-resident
`core1_park()` and `watchdog_site()` back in reach. `SYSRESETREQ` resets the
processor core and nothing else (RP2040 datasheet 2.4.2.9), so the QSPI
interface keeps the state the hook leaves, and the warm boot's XIP exit
sequence (2.8.1.2) is written for a flash in continuous-read mode, the state
every other reset of a running kernel hands it. The next
boot reads the cause as `machdep.watchdog_site` 5 (reset after a program
command) or 6 (after an erase command), and `machdep.watchdog_arg` holds the
flash offset of that last command, so a trial confirms where the cut landed.
The chip's own state is not interrupted: the ROM waits out each command's busy
bit, so the cut is between two complete commands. A power loss inside a
command is a different fault and is not reached.

Board procedure, one trial per freshly flashed image so that every trial
starts from the same root. Calibration run:

1. Boot the `STORAGE_STATS=yes` kernel, log in as root, `sync; sync`.
2. `sysctl -w machdep.flash_reset_program=1000000` arms a count the run does
   not reach.
3. `dd if=/dev/zero of=/probe bs=1k count=3`, then `sync`.
4. `sysctl machdep.flash_reset_program` reads T, the program commands the
   workload and the sync issued; read it twice, and a nonzero difference is
   the program commands of the utility's own fork (a swap-out that reaches
   flash counts), which makes T a starting guess only. `ufs_sync()` ends with
   the forced checkpoint, whose page is one 1 KiB program request, so
   commands T-3 to T are its four 256-byte chunks (T-3 the first, T the last,
   which completes the page) and T-4 is the last command before it. The
   offset check below, not the arithmetic, decides what a cut hit.

Sweep: for each k from T-4 to T, flash the same image again, boot, `sync;
sync`, arm `sysctl -w machdep.flash_reset_program=k`, run the same `dd`, and
`sync`. The board resets inside the `sync`. After the next boot read
`machdep.watchdog_site` (5) and `machdep.watchdog_arg` (offset a). A Dhara
group is one 8 KiB block (seven data pages and the checkpoint page, the
`8 * ceil(N / 7)` program pages of the checkpoint cost above), and the root
starts at a multiple of 8 KiB, so a cut hit the checkpoint page when a is
below `FLASH_SWAP_OFFSET` (0x1a0000 on a plain Pico) and a mod 8192 is at
least 7168; the chunk is (a mod 1024) / 256, counted from 0. Move k until a
names the chunk the trial wants.
Then check the root: fsck(8), `ls -l /probe`, and a read of its three blocks. k = T-4 and k = T are the controls: the checkpoint is absent
or complete. The T-3 to T-1 cuts are the cases the host oracle says recovery
accepts. If T differs between calibration runs from the same image, a swap
program or another write ran inside the window and the offset check will
show it.

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
`IO_SYNC` writes to files and directories, unmount and shutdown each force
one, and so does the last close of a root-unit node that any open since its
previous last close held for writing (a node held only for reading and the
swap unit force none); a checkpoint with
nothing pending programs nothing. An `IO_SYNC` write (a descriptor opened `O_FSYNC`) to a block device node lacks a filesystem to sync, so `rwip()` calls `bdev_checkpoint()` on
the written device after the buffer write and returns its error; a write without `IO_SYNC` skips the checkpoint. fsync(2) on a block device descriptor drains the
represented device's buffers, then checkpoints that device with
`bdev_checkpoint()` and returns its error, before `ufs_sync()` syncs and
checkpoints the filesystem that holds the node, which may sit on another
device. A
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
`EIO`. Through `ufs_sync()` it also latches in the mount's `m_write_error`
(the synchronous block-device write above returns it without a latch, as it lacks a mount), under the rule that
already governs a failed buffer write: the first error is kept, every later
`ufs_sync()` of that mount returns it, and nothing clears it while the
mount exists. The slot is reset only when a mount is made (`mountfs()` loads
the device's recorded error) and by an unmount over media that
`DIOCMEDIAGONE` reports removed; the flash driver never reports removal, so a
flash mount with a latched error refuses unmount, and the root keeps the
error until reboot. sync(2) returns void, so its failure surfaces
through that latch, a later fsync(2) and the shutdown report, which also
prints a failed shutdown checkpoint.

A process forces a checkpoint with `ioctl(fd, DIOCCHECKPOINT, &status)` on a
flash block node it opened for writing; `flioctl()` refuses a descriptor
opened read-only with `EBADF`, and opening the node requires the node's
permissions. `check-flash-checkpoint` holds the driver to these properties
over a NOR model, `kernel.ufs-disk.ilp32` holds the filesystem's sync paths
to issuing the checkpoint, and `check-rp2040-shutdown` holds shutdown to
forcing it. One board observation exists, on the measured Pico with the PICO
kernel and image of this policy: seven warm resets forced through the USB
reset interface (`picotool reboot -f`), no power interruption. A directory
followed by sync(2) survived; a file written 45 seconds before the reset, with
no sync(2) call, survived by the update(8) checkpoint; directories made
seconds before a reset were lost or salvaged, `fsck -p` repaired the root and
rebooted, and the following boot checked clean each time. One boot after
such a repair left the USB console silent until the next forced reset, which
checked clean; the cause is not established. Five later resets through
the same repair path and fourteen further forced-reset trials, nine of
which took it, did not repeat it (issue #569 holds the captures and the
ranked hypotheses). The reset matrix
over write, erase and checkpoint phases and every power interruption (W03 of
`docs/research/rp2040-memory-wear-engineering-program.md`) has not been run.
Production
`dhara_nand_prog()` passes one 1 KiB extent to `flash_program()` while
`dhara_nand_copy()` passes four 256-byte extents, so the equivalent count is
neither a wrapper-call count nor a ROM-command count. N writes followed by one checkpoint
program `8 * ceil(N / 7)` pages while the journal stays below the
`auto_gc()` threshold. An image that maps all 989 sectors puts
the journal at that threshold: rewriting recently written sectors in
batches then costs about 6.3 pages per write, because each write first
copies live tail pages. At full occupancy, checkpointing on sync rather
than per request therefore saves about a fifth of the programs (8 to about
6.3 pages per write in that trace, 6.0 in `check-flash-checkpoint`'s), not
the six sevenths a journal below the threshold gives. The shipped image
leaves its free blocks unmapped (`check-flash-sparse`) and starts below
full occupancy; a root that has since written every free block reaches it. With 75 percent or less of the capacity live, the
same traces stay below the threshold and cost 1.2 pages per write. These are
host-model counts for the exercised traces; electrical completion, command
execution inside the ROM and physical-sector wear require separate board
measurements.

The threshold is a local change to the vendored Dhara (`auto_gc()` in
`dhara/map.c`; `dhara/README.md` lists the local changes). Upstream starts
collecting when the journal holds `dhara_map_capacity()` user pages (989),
which leaves at most 989 minus the live count as garbage. The port starts
at journal capacity less the 64-page safety margin less a quarter of the
live sectors less one 8-page checkpoint group (1,316 - 64 - 758 / 4 - 8 =
1,055 for the shipped image), so more
garbage stays in the journal and a write copies fewer live pages. The
capacity the map advertises stays 989. With this trigger the trace above
costs 4.91 pages per write at 90 percent live, against 6.28 with
upstream's, and 1.20 at 80 percent; at 100 percent it is unchanged.
On the measured Pico, `cc -o d d.c; sync` repeated 24 times cost 153.0
root erase sectors and 3.75 units per 1 KiB write per compile, against
183.9 and 4.49 with the upstream trigger. `check-dhara-gc-trigger` holds the
trigger to an adversarial workload that keeps the journal tail live
without the journal filling or entering the safety margin.
`research/dhara-gc-trigger.md` gives the bound and the measurements.

`flashimg` therefore maps only what the system reads. It walks the free list
of each UFS partition of the disk image (superblock entries, then each chain
block that entry 0 names) and writes every 1 KiB sector except the free
blocks that do not chain the list; those stay unmapped and read as 0xff
bytes. Nothing reads such a block before it is allocated: `alloc()` in
`sys/kern/ufs_alloc.c` reads only the chain block and takes a data block
through `getblk()` without a read, `pass5()` in `sbin/fsck/pass5.c` reads
only chain blocks, and `expanddir()` in `sbin/fsck/dir.c` reads a block it
has just allocated and overwrites all of it. The walk must account for the
superblock's `fs_tfree` blocks exactly, or `flashimg` refuses the image. The
`full` PICO image built at this change has 988 filesystem blocks: 756 in
use, 1 chain block and 231 free blocks left unwritten, so with the partition
table 758 of the 989 capacity sectors are live (76.6 percent) where all 989
were. Repeating the host model's trace at 758 live sectors gives 1.20 pages
per batched write
against 6.28 at 989, and 8.00 per write with a checkpoint after each, as
before. The share grows as the system allocates blocks, since a written
block keeps its Dhara sector mapped after it is freed: the kernel issues no
trim, because trim at free cost more root erases on the board
than leaving freed blocks mapped
(`docs/research/unimplemented-storage-work.md`).
`check-flash-sparse` reads a flashimg region back through the map.
