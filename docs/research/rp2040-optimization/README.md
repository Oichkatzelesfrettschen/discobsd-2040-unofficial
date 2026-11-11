# RP2040 optimization program

This directory is the handoff and design hub for the 32-bit C17/Thumb-1
footprint program. It keeps related experiments together without moving the
stable authorities that existing documents and gates already cite.

The program optimizes for correctness and recoverability first, then RAM,
kernel flash, packed-root storage and flash wear. Extra cycles are acceptable
when the measured storage or memory result is positive and interrupt latency
and persistence contracts remain intact.

## Current position

| Workstream | Decision or next evidence | State |
| --- | --- | --- |
| Hot `struct user` layout | Cold save areas moved behind the Thumb-1 hot range; all four kernels save 2,024--2,056 text bytes with no data/BSS growth | implemented in this PR |
| Alignment | Retain the 8-byte ABI, stack and section boundary; use 4-byte natural alignment for words and narrower storage only behind explicit byte/halfword contracts | decision recorded |
| RAM and memory map | Retain the 192/58/3/3/8 KiB split; projected windows above 192 KiB consume the worst configuration's stability margin before they provide a material process gain | adversarial review recorded |
| Wear and recovery | Count requested program/erase equivalents and prove reset-state recovery before changing checkpoint, eviction or staging policy | evidence plan |
| Section garbage collection | `KERNEL_GC=yes` saves 2,132--3,548 text bytes across four builds; startup address words identified; boot evidence remains open | opt-in mode implemented |
| PICO/PICO_DIAG | Keep the shipping PICO lean; move `SYSTRACE` and `CAPACITY_STATS` to a diagnostic sibling after the reader/build inventory is closed | design queued |
| Anchored hot scalars | Prototype a small core-0-owned scalar bank; do not import an SMP per-CPU framework or reserve `r9` | experiment queued |
| Compact dispatch | Keep byte argument counts and full aligned handler pointers as the control; prototype generated halfword code offsets only if relocation and Thumb-bit checks pass | experiment queued |
| Capacity admission | Reject invalid metric kinds before indexing; `-O2` compilation proceeds, but PicoCalc exceeds FLASH | bounds fix implemented |

## Documents and ownership

UART flash work buffers fit SRAM4. USB ring peak and discarded-byte accounting
are implemented through read-only sysctls; board measurements remain pending.

- [alignment.md](alignment.md) owns byte-alignment decisions and experiments.
- [memory-map.md](memory-map.md) owns current linked RAM decomposition,
  adversarial alternatives and map-level admission evidence.
- [wear-recovery.md](wear-recovery.md) owns the evidence order for flash policy.
- [footprint-candidates.md](footprint-candidates.md) owns the four bounded
  footprint experiments named above.
- [bsd-comparisons.md](bsd-comparisons.md) records the pinned upstream ideas
  and the limits of each comparison.
- [kr-migration.md](kr-migration.md) scopes caller-complete C17 interface units
  from the remaining K&R definitions, declarations and callback protocols.
- [`../kernel-footprint-handoff.md`](../kernel-footprint-handoff.md) retains
  the detailed historical measurements and candidate queue.
- [`../rp2040-memory-wear-engineering-program.md`](../rp2040-memory-wear-engineering-program.md)
  and [`../flash-storage-expectation-plan.md`](../flash-storage-expectation-plan.md)
  remain the storage architecture and Dhara amplification authorities.
- `sys/arch/rp2040/doc/STORAGE.md`, `CAPACITY.md` and `TESTING.md` remain the
  operator-facing contracts.

## Handoff rule

Update this page when a workstream changes state. Put repeatable commands,
acceptance thresholds and rejected controls in the workstream document. Put
captured output in a named evidence artifact only when review needs the raw
record. Do not create another session-named handoff for this program.

Every measurement must identify the revision, configuration, toolchain,
command and artifact. Host models establish algorithm behavior; cross builds
establish ARM layout and size; a board run establishes only the named board,
image and workload. A proposed or inferred result must not be reported as a
measurement.
