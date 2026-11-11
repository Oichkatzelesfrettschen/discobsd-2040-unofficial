# Bounded RP2040 footprint experiments

These experiments follow the resolved `struct user` layout work. They are
independent: do not combine their deltas or land them in one code change.

## Cross-configuration section garbage collection

`KERNEL_GC=yes` enables function/data sections and linker collection in the
kernel build. The default is `no`; other values fail during Makefile parsing.
A `KERNEL_SECTION_GC` parameter marker invalidates all objects when the mode
changes. Size- and cycle-specific compile rules retain their optimization
levels and receive the section flags. The generated configuration Makefiles
carry the same rule as `conf/Makefile.rp2040`.

GCC 15.2.1, GNU ld 2.45.1, `-Os`, `2b387882` reconciled with `42b49f48`:

| Configuration | Default text | Collected text | Text saving | BSS saving |
| --- | ---: | ---: | ---: | ---: |
| PICO | 92,701 | 89,157 | 3,544 | 16 |
| PICO_UART | 85,016 | 81,468 | 3,548 | 16 |
| PICOCALC | 109,909 | 107,785 | 2,124 | 24 |
| PICOCALC_PSRAM | 112,023 | 109,879 | 2,144 | 24 |
| PICO_DIAG | 94,100 | 91,752 | 2,348 | 16 |

Data sizes remain unchanged. All five collected links pass flash-work,
SwapRAM placement, flash-headroom and divider-ownership checks. Default
objects are restored after the matrix. Boot and hardware evidence remain
open, so collection remains
an explicit experiment mode.

After trace-source closure at `8c8fc442`, matched full-pointer links with the
same compiler/linker and `-Os` measure:

| Configuration | Default flash span | Collected flash span | Flash saving | BSS saving |
| --- | ---: | ---: | ---: | ---: |
| PICO | 91,772 | 89,424 | 2,348 | 16 |
| PICO_UART | 84,016 | 81,668 | 2,348 | 16 |
| PICOCALC | 109,108 | 108,180 | 928 | 24 |
| PICOCALC_PSRAM | 111,272 | 110,328 | 944 | 24 |
| PICO_DIAG | 94,364 | 92,016 | 2,348 | 16 |

The omitted trace source already releases bytes that collection previously
discarded. Those savings are not additive. Data, SwapRAM pool sizes and
eight-byte section/stack alignment remain unchanged. These full-pointer
builds and the compact collected builds pass the same placement, divider
and headroom owners.

The 32-byte `locore0.o` input `.text` contains eight anonymous address words
before the explicit `.text.Reset_Handler` section. `objdump -h -r locore0.o`
shows eight ABS32 relocations to linker boundaries and no incoming reference
to those words. Reset code carries its own literal pool and calls in the
separate retained handler section. The vector table and boot2 remain `KEEP`
roots. The discarded words therefore establish no startup dependency.

`unix.map` retains the discarded input-section ledger. GNU ld 2.45.1 makes
`--print-gc-sections` diagnostics fatal under `--fatal-warnings`; the supported
mode keeps fatal warnings and uses the map ledger. Reproduction:

```sh
bmake MACHINE=rp2040 KERNEL_GC=yes kernel
bmake -C sys/arch/rp2040/compile KERNEL_GC=yes check-flash-swap check-swapram
bmake MACHINE=rp2040 check-kernel-headroom
bmake MACHINE=rp2040 KERNEL_GC=no kernel
```

The final command restores default objects through the parameter stamp.
Changing `COPTS` separately still requires a clean build.

The unresolved admission evidence is boot/smoke execution. Keep vendored Dhara and
heatshrink source intact. Dead-member collection does not authorize a local
source fork or source deletion.

## Additional linker flags

`tools/rp2040_footprint_probe.py` relinks existing objects into temporary files
and compares each control against its built kernel. It preserves fatal
warnings and both production linker scripts. Its optional `--compare-ref`
compiles the four core interface units against a Git reference with the
same target flags. No production build products change.

GCC 15.2.1, GNU ld 2.45.1, `2b387882` reconciled with the typed-driver and
resource changes in `42b49f48`:

| Configuration | Default flash span | Data | Ordinary BSS | Post-exchange RAM margin |
| --- | ---: | ---: | ---: | ---: |
| PICO | 92,968 | 264 | 15,840 | 21,792 |
| PICO_UART | 85,216 | 200 | 15,408 | 38,608 |
| PICOCALC | 110,304 | 392 | 28,504 | 9,672 |
| PICOCALC_PSRAM | 112,472 | 448 | 28,656 | 9,104 |
| PICO_DIAG | 94,364 | 264 | 15,840 | 21,792 |

Flash span starts at `0x10000000` and includes boot2 and load-image padding.
Ordinary BSS excludes the 16 KiB SwapRAM pool and scratch sections. The
post-exchange margin follows the linked `__kernel_ram_used_end` and already
subtracts the mandatory 4 KiB reserve; it is not a measured runtime peak.

| Configuration | Alignment-sort flash-span delta | BSS-extent delta |
| --- | ---: | ---: |
| PICO | -12 | -168 |
| PICO_UART | -8 | -72 |
| PICOCALC | -12 | -8 |
| PICOCALC_PSRAM | -8 | -8 |
| PICO_DIAG | -8 | -64 |

`--sort-common=descending`, `--relax`, `--no-relax` and linker `-O2` produce
zero measured delta in every configuration. Their combination with alignment
sorting equals alignment sorting alone. These measurements use ordinary
objects, not function-section GC objects. A linker optimization level does
not select a compiler optimization level.

`KERNEL_SORT_ALIGNMENT=yes` enables the sorting experiment; its default is
`no`, and any other value fails parsing. The parameter stamp invalidates
objects when switching modes. Sorting does not lower any required object
alignment or the retained 8-byte section/stack boundary. BSS extents include
padding, so removing diagnostic objects need not release their nominal size
until an alignment hole also closes. Boot and runtime evidence remain separate.

```sh
${PYTHON} tools/rp2040_footprint_probe.py --board PICO --board PICO_UART \
  --board PICOCALC --board PICOCALC_PSRAM --board PICO_DIAG --compare-ref fb4fae87
bmake MACHINE=rp2040 KERNEL_SORT_ALIGNMENT=yes kernel
bmake MACHINE=rp2040 KERNEL_SORT_ALIGNMENT=no kernel
```

`--verify-layout` applies the flash, SwapRAM and divider owners to both the
private control and alignment-sorted link. All five pairs pass. Required
Python dependencies are Capstone and pyelftools; build-only results do not
substitute for these binary-inspection checks.

## Larger SwapRAM link bound

`--swapram-32` recompiles the sole production translation unit whose storage
depends on `SWAPRAM_KB`, replaces `swapram.o` in a private link and retains
the production RAM/exchange assertions. PICO and PICO_DIAG link a 32,768-byte
pool without changing their loaded flash span, data or ordinary BSS. Each
retains 5,408 bytes beyond the mandatory 4,096-byte exchange reserve.
PICOCALC and PICOCALC_PSRAM fail the production reserve assertion. PICO_UART
has no configured pool; the probe adds none. No board pool size changes.
Compression acceptance, fallback traffic, runtime peaks and latency remain
unmeasured for the larger pool, so link capacity is not a wear reduction.

## Lean PICO and PICO_DIAG

PICO omits `SYSTRACE` and `CAPACITY_STATS`; PICO_DIAG retains both. The split is:

- `PICO`: shipping USB/UART kernel, retains `HALTREBOOT`, omits both diagnostic
  options;
- `PICO_DIAG`: identical board/storage behavior plus `SYSTRACE`,
  `CAPACITY_STATS`, and opt-in build variables such as `STORAGE_STATS=yes`.

The architecture registry, generated Makefiles, headroom/divider checks and
Linux/macOS firmware image coverage include PICO_DIAG. The generated-config
gate requires exactly the two diagnostic-option additions and the board-name
change; missing-option and diagnostic-in-base mutations fail. Capacity image
preparation selects `BOARD=PICO_DIAG`. PICO_UART and PicoCalc retain their
existing instrumentation.

Matched links measure 92,968 bytes from flash origin through the final load
byte for lean PICO and 94,364 for PICO_DIAG: a 1,396-byte difference. Both
ordinary `.bss` extents remain 15,840 bytes because alignment absorbs the
removed state. The ILP32 sysctl fixture requires `EOPNOTSUPP`, unchanged
payload bytes and an unchanged returned size for all three absent diagnostic
nodes. Consumer tests also reject unavailable and malformed capacity records.

The split is configuration hygiene as well as size work: telemetry has a named
home, while the default image does not pay its permanent RAM/flash cost.

## Trace-name source closure

Against `ac6afe1d`, GCC 15.2.1 and GNU ld 2.45.1 at default `-Os`, making
`kern/syscalls.c` conditional on `SYSTRACE` changes loaded flash span by:

| Configuration | Flash bytes | Data/BSS bytes |
| --- | ---: | ---: |
| PICO | -1,196 | 0 / 0 |
| PICO_UART | -1,200 | 0 / 0 |
| PICOCALC | -1,196 | 0 / 0 |
| PICOCALC_PSRAM | -1,200 | 0 / 0 |
| PICO_DIAG | 0 | 0 / 0 |

The only RP2040 call to `syscall_name` lies under `SYSTRACE_ON`, which expands
to zero without the option. No live call path is removed. PICO_DIAG retains
the decoder, all 163 trace spans and their name pool. Generated-source and
final-image gates enforce membership; the numeric syscall ABI and handler
table remain unchanged. The preceding default measurements retain their
pre-closure revision boundary. No section-GC flag is required for this saving.
Lean PICO then spans 91,772 flash bytes versus PICO_DIAG's 94,364, a
2,592-byte difference; their ordinary BSS extents remain equal.

## Anchored hot scalars

The PICO disassembly survey found 891 literal words referring to 169 distinct
RAM addresses. A small structure can let code load one base literal and reach
several related 32-bit fields with Thumb-1 immediate offsets. DragonFly's
`globaldata`, NetBSD's `cpu_data`, and FreeBSD/OpenBSD per-CPU state validate
the ownership pattern, but their SMP facilities are outside scope.

Prototype one core-0-owned bank, ordered by measured co-use rather than name.
Initial groups to measure are clock/tick state, scheduler/run state and
VM/swap cursors. Keep interrupt-shared/volatile qualification and core-1-owned
state explicit. Add offset assertions for the applicable Thumb byte, halfword
or word reach; never assume the 124-byte word reach also applies to byte or
halfword instructions.

Do not dedicate `r9`: it is an AAPCS platform register and the XIP shared-libc
design already has a possible GOT role for it. A normal local base may save
literals only in functions that use multiple bank members, so compare emitted
instructions per function and the whole image. Reject a bank that merely
trades a literal for an extra base load, increases interrupt latency, obscures
ownership or grows any bounded configuration.

The private `--hot-clock-bank` probe groups `monosec`, `realsec`, `monotick`
and `realtick`. It retains external symbol aliases for existing consumers,
asserts a 12-byte, four-byte-aligned bank and offsets 8/10 for the halfwords,
and replaces only `kern_clock.o` in the candidate link. All five candidates
retain the 1,196-byte clock code section. Initialized data and loaded flash
each grow by eight bytes; ordinary BSS extent does not shrink. The initialized
deadline pulls the other bank members into `.data`, and alignment absorbs
their removed BSS. This candidate is rejected. The probe makes no runtime
claim and does not establish an alias-based production ownership design.

## Compact dispatch tables

The default representation retains 163 argument bytes and 652 bytes of
naturally aligned handler pointers. `KERNEL_COMPACT_DISPATCH=yes` replaces
only the pointer table with 163 unsigned halfwords. The generated private
header decodes `0x10000100 + 2 * offset + 1` into the existing handler type.
The caller retains its bounds check and u-area calling contract. Full-pointer
dispatch remains the default; no public numeric syscall definition changes.

The metadata generator owns two linker inputs. EXTERN roots preserve selected
handlers under GC. The table fragment uses ABSOLUTE handler addresses inside
the output section and rejects either range violation, odd code addresses,
an odd table start or an extent other than 326 bytes. GNU ld expressions
strip the function's Thumb tag; the post-link image gate checks ELF function
metadata independently and verifies every reconstructed pointer. A failure
blocks derived-image publication, including replacement of stale images.
The parameter stamp invalidates objects and regenerated fragments in either
mode-switch direction. Missing PYTHON and invalid mode values fail parsing.

Matched against `8c8fc442`, GCC 15.2.1, GNU ld 2.45.1, default `-Os`, without
GC or alignment sorting:

| Configuration | Loaded flash-span delta | Data/BSS delta | Syscall static frame, full/compact |
| --- | ---: | ---: | ---: |
| PICO | -320 | 0 / 0 | 32 / 32 |
| PICO_UART | -308 | 0 / 0 | 32 / 32 |
| PICOCALC | -320 | 0 / 0 | 32 / 32 |
| PICOCALC_PSRAM | -308 | 0 / 0 | 32 / 32 |
| PICO_DIAG | -308 | 0 / 0 | 48 / 48 |

The 326-byte gross table saving is reduced by decoder/literal/padding costs
in those whole-image deltas. Static frames are compiler reports, not runtime peaks.
All five compact links, both ordinary and collected, pass dispatch, flash,
SwapRAM, divider and headroom checks. Default objects are restored afterward.
The default-mode generated fragments are empty and add no image bytes.

`check-compact-dispatch` executes all 163 selections and indirect calls in
13 Cortex-M0 instruction runs: full/compact controls, INET/GLOB combinations,
tracing, defined-zero options, GC, alignment sorting and an Arm Clang build.
Volatile function-pointer storage prevents the comparison oracle from turning
the tested indirect call into a direct branch. Two decoder mutations return
the expected pointer-mismatch status; four linker controls, a non-Thumb
image and an unsupported host architecture are rejected. Three image controls
and eleven corruptions calibrate the companion structural checker. These
fixtures execute stub handlers, not kernel syscalls or networking code.

The measured artifacts are the five configuration directories' `unix.elf`
images and compiler `-fstack-usage` reports for production `syscall.c`.
Reproduce each image mode before inspecting it; ordinary make-variable
queries describe the requested mode, not the options of stale objects:

```sh
: "${PYTHON:?select the gate interpreter}"
bmake MACHINE=rp2040 logged-build
bmake -C sys/arch/rp2040/compile KERNEL_COMPACT_DISPATCH=yes all
bmake MACHINE=rp2040 KERNEL_COMPACT_DISPATCH=yes check-dispatch-image \
  check-flash-swap check-swapram check-divider check-kernel-headroom
bmake -C sys/arch/rp2040/compile KERNEL_COMPACT_DISPATCH=yes KERNEL_GC=yes all
bmake MACHINE=rp2040 KERNEL_COMPACT_DISPATCH=yes KERNEL_GC=yes \
  check-dispatch-image check-flash-swap check-swapram check-divider check-kernel-headroom
bmake -C sys/arch/rp2040/compile KERNEL_COMPACT_DISPATCH=no KERNEL_GC=no all
bmake MACHINE=rp2040 check-compact-dispatch
```

Boot and full kernel syscall-path execution remain open. Keep the mode opt-in.
`cdevsw` mixes function and RAM data pointers and remains outside this encoding;
packed function pointers and larger upstream `sysent` records are not used.
