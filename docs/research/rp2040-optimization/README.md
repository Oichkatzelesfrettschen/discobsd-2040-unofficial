# RP2040 optimization program

This directory groups the research notes of the 32-bit C17 and Thumb-1
footprint program: alignment, the linked RAM map, flash telemetry and
interruption recovery, and the BSD design comparisons. It leaves the stable
authorities that existing documents and gates cite where they are. Each note
states its evidence class; the closeout record
[`../kernel-footprint-pr435-closeout.md`](../kernel-footprint-pr435-closeout.md)
holds the disposition of every mechanism of the superseded PR #435 branch and
the measurements that decide which of them are open.

The program optimizes correctness and recoverability first, then RAM, kernel
flash, packed-root storage and flash wear. Extra cycles are acceptable when
the measured storage or memory result is positive and interrupt latency and
persistence contracts hold.

## Notes

- [alignment.md](alignment.md): which alignments the build keeps and the
  admission rule for narrower storage.
- [memory-map.md](memory-map.md): linked RAM decomposition per kernel
  configuration, DPSRAM ownership and the process-window bound.
- [wear-recovery.md](wear-recovery.md): the evidence order for flash policy
  and the interruption oracle of `check-dhara-amplification`.
- [bsd-comparisons.md](bsd-comparisons.md): pinned upstream patterns and the
  limit of each comparison.

## Neighboring authorities

- [`../kernel-footprint-handoff.md`](../kernel-footprint-handoff.md): the
  measured candidate queue with per-item bytes and acceptance commands.
- [`../rp2040-memory-wear-engineering-program.md`](../rp2040-memory-wear-engineering-program.md)
  and [`../flash-storage-expectation-plan.md`](../flash-storage-expectation-plan.md):
  the storage architecture and Dhara amplification authorities.
- `sys/arch/rp2040/doc/STORAGE.md`, `CAPACITY.md` and `TESTING.md`: the
  operator-facing contracts and the gate definitions.

## Evidence rule

Every measurement names the revision, configuration, toolchain, command and
artifact. Host models establish algorithm behavior; cross builds establish
ARM layout and size; a board run establishes the named board, image and
workload. A proposed or inferred result is never reported as a measurement.
