# Filesystem and storage drivers: the clean flag, SD against the Dhara root, and an overhaul plan

This note measures what a filesystem clean flag (2.11BSD patch 456, with
its follow-ups 458 and 461) would save at boot on each root this port
mounts, records the falsifiers for that benefit, describes the storage
stack as built layer by layer, audits the disk and controller drivers
against the reference trees, takes a C17 census of the storage sources,
and decomposes the work into slices of one pull request each. It extends
`docs/research/memory-devices.md` (pull request #649, branch
`docs/memory-devices`), whose device inventory, wiring and slices M1-M19
it cites rather than repeats, and `docs/research/upstream-patch-survey.md`
(pull request #645), whose ledger items `ufs-clean-state-transition` and
`ufs-superblock-clock-order` it resolves into a design. Owner decisions
D105 (this study), D106 and D109 (defect fixes before the GizmoBSD rename,
new implementation after it) bind the plan.

Evidence classes: source read (SRC), host execution (H), Renode execution
(R), model arithmetic from measured inputs (M), inference (I), and the
existing board observations this note cites (B, never new here). No board,
serial port or network was used. The tree was read and built at
`origin/main` 4b3eaf6c; kernels and images were built with
arm-none-eabi-gcc 14.2.1 (`15:14.2.rel1-1`) on a separate Linux build host,
and the host programs with the build host's gcc 16.2.1. Renode is
1.17.0+20260907gitf1dd1b4af with the RP2040 models pinned at 205a5e4b; the
baseline `check-renode` gate passed on that build (exit 0).

## Finding and short answer

**As 2.11BSD built it, the clean flag does not pay on the flash root,
barely pays on the 32 MiB SD root of pull request #558, and pays on SD
filesystems that are not the mounted root, in proportion to their size.** On the Dhara root the whole
boot-time `fsck -p`, from rc's banner to fsck's summary line, takes
0.08-0.10 virtual seconds of a 3.85-3.99-second boot to the login prompt
under Renode (R, three runs, 0.02 s resolution; Renode counts one
instruction per cycle at 125 MIPS and models no flash latency), and its
host-measured I/O is 92 KiB of reads (H). A clean flag can remove at most
that span, about 0.10 to 0.15 s on the board once the Cortex-M0+'s cycles
per instruction are counted (I). It would add a whole reboot, 4.3-4.4 virtual s, after every unclean stop
that left the root consistent, because fsck's write of the clean bit on a mounted root counts
as a modification and `/etc/rc` answers exit 4 with `reboot -n`. On an SD
filesystem the check grows with the inode table and the free-list chain:
the host count of fsck's reads gives 0.4-0.6 s of SPI read time for an
empty 32 MiB filesystem (the size of the SD root of pull request #558),
1.7-2.9 s half full, and 55-92 s for a half-full 1 GiB filesystem, before
CPU time and scratch writes (M). Above
about 58 MiB fsck's maps no longer fit its 16 in-core buffers and spill to
`/dev/temp0`, which the kernel carves from the swap device; on the raw flash
swap every scratch rewrite erases two 4 KiB sectors, so checking a half-full
1 GiB filesystem costs about 1,150 sector erases of `fl1`, and a filesystem
over about 1 GiB cannot be checked at boot at all on a board whose swap is
the 384 KiB flash region (H, M). The SD root of #558 is itself a mounted
root (its `/etc/fstab` names `/dev/sd0c`), so it pays the same reboot:
its check of about 0.4-0.6 s of SPI time breaks even at one consistent
unclean stop per 7-11 clean boots (M). A variant in which fsck hands a
clean result on the mounted root to the kernel instead of writing it
removes that reboot (section 8, decision 1).

**The donor patch cannot be imported as written.** Its `MNT_CLEAN` value,
0x2000, is this tree's `MNT_V7` (`sys/sys/mount.h:102`). Its mount-time
`ufs_sync()` stamps `fs_time` with a clock the port has not yet set,
because `main()` seeds the clock from the root superblock's `fs_time` only
after `mountfs()` returns (`sys/kern/init_main.c:133`); that is the defect
patch 458 repairs in 2.11BSD, and here it would zero the only clock the port
keeps across a reboot. Its shutdown path marks filesystems clean on the
first panic, because 2.11BSD's `panic()` reaches `boot()` without
`RB_NOSYNC` and patch 461 put `fsclean()` in place of `sync()` there; this
port's `panic()` (`sys/kern/subr_prf.c:581-591`) behaves the same way. The
image tools write `fs_flags` 0 (`tools/fsutil/superblock.c:401`), so every
shipped image would boot as dirty once.

**Unclean stops on the Dhara root do leave fsck work.** The board record in
`sys/arch/rp2040/doc/STORAGE.md` ("Checkpoint cost") has directories made
seconds before a forced reset lost or salvaged and `fsck -p` repairing the
root (B). The Renode trials of this note agree (R): a metadata workload
cut by `reboot -n`, at once or two seconds later, left the superblock's free
list behind the inodes, and fsck reported "ALLOCATED INODE(S) IN IFREE
LIST", "34 DUP BLKS IN FREE LIST" and "BAD FREE LIST (SALVAGED)", then
rebooted; the same workload followed by `sync` checked clean, and an
in-place `dd conv=notrunc` rewrite cut the same way checked clean. The free
list and the free-inode cache live in the in-core superblock and reach the
medium only at a superblock write, so an allocation whose inode or data
blocks reach flash before the next superblock write leaves fsck work,
whatever the translation layer below does. The extra reboot costs
4.3-4.4 virtual seconds from banner to banner (R), so on the root the donor
semantics break even at about one consistent unclean stop per 45-55 clean
boots.

**Five driver gaps lead the audit** (section 4): the SD driver retries
nothing, so one CRC or timeout error fails the whole buffer with `EIO`;
fsck's scratch maps wear one spare sector of the raw swap on every rewrite;
the SD path moves one sector per command at 12.5 MHz, one polled byte at a
time; flash program and erase are never verified; and both drivers
busy-wait synchronously, the SD write for up to 500 ms per sector.

The plan has 22 slices: three defect fixes that may land before the rename
and nineteen implementation slices after it. Six owner decisions remain
(section 8).

## 1. The storage stack as built

| Layer | Code | Interface contract | `struct buf` ownership and lifetime | Synchronization | Errors | Tests that exercise it |
| --- | --- | --- | --- | --- | --- | --- |
| System calls | `sys/kern/ufs_syscalls.c`, `ufs_syscalls2.c`, `vfs_vnops.c`, `sys_inode.c` | file operations reach `rwip()`; `sync()` (`ufs_subr.c:32`) visits each mount and calls `ufs_sync()` (`ufs_syscalls2.c:192`), which writes inodes, delayed buffers, then the superblock when `fs_fmod` is set, and ends with `bdev_checkpoint()` (`:235`) | none held across the call | `updlock` serializes `sync()`; `fs_ilock`, `fs_flock` skip a busy mount | latched in `m_write_error` and returned by fsync, IO_SYNC writes and unmount | `check-kernel` (`kernel.ufs-disk.ilp32`), `check-storage-correctness`, `check-fs-stress`, `check-ufs-disk-mutants` (H) |
| Inode and namespace | `ufs_inode.c`, `ufs_namei.c`, `ufs_bmap.c`, `ufs_alloc.c` | `bmap()` maps logical to filesystem blocks; `balloc()` refills the superblock free list and writes a superblock image (`ufs_alloc.c:80-105`) | borrowed from the cache for one operation | `ILOCK` per inode; `fs_flock`, `fs_ilock` on the lists | `u.u_error` | as above, `check-fsck-inode-range` for the on-disk invariants (H) |
| Buffer cache | `ufs_bio.c`: `bread` 186, `bwrite` 261, `bdwrite` 297, `brelse` 318, `getblk` 438, `biowait` 522, `biodone` 539, `bflush` 626, `bdevsync` 656 | 1 KiB blocks (`DEV_BSIZE`); `NBUF` 4 on this port (`include/machparam.h:56`) | `getblk` returns the buffer `B_BUSY` to the caller; `brelse` returns it; a `B_ASYNC` write is released by `biodone` | `splbio()`, which on the RP2040 is PRIMASK (`dev/sd.c:46-51`); `biowait` sleeps on the buffer until `B_DONE` | `B_ERROR` and `b_error`; `biodone` latches a write error into the mount and into open block inodes (`:545-566`) | `check-kernel`, `check-storage-correctness` (H) |
| Block switch | `rp2040/conf.c:130-170` | majors 0 `fl`, 3 `psram`, 4 `swap`, 5 `sd`; character major 4 `temp` (`:205`) | passes the caller's buffer | none | none | `check-rp2040-devsw` (H) |
| Flash driver | `dev/flash.c`: `flstrategy` 1033, `fl_raw` 995, `fl_checkpoint` 785, `flioctl` 1157 | Dhara logical 1 KiB sectors for `fl0`; raw bytes for `fl1`; `DIOCCHECKPOINT` (`:1191`) | completes the transfer and calls `biodone` before it returns: every request is synchronous | `splbio` around the map; the ROM calls run at `splhigh` with XIP down (`:559`, `:588`) | `EIO` on a map or ROM failure; a write past the writable end is refused before the ROM call (`:557`) | `check-flash-checkpoint`, `check-flash-sparse`, `check-flash-reset-cells`, `check-flash-device-bounds`, `check-dhara-*`, `check-flash-swap` (H); `check-renode` (R) |
| Translation layer | `dhara/journal.c`, `dhara/map.c` (vendored, local changes in `dhara/README.md`) | a write is durable at the next checkpoint: a filled group or `dhara_map_sync()`; a reset returns the map to the last checkpoint, which covers the writes before it in the order the journal appended them (I, from the journal's append-only design; a torn checkpoint breaks it until pull request #636) | none | under the driver's `splbio` | `DHARA_E_*` to the driver | as above; `docs/research/dhara-audit.md` |
| Raw swap and temporary units | `dev/flash_swap.c`: `flash_swap_append` 21, `flash_swap_rewrite` 62; `sys/dev/swap.c`: `swtemp_write_flags` 43, `TFALLOC` 265 | images append into erased sectors; a temporary unit appends from block 0 in order and rewrites through the spare sector from its first out-of-order write on | synchronous | `splbio` | `EIO` | `check-flash-swap`, `check-swap-*` (H) |
| SD driver | `dev/sd.c`: `sdopen` 288, `sdstrategy` 328, `sdioctl` 432, `sddump` 520, `sd_detect_poll` 193 | 512-byte sectors under 1 KiB blocks, MBR partitions, `DIOCMEDIAGONE`, `DIOCREINIT` | synchronous; `b_resid` names every byte that did not move | `splbio` around the replaced flag only; transfers run with interrupts enabled (`:30-35`) | `EIO` on the first failed sector, no retry | `check-picocalc-host` (`sdstrat_test`, 56 mutants), `check-renode-picocalc-dump` (R) |
| SD protocol | `dev/sdspi.c`: `sd_cmd` 122, `sd_rx` 174, `sdspi_init` 224, `sdspi_read_span` 322, `sdspi_write_span` 344 | SPI mode, CMD0/8/59/41/58/16/9, CMD17 and CMD24 only, CRC7 and CRC16 checked | n/a | none | `SDE_*` codes; time limits 1 s, 100 ms, 500 ms (`:55-57`) | `check-picocalc-host` (`sdspi_test`, 30 mutants) |
| SD controller | `dev/sd.c:126-153`: PL022 SPI0, byte-polled `sd_xfer`, 398 kHz then 12.5 MHz | one byte per call, waits for receive before the next send | n/a | none | none | same |
| PSRAM | `dev/psram.c:63`, `dev/psram_xfer.c` | 1 KiB blocks of the 8 MiB part, swap minor 0 | synchronous | frames run with interrupts masked (memory-devices note) | `EIO`, `EINVAL` | `check-psram-pio` (H); never run on a board |
| Boot check | `etc/rc.rp2040:21-43`, `sbin/fsck` | `fsck -p` checks every fstab entry of pass 1 in the parent; exit 4 on a modified mounted root gives `reboot -n` | n/a | n/a | exit 2, 8, 12 stop the boot | `check-fsck-inode-range` (H), `check-renode` (R) |

The root is mounted read-write before fsck runs, `async` and
`noaccesstime` (`etc/fstab.rp2040`). `mountfs()` sets `fs_fmod` on every
read-write mount (`ufs_mount.c:270`) and replaces the in-core `fs_flags`
with the mount flags (`:275`), and `m_flags` is that same word
(`sys/sys/mount.h:57`); the next `ufs_sync()` writes it to disk. On-disk
`fs_flags` therefore holds the last mount's flags (0xc0 for the root after
`mount -a` applies `async` and `noaccesstime`, I) and
carries no state of its own today. The first Renode boot's fsck reports
102 files and 756 blocks where the host check of the same image reports 101
and 755, so one inode and one block are allocated between the root mount and
rc's fsck; which program allocates them was not traced.

## 2. Patches 456, 458 and 461, and the 4.4BSD clean flag

Read in full from the 211bsd repository (`PATCHES.md` rows 44-49):
456 is `e4dc5d6` (10 files, +106 -14), 458 is `43746ac`, and 461
(`47b8ef2`) moves the shutdown call. Two leads of the survey that preceded
this note are corrected here: `fsclean()` is called from `boot()` (in
`sys/pdp/machdep2.c`), not from the sync path, and 4.4BSD-Lite2 does carry
an FFS clean flag (`sys/ufs/ffs/fs.h:219` `fs_clean`, `:236` `fs_state`;
`ffs_vfsops.c:158-186, 497, 562-565`; `sbin/fsck/setup.c:145`), which a
substring search for `fs_clean` had hidden behind `lfs_cleansz`.

### State machine

| Event | 2.11BSD after 456, 458 and 461 | 4.4BSD-Lite2 FFS (`ffs_vfsops.c` lines are Lite2's; LiteBSD lines where named) |
| --- | --- | --- |
| newfs | no change: the bit starts clear | `fs_clean = 1` (LiteBSD `sbin/newfs/mkfs.c:434`) |
| read-write mount | `WASCLEAN` set if the disk said `CLEAN`; in-core flags become the mount flags (so `CLEAN` clear); `ufs_sync()` at once if `fs_fmod` (`ufs_mount.c` +2 lines) | `fs_clean = 0`, synchronous `ffs_sbupdate(MNT_WAIT)` (`ffs_vfsops.c:496-499`) |
| periodic `sync()` | stamps `fs_time` before `ufs_sync()` (458); the written image carries `CLEAN` clear | no change |
| `balloc()` superblock write | 458 removes its `fs_time` stamp | n/a |
| unmount | `CLEAN` set if `WASCLEAN`, `fs_fmod` set, `fs_time` stamped (458), `ufs_sync()` | `fs_clean = 1`, synchronous write, reverted on error (`:561-566`) |
| shutdown | `fsclean()` in place of `sync()` (461): `CLEAN` set where `WASCLEAN`, `ufs_sync()`, `CLEAN` cleared in core again | no clean marking at reboot |
| panic | first panic: `boot()` without `RB_NOSYNC`, so `fsclean()` marks clean | no marking |
| rw to ro update | not supported (`EPERM`) | `fs_clean = 1`, synchronous write (`:152-165`) |
| fsck | skips when `CLEAN`, or when the root is mounted and `WASCLEAN` is on disk, unless `-f`; after a check with no "no" answer marks `CLEAN|WASCLEAN` | skips when clean and `skipclean`; `ckfini(markclean)` marks clean (LiteBSD `sbin/fsck_ffs/utilities.c:302-311`) |

Patch 458's title, "superblock modified time set to 0 on boot", names the
mechanism: 456 added a `ufs_sync()` at the end of `mountfs()`, and
`ufs_sync()` stamped `fs_time = time.tv_sec` while the clock still read 0.
458 moved the stamp from `ufs_sync()` and `balloc()` to `sync()` and
unmount. In this tree `ufs_sync()` stamps at `ufs_syscalls2.c:225`,
`balloc()` stamps its image at `ufs_alloc.c:100`, and the clock comes from
the root superblock after the mount (`init_main.c:133`), so the two ledger
items are one design question: the mount-time superblock write must carry
the on-disk `fs_time` it read, not the unset clock.

### On-disk bits and compatibility

`struct fs` has `u_int fs_flags` (`sys/sys/fs.h:60`), 32 bits, of which the
mount flags use 0x00ff, `MNT_UPDATE` 0x1000 and `MNT_V7` 0x2000
(`mount.h:78-102`); `MNT_VISFLAGMASK` 0x0fff bounds what statfs reports.
`MNT_V7` is passed only by mount(8) and never reaches a native superblock,
because the V7 path loads its own format (`ufs_mount.c:246-257`), but the
in-core word is shared, so the donor's 0x2000 cannot be reused. 0x4000 and
0x8000 are free and stay outside the statfs mask; no new superblock field
is needed and the layout is unchanged.

Older images stay readable; one direction keeps a window:
- An image written by any kernel or tool of this tree has neither bit set
  (fsutil writes 0, a kernel writes its mount flags), so a new kernel's
  fsck checks it once, marks it clean, and on the mounted root reboots once.
- An old kernel mounting a clean-marked image keeps the bit on disk until its
  first superblock write, which replaces the word with its mount flags and so
  clears the bit. The window is `fs_fmod` set at mount and written at the
  first `sync()`, up to one update(8) interval: an old kernel that writes
  data and stops inside that window leaves a dirty filesystem marked clean.
  A mount-time synchronous write closes the window only for new kernels.
- The image tools should write `CLEAN` into a new filesystem, as LiteBSD's
  newfs does; otherwise the first boot of every image runs the full check
  and an extra reboot.

### Design constraints this tree adds

1. The dirty mark must reach the medium before any other write of the
   mount. On Dhara a checkpoint covers the writes appended before it (I;
   `dhara-audit.md` for the torn-checkpoint exception), so a synchronous
   `bwrite()` of the superblock issued first suffices without a forced
   checkpoint: any later write that survives carries it. On SD the
   card acknowledges CMD24 only after the data response and busy phase, so
   a synchronous write ordered first suffices too, short of the card's own
   power-loss behavior (I).
2. The clean mark must be the last write before the checkpoint that makes
   the shutdown durable: `rp2040_shutdown_sync()` already runs `sync()`,
   drains buffers and forces the root checkpoint
   (`rp2040/shutdown_sync.c`), so the mark belongs between the drain and the
   checkpoint, and only when no buffer stayed busy, no `m_write_error` is
   latched and `panicstr` is NULL.
3. The mount-time write keeps the on-disk `fs_time` (458's ordering).
4. On a mounted root the clean mark costs a reboot: fsck's write of the
   superblock sets `dfile.mod` (`sbin/fsck/utilities.c:244-246`), which
   ends a mounted-root check in `sync(); exit(4)` (`main.c:259-262`), and
   rc answers with `reboot -n`. That holds for the flash root and for the
   SD root of #558 alike. The benefit on a root is the clean-boot saving
   minus the reboots added by unclean stops that needed no repair
   (section 3), unless fsck passes a clean result on the mounted root to
   the kernel (a mount update that sets `WASCLEAN` in core) and writes
   nothing, so the next orderly shutdown writes the mark.

## 3. Measured fsck cost and the clean-flag falsifiers

### The flash root under Renode

`docs/research/filesystem-storage-overhaul/renode_fsck_timing.py` boots the
PICO_UART kernel on the `check-renode` machine with the built
`distrib/rp2040/flash.bin`, advances virtual time in 0.02 s slices inside
the measured spans, and ends each trial with `reboot -n`, which skips
`sync()`, the buffer flush and the shutdown checkpoint, so the next boot
sees the root as a processor reset at that instant leaves it. Two controls
decide whether its rows count: the synced trial must check clean and the
open-unlinked trial must be repaired.

| Trial | Boot | fsck span (virtual s) | Outcome | fsck output | Files, used blocks |
| --- | ---: | ---: | --- | --- | --- |
| first boot of the image | 0 | 0.08 (0.08-0.10 over three runs) | clean | summary only | 102, 756 |
| `fsck -p` on a missing node at the root shell | - | 0.36 | floor | "Can't stat", "CAN'T CHECK FILE SYSTEM" | - |
| `sync; fsck -p /dev/fl0a` at the root shell | - | 0.48 | refused | "Can't open /dev/fl0a" | - |
| workload, `sync`, cut (negative control) | 0 | 0.08 | clean | summary only | 106, 793 |
| open file removed, `sync`, cut (positive control) | 0 | 0.08 | repaired | BLK(S) MISSING (SALVAGED) | 106, 793 |
| | 1 | 0.06 | clean | summary only | 106, 793 |
| workload, cut at once | 0 | 0.08 | repaired | ALLOCATED INODE(S) IN IFREE LIST (FIXED); 34 DUP BLKS IN FREE LIST; BAD FREE LIST (SALVAGED) | 110, 829 |
| | 1 | 0.06 | clean | summary only | 110, 829 |
| workload, two seconds, cut | 0 | 0.12 | repaired | as above | 114, 865 |
| | 1 | 0.08 | clean | summary only | 114, 865 |
| synced file rewritten in place, cut | 0 | 0.08 | clean | summary only | 115, 866 |

Both controls held, so the rows count (`renode-fsck-timing.tsv`). The
workload is `mkdir` of two directories, `cp /etc/rc` and `cp /bin/sh`
into them, `echo` into a new file, `ln` and `rm`, one command a line. Every
workload's files survived the cut; only the free list and the free-inode
cache disagreed.

The shell row is a second floor, not a check: at securelevel 1 `openi()`
refuses every open of a mounted block device, reads included
(`sys/kern/sys_inode.c:665-669`, the 2.11BSD rule that lets only the
boot-time fsck at securelevel 0 open the root), and the flash driver has no
raw node, so the mounted root cannot be checked from a multi-user shell.

Renode time is an instruction count at the model's `PerformanceInMips` 125
(`cores/rp2040.repl:23`) with flash erase and program taking no time, so it
undercounts the board by the Cortex-M0+'s cycles per instruction (about 1.2
to 1.5 for this code, I) and by every flash wait. Both shell rows include the interactive
shell's fork, which swaps the shell out, so they exceed the whole boot-time
check; the boot span is the cleaner figure.

### fsck's I/O on the host, root and SD sizes

`fsck_io_model.py` builds the tree's fsck for the host at ILP32 (the
`tests/fsck_inode_range` build), runs it under `fsck_io_shim.c`, which
counts reads on the image and appends and rewrites on the scratch device and
sends `/dev/temp0` to a file, and builds images with `tools/fsutil` (one
inode per 16 KiB), empty or half filled with copies of `usr.bin` (H). Its
calibration passed: the shim's byte count equals strace's on the same run
and lies between the geometric floor (superblock, inode blocks, free-list
chain) and that floor plus 8 KiB; a lowered free count draws "FREE BLK COUNT
WRONG IN SUPERBLOCK" and a write; a 64 MiB image opens the scratch device.
The shipped roots, read from `distrib/rp2040/sdcard.img` and
`sdcard-PICOCALC.img` at 4b3eaf6c, need 92 KiB and 100 KiB of reads.

| Size | Fill | Inodes | Used blocks | fsck reads (KiB) | Scratch appends | Scratch rewrites | SPI read time, 1.2-2.0 ms/KiB (s) | Scratch on raw flash, typical (s) | `fl1` sector erases |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 988 KiB root | shipped | 128 | 755 | 92 | 0 | 0 | 0.1-0.2 | 0 | 0 |
| 8 MiB | half | 512 | 2,184 | 311 | 0 | 0 | 0.4-0.6 | 0 | 0 |
| 32 MiB | empty | 2,048 | 2 | 299 | 0 | 0 | 0.4-0.6 | 0 | 0 |
| 32 MiB | half | 2,048 | 9,240 | 1,458 | 0 | 0 | 1.7-2.9 | 0 | 0 |
| 56 MiB | half | 3,584 | 15,851 | 2,541 | 0 | 0 | 3.0-5.1 | 0 | 0 |
| 60 MiB | half | 3,840 | 17,204 | 2,705 | 18 | 2 | 3.2-5.4 | 0.4 | 9 |
| 128 MiB | half | 8,192 | 36,593 | 5,708 | 38 | 52 | 6.8-11.4 | 5.8 | 114 |
| 256 MiB | half | 16,384 | 72,497 | 11,504 | 74 | 126 | 13.8-23.0 | 13.9 | 271 |
| 1 GiB | empty | 65,536 | 2 | 9,326 | 290 | 276 | 11.2-18.7 | 32.1 | 625 |
| 1 GiB | half | 65,536 | 290,869 | 46,051 | 290 | 538 | 55.3-92.1 | 59.0 | 1,149 |

`fsck-io.tsv` beside this note holds every row with the x86 instruction
counts. The SPI figure is a model: per 512-byte sector `sdspi_read_span()`
makes about 530 one-byte exchanges (command, response, start token, 512 data
bytes with a CRC16 fold each, two CRC bytes, deselect byte), and `sd_xfer()`
waits for each received byte before sending the next (`dev/sd.c:126-134`),
so a byte costs its 0.64 us on the wire at 12.5 MHz plus about 0.3 us of
loop, about 0.5 ms per sector, plus the card's read access time, taken as
0.1 to 0.5 ms (I; the SD Physical Layer allows up to 100 ms). CPU time comes
on top: scaled from the Renode root span by the ratio of host instruction
counts it is at most 1.2 s for 32 MiB half full and 60 s for 1 GiB half full, a
crude upper bound, since it counts the exec and fork inside the root span as
checking work (I). Scratch time uses the W25Q16JV typical figures, 45 ms per
sector erase and 0.4 ms per page program; each rewrite through
`flash_swap_rewrite()` erases the spare sector and the destination and
programs 32 pages, so the maxima (400 ms, 3 ms) give eight times as much.

Three thresholds follow from fsck's sources (`sbin/fsck/main.c:73, 324-325`,
`setup.c:103-158`) and are confirmed by the table:
- fsck's maps take about 9/32 byte per KiB of filesystem (block map 1/8,
  state map 1/32, link counts 1/8 at one inode per 16 KiB), and the in-core
  pool is 16 buffers of 1,044 bytes, 16,704 bytes, so a filesystem above
  about 58 MiB spills to `/dev/temp0` (57,344 KiB stays in core, 61,440 KiB
  spills: H).
- `/dev/temp0` is carved from the swap device. On a bare Pico and on a
  PicoCalc without PSRAM that is `fl1`, 380 KiB usable, part of it held by
  the swapped-out images of init and the rc shell while fsck runs, so a
  filesystem of more than about 1 to 1.3 GiB cannot be checked at boot
  (M: the 290 KiB of scratch the 1 GiB rows allocate, scaled to the free
  swap).
  On PicoCalc kernels whose swap is the PSRAM the scratch costs no erase.
- Each scratch rewrite erases the one spare sector at
  `FLASH_SWAP_SCRATCH_OFFSET`, so one check of a half-full 1 GiB
  filesystem spends 538 of that sector's 100,000 rated cycles.

### Falsifiers and verdicts

| Claim | Falsifier | Evidence | Verdict |
| --- | --- | --- | --- |
| "A clean flag saves boot time on the flash root" | the boot fsck span is small beside the boot, and the reboots added after unclean stops outweigh it | 0.08-0.10 virtual s against a login prompt at 3.85-3.99 virtual s (R); one added reboot costs 4.3-4.4 virtual s (R) | falsified for the root as a time saving; the root's check costs one exec and 92 KiB of XIP reads |
| "Dhara's journal leaves fsck nothing to do after an unclean stop" | fsck repairs after a cut | board: directories made seconds before a forced reset lost or salvaged and repaired by `fsck -p` (B, `STORAGE.md`); Renode: both allocating cuts repaired, the in-place rewrite clean (R) | falsified: the free list and free-inode cache live in the in-core superblock, so an allocation whose blocks reach flash before the next superblock write leaves fsck work above any translation layer |
| "A clean flag saves boot time on an SD filesystem" | the SD check is short beside the boot | 0.4-0.6 s for the empty 32 MiB SD root of #558 and 1.7-2.9 s half full; 55-92 s for 1 GiB half full plus about a minute of scratch rewrites on `fl1` (M) | holds above a few MiB for a filesystem that is not the mounted root; on the SD root the exit-4 reboot offsets it unless fsck hands the result to the kernel; settled by a board timing of `fsck -p /dev/sd0a` |
| "fsck can check any card the driver accepts" | a card filesystem whose maps exceed the scratch device | about 1 to 1.3 GiB on the 380 KiB raw swap (H, M) | falsified; the PSRAM swap or larger in-core maps lift it |

The board measurements that settle the remaining numbers: on a bare Pico,
the USB console's host timestamps from rc's banner to fsck's summary on a
clean boot and after a `picotool reboot -f` cut; on a PicoCalc, `time fsck
-p /dev/sd0a` on cards formatted at 32 MiB, 256 MiB and 1 GiB, with
`STORAGE_STATS` swap erase counts before and after.

## 4. Driver audit against the reference trees

The reference drivers were read at the lines given (SRC). RetroBSD
`sys/pic32/sd.c` (977 lines) and LiteBSD `sys/mips/dev/sd.c` (1,061 lines)
are one lineage; NetBSD and OpenBSD `sys/dev/sdmmc/sdmmc_mem.c` are the
native-bus stacks; FreeBSD `sys/dev/mmc/mmcspi.c` (2,378 lines) is the SPI
transport; NetBSD `sys/dev/spi/spiflash.c` is the NOR driver; 2.11BSD
`sys/pdpuba/ra.c` and `sys/pdp/ram.c` are the inherited disk and RAM-disk
drivers.

| Mechanism | This tree | References | Gap | Severity |
| --- | --- | --- | --- | --- |
| Multi-block transfer | CMD17 and CMD24 per sector (`sd.c:53-57`, `:376-382`) | RetroBSD CMD18 with CMD12 (`sd.c:493, 547`), CMD25 with ACMD23 pre-erase and stop token 0xfd (`:570-635`); NetBSD CMD18, CMD25, ACMD23 (`sdmmc_mem.c:2121, 2347, 2360`) | no multi-block; slice M9 of the memory-devices note | medium |
| Bus clock and transfer loop | 12.5 MHz, one byte per call, receive awaited before the next send | RetroBSD 12.5 MHz then 25 MHz with a CMD6 switch to 50 MHz (`sd.c:61-65, 427-458`); LiteBSD up to 50 MHz (`sd.c:54-58, 512-526`) | no 25 MHz (slice M10) and no FIFO pipelining; about 0.95 us per byte against 0.64 us on the wire | medium |
| Timeouts | 1 s init, 100 ms read, 500 ms busy (`sdspi.c:55-57`) | RetroBSD per-phase limits with maxima published for sysctl (`sd.c:68-123`); FreeBSD 3 s (`mmcspi.c:101`) | limits present; no observed-maximum counters | low |
| Retry policy | none: the first failed sector fails the buffer (`sd.c:376-389`) | FreeBSD retries every command and the stop (`mmcspi.c:100`, `:1439`, `:2082`); NetBSD retries identification (`sdmmc_mem.c:345`) | with CRC checking on, a single transfer error reaches the filesystem as `EIO` | high |
| Card identification | CSD only; type SDSC v1, v2, SDHC (`sdspi.c:288`) | RetroBSD defines CMD10 (`sd.c:133`) and never issues it; NetBSD's `sdmmc_mem.c` reads the CID during its scan (`:345-346`) | no CID, so no card identity for logs or for a replaced-card check at open | low |
| Capacity classes | CSD v1 and v2, byte and block addressing, 32-bit sector count | same in all | none in the driver; the filesystem check caps usable card size (section 3) | medium (fsck) |
| Partitions | MBR primary entries inside the card (`sd.c:243-271`) | RetroBSD MBR (`sd.c:5-11`); 2.11BSD `ra.c` disklabels (`:32-57`) | no extended or GPT tables | low |
| Removable media | card-detect debounce, replaced flag, `DIOCMEDIAGONE` (`sd.c:186-234`) | RetroBSD and LiteBSD have none; FreeBSD's `mmcspi.c` lists card presence detection as not implemented (`:68-69`) | ahead of the references; no event to userland | low |
| Erase and discard | none | FreeBSD erase commands (`mmcspi.c:1059-1060, 1088`); NetBSD discard (`sdmmc_mem.c:2487`) | no TRIM path; the filesystem frees no blocks to the card | low |
| Power-down | none; ALDO1 belongs to the PMU (memory-devices note) | FreeBSD's `mmcspi.c` leaves card power control unimplemented (`:70`) | none actionable from the RP2040 | low |
| Concurrency | synchronous busy-wait with interrupts enabled; a write may hold the CPU 500 ms | RetroBSD and LiteBSD also polled; FreeBSD's `mmcspi.c` does not yield during busy (`:72`); NetBSD has DMA block paths (`sdmmc_mem.c:105-108`) | no DMA, no interrupt-driven transfer; long stalls of every process | medium |
| Flash program and erase | ROM calls at `splhigh`, no read-back (`flash.c:544-604`) | NetBSD `spiflash.c` erases only when a sector needs it and never reads back (`:402-470`) | a NOR failure is invisible to Dhara, which treats only refusals as bad blocks (`:557`) | medium |
| Flash wear | Dhara spreads the root; raw swap appends; temporary rewrites copy through one spare sector (`flash.c:1018`) | none comparable | the spare sector takes one erase per scratch rewrite (section 3) | medium |
| Flash checkpoint integrity | torn-checkpoint fix in pull request #636 | `docs/research/dhara-audit.md` | owner decisions D26 and D28 open there | high (tracked) |
| RAM disk and external RAM | none on the root; PSRAM swap minor; a `/tmp` minor on #624 | 2.11BSD `ram.c` (153 lines); RetroBSD `sdramp.c`, a swap device on external SDRAM (`:1-7`), the PSRAM driver's counterpart; LiteBSD `vn.c`, a file-backed disk (`:43-60`) | none needed beyond #624; a file-backed disk has no user on this port | low |

## 5. C17 census

Instruments: `tools/c17/kr_scan.py` at 4b3eaf6c, the C17 source
denominator `tools/c17/source-denominator.json` (schema 2), lizard 1.24.1
(cyclomatic complexity above 15), Cppcheck 2.22.0 (`--enable=warning,portability`),
jscpd 4.0.5 (clones of 60 tokens or more) and a lexical count. Scope: the 41
files of `sys/kern/ufs_*.c`, `vfs_vnops.c`, `sys/dev/swap.c`, the flash, SD,
PSRAM and FAT32 drivers, the Dhara core, `sbin/fsck`, `sbin/mount`,
`sbin/mkfs` and `sbin/fdisk`.

| Group | Files | Non-blank code lines | K&R definitions | `register` | `caddr_t` | `(struct buf *)` casts | BSD integer names / `<stdint.h>` names | Functions: external / internal / external but used only in their own file | Denominator state |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| Kernel filesystem (`ufs_*`, `vfs_vnops`, `swap`) | 14 | 5,840 | 0 | 243 | 72 | 2 | 210 / 0 | 126 / 13 / 8 | 7 files `A, migration-open`, 7 `unreviewed` |
| Port drivers (flash, flash_swap, sd, sdspi, psram, psram_xfer, fat32tail) | 7 | 2,204 | 0 | 0 | 2 | 0 | 133 / 4 | 43 / 28 / 2 (fat32tail not linked) | `unreviewed` |
| Dhara core | 3 | 1,004 | 0 | 0 | 0 | 0 | 0 / 41 | 24 / 20 / 6 (library entry points the port does not call) | `unreviewed` |
| fsck | 12 | 2,621 | 66 | 69 | 0 | 0 | 91 / 0 | 64 / 0 / 22, `main` included | `unreviewed` |
| mount, mkfs, fdisk | 5 | 1,169 | 33 | 23 | 0 | 0 | 17 / 0 | 27 / 6 / 25, three `main`s included | `unreviewed` |

The function columns come from `arm-none-eabi-nm` (GCC 14.2.1) over the
PICOCALC_PSRAM kernel objects and the world's userland objects at
4b3eaf6c: external is a defined global text symbol, internal a local one,
and the last figure counts external functions no other object of the same
link references, the candidates for `static`. BSD integer names are
`u_int`, `u_char`, `u_short`, `u_long`, `daddr_t`, `off_t` and `ino_t`.
Implicit `int` is absent from every compiled file: the kernel and the
utilities build with `-std=gnu17` (`share/mk/sys.mk:107`,
`conf/Makefile.rp2040:43`), which GCC 14 rejects implicit `int` under by
default, and no makefile relaxes it; `sbin/fsck/cross.c` is not compiled
and was not checked. Missing `const` was not measured: it needs a tool
pass (for example clang-tidy's `readability-non-const-parameter`) with the
tree's include paths.

Mechanical items, expected to leave object code unchanged (verify by
comparing objects): the 99 K&R definitions in the utilities, `register`
(335 uses), file-local functions not declared `static`, and the duplicate of
fsck's `checkfilesys()` in `sbin/fsck/cross.c:31-146`, a 115-line clone of
`main.c:155-270` that the program's Makefile does not link (jscpd).

Semantic items, each needing a test before the edit:
- K&R parameters of type `char` or `short` change from promoted to
  declared types when a prototype appears; fsck's `reply()`, `pfatal()`
  and `pwarn()` are variadic and must take `<stdarg.h>` forms.
- Pointer-integer casts: `(struct mount *)((int)fs - offsetof(...))` in
  `ufs_mount.c:75` and `:126` is a container-of through `int`; the C17 form
  goes through `char *` and keeps ILP32 behavior.
- `caddr_t` buffer arithmetic in `ufs_bio.c` and `ufs_namei.c` (30 uses)
  becomes `char *` or `void *` with the arithmetic kept on `char *`.
- `struct fs` is an on-disk layout of `u_int` and `int` fields; fixed-width
  types (`uint32_t`, `int32_t`) and a `_Static_assert` that it fits
  `SBSIZE` make the layout explicit; the `balloc_test` gate asserts the size
  today only in its fixture.
- Cppcheck: `ufs_alloc.c:298` prints an `ino_t` (`unsigned int`) with `%d`
  and repeats the "bad block" text `badblock()` already printed; its
  `fat32tail.c:109` uninitialized-variable report reads a field behind a
  `valid` test and is a false positive (SRC).
- lizard reports 33 functions above 15; the highest are `rename()` (66,
  `ufs_syscalls.c:1295`), `pass2check()` (48, `sbin/fsck/pass2.c:81`),
  `bmap()` (45, `ufs_bmap.c:26`) and fsck's `main()` (41). These are
  review targets, not mechanical edits.

The port drivers and Dhara already follow `docs/research/STYLE-GUIDE.md`
closely (prototypes, `static`, `_Static_assert`, fixed-width types where the
hardware fixes the width); their remaining work is the GizmoBSD header
conversion of the DiscoBSD ISC files.

## 6. Defects found

| Id | Defect | Evidence | Fix |
| --- | --- | --- | --- |
| F-1 | `setup.c:120-121` tests `if (! sfile.wfdes)` after `open()`; `-1` passes, so a failed open of the scratch device ends in "CANNOT SEEK: BLK 0" and exit 8 rather than "Unable to open temp device" | H: a 64 MiB image on a host without `/dev/temp0` prints "CANNOT SEEK: BLK 0", exit 8 | test `< 0` on both scratch opens |
| F-2 | `main.c:283-288` accepts `-t file` into `scrfile`, and `setup.c:119` overwrites it with `/dev/temp0` | SRC | honor a given `-t` |
| F-3 | `ckfini()` (`utilities.c:182-190`) tests descriptors for nonzero, so every check that never opened the scratch device issues `ioctl(0, TFALLOC)` on standard input, and a read-only check (`wfdes` -1) also calls `close(-1)` | SRC | test `>= 0`; release the extent only when one was allocated |
| F-4 | `ufs_alloc.c:298` format mismatch and duplicate message | Cppcheck, SRC | `%u`, one message |
| F-5 | `blockcheck()` returns at `main.c:115` before its body | SRC | remove the unreachable body |

## 7. The overhaul plan

Text and RAM figures are inference from comparable code unless marked;
kernel text and kernel RAM are both the tightest budgets, so each kernel
slice reports both, with the compiler named.

### Before the rename (defect fixes, D109)

| Slice | Content | Files | Cost | Gate and its known-bad input | Evidence without a board | Board measurement |
| --- | --- | --- | --- | --- | --- | --- |
| P1 | F-1, F-2, F-3 | `sbin/fsck/setup.c`, `main.c`, `utilities.c` | fsck +0 to +40 B | `check-fsck-inode-range` gains a scenario: an image above the in-core pool with no scratch device must print the temp-device message and exit 8; the present source prints "CANNOT SEEK" | host | none |
| P2 | F-4 | `sys/kern/ufs_alloc.c` | kernel -20 to 0 B | `check-kernel-printf-formats` if its corpus covers the file, else a source mutant | build | none |
| P3 | F-5 and the unlinked `cross.c` duplicate | `sbin/fsck/main.c`, `cross.c` | fsck -100 to 0 B | `check-fsck-inode-range` unchanged; `check-c17-source-denominator` after the removal | host | none |

### After the rename (implementation)

| Slice | Content | Files | Text, RAM | Gate and its known-bad input | Evidence without a board | Board measurement | Depends on |
| --- | --- | --- | --- | --- | --- | --- | --- |
| T1 | `renode_fsck_timing.py` as a Makefile target with a `TESTING.md` row, outside the aggregate like `check-renode` | `tools/renode`, `Makefile`, `TESTING.md` | 0, 0 | its own controls: the synced trial checks clean, the unlinked trial is repaired | Renode | none | none |
| T2 | `fsck_io_model.py` as an on-demand host target | `tests`, `Makefile`, `TESTING.md` | 0, 0 | its calibration (strace equality, free-count repair, scratch open) | host | none | P1 |
| A1 | clean flag, kernel half: `MNT_CLEAN` 0x4000 and `MNT_WASCLEAN` 0x8000, mount-time synchronous dirty write keeping `fs_time`, mark clean at unmount and at orderly shutdown only, with no busy buffer, no latched error and no panic | `ufs_mount.c`, `ufs_subr.c`, `ufs_syscalls2.c`, `shutdown_sync.c`, `mount.h` | +100 to +160 B, 0 B (the donor adds about 50 kernel lines) | `check-kernel` (`kernel.ufs-disk.ilp32`) over the state table of section 2; mutants: the bit at 0x2000, a clean mark after a panic, an asynchronous mount write, `fs_time` stamped from the unset clock | host | clean boot and cut boot on a Pico | owner decision 1 |
| A2 | clean flag, fsck half: `-f`, skip on `CLEAN`, the mounted-root `WASCLEAN` rule, the mark after a check with no "no" answer, and (with decision 1's first option) the mounted-root hand-off instead of a write | `sbin/fsck`, `fsck.8`, and for the hand-off `ufs_mount.c` | fsck +120 to +200 B; hand-off +30 to +60 B kernel (I) | `check-fsck-inode-range` scenarios clean-skip, dirty-check, forced, answered-no stays dirty; `check-kernel` for the hand-off; mutants: mark despite a "no", a hand-off that writes the superblock | host | as A1, plus an SD-root boot under the loader | A1 |
| A3 | new images born clean | `tools/fsutil`, `sbin/mkfs`, `tools/flashimg` | 0 kernel | `check-fs-profiles`, `check-flash-sparse`; mutant: an image with the bit clear must boot through the check in Renode | host, Renode | none | A1 |
| A4 | fsck maps sized to the process window instead of 16 buffers | `sbin/fsck/main.c`, `setup.c` | fsck +50 to +100 B; user window only | `check-fsck-inode-range` at 256 MiB in core with no scratch device; known-bad: the 16-buffer pool needs the scratch | host | `time fsck` on a 256 MiB card | owner decision 3 |
| A5 | scratch rewrites without one hot spare: rotate the spare through free swap sectors, or keep temporary units append-only with a remap | `flash.c`, `flash_swap.c`, `sys/dev/swap.c` | +80 to +200 B, +0 to +64 B | `check-flash-swap` with an erase-count oracle per sector; known-bad: the present single spare | host | `STORAGE_STATS` erase counts around one large fsck | owner decision 5 |
| B1 | SD retry: up to three attempts per sector on CRC error, timeout or error token, then card re-initialization once | `dev/sd.c`, `dev/sdspi.c` | +80 to +150 B, +4 B | `sdspi_test` transcripts with one corrupted CRC16 and one missing token that must succeed on retry; known-bad: the present driver fails them | host | injected-error count on a card | owner decision 4 |
| B2 | FIFO-pipelined data phase (keep the 8-entry PL022 FIFO full) | `dev/sd.c`, `dev/sdspi.c` | +60 to +120 B, 0 B | `sdspi_test` byte model unchanged; a timing model of the loop | host | read rate on one card | none |
| B3 | multi-block CMD18 and CMD25 (memory-devices M9) | as there | as there | as there | as there | as there | B1 |
| B4 | 25 MHz after identification (memory-devices M10) | as there | as there | as there | as there | as there | B2 |
| B5 | CID read and report; refuse a different card at reopen of a held partition | `dev/sdspi.c`, `dev/sd.c` | +100 to +150 B, +16 B | `sdspi_test` CID decoding against hand-decoded registers | host | none | none |
| B6 | observed-maximum wait counters under `STORAGE_STATS` | `dev/sd.c`, `storage_stats.h` | 0 default, +60 B diagnostic | `check-rp2040-storage-delta` version bump | host | a capture | none |
| B7 | erase (CMD32, CMD33, CMD38) behind a filesystem trim | `dev/sdspi.c`, `dev/sd.c` | +80 B | `sdspi_test` | host | none | a trim design (`unimplemented-storage-work.md`) |
| C1 | flash program read-back and erase blank check, reported to Dhara as a bad block | `dev/flash.c` | +60 to +100 B, 0 B; about 10 us of XIP read per 1 KiB program | `check-flash-checkpoint` with a stuck-bit NOR model; known-bad: the present driver accepts it | host | none | none |
| C2 | Dhara torn checkpoint (pull request #636) and the resume policy | per `dhara-audit.md` | per that note | per that note | per that note | per that note | D26, D28 |
| E1 | fsck to prototypes, `static` and `<stdarg.h>`; `register` removed | `sbin/fsck` | 0 B expected | object comparison plus `check-fsck-inode-range` | host | none | P1, P3 |
| E2 | mount, mkfs, fdisk the same way | `sbin/mount`, `sbin/mkfs`, `sbin/fdisk` | 0 B expected | object comparison plus `check-umount-contracts`, `check-fs-profiles` | host | none | none |
| E3 | kernel `ufs_*`: `register` removed, `caddr_t` arithmetic on `char *`, container-of through `char *`, `struct fs` in fixed-width types with a size assertion | `sys/kern/ufs_*.c`, `sys/sys/fs.h` | 0 B expected; measured per kernel | `check-kernel`, `check-c17-kernel-inventory`, kernel headroom; object comparison | host | none | none |

## 8. Residual owner decisions

1. **Clean flag scope.**
   - Every UFS filesystem with a hand-off for a mounted root (recommended):
     fsck writes the mark on a filesystem it checked unmounted, and on the
     mounted root it passes a clean result to the kernel through a mount
     update and writes nothing, so no exit-4 reboot follows; the flash root
     saves 0.08-0.10 virtual s per clean boot, the SD root of #558 about
     0.4-0.6 s of SPI time, and card filesystems their full check; costs a
     small kernel hook beyond the donor.
   - The donor's semantics, root included: the least new code, but each
     unclean stop of a root that needed no repair costs a reboot of 4.3-4.4
     virtual s, so the flash root loses time and the SD root breaks even at
     one such stop per 7-11 clean boots.
   - Every filesystem but the mounted root, which fsck always checks in
     full: no reboot and no hand-off, but the SD root keeps paying its
     check on every boot.
   - Not imported: no on-disk change; SD boots keep their full check, which
     A4 and B2-B4 shorten.
2. **Bit values.** 0x4000 and 0x8000 in `fs_flags` (recommended: no layout
   change, outside the statfs mask), or a new superblock word in the unused
   tail of the block (room for later state, but every image tool changes).
3. **fsck map memory.** Size the maps to the process window (recommended:
   in core to about 450 MiB, no scratch wear), or keep the 16-buffer pool
   and its scratch device (no change, the 1 GiB ceiling stays).
4. **SD retry count.** Three attempts then one re-initialization
   (recommended, FreeBSD's count), one attempt and no re-initialization
   (smaller, masks less), or none (today).
5. **Scratch wear.** Rotate the spare sector (small change, spreads the
   erases), append-only temporary units with a remap (no rewrites at all,
   more code), or leave it (one sector takes every scratch rewrite).
6. **Board time.** A Pico session for the root's clean and cut boots and a
   PicoCalc session for SD fsck at three card sizes; without them the SD
   figures stay a model.

## Unverified and not run

- No board run: every SD time is a model from the driver's byte count and
  an assumed card access time; the Renode spans omit flash latency.
- Renode's SD card model was not used: it answers without the card's access
  time, so its fsck time would measure only the CPU.
- The CPU share of the SD figures scales the Renode root span by host x86
  instruction counts, which is a crude proxy for Cortex-M0+ cycles.
- Which program allocates the inode and block between the root mount and
  rc's fsck on the first Renode boot was not traced.
- The card's behavior across a power loss (whether a CMD24 the card
  acknowledged survives) is outside every model here.
- Under the PicoCalc UF2 loader a reset re-enters the loader, which owns the
  first 16 KiB of flash; whether it relaunches the kernel or stops at its
  menu was not read, so the cost of an exit-4 reboot on the SD root there
  may be larger than a bare reboot.
- Missing `const` qualifiers were not counted.

## Files and commands

Beside this note in `docs/research/filesystem-storage-overhaul/`:
`renode_fsck_timing.py` (the Renode driver), `fsck_io_model.py` and
`fsck_io_shim.c` (the host model and its counting shim), `fsck-io.tsv`
(the host rows), `renode-fsck-timing.tsv` (the Renode rows).

    # host model, on the build host (about 160 s of CPU)
    bmake -C tests/fsck_inode_range fsck_host
    bmake -C tools/fsutil fsutil
    gcc -m32 -std=gnu17 -Wall -Wextra -Werror -shared -fPIC \
        -o fsck_io_shim.so docs/research/filesystem-storage-overhaul/fsck_io_shim.c -ldl
    $PYTHON docs/research/filesystem-storage-overhaul/fsck_io_model.py \
        tools/fsutil/fsutil tests/fsck_inode_range/fsck_host fsck_io_shim.so \
        usr.bin WORKDIR fsck-io.tsv

    # Renode, after bmake MACHINE=rp2040 build flash and the model fetch
    $PYTHON -u docs/research/filesystem-storage-overhaul/renode_fsck_timing.py \
        renode tools/renode sys/arch/rp2040/compile/PICO_UART/unix.bin \
        COPY-OF-distrib/rp2040/flash.bin OUTDIR

Image hashes at 4b3eaf6c: `sdcard.img`
54f422a76b061e20d37ea067c364c8ec0395c3af9268be05756f1ab974f923d6,
`sdcard-PICOCALC.img`
bf60f2af897316786ece5f2008c413de57e422a76ca8c3d8fc4f2cf12ac89061.
Reference trees read: 211bsd at 0a38e59, 4.4BSD-Lite2, LiteBSD, retrobsd,
netbsd-src, openbsd-src and freebsd-src as checked out in the local BSD
workspace.
