# Stream: local measurement of libc duplication and a fixed-address libc prototype

Source products: the RP2040 build products present in the canonical checkout,
dated 2026-10-24 (lib/libc.a, lib/crt0.o, every box's `*.tool.o`, and
standalone program objects). Toolchain: arm-none-eabi-gcc 16.2.0. Scripts and
raw rows live in `../measure/`; `relink_against_libcx.sh REPO OUTDIR
[union|whole]` regenerates every input it uses. Kernel size from
`arm-none-eabi-size sys/arch/rp2040/compile/PICO/unix`: text 108,478, data
248, bss 39,848.

## System facts the measurement rests on

- `sys/arch/rp2040/doc/MPU.md` lines 5-7: one process is resident at a time and
  every image occupies the same 144 KB window at USER_DATA_START (0x20000000).
  Every application therefore already has a known, fixed load address.
- `lib/elf32-arm.ld`: user executables link at 0x20000000. Both loaders copy
  the payload to the window base (`sys/kern/exec_aout.c:190`,
  `sys/arch/rp2040/rp2040/exec_hsaout.c:168`) and `vm_swap.c:62` models clean
  text followed by one mutable extent; a libc RAM block below the program needs
  loader, admission and swap changes (stream 08).
- MPU regions 4 to 7 are disabled (MPU.md map table). MPU region 0 already
  grants user code r-x on the boot ROM, whose float routines libc calls through
  `lib/libc/arm/gen/rom_float_resolver.S`: user code already calls
  fixed-address shared code outside its image.
- heatshrink packing (`tools/bin/rp2040/hsaout -s`): box 33,756 raw to 27,977
  packed, utilbox 55,077 to 44,773, sh 36,308 to 31,070 (0.81 to 0.86).

## Static libc duplication (linker attribution)

30 executables (6 boxes, 24 standalone) were relinked with `-Wl,-Map` using the
build's flags. `map_libc_bytes.py` sums allocatable input-section sizes by the
input file the map names. The figures are linker attribution: merged strings
are charged to whichever input the map shows first, which moves several hundred
bytes between crt0 and program without changing the image. The block results
below use whole-image deltas and do not depend on attribution.

- libc-attributed image bytes across the 30: 264,611; other: 374,161.
- Union of libc members used: 257 members (`libc-union-members.txt`).
- Per-executable libc attribution ranged from 226 bytes (tee) to 17,073 (login).

## Prototype: one fixed-address libc, programs linked against its symbols

`libcx.ld` links the chosen members plus libgcc once: text and rodata at a
flash address, data at 0x20000000 with its load image after text, bss after
data. The address 0x10100000 in `libcx.ld` lies inside today's Dhara root
(0x10020000 to 0x101A0000) and only serves the size experiment; stream 08
places the image in a new partition.

| Image | text | data | bss | programs link at |
| --- | ---: | ---: | ---: | --- |
| union (257 members) | 32,448 | 1,006 | 4,414 | 0x20001600 |
| whole libc.a | 50,000 | 1,058 | 4,806 | 0x20001800 |

The "331 KB" figure in `distrib/rp2040/Makefile.inc:145` is the archive file
size with symbol tables, not code.

Three upward references from libc to the program block a shared libc and are
placed in libc RAM by `--defsym`: `environ` and `__progname` (defined in
lib/crt0.o) and `_end` (sbrk's initial break). An implementation turns each
into a libc-owned variable that crt0 sets.

Each executable links twice from the same objects: static (`-lc`, base
0x20000000) and shared (`--just-symbols` before crt0, shifted base). Both pass
through elf2aout and `hsaout -s`. The harness enforces three rules:

1. Export collision: a program global that libc also exports binds silently to
   the libc address under `--just-symbols`, so any intersection fails the row.
   Calibrated: `bin/sh` (whose `io.c:109` defines a descriptor-redirecting
   `rename(int,int)`) fails, tee passes.
2. Residual members: a shared-form link that still pulls any libc.a member
   fails, since a residual member duplicates libc state. Placing
   `--just-symbols` before crt0 removes the exit, _exit and fakcu residue an
   earlier ordering left.
3. Library order: program libraries follow the objects, as the source
   Makefiles order them.

| Set (rows passing all rules) | Static packed blocks | Shared packed blocks | Saved |
| --- | ---: | ---: | ---: |
| union image, 28 executables | 502 | 288 | 214 |
| whole image, 26 executables | 475 | 278 | 197 |
| 5 boxes (box, utilbox, sysbox, textbox, grepbox) | 145 | 94 | 51 |

Rows failing: adminbox (objects predate its built-in true and false applets);
sh against both images (`rename`; also `tmpnam` against the whole image); ps
(`uname`) and init (`shutdown`) against the whole image. Each collision is a
program defining an external name the C library reserves; making the program's
function `static` repairs it. A larger export set raises the collision count.

## Do boxes still pay once libc is shared?

Every applet of the five boxes was linked standalone from its localized
`*.tool.o` with `--defsym=main=<tool>_main` (`applet-results.txt`). These
links predate the residual-member rule: each shared-form applet still carries
about 50 bytes of exit, _exit and fakcu, which biases the separate-file column
upward by at most one block per applet. Localized tool objects export only
`<tool>_main`, so the collision rule cannot fire for them.

| Layout of the five boxes' applets | Packed blocks |
| --- | ---: |
| static libc, separate files | 538 |
| static libc, boxes (shipping) | 145 |
| shared libc, separate files | 138 |
| shared libc, boxes | 94 |

A shared libc alone matches the shipping boxes; together they reach 94. Boxes
save 44 blocks over separate files once libc is shared.

## Flash cost of the libc partition (exact)

`flashimg -c` rebuilt with `FLASH_FS_OFFSET` moved gives the root capacity:

| libc partition | Dhara region | Root capacity | Root loss |
| --- | ---: | ---: | ---: |
| none (today) | 1536 KB | 989 KB | 0 |
| 48 KB (union image fits) | 1488 KB | 961 KB | 28 |
| 64 KB (whole image fits) | 1472 KB | 950 KB | 39 |

Net root gain on the measured rows: union 214 - 28 = 186 blocks; whole 197 - 39
= 158 blocks. The `flash.h` comment above `FLASH_FS_OFFSET` still says the
kernel measures 90 KB with 38 KB free; it measures 108.7 KB.

## Native toolchain

The board's a.out `ld` encodes Thumb branches directly and rejects
displacements beyond about 4 MiB (`usr.bin/ld/ld.c:625`); a call from SRAM to
XIP flash spans about 255 MiB. On-board `cc` therefore cannot link against an
XIP libc without long-call stubs and an absolute-symbol import object; it keeps
linking static `libc.a`, so a whole-libc image buys no native-toolchain
coverage.

## Costs the prototype exposes

- RAM: every process reserves the image's whole libc state (union: 5,420
  bytes, rounded to 0x1600). Large members: gr_mem 800, crypt KS 768, regex
  expbuf 512, several 256-byte `line` buffers, `_ctype_` 257, `_iob` 192. The
  reservation is mutable and swaps with every process.
- Branches from 0x2000xxxx to XIP flash exceed Thumb BL range; GNU ld inserts
  long-branch veneers, counted in the block totals above.
- XIP execution speed and competition with the XIP kernel for the 16 KB cache:
  unmeasured.

## What the measurement establishes and what it leaves open

- Established: shared libc text removes 197 to 214 packed blocks over 26 to 28
  executables, at a 28-block partition cost for the union image.
- Established for today's design: relocation is unnecessary for the
  application load address, which the single window fixes.
- Not established: runnable images (no loader, crt0 or swap change exists), a
  comparison against an implemented PIC design (relocation metadata size,
  loader time), XIP speed, swap growth.

## Resolution of the adversarial review

1. ACCEPT (blocker). `bin/sh/io.o` defines `rename`; the export-collision rule
   now fails the row, calibrated on sh and tee.
2. ACCEPT. Library order fixed; awk and login now pass.
3. ACCEPT. The stream states that links are not runnable images and points to
   stream 08 for loader, admission and swap.
4. ACCEPT. Native `ld` range limit recorded; whole-libc no longer claims native
   coverage.
5. ACCEPT. Conclusion narrowed to "relocation is unnecessary for today's load
   address"; no PIC implementation was measured.
6. ACCEPT. The harness generates its linker script, symbol file and export
   list from the repository and the member list.
7. ACCEPT. Byte figures labeled linker attribution; savings rest on
   whole-image packed blocks.
8. ACCEPT with correction: the exact loss comes from `flashimg -c`, 28 KB for
   a 48 KB partition (the review's formula estimate was 17 KB for 32 KB).
9. ACCEPT. Wording now says boxes save 44 blocks.
