Source reading at `aec36799`, `arm-none-eabi-gcc` 16.2.0 object sizes, host runs of `tests/libc_contracts/mktime_contract_test.c`, and three clean distribution builds that differ in the type of `time_t`, each through the packed root image; no qemu-user, Renode or board run.

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
| `struct fs` is 512 bytes and a 64-bit `fs_time` makes it 516 | `DEV_BSIZE` is 1024 (`sys/arch/rp2040/include/machparam.h`); the fields of `struct fs` sum to 1024 | refuted: the superblock is 1024 bytes and fills its block exactly; a 64-bit `fs_time` at offset 968 makes it 1032, measured with the target compiler |
| A 64-bit timestamp makes `struct dinode` 76 bytes and breaks `INOPB` | the target compiler gives 80: three fields grow by four bytes each and the 64-bit members align the struct to eight | holds with the size corrected; 1024 is not a multiple of 80 |
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
checks natively and as ILP32; the repaired source passes all of them.

## What survives

Three statements of the proposal survive, each in a narrower form.

1. The on-disk format needs no new bytes. Reading the three inode
   timestamps and `fs_time` as unsigned moves the last representable second
   from 2038-01-19 to 2106-02-07 with the 64-byte inode and the 1024-byte
   superblock unchanged. A timestamp before 1970 changes meaning under that
   reading: `touch -d 1969-12-31T23:59:58Z` stores -2 today, which an
   unsigned reading shows as 2106-02-07T06:28:14Z. The `Z` matters, since
   the kernel's default zone is 480 minutes west and `-t` reads local
   time; so does the second, since `ufs_setattr()` reads -1 as "leave
   this time alone" (`VNOVAL`) and stores nothing for it. The build images hold no such
   timestamp, since the clock starts at the image's build time, and a file
   system written on a board may hold one only where a user set it. The
   change therefore states the rule (a stored value at or above 2^31 is a
   time after 2038) and `fsck` reports inodes it affects.
   `di_reserved[1]` remains free for two high bits per timestamp (the
   scheme ext4 uses) if 2106 is ever too near.
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

## The interface width: 64 bits, chosen

The width of `time_t` in the user interface had two candidates with real
costs on both sides. The owner chose the 64-bit interface over a 32-bit
unsigned kernel and disk, on the POSIX.1-2024 requirement in the table's
fifth row. The table records what each candidate costs.

| | Unsigned 32-bit `time_t` | 64-bit `time_t`, 32-bit unsigned kernel and disk |
| --- | --- | --- |
| Last second | 2106-02-07 | 2106 in the kernel until a carry word lands; no interface limit |
| Shipped `full` image, measured below | no block | 7 of 217 free blocks in a prototype that cannot run; the working change's cost is unmeasured |
| Interface break | none in size; signedness changes | every time-carrying system call and every program, once |
| Times before 1970 | unrepresentable | representable in programs; clamped on disk |
| POSIX.1-2024 `time_t` (`<sys/types.h>`: an integer type at least 64 bits wide, set by Austin Group defect 1462) | fails | meets |
| Source audit | every signed comparison and subtraction of `time_t` | every `long` that holds a `time_t`, every `%ld` that prints one |

## Programs compiled on the board

The choice above binds programs the cross compiler builds. The compiler the
image ships as `/usr/bin/cc`, Smaller C (`usr.bin/smlrc`), has no 64-bit
integer type: `tokLongLong` is commented out in `smlrc.c`, and
`include/stdlib.h` hides `atoll()` from it for that reason. The selected
image ships neither headers nor a preprocessor (`usr.bin/smlrc/README.rp2040.md`),
so a program compiled on the board declares `time()` and `stat()` by hand
and binds to them by name, and it links the `/usr/lib/libc.a` the cross
compiler built. A 64-bit `time_t` behind the existing names would hand such a
program a 16-byte `struct timeval` where it reserved 8.

The leading design keeps the existing names narrow. `stat`, `fstat`,
`lstat`, `time`, `ctime`, `gettimeofday` and the other time-carrying entry
points keep their 32-bit unsigned meaning and their present layouts, which
are the kernel's; the headers the cross compiler reads declare the 64-bit
types and redirect each of those functions to a second symbol by an
assembler label, and libc holds that second set as wrappers that call the
narrow entry and widen each time zero-extended, or check and narrow on the
way in. The kernel gains no code and no second interface. NetBSD's
`__RENAME` symbols and glibc's `_TIME_BITS=64` redirects are the same
mechanism; neither source was re-read for this note.

What the design owes before it is adopted:

- libc is itself compiled by the cross compiler, so `time()`, and
  `ctime()`, `localtime()` and `gmtime()` in `ctime.o`, need a narrow and a
  wide build each, and an internal call must reach the one its caller's
  types match.
- The wrappers name both layouts in one translation unit, so the narrow
  structures need private tags (`struct __stat32`, `struct __timeval32`).
- Every time-carrying system call in `sys/kern/syscalls.master` takes a
  wide wrapper: `stat`, `fstat` and `lstat` (`struct stat`);
  `gettimeofday`, `settimeofday`, `adjtime`, `select`, `utimes` and
  `futimes` (`struct timeval`); `getitimer` and `setitimer`
  (`struct itimerval`); `getrusage` and `wait4` (`struct rusage`); and
  `pselect` (`struct timespec`). A call left on the narrow entry receives
  a wide caller's layout.
- `lastlog.ll_time` is a `time_t` (`include/lastlog.h`) and `utmp.ut_time`
  a `long`; both are disk records and take explicit 32-bit fields before
  the type changes.
- One executable can hold objects from both compilers: the Smaller C test
  harness links a cross-compiled companion (`usr.bin/smlrc/tests/run.sh`).
  An object that reaches the other width's symbol links without a
  diagnostic and overruns the smaller object, so the gate is a symbol scan
  of every staged program, calibrated on an object bound the wrong way.
- The wrapper cost is unmeasured: text per wrapper, text per shipped
  program, `libc.a` growth and free blocks on the packed image.

Adding `long long` to Smaller C would leave one interface and is the one
alternative not excluded: a build of `fca28c6a` with
arm-none-eabi-gcc 16.2.0 links the compiler at 47,460 bytes of text, 1,140 of
data and 26,312 of bss (`arm-none-eabi-size usr.bin/smlrc/smlrc.elf`),
74,912 in a 192 KiB process window (`machparam.h`);
`usr.bin/smlrc/README.rp2040.md` records an earlier build's figures. The
cost of the 64-bit type in it is unmeasured.
Converting in the kernel instead, with old and new system calls side by
side, keeps two user interfaces as well and spends kernel text for a clock
that stays 32 bits wide; `check-kernel-headroom` at `fca28c6a` reports
30,832 bytes free in the PICO kernel's flash region and 9,880 in
PICOCALC_PSRAM, the tightest of the four.

## Measured cost of each interface width

Three trees were built from clean worktrees with
`bmake MACHINE=rp2040 distribution`, which builds the world, packs the
`full` profile and writes the root image: `61f6cf6d` as it stands; `0db61ca4`
(`archive/measure-time-width-u32`), where `time_t` is `unsigned long`
everywhere, the kernel included; and `9c623881`
(`archive/measure-time-width-i64`), where programs see `time_t` as
`long long` while the kernel, the inode timestamps and `fs_time` keep
`long`. Both variants carry the source and Makefile changes listed below.
Free blocks are `fsutil`'s report on the 988-block root partition of each
image.

| Tree | Free blocks | PICO kernel text |
| --- | --- | --- |
| `61f6cf6d` | 217 | 102,094 |
| unsigned 32-bit | 217 | 102,094 |
| 64-bit interface | 210 | 102,094 |

The 64-bit variant's image has 7 fewer free blocks. That is the observed
delta of a prototype that cannot run, and it bounds the working change's
cost from neither side: the variant changes the type programs see and
nothing else, so the kernel still copies an 8-byte `struct timeval` where
programs expect 16; callers were compiled with the diagnostic downgraded;
`touch` is compiled from an undefined shift; and the seven-bit split
removes division code that the figure includes. Conversion at the system
call boundary adds kernel text, the split removes program text, and the
net is unmeasured until an image that boots is built. With the wide type the target
compiler gives `struct stat` 72 bytes (56 today) and `struct timeval` 16
(8 today), since a 64-bit member aligns its struct to eight. Across all 1,677 staged files,
most of which no profile ships, it changes 158 files and adds 44,434 bytes;
the unsigned variant removes 89. The largest growth sits in programs that
link the calendar conversion, between 636 and 956 bytes each before
packing (`uucico`, `adminbox`, `init`, `tar`, `sysbox`, `box`, `find`,
`utilbox`, `getty`, `login`, `ps`, `fsck`, `su`): the conversion divides a
64-bit count by 86,400 and so brings in the compiler's 64-bit division
routines. The seven-bit split above replaces those routines with 136
bytes; how much of the growth it removes per program is an inference from
the object sizes (about 280 bytes) until a build measures it.

The kernel text is equal in all three. In the 64-bit variant no kernel type
changes. In the unsigned variant the kernel's `time_t` changes signedness,
`(time_t)-1` becomes 4,294,967,295 there, and the generated code happens to
have the same size; `struct timeval.tv_sec` stayed `long` in that variant.

What the variants changed to build, which is the work a real change owes:

- Unsigned: `time()` in libc returns `long` and must return `time_t`;
  `bin/tar/tar.c` tests `st_mtime < 0`. With `struct timeval.tv_sec` also
  unsigned the kernel stops at `kern_time.c` (the interval-timer bounds and
  `itimerfix()`) and at `clock_catchup_apply()`'s `long *`.
- 64-bit: the same `time()` return type; `utmp.ut_time` is `long`;
  callers that pass a `long *` or the address of a 32-bit record field to
  `time()`, `ctime()`, `gmtime()` and `localtime()` were compiled as they
  stand with the diagnostic downgraded, so those programs are wrong at run
  time and the figure understates by their conversion; `adminbox` grows
  from 29,120 to 30,064 bytes and passes its 25-block packed limit, which
  the variant raises.
- Both: twelve program Makefiles carry their own `-Werror`, removed in the
  variants; the tree's warning flags stayed on without it.
- 64-bit, `usr.bin/touch/touch.c`: the variant removes the assertion that
  `time_t` fits a `long` and leaves `TIME_MAX` as it is, whose shift
  count is then negative, so `touch` in that image is compiled from
  undefined arithmetic and its bytes are part of the figure.

Limits of the measurement: the 64-bit variant converts nothing at the
system call boundary, so its image cannot run and the kernel cost of the
conversion is unmeasured; no variant was executed; one profile and one
kernel configuration were measured.

## Plan

Each step is one pull request. The first two are owed whichever width is
chosen; neither makes the code width-independent. `mktime.c` asserts that
`time_t` is a signed `long` and derives its range and day arithmetic from
that, so the step that changes the type adapts it.

1. `timegm()` range repair and the shared calendar (this change).
2. Width assertions where a time meets a fixed layout: `struct dinode`,
   `struct fs` and the V7 inode already assert their sizes; add the
   signedness and width assertions that make a change of `time_t` a compile
   error at each site that assumes four signed bytes
   (`tests/kernel/balloc_test.c` and `usr.bin/touch/touch.c` have them;
   `sys/kern/kern_time.c`'s `LONG_MIN`/`LONG_MAX` clamp, `difftime.c` and
   the `tar`, `ar` and `ls` formatters do not).
3. The width decision: 64 bits, recorded above.
4. Unsigned on disk and in the kernel clock: `icommon2`, `fs_time`,
   `time.tv_sec` and the `adjtime()` clamp. The gate is `balloc_test` and
   `ialloc_test` with a clock past 2038, and a Renode boot from an image
   whose `fs_time` is past 2038.
5. A target-executed proof of the two-name design: one test program built
   from a Smaller C object and a cross-compiled object, each reading the
   clock and a file's times through its own width and agreeing on a value
   with bit 31 set; with it, a bounded trial of `long long` in Smaller C.
6. The 64-bit interface by the surviving design, with the forward
   conversion moved to closed form and the seven-bit split.
7. `date`, `touch`, `ls`, `tar`, `ar`, `ps` and the host tools
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
- The 64-bit interface's cost is measured with the compiler's own 64-bit
  division; with the seven-bit split and with every call site converted
  it is unmeasured.
- The kernel sets its clock at boot from the superblock
  (`sys/kern/init_main.c`: `time.tv_sec = fs->fs_time`) and reads no
  hardware clock, so the time after a power cycle is the last superblock
  write. The RP2040's own real-time clock peripheral is unused.
- Time carriers outside the system calls are not yet audited for width:
  the crash-dump header, the msdosfs and V7 file-system timestamps, and
  the host tools that stamp an image (`tools/fsutil`, `sbin/mkfs`).
- No target execution: qemu-user, Renode and the board did not run the
  repaired `timegm()` or `mktime()`.
