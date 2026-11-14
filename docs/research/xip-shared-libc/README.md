# Shared execute-in-place libc against multicall boxes and relocating loaders

This note asks whether DiscoBSD's RP2040 userland should keep sharing libc
only through multicall executables ("boxes"), adopt position-independent code
with a loader that relocates at exec time, or link every program against one
libc stored once in flash. It reconciles nine research streams under
`streams/`, each reviewed adversarially by Codex (`gpt-daybreak-blue-latest`,
high effort; reviews under `reviews/`) and resolved by the stream's own agent,
plus a naming pass by Grok. Measurements replay from `measure/` against the
build products of a completed RP2040 build. The rows here come from build
products dated 2026-10-24 in a checkout at commit 122ec634; the source commit
of those products is not recorded, so a replay rebuilds first.

The 144/160 KiB window and LARGE-epoch premises in this retained study describe
the measured revision. `../swap-exhaustion/review-04.md` supersedes the live
layout with a fixed 192 KiB window and a kernel-resident SwapRAM pool.

## Shared-library implementation contract

The [executable-tail object experiment](object-abi/README.md) supplies compiler,
linker, a.out conversion and QEMU witnesses for process-local object identity.
Its five application layouts and eleven calibrated rejection controls establish
that prerequisite. Full-libc sizing, versioned admission and runtime lifecycle
qualification remain separate implementation work.

The [export-closure checker](export-closure/README.md) resolves explicit API
roots through compiled archive providers. Its calibration rejects a selection
that drops a required interface merely because installed programs never call it.
The all-global screening receipt retains input-section costs and leaves public
ABI selection and final PIC sizing open.

The [calendar PIC envelope](export-closure/calendar-pic-envelope.md) records
the integrated 449-member compilation and 646-global link. The experimental
image occupies 62,396 flash bytes, exceeding the fixed reservation by 13,244
bytes. Hidden visibility supplies no flash savings. The measured size requires
explicit ABI closure and implementation reductions before image admission.

The [native-call veneer experiment](../native-call-veneers.md) checks a
12-byte Thumb encoding with eight bytes of temporary stack storage, canonical
flash pointer identity, and native/GNU assembler agreement. Automatic branch
redirection, stable slots, and final shared layout remain implementation work.

The accepted implementation keeps multicall executables and static native
output while adding an explicit shared-link mode. The initial libc reservation
is 48 KiB immediately after the 128 KiB kernel. Raw Dhara space becomes
1,488 KiB; raw swap remains 384 KiB. Production geometry and `flashimg -c`
determine logical capacity and image limits. The application base stays
`0x20000000` and the process window stays 192 KiB. A size-gate failure requires
rescoping rather than implicit partition growth. These are implementation
requirements; the shipped layout remains the static layout until qualification.

Public data imports require an object ABI as well as callable function slots.
Each executable reserves libc initialization data, BSS, GOT and alignment at
the tail of writable BSS after every application and multicall overlay extent.
The reservation contributes to ELF writable `PT_LOAD.p_memsz`, converted
`a_bss` and final end symbols. Changing `_end` alone cannot establish admission.
Generated executable-specific addresses resolve ordinary references and
initialized pointers to `errno`, `environ` and `_iob` at link time. Application
text remains immutable when swap reloads clean text from its executable.

Static startup sets the compiler-required PIC base in `r9`, copies initialization
data, relocates only libc-owned writable state and initializes libc-owned
current/minimum break cells before shared calls. One authoritative ABI
description generates function slots, mutable-object offsets, sizes, alignments
and layout identity. The link rejects conflicting definitions and accidental
interposition. Shared syscall assembly and ROM-float trampolines access the
same process-local objects and preserve descriptor alignment and provider
contracts. The candidate needs compiler/linker/runtime fixtures before ABI
acceptance; reserving `r9` alone cannot redirect public object references.

One versioned library requirement record spans cross ELF, raw a.out, packed
a.out and native output. Kernel exec admission validates metadata version, ABI
identity, required slots, mutable layout, aligned state containment in admitted
BSS, complete image/stack fit, truncation and arithmetic overflow before
replacing the process. Static formats remain accepted. Startup repeats essential
checks before its first shared call. Function slots stay stable within an ABI
version; a mutable-layout change requires an explicit compatibility decision
and rebuilding affected binaries. Reserved fields acquire meaning only through
documented format versions and flags.

Read-direction usercopy and MPU coverage extend to the exact libc flash
interval; write destinations remain restricted to writable process memory.
Native shared mode derives object imports and reservation from the same ABI
description and emits import stubs or veneers for SRAM-to-flash Thumb calls.
Unsupported imports fail explicitly. Kernel, libc and root images form one
matched release set; live library replacement remains prohibited. Recovery
documentation addresses interrupted flashing without asserting atomic UF2
installation.

Qualification exercises differing BSS sizes, zero-data applications, multicall
overlays, object address identity, initialized pointers, malformed descriptors,
raw/packed parity, heap boundaries, callbacks, signals, setjmp/longjmp, fork and
swap restoration. Matched clean builds measure each constrained surface
separately. Authorized board workloads cover startup, stdio and process
switching; each workload permits at most 10% regression with measurement
uncertainty reported. Host, ILP32, cross, Qemu-user, Renode and physical-board
evidence retain separate claims.

## Historical screening verdict

The following estimates retain the original study's artifact limitations.
The implementation contract above supersedes the claims that startup-only
relocation leaves exec admission unchanged and that native output remains
exclusively static. Historical savings supply candidate experiments rather
than available capacity at the refreshed implementation baseline.

1. Keep the boxes and add a shared libc whose text executes in place from a
   48 KB flash partition. Over 28 shipped executables that pass every gate,
   packed root use falls from 502 to 288 blocks (214 saved); the partition
   costs 28 blocks of root capacity (989 to 961 KB, `flashimg -c`), a net gain
   of 186 blocks against about 310 free today (309 by `fsutil --check` in
   `sys/arch/rp2040/doc/research/utilbox.md`, 314 by `df` on the board in
   `STORAGE.md`). Boxes still save 44 blocks over separate files once libc is
   shared (stream 01).
2. Position-independent executables with an exec-time relocating loader solve
   a placement problem this kernel does not have: one process is resident at a
   time and every image occupies the same window at 0x20000000 (`MPU.md`).
   FDPIC, the standard no-MMU shared-library ABI, is unavailable on Thumb-1
   (GCC `arm.cc`, "FDPIC mode is not supported in Thumb-1 mode").
3. Position independence survives in one narrow place: libc's own data. Built
   with an r9-relative GOT, libc state lives in ordinary break memory after the
   program's bss and crt0 relocates it once at startup, the scheme Tock's
   `libtock-c` runs on Cortex-M0. This leaves the kernel loader, exec admission
   and swap untouched, for about 2.3 KB of flash and about 13 percent more
   libc RAM per process. A fixed libc-data block at the window base, the
   alternative, needs changes to both loaders, the process-length invariant,
   and two-extent swap (stream 08).
4. Keep the name `box`. Neither proposed rename buys a functional gain, and
   `crunchpod` loses on lineage and collisions (section "Naming").

## What survived falsification, and what did not

| Claim | Outcome | Evidence |
| --- | --- | --- |
| Boxes exist because static libc is duplicated per file, which headers cannot fix | survives | 264,611 libc-attributed bytes across 30 executables; a header shares source, not linked code (stream 01) |
| Boxes are the only way to share libc on this system | falsified | a fixed-address libc alone matches the shipping boxes (138 against 145 blocks for five boxes' applets) |
| PIC plus exec-time relocation beats the current design | falsified for executables | the window fixes every load address; no relocation is needed for placement |
| A relocating step has no use here | falsified, with narrower scope | crt0-time relocation of libc-owned data remains useful (stream 08); the object ABI and versioned pre-exec admission still require loader changes |
| `--just-symbols` fails closed on symbol clashes | falsified | sh's `rename(int,int)` bound silently to libc's pathname `rename`; a calibrated gate now fails sh and passes tee |
| A whole-libc image buys on-board `cc` support | falsified | the board ld rejects Thumb calls beyond about 4 MiB without veneers (`usr.bin/ld/ld.c:592-645`); board-built programs keep static `libc.a` |
| libc flash strings work as syscall arguments | falsified | `baduaddr_span` (`machdep.c:920`) admits only the SRAM window; the kernel must admit the libc range for read-direction transfers |
| The LARGE SwapRAM epoch is MPU-safe | falsified, independent defect | a P_LARGE stack reaches 0x20024000-0x20028000, which `mpu_map` never grants; the board transcript in `MPU.md` faults there; retained LARGE runs predate the MPU (stream 07) |

## Relation to the earlier multicall composition study

`docs/research/dpucp-2024-multicall-composition.md` measured the shipping
boxes against their 71 primary applets linked separately (615 against 182
packed blocks). This study's static separate-file links reproduce that
ledger's standalone column exactly for the five boxes it relinked (box 134,
sysbox 68, textbox 156, utilbox 162, grepbox 18), so the two harnesses agree
on the baseline. The new columns are the shared-libc forms: 138 blocks for
those applets as separate files and 94 as boxes. That study's 192-member libc
closure covers the boxes and the shell; the 257-member union here adds the
standalone programs. Its composition gate requires strictly positive per-box
savings, which still hold once libc is shared (44 blocks across the five
boxes).

## Decisions the measurements settle

- Member set: the union image (257 members plus an allow-list, 32,448 bytes
  of text) fits 48 KB and costs 28 blocks; the whole archive (50,000 bytes)
  needs 64 KB, costs 39 blocks, exposes three more export collisions, and
  gains nothing for the native toolchain. Use the union with a size gate.
- Export namespace: sh (`rename`, `tmpnam`), ps (`uname`) and init
  (`shutdown`) define external names the C library reserves. Making each
  function `static` is correct today, independent of the shared libc.
- Upward references: `environ`, `__progname` and `_end` move into libc-owned
  cells set by crt0; `brk()` clamps against a libc-owned minimum break
  (`sbrk.c:44`).
- libc RAM: every process pays the image's whole libc state (5,420 bytes
  for the union, rounded to 5,632). Lazy allocation of `gr_mem`, crypt `KS`,
  regex `expbuf` and the 256-byte line buffers, and a const `_ctype_`, cut it
  by about 3 KB; the stdio, errno and environ ABI must be fixed first.

## Clever savings and safety items beyond libc

From the RTOS, microkernel and hardware streams (06, 07), ranked by payoff
over cost, each with its falsifier in the stream:

1. Repair the LARGE-epoch MPU gap with a region enabled only while the pool is
   evacuated, plus a combined MPU-and-LARGE board gate.
2. Guard the kernel stack: the retained workload left 220 untouched sentinel
   bytes of 2048, and the only check runs at syscall entry.
3. Read the XIP access and hit counters and the BUSCTRL counters before any
   cache decision; they cost no RAM.
4. Use the non-allocating XIP alias (0x11000000) for flash data reads, after
   counters and elapsed time show file and swap reads evict kernel text.
5. Record the panic PC in watchdog scratch3, which survives SYSRESETREQ.
6. Benchmark ROM memcpy against the kernel's C `bcopy` running from XIP.
7. A 32-byte no-access subregion as a user stack guard (32-63 window bytes).
8. A versioned header at the libc base, after the a.out DLL and NuttX
   userspace-header precedent, so exec or crt0 rejects a mismatched libc.

Not pursued without new measurement: the XIP cache as 16 KB of SRAM (kernel
text would run uncached), core1, DMA, interpolators, lazy MPU loading on
fault (ARMv6-M has no fault address register), FCSE and ARM domains, paging.

## Naming

Grok, asked to role-play a PicoCalc owner (`reviews/grok-naming-verdict.md`),
ranked `box` ahead of `crunch`, `crunchbox` and `crunchpod`, partly on
listing width. That argument does not survive the source: on a terminal `ls`
rounds the column width up to the next tab stop (`(width + 8) &~ 7`,
`bin/ls/ls.c:443-444`), so every name of 8 to 15 characters takes the same
16-column slot on the 53-column PicoCalc console (`LT_COLS`,
`sys/arch/rp2040/dev/lcdterm.h:35`); `adminbox` and `puzzlecrunchbox` lay out
identically. Grok's claim that `crunch` stops being accurate once libc leaves
the file fails too: crunchgen's defining property is merging programs with
argv[0] dispatch, which survives the shared libc.

What remains decides the question narrowly. `ps` shows the invoked link name,
so users rarely meet the family name and the typing cost applies only to
direct invocation. `crunch` names the BSD lineage (crunchgen, crunchide,
FreeBSD /rescue, OpenBSD instbin) where `box` names the busybox lineage; both
are recognized. `crunchbox` collides with Crunch.io's crunchBox and CrunchBang
tooling; `crunchpod` carries no BSD lineage and borrows Kubernetes' meaning. A
rename touches directories, Makefiles, the manifest, three gates and the docs
for no functional gain. Keep `box`; if lineage is wanted, `crunchbox` beats
`crunchpod`.

## Precedent: SVR3 static shared libraries

AT&T System V Release 3 shipped this design without position independence:
`libc_s` linked once at a fixed address, a numbered branch table with
reserved slot ranges so the table grows append-only, a hide-by-default export
policy, pointer-indirected references to program-owned data, a host stub
archive that attaches a dependency record (the library's pathname) to every
linked program, a kernel that validates that record at exec and caps
libraries per process, and `chkshlib` to check offline that a new library
keeps old programs working (stream 09, `spec` lines 5-8, 137, 376-377). SVR3
placed library data at a fixed address in an MMU-backed private region. Fixed
placement is possible here too, at the window base, which is the fixed-data
design of stream 08; the r9-relative GOT is chosen because it leaves the
loader and swapper unchanged, not because fixed placement is impossible.
SVR3's record names a pathname and carries no ABI version, so the header here
adds an explicit major version and slot count. SunOS 4 and OSF/1 add the
major/minor naming rule and a `mvalid`-style range check; Palm OS keeps a
per-library globals pointer beside its dispatch table. Inside this tree,
`lib/libc/arm/gen/rom_float_resolver.S` already binds user code to the boot
ROM's fixed-address float routines with a version floor and a window-bounded
cache cell; the board transcript in `MPU.md` (`fptest`, `FPTEST OK`, and the
"rom float divide" probe) shows it executing on this target.

The precedent settles the export question. A numbered, append-only branch
table lets shipped programs survive a libc rebuild and is the only form
user-built binaries can depend on, since they outlive any one image (stream
03); direct symbols freeze the address of every export a program
references, while hidden internals may still move. The table costs one slot
per export (a Thumb-1 slot needs a literal load and a branch, 8 bytes) and
one extra jump per call, both measured in roadmap 6 before the choice is
final.

## Roadmap

Each item lands as its own reviewable change that builds and bisects alone,
with the gate named beside it. Items 1 to 3 are independent of the shared
libc and land first.

1. MPU coverage of the LARGE bonus interval: a region enabled only while the
   pool is evacuated, disabled with readback before it reopens. Gate: a LARGE
   image with the map enabled, live region readback, unprivileged accesses
   across the bonus interval, and edge probes on every enabled span (`MPU.md`,
   "Known gap").
2. Export namespace: make sh's `rename` and `tmpnam`, ps's `uname` and init's
   `shutdown` static. Gate: the export-intersection check, calibrated on the
   unfixed sh object, in the root check targets.
3. Read-direction admission of the libc flash range in `copyin`,
   `copyinstr` and the writev source path, with `copyout`, readv targets and
   `physio` kept window-only (the `mvalid` model). Gate: a fault test that
   reads succeed and writes fail with EFAULT.
4. libc ABI: libc-owned `environ`, `__progname`, current and minimum break;
   `_iob`, `errno` and `environ` access defined for indirection; lazy
   allocation of large libc state behind first use (an open-call convention
   after Palm's per-library globals pointer); const `_ctype_`. The two
   static-state items stand alone under today's static links and are
   settled by measurement (section "Static libc state under static links"):
   `_ctype_` is const already (`6b76f490`); getgrent's 1,057-byte record
   is allocated on first use; crypt `KS`, regex `expbuf` and the 256-byte
   line buffers stay static, because no shipped program saves a window
   kilobyte or a swap block by moving them.
5. r9-relative libc data: libc built with `-fPIC -msingle-pic-base
   -mpic-register=r9 -mno-pic-data-is-text-relative`, programs with
   `-ffixed-r9`, crt0 placing libc data after bss and fixing up the GOT and
   data words, a clean exit when libc data does not fit, and a decision on the
   ROM float trampolines (rewrite r9-relative, or keep float printf out of
   libc behind a crt0-set hook). Gates: r9 checked at signal-handler and
   callback entry and exit; image plus libc data plus stack within MAXMEM.
6. `lib/libcx`: link script, a versioned header at the partition base (magic,
   major, minor, slot count, ABI hash), the branch table and export list with
   hide-by-default, the union member set plus an allow-list with libgcc leaf
   helpers private to each program, and a size gate; the 48 KB partition,
   `FLASH_FS_OFFSET`, UF2 assembly, and MPU region 4 with mputest probes.
   Gates: ABI hash agreement and rejection of a mutated hash, member closure,
   no residual libc.a members, a `chkshlib`-style check that the new table
   keeps every shipped program's slots, table size and per-call cost
   measured against direct symbols.
7. Switch shipped programs and boxes to the shared libc; the composition gate
   carries the frozen block and swap numbers and the veneer count.
8. Board evidence: coremark and a stdio microbenchmark inside an agreed
   bound, elapsed cycles with XIP access and hit counters, swap bytes per
   switch, exec latency, cold and warm cache, and an uncached 0x13 control.
   A missed bound falls back to the fixed-data design of stream 08.
9. Board ld import stubs: a generated stub archive at the slot addresses
   whose dependency and version record arrives by an undefined-symbol
   reference (the
   `mkshlib` host library), and thunk generation in the 2.11BSD `ld -Z` style
   for calls beyond Thumb BL range. Gate: an absolute-call fixture.
10. Kernel stack guard in a spare MPU region, gated on a board test that a
    stacking fault into the guard does not lock up and that the u-area
    exchange survives.
11. User stack guard as a 32-byte no-access subregion at the break, with a
    canary at the window floor checked on syscall return as the fallback
    (PC-Xinu `STKCHK`). This item and item 1 reprogram a region at run time,
    so each restates `MPU.md`'s programmed-once invariant with its readback,
    and each audits variable shifts in the span and size arithmetic.
12. Measurement instruments and small wins, each after its measurement: XIP
    and BUSCTRL counters, the non-allocating XIP alias for flash data reads,
    the panic PC in watchdog scratch3, ROM memcpy against the kernel's
    `bcopy`, per-program stack budgets from `-fstack-usage`.

The two counter instruments of item 12 are numbered last and are built
first. Item 8's board evidence reads `XIP_CTRL` `CTR_ACC` and `CTR_HIT`, and
so does every cache question the shared libc raises: libc text at
0x10020000 shares the 16 kB, two-way cache of 8-byte lines (datasheet
2.6.3.1) with kernel text, so the miss rate of a libc-heavy workload is the
first number that can confirm or overturn the verdict. The kernel alone
runs at a 99.7 percent hit rate, and uncached execution is 23 times slower
on a shell command (`../hardware-review-reconciliation.md`, "The cache
control"), so the bound of item 8 is a miss rate, read as `xip_acc` less
`xip_hit` in one command.
`../hardware-review-reconciliation.md` orders the counters, the
non-allocating alias and line alignment as its roadmap items 1 to 4 and
records the kernel sizes of `7a270276` (PICO flash image 95,248 bytes,
35,568 free; the 108,726 quoted in `FINDINGS.md` is a retained row of the
measured revision).

Bytes a process may execute stay immutable while it can run: Lites kept the
old emulator file mapped for running programs while a replacement installed
by pathname. Here the policy replaces the libc partition only by a whole-image
flash, because the UF2 already carries libc and root together and no process
survives a reflash.

`FINDINGS.md` maps every finding from every stream, review and pass to this
roadmap.

## Static libc state under static links

Every shipped program links static `libc.a`, so a member's data and bss
cost only the programs that link it, and the executable format decides
which of those bytes matter. `elf2aout` puts the non-writable load segment
(`.text` and `.rodata`, `lib/elf32-arm.ld:102`) in `a_text` and the
writable one in `a_data` and `a_bss` (`tools/elf2aout/elf2aout.c:468-487`).
Both loaders place text, data and bss in one extent at `__user_data_start`
(`sys/kern/exec_aout.c:150-153`, `sys/arch/rp2040/rp2040/exec_hsaout.c:172-174`,
`sys/kern/exec_subr.c:435`), so moving a table from `.data` to `.rodata`
frees no window byte. Text is not swapped: `p_tsize` records `a_text`
(`exec_aout.c:225`, `exec_hsaout.c:196`), swapout skips it
(`sys/kern/vm_swap.c:27-34,42-47,246`) and swapin re-reads it from the
executable (`vm_swap.c:171`, `exec_aout.c:89`), so a const table leaves the
swap image. Bss is swapped with data and the break (`vm_swap.c:196,246`):
the flash tier writes it raw in 1,024-byte blocks (`DEV_BSIZE`,
`sys/arch/rp2040/include/machparam.h:103,108`), while the PICO kernel's
16 KB SwapRAM tier (`sys/arch/rp2040/compile/PICO/Makefile:6-7`) runs
heatshrink at window 9 and lookahead 8 (`heatshrink_config.h:34,37`) twice
per swapout (`swapram.c:359-400,423`), and the tree's encoder turns 1,057
zero bytes into 12. malloc extends the arena to the next 1,024-byte address
(`lib/libc/gen/malloc.c:67,142`) and every shipped program links it, so a
bss cut moves the heap start, the window headroom and the flash-tier swap
image only when it crosses a kilobyte boundary.

The rows below come from the 37 shipped executables of a clean build of
`58ec71f1` (arm-none-eabi-gcc 16.2.0), each relinked from its own objects
with a link map; every relink reproduces the shipped a.out byte for byte.
"Saving" is the upper bound with no text added: the kilobyte of heap start
and the flash swap block a run gains when it never calls the member.

| Candidate | Member bss | Shipped programs linking it | Saving | Disposition |
| --- | --- | --- | --- | --- |
| `_ctype_` | none; 257 bytes of `.rodata` | 19 | in `a_text` of all 19, outside swap | const since `6b76f490` |
| getgrent `gr_mem` and `line` | 1,077 (800 + 257) | box, find, login, su, tar, utilbox | 1 KiB and 1 block in each | allocated on first use |
| crypt `KS` | 1,130 (768) | login, passwd | none; both call `crypt()` on the main path | static |
| regex `expbuf` | 585 (512) | utilbox (`more`) | none; 66,318 to 65,806 bytes stays in one kilobyte | static |
| getpwent `line` | 344 (256) | adminbox, box, find, fsck, login, passwd, ps, su, tar, utilbox | find, fsck and su; a 256-byte cut crosses a kilobyte in 3 of 10 layouts, and su reads the file on its main path | static |
| getttyent `line` | 288 (256) | init, login | init, which reads `/etc/ttys` at every start | static |
| fstab `line` | 288 (256) | fsck, sysbox | fsck, which reads `/etc/fstab` on its main path; none in sysbox | static |
| dirname `dname` | 256 | textbox | none | static |

getgrent allocates one record holding the 200-pointer member vector and the
257-byte line on its first call and keeps it for the process lifetime,
because `getgrgid()` and `getgrnam()` return an entry after `endgrent()`.
The member's bss falls from 1,077 to 24 bytes and its text grows from 228 to
252. In the six linking programs bss falls by 1,056 bytes, text grows by 24,
packed root blocks are unchanged, and the flash-tier swap image falls by one
block (box 15 to 14, find 11 to 10, login 4 to 3, su 4 to 3, tar 5 to 4,
utilbox 12 to 11) in every run that does not read the group file: every box
applet but `chgrp`, `chown` and `ls`, every utilbox applet but `id`, and
find without `-group`, `-nogroup` or `-ls`. A run that does read it places the
record in the arena with one header word, 32 bytes beyond the static
layout, which adds a kilobyte only when its break lands within 32 bytes
below a boundary. A failed allocation returns NULL with `ENOMEM`;
`initgroups()` then sets the primary group alone, as it does when
`/etc/group` cannot be opened. `check-libc-getgrent` pins first use,
repeated use, the failure path and the bss bound against four mutants.
The SwapRAM tier saves about 12 bytes per swapout for the same runs; how
often each tier takes an image is a board measurement this note does not
carry.

## Evidence classes and limits

Everything above is build evidence and host link evidence: links, sizes,
packed blocks, symbol tables, object-level PIC sizes, and source reading. No
runnable shared-libc image exists, no board ran any of it, XIP execution speed
and swap growth are unmeasured, and the PIC data design's run-time cost rests
on object sizes alone. The LARGE-epoch defect rests on source plus one
retained board transcript of the denied address, not on a LARGE run with the
MPU enabled.

The [full-library state experiments](full-library-state.md) record LTO sizing,
process-state relocation controls, installed-header discovery and compact
callable-slot execution. The fixed reservation remains a failed size gate.
