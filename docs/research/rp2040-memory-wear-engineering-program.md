# RP2040 memory, recovery, and flash-wear engineering program

## Status and evidence boundary

This document turns the memory and storage architecture audit into a bounded
engineering program. It describes proposed work and acceptance contracts. It
does not report that the watchdog, telemetry, 32 KiB SwapRAM pool, compressed
flash format, checkpoint batching, or RAM-staged rewrite mechanism exists.

The submitted audit snapshot is commit
`728677b8ff4330decf579b8ea03693ee904bb592`. The size measurements and
worktree observations below are local observations supplied for that snapshot.
They are neither independently reproduced artifacts nor continuing claims
about a moving `main` branch.

The snapshot inspection found a dirty `fix/watchdog-hang-breadcrumb` worktree
based on `bc277f8cf05c04e716b1caec4f7611a935e4ae75`. Its draft records flash
phases in watchdog scratch1--3, but it lacks the normal arming path, periodic
reload, and a writer for `WD_ARMED_TAG`. The branch is incomplete
instrumentation. Scratch1--3 remain provisionally owned by that work until an
isolated integration resolves or retires it.

The program uses five evidence classes and keeps their claims separate:

1. Source inspection establishes what the pinned code says.
2. Host tests establish behavior of the linked production algorithm under a
   model.
3. Target builds establish compilation, placement, and linked dependencies.
4. Board runs establish behavior of one identified kernel, filesystem image,
   board, reset method, and workload.
5. Power-interruption tests establish only the persistent-state boundary they
   actually interrupt.

## Architectural decision

The next capacity experiment uses ordinary main-SRAM headroom. Peripheral
registers, FIFOs, PIO state, DMA state, and the XIP cache do not form an
ordinary allocation tier. SRAM4 and SRAM5 already hold the USB transmit ring
in the PICO build. USB DPRAM contains fixed USB state and three named working
objects, with two unassigned gaps totaling 1,200 bytes. Those gaps remain
available only for named owners whose initialization follows `usbinit()`.

The first implementation track establishes recovery and measurement. The
second track compares otherwise equivalent 16 KiB and 32 KiB SwapRAM builds.
The third track evaluates compressed flash images and Dhara checkpoint policy
as separate storage changes. A measured ordinary-rewrite rate may activate a
fourth, conditional RAM-staging experiment.

The 32 KiB pool has two mutually exclusive uses. SMALL mode offers a larger
compressed-image pool. LARGE mode evacuates and closes the pool, then lends
the same bytes to a process window. A larger pool cannot supply both benefits
at once.

## Submitted memory measurements

| Linked artifact | Text | Data | Reported BSS | Main-RAM headroom | Kernel-flash headroom |
| --- | ---: | ---: | ---: | ---: | ---: |
| PICO USB | 101,509 B | 248 B | 39,648 B | 76,576 B | 29,312 B |
| PICO_UART | 89,803 B | 192 B | 14,632 B | 93,456 B | 41,076 B |

The PICO calculation is:

```text
ordinary kernel-RAM end: 0x2002BCE0
U0AREA start:            0x2003E800
difference:              76,576 bytes, 74.78125 KiB
```

The value is linked headroom inside the reserved kernel-RAM interval. It is
not a heap and does not enlarge the 3 KiB u-area that contains `struct user`
and the active kernel stack.

The reported BSS total includes NOBITS storage that `arm-none-eabi-size`
accounts as BSS, including the 16 KiB `.swapram_pool` and 8 KiB `.scratch`
sections in PICO. The linker marks both sections NOLOAD and places them outside
ordinary `.bss` clearing. Their owners establish valid contents before use.

## Special-memory ownership at the snapshot

| Resource | Submitted ownership | Capacity status |
| --- | --- | --- |
| SRAM0--3 striped window | user window, kernel RAM, and two u-areas | linked PICO headroom is the first capacity candidate |
| SRAM4 and SRAM5, PICO | complete 8 KiB USB transmit ring | allocated |
| SRAM4 and SRAM5, PICO_UART | fixed flash and boot working objects under its separate linker layout | configuration-specific; not the USB ring |
| XIP cache data RAM | active XIP cache for the flash-resident kernel | role conversion deferred |
| USB DPRAM | USB state plus Dhara, codec, and flash work buffers | 1,200 bytes remain in two gaps |
| watchdog scratch0 | raw-swap next-fit cursor | allocated |
| watchdog scratch1--3 | dirty watchdog draft | provisionally allocated |
| watchdog scratch4--7 | boot ROM reboot conventions | reserved |

The PICO USB-DPRAM ledger is:

| Address interval | Bytes | Owner |
| --- | ---: | --- |
| `0x50100000--0x5010023F` | 576 | USB control and endpoint reservation |
| `0x50100240--0x5010063F` | 1,024 | Dhara page buffer |
| `0x50100640--0x5010073F` | 256 | unassigned gap |
| `0x50100740--0x50100B4F` | 1,040 | shared codec workspace |
| `0x50100B50--0x50100EFF` | 944 | unassigned gap |
| `0x50100F00--0x50100FFF` | 256 | flash program-page buffer |

`usbinit()` clears the complete DPRAM array. A future gap owner requires a
named linker object, explicit adjacent-bound assertions, initialization after
that clear, and a defined response to USB reinitialization. The gaps do not
form a generic heap.

The RP2040 contains an eight-region MPU, which the port programs to fence the
user window from the kernel (sys/arch/rp2040/doc/MPU.md), so an operating-system
claim concerns configuration rather than hardware absence. USB DPRAM is execute-never under the default map, but
the hardware can change the relevant attributes; the program keeps executable
flash-recovery code in ordinary SRAM.

Pool-only projections retain every other linked object and alignment:

| SwapRAM pool | SMALL window | LARGE window | Projected RAM end | Projected headroom |
| ---: | ---: | ---: | ---: | ---: |
| 16 KiB | 144 KiB | 160 KiB | `0x2002BCE0` | 76,576 B |
| 32 KiB | 144 KiB | 176 KiB | `0x2002FCE0` | 60,192 B |
| 48 KiB | 144 KiB | 192 KiB | `0x20033CE0` | 43,808 B |

The telemetry proposal changes the 32 KiB projection:

| Proposed allocation | Remaining projected headroom |
| --- | ---: |
| 32 KiB pool before new instrumentation | 60,192 B |
| Add 480 live 32-bit erase counters | 58,272 B |
| Add a separate 1,920-byte frozen counter snapshot | 56,352 B |

The last value excludes aggregate counters, record headers, saturation state,
alignment, code growth, watchdog state, and every other instrumentation
object. A target link result replaces each projection.

PICO enables `SWAPRAM` with `SWAPRAM_KB=16`. PICO_UART omits `SWAPRAM` and
must retain that expected absence in its provenance gate. At 64 KiB, the pool
exceeds the 65,535-byte compact-offset limit and changes SwapRAM record fields
from `u_short` to `unsigned int`.

## Execution classes

Every task declares one of these classes. A row that needs more than one class
uses separately gated child actions.

| Class | Permitted state effect | Authorization boundary |
| --- | --- | --- |
| inspection | reads source, artifacts, refs, and retained evidence | ordinary repository inspection |
| local-source-write | edits an isolated worktree | explicit implementation request |
| local-build-write | creates generated files or build artifacts locally | explicit implementation request |
| board-read | observes the running board without reset or flash writes | exact board opt-in when a device path is opened |
| board-reset-write | flashes, resets, injects a processor stall, or writes the board filesystem | separate exact opt-in |
| power-interruption | removes or changes MCU or flash power during a declared transition | separate attended authorization and fixture |

A timestamp can prompt investigation but cannot invalidate content by itself.
Artifact provenance follows source and generated inputs, configuration,
compiler commands, toolchain identity, and content hashes. A byte-identical
rebuild from pinned inputs can establish reproducibility despite normalized or
old timestamps.

## Dependency graph

```text
A01 inspection -> A01 build reproduction
                 |-> A03 watchdog contract -> H01 host model -> T04 linked XIP audit
                 |                                      |-> T03-16 -> W01 localization
                 |-> H02 counters -> H08 snapshots -> T01 16 KiB baseline
                 |                         |                 |-> T02 32 KiB build
                 |                         |                 |-> T03-32 -> W02 comparison
                 |-> H03 NOR oracle -------+
                 |-> H04 authority model -> H05 format -> H06 publication -> H07 restore
                 |                                      |                  |-> H09 injections
                 |-> A04 durability map ------------------------------------+-> W03 power matrix
                                                                    W04 controls W02 and W03

H10 RAM-staged rewrite activates only after H02/H03 measure material rewrite traffic
and an explicit failure-policy decision accepts the changed recovery surface.
```

## Active frontier

| ID | Priority | Mechanism | State | Execution class | Dependencies |
| --- | ---: | --- | --- | --- | --- |
| A01-I | 0 | submitted-artifact provenance inspection | partial | inspection | -- |
| A01-B | 0 | configuration-specific artifact reproduction | open | local-build-write | A01-I |
| A02-I | 1 | architecture-text conflict inventory | partial | inspection | A01-I |
| A02-W | 1 | architecture-authority reconciliation | partial | local-source-write | A02-I |
| A03 | 0 | watchdog coverage and timing contract | partial | inspection | A01-I |
| A04 | 2 | filesystem-to-Dhara durability call map | partial | inspection | A01-I |
| H01 | 0 | watchdog state and progress model | open | local-source-write, local-build-write | A03 |
| H02 | 0 | attempted/returned/verified flash telemetry | open | local-source-write, local-build-write | A01-B |
| H03 | 0 | interrupted-prefix NOR operation oracle | partial | local-source-write, local-build-write | H02 |
| H04 | 1 | SwapRAM reservation and authority model | partial | local-source-write, local-build-write | A01-I |
| H05 | 2 | portable compressed-swap record | blocked | local-source-write, local-build-write | H03, H04 |
| H06 | 2 | transactional compressed-image publication | blocked | local-source-write, local-build-write | H05 |
| H07 | 2 | safe mixed-format restoration | blocked | local-source-write, local-build-write | H06 |
| H08 | 0 | coherent counter snapshot and export | blocked | local-source-write, local-build-write | H02 |
| H09 | 1 | runtime, processor-reset, and power-loss injection oracles | blocked | local-source-write, local-build-write | H04, H08 |
| H10 | 3 | conditional RAM-staged ordinary rewrite | conditional | local-source-write, local-build-write | H02, H03, failure-policy decision |
| T01 | 0 | frozen instrumented 16 KiB PICO baseline | blocked | local-build-write | H01, H02, H03, H08, T04 |
| T02 | 1 | controlled 32 KiB PICO build | blocked | local-build-write | T01, H04 |
| T03-16 | 0 | 16 KiB bounded kernel-stack argument | blocked | local-build-write | T01 |
| T03-32 | 1 | 32 KiB bounded kernel-stack argument | blocked | local-build-write | T02, T03-16 |
| T04 | 0 | watchdog-linked XIP-off dependency audit | blocked | local-build-write | H01 |
| R01 | 0 | running-board artifact identity | open | board-read | A01-B, exact device opt-in |
| W01 | 0 | watchdog and UART failure localization | blocked | board-reset-write | R01, T03-16, T04, H09 |
| W02 | 1 | controlled 16-versus-32 KiB comparison | blocked | board-reset-write | W01, T03-32, W04 |
| W03 | 2 | Dhara persistent-state reset matrix | blocked | board-reset-write, power-interruption | A04, H09, W04 |
| W04 | 0 | initial-state and perturbation control | blocked | inspection, board-read | H08, R01 |

## Contract details

### A01 -- configuration-specific provenance

A01-I records the submitted snapshot, configuration files, generated files,
toolchain, compiler commands, existing artifact hashes, and worktree state.
A01-B reproduces artifacts in an isolated worktree without destroying the
watchdog draft or unrelated untracked files. The watchdog record includes its
base commit, tracked diff, and relevant untracked inputs before integration.

The PICO gate requires evidence that `SWAPRAM` and `SWAPRAM_KB=16` reached the
relevant compilation units, that the linked pool has the configured size, and
that the final `swapout` path calls `swapram_out`. The PICO_UART gate requires
the expected absence of the pool, SwapRAM objects, and call path. Configuration
text, actual compiler command lines, symbols, and disassembly remain separate
evidence surfaces.

Completion requires an explained source-to-hash chain for each artifact and a
rebuild whose differences, including byte identity when expected, are
accounted for. An unexplained artifact or mismatched feature set falsifies the
gate. A timestamp alone does not.

### A02 -- architecture-authority reconciliation

A02-I inventories conflicting descriptions without editing them. A02-W changes
the canonical owner in an isolated worktree and updates the documentation map.
The inventory covers SRAM4/SRAM5 ownership, PICO versus PICO_UART SwapRAM,
NOLOAD storage, MPU capability versus configured isolation, linked-size
claims, and the ascending swap-image write path.

Completion requires source, linker, configuration, and documentation to state
one compatible ownership model. Generated size reports replace supposedly
current constants in long-lived architectural prose.

### A03 and H01 -- watchdog coverage, timing, and breadcrumbs

The hardware watchdog and the progress policy are distinct. A periodic timer
reload detects loss of timer service. It does not detect an operation that
stalls while the timer interrupt continues. H01 therefore carries two controls:

1. A masked-interrupt stall must expire because the reload owner cannot run.
2. A selected operation that stops making progress while the timer continues
   demonstrates the uncovered class for an unconditional clock reload.

An idle kernel remains healthy without USB traffic or process creation. A
later progress watchdog must name the bounded operation and deadline rather
than treating activity as health.

RP2040-E1 makes the counter decrement twice per watchdog tick. A nominal
one-microsecond tick and 24-bit load field produce an approximately 8.39-second
maximum interval. The configured deadline must satisfy:

```text
maximum measured legitimate reload gap + declared margin < deadline
```

The measurement includes back-to-back erase/program operations. Configuration
outside the proven range fails instead of silently changing the timeout.
Watchdog countdown control must preserve the shared tick generator used by
system timing.

Breadcrumb publication records reset reason and old scratch contents before
overwriting them. A generation or validity convention distinguishes complete,
interrupted, and stale site/argument updates. Tests reset between every scratch
write. Scratch0 remains the swap cursor; scratch4--7 remain reserved for boot
ROM reboot conventions. Reports call the breadcrumb the last recorded phase,
not a faulting PC or root cause. The record also identifies timer expiry,
deliberate watchdog reset, and debug-pause settings.

RUN-pin reset and digital-core power cycling clear watchdog scratch. A test
that needs retained breadcrumbs uses a scratch-preserving reset path and
records the method.

Completion requires positive controls for the covered timer-loss class and
counterexamples for live-timer stalls. Unsupported failure classes remain
listed as uncovered.

### A04 -- filesystem and Dhara durability boundary

The call map starts at `fsync()` and `sync()`, not at `flstrategy()`.
`fsync()` invokes `syncip()` and, after success, `ufs_sync()` for the mount.
`ufs_sync()` covers inode updates, dirty buffers, superblock state, and the
mount's recorded write error. The map records immediate errors, retained mount
errors, and the event that clears each error.

A deferred-checkpoint design supplies an explicit flush operation. The
decisive host test begins with clean filesystem buffers and pending Dhara map
changes, then invokes the durability operation without manufacturing another
data write. Empty files, unchanged files, applicable directories, and prior
write failures receive defined results.

Completion requires a declared acknowledgment boundary even when ordinary
writeback generates zero new device requests. Host-side evidence capture must
not write its own result into the filesystem under test.

### H02 and H08 -- telemetry and coherent export

H02 distinguishes three states for each operation:

- attempted: the wrapper invoked the ROM operation;
- returned: the ROM call returned through the wrapper;
- verified: a named readback or integrity mechanism checked the result.

The current wrappers perform no independent readback, so a returned operation
does not become verified. Interrupted operations retain their observed prefix
instead of becoming a confirmed success or confirmed no-op. Records carry
offsets and lengths. The host derives page equivalents and geometry estimates
without equating one C call with one undocumented internal flash command.
Verified-byte and verification-failure counters exist only beside a mechanism
that actually performs that verification.

The initial counter set includes SwapRAM admission and fallback reasons,
compressed and expanded bytes, pool free bytes and largest extent, SMALL/LARGE
residency, erases by physical sector, ROM program calls and byte lengths,
Dhara synchronization calls, checkpoints, and garbage collection. Per-sector
32-bit erase counters saturate rather than wrap silently.

H08 serializes every counter writer. The generation becomes odd only around a
short counter update and returns even before a flash operation, sleep, copy, or
UART transmission. The writer protocol supplies compiler and hardware
ordering; `volatile` alone is insufficient. The reader runs in a context that
cannot preempt a writer and then spin while preventing its completion.

The first implementation reserves a named static snapshot buffer outside the
u-area stack. A reader copies the live record, validates the generation, and
only then transmits the frozen copy. It bounds retries and returns a framed
`snapshot busy` result instead of looping indefinitely. The exported record
contains schema version, payload length, run identifier, generation, units,
saturation state, and an integrity check.

Completion requires deterministic interleaving or correctly synchronized host
tests for interrupted writes, concurrent snapshot requests, saturation, and
retry exhaustion. A receiver must never accept an incoherent record as valid.

### H03 -- NOR oracle at matching abstraction levels

The existing successful ordinary-rewrite result remains:

```text
per affected destination sector:
  two 4 KiB erase calls
  thirty-two 256-byte program calls
  8,192 programmed bytes
```

The oracle adds failure after each erase and programmed page, torn-program and
torn-erase outcomes, empty requests, reserved-sector collisions, arithmetic
overflow, invalid offsets, and invalid lengths. Invalid requests fail before a
destructive callback. Interrupted traces preserve their observed prefixes.

Telemetry and the model must compare attempted calls with attempted calls,
returned calls with returned calls, and verified contents only where a
verification mechanism exists. Geometry-derived counts remain estimates of
external command traffic, not measurements of undocumented flash internals.

### H04 -- reservation is not authority

The pinned evacuation reserves all expanded destinations before writing. It
stores provisional addresses in `p_daddr`, `p_saddr`, and `p_addr` while the
RAM entry remains live. Nonzero addresses alone therefore do not make flash
authoritative.

The audit names the discriminator every reader uses. The state model separates:

1. allocation rollback when a reservation fails;
2. per-image publication after required bytes and integrity complete; and
3. whole-evacuation behavior after a media failure moves some earlier images.

All-or-none allocation does not imply all-or-none I/O. A supported mixed,
retryable, or fatal outcome after partial evacuation requires an explicit
policy. Completion requires every intermediate state to have one owner and
every reader to select only a complete authoritative image.

### H05 -- portable compressed record

The on-flash record defines byte order, encoded widths, alignment, magic,
version, codec and parameters, stored lengths, expanded lengths, integrity,
and unknown-flag handling. Native C padding is excluded from the format.
Metadata integrity covers destination lengths as well as payload bytes.

The writer constructs the sequential representation before programming. It
must not finalize a header with a second `flash_swap_append()` call at the
record's sector boundary because the helper erases on that boundary.

Tests cover zero-length segments, allocation grain, program-page and
erase-sector boundaries, maximum lengths, and overflow-adjacent values. The
completion claim is bounded: exhaustive checks over a declared reduced domain,
boundary tests, generated properties, and fuzzing of the full parser. Ordinary
tests do not justify the phrase "every valid record."

### H06 and H07 -- publication and restoration

The compressed eviction order is:

```text
reserve destination
-> write complete representation
-> satisfy completion and integrity policy
-> publish authoritative descriptor
-> release RAM image
```

Failure injection runs before and after every transition. Allocation failure
retains the original RAM image. Publication never precedes complete data, and
RAM release never precedes publication.

Restoration validates record type, lengths, destination bounds, metadata, and
payload before decoding into process state. The authoritative swap source
remains intact until restoration completes and process context can be safely
published. Decode failure cannot return through a partly restored stack or
u-area. Mixed raw and compressed images retain an unambiguous discriminator.

The runtime contract applies within one boot. It does not claim process-image
restoration after reset.

### H09 -- three injection domains

| Injection class | Required result |
| --- | --- |
| returned error or interrupted runtime transition | preserve or transfer authority under the runtime transaction contract |
| watchdog or processor reset | recover diagnostics and restart safely; volatile process authority need not survive |
| actual MCU or flash power interruption | recover within the declared persistent-storage boundary |

Each manifest names the reset components, scratch-retention expectation,
external-flash supply state, flash busy state, and externally observed timing.
A RUN-pin pulse is a processor reset, not a power-loss test. The harness
captures volatile diagnostics before an action that clears them.

Persistent-storage tests define a set of permitted recovered states. An
unsynchronized update may survive when Dhara reached an automatic
synchronization point; disappearance is not the only permitted outcome.

### H10 -- conditional RAM-staged ordinary rewrite

H10 activates only when H02/H03 show material ordinary-rewrite traffic and an
explicit decision accepts the changed failure surface. A named 4 KiB main-SRAM
buffer would read the destination sector, merge replacement bytes, erase the
destination once, and program sixteen pages. The calculated successful-path
cost becomes one erase and 4,096 programmed bytes per affected sector, a 50
percent reduction for that path alone.

The existing spare-sector algorithm retains a flash copy while rebuilding the
destination. RAM staging loses that copy across power loss. The experiment
must establish the existing recovery property and obtain an architecture
decision before changing it. The append path, spare-sector allocation, pool
size, and allocator remain unchanged during the comparison.

The experiment also tests a full-sector replacement fast path whose source
remains accessible while XIP is unavailable. That path does not read bytes that
the replacement fully covers.

### T01 and T02 -- frozen comparison builds

T01 freezes the accepted watchdog, telemetry, snapshot, NOR-oracle, and XIP
dependency configuration in a 16 KiB PICO build. T02 changes only
`SWAPRAM_KB` to 32 within that configuration. Both builds record source and
generated-input hashes, compiler commands, toolchain, ELF/UF2 hashes, linked
sections, symbol sizes, and disassembly.

The 32 KiB acceptance value comes from the target link. The arithmetic
expectation is 56,352 bytes after the live and frozen 1,920-byte arrays, before
the remaining instrumentation. A discrepancy becomes a finding to explain,
not an automatic defect. PICO_UART remains a separate no-SwapRAM provenance
control rather than a pool-size comparison arm.

### T03-16 and T03-32 -- bounded kernel-stack argument

Per-function `-fstack-usage` results do not form a call-chain bound. T03 maps
the declared call chains through SwapRAM, flash, telemetry, watchdog service,
filesystem synchronization, error reporting, interrupt entry, and exception
entry. The map accounts separately for assembly, indirect calls, ROM routines,
compiler helpers, dynamic stack classifications, and uncovered paths.

The available interval is the proven-unused part of the 3,072-byte u-area,
not the entire region. Watermark initialization must not repaint an active
stack or overwrite `struct user`. Results distinguish process0 from ordinary
processes and remain observations for the executed paths.

T03-16 closes before W01 exercises diagnostic firmware. T03-32 closes before
W02 compares pool sizes. Each gate retains a preregistered absolute reserve
over the declared call and interrupt model. An unexplained disagreement
between static and watermark results remains open.

### T04 -- linked XIP-off dependency audit

The flash wrappers mask interrupts, exit XIP, call ROM operations, flush,
restore XIP through the RAM-resident boot2 copy, and restore interrupt state.
T04 starts at the exact linked watchdog-integrated image and classifies every
reachable dependency during that blackout.

The audit includes direct and indirect calls, compiler arithmetic helpers,
copy routines, literal pools, read-only tables, format strings, assertions,
atomic-library calls, resolved ROM routines, MMIO, and `boot2_copy`. Every
target resolves to an explicitly permitted RAM, ROM, or MMIO access.
Unclassified indirect calls remain unresolved.

The blackout path stays bounded and allocation-free. It records compact state
directly and performs formatted output, filesystem logging, and UART draining
only after XIP and interrupt service return. The report also states the fault
and exception policy; masking ordinary interrupts does not prove that an
exception handler can execute safely while XIP is unavailable.

### R01 and W01 -- board identity and failure localization

R01 records the serial-by-id path, USB identity, board and ROM revision, flash
identity, kernel and filesystem hashes, configuration, toolchain, reset method,
and instrumentation schema before a run. A banner or source checkout alone
does not identify the flashed bytes.

W01 first injects a bounded masked stall and demonstrates the declared
watchdog coverage. It then demonstrates the live-timer counterexample. Only
after those controls pass does it replay the retained USB workload with UART
as the independent observation path. The USB reset interface is part of the
failing subsystem and supplies no independent evidence.

The investigation records one of four outcomes:

- reproduced and localized;
- reproduced but unlocalized;
- not reproduced within the declared exposure; or
- blocked by inadequate instrumentation.

Three independent unsuccessful falsification attempts form an
investigation-budget boundary. They neither confirm a hypothesis nor prove a
fix.

### W02 and W04 -- controlled pool-size comparison

W04 records the swap allocation cursor, free-map shape, pool state, Dhara
state, cache state, filesystem state, and preconditioning workload. The run
order is counterbalanced. Setup and image-restoration traffic stays outside
the workload interval where appropriate but remains in cumulative wear
accounting.

The comparison contains one common workload that both pools admit and a
separate capacity-boundary workload admitted only by the larger pool. Refusal
by 16 KiB is capacity evidence, not a directly comparable execution-time
sample. Equivalent runs use identical instrumentation, inputs, and export
timing. Snapshots export after the measured interval unless export overhead is
the subject of the test.

A bounded minimal-versus-instrumented comparison records instrumentation
changes in code placement, latency, interrupt blackout, UART traffic, and
operation counts. Each run receives an identifier and records elapsed
exposure, sessions, bytes transferred, swaps, mode transitions, and
synchronization events. Three repetitions are a minimum reproducibility check,
not a reliability guarantee.

### W03 -- persistent-state recovery

W03 uses H09's separate reset and power-interruption oracles on a disposable,
identified filesystem image. It injects around Dhara map updates, automatic
and explicit synchronization, checkpoint progress, garbage collection,
acknowledged `fsync`, and acknowledged `sync`. Each result checks translation
map recovery and filesystem consistency separately.

Completion requires the recovered state to lie inside the preregistered set
permitted by the exact acknowledgment and synchronization history. A clean
read-only filesystem check alone does not establish the durability contract.

## Experiment matrices

### SwapRAM pool and epoch

| Pool | Epoch or transition | Workload | Required observations |
| ---: | --- | --- | --- |
| 16 KiB | SMALL | compressible churn | admission, occupancy, largest extent, physical writes |
| 32 KiB | SMALL | identical churn | same metrics and delta from 16 KiB |
| 16/32 KiB | SMALL -> LARGE | live RAM images | expanded reservations, writes, publication, rollback |
| 16/32 KiB | LARGE | parent, inheriting child, swapped holder | entitlement count and pool-closed duration |
| 16/32 KiB | LARGE -> SMALL | last holder exits | reopening and later RAM admission |
| 16/32 KiB | spool active | LARGE requested | deferral, wakeup, and spool ownership |

### Compression and flash fragmentation

| Compressibility | Flash-map state | Required result |
| --- | --- | --- |
| high | contiguous capacity | expanded reservation succeeds |
| high | fragmented but sufficient total | contiguous rule may refuse; RAM remains authoritative |
| high | insufficient total | all provisional extents roll back |
| low | contiguous capacity | larger reservation and physical-write count |
| low | fragmented | bounded refusal without leaked extents |
| mixed images | failure after earlier publication | declared mixed, retryable, or fatal policy |

### Watchdog controls

| Control | Timer service | Selected operation | Required interpretation |
| --- | --- | --- | --- |
| masked stall | stopped | stopped | watchdog expires under covered policy |
| live-timer stall | running | stopped | unconditional timer reload demonstrates uncovered class |
| idle system | running | idle | healthy without synthetic activity |
| consecutive flash calls | masked during calls | completes | measured legitimate reload gap remains below deadline |

### Fault domains

| Point | Runtime error | Processor reset | Flash power interruption |
| --- | --- | --- | --- |
| before reservation | original authority | safe restart | prior durable state |
| after reservation | rollback or retry | volatile map discarded | prior durable state plus permitted flash effects |
| during payload | observed prefix and retained authority | diagnostic restart | modeled torn operation |
| after payload, before publish | original authority | diagnostic restart | complete but unpublished record policy |
| after publish, before RAM release | new authority | diagnostic restart | published durable record policy |
| during Dhara checkpoint | returned error where possible | map resume policy | permitted synchronization-point state |

## Promotion gates

The program promotes only independently supported results.

The recovery track closes when the linked target contains the accepted
watchdog paths, the XIP-off audit classifies every dependency, positive and
negative controls demonstrate the declared coverage, and UART remains an
independent evidence path.

The measurement track closes when writer serialization, frozen snapshots,
saturation, abstraction-level labeling, NOR fault prefixes, and exported
record integrity pass calibrated tests.

The 32 KiB pool promotes when T01 and T02 differ only by the declared pool
configuration, both stack gates close, ownership and rollback remain correct,
and controlled board workloads demonstrate the declared capacity or
physical-write benefit. Successful linking alone carries no promotion.

Compressed flash eviction, Dhara checkpoint batching, and RAM-staged ordinary
rewrite each require separate review, evidence, and board decisions. A result
from one mechanism cannot promote another.

## Stop and report conditions

Stop the affected track and report when:

- artifact identity or configuration-specific provenance remains unresolved;
- the watchdog cannot recover from its positive control;
- the live-timer counterexample contradicts the declared coverage;
- an XIP-off dependency reaches unclassified flash content or unbounded work;
- stack analysis leaves an unclassified call, interrupt, or exception path;
- telemetry and the NOR oracle compare different abstraction levels;
- a snapshot can livelock or transmit incoherent bytes as valid;
- an evacuation or restoration state lacks one authoritative owner;
- a reset test expects volatile process state to survive without a restoration
  mechanism;
- a durability result falls outside the preregistered permitted set; or
- a measurement contradicts the RP2040, ARM, flash, or Dhara specification.

## Deferred capacity mechanisms

The 1,200 USB DPRAM bytes remain available for linker-owned objects with
explicit initialization and invalidation. XIP cache-as-SRAM remains deferred
because the kernel executes from cached external flash. Register/FIFO
scavenging remains deferred because operational state is not malloc-compatible
memory. Core1, PIO, and DMA require a measured latency, throughput, or protocol
need rather than a capacity claim.

## Primary references

- RP2040 datasheet, sections 2.4.1, 2.13.4, 4.7, and erratum RP2040-E1:
  <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- Reproducible Builds, `SOURCE_DATE_EPOCH`:
  <https://reproducible-builds.org/docs/source-date-epoch/>
- Linux sequence-counter contracts:
  <https://docs.kernel.org/locking/seqlock.html>
- GCC volatile ordering:
  <https://gcc.gnu.org/onlinedocs/gcc/Volatiles.html>
- GCC stack-usage output:
  <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>
- Dhara synchronization and recovery:
  <https://github.com/dlbeer/dhara>

Repository evidence starts with `docs/research/invariant-map.md`,
`docs/research/memory-ownership-plan.md`,
`sys/arch/rp2040/doc/research/zswap.md`, `sys/arch/rp2040/dev/flash.c`,
`sys/arch/rp2040/dev/flash_swap.c`, and
`sys/arch/rp2040/rp2040/swapram.c` at the submitted snapshot.
