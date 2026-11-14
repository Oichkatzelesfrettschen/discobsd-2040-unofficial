Source reading at `aec36799`, `arm-none-eabi-gcc` 16.2.0 object sizes, host runs of `tests/libc_contracts/mktime_contract_test.c`, and three clean distribution builds that differ in the type of `time_t`, each through the packed root image; for the section on programs at the 64-bit view, builds of `9561fd1c` and of the change through `bmake flash`, the qemu-user gates and one Renode boot; for the section on the 32-bit view, clean builds of `c2ffd2d8` and of the change through `bmake flash`, the host contract gates and the Smaller C tier under qemu-arm; for the board archive's calendar members, a clean build through `bmake flash` and one Renode boot that compiles a program on the target and one run of the same program on the measured Pico; no other board run.

# Time representation past 2038: verified claims, measurements and plan

`time_t` is `long` (`include/time.h`, `sys/sys/types.h`), four bytes on
ARMv6-M, so the last second it names is 2038-01-19T03:14:07Z. The board has
no time-of-day clock: `sys/kern/init_main.c` seeds `time.tv_sec` from the
root superblock's `fs_time`, and `date` sets it. A proposal to widen the
representation, with storage-wear changes attached to it, was checked claim
by claim against this tree. The checks leave one decision for the owner (the
width of `time_t` that programs see), one defect that holds at any
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
   stored. A 64-bit `time_t` in the interface programs are compiled
   against can be served from a 32-bit count that is zero-extended on the
   way out, in the kernel or in libc, so the costly part (that interface)
   changes once and the cheap part (the kernel's representation) is free
   to change later.

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
narrow entry and widen on the way out, or check the range and narrow on
the way in. A second count since the epoch (`st_mtime`, `gettimeofday`)
widens zero-extended, since the kernel reads it unsigned. A signed
interval widens sign-extended: `adjtime()` returns the correction still
owed in `olddelta`, which is negative while the clock is being slowed
(`sys/kern/kern_time.c`). The kernel gains no code and no second interface. NetBSD's
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
- `sysctl()` carries a time as payload: `KERN_BOOTTIME` copies out the
  kernel's `struct timeval` (`sys/kern/kern_sysctl.c`). The call's
  signature holds no time type, so a redirect by name cannot select a
  width; a wide caller reads the narrow payload through a private narrow
  structure or a libc helper that widens it.
- `lastlog.ll_time` is a `time_t` (`include/lastlog.h`) and `utmp.ut_time`
  a `long`; both are disk records and take explicit 32-bit fields before
  the type changes.
- One executable can hold objects from both compilers: the Smaller C test
  harness links a cross-compiled companion (`usr.bin/smlrc/tests/run.sh`).
  An object that reaches the other width's symbol links without a
  diagnostic and overruns the smaller object. The linked program cannot
  show it: `tools/elf2aout` writes `a_syms = 0` in the normal conversion,
  and an executable with a wide caller holds both symbol sets anyway,
  because each wide wrapper calls its narrow entry. The gate therefore
  reads the undefined references of each relocatable object before the
  link against the width its compiler implies, allows the narrow calls of
  the wrapper members by name, and is calibrated on a wrongly bound object
  inside an otherwise valid program built by both compilers.
- The wrapper cost is unmeasured: text per wrapper, text per shipped
  program, `libc.a` growth and free blocks on the packed image.

Adding `long long` to Smaller C would leave one interface and is the one
alternative not excluded: a build of `fca28c6a` with
arm-none-eabi-gcc 16.2.0 links the compiler at 47,460 bytes of text, 1,140 of
data and 26,312 of bss (`arm-none-eabi-size usr.bin/smlrc/smlrc.elf`),
74,912 in a 192 KiB process window (`machparam.h`);
`usr.bin/smlrc/README.rp2040.md` records an earlier build's figures.

A source reading of the compiler, with no implementation, describes what
that alternative would take. The compiler holds every expression value
in one machine word (`int stack[STACK_SIZE][2]`, `smlrc.c`) and one
working register, and `cgthumb.c` emits no add-with-carry instruction.
It does pass and return a structure by value: `NO_STRUCT_BY_VAL` is
defined only under `NO_EXTRAS`, which no build sets. A 64-bit value
could therefore travel inside an expression as the address of an 8-byte
frame temporary, the way a structure value does, with each operator
lowered to a helper call as the floating-point operators are. The
procedure call standard still asks for a register pair in three places
the compiler has no mechanism for: a 64-bit call result, a 64-bit
return value, and a 64-bit argument among the first four argument
words, which must start at an even register; the call path in
`cgthumb.c` loads those words into `r0` to `r3` one by one and pads
none. `libc.a` defines the 32-bit `__aeabi_idiv`, `__aeabi_idivmod`,
`__aeabi_uidiv` and `__aeabi_uidivmod` and no 64-bit multiply, divide
or shift helper (`arm-none-eabi-nm lib/libc.a`), so a program that
multiplies or divides a 64-bit count needs new library members, and
every 64-bit operation in a compiled program becomes a helper call and
a frame temporary. The reading projects several hundred changed source
lines across the type parser, each operator branch of the expression
evaluator and the call and return paths; that figure is a projection
with no implementation or size measurement behind it. The cost of the
libc wrappers is unmeasured as well, so the two are not ranked by size.
The wrappers stay the chosen course for a different reason: they leave
the compiler that builds every program on the board unchanged.
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
removes division code that the figure includes. The conversion the
prototype omits adds text where it is placed (in the leading design
under "Programs compiled on the board", a libc wrapper in each program
that calls a time-carrying entry point, since the linker takes only the
archive members a program references; kernel text if the conversion sits
at the system call boundary), the split removes program text, and the
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

Limits of the measurement: the 64-bit variant converts nothing between
the kernel's 32-bit layouts and the wide ones programs see, so its image
cannot run and the cost of the conversion, in libc or in the kernel, is
unmeasured; no variant was executed; one profile and one kernel
configuration were measured.

## The calendar functions of the 64-bit view

libc builds the functions that convert or carry a count themselves a
second time from their own source with `_TIME_BITS=64`: `ctime.c`
(`__ctime64`, `__localtime64`, `__gmtime64`), `mktime.c` (`__mktime64`,
`__timegm64`), `time.c`, `difftime.c` and `wait3.c` in `lib/libc/gen`, and
`utime.c` and `ftime.c` in `lib/libc/compat`, whose declarations the new
`include/utime.h` and `sys/sys/timeb.h` carry. The a.out archive gains the
same members except `mktime64.o`, since it carries no `mktime.o`. The zone
both widths read moved from `ctime.c` into `tzset.c` (`tzstate.h`), so a
program of either width links one `tzset()` and one `tzname[]`. `struct
tms` counts sixtieths of a second of CPU time (`lib/libc/compat/times.c`),
so its fields are `long` at both widths and `times()` needs no second
symbol.

The 64-bit forward conversion is the closed form with the seven-bit split
(`ctime.c`, `offtime.h`); the inverse sums the fields in 64 bits with no
carry between them, so a field at either end of an `int` still reaches an
instant inside the range. The range is every year an `int` `tm_year`
holds, -67,768,040,609,740,800 to 67,768,036,191,676,799. No member
imports a 64-bit multiply, divide or arithmetic-shift helper
(`tests/time64_calendar/check.sh`, `arm-none-eabi-nm`).

Text of each ELF member, `arm-none-eabi-size`, `arm-none-eabi-gcc` 16.2.0
at the libc flags (measured):

| Source | 32-bit member | 64-bit member |
| --- | --- | --- |
| `ctime.c` | 479 (`ctime.o`) | 663 (`ctime64.o`) |
| `tzset.c`, both widths | 119 | -- |
| `mktime.c` | 618 | 678 |
| `time.c` | 32 | 38 |
| `difftime.c` | 32 | 92 |
| `wait3.c` | 18 | 18 |
| `utime.c` | 26 | 42 |
| `ftime.c` | 52 | 58 |

`ctime.o` was 586 bytes with the zone in it; with the zone in `tzset.o`
the pair is 598, 12 bytes more from `localtime()` addressing the state
and its own result through two literals instead of one. Every other
32-bit member is byte-identical to its build before the change.

Two refusals remain in `mktime()` at both widths, both from its search
starting at `timegm()` of the fields: the local time whose UTC reading is
-1 is refused in any zone but GMT, since -1 is also the refusal; and a
local time within the zone's offset of either end of the range is refused
when the first probe of the search falls outside it.

## Programs on the 64-bit view

`sys/sys/_time_t.h` gives the 64-bit view to every unit the cross
compiler builds outside the kernel and Smaller C: with `_TIME_BITS`
undefined, a compiler that defines `__ARM_EABI__` and not `__unix__` gets
`long long time_t` and the `__NAME64` bindings; `_TIME_BITS=32` is the
opt-out and `_TIME_BITS=64` the explicit request. The default is keyed to
the target compiler because about fifty host contract tests read the
tree's headers and link the host's libc, which defines the existing names
(`stat`, `time`) and, on 32-bit glibc, some `__NAME64` names with its own
layouts; under a header default for every compiler those builds failed to
link (61 undefined references to `__stat64` in one `check-host` run) or
bound to glibc's symbols.

libc compiles every member with `_TIME_BITS=32` (`lib/libc/Makefile`,
`lib/libc_aout/libc/Makefile`, `distrib/rp2040/mkboardlibc.py`): the
members that define the existing names keep the kernel's layouts for
programs Smaller C compiles on the board, and a member whose interface
carries no time (stdio's `fstat`, `sleep`'s `setitimer`, `waitpid`'s
`wait4`) calls the kernel's entry point without a wrapper, which keeps
the board archive's member closure as it was. With the default still
32-bit, that selection compiled all 1,084 ELF, a.out and board members
byte-identical to the parent. The `time64_*.c` members replace the
selection with 64 themselves, and the `NAME64.o` rules undefine it first.
`syslog.o` is built at the 64-bit view as well: it stamps each message
with `time()` and `localtime()` and carries no time in its interface, so
at the 32-bit view its log lines would read a clock past 2038 as 1901.
The other members that reach a 32-bit entry point internally (`devname`,
`getcwd`, `getwd`, `getusershell`, `mktemp`, `ndbm`, `opendir`,
`scandir`, `ttyname`, stdio's `filbuf`, `flsbuf` and `remove`, `alarm`,
`ualarm`, `sleep`, `usleep`, `wait`, `waitpid`; `arm-none-eabi-nm -u`)
read only fields or intervals the 32-bit layouts carry. `sleep` takes the
difference of its two clock readings modulo 2^32 (see "The 32-bit view:
unsigned to 2106" below).
Test builds that compile libc members from source (`tests/libc_contracts`,
`tests/dirent_contracts`, `tests/time64_*`, the a.out probe) select the
32-bit view the same way.

`tools/check_time_binding.py` (`check-time-binding`, in the cross tier)
reads the undefined symbols of every ARM relocatable object a build
leaves under the program and library trees, `lib/libc` excepted, and
rejects a reference to any name a `__TIME64_NAME()` declaration binds
(24 names). It found one binding the compiler did not: uucp declared its
own `struct timeb` and called `ftime` without `<sys/timeb.h>`, so
`cntrl.o` and `gio.o` reached the 32-bit `ftime` with a 16-byte
structure. Pointed at `lib/libc/gen`, which it excludes, it lists the
members above as the control that it reads what it should. It is
calibrated on a unit at the default view (passes), the same unit with
`_TIME_BITS=32` and a unit declaring `long time();` by hand (each
rejected), and a host object (skipped).

What the programs needed, by class (measured with `-Wall -Wextra
-Werror` at the 64-bit default):

- Kernel payloads: `KERN_BOOTTIME` is the kernel's 8-byte `struct
  timeval`; `sysctl`, `w` and `vmstat` read it as `struct __timeval32`,
  which `sys/sys/time.h` now declares in every view. `vmstat` checked the
  payload size and would have exited on the 16-byte wide structure. `ps`
  widened `kp_start`, an `int32_t`, with its sign and now reads it
  unsigned. Each installed header compiled at both views with
  `-g -fno-eliminate-unused-debug-types` and compared with `pahole
  --sizes` changes seven structures only: `timeval` (8 to 16 bytes),
  `timespec` (8 to 16), `itimerval` (16 to 32), `stat` (56 to 72),
  `rusage` (72 to 88), `timeb` (12 to 16) and `utimbuf` (8 to 16), each
  passed only to a function with a `__NAME64` symbol; `kinfo_proc` (184),
  `proc` (100), `statfs` (236), `tty` (100), `utmp` (44) and `lastlog`
  (28) keep their sizes.
- Disk records: `utmp.ut_time` and `lastlog.ll_time` are `utime32_t`, with
  the 44- and 28-byte record sizes asserted where `long` is four bytes.
  `date` (`bin/date2`) passed `&wtmp[1].ut_time` to `time()` through a
  `(time_t *)` cast, which at the wide view writes eight bytes into a
  four-byte field; `login`, `last`, `who`, `init`, `reboot`, `logwtmp` and
  `logout` read or write the field through `utime32_t`. `last` keeps its
  per-line logout times as `time_t`, since it marks a crash by negating
  one and a count past 2038 is negative as a `long`.
- Formats: the shipped programs print a time with `%lu` of an `unsigned
  long`, which holds every count the kernel can hold, rather than `%lld`:
  `_doprnt` prints `?` for any `ll` conversion unless the program links
  `doprnt_llong.o` (`PRINTF_LLONG=yes`, about 200 bytes). The uucp status
  file and log, zmodem's octal file time, Tcl's `file mtime` and
  `sysctl -n kern.boottime` keep their text formats; uucp reads the time
  field back with `%lu`, which also reads a negative count an older
  program wrote after 2038 as the same unsigned count.
- 64-bit division: an interval kept in `time_t` brings in
  `__aeabi_ldivmod` and libgcc's `__divdi3` (about 900 bytes in
  `adminbox`, which then exceeded its 25-block packed watermark).
  `shutdown`, `w`, `uusnap`, `tip`, `canfield`, `last`, `find` and
  `touch` keep intervals and day counts in `long` or `unsigned long`;
  `find -mtime` divides the magnitude of a difference of two kernel counts,
  which is below 2^32, as an `unsigned long`.
- `timeout` holds its durations as two `long`s; a `struct timeval` member
  grew `main`'s frame from 128 to 160 bytes, past the limit
  `tests/timeout_contracts/stack_test.py` holds.
- `touch` counts seconds in `time_t` and derives its bounds for any width
  up to `long long`; `-t` and `-d` reach 2106, the kernel's limit.
- A `-Wconversion` pass over the 126 sources that read a time, at the
  64-bit view, found two absolute times narrowed to `long`: `shutdown
  +minutes` returned the shutdown time through a `long`, and the shell
  kept each mail file's modification time in a `long` and compared it
  with `st_mtime`, which past 2038 would announce new mail at every
  check. Both now hold `time_t`. The remaining conversions it reports
  store a clock reading in a disk field of the kernel's width (`fsck`,
  `mkfs`), seed a random generator or time a benchmark (games,
  `basic`, unixbench), measure an interval (`tip`, Tcl's `time`), or sit
  in sources the build does not compile (`bin/csh`, `bin/date`). Sources
  that need their own include paths were not reached by the pass, among
  them uucp, `newfs_v7fs` (whose V7 inode stores an `int` time) and
  `retro` (a 32-bit Forth cell).
- Hand-written declarations (`time_t time();` and the like) in `ls`,
  `chpass`, `rogue`, `pom`, `dhrystone`, uucp and libutil give way to the
  headers. GCC keeps a header's assembler label on a later block-scope
  redeclaration, so most of them were already bound; `rz` had no header
  and passed a `time_t[2]` to `utime()`, now a `struct utimbuf`.

Sizes, `bmake flash` of the `full` profile before (`9561fd1c`) and after,
`arm-none-eabi-gcc` 16.2.0 (measured; a.out text and file bytes of the 35
programs the manifest ships):

| Program | Text before | Text after | Change |
| --- | --- | --- | --- |
| `/bin/sysbox` | 27,744 | 28,364 | +620 |
| `/bin/tar` | 27,432 | 28,048 | +616 |
| `/bin/box` | 29,572 | 30,136 | +564 |
| `/sbin/adminbox` | 26,296 | 26,804 | +508 |
| `/usr/bin/find` | 15,636 | 16,136 | +500 |
| `/sbin/init` | 16,464 | 16,944 | +480 |
| `/usr/bin/utilbox` | 53,700 | 54,144 | +444 |
| `/bin/ps` | 15,488 | 15,860 | +372 |
| `/usr/bin/login` | 19,636 | 19,980 | +344 |
| `/sbin/fsck` | 26,504 | 26,836 | +332 |
| all 35 | 703,672 | 710,692 | +7,020 (file bytes +7,028) |

The root image keeps 238 of its 988 blocks free (240 before), `adminbox`
packs to 24 blocks against its watermark of 25 (24 before; 25 in a build
where `syslog` converted the clock at the 32-bit view and so linked both
calendars), and every
kernel's text, data and bss are unchanged (PICO 99,763 text,
`check-kernel-headroom` 30,832 bytes free in the PICO region); the kernel
images differ only in the version string and its checksum. The growth is
smaller than the prototype's 7 blocks because the calendar members use the
seven-bit split and libc's own callers stay on the 32-bit entry points
(an inference from the members each program links, not a measurement).

`check-renode` (PICO_UART kernel under Renode 1.17.0 with the pinned
RP2040 models, measured) now sets the clock as root with `date -u
4607230038` and reads it back: `date -u` printed `Mon Jul 23 00:38:01 GMT
2046`; `touch -t 204501020304` and a file created at that clock listed as
`Jan  2  2045` and `Jul 22 16:38` (the kernel's zone, eight hours west);
and `sysctl -n kern.boottime` printed 2415919069, a count with bit 31
set, since the kernel moves its boot time with a step of the clock. Each flash program or erase re-enters XIP
through the boot ROM, which ten warning classes count. On this branch the
boot run without the stage counted 5,935 against ceilings of 6,000, with
`date -u 4607230038` alone 6,530, and with the whole stage 7,660, so the
clock step and the file operations account for about 600 and 1,100
re-entries; `tools/renode/warning-classes.txt` raises those ceilings by
half in a commit of its own. `main` was not run, so whether it sat as
near its ceilings is unmeasured. One earlier run of the date-only variant
printed nothing on UART0 within 20 virtual seconds while another
session's Renode ran on the host, and the immediate rerun passed; the
cause is not identified. No board ran this change.

The narrow view was a signed `long` in this change; the next section makes
it the kernel's unsigned count.

## The 32-bit view: unsigned to 2106

`sys/sys/_time_t.h` gives the 32-bit view `typedef utime32_t time_t`, the
type the kernel already had, so a program Smaller C compiles on the board
and the libc members that define the existing names read a count with bit
31 set as a time after January 2038, through 2106-02-07 06:28:15 UTC. No
symbol and no layout changes: `time_t`, `struct stat`'s three times and
`struct timespec`'s `tv_sec` keep four bytes, the 64-bit view's
`__NAME64` members are byte-identical to their build before the change
(the 47 `NAME64.o`, `time64_*.o` and `syslog.o` objects under `lib/libc` and `lib/libc_aout`, `md5sum`), and an object
compiled before it links with the same bytes; only the reading of bit 31
changes. `struct timeval` keeps its signed `long` `tv_sec`: the kernel
passes intervals in it (`select` and `setitimer` timeouts, `adjtime`
deltas, which go negative while the clock is slowed), so a program that
reads the clock from it converts to `time_t`, as `time()` does.

What the type change asks of a caller, and what was chosen:

- `(time_t)-1` is the count 4294967295, 2106-02-07 06:28:15 UTC, which is
  also the value `utimes` reads as "leave this time alone". It stays the
  error answer of `time()`, `mktime()` and `timegm()`, as C defines it, so
  the last second of the range is ambiguous the way one second before
  1970 was; `mktime()` and `timegm()` leave `tm_wday` as the caller set
  it when they refuse and rewrite it when they accept, so a caller tells
  them apart by setting it to -1 first, as `touch` does (`<time.h>`). A
  program that tests `t == (time_t)-1` is unchanged; one
  that tests `t < 0` never sees an error (no shipped program is built at
  this view; `check-time-binding` rejects one).
- A difference of two counts in `time_t` wraps instead of going negative.
  `difftime()` converts each end to `double` first, so it gives the signed
  difference; `sleep()` takes the difference modulo 2^32, which also
  carries it across the 2106 wrap.
- Times before 1970 have no count. `timegm()` and `mktime()` refuse them.

The members, measured with `arm-none-eabi-size` (ELF, `lib/libc/gen`) and
the tree's `size` (a.out, `lib/libc_aout/libc`) after a clean build of
`main` (`c2ffd2d8`) and of the change, `arm-none-eabi-gcc` 16.2.0 at the
libc flags:

| Member | ELF text before | after | a.out text before | after | Change |
| --- | --- | --- | --- | --- | --- |
| `ctime.o` | 479 | 479 | 448 | 448 | the count divided unsigned, through its low 32 bits; transitions compared by sign |
| `mktime.o` | 618 | 636 | -- | -- | range 0..4294967295 as 32-bit constants; fields kept as days and a second of the day, so a zone offset reaches both ends of the range; the answer accepted by range, not by value (544 bytes before the range-edge repair below) |
| `difftime.o` | 32 | 32 | 32 | 32 | imports `__aeabi_ui2d` in place of `__aeabi_i2d` |
| `sleep.o` | 76 | 72 | 76 | 72 | elapsed time modulo 2^32 |
| `time.o`, `tzset.o`, `ftime.o`, `utime.o` | 32, 119, 52, 26 | unchanged | 32, 112, 52, 28 | unchanged | byte-identical objects |

After `bmake flash` of the `full` profile the root image keeps 236 of its
988 blocks free, as before, and every kernel's text, data and bss are
unchanged (PICO 99,763 text; `check-kernel-headroom` 30,832 bytes free in
the PICO region). 64 staged programs differ: 49 of the 50 whose build
leaves a disassembly define `sleep`, which `execvp` calls on `ETXTBSY`;
the fiftieth, `pdc`, defines no time function and differs between two
builds of one tree, so its difference is the build's own.

No member imports a 64-bit helper (`arm-none-eabi-nm -u`: `ctime.o` and
`mktime.o` import only the 32-bit `__aeabi_idiv`, `__aeabi_idivmod`,
`__aeabi_uidiv` and `__aeabi_uidivmod`). The bound 0xffffffff is written
as a constant, which costs nothing where `unsigned long` is four bytes
and keeps a host build whose `unsigned long` is eight bytes
(`tests/libc_contracts`) to the target's range and refusal: there the
refusal is still the count 4294967295, not `(time_t)-1`, and
`__discobsd_offtime()` reads the count through its low 32 bits. So the
native host variants of the ctime, mktime, zone, difftime and sleep
gates run the same cases as the ILP32 ones. Only the ILP32 variants are
honest about a four-byte `long`: the parent revision's zone and difftime
members and its sleep across the 2038 carry fail there and pass natively,
where their signed `long` is eight bytes; natively the parent's sleep
fails only across the 2106 wrap.

`localtime()` with a zone file (`TZ_ZONEINFO`) compares the instant with
each transition, which the file stores as a signed four-byte count:
a transition before 1970 precedes every instant, and the comparison
says so instead of reading it as a count near 2^32.

The tests (measured, host and qemu-user):

- `check-libc-ctime` and `check-libc-mktime` read 3,027 counts and 37
  field sets of `tests/libc_contracts/calendar32_vectors.py` (CPython's
  `datetime`) and every whole day from 1970 to 2106-02-06 against a
  counting calendar; 157,910 checks in the mktime gate per variant, one
  of which counts the round trips of a whole band.
  Built against the parent revision's headers and members, the new mktime
  gate fails 74,614 of 157,895 checks at ILP32 and 52,999 natively, and
  the ctime gate fails at both widths.
- `check-libc-zone` adds a file whose first transition is stored
  negative; the parent revision fails it at ILP32.
- `check-libc-difftime`'s cases span 0..4294967295 and the 2038 carry;
  the parent revision fails four of nine at ILP32.
- `check-libc-sleep` (new) replays clock readings across the 2038 carry
  and the 2106 wrap; the parent revision fails the interrupted sleep
  across the 2038 carry at ILP32 and the one across the 2106 wrap
  natively.
- `t23_timeconvert` (new, Smaller C under qemu-arm) prints
  `asctime(gmtime())` of 0, 2147483647, 2147483648, 2415919104,
  4294967294 and 4294967295 and converts each back through `timegm()`;
  against the parent revision's `libc.a` it prints Dec 13 1901, Jun 16
  1910 and Dec 31 1969 for the last four.

`mktime()` at the 32-bit view converts both ends of the range in every
zone. A first version started its search at `timegm()` of the fields and
took an answer equal to `TIME_REFUSED` as a refusal, which refused three
kinds of valid local time: in a zone west of Greenwich the first hours of
1970-01-01 UTC, whose fields fall on 31 December 1969 (counts 0 to 28,799
in the kernel's default zone, eight hours west); in a zone east of it the
last hours before 2106-02-07 06:28:15 UTC, whose fields read past that
instant as UTC; and in any zone the local time whose fields read in UTC
as the count 4294967295 (in UTC+1 the local fields 2106-02-07 06:28:15,
count 4294963695), GMT's last second among them. `split_fields()` now
keeps the fields as a day count and a second of the day, both in a
`long`, `join_count()` subtracts the zone offset as whole days and a
remainder below a day, so every step stays in 32 bits, and the search
starts at the fields read as UTC held to the nearer end of the range;
an answer is accepted by its range alone, so the last count rewrites the
struct like any other. `mktime.o` grows from 544 to 636 bytes and imports
no 64-bit helper; `mktime64.o` is byte-identical (`md5sum`).
`check-libc-mktime` round-trips every count of the first and the last day
(0..86,399 and 4,294,880,896..4,294,967,295) through `localtime()` and
`mktime()` in GMT and in zone files eight hours west, one hour east and
fourteen hours east, requires the local fields of the instants one second
and one day outside each end to be refused with the sentinel left
(CPython `datetime` fields, 40 vectors), and rejects two mutants, the
preflight before the offset and the value-only refusal check
(`tests/libc_contracts/mktime_contract_mutate.py`). The gate built
against the first version fails nine checks at ILP32 and natively, in all
four zones; `t23_timeconvert`, which now also converts each count through
`mktime()` in GMT, fails at the last count against it.

What stays limited:

- The 64-bit `mktime()` keeps the value-only check, since its member is
  held byte-identical: it refuses the local time whose fields read in UTC
  as the count -1, one second before 1970 in GMT and the corresponding
  instant in any other zone.
- The board's `/usr/lib/libc.a` (`distrib/rp2040/boardlibc-members`)
  carries the forward conversion: `ctime.o` (`ctime`, `localtime`,
  `gmtime`), `tzset.o`, `asctime.o`, `timezone.o`, which `tzset.o` calls,
  and `index.o`, which `timezone.o` calls. The archive grows from 45,314
  to 48,294 bytes and the packed root keeps 233 of its 988 blocks free
  (236 with the earlier member list, same tree). It carries no `mktime.o`,
  so a program compiled on the board converts a count to a date and not a
  date to a count. `check-board-libc-output` links a consumer of the five
  entry points against the archive, and `check-renode` compiles a program
  with the image's `cc` at a 2046 clock and reads the date `ctime()`
  prints.
- `timercmp()` and a program's own comparison of two `tv_sec` clock
  readings stay signed, since the field is shared with intervals.
- `tests/rp2040/fptest` calls `difftime` at the 64-bit view, so its
  `difftime` corpus runs `__difftime64`, not this member, and its comment
  still describes the 32-bit member's `__aeabi_i2d` path.
- The board archive's `ctime.o`, `tzset.o`, `timezone.o` and `asctime.o`
  ran in Renode, in the program `check-renode` compiles on the target at
  a 2046 clock, and on the measured Pico (PICO kernel, USB console): with
  the clock set to 2046-07-23 00:38 UTC, the same two-line program
  compiled by `/usr/bin/cc` in 30.86 seconds of real time (3.63 system)
  and printed `Sun Jul 22 16:38:44 2046`, the kernel zone's local time.
  `mktime.o` is in neither archive the board ships; qemu-user ran it
  under `t23_timeconvert` with the tier's stand-in clock.
- Compiling that two-line program with the image's `cc` raises the
  Renode run's flash re-entries (one per program or erase command) from
  about 7,660 to about 14,400. The compiler, assembler and linker write
  their files under `/tmp` on the root file system, and each of the four
  processes can be written to the raw swap region; the board's 31
  seconds for the compile, against 3 virtual seconds in Renode, whose
  flash model erases in no time, point at flash commands as the cost.
  Which of the two paths carries the traffic is unmeasured, and a count of the program commands
  by flash offset in the Renode log would decide it.

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

Step 6 for the programs the cross compiler builds, and the program half of
step 7, are the section "Programs on the 64-bit view" above; `tar`, `ar`
and the host tools compile at the wide view unedited, and their review
for times past 2038 stays open with step 4.

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
- The RP2040 has a real-time clock peripheral (datasheet section 4.8,
  `sys/arch/rp2040/doc/DATASHEET-INDEX.md`) that divides the `clk_rtc`
  reference to one hertz; the section names no supply apart from the
  chip's own. The port leaves it unused. Whether it keeps counting
  through a PicoCalc power-off, where a battery is present, is
  unmeasured; on a bare Pico a power cycle resets it, so it would not
  replace the superblock time at a cold boot there.
- The flash part on the measured board answers JEDEC `EFh 4015h`, a
  Winbond W25Q16JV. Its datasheet (revision G, features list and AC
  characteristics) gives a minimum of 100,000 program-erase cycles per
  sector, more than 20 years of data retention, and a 4 KB sector erase
  time of 45 ms typical and 400 ms maximum; the board measured 29,072
  microseconds for one erase (`tools/flash-semantics`, recorded in
  `renode-rp2040-upstream-reports.md`). Those figures are the
  budget the storage-wear items above are judged against, and no wear
  count has been measured on the board.
