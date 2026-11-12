# RP2040 compiler, debug and optimization survey

The RP2040 PICO kernel uses a 128 KiB flash partition shared with boot2;
the root filesystem and raw swap begin at fixed flash offsets. A compiler
setting that exceeds the linker region cannot produce a valid image for that
layout. Debug information belongs in retained host ELF files; only loadable
sections occupy flash. Kernel and userland symbol retention are separate
problems: kernel builds carry `-g` and retain `unix.elf`, whereas many
userland recipes convert an intermediate ELF to a.out and remove that ELF.
A complete userland debug-symbol policy remains open.

A separate `bin/echo` pilot at `f3b2d0482af51b98b9243e7a3d4b3d7e5ef168b6`
used `bmake -k`, `COPTS="-Os -g -fno-common"` and the existing
`C17_CAPTURE_DIR` recorder. The recorder captured the intermediate ELF before
the recipe removed it. Its link output and `elf2aout` input both had SHA-256
`d184dead7c9ebab1a457f64a0417dc4f6dd79580fb9d1e1054d90a8815b2b4a4`;
the converted a.out had SHA-256
`b14816486071f71b40bae6a0742fb77b4edbd6a91208981dd369116f779e0614`.
`arm-none-eabi-readelf -S` found `.debug_info`, `.debug_line` and `.symtab`
in the captured ELF. The pilot worktree and capture were unavailable at a
later inventory, so these values are observations without retained artifacts.
The pilot compiles `echo.c` with debug information but
links an existing libc archive, so it does not establish debug coverage for
libc or other programs. A clean captured world build must establish that
coverage and bind every installed executable to its retained ELF.

A fresh `bmake -k MACHINE=rp2040 'COPTS=-Os -g -fno-common' build` at
the same revision reached userland but failed linking `games/warp`: the
GCC driver selected newlib's `libg.a` for its implicit debug-library pass,
and newlib's system-call references were unresolved. A retry with an explicit
`-lc` after warp's `-ltermcap -lm` linked successfully. The retained link map
lists the project libc and loads `libg.a` without selecting any `libg.a`
member. The successful ELF has SHA-256
`378b4a26cbcf4de96c09ed4d820ecb1a99022b88e3a55286b8aec1f1bc6bbc28`.
The failed build and focused retry are recorded by the `debug-world-build`
and `warp-debug-libc-retry` logged gates. A subsequent full retry is described
below.

The capture also shows that caller-supplied `COPTS` does not govern every
recipe: native a.out library Makefiles replace `CFLAGS` and omit `-g`, some
assembly commands omit `-g`, and some C recipes append a later optimization
option. Debug coverage and effective optimization must therefore be checked
per executed command and per shipped artifact, not inferred from `COPTS`.

The resumed `debug-world-build-final` completed with `-Os -g` and the explicit
warp libc link. Across the three retained capture directories for the clean
attempt and retries, successful recorded compilation invocations comprised
3,697 C commands with `-g`, 82 C commands without it and 405 assembly commands
without it. Counts are invocations, including repeats, rather than a count of
shipped files. Missing C debug flags occurred in native a.out libraries and
selected programs; recorded assembly omissions covered libc and kernel sources.
The last effective optimization flag on recorded C invocations was `-Os` for
3,631, `-O` for 109, `-O2` for 28 and `-O0` for 11. The capture excludes
uncaptured host tools and cannot alone prove debug coverage for reused objects
or every installed executable. A clean end-to-end symbol-to-image inventory
remains open.

For one native curses object, identical `cr_put.c` source compiled with and
without `-g` produced 12,512 and 3,668 file bytes, respectively, with the same
1,468 text and 24 BSS bytes. The extracted `.text` sections were byte-equal;
the debug object contained `.debug_info` and `.debug_line`. Installing debug
objects in native archives can therefore increase root storage even when
loadable code stays the same. Retaining matching host-side debug objects while
shipping compact archives is a candidate design, not yet an implemented or
validated policy. The `native-curses-debug-object-retry` gate and object
artifacts retain this single-file comparison.

The native libc route has a further assembler constraint. With its existing
`-B` selection of the tree's Thumb assembler, compiling `atoi.c` at `-Os -g`
failed at the emitted `.loc` directive with `bad instruction`; the otherwise
matching `-Os` control compiled successfully. The captured commands and
results are the `native-libc-debug-object` and `native-libc-plain-object`
gates. The assembler recognizes selected CFI directives but does not currently
recognize `.loc`; skipping debug directives would not retain their information.
A debug companion assembled by the standard toolchain needs a validated
source, code and relocation binding to the shipped native object.
For `atoi.c`, a single standard-assembler `-g` companion and the tree
assembler's ordinary native object both contained 72 text bytes, and those
bytes matched exactly. The companion also contains debug information. This
single object's text has no relocations, so the result supports investigating
the companion design but does not
establish matching relocation semantics, final linked addresses, or debug
coverage for other native objects.

A `-fdebug-prefix-map` candidate passed focused C-object controls for the
kernel and two userland routes, but a subsequent `bmake -k` logged build
failed in the native a.out libc route. GCC forwarded `--debug-prefix-map` to
the tree's Thumb assembler selected by `-B`; the assembler treated the mapping
operand as another input file. The failed full-build receipt is
`rp2040-build-local-20261109T132627Z-3499023-3c3f384d`.

The mapping is now the default. `usr.bin/as` consumes the forwarded operand
(its `thumb-options.sh` test fails the previous assembler on both spellings),
and `share/mk/sys.mk` and `sys/arch/rp2040/conf/Makefile.rp2040` set
`DEBUG_PATH_MAP` to `-fdebug-prefix-map=${TOPSRC}=.` for every RP2040 C and
`.S` compile. The rebuilt assembler first had to reach `tools/bin/rp2040`:
`tools/aoututils/as` compiles a shim that includes `usr.bin/as/as-thumb.c`,
and the shim's date never changed, so the installed assembler predated the
fix until `tools/aoututils/Makefile.inc` recorded each object's included
files. A logged build on 2026-11-10 then assembled 116 `.S` files through
the `-B` route with the mapping and completed (receipt
`rp2040-build-local`, 131.5 s, an incremental build after the flag change).
`check-debug-paths` (`tools/check_debug_paths.py`) gates the claim: for
`bin/echo`, `usr.bin/smlrc` and the PICO kernel a `-g` probe carries DWARF
without the checkout path while its unmapped control carries it, and the
stripped objects match; for `lib/libc_aout/libc` a `.S` probe assembles
mapped, unmapped and with `-g` into one byte-identical a.out object, and a
stand-in assembler that reads the operand as a second source fails the
mapped probe. The a.out assembler writes no debug sections, so the mapping
there establishes acceptance of the operand, not path rewriting.

## Measured PICO build matrix

On 2026-11-09 a clean PICO sweep used the same staged source snapshot based on
`af2ca5997270b56fb976f3d71b2aa8dcff7aeb44`, with SHA-256
`1bbff5645b4e641869bbb447c1e51bf30c9d4c5752893d3926275eb6121c8d3c`,
for both GCC versions. Each row cleaned the PICO build directory and ran
`bmake -k` with `-g` and the kernel's warning and target flags. Text, data
and BSS are `size` values in bytes; an overflow is a failed FLASH link.

| Optimization | GCC 16.2.0 text/data/BSS | Arm GNU GCC 13.2.1 text/data/BSS |
| --- | --- | --- |
| `-O0` | overflow by 21,496 | overflow by 21,856 |
| `-O` | 108,595 / 256 / 39,832 | 110,291 / 256 / 39,832 |
| `-O1` | 108,595 / 256 / 39,832 | 110,291 / 256 / 39,832 |
| `-O2` | 111,747 / 256 / 39,864 | 111,971 / 256 / 39,864 |
| `-O3` | overflow by 5,540 | overflow by 4,644 |
| `-Og` | 107,815 / 256 / 39,832 | 109,079 / 256 / 39,832 |
| `-Os` | 95,451 / 256 / 39,832 | 96,027 / 256 / 39,832 |
| `-Oz` | 95,451 / 256 / 39,832 | 96,027 / 256 / 39,832 |

Equal section sizes do not by themselves establish equal instructions or
execution. The clean build output and binaries are under
`.build-logs/optimization-gcc16-clang-matrix` and
`.build-logs/optimization-arm13-matrix`. The compiler setting does not
replace the explicit `-Os` rule for `kern_time.c`.

A matched GCC 16.2 object comparison accounted for 83 PICO objects. The sum
of object text sizes was 57,215 bytes larger at `-O0` than at `-Os`; the
largest individual differences were `ufs_namei.o` 3,815, `usb.o` 3,335,
`journal.o` 3,094, `ufs_syscalls.o` 2,784 and `tty.o` 2,579 bytes. Object
sizes include allocatable read-only data and do not predict independent
linked-image savings. The flash failure therefore needs multiple measured
source or layout improvements, rather than a single identified oversized
function.

At GCC 16.2 `-O0`, adding `-fomit-frame-pointer` reduced the overflow to
14,432 bytes. Adding function/data sections and linker collection reduced the
overflow further to 10,624 bytes. Both remain failed links; frame-pointer
omission changes unwinding properties and collection requires a complete
indirect-root audit before production use. An independent source-factoring
trial linked at unmodified `-O3` with 2,096 bytes of flash headroom, using
several explicit no-inline constraints and a factored random sampler. The
same trial increased `-Os` text by 8 bytes and increased the `-O0` overflow
to 21,520 bytes. The trial has not established runtime or debugging tradeoffs
and is not the production solution.

The initial clean, isolated PICO builds used commit
`f3b2d0482af51b98b9243e7a3d4b3d7e5ef168b6`, ARM GCC 16.2.0, `-g`,
`-Wall -Wextra -Werror`, GNU17, `-mcpu=cortex-m0plus`, `-mthumb`, the
production freestanding and ABI flags, and `bmake -k`. Each optimization level
used a separate worktree. `kern_time.c` retained the explicit `-Os` rule.
The table reports the `arm-none-eabi-size` text field, including boot2 and
other executable sections; the field is not the size of `.text` alone.

| GCC kernel options | Result | Text bytes | Data bytes | BSS bytes |
| --- | --- | ---: | ---: | ---: |
| `-Os` | link succeeds | 95,676 | 256 | 39,848 |
| `-O` | link succeeds | 108,704 | 256 | 39,848 |
| `-O1` | link succeeds | 108,704 | 256 | 39,848 |
| `-Og` | link succeeds | 107,940 | 256 | 39,848 |
| `-O0` | two clobber warnings and fault-handler literal-pool range error; after experimental repairs, link overflows FLASH by 21,716 bytes | unavailable | unavailable | unavailable |
| `-O2` | capacity-index warnings; after experimental repairs, link succeeds | 111,872 | 256 | 39,880 |
| `-O3` | capacity-index warnings; after experimental repairs, link overflows FLASH by 8,544 bytes | unavailable | unavailable | unavailable |

The `-O2` and `-O3` retries included changes to the fault-handler literal pool,
two checkpoint locals, capacity bounds and the readlink error path; their
results therefore differ in source as well as optimization from the clean
baseline rows. The current-source `-O0` retry at commit
`1b2cdb1e768b6fe96c5f9280c44b36204629d6e1` with those repairs overflowed by
22,684 bytes. No overflowed image was flashed. GCC's
`-Q --help=optimizers` output was byte-identical for `-O` and `-O1`.
With the later candidate formatter and warning fixes on the same current-source
base, fresh GCC 16.2 builds compiled but `-O0` and `-O3` exceeded FLASH by
22,816 and 6,676 bytes, respectively.

Matched `-Os` and `-Oz` PICO builds of that candidate each linked with
96,356 text, 256 data and 39,848 BSS bytes. Both load images contained 96,612
bytes. Five differing bytes corresponded to the generated build number and
time, and GCC's `-Q --help=optimizers` outputs matched. The result is limited
to this source, compiler and build configuration; it does not establish equal
execution time or equal optimization behavior for other programs. The
`pico-os-size-candidate` and `pico-oz-size-candidate` gates retain the logs.
Arm GNU 13.2.1 produced 96,831 text, 256 data and 39,848 BSS bytes at both
settings on the same candidate with the sanitized version generator. Its
97,088-byte load images differed at five build-number and time bytes, and its
optimizer-help outputs matched. The generator change means the Arm GNU 13.2.1
and GCC 16.2.0 byte totals above do not form a controlled compiler-to-compiler
comparison. A GCC 16.2.0 `-Os` replay with the same sanitized generator reported
96,299 text, 256 data and 39,848 BSS bytes, with a 96,556-byte load image.
The matched Arm GNU 13.2.1 result is 532 text and load-image bytes larger;
build-number and timestamp values differ but occupy the same number of bytes.
These are link-size measurements, not speed or functional-equivalence results.
The `arm13-pico-os-candidate`, `arm13-pico-oz` and
`arm16-pico-os-sanitized` gates retain the commands and output.

Two GCC 16.2.0 `-Os` one-flag trials used the same candidate and sanitized
generator. Disabling small-function inlining linked at 96,235 text bytes,
64 bytes below the 96,299-byte control; disabling inlining of functions
called once linked at 96,333 text bytes, 34 bytes above it. Both retained
256 data and 39,848 BSS bytes. The `pico-os-no-small-inline` and
`pico-os-no-once-inline` gates retain the results. The 64-byte reduction is
only a link-size observation: changed runtime, stack use and target behavior
remain unmeasured, so neither flag is promoted to the production build.

A four-way `-O0` experiment used the same current-source candidate and GCC
16.2.0. Each link failed the fixed FLASH bound:

| Compiler sections | Linker collection | FLASH overflow |
| --- | --- | ---: |
| ordinary | off | 22,816 bytes |
| function and data sections | off | 22,928 bytes |
| ordinary | on | 22,216 bytes |
| function and data sections | on | 18,776 bytes |

Splitting alone increased overflow by 112 bytes; collection with split
sections reduced it by 4,152 bytes relative to splitting alone. The linker
reports discarded functions, including `swapram_out`; retained vectors,
indirect references, debugger-visible symbols and runtime behavior require an
explicit root audit and tests before section collection can become a
production setting. The experiments produced no bootable image and no image
was flashed. Commands, link maps, discarded sections and failures are retained
in the `candidate-o0`, `pico-o0-split-no-gc`, `pico-o0-unsplit-gc` and
`pico-o0-section-gc` logged gates.

Replaying the PICO kernel on `af2ca5997270b56fb976f3d71b2aa8dcff7aeb44`
plus the integration patch compiled through the warning and assembler stages,
then exceeded FLASH by 21,496 bytes at `-O0` and 5,540 bytes at `-O3`.
The source snapshot patch used for both builds has SHA-256
`eaf577cadd2baa0a589e546fa3f9d2604fe96758c761aa01fb2aa3df57e28d52`;
the `integrated-pico-o0` and `integrated-pico-o3` gates retain the commands
and output. The source and version generator differ from the earlier matrix,
so the two sets do not isolate an optimization-level effect.

The same integrated `-O3` source with function/data sections and linker
garbage collection linked a 130,076-byte load image, leaving 996 bytes in the
131,072-byte kernel partition. The calibrated `check_kernel_headroom.py`
rejects the image because its 996-byte margin is 28 bytes below the required
1,024-byte margin. The discarded-section map requires a root audit and the
image has not passed runtime tests. With GNU ld 2.47.20260726,
`--print-gc-sections` combined with `--fatal-warnings` returned failure even
for a two-function calibration input; the successful kernel link kept fatal
warnings and recorded discarded sections in its map without the print option.
The `integrated-pico-o3-section-gc` and `integrated-pico-o3-gc-without-print`
gates retain both attempts.

Adding `-fno-inline-small-functions` to the integrated `-O3` split-section
and garbage-collection experiment linked 112,355 text, 256 data and 39,840
BSS bytes. Its 112,612-byte load image left 18,460 bytes of FLASH headroom and
passed the 1,024-byte headroom check. Rebuilding the unadjusted `-O3` control
reproduced 129,819 text bytes; the optimizer-help comparison changed only the
small-function-inlining setting. The linked `prf` symbol was 6,494 bytes in
the control and 2,428 bytes in the adjusted image. Symbol splitting, cloning
and relocation mean that individual symbol deltas do not by themselves
explain the full 17,464-byte text difference. Both builds still need a
discarded-section root audit, functional tests and physical timing before any
setting can be accepted. The `integrated-pico-o3-gc-no-small-inline` and
`integrated-pico-o3-gc-repeat` gates retain the results.
The linked-symbol comparison suggests that repeated inlining of the static
`putchar` helper into `prf` contributes to `-O3` code growth: the smaller
image has a 92-byte `putchar` symbol while the control has none. A targeted
no-inline experiment predicts a `prf` reduction of at least 2,000 bytes at
`-O3`. A smaller reduction falsifies that threshold; either outcome needs
paired `-Os` size, formatter contracts and runtime timing before adoption.
Constraining that helper alone reduced `prf` from 6,494 to 2,456 bytes at
`-O3`, and reduced the whole-kernel FLASH overflow from 5,540 to 1,412 bytes.
At `-Os`, a matched control and trial linked at 95,453 and 95,421 text bytes,
respectively, with unchanged data and BSS totals. The source-level trial meets
the predicted size threshold but remains experimental: formatter contracts,
target behavior and timing have not been run for that constraint. The
`formatter-noinline-pico-o3`, `formatter-noinline-pico-os` and
`formatter-control-pico-os` gates retain the results.
With the same inlining adjustment and split sections but without collection,
the PICO kernel linked at 116,115 text bytes. Without either split sections or
collection, it linked at 115,635 text, 256 data and 39,864 BSS bytes; its
115,892-byte load image passed the headroom check with 15,180 bytes available.
The `integrated-pico-o3-no-small-split-no-gc` and
`integrated-pico-o3-no-small-inline` gates retain these results. The unsplit
variant avoids dependence on linker collection but still needs runtime and
performance validation.
An initial binary audit found `.boot2`, `g_pfnVectors`, `Reset_Handler`,
`Default_Handler`, `.ramfunc` and the kernel `nlist` table in the smaller ELF.
All 35 address relocations from PICO `sysctl.o`'s `.rodata.nlist` resolved to
defined symbols in that ELF. The audit covers those named roots and does not
establish all indirect entry points or their runtime behavior. Its retained
`check-gc-nlist-roots.py` probe rejected a copy of the ELF with the `boottime`
symbol removed. The syscall-handler table contains 163 relocations naming 108
distinct targets; all 108 names are defined in the ELF. A separate retained
probe rejected an ELF copy with the `read` symbol removed. These checks bind
symbol names, not the correctness of the function bodies or table values.

In a separate object probe with the same source, target flags and split
sections, `arm-none-eabi-nm -S` measured `namei_at` at 5,392 bytes with
`-O0` and 2,928 bytes with `-Os`. Those are object-symbol sizes, not linked
image savings or proof that a source change preserves pathname semantics.
The 2,464-byte gap identifies a worthwhile disassembly and control-flow
investigation; pathname, mount, symlink and failure-path tests must govern
any restructuring.
The `compiler-gc-o0` worktree retains the replay script and output as
`.build-logs/namei-optimization-probe.py` and its adjacent `.log`.

Arm GNU 13.2.rel1, GCC 13.2.1, was downloaded from Arm and verified against
Arm's published archive SHA-256
`6cd1bbc1d9ae57312bcd169ae283153a9572bd6a8e4eeae2fedfbc33b115fdbb`.
The same clean PICO source and flags produced 96,184 text bytes at `-Os`
and 110,440 at both `-O` and `-O1`; data and BSS remained 256 and 39,848
bytes. The initial `-O0`, `-O2` and `-O3` builds reported the same
clobber, literal-pool and capacity-index diagnostic classes as GCC 16.2.
After the same experimental repairs, `-O2` linked at 112,288 text, 256
data and 39,880 BSS bytes; `-O0` and `-O3` overflowed FLASH by 22,060 and
7,552 bytes. The repaired rows are source variants of the clean baseline.
These cross-build results do not establish execution on hardware.

The `-Os` PICO ELF measured 889,512 host file bytes, including 368,212 bytes
of `.debug_info` and 144,494 bytes of `.debug_line`; those sections have
address zero and are absent from the raw flash load image. `size`'s BSS field
includes multiple no-load sections, so it is not a runtime peak or a free-RAM
measurement. Linked sections, process-window headroom, stack high-water,
heap peak, root blocks, flash traffic and erase wear need separate receipts.

## Physical-board comparison

On one bare RP2040 board at 125 MHz, the `-Os`, `-O`, `-Og` and `-O2` kernels
from the same source revision ran the same checked test root, whose packed executable
expanded byte-for-byte to the raw executable. Each timed arm ran ten `seq`
executions and the order alternated within each pair. The `-Os` campaign used
24 pairs; `-O`, `-Og` and `-O2` each used 12. The campaigns were sequential, not interleaved across
kernel images. Each arm returned success.

| Kernel | Packed mean seconds per exec | Raw mean seconds per exec | Paired premium ms per exec | 95% paired t interval ms |
| --- | ---: | ---: | ---: | ---: |
| `-Os` | 1.3399 | 1.3002 | 39.75 | 22.47 to 57.03 |
| `-O` | 1.1205 | 1.0572 | 63.33 | 44.24 to 82.43 |
| `-Og` | 1.2062 | 1.1514 | 54.75 | 35.48 to 74.02 |
| `-O2`, experimental repairs | 1.1379 | 1.0863 | 51.58 | 37.06 to 66.11 |

The t intervals assume independent paired differences. The `-O` means are
16.4% and 18.7% lower than the respective `-Os` means; temporal drift and
filesystem state remain possible contributors. The comparison does not
isolate decoding, CPU time or the optimization setting. The `-O2` source
included the five repairs listed above, so its comparison also has a source
difference. The loop times in seconds, in pair order, were:

    -Os packed 13.23 13.45 13.25 13.57 13.24 13.40 13.24 13.28 13.31 13.22 13.29 13.28
    -Os raw    12.71 13.96 12.73 12.96 12.89 13.56 12.79 12.99 12.94 12.94 13.37 12.75
    -Os packed 14.22 13.24 13.47 13.44 13.66 13.86 13.25 13.49 13.39 13.22 13.29 13.29
    -Os raw    12.97 12.75 12.72 12.77 12.88 12.77 12.75 13.85 12.80 12.96 12.93 13.30
    -O  packed 11.31 11.27 10.98 11.14 10.90 11.61 11.05 10.97 11.65 11.64 10.95 10.99
    -O  raw    10.62 10.48 10.71 10.54 10.54 10.58 10.66 10.50 10.57 10.55 10.46 10.65
    -Og packed 12.36 12.57 11.90 11.94 11.92 11.95 12.15 12.11 11.94 11.86 11.91 12.13
    -Og raw    11.45 11.35 11.50 11.67 11.62 11.46 11.42 11.53 11.46 11.65 11.65 11.41
    -O2 packed 11.42 11.28 11.18 11.71 11.29 11.33 11.42 11.46 11.56 11.35 11.22 11.33
    -O2 raw    10.73 10.88 10.75 10.83 10.83 10.81 11.33 10.76 10.75 10.90 10.99 10.80

The `-Os` kernel, `-O` kernel, and shared test-root UF2 SHA-256 values were
`487fa565aa9032ec0cca1682851cd7d8b708ca1621b7ec6517c21abf39341b30`,
`c7c26d7a1a6ebb1298db78b46c1a6e2e8bcc7ee8c07b747e6a154490fb445545`, and
`1326a4a39977f915ee675c54996eebf87bf931a9845def10d34aa38eccf95cce`,
respectively. The `-Og` kernel UF2 SHA-256 was
`f5dc863175c8b33d1a2ba64040bd10b2444d69e45b35885799ddcb6dedd608d1`.
The `-Og` campaign used 12 pairs. The board was restored to the plain root
and reached `login:`.
The `-O2` kernel UF2 SHA-256 was
`1a99a231e0fa6d075a5e6c192427a52b8961fe9163cc70fcd29b3475218a0155`.

### Board comparison of `-Os` and adjusted `-O3`

On 2026-11-09 one bare RP2040 board ran the integrated source based on
`af2ca5997270b56fb976f3d71b2aa8dcff7aeb44`. Both kernels used GCC 16.2.0,
the same warning and ABI flags, and the PICO configuration with a configured
125 MHz clock. The `-Os` kernel used `COPTS=-Os`; the other used
`COPTS="-O3 -fno-inline-small-functions"`. The separate `kern_time.c` rule
kept `-Os` in both. The source snapshot patch for the `-O3` build is identified
above; the 21 changed kernel, include and build source files were compared
between worktrees and matched. The two images also carry distinct generated
build numbers and times.

The full-profile test root added `/usr/rawbin/seq` as a raw `textbox` copy.
Filesystem checking reported 196 free blocks; extracted packed `textbox`
expanded byte-for-byte to the raw copy, both with SHA-256
`3b5bf95030390a1b510cbd50e90e30843b1057f76a350c8080b9d8a5ccbe14f6`.
The `-Os` kernel UF2, adjusted `-O3` kernel UF2 and shared test-root UF2 had
SHA-256 values `5f52abaf523ee19159ff8f640e838fad5ed8939600e13402b98cb599e4c48c9b`,
`8b7906ea46c83938797d954de5230da9a967cc1d50079806b29f085f109bf1b0`, and
`cea48e6ca78919976cd90aa1ba4c92c43680116aa29ba599d4c1e031d2be9362`.
The flash wrapper selected `picotool load --verify`; each block checked the
reported kernel version and a successful `seq 1` through both executable
paths. Each arm timed ten `seq` executions, required a zero exit marker, and
alternated packed-first and raw-first within each pair. The block order was
`-Os`, `-O3`, `-O3`, `-Os`: each block had six pairs, and the two middle blocks
shared a boot.

| Kernel | Packed mean seconds per exec | Raw mean seconds per exec | Paired premium ms per exec | 95% paired t interval ms |
| --- | ---: | ---: | ---: | ---: |
| `-Os` | 1.38233 | 1.29042 | 91.92 | 69.19 to 114.65 |
| `-O3 -fno-inline-small-functions` | 1.13167 | 1.07450 | 57.17 | 46.65 to 67.68 |

The intervals use twelve pair differences per kernel and assume independent
pair differences. The `-O3` means are 18.13% lower for packed and 16.73%
lower for raw execution relative to `-Os`. The bracketed runs reduce a simple
one-direction ordering effect, but three boots and shared filesystem state
still confound the optimization comparison. The measurements include shell,
fork, wait, swap and filesystem activity; they do not isolate decoder or CPU
time and do not establish performance for other workloads. Block means in
seconds per exec were 1.37883/1.28383 and 1.38583/1.29700 for the first and
last `-Os` blocks, and 1.13117/1.07133 and 1.13217/1.07767 for the two
`-O3` blocks, with packed first in each pair of numbers.

The board's elapsed times in seconds for each ten-exec arm, in pair order,
were:

    block 1 -Os packed 13.76 13.51 13.81 14.03 13.53 14.09
    block 1 -Os raw    12.90 12.76 12.84 12.92 12.79 12.82
    block 2 -O3 packed 11.16 11.31 11.32 11.30 11.31 11.47
    block 2 -O3 raw    10.88 10.40 10.66 10.67 10.76 10.91
    block 3 -O3 packed 11.28 11.24 11.32 11.33 11.45 11.31
    block 3 -O3 raw    10.86 10.57 10.77 10.74 10.77 10.95
    block 4 -Os packed 14.35 13.94 13.56 14.21 13.56 13.53
    block 4 -Os raw    13.16 12.91 12.82 12.68 12.83 13.42

The retained plain kernel and root were loaded with verification after the
campaign. The first recovery login check timed out despite USB enumeration; a
subsequent clean reboot reached `login:`. An earlier attempt collected no
timings because it checked for the console before re-enumeration and its first
recovery attempt requested BOOTSEL before the interface returned; a verified
retry reached `login:`. The corrected runner waits for the device before
timing and recovery. Normalized results and flash logs are retained under
`.build-logs/packed-exec-optimization-board`.

## Compilers, flags and failure scope

The host compiler is x86-64 GCC 16.2.1 and rejects ARM Thumb options. The
installed cross compiler is ARM GCC 16.2.0; the separately downloaded Arm
GNU compiler is GCC 13.2.1. Clang 23.1.4 accepts
`--target=arm-none-eabi` with the Cortex-M0+ options. The Pico SDK resolver
found no complete SDK in its configured locations and no distinct Pico SDK
compiler was established. Clang's reported bare-metal runtime archive was
absent, so the initial Clang survey explicitly selected ARM GCC's ARMv6-M
`libgcc.a`; full Clang link and ABI compatibility remain unproven.

The clean `-Os` Clang PICO sweep reported 107 errors across 22 files at the
older revision, including 98 deprecated non-prototype diagnostics; the
current-source sweep after selected experimental fixes reported 105 errors
across 21 files, including 97 such diagnostics. Clang stopped reporting after
its per-translation-unit error limit in at least `tty.c`, so the counts are
lower bounds on emitted diagnostics, not a complete inventory. The latter includes ARM
assembly syntax incompatibilities and the old kernel formatter's manual
variadic walk. These counts are scoped to each PICO `-k` build and are not a
count of independent defects. Full Clang builds at all levels remain open.

A fresh `1b2cdb1e768b6fe96c5f9280c44b36204629d6e1` PICO `-Os` sweep with
`bmake -k`, Clang 23.1.4 and `-ferror-limit=0` emitted 118 error lines in
22 source files: 109 deprecated non-prototype diagnostics, five incompatible
pointer types, one pointer-sign diagnostic, one possibly uninitialized value,
and two Thumb assembler immediate errors. The first sweep lacked its generated
`tools/bin/rp2040/config`. After a successful host-tools build, the retry
emitted the same counts without that missing-tool failure. Neither sweep
establishes a complete build or enumerates failures hidden behind failed
prerequisites. The original worktree and those two logs were unavailable at
the later inventory, so the counts are observations without retained logs.

A new sweep of the integrated candidate based on `af2ca599` built host tools,
then invoked the PICO `all` target with `bmake -k`, Clang 23.1.4,
`-ferror-limit=0`, and `-Os`. The sweep emitted 105 deprecated non-prototype
errors and two Thumb assembler immediate errors across 20 source files; it
failed before linking. The compiler selected ARM GCC 16.2's ARMv6-M libgcc
archive, whose compatibility with a complete Clang kernel remains untested.
The receipt and output are in the retained `clang-current-os-kernel-keep-going`
evidence directory. Failed prerequisites can hide later diagnostics.
One diagnostic is the indirect call through the unprototyped routine pointer
in `ucall()`, whose historical interface permits autoconfiguration callbacks.
Changing that pointer type requires an inventory of actual callback signatures
and the user/kernel ABI; a warning suppression would not establish type safety.

On that same snapshot, clean Clang 23.1.4 PICO sweeps at `-O0`, `-O`, `-O1`,
`-O2`, `-O3`, `-Og`, `-Os` and `-Oz` each emitted 105 deprecated
non-prototype diagnostics and two Thumb assembler immediate diagnostics.
Each keep-going sweep stopped short of linking. A source trial changed 18
old-style definitions to typed parameter lists and used PC-relative addresses
in the copied process-1 entry code. GCC 16.2 and Clang compiled the affected
objects at all eight levels. A fresh Clang `-Os` sweep then emitted 87
non-prototype diagnostics and reached no link. The GCC `-Os` objects for the
eight typed-definition translation units matched their original objects after
debug-section removal, including their relocations and remaining metadata.
The trial does not resolve the remaining Clang or GCC flash failures.

An isolated `locore.S` experiment replaces the two forward symbol-difference
immediates and their base-address addition with PC-relative `adr` instructions.
GCC 16.2 and Clang 23 both assembled the experiment for Cortex-M0+ and emitted
the same two address instructions targeting the path and argument vector in
their respective disassemblies. A Unicorn Thumb probe copied each image to
the user-window base, executed those two instructions, and checked the path
and vector addresses; an altered immediate produced a different path address.
GCC's `icode` object symbol fell from 72 to 64 bytes in this experiment;
linked-image size remains unmeasured.
The source, objects and probe are in `.build-logs/icode-adr-probe`. The probe
does not execute the privilege transition, syscall, or complete board boot,
so the candidate has not entered production.

The `check-icode-relocation` cross gate assembles the updated entry code with
GCC and Clang, checks the privilege-transition bytes, copied PC-relative
addresses, supervisor calls, strings and argument vector, and rejects four
address, opcode and vector mutations for each compiler. A separate physical
PICO run booted the `-Os` trial kernel whose UF2 SHA-256 was
`dc755be815507ff18cb8754e53a34702bfd65cbd03ba6d3b617fa64046cbd7ff`;
the selected kernel identity and both packed and raw `seq 1` commands passed.
The retained plain kernel and root images then loaded with verification and
reached `login:`. The control, flash and recovery logs are in
`.build-logs/optimization-gcc16-clang-matrix/icode-board`.
The boot does not establish MPU region coverage or a deliberate fault test.

At the older revision, separate clean PICO `-Os` GCC sweeps added one warning
family at a time under `-Werror`: `-Wformat=2` linked successfully;
`-Wshadow` reported eight errors in six files; `-Wundef` reported one error;
and `-Wstrict-prototypes` reported 53 errors in 16 files. A successful PICO
sweep does not establish acceptance by other configurations or compilers.
The candidate changes resolve the identified shadow and undefined-macro
diagnostics and add all three flags to the RP2040 kernel warning policy.
All four configurations compiled with those flags using GCC 16.2 and Arm
GNU 13.2 in the candidate source. The original Arm GNU 13.2 `-Os` worktree was
unavailable at the later inventory; the candidate `-Os` and `-Oz` gate logs
remain in the separate section-collection investigation worktree. The
warning-policy gate includes a clean
control and one omitted-flag control for each added family. Clang's broader
PICO warning backlog and all userland routes remain separate.

ARM GCC reports `armv6s-m` for `-mcpu=cortex-m0plus`; both ARM GCC and Clang
predefine ARMv6-M and Thumb-1 for that CPU. Explicit `-march=armv6-m
-mtune=cortex-m0plus -mthumb` produced the same inspected architecture macros,
but macro equality does not prove identical generated instructions or timing.
Target-specific tuning changes require object inspection, size comparison and
execution. `-Wall`, `-Wextra`, `-Werror`, `-g`, `-ffreestanding`,
`-fno-builtin`, `-fno-common`, `-fno-strict-aliasing`, `-mcpu=cortex-m0plus`
and `-mthumb` were accepted by the surveyed ARM GCC and Clang kernel commands.
The earlier complete Clang matrix failed at callback compilation. Later
candidate snapshots link `-Os` and `-Oz`; `-O0` and `-O3` exceed the flash
partition. A complete matrix on the final combined candidate remains required.

## Measurement surfaces

The non-final three-compiler PICO census uses source-manifest SHA-256
`3b5282df7e16b4f56cd09f1f5a8c0bc3f97fe3396dac60f786a639700fc03e30`.
The local, ignored `.build-logs/compiler-size-three-toolchain-census` directory
retains the commands, tools, maps, objects and logs. The following numbers are
bytes of headroom in the FLASH region after the complete load span; a negative
number denotes overflow. The `-O0` and `-O3` rows use failed-map `.ramfunc`
load ends; the linked rows use ELF program headers. All profiles retain the
existing separate `kern_time.c -Os` rule.

| Default optimization | GCC 16.2 | Arm GNU GCC 13.2 | Clang 23 |
| --- | ---: | ---: | ---: |
| `-O0` | -22,032 | -22,392 | -22,284 |
| `-O` | 22,164 | 20,468 | 18,232 |
| `-O1` | 22,164 | 20,468 | 18,232 |
| `-O2` | 19,004 | 18,780 | 68 |
| `-O3` | -6,812 | -5,604 | -15,496 |
| `-Og` | 22,944 | 21,688 | 11,000 |
| `-Os` | 35,308 | 34,732 | 28,644 |
| `-Oz` | 35,308 | 34,732 | 33,352 |

The six overflowing links and Clang `-O2` fail the 1,024-byte release
headroom requirement. All other rows link with that margin and show zero
warning lines. The capture's error counter recognizes `error:` but does not
count GNU ld's overflow messages, so its zero error counts on failed rows do
not mean clean links; the retained logs and exit statuses carry the verdict.
The later harness recognizes those linker diagnostics. Equal headroom does not
establish identical code. These rows also embed different wall-clock version
strings, so their linked bytes do not support a controlled code-identity
comparison. The corrected matrix fixes `SOURCE_DATE_EPOCH` and compares
generated version hashes. None of these rows is the final integrated candidate.

A later GCC 16.2 PICO comparison fixes `SOURCE_DATE_EPOCH` to the recorded
HEAD timestamp and produces the same `vers.c` SHA-256 in all four rows:
`4fdef4595fb1f739d68d89c83e9ae05b1bec17d6950cba43e77952113a62b76d`.
`-O` and `-O1` each produce 108,051 linked `.text` bytes with identical
SHA-256; `-Os` and `-Oz` each produce 94,939 linked `.text` bytes with
identical SHA-256. GCC's `-Q --help=optimizers` reports identical settings
within each pair. The source manifest SHA-256 is
`0b1196a54ed7b0a7edcb868b5341cfdf0f87669f38b6868eb4a9dd06c07798bb`;
`.build-logs/compiler-gcc16-fixed-version-comparison` retains the local
ignored capture and option listings. The `.text` comparison includes linked
read-only data but does not establish whole-ELF identity, other compiler
behavior, physical timing or a final-candidate result.

GCC documents `-Os`, `-Oz`, `-Og` and the precedence of the last optimization
option in its [optimization manual](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).
The [Arm options manual](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)
distinguishes ISA selection from tuning: `-mcpu` supplies both unless an
explicit `-march` or `-mtune` overrides the corresponding component. The
[Pico SDK 2.2.0 toolchain](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/cmake/preload/toolchains/pico_arm_cortex_m0plus_gcc.cmake)
uses `-mcpu=cortex-m0plus -mthumb`. Equal flag names across GCC and Clang do
not guarantee equal optimization passes or ABI-compatible runtime libraries.

Debray et al., [Compiler Techniques for Code Compaction](https://doi.org/10.1145/349214.349233),
report code factoring and interprocedural reductions on Alpha, not RP2040.
The inspected [LLVM ARM outliner implementation](https://github.com/llvm/llvm-project/blob/22d766e063e2335209f018f169317c444217ab1d/llvm/lib/Target/ARM/ARMBaseInstrInfo.cpp)
excludes Thumb-1 functions. For a candidate repeated sequence of `L` bytes at
`N` sites, `(N-1)*L - N*C - F` is a screening estimate, where `C` is the
replacement call size and `F` is helper overhead. Literal pools, alignment,
veneers and register spills make final linked size the deciding observation.
The estimate is an accounting model, not a theorem or a measured saving.

Stored compression and compiler compaction affect different resources. The
packed loader reconstructs the uncompressed text and data into SRAM, and a
successful exec logically decodes both streams in preflight and again after
commit. Cache hits mean the two passes do not imply twice the physical flash
traffic. A candidate code factoring change must compare raw bytes, packed
bytes, filesystem blocks and elapsed time: removing repeated native code can
also remove repetition the compressor encoded cheaply. The original
[heatshrink documentation](https://github.com/atomicobject/heatshrink/blob/master/README.md)
describes the window and lookahead tradeoff; the port's own format and
workspace limits govern any change to those parameters.

The non-final candidate's logged build at source base `af2ca599` produced the
following raw executables. `tools/bin/rp2040/hsaout -c` packed each file, and
`tools/inspect_packed_exec_work.py` verified the decoded body against the raw
image before calculating the successful loader path. GCC 16's ARMv6-M layout
reports a 544-byte decoder and a 480-byte staging area inside the 1,024-byte
workspace. The local `.build-logs/packed-exec-work-geometry-v3` capture records
the input files and tool with SHA-256 identities; that ignored build-evidence
directory is not part of the committed source tree. The provenance JSON has
SHA-256 `2bfaaeef6ce6d86bcbf09df60879e2e9ac79c1c3076d07201df4029bd393d302`;
the staged-index stream at capture has SHA-256
`797d7e41ba65e5a640f9f12d63ab0d5fdcc7d66f7a251ee02c9b406baccdd36e`.
The indexed source and build receipt are separate identities: the analysis
tool and its tests changed after that build, while the capture verifies the
actual executable bytes it analyzes.

| Executable | Raw / packed bytes | Raw / packed root blocks | Packed stream fills, both passes | Expanded bytes sent to CRC, both passes |
| --- | ---: | ---: | ---: | ---: |
| `usr/bin/seq` | 7,800 / 6,896 | 9 / 8 | 32 | 15,536 |
| `bin/sh` | 36,316 / 31,079 | 37 / 32 | 130 | 72,568 |
| `usr/bin/utilbox` | 55,381 / 45,054 | 56 / 45 | 190 | 110,698 |

These figures count calls to the packed stream's fill callback and expanded
bytes on two successful passes. The raw loader issues one body `rdwri` call;
both paths perform other header operations, and filesystem code can split
requests into multiple block operations. The table establishes neither flash
transactions nor timing. A failed preflight short-circuits later work, and a
packed process can also decode text during swap-in.

The [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
describes two Cortex-M0+ cores, 264 KiB of SRAM in six banks, a DMA engine,
programmable I/O, per-core divider and interpolator hardware, and a flash
execute-in-place (XIP) subsystem. Section 2.6.3.1 specifies a 16 KiB,
two-way set-associative XIP cache with a one-cycle hit; it caches flash
accesses, not ordinary SRAM accesses. The tested PICO configuration uses a
125 MHz CPU clock, while the chip specification and other boards may use
different clock settings.

Datasheet sections 2.1.1.2 and 2.6.3.5 define four
selectable 24-bit saturating AHB bus-fabric event counters and two 32-bit
saturating XIP cache access and hit counters. Counter configuration and clear
are writes to shared hardware state. A measurement must record selection,
start and end, saturation, concurrent activity and instrumentation cost; the
counter descriptions do not establish a measurement for this port. The
microsecond timer, SRAM bank contention, DMA, flash erase and program traffic,
USB and interrupt activity, stack watermark, process-window headroom and
filesystem block accounting provide additional distinct surfaces. Physical
board runs, Renode, instruction emulation, cross builds and static sizes carry
separate claims.

The existing `STORAGE_STATS` option supplies 26 saturating 32-bit software
counters and a sticky saturation flag through `machdep.storage_stats`.
`storage_snapshot()` samples dirty buffers and copies those counters while
the executing core masks interrupts. The implementation does not establish
per-process attribution or cross-core exclusion. `flash_erase()` and
`flash_program()` increment physical-traffic counters before the ROM calls;
those counts describe issued operations, not independently verified media
completion. A comparison must retain the saturation flag and distinguish
logical requests, physical requests, dirty-buffer state, and unrelated work.

`timer_us()` reads the low 32 bits of the free-running microsecond timer.
Unsigned subtraction disambiguates an elapsed interval only when it is shorter
than one full low-word wrap; the interval includes interrupt and flash stalls
and is not a CPU-cycle count. The datasheet index names timer section 4.6.2
and register section 4.6.5. XIP counter ownership, clear and saturation
behavior still need primary-source verification and board controls before any
counter result can set a numeric optimization budget.

## Compiler frame attribution

The local `.build-logs/compiler-stack-usage-controls-v2` capture retains
compiler `-fstack-usage` output for clean PICO `-O0` and `-Os` builds. Its
source manifest has SHA-256
`46a0b0cda2f41f1cb4fdab0634a41935bdd71a0807369d8b474023ea3a311fa5`.
The capture precedes subsequent test and documentation edits and does not
represent the final candidate. GCC 16.2 and Arm GNU GCC 13.2 each emit 80
reports per row. Clang 23.1 emits 75 and omits reports for five objects whose
retained `nm` output contains data symbols but no code symbols. Each `-O0`
link fails the flash bound; all three `-Os` links pass the 1,024-byte margin.

| Compiler and level | `namei_at` | `ttyinput` | `prf` | `rwip` | `cpu_sysctl` |
| --- | ---: | ---: | ---: | ---: | ---: |
| GCC 16, `-O0` | 400 | 24 | 152 | 80 | 72 |
| GCC 16, `-Os` | 376 | 24 | 112 | 64 | 48 |
| Arm GNU 13, `-O0` | 400 | 24 | 152 | 80 | 72 |
| Arm GNU 13, `-Os` | 376 | 24 | 112 | 64 | 48 |
| Clang 23, `-O0` | 424 | 56 | 200 | 120 | 104 |
| Clang 23, `-Os` | 440 | 32 | 128 | 96 | 48 |

Values are bytes in compiler-reported static frames. Inlining changes the
meaning of individual function frames between profiles; the table cannot
establish maximum call-chain, assembly, interrupt, or board stack use. The
capture compiles `kern_time.c` at its configured `-Os` exception in every row.
The data nominate stack-sensitive trials but do not set promotion budgets or
demonstrate runtime safety.

The identity-bound `.build-logs/compiler-object-attribution-matrix` capture
retains matching `-O0` and `-Os` PICO objects, dependency files and stack
reports for all three compiler routes. Its source-manifest SHA-256 is
`e72b6b76e25f4054904185d3a0364321bb1c355e0fe2de111596923ef1447768`;
its results SHA-256 is
`ba1ce1d7d2828c23aa35f691ffe67ad9e79eb340d3560fa2f2aa285ef9fc0890`.
Each route retains 83 objects per level. The capture exits nonzero because all
three `-O0` links exceed flash; the `-Os` links pass the headroom margin.

`tools/analyze_compiler_objects.py` verifies the row hashes before and after
analysis and retains section, function-symbol, relocation and disassembly
evidence. The local analyzer used for the following results has SHA-256
`e9e6ea857c0445aeed2a6823d72ad0ab531b329c83bf3dce2b3774e35ce0b43a`;
it uses relative object paths and the capture working directory, unlike the
analyzer in the capture's earlier source manifest. The capture and analysis
precede this documentation edit and are not final-candidate receipts.

| Compiler | Largest object executable-section delta | Matched `namei_at` sizes, `-O0` / `-Os` |
| --- | --- | --- |
| GCC 16 | `ufs_namei.o`: +3,776 bytes | 5,390 / 2,924 bytes |
| Arm GNU GCC 13 | `ufs_namei.o`: +3,720 bytes | 5,386 / 2,968 bytes |
| Clang 23 | `tty.o`: +2,956 bytes | 3,924 / 3,220 bytes |

Executable-section totals describe input objects. Function sizes are compared
only for unique, nonzero-size symbols present in both objects; inlining,
literal placement, discarded code and alignment prevent adding those deltas
or treating them as linked savings. The compiler-dependent ranking directs
separate source investigations. Stack and latency budgets and the decisive
board measurements remain open before a size trial can be promoted.

## macOS host-tier deadline

The macOS firmware job for PR 412 head `893fc2bb` reaches the 1,080-second
host-tier hard deadline while `usr.bin/calc/tests/mutants.py` is still running
and the process snapshot shows a newly started calculator limits case. The
runner exits 124, so the cross and board-build tiers do not run. Earlier
successful host-tier steps in firmware runs `42693211414`, `42605742193`,
and `42603604298` take 14:28, 12:50, and 13:56 respectively. Those times
show that the old deadline leaves limited margin; they do not establish the
completion time of the interrupted run or prove why that run is slower.

The candidate gives the macOS host tier a 1,500-second hard deadline inside a
27-minute step, retains the 300-second idle deadline and keeps the job's
60-minute limit. Calculator mutation output runs unbuffered so the retained
log shows completed mutants during the gate. A successful earlier macOS job
spends about six minutes in the subsequent cross tier. The increased budget
remains a hypothesis until hosted CI completes against the integrated head;
the longer deadline does not turn the interrupted run into a pass.
