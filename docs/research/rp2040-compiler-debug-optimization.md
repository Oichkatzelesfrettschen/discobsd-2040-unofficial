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
`C17_CAPTURE_DIR` recorder. The recorder retained the intermediate ELF before
the recipe removed it. Its link output and `elf2aout` input both had SHA-256
`d184dead7c9ebab1a457f64a0417dc4f6dd79580fb9d1e1054d90a8815b2b4a4`;
the converted a.out had SHA-256
`b14816486071f71b40bae6a0742fb77b4edbd6a91208981dd369116f779e0614`.
`arm-none-eabi-readelf -S` found `.debug_info`, `.debug_line` and `.symtab`
in the retained ELF. The pilot compiles `echo.c` with debug information but
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
and `warp-debug-libc-retry` logged gates; a complete retry remains necessary.

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

## Measured PICO build matrix

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
prerequisites. The commands and output are retained in the
`clang-unlimited-pico-os-all` and `clang-unlimited-pico-os-retry` logged gates.

At the older revision, separate clean PICO `-Os` GCC sweeps added one warning
family at a time under `-Werror`: `-Wformat=2` linked successfully;
`-Wshadow` reported eight errors in six files; `-Wundef` reported one error;
and `-Wstrict-prototypes` reported 53 errors in 16 files. A successful PICO
sweep does not establish acceptance by other configurations or compilers.
The candidate changes resolve the identified shadow and undefined-macro
diagnostics and add all three flags to the RP2040 kernel warning policy.
All four configurations compiled with those flags using GCC 16.2 and Arm
GNU 13.2 in the candidate source; the warning-policy gate includes a clean
control and one omitted-flag control for each added family. Clang's broader
PICO warning backlog and all userland routes remain separate.

ARM GCC reports `armv6s-m` for `-mcpu=cortex-m0plus`; both ARM GCC and Clang
predefine ARMv6-M and Thumb-1 for that CPU. Explicit `-march=armv6-m
-mtune=cortex-m0plus -mthumb` produced the same inspected architecture macros,
but macro equality does not prove identical generated instructions or timing.
Target-specific tuning changes require object inspection, size comparison and
execution. `-Wall`, `-Wextra`, `-Werror`, `-g`, `-ffreestanding`,
`-fno-builtin`, `-fno-common`, `-fno-strict-aliasing`, `-mcpu=cortex-m0plus`
and `-mthumb` were accepted by the surveyed ARM GCC and Clang kernel commands;
the complete Clang build remains failing for the reasons above.

## Measurement surfaces

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
