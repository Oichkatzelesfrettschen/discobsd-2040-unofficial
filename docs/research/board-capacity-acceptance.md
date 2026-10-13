# Board capacity acceptance

## Scope and deciding evidence

The C17/SUS continuation completes its hosted interface and compile-footprint
units through `kernel-uio-typed-interfaces.md`. Roadmap item 12 remains a
physical-board measurement. A smaller compiler frame establishes a local
stack reduction; a kernel watermark, process allocation extent and flash
counter each measure a different runtime surface.

| Surface | Mechanism | Interpretation and falsifier |
| --- | --- | --- |
| Process extent | `stdiocap` queries its own `KERN_PROC_PID` record before and after one operation | `p_ssize` includes the exec allocation floor and grows at syscall/signal entry. The trap control must increase the extent beyond 8192 bytes; the unwind control exposes activity between samples. |
| Process touched peak | Requires a separately calibrated paint/scan mechanism | Open. Painting below the recorded stack extent loses provenance across swap; reserved unwritten bytes and sentinel collisions also limit watermarks. |
| Kernel touched stack | `sysctl kern.capacity` | Reports the u-area watermark, including its saturation flag. `CAPACITY.md` defines its paint, sampling and collision limits. |
| Table pressure | `sysctl kern.capacity` | Records each fixed-table limit, occupancy peak, minimum free and failures. A capacity reduction also needs a calibrated exhaustion case. |
| Storage traffic | `sysctl machdep.storage_stats` with `STORAGE_STATS=yes` | Unsaturated before/after counters give workload deltas. Counter saturation rejects exact subtraction. Program/erase counts describe traffic; device lifetime wear remains a separate inference. |

`sys/arch/rp2040/rp2040/syscall.c` grows the process extent from saved user
SP. `sig_machdep.c` grows it for signal frames. `sys/kern/exec_subr.c`
initializes the allocation floor; `sys/kern/vm_swap.c` saves only the recorded
extent. These mechanisms explain why a user paint below that extent requires
additional design before a touched-peak claim.

## Sampled-stack probe

`tests/rp2040/stdio_capacity/stdiocap.c` uses the production libc and sysctl
ABI. The process record and 2049-byte output payload live in BSS so the
observation buffer stays outside the sampled stack. Each invocation executes
one operation, starts with a fresh exec floor, and emits a `sampled-stack`
record with operation, length, before, after and delta.

The calibration helper touches an 8192-byte volatile array. `trap` invokes
`getpid()` while the array remains live. `leaf` returns before another
syscall. Run both as separate processes with identical environment and invocation
paths. Both control arguments contain four bytes; retain absolute before/after
values and inspect their floors. Signals and scheduling belong in the capture. An unwind increase
requires investigation; an equal extent illustrates the sampler's blind
spot and establishes neither zero stack use nor a touched peak.

The output helper touches a common 2048-byte caller frame, keeps it live
across the library call, and writes to an unbuffered `/dev/null` stream.
The caller frame lifts the workload above the initial allocation floor.
`fprintf` and `vfprintf` must return the requested byte count; `fputs` must
return a nonnegative result. Stream errors, close errors, malformed process
records and decreasing extents fail the probe. The observation syscall has
its own frame; the before snapshot includes that observation path.

Use lengths `0 1 255 256 257 511 512 513 1023 1024 1025 2049` for each API.
The hosted `check-libc-output` fixture supplies exact byte-prefix, write-count,
short-write and failure-position oracles. The board probe supplies sampled
extent and production syscall execution. Combining the two evidence classes
preserves each class's limits.

The root `check-board-build` target compiles and converts `stdiocap` to
OMAGIC a.out. The fixture stays outside the shipped manifest. Compiler
`-fstack-usage` output and disassembly must retain both volatile calibration
frames and the live `getpid` call before a board calibration counts.

## Image preparation

Use a clean isolated worktree with the desired reviewed revision. Select and
export `PYTHON`, then run these build-only commands serially:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check
bmake MACHINE=rp2040 STORAGE_STATS=yes kernel
```

`distribution` includes `build`. A later `build` can replace the selected
staging libc with the full archive, so regenerate `distribution` before
packaging when that sequence occurs. Stage `stdiocap` from its board-build
output into the test staging `/usr/bin`, with executable mode; append its
`file /usr/bin/stdiocap` record under `filemode 0755` to the test manifest.
Retain the exact test manifest, build `bmake MACHINE=rp2040 flash`, and restore
the tracked manifest before committing. Retain the image bytes separately
before another image build overwrites them.

Record source commit and source diff, compiler/bmake/Python versions, build
commands and exit statuses, `STORAGE_STATS=yes`, capacity configuration,
ELF sizes, image SHA-256 hashes, selected libc hash and test-manifest hash.
Decode every UF2 block and verify family, address, payload and block-count
consistency. The kernel belongs in `[0x10000000, 0x10020000)`; the root belongs
in `[0x10020000, 0x101a0000)`. Raw swap occupies the remaining 384 KiB.
`flash.uf2` contains the root alone. A two-image operation loads both
`unix.uf2` and `flash.uf2`.

## Attended execution

Repository `AGENTS.md` requires explicit hardware opt-in. Build commands
prepare files; flashing and serial commands remain gated. Present the exact
retained image paths and hashes, the target board identity, root replacement
scope and capture directory before requesting opt-in. Resolve the serial
port through `/dev/serial/by-id/*DiscoBSD*`. Retain raw bytes as well as any
parsed result.

The initial workload records:

1. Boot and login transcript, image identity, `uname -a`, `df`,
   `sysctl kern.capacity`, and `sysctl machdep.storage_stats`.
2. Fresh-process `stdiocap trap` and `stdiocap leaf` controls, with statuses.
3. The three output APIs at all twelve lengths, each in a fresh process,
   followed by capacity and storage snapshots.
4. The existing `streamtest`, signal and SwapRAM fixtures when staged for
   the named test image; every fixture carries its own status and capture.
5. A synchronized final storage snapshot. Any saturation, unexpected
   allocation failure, extent regression, fault or missing result rejects
   the corresponding acceptance claim and preserves the transcript for RCA.

After opt-in, the shell workload for the stdio portion is:

```sh
sysctl kern.capacity
sysctl machdep.storage_stats
stdiocap trap
stdiocap leaf
for operation in fprintf vfprintf fputs; do
    for length in 0 1 255 256 257 511 512 513 1023 1024 1025 2049; do
        stdiocap "$operation" "$length" || exit 1
    done
done
sysctl kern.capacity
sync
sysctl machdep.storage_stats
```

The operator checks each control status before the loop and retains the
complete transcript. The control's positive result admits only sampled
extent; the leaf control and each workload result retain their stated limits.

A complete capacity-reduction decision additionally runs the workload corpus
and exhaustion controls in `sys/arch/rp2040/doc/CAPACITY.md`, including buffer
pressure and deliberate fault handling. The initial stdio workload alone
leaves that decision open. Recovery tests remain mandatory when a change
alters flash behavior. These stdio and typed-interface units preserve the
flash implementation; the board traffic measurements still remain open.

## Hosted preparation receipt

The isolated `distribution` and complete `check` commands exit zero with the
new board-build fixture. The final Smaller C suite passes all 18 cases,
including `t18_stdio`; the copied `filbuf.o` archive mutation fails and the
restored archive passes. These receipts establish hosted/cross acceptance.

The Cortex-M0+ probe links at 7656 bytes text, 200 bytes data and 2228 bytes
BSS. GCC stack-usage records 8208 bytes for `calibrate`, 2080 for `run_output`,
64 for `sample_extent`, 56 for `main`, and 12 for the variadic forwarding
helper. Disassembly retains the volatile calibration loop and its live
`getpid` relocation. Each number describes the compiled fixture, excluding
callee frames unless named; the shipped manifest excludes the fixture.

## Acceptance state

The sampled-stack fixture has a build gate; physical execution, its trap and
unwind calibration, process touched peak, table-pressure corpus and storage
traffic remain open until retained board captures decide them. The roadmap
retains item 12 as open. Neither a hosted Smaller C run nor an emulator run
closes the physical-board or on-device preprocessing boundary.
