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
| Wear and recovery | All 464 complete-operation cuts recover; a half-programmed page fails at cut 26. Resolve the payload-integrity contract before changing wear policy | model counterexample retained |
| Section garbage collection | After trace-source closure, `KERNEL_GC=yes` saves 928--2,348 flash-span bytes across five builds; placement, headroom and divider checks pass; boot evidence remains open | opt-in mode implemented and remeasured |
| PICO/PICO_DIAG | PICO omits both diagnostic options; PICO_DIAG adds exactly those options. Build, generated-config, reader and CI coverage include the sibling | implemented |
| Trace-name membership | Only SYSTRACE configurations link the trace decoder/name pool; four ordinary kernels save 1,196--1,200 flash bytes with unchanged RAM | source selection and calibrated image gate implemented |
| Additional linker flags | Alignment sorting reduces flash span by 8--12 bytes and BSS extent by 8--168 bytes; relaxation, common sorting and linker `-O2` produce no measured saving | opt-in sorting mode and private probe implemented |
| Anchored hot scalars | Four-field clock bank leaves code unchanged and adds eight data/flash bytes in every configuration; no production bank or `r9` reservation | private prototype rejected |
| Compact dispatch | Opt-in halfword decoder saves 308--320 flash-span bytes in five kernels; generated range/extent assertions, image admission and Cortex-M0 instruction tests pass | implemented; default remains full pointers pending boot evidence |
| Capacity admission | Reject invalid metric kinds before indexing; `-O2` compilation proceeds, but PicoCalc exceeds FLASH | bounds fix implemented |
| K&R interfaces | Six units include typed driver/service slots and the independent resource object-bounds repair; pure definition conversions preserve code bytes | implemented and reconciled with the existing PR commits |
| `adjtime` arithmetic | Commit `98810124` already splits seconds/ticks before division; current PICO contains no `__aeabi_ldivmod`, `__aeabi_lmul` or `__divdi3` | implemented before this tranche |
| 32 KiB SwapRAM | Private PICO/PICO_DIAG links retain 5,408 bytes beyond the exchange reserve; PicoCalc links reject the larger pool; UART remains unconfigured | link experiment implemented; no default change |

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
  from the remaining K&R definitions, declarations and callback protocols,
  and records scoped implementation, resource costs and admission residuals.
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

## Remaining implementation boundaries

The implemented tranche does not close every historical queue entry:

- Wear-policy admission stops at the partial-program counterexample. Payload
  integrity/fallback, the atomicity assumption and acknowledgement semantics
  require an explicit recovery design and production-adapter interruption
  evidence. Board erase-address traces and endurance distribution remain unrun.
- Halfword dispatch remains opt-in pending kernel boot and full syscall-path
  execution. Stub-handler emulation establishes address selection and decoding,
  not networking, scheduling, persistence or physical-board behavior.
- Kernel LTO retains the assembly-only roots, synthesized memory routines,
  scratch placement and literal-pool constraints recorded in the historical
  handoff. Userland/libc section collection remains a separate unimplemented
  experiment; kernel collection does not establish its startup roots.
- Scheduler/context declarations, kernel/libc
  nonreturning contracts, Boot ROM overlapping-copy semantics and remaining
  shipped libc K&R families retain separate caller/behavior closure. The
  completed syntax conversions do not establish those semantic contracts.
- Further scalar banks, narrower individual buffers and named USB-DPSRAM
  owners require measured co-use/access contracts. The rejected clock bank
  and unchanged 8-byte ABI do not justify blanket packing or alignment cuts.

Boot, physical-board telemetry, power-loss tests and runtime memory/traffic
comparisons remain distinct from the host and target-link results above.
