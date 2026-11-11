# Bounded RP2040 footprint experiments

These experiments follow the resolved `struct user` layout work. They are
independent: do not combine their deltas or land them in one code change.

## Cross-configuration section garbage collection

The historical PICO scratch link using `-ffunction-sections
-fdata-sections` and `--gc-sections` removed as much as 2,463 text bytes and
reported 67 discarded sections. It also discarded a 32-byte `locore0.o`
section that has not yet been explained. That result is a lead, not an
admission result.

Rebuild PICO, PICO_UART, PICOCALC and PICOCALC_PSRAM from clean object
directories with the compile flags and production link recipe plus
`--gc-sections --print-gc-sections`. Preserve the normal linker assertions,
map file, fatal warnings and `KEEP` roots. For each configuration retain:

- baseline and candidate text/data/BSS, flash image and headroom;
- the discarded-section list with object and symbol attribution;
- the union and intersection across configurations;
- relocation checks for vector, reset, RAM functions, boot2, driver and
  generated dispatch roots;
- the full kernel test tier and a boot/smoke run for every board available.

Explain the `locore0.o` section before using a collected image. Keep vendored
Dhara and heatshrink source intact; dead members may be collected at link time
without creating a local source fork. Source deletion is a later, separate
decision and requires searches beyond the four RP2040 configurations.

## Lean PICO and PICO_DIAG

The current PICO config includes `SYSTRACE` and `CAPACITY_STATS`. The measured
handoff attributes 544 text bytes to the former and 760 text plus 96 BSS bytes
to the latter. The target split is:

- `PICO`: shipping USB/UART kernel, retains `HALTREBOOT`, omits both diagnostic
  options;
- `PICO_DIAG`: identical board/storage behavior plus `SYSTRACE`,
  `CAPACITY_STATS`, and opt-in build variables such as `STORAGE_STATS=yes`.

Before implementation, inventory every `kern.systrace`, `kern.capacity` and
capacity-reader assumption. Generate and track the new config Makefile, add it
to kernel/headroom/CI build coverage, and point diagnostic board procedures to
PICO_DIAG. Absence of a diagnostic sysctl on PICO must return an error rather
than stale output. Do not make instrumentation mandatory merely to keep an
existing procedure unchanged.

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
