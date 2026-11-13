Source reading at `aec36799`, `arm-none-eabi-gcc` 16.2.0 object sizes, and host runs of `tests/libc_contracts/mktime_contract_test.c`; no qemu-user, Renode or board run.

# Time representation past 2038: verified claims, measurements and plan

`time_t` is `long` (`include/time.h`, `sys/sys/types.h`), four bytes on
ARMv6-M, so the last second it names is 2038-01-19T03:14:07Z. The board has
no time-of-day clock: `sys/kern/init_main.c` seeds `time.tv_sec` from the
root superblock's `fs_time`, and `date` sets it. A proposal to widen the
representation, with storage-wear changes attached to it, was checked claim
by claim against this tree. The checks leave one decision for the owner (the
width of `time_t` at the system call boundary), one defect that holds at any
width (repaired with this note), and a plan whose steps are ordered by what
that decision does and does not change.

## Evidence base

- Sizes are `arm-none-eabi-size` text of objects compiled with
  `-mcpu=cortex-m0plus -mthumb -Os -std=c17 -ffreestanding`.
- The kernel figure is the PICO `unix.elf` in a local build tree on
  2026-11-13; the tree does not record its source revision, so it bounds the
  size and dates nothing.
- Host runs use the tree's headers (`-nostdinc -isystem include`), natively
  and with `-m32`.
- The proposal cites clones of FreeRTOS, seL4, Fiasco.OC, L4Re, Genode,
  NuttX, Zephyr and Minix 3 that were deleted after reading, and BSD and
  System V archives outside this repository. Those readings were not
  repeated; a claim that rests on them alone is marked `not run`.

## Verdicts on the proposal's claims

| Claim | Check | Verdict |
| --- | --- | --- |
| `time_t` is a four-byte signed `long`; `struct timeval.tv_sec` is `long` | `include/time.h`, `sys/sys/types.h`, `sys/sys/time.h` | holds |
| `struct dinode` is 64 bytes with four unused bytes in `di_reserved[1]` | `sys/sys/inode.h`; `_Static_assert` in `sys/kern/ufs_v7.c` | holds |
| `struct fs` is 512 bytes and a 64-bit `fs_time` makes it 516 | `DEV_BSIZE` is 1024 (`sys/arch/rp2040/include/machparam.h`); the fields of `struct fs` sum to 1024 | refuted: the superblock is 1024 bytes and fills its block exactly; a 64-bit `fs_time` at offset 968 makes it 1028 |
| A 64-bit timestamp makes `struct dinode` 76 bytes and breaks `INOPB` | three fields grow by four bytes each; 1024 is not a multiple of 76 | holds |
| PICO kernel text is 98,390 bytes | `arm-none-eabi-size` of the local `unix.elf`: text 101,390, data 272, bss 43,136 | stale by 3,000 bytes |
| The kernel links no 64-bit division | `unix.map` defines `__aeabi_uidiv`, `__aeabi_idiv` and their `mod` forms; `__aeabi_ldiv0` is the divide-by-zero stub | holds |
| `ctime.o` is 578 bytes of text and 124 of bss; `mktime.o` is 848 | measured | holds |
| A closed-form calendar removes those objects' lookup tables and shrinks them | measured below | half refuted: the forward direction grows by 110 bytes; the inverse shrinks by 280 |
| No program in the tree uses the closed form | `usr.bin/touch/touch.c` carries `days_from_civil()` and `civil_from_days()` with bounds derived from `time_t` | refuted |
| The root should default to `MNT_NOATIME` | every RP2040 `Config` sets `ROOT_MOUNT_FLAGS=MNT_NOATIME`; `check-root-noatime` holds it | already shipped |
| Deferring `fl_sync()` across batched writes saves 6.67 times the page programs | `flash-storage-expectation-plan.md`, finding F6: at the shipped occupancy the host model gives 6.28 pages per write against 8.00 | refuted for the shipped image: about 21 percent; the 6.67-fold figure needs the live share at or below about 75 percent |
| Trimming `MAXMNTLEN` frees room for a wider `fs_time` | `fs_fsmnt` is 28 bytes of a full 1024-byte block | arithmetic holds with the corrected size; the trade is unneeded under the plan below |
| FreeBSD UFS1 stores unsigned 32-bit timestamps | `not run`: no FreeBSD source in this tree | open; the plan does not depend on it |
| Datasheets for the W25Q16JV and ESP-PSRAM64H are stored in the notes repository | both files exist there, untracked | holds; they are uncommitted and `DATASHEET-INDEX.md` does not cite them |

## Measurements

### The forward conversion: the year loop is the smaller form at four bytes

`ctime.c` walks one year at a time from 1970. With a four-byte `time_t` the
walk is at most 68 steps. The closed form (bias by whole 400-year eras,
divide by 146,097, recover the year of the era, then the month from
`(5 d + 2) / 153`) needs seven divisions, each a call on a core without a
divide instruction.

| Object | Year loop | Closed form |
| --- | --- | --- |
| `ctime.o` text | 578 | 376 |
| closed-form forward routine | -- | 312 |
| total for a program that links `ctime.o` alone | 578 | 688 |

The closed form costs 110 bytes in every program that prints a date and
buys nothing while the loop is bounded by 68. It becomes necessary when the
second count is 64 bits wide, where the loop's bound is the year itself.

### The inverse conversion: one calendar instead of two

`mktime.c` carried its own month-length tables and year loops beside the
forward conversion in `ctime.c`. With the day count in closed form and the
fields read back through the forward conversion `ctime.c` already has,
`mktime.o` goes from 848 to 568 bytes and `ctime.o` from 578 to 582. No
program in the shipped manifest links `mktime.o` (`touch` carries its own
calendar and `ar` avoids it), so the saving reaches `libc.a` and programs
built on the board.

### Splitting a 64-bit second count without 64-bit division

86,400 is 128 times 675. The low seven bits of the count are the low seven
bits of the second of the day; the rest is a division by 675, which
schoolbook division over 16-bit limbs performs in four 32-bit divisions,
because a remainder below 675 shifted left by 16 stays below 2^26. A
prototype is 136 bytes and agrees with the compiler's 64-bit division on
3.7 million in-range random counts and eleven boundary values (ILP32 host
build). The library route links `__aeabi_ldivmod` (72 bytes) and
`__divmoddi4` (342 bytes). The split therefore keeps the kernel's
division helpers as they are and costs a program a third of the generic
routine.

## The defect found: `timegm()` wraps at both ends of the range

`timegm()` admitted one day past each end of the range and then multiplied
it into seconds. Parent source, ILP32 host build:

| Fields (UTC) | Parent answer | Correct answer |
| --- | --- | --- |
| 2038-01-19 03:14:08 | -2147483648 | `(time_t)-1` |
| 2038-01-19 23:00:00 | -2147412496 | `(time_t)-1` |
| 1901-12-13 20:45:51 | 2147483647 | `(time_t)-1` |

It also wrote `tm_wday` before the range check, so a refused time cleared
the sentinel that the file's own header tells callers to rely on. The
repaired `timegm()` compares before each operation that could leave the
range and reads the fields back only for an accepted time. The contract
test gains the eight boundary instants, both extreme `tm_mday` values, both
extreme `tm_year` values, and every whole day of the range against a
calendar that counts day by day. The parent source fails 18 of the 157,838
checks at either width; the repaired source passes all of them.

## What survives

Three statements of the proposal survive, each in a narrower form.

1. The on-disk format needs no new bytes. Reading the three inode
   timestamps and `fs_time` as unsigned moves the last representable second
   from 2038-01-19 to 2106-02-07 with the 64-byte inode and the 1024-byte
   superblock unchanged, and every image written so far stays valid because
   no timestamp before 1970 exists on a board whose clock starts at its
   image's build time. `di_reserved[1]` remains free for two high bits per
   timestamp (the scheme ext4 uses) if 2106 is ever too near.
2. The kernel needs no 64-bit clock. `hardclock()` increments a 32-bit
   second count; read as unsigned it serves until 2106. A carry word in the
   style of FreeRTOS's overflow counter is one compare in the tick path and
   can be added later behind an unchanged system call interface.
3. The width visible to programs is a separate question from the width
   stored. A 64-bit `time_t` at the system call boundary can be served by a
   kernel that zero-extends a 32-bit count, so the costly part (the
   interface every program is compiled against) changes once and the cheap
   part (the kernel's representation) is free to change later.

The Q32.32 fixed-point clock and a uniformly 64-bit kernel and disk format
stay refuted for this target, for the reasons the proposal gives.

## The open decision

The width of `time_t` in the user interface has two candidates with real
costs on both sides, and the tree does not choose between them.

| | Unsigned 32-bit `time_t` | 64-bit `time_t`, 32-bit unsigned kernel and disk |
| --- | --- | --- |
| Last second | 2106-02-07 | 2106 in the kernel until a carry word lands; no interface limit |
| Bytes added | none | `struct stat` 12, `struct timeval` 8, plus code wherever a program stores or compares a time |
| Interface break | none in size; signedness changes | every time-carrying system call and every program, once |
| Times before 1970 | unrepresentable | representable in programs; clamped on disk |
| POSIX.1-2024 `time_t` (at least 64 bits, as recalled; `not run` against the standard's text) | fails | meets |
| Source audit | every signed comparison and subtraction of `time_t` | every `long` that holds a `time_t`, every `%ld` that prints one |

Evidence that would settle it: the growth of the full root image under the
64-bit interface, which only a branch that builds it can measure, against
the 270 free blocks of the `full` profile.

## Plan

Each step is one pull request. The first two hold at either width.

1. `timegm()` range repair and the shared calendar (this change).
2. Width assertions where a time meets a fixed layout: `struct dinode`,
   `struct fs` and the V7 inode already assert their sizes; add the
   signedness and width assertions that make a change of `time_t` a compile
   error at each site that assumes four signed bytes
   (`tests/kernel/balloc_test.c` and `usr.bin/touch/touch.c` have them;
   `sys/kern/kern_time.c`'s `LONG_MIN`/`LONG_MAX` clamp, `difftime.c` and
   the `tar`, `ar` and `ls` formatters do not).
3. The decision above.
4. Unsigned on disk and in the kernel clock: `icommon2`, `fs_time`,
   `time.tv_sec` and the `adjtime()` clamp. The gate is `balloc_test` and
   `ialloc_test` with a clock past 2038, and a Renode boot from an image
   whose `fs_time` is past 2038.
5. The user-interface width chosen in step 3, with the forward conversion
   moved to closed form and the seven-bit split if the width is 64 bits.
6. `date`, `touch`, `ls`, `tar`, `ar`, `ps` and the host tools
   (`tools/libufs`, `sbin/mkfs`, `sbin/fsck`) read and print through the
   chosen type.

The storage-wear items of the proposal stay with their owner,
`flash-storage-expectation-plan.md`: group commit is its phase P3 and waits
at stop point P3.0 on the sparse image (P8b) and the board baseline
(P0b.3).

## Residuals

- `mktime()` calls `localtime()` and so overwrites the object a caller's
  earlier `localtime()` returned; C permits that only of the functions that
  return such a pointer. The shared conversion now takes a caller-supplied
  struct, which is the piece a repair needs.
- The sizes are of single objects. Linked growth per program was not
  measured, and the 64-bit interface's cost across the root image is
  unmeasured.
- No target execution: qemu-user, Renode and the board did not run the
  repaired `timegm()`.
