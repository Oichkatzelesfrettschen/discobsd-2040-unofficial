# RP2040 compiler and kernel-size compatibility scope

## Decision boundary

The repair preserves the shipped flash and SRAM map: boot2 plus the kernel
occupy 128 KiB, the Dhara root occupies 1,536 KiB, raw swap occupies 384 KiB,
and one process receives a flat 192 KiB SRAM window.  The process window cannot
absorb kernel flash overflow.  A larger kernel partition, an overlay loader, or
an SRAM-loaded debug kernel constitutes a separate architecture decision.

The acceptance matrix contains `-O0`, `-O`, `-O1`, `-O2`, `-O3`, `-Og`,
`-Os`, and `-Oz`.  The kernel matrix uses system ARM GCC 16.2.0, Arm GNU GCC
13.2.1, and system Clang 23.1.4 targeting ARMv6-M.  Host-only fixtures use
system GCC 16.2.1 and system Clang.  Every supported route retains `-Wall`,
`-Wextra`, `-Werror`, `-Wshadow`, `-Wundef`, and `-Wformat=2`; a compiler-specific
flag enters the build only after a positive and negative calibration proves its
purpose and portability boundary.

System GCC 16.2.1 targets the host and therefore cannot replace an ARM cross
compiler; its compatibility surface is shared host tools and extracted
contracts.  Arm GNU 13.2.1 is an explicitly identified cross toolchain, not
evidence that a complete Pico SDK installation or a distinct SDK-bundled
compiler exists.  The evidence manifest must identify any SDK checkout,
toolchain file, and compiler binary separately or record SDK compiler coverage
as unavailable.

The final integration claim requires a frozen staged-index identity, clean
per-row builds, focused calibrated gates, `logged-build`, and one fresh
`logged-check`.  Renode and physical-board observations remain separate
evidence classes.  Physical execution uses exact image hashes and the existing
explicit hardware opt-in.

## Frozen candidate and evidence

The staged candidate is based on commit
`af2ca5997270b56fb976f3d71b2aa8dcff7aeb44`.  The index-entry stream from
`git ls-files -s` has SHA-256
`aa5c0a5e530cf0c2ebe6aaf8afea0f33428e43b1867955c476fe27148e7c5acb`.
The byte-identical scratch reconstruction lives outside the repository during
the scoping run.  Durable documentation uses repository-relative evidence
locators after implementation.

The retained aggregate receipt under
`.build-logs/rp2040-check-local-20261109T193820Z-711728-3124351d/` records a
successful build stage and a failed check stage.  The failure was a stale C17
kernel ledger.  A focused regenerated-ledger check passes, but that retry does
not replace the required final aggregate.

The first scratch matrix replay is an invalid negative control: the staged-tree
reconstruction omitted generated `tools/bin/rp2040/config` and `boot2sum`, so
`bmake -k` compiled independent objects and ended with ``all' not remade``.
Empty GCC diagnostic counts from that run do not describe compiler success.
The corrected harness checks both executable prerequisites before the first
row and records their SHA-256 values.

The corrected scratch replay compiles the frozen staged reconstruction at all
24 compiler/level combinations.  System ARM GCC and Arm GNU GCC emit zero
compiler diagnostics at every level.  Both GCC versions link at `-O`, `-O1`,
`-O2`, `-Og`, `-Os`, and `-Oz`; both exceed flash at `-O0` and `-O3`.
Clang reports the same three callback diagnostics at all eight levels.  The
fresh GCC16 true full-span excesses are 21,968 bytes at `-O0` and 5,900 bytes
at `-O3`; Arm GNU 13.2.1 excesses are 22,328 and 5,004 bytes.  These values
are 32 to 40 bytes above older retained GCC measurements because generated
version payloads differ; source optimization comparisons must fix that payload.

| Level | GCC16 text/data/BSS or ld excess | Arm13 text/data/BSS or ld excess | Clang23 |
| --- | --- | --- | --- |
| `-O0` | ld excess 21,528; true 21,968 | ld excess 21,896; true 22,328 | three callback errors |
| `-O` | 108,627 / 256 / 39,832 | 110,315 / 256 / 39,832 | three callback errors |
| `-O1` | 108,627 / 256 / 39,832 | 110,315 / 256 / 39,832 | three callback errors |
| `-O2` | 111,779 / 256 / 39,864 | 112,003 / 256 / 39,864 | three callback errors |
| `-O3` | ld excess 5,572; true 5,900 | ld excess 4,676; true 5,004 | three callback errors |
| `-Og` | 107,847 / 256 / 39,832 | 109,103 / 256 / 39,832 | three callback errors |
| `-Os` | 95,483 / 256 / 39,832 | 96,059 / 256 / 39,832 | three callback errors |
| `-Oz` | 95,483 / 256 / 39,832 | 96,059 / 256 / 39,832 | three callback errors |

## Measured compiler blockers

The latest complete Clang `-Os` attempt reaches three indirect-call failures:

- controller initialization calls an unprototyped slot with
  `struct conf_ctlr *`;
- device initialization calls the same slot with `struct conf_device *`;
- `ucall` invokes an unprototyped routine with two integer arguments.

One replacement callback type cannot describe both driver categories.  The
type-correct narrow design splits the descriptor types: controller descriptors
hold `int (*)(struct conf_ctlr *)`, and device descriptors hold
`int (*)(struct conf_device *)`.  The generator, RP2040, and STM32 declarations
move together.  An object-pointer cast is rejected because C does not make an
incompatible function type callable merely by casting its stored address.

The privileged call record uses `int (*)(int, int)`, matching both kernel
dispatches and the historical two-argument interface.  Target disassembly and
a host behavioral fixture must verify argument order, return propagation,
privilege denial, and saved-priority restoration.

The service callback remains an explicit stop point.  The generator declares
`void NAMEattach(int)`, while startup calls the function with zero arguments;
all shipped RP2040 service tables contain only sentinels.  Implementation must
choose and document one contract after tracing dormant STM32 or external
users.  Compilation success cannot invent the missing integer's meaning.

## Physical flash accounting

GNU ld's familiar FLASH overflow omits the final `.ramfunc` load bytes because
the linker script assigns `.ramfunc` an explicit load address after `.data`
without allocating that load address through `> FLASH`.  The production
admission quantity is the union of file-backed physical load intervals, ending
after boot2, text, exception tables, data initializers, and `.ramfunc`
initializers.  Non-ALLOC DWARF and NOBITS BSS do not consume target flash.

| Level | `.text` | `.ramfunc` | ld overflow | Full load span | True excess |
| --- | ---: | ---: | ---: | ---: | ---: |
| `-Os` | 94,883 | 304 | fits | 95,708 | fits |
| `-O0` | 152,048 | 440 | 21,496 | 153,008 | 21,936 |
| `-O3` | 136,091 | 328 | 5,540 | 136,940 | 5,868 |

The existing `tools/check_kernel_headroom.py` verifier already includes
`.ramfunc` through file-backed physical `PT_LOAD` intervals.  Its geometric
calibration uses margin zero: exact fit passes and one-byte overflow fails.
Release admission is a separate verdict that requires at least 1,024 bytes of
headroom.  The repair extends the existing verifier with malformed-overlap and
failed-map analysis and adds an explicit final load-end linker assertion.

## Size attribution and lawful repairs

The failed unsplit maps preserve exact section and object contributions but
omit complete local function sizes.  Adjacent global-symbol spans are upper
bounds that can include static functions, literal pools, and alignment.  The
implementation sweep retains every object, local symbol, relocation, map,
disassembly, inline report, and stack-use report even when final link fails.

The largest observed `-O0` versus `-Os` object growth occurs in pathname lookup,
USB, Dhara journal, UFS system calls, and TTY code.  Source factoring must
preserve locking, error ordering, volatile device access, namespace rules, and
power-loss behavior.  Object deltas nominate targets; only final linked load
span measures savings.

The retained `-O3` factoring experiment constrains repeated formatter and TTY
helper inlining, factors signal and exec helpers, and factors the random
sampler.  The experiment produces a 128,976-byte load span with 2,096 bytes of
headroom, while increasing `-Os` text by eight bytes and increasing `-O0`
overflow by 24 bytes.  The result proves that `-O3` duplication is recoverable;
it does not prove runtime, stack, debug, or semantic acceptability.  Each
constraint therefore becomes a one-variable trial before a minimal group is
selected.

Section splitting plus garbage collection remains investigational.  Ordinary
objects retain unused functions, but indirect roots include vectors, syscall
tables, nlist-visible addresses, device switches, line disciplines, driver and
service callbacks, file operations, timeout callbacks, assembly references,
and the complete flash-operation SRAM closure.  A symbol-name-only test cannot
detect a retained name with the wrong pointer value.

A default-COPTS `-O0` image and a debuggable fitting image are different
products.  The existing PICO profile always compiles `kern_time.c` at `-Os`,
so every matrix row names that object-level exception.  A true all-object
`-O0` experiment forms a separate measurement and cannot inherit the default
profile's size or timing claims.
Host DWARF costs zero target flash.  `-Og` and `-O` already fit in retained
measurements and may define a production debug profile after debugger-fidelity
tests.  `-fomit-frame-pointer` saves about 7 KiB at `-O0` but still leaves the
image oversized and changes frame-chain observability.  The required
default-COPTS `-O0` profile remains open until source factoring and lawful
collection recover the measured excess plus the required margin.

## Primary-source and optimization model

The implementation binds claims to the source that governs them.  The Arm
Architecture Reference Manual for ARMv6-M governs Thumb-1 encodings, literal
reach, exceptions, and architectural registers.  The Procedure Call Standard
for the Arm Architecture governs r0--r3 argument/result transport and stack
alignment.  The RP2040 datasheet, resolved through
`sys/arch/rp2040/doc/DATASHEET-INDEX.md`, governs XIP cache, bus counters,
timers, SRAM banks, DMA, PIO, and flash-interface behavior.  The GCC and Clang
version-matched manuals govern optimization, inlining, debug, target, and
warning flags.  The GNU ld manual, ELF specification, and DWARF 5 specification
govern load addresses, program headers, and non-ALLOC debug data.  Tool output
and target disassembly still decide what these compiler versions emit.

The target probe compares `-mcpu=cortex-m0plus` with the compiler-supported
combination of `-march=armv6-m` and `-mtune=cortex-m0plus`.  Acceptance checks
effective predefined macros, ELF attributes, instruction encodings, helper
calls, ABI tags, and linked bytes.  A compiler rejecting one spelling receives
an explicit profile rather than a suppressed diagnostic.

Configuration selection follows a lexicographic objective.  Correctness,
configured functionality, and failure semantics rank first.  The full load
span must then fit with at least 1,024 bytes of headroom.  Measured kernel and
process RAM admission, worst-case stack reserve, and flash-write safety rank
next.  Packed-root bytes and latency rank after those constraints.  The
measurement-contract task fixes numeric stack, RAM, counter, repetition, and
latency budgets from the unchanged control before any source trial can be
promoted; an optimization cannot choose its own budget after observing its
result.

## Static and instrumented call graphs

The static capture contains a cscope database over 486 C and header files and
a GNU cflow graph over the 78 PICO `CFILES` sources plus generated `ioconf.c`.
The full graph contains 2,509 lines.  Focused graphs cover driver configuration,
`ucall`, exec dispatch, and packed exec.  Cflow does not evaluate the PICO
preprocessor configuration and reports conditional duplicate definitions; it
also cannot resolve calls through callback tables.

The indirect-edge manifest must name and validate these families:

| Edge family | Source of truth | Required oracle |
| --- | --- | --- |
| exception vectors | linker script and vector assembly | exact slot, Thumb bit, and target value |
| syscalls | `sysent` and generated dispatch metadata | table value and callable signature |
| driver/configuration | generated `ioconf.c` and typed descriptors | category type, order, pointer identity, alive transition |
| file operations | `struct fileops` initializers | arity, flags, return/error propagation |
| timeouts | timeout queue records | typed callback and argument identity |
| exec formats | exec switch | preflight/commit order and failure boundary |
| nlist/debug roots | exported symbol table | name and address value |
| flash SRAM closure | `.ramfunc` relocations and call graph | zero flash-resident code or literal dependency during XIP loss |

Runtime instrumentation for packed and raw exec records a boot identifier,
kernel/root/program hashes, preflight and commit cycle counts, compressed and
expanded bytes, decoder state transitions, CRC updates, `rdwri` calls and
residuals, stack high-water, and XIP/cache-counter configuration.  The packed
path intentionally decodes text and data twice: preflight validates while the
old image survives, and commit writes after `exec_estab`.  Removing preflight
would exchange atomic failure semantics for speed and is outside this repair.

## Resource denominator

Every accepted configuration reports kernel load span, allocatable section
sizes, kernel static RAM, process text/data/BSS admission, process stack and
heap high-water, root blocks before and after packing, packed/raw executable
bytes, retained host-debug bytes, CPU cycles, instruction-emulation counts where
available, XIP/cache counter deltas, flash program/erase traffic, and console or
workload latency.  Each table names compiler, linker, runtime archive, source
identity, build options, configuration, and measurement environment.

RP2040 contains two Cortex-M0+ cores, 264 KiB banked SRAM, execute-in-place QSPI
flash with an XIP cache, DMA, USB device control, timers/alarms, watchdog,
interpolators, PIO, and a bus fabric with performance counters.  The shipped
kernel's actual ownership and concurrency determine which blocks form useful
measurement surfaces; hardware presence alone does not establish availability
or safe counter ownership.

## Acceptance and falsifiers

The scoping run completes when it records the measured blockers, bounded graph
evidence, and an adversarially reviewed implementation plan.  Implementation
completes only if all three required ARM compiler routes compile every matrix
level without diagnostics and every required level, including the
default-COPTS `-O0` and `-O3` profiles, links with at least 1,024 bytes of true
load-span headroom.  A remaining required overflow stays an open blocker unless
the user explicitly changes the requirement.  A size repair fails if it
removes configured functionality, changes a callback
contract without a behavioral oracle, loses a live indirect root, increases
worst-case stack beyond its measured bound, or changes packed-exec failure
semantics.

The final candidate runs focused calibrated gates, a fresh clean matrix,
`logged-build`, and `logged-check`.  An authorized board campaign follows only
after host/cross closure, records exact artifacts and controls, and restores a
verified plain image.  Any source edit after the final aggregate reopens the
affected focused gates and the aggregate.
