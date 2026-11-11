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
| Section garbage collection | `KERNEL_GC=yes` saves 2,132--3,548 text bytes across four builds; startup address words identified; boot evidence remains open | opt-in mode implemented |
| PICO/PICO_DIAG | PICO omits both diagnostic options; PICO_DIAG adds exactly those options. Build, generated-config, reader and CI coverage include the sibling | implemented |
| Additional linker flags | Alignment sorting reduces flash span by 8--12 bytes and BSS extent by 8--168 bytes; relaxation, common sorting and linker `-O2` produce no measured saving | opt-in sorting mode and private probe implemented |
| Anchored hot scalars | Four-field clock bank leaves code unchanged and adds eight data/flash bytes in every configuration; no production bank or `r9` reservation | private prototype rejected |
| Compact dispatch | All 163 handlers in five linked controls round-trip through halfword offsets and retain the Thumb bit; decoder, generated link assertions and execution remain open | representability probe implemented |
| Capacity admission | Reject invalid metric kinds before indexing; `-O2` compilation proceeds, but PicoCalc exceeds FLASH | bounds fix implemented |
| K&R interfaces | 31 core definitions and six libc definitions converted; core code bytes remain identical under matched target flags. Shared callbacks and semantic repairs remain separate | first interface tranche implemented |
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

## Remaining implementation boundaries

The implemented tranche does not close every historical queue entry:

- Wear-policy admission stops at the partial-program counterexample. Payload
  integrity/fallback, the atomicity assumption and acknowledgement semantics
  require an explicit recovery design and production-adapter interruption
  evidence. Board erase-address traces and endurance distribution remain unrun.
- Halfword dispatch still needs generated range assertions, an actual decoder,
  conditional INET execution and net image measurements. Address round trips
  alone do not admit a replacement dispatcher.
- Kernel LTO retains the assembly-only roots, synthesized memory routines,
  scratch placement and literal-pool constraints recorded in the historical
  handoff. Userland/libc section collection remains a separate unimplemented
  experiment; kernel collection does not establish its startup roots.
- Driver initialization shares a slot between controller and device pointers.
  Service calls supply no arguments, while the generator declares an integer
  parameter. Exact adapters or separate typed slots require a shared-port
  interface design; casts do not resolve the mismatch.
- `ruadd` object bounds, scheduler/context declarations, kernel/libc
  nonreturning contracts, Boot ROM overlapping-copy semantics and remaining
  shipped libc K&R families retain separate caller/behavior closure. The
  completed syntax conversions do not establish those semantic contracts.
- Further scalar banks, narrower individual buffers and named USB-DPSRAM
  owners require measured co-use/access contracts. The rejected clock bank
  and unchanged 8-byte ABI do not justify blanket packing or alignment cuts.

Boot, physical-board telemetry, power-loss tests and runtime memory/traffic
comparisons remain distinct from the host and target-link results above.
