# A format descriptor under the inode layer: mounting V7 volumes with shared code

Status: implemented behind the `V7FS` kernel option and gated on the host
(`check-v7-format`). No kernel configuration sets the option, no board has
mounted a V7 volume, and `mount(8)` has no way to pass `MNT_V7` yet; the
"Open work" section lists what a shipped mount still needs.

## Finding

The Seventh Edition (V7) file system and the DiscoBSD kernel's native file
system run the same algorithms over different constants and record layouts.
`bmap`, `balloc`, `free`, `ialloc` and the truncate walk in `sys/kern/ufs_*.c`
are line-for-line descendants of V7's `bmap` (`sys/subr.c`), `alloc`, `free`,
`ialloc` (`sys/alloc.c`) and `itrunc`/`tloop` (`sys/iget.c`). A V7 mount
therefore needs four numbers and four record codecs under the existing inode
layer, with one copy of each algorithm serving both formats. It needs neither
a second set of functions nor an operations switch above the inode.

## How many parameters: the System V Release 3 count, checked

System V Release 3 serves 512-byte and 1024-byte volumes from one kernel
through `sys/fs/s5macros.h`. The file defines twelve macros, each taking the
mount's block size: `FsBSIZE`, `FsBSHIFT`, `FsNINDIR`, `FsBMASK`, `FsINOPB`,
`FsLTOP`, `FsPTOL`, `FsNMASK`, `FsNSHIFT`, `FsITOD`, `FsITOO`, `FsINOS`.

Twelve is a count of spellings. Every one of the twelve is a function of a
single two-valued predicate, `FsLRG(bsize)`, so the Release 3 design has one
degree of freedom. The macro count is no guide to the size of a geometry
record.

The DiscoBSD inode layer names about 25 distinct layout constants and types
(`DEV_BSIZE`, `blkoff`, `lblkno`, `INOPB`, `itod`, `itoo`, `NINDIR`,
`NSHIFT`, `NMASK`, `NADDR`, `NDADDR`, `NICFREE`, `SUPERB`, `struct dinode`,
`struct fblk`, `i_addr` and others) at roughly 150 sites. That is the edit
surface. Deriving the parameters from the two formats gives four numbers
and one tag:

| Parameter | V7 | Native | What derives from it |
|---|---|---|---|
| `g_bshift`, log2 of the block size | 9 | 10 | block size and mask; inodes per block (`g_bshift - 6`, both disk inodes are 64 bytes); indirect fan-out (`g_bshift - 2`, both store 4-byte entries); the shift from file-system block to buffer block (`10 - g_bshift`) |
| `g_ndaddr`, direct addresses in the inode | 10 | 4 | total addresses (`g_ndaddr + 3`; both have single, double and triple indirect) |
| `g_sblock`, superblock position in blocks | 1 | 0 | first inode block (`g_sblock + 1`), so the inode-number-to-block mapping |
| `g_nicfree`, free-chain chunk | 50 | 200 | when the free chain spills and refills |
| `g_format`, the record tag | V7 | native | which codec reads the superblock, disk inode, free-chain block and directory block |

Three candidates fail to be parameters:

- The free-inode cache size (100 in V7, 32 native) is a hint. `ialloc`
  rebuilds it by scanning the inode list, so a V7 mount keeps the 32 entries
  nearest the top of the on-disk stack and writes back a valid, shorter list.
- `s_isize`/`fs_isize` means "first block after the inode list" in both
  kernels' `badblock` and `ialloc`.
- Byte order is fixed at little-endian for the kernel; PDP-11-order images
  stay with the userland tool, whose NetBSD core handles all three orders.

With two formats the record is one bit of information and the four numbers
are a constant table row (`fs_geom_table` in `sys/kern/ufs_v7.c`). The
four-number form is kept because it also describes a 1024-byte volume in V7
layout (10, 10, 1, 50), the System V "1K" layout. Whether that third row
works unchanged is unverified: the System V superblock carries a type and
magic field V7 lacks.

## Precedent inside BSD: ufsmount.h

4.4BSD-Lite2 answers the same question for FFS and LFS, which share
`ufs_bmap.c`. `sys/ufs/ufs/ufsmount.h` puts three numbers in the mount
record: `um_nindir`, `um_bptrtodb` and `um_seqinc`. NetBSD's descendant adds
`um_lognindir`, `um_dirblksiz`, `um_maxsymlinklen` and `um_fstype`, plus a
`struct ufs_ops` of seven entries. BSD's own cost for two formats under one
inode layer is about five numbers and seven operations. The vnode interface
above it (29 operations in SunOS 4, 27 functions in NetBSD's v7fs glue)
exists for file systems whose algorithms differ.

The native headers still showed the 4.2BSD seam before this work:
`sys/sys/fs.h` defines `fsbtodb` and `dbtofsb` as identities, `bmap` called
`dbtofsb`, and the comments on `blkoff` and `lblkno` read "calculates
(loc % fs->fs_bsize)".

## The accessors and the byte-identity claim

`sys/sys/fsgeom.h` defines `struct fs_geom` and one accessor per derived
value (`FS_BSIZE`, `FS_NINDIR`, `FS_NDADDR`, `FS_ITOD`, `FSBTODB`,
`FSBADDR`, `IADDR` and the rest). The inode layer calls the accessors at
every site that named a layout constant. Built with `V7FS`, an accessor
reads the mount's row; built without it, each accessor expands to the token
sequence the site held before.

The claim is exact and was checked: for the four shipped kernels (PICO,
PICO_UART, PICOCALC, PICOCALC_PSRAM) every object file of a build with the
accessors equals the same object from the commit before them once debug
sections are stripped (83, 79, 96 and 98 objects). The comparison was
calibrated by its first run, which reported `ufs_inode.o`: `itrunc()` had
been written through the whole address array where the original used the
direct and indirect views, which moved registers and grew the function 16
bytes. `IADDR_DB` and `IADDR_IB` restored the original expression.

## Two V7 blocks in one buffer

The buffer cache has four 1024-byte buffers identified by device and
1024-byte block. V7 block `b` is half `b & 1` of buffer block `b >> 1`
(`FSBTODB`, `FSBADDR`). Release 3 avoided the question with sized buffers
(`bread(dev, bn, bsize)`); a four-buffer pool cannot. These rules follow
from two blocks sharing one buffer identity:

1. A write to a V7 block reads the buffer first. `FSGETBLK` and `IN_GETBLK`
   are `getblk()` only where a block fills its buffer.
2. `balloc()` zeroes the part it allocated, never the buffer, and fails the
   allocation if the read of the buffer failed, since the write-back would
   put that failure in the other part.
3. `free()` writes the chain header into the freed block's part.
4. `rwip()`'s "wrote a whole block" tests compare against the file's block
   size.
5. The V7 superblock is the second half of buffer block 0; `v7_sbwrite()`
   reads the buffer, so the boot block survives every superblock write.
6. `bmap()` releases an indirect block before it calls `balloc()` and reads
   it again afterwards. The block `balloc()` returns, or a free-chain block
   it reads on the way, can be the other half of the buffer `bmap()` holds,
   and a buffer requested by its holder never arrives. The host gate's
   cache fails the run on that request.

Raw access to the same device stays coherent because the buffer identity is
unchanged. Two delayed writes to sibling blocks reach the card as one
transfer.

## Inode codec and the address escape

`iget()` and `iupdat()` end in a copy between the disk inode and the in-core
fields; the V7 branch is `v7_iread()` and `v7_iwrite()`. Owner and group
widen on read; a value the 16-bit fields cannot hold fails the update with
`EOVERFLOW` and nothing written. A device node keeps its number in the
first address, as V7 does.

The disk inode holds thirteen 3-byte addresses (39 bytes) and the in-core
address slot is 28 bytes. Thirteen 24-bit addresses are 312 bits and the
slot is 224, so no encoding keeps them all in core. Each address is kept in
16 bits (26 bytes), and the value `0xffff` is an escape: the address is on
the disk. `v7_addr_get()` reads an escaped address from the disk inode and
`v7_addr_set()` stores one there at once; `v7_iwrite()` leaves an escaped
address's three bytes as they are. A volume of fewer than 65,535 blocks
(32 MB) never takes the escape and pays nothing; a larger one, up to the
format's 8 GB, pays one buffer-cache lookup for each block above that line.
The in-core inode does not grow.

System V used the same device for owners: SVR4 and illumos UFS keep a 16-bit
`ic_suid` whose value `UID_LONG` (65535) sends the reader to the 32-bit
`ic_uid` (`usr/src/uts/common/sys/fs/ufs_inode.h`). SVR4's s5 folds an
expanded device number into three otherwise unused address slots. Sixth
Edition's large-file flag reinterprets the whole address array per inode.

## Directories as native directories of twice the size

The kernel's directory code (`namei_at()`, `direnter()`, `dirremove()`,
`dirrewrite()`, `dirempty()`, `checkpath()`, rename, mkdir, rmdir) and
libc's `readdir()`, which calls `read(2)` on the directory, all parse
`struct direct` records in `DIRBLKSIZ` (1024-byte) blocks. A V7 directory
block is 32 entries of 16 bytes. `v7_dirread()` presents it as one
`DIRBLKSIZ` block in which entry `j` is a 32-byte record at byte `32 * j`,
and `v7_dirwrite()` contracts the block back. `blkatoff()`, `rwip()` and
the two `bwrite()` calls in `direnter()` and `dirremove()` are the only
places that know. The directory reads as a native one of twice its size:
`v7_iread()` doubles the size and `v7_iwrite()` halves it, a logical block
of the expanded directory is the same V7 logical block, and an offset maps
by a shift, so `lseek`, `telldir` and `seekdir` are exact with no state.

Two details make the native code work unchanged:

- Block 0 of a directory that starts with "." gives "." 12 bytes and ".."
  the 52 after them. That is the layout mkdir's template writes and the one
  rename and `checkpath()` read ".." from at byte 12.
- The expansion is rebuilt from the volume on every read and holds one
  record per entry, so a live record never carries free space. `DIRFOOT()`
  tells the slot search so, and the 4.3BSD compaction path finds nothing to
  compact. An entry is created in the first free record and removed by
  zeroing its inode number, as V7 did, so entries never move and a program
  that removes names while reading the directory misses none.

`v7_contract()` places each live record in the first entry at or after its
byte offset, which returns every record the expansion produced to the entry
it came from, and refuses a block that does not contract before the
volume's copy is touched. A name longer than 14 bytes fails creation with
`ENAMETOOLONG` where V7 truncated silently. Minix 3's `fs_getdents`
(`fs/mfs/read.c`) translates fixed entries the same direction for user
programs only; presenting the expanded form to the kernel's own directory
code is what removes a second set of directory functions.

A symbolic link (mode 0120000, a 2BSD extension NetBSD's core also reads)
is a file of the volume's block size; `namei_at()` reads its first block
through `FSBADDR`.

## Limits a V7 mount keeps

Fourteen-byte names, 16-bit owners and device numbers, a 16-bit link count
(the volume's field is signed, so a count above 32,767 reads as negative on
a Seventh Edition system), no inode flags, no magic number. Because V7 has
no magic number the mount type is stated by the caller (`MNT_V7`) and never
detected, and `v7_sbload()` range-checks the superblock before the mount
succeeds.

## A V7 volume is foreign media

The native root is written by this tree's own tools; a V7 volume arrives on
an SD card. The format's fields are therefore checked where they are read,
and each check is a macro that is constant false for the native format:

- `FS_BADADDR()` refuses a direct address, an indirect root or an indirect
  entry outside the data area in `bmap()`, with `EIO`, before a buffer is
  read or written. Without it a file whose first address is 1 writes over
  the superblock.
- `FS_BADINO()` refuses in `iget()` an inode number outside the inode
  table, and `v7_sbload()` drops such a number from the free-inode hints
  and refuses an inode table larger than the 16 bits a directory entry
  names.
- `v7_iread()` refuses a directory size that is no whole number of entries
  or that would not double into a positive `off_t`.
- `FS_WIDE16()` refuses, at `ialloc()`, `chown1()` and `mknod()`, an owner,
  group or device number above 65,535, so the value never enters an inode
  whose directory entry would outlive a failed `v7_iwrite()`.
- A wide address whose disk inode does not read answers -1 from
  `v7_addr_get()`, which `FS_BADADDR()` refuses; it had read as a hole,
  which `bmap()` fills by allocating over the live pointer.
- `FSRESID()` and `IN_AVAIL()` judge a short transfer by the part the
  caller selected. The last buffer of a device with an odd sector count
  holds a first part and no second, the SD driver reports the absent 512
  bytes in `b_resid`, and the first part is whole. A failed indirect read
  in a `V7FS` kernel fails `bmap()` with `EIO` where the native kernel
  answers block 0, because block 0 of a V7 volume is its boot block.
- `balloc()` counts a block whose buffer does not read out of the free
  total and leaves it out of the chain.
- A V7 directory write in `rwip()` joins the `IO_SYNC` completion
  (`syncip()` and `ufs_sync()`) the other writes take.

## Cost, measured

The scratch configuration was PICOCALC plus `options "V7FS"`, built with
arm-none-eabi-gcc at the PICOCALC flags on 2026-11-10:

| | text | data | bss |
|---|---:|---:|---:|
| PICOCALC | 115,027 | 384 | 53,336 |
| PICOCALC with `V7FS` | 120,553 | 384 | 53,352 |
| difference | 5,526 | 0 | 16 |

The checks on foreign media are 712 of those bytes; the build before them
measured 119,841. `ufs_v7.o` was 2,314 bytes in a build 784 bytes smaller;
the rest is the inode layer reading geometry from the mount where it held
constants (`sys_inode.o` grew 620 bytes, `ufs_inode.o` 784, `ufs_alloc.o`
376, `ufs_bmap.o` 336 in that build). The 16 bytes of bss are two fields in
each of two mount slots. The estimate before the build was 3 to 4 KB for
read and write together. NetBSD's v7fs core alone measures 7,947 bytes
before any adapter.

Only PICOCALC and PICOCALC_PSRAM have a second mount slot (`NMOUNT=2`) and
a second block device (the SD card); PICO and PICO_UART are built with
`NMOUNT=1` and `SINGLE_UFS_ROOT`, which `fsgeom.h` refuses to combine with
`V7FS`. Those two kernels have about 15.7 KB and 13.5 KB of flash headroom
by `arm-none-eabi-size` against the 131,072-byte region;
`check-kernel-headroom` is the gate of record and was not rerun.

## Evidence

`check-v7-format` (`tests/kernel/v7_format_check.sh`) holds three
implementations to one volume. The NetBSD core in `usr.sbin/v7fs` formats a
4,096-sector volume and stores four files. The kernel's own `ufs_alloc.c`,
`ufs_bmap.c`, `ufs_inode.c`, `ufs_namei.c`, `sys_inode.c` and `ufs_v7.c`,
built for the host at ILP32 with `V7FS`, read them through `namei()` and
`rdwri()`, then create, truncate and remove files and directories. The core
reads every surviving file back byte for byte, and
`tests/kernel/v7_check.py`, which shares code with neither, finds every
data block free or named exactly once and every link count equal to its
directory entries. The kernel code descends from V7 and the core was
written independently, so agreement is two implementations and a checker.
The run then turns hostile: a direct address that names the superblock, an
indirect root below the data area, a wide address whose disk inode fails to
read, a device that ends after the first part of its last buffer with the
file's indirect block in that part, an inode number past the table and an
owner of 65,536. Fourteen source mutants each fail the run and the checker
rejects six damaged images. `sys/arch/rp2040/doc/TESTING.md` carries the gate's full claim.

## Open work

- `mount -o v7` passes `MNT_V7` and `mount` lists such a volume as `v7`,
  but no configuration sets `V7FS`, so a shipped kernel answers the option
  with "Incorrect super block". Setting the option on the PicoCalc
  configurations follows a board run.
- No board has run this code. The host gate exercises the inode layer over
  a four-buffer cache model; the SD driver, real sleeps and a second mount
  beside the flash root are unexercised.
- A short transfer is exercised for one read of an indirect block in the
  last buffer; a write there, and a short transfer anywhere else, are not.
- The gate's `syncip()` and `ufs_sync()` are stubs, so the synchronous
  completion of a directory write is compiled and not exercised, and the
  `chown1()` and `mknod()` refusals live in `ufs_syscalls.c`, which the
  gate does not build. Rename, the rewrite of `..`, the block the
  allocator retires after a failed read and truncation back up through a
  hole have no case either.
- Volumes above 65,534 blocks are covered only by the escape's unit test,
  which stores and reloads wide addresses; no file was allocated there.
- V7's `icheck.c` and `dcheck.c`, ported from K&R C, would add the original
  checkers as a fourth implementation.

## Sources and their terms

V7 sources fall under the Caldera license of 2002, which carries an
advertising clause like the one NOTICE already records. Minix 3 and NetBSD
are BSD-licensed. System V Release 3 and 4, illumos, Ultrix and SunOS
sources were read for the structure of their interfaces; this work copies
no code from them or from V7.
