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

GCC 15.2.1, GNU ld 2.45.1, `-Os`, following `f019845e`:

| Configuration | Default text | Collected text | Text saving | BSS saving |
| --- | ---: | ---: | ---: | ---: |
| PICO | 94,021 | 91,665 | 2,356 | 16 |
| PICO_UART | 84,944 | 81,396 | 3,548 | 16 |
| PICOCALC | 109,845 | 107,713 | 2,132 | 24 |
| PICOCALC_PSRAM | 111,951 | 109,807 | 2,144 | 24 |

Data sizes remain unchanged. All four collected links pass the flash-work
and SwapRAM placement checks and the flash-headroom gate. Divider ownership
validation requires Capstone and pyelftools, unavailable in the execution
environment. Boot and hardware evidence remain open, so collection remains
an explicit experiment mode.

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

The unresolved admission evidence is boot/smoke execution and a collected
PICO_DIAG matrix after the configuration split. Keep vendored Dhara and
heatshrink source intact. Dead-member collection does not authorize a local
source fork or source deletion.

## Additional linker flags

`tools/rp2040_footprint_probe.py` relinks existing objects into temporary files
and compares each control against its built kernel. It preserves fatal
warnings and both production linker scripts. Its optional `--compare-ref`
compiles the four core interface units against a Git reference with the
same target flags. No production build products change.

GCC 15.2.1, GNU ld 2.45.1, `fb4fae87` plus the interface/configuration changes:

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

Matched links measure 92,904 bytes from flash origin through the final load
byte for lean PICO and 94,300 for PICO_DIAG: a 1,396-byte difference. Both
ordinary `.bss` extents remain 15,840 bytes because alignment absorbs the
removed state. The ILP32 sysctl fixture requires `EOPNOTSUPP`, unchanged
payload bytes and an unchanged returned size for all three absent diagnostic
nodes. Consumer tests also reject unavailable and malformed capacity records.

The split is configuration hygiene as well as size work: telemetry has a named
home, while the default image does not pay its permanent RAM/flash cost.

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

The current generated control is already compact and alignment-safe. In the
current PICO ELF, `syscall_nargs` is 163 bytes and `syscall_handlers` is 652
bytes: one byte and one 4-byte pointer for each of 163 entries. Trace names use
a 326-byte halfword span table and an 819-byte shared string pool.

The next prototype may encode handler addresses as generated 16-bit offsets
from a fixed, even text anchor while retaining byte argument counts. Its
generator/assembler must prove:

- every target is a Thumb function, representable relative to the anchor and
  restored with the Thumb bit before the indirect call;
- the table is 2-byte aligned and decoded without an unaligned word access;
- conditional `INET` entries, `nosys` aliases and all 163 ABI slots match the
  full-pointer table;
- link movement outside the representable range fails at build time;
- dispatch behavior, syscall tracing and every configuration pass unchanged.

The theoretical pointer-table saving is 326 bytes before decoder and anchor
cost. `cdevsw` is not the same problem: it mixes function pointers with RAM
data pointers and needs a separately generated prototype, if any. Do not
recreate the larger upstream BSD `sysent` records or use packed function
pointers.

The private footprint probe reads every handler pointer from each of the five
linked control ELFs, requires a matching Thumb-function symbol and round-trips
all 163 slots through `(pointer - 1 - 0x10000100) / 2`. The largest encoded
offset is 32,710; the representation has a 65,535 limit. Missing slots,
even/non-Thumb pointers and an out-of-range address calibrate rejection.
This checks actual linked addresses, not a future link's representability.
Generated link-time range assertions, a decoder, INET variant execution and
whole-image net savings remain prerequisites to replacing production pointers.
