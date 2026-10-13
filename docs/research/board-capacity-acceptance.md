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
output into the test staging `/usr/bin`, with executable mode; append a `file /usr/bin/stdiocap` record followed
by `mode 0755`, `owner 0`, and `group 0` to the test manifest. `filemode`
belongs only inside a `default` section; per-file modes avoid changing the
existing defaults.
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
run_checked()
{
    if "$@"; then
        command_status=0
    else
        command_status=$?
    fi
    echo "status=$command_status command= $*"
    return "$command_status"
}
run_checked sysctl kern.capacity || exit 1
run_checked sysctl machdep.storage_stats || exit 1
run_checked stdiocap trap || exit 1
run_checked stdiocap leaf || exit 1
for operation in fprintf vfprintf fputs; do
    for length in 0 1 255 256 257 511 512 513 1023 1024 1025 2049; do
        run_checked stdiocap "$operation" "$length" || exit 1
    done
done
run_checked sysctl kern.capacity || exit 1
run_checked sync || exit 1
run_checked sysctl machdep.storage_stats || exit 1
```

The wrapper prints each command and its status with the shell's builtin
`echo`; the fixed workload arguments contain neither options nor backslash
escapes. Builtin reporting avoids extra utility processes inside the storage
interval. A failure ends the workload before the next command. Explicit
`sysctl` operands propagate lookup, syscall and snapshot-ABI failures; optional
node enumeration retains best-effort behavior. `check-sysctl-status` exercises
both contracts and rejects status, operand, continuation and external-reporting
mutants. `check-posix-sh` repeats the workload controls with the repository shell. The operator retains the complete transcript. The control's positive result admits only sampled
extent; the leaf control and each workload result retain their stated limits.

A complete capacity-reduction decision additionally runs the workload corpus
and exhaustion controls in `sys/arch/rp2040/doc/CAPACITY.md`, including buffer
pressure and deliberate fault handling. The initial stdio workload alone
leaves that decision open. Recovery tests remain mandatory when a change
alters flash behavior. These stdio and typed-interface units preserve the
flash implementation; the board traffic measurements still remain open.

## Snapshot command acceptance

An explicit snapshot only enters the evidence set when its producer returns
success and validates the returned ABI. `sysctl` accumulates explicit operand
failures while advancing each operand once. The utility preserves best-effort
`-a`, `-A` and subtree enumeration. The status repair also checks that
`getloadavg` returns all three samples before printing them.

`bmake MACHINE=rp2040 check-sysctl-status` compiles the exact utility source
against target headers in ILP32 host processes. Host stdio remains behind a
separate adapter; injected syscall records use the target layouts. Twenty-two
cases exercise successful snapshots, lookup errors, unavailable nodes, malformed
ABI version/size/length, load-average failure, distinct reads and assignments,
and enumeration. Mutations restoring unconditional success or repeated first
operands must fail. The documented workload gate injects failure at every one
of its 43 command positions; continuation and external status-output mutations
must fail. `check-posix-sh` supplies the repository shell independently of the
host shell. The fixture supplies only PATH, TRACE, FAIL_INDEX and LC_ALL;
Ubuntu bmake exports `.MAKE.LEVEL.ENV`, which the repository shell rejects
before executing commands when Python forwards the host environment unchanged.
Injecting that variable reproduces the startup rejection; the declared fixture
environment passes the same 43 failure positions. The firmware ILP32 execution inventory retains the fixture binary's
hash and ELF32 evidence.

Paired ARMv6-M compilation with GCC 16.2.0, `-Os`, `-Wall -Wextra -Werror`
and the build's layout defines compares the pre-repair utility at `bbccbf88`
with the repaired source. Relinking each object against the same archive and
other applet objects isolates the utility change; the adminbox comparison also
substitutes the matching sysctl object in its BSS-overlay linker script.
`arm-none-eabi-size`, `elf2aout` and `hsaout -s` give:

| Artifact | Before | After | Change |
| --- | ---: | ---: | ---: |
| sysctl object text | 4643 | 4736 | +93 bytes |
| sysctl linked text | 15312 | 15404 | +92 bytes |
| sysctl packed bytes | 13546 | 13609 | +63 bytes |
| sysctl packed root blocks | 15 | 15 | 0 |
| adminbox linked text | 24536 | 24628 | +92 bytes |
| adminbox packed bytes | 21639 | 21686 | +47 bytes |
| adminbox packed root blocks | 23 | 23 | 0 |

Data/BSS remain 1524/1528 bytes for standalone sysctl and 1716/5558 bytes for
adminbox. `-fstack-usage` reports `main` at 16 then 24 bytes; `parse` remains
1168 bytes and the enumeration body remains 1048 bytes. The correctness repair
fits the existing packed-root block budget. The measurements establish compiled
storage and frame sizes, separate from board stack peaks or flash traffic.
Inherited unrestricted name copying and output-delivery error handling remain
outside the explicit query-status contract and require separate bounded work.

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

The earlier retained test image predates the sysctl failure-status repair and
remains excluded from board admission. The earlier image uses source `21f4ae31326ca70d27e7ce89b4f81badb944f087`,
GCC 16.2.0, bmake 20260912, Python 3.14.7 and picotool 2.3.1. The optional
storage-counter PICO kernel reports text/data/BSS 105142/376/39848 bytes.
The test root adds only `stdiocap` and `streamtest`; filesystem checking reports
97 files, 716 used blocks and 263 free blocks. Extraction reproduces both
fixture payloads, their 0755 modes and the selected 54258-byte libc archive.
The tracked manifest returns to its original bytes after packaging.

| Retained image | SHA-256 | Verified payload interval |
| --- | --- | --- |
| `unix.uf2` | `1f787f843bcf69d75a1b8dc2906557abd2f7c8acc327ff3d1e3cb5bc10d3a8bf` | `[0x10000000, 0x10019d00)` |
| `flash.uf2` | `6f3e7dac895ca79d424d8e4c37552f794eecb1378ffded7ae3c2d08285367e6e` | `[0x10020000, 0x101a0000)` |

The UF2 inspection accepts a synthetic valid block and rejects eight
metadata mutations before inspecting the real images. It checks RP2040
family identity, block metadata, address bounds and complete root-region
coverage. The image hashes identify one prepared pair; a rebuild creates
its own receipt, including any kernel version or archive timestamp changes.

## Regenerated snapshot-status image

The replacement distribution uses production source `268c8c32d859cd2d350df2ca0072dc6ff1830707`.
The preparation receipt names `25261688e263215c1a1a7f6b49be099f90185e93`, whose
source delta only tightens the two host-test exit assertions. The tool versions,
STORAGE_STATS setting and kernel text/data/BSS match the earlier receipt.
The full non-hardware aggregate passes before packaging; the final exact-status
fixture and repository-shell controls also pass. Filesystem checking again
reports 97 files, 716 used blocks and 263 free blocks.

Extraction and decompression reproduce the repaired adminbox bytes. Extracted
stdiocap, streamtest and the 54258-byte selected libc match their build inputs;
the two probes and adminbox carry mode 0755. The tracked test manifest restores
its original bytes after packaging. The calibrated UF2 validator accepts the
pair and rejects all eight malformed metadata controls.

| Replacement image | SHA-256 | Verified payload interval |
| --- | --- | --- |
| `unix.uf2` | `02e936c6e4293a8bb645b7e8b18a7c2c3c29a8f5d0d1a7f8b86fd408460dfdb8` | `[0x10000000, 0x10019d00)` |
| `flash.uf2` | `400bd724a6fc5d42832e8a50674416d89847088387fa3801c9515e37c6b62bbe` | `[0x10020000, 0x101a0000)` |

The replacement pair requires its own explicit hardware opt-in. The prepared
files establish host packaging and image identity; board behavior remains open.

## Acceptance state

The sampled-stack fixture has a build gate; physical execution, its trap and
unwind calibration, process touched peak, table-pressure corpus and storage
traffic remain open until retained board captures decide them. The roadmap
retains item 12 as open. Neither a hosted Smaller C run nor an emulator run
closes the physical-board or on-device preprocessing boundary.

## Login prerequisite evidence

The attended replacement-image run reaches `login:` after both UF2 loads
and filesystem checks succeed. Entering `operator` produces `Password:`.
The source and extracted root both store operator offset 139; the named empty
operator shadow field begins at byte 127. `getpw()` seeks the stored byte
offset directly, so the stale offset selects a nonempty field. The account
repair selects byte 127. The account-image checker requires exact offsets
into uniquely named root and operator records and rejects shifted offsets,
missing or duplicate records, an unlocked root, and a nonempty operator field.

The capture establishes boot and the failed login prerequisite. Capacity,
stack calibration, and storage workloads remain unexecuted in that run. A
replacement image requires verified account packaging and an attended login
before the capacity workload supplies hardware evidence.
