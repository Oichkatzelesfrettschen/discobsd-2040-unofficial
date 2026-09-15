# UFS fixed-table SRAM reduction on RP2040

The RP2040 port now trades bounded scans and additional swap traffic for
10,328 bytes of linked kernel BSS and eight filesystem blocks. The change
targets ownership and lifetime, not K&R function syntax. GNU C17 emits the
same Thumb code for equivalent old-style and prototyped definitions, while
the old header-owned tentative definitions and permanently resident tables
consume measurable SRAM.

The measurements in this note use the PICO kernel linked from port merge
`af086a09` (port PR #65), based on `8e7f581f`.

## Linked result

| Quantity | Baseline | Reduced | Change |
| --- | ---: | ---: | ---: |
| Kernel text | 98,790 | 99,402 | +612 bytes |
| Kernel data | 248 | 248 | 0 |
| Kernel BSS | 54,304 | 43,976 | -10,328 bytes |
| UFS/VFS cluster text | 22,516 | 21,585 | -931 bytes |
| Buffer payloads | 10,240 | 4,096 | -6,144 bytes |
| Cached inodes | 2,592 | 2,016 | -576 bytes |
| Mount table | 2,440 | 1,036 | -1,404 bytes |
| Buffer headers and free queues | 620 | 252 | -368 bytes |
| Pathname cache | 240 | 144 | -96 bytes |

The linked allocation gate reports 4,240 bytes for the four buffer payloads
and headers, 2,016 bytes for 24 inodes, 1,036 bytes for one mount record, and
8,720 bytes recovered by the mechanisms below. The larger 10,328-byte BSS
change includes interactions outside those named allocations.

The rebuilt root has 988 blocks, a nine-block inode list, 91 allocated
inodes, 37 free inodes, and 317 free data blocks. The earlier 256-inode image
used 17 metadata and inode-list blocks. A 128-inode image therefore recovers
eight 1,024-byte blocks while retaining the measured 37-slot creation margin.

## Implemented mechanisms

### Swap-backed exec argument spool

`sys/kern/exec_subr.c` serializes argument and environment strings into a
length-delimited stream. SwapRAM supplies the first backing tier and raw swap
supplies the bounded fallback. The loader replays the stream into the final
user stack after executable validation and releases the extent on every
success and error path.

The spool removes the requirement to retain six 1,024-byte cache buffers
during exec. `sys/arch/rp2040/include/machparam.h` consequently reduces
`NBUF` from ten to four. The remaining buffers cover one spool staging block,
filesystem or packed-executable I/O, and a sleep margin.

The packed-text decoder and exec spool reuse one codec workspace. SwapRAM
evacuation moves a live spool to raw swap before reusing the pool, so pool
pressure cannot invalidate an in-progress exec.

### One UFS root

Both shipped kernel configurations define `NMOUNT=1` and
`SINGLE_UFS_ROOT`. The onboard layout has one UFS root on `fl0a`; `fl1` is raw
swap. The kernel rejects an additional mount with the existing table-full
result while preserving root remount, `df`, `statfs`, mount listing, and
`umount -a`.

The single-root profile removes the second mount record, synthesizes the
fixed `root` and `/` names, omits the unused quota pointer, and derives each
cached inode's device and filesystem identity from the root record. `iget()`
validates that identity before populating a cache entry.

### Linear bounded caches

The inode cache scans 24 entries instead of maintaining 16 hash heads and two
hash pointers in each inode. The buffer cache scans four headers instead of
maintaining hash heads and hash links. Their independent free-list and buffer
queue links remain because allocation and delayed I/O still need them.

The four-entry pathname cache uses a second-chance clock. Each entry carries
one reference bit rather than hash links and a doubly linked LRU. Generation
snapshots still reject an entry recycled while `igrab()` sleeps. The host
gate directly executes empty-slot reuse, the clock sweep and replacement
order, `nchinval()`, and generation-change races.

### Compact inode fields

`i_flag`, `i_count`, `i_id`, and `i_flags` are adjacent 16-bit fields. Static
assertions bind the transient flag mask, persistent flag mask, and fixed-table
reference bound to that width. Kernel paths reject unsupported persistent
flag bits with `EIO` for disk data and `EINVAL` for syscall input instead of
silently truncating them.

The nonzero 16-bit generation counter invalidates the pathname cache when it
wraps. `iflush()` clears the released inode identity so a linear scan cannot
rediscover a stale entry. RP2040 `pstat` and `fstat` compile with the same
compact layout used by the kernel.

### GNU C17 ownership boundary

PICO and PICO_UART compile with `-std=gnu17 -Wall -Werror -fno-common`.
All 13 UFS-centered translation units use prototypes, and
`tools/verify_ufs_prototypes.sh` asserts `__STDC_VERSION__ == 201710L` before
checking strict prototypes and old-style definitions. Headers declare shared
tables with `extern`; one source file owns each definition.

The board libc also compiles as GNU C17 with `-fno-common`, `-Wall`,
`-Wextra`, and `-Werror`. Seven unrelated legacy userland subtrees retain
their C90 `-ansi` flags; their language migration is separate from the UFS
memory change.

The rebuilt PICO and PICO_UART object sets and board libc archive contain zero
COMMON symbols. The generated kernel Makefiles carry the same dialect and
tentative-definition policy as `sys/arch/rp2040/conf/Makefile.rp2040`.

## Reproducible gates

Run from the port repository root:

```console
bmake MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check-cache-footprint
bmake MACHINE=rp2040 check-ufs-prototypes
bmake MACHINE=rp2040 check-flash-swap
bmake MACHINE=rp2040 check-swapram
bmake MACHINE=rp2040 check-hsaout
tools/bin/fsutil --check --partition=1 distrib/rp2040/sdcard.img
arm-none-eabi-size sys/arch/rp2040/compile/PICO/unix.elf
```

The host gates cover spool serialization and replay, SwapRAM coexistence and
raw-swap fallback, spool evacuation, script argument order, `NCARGS`, records
crossing block boundaries, cache layouts, direct clock behavior, generation
wrap, compact flag rejection, and linked swap ownership. The packed a.out
gate completed 5,363 checks with zero failures. `fsutil` completed all five
filesystem phases and reported 91 files, 662 allocated blocks, and 317 free
blocks.

Host and linked-artifact evidence cannot prove the miss rate or delayed-write
behavior of the smaller caches. The pressure workload in
`tests/rp2040/cache_footprint/README.md` remains the board acceptance gate.
Flashing and device operation require a separately authorized session.
