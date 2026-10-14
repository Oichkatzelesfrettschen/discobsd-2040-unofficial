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
its original bytes after packaging. The calibrated UF2 validator, now
`tools/check_rp2040_uf2.py` behind `check-uf2-images`, accepts the pair and
rejects all eight malformed metadata controls.

| Replacement image | SHA-256 | Verified payload interval |
| --- | --- | --- |
| `unix.uf2` | `02e936c6e4293a8bb645b7e8b18a7c2c3c29a8f5d0d1a7f8b86fd408460dfdb8` | `[0x10000000, 0x10019d00)` |
| `flash.uf2` | `400bd724a6fc5d42832e8a50674416d89847088387fa3801c9515e37c6b62bbe` | `[0x10020000, 0x101a0000)` |

The replacement pair requires its own explicit hardware opt-in. The prepared
files establish host packaging and image identity; board behavior remains open.

## Acceptance state

The repaired image completes roadmap item 12 for the named stdio and stream
workload. The retained board capture establishes sampled process extents,
kernel-stack and fixed-table high-water marks, allocation-failure counts and
storage-counter deltas for that execution. The capture does not establish a
process touched peak, an electrical wear count or headroom under the broader
capacity corpus. `sys/arch/rp2040/doc/CAPACITY.md` therefore keeps every fixed
limit and `USIZE` unchanged. On-device preprocessing also remains outside the
host-preprocessed Smaller C receipt.

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

## Shell shared-break repair

The account-repaired image admits operator login and a cold `streamtest`, but
the complete stdio workload followed by `streamtest` faults in libc `malloc`.
Disassembly places the HardFault after a corrupted free-list link. The shell's
`alloc`/`setbrk` arena and libc `malloc` both advance the same process break;
prompt `\\w` reaches `getwd` directory streams, prompt `\\u` reaches
`getpwuid` stdio, completion reaches `opendir`, and `exit.o` previously carries
`atexit` beside `exit`. Those archive edges let libc establish a second heap
over the shell-owned arena.

The repaired target shell uses its existing allocation-free directory reader
for completion, exposes its internal current-directory path to the prompt,
and parses the bounded user name directly from `/etc/passwd` with descriptor
I/O. Splitting `atexit` into its own libc member lets the shell retain `exit`
without pulling `malloc`. `check-sh-heap-boundary` calibrates its symbol oracle
and rejects target disassembly containing `malloc`, `calloc`, `realloc`,
`free`, `getwd`, `getcwd`, `opendir`, `readdir` or `closedir`.

The POSIX shell gate also exposes a separate here-document defect during the
repair. The lexer now retains delimiter quote state across `<<-` lookahead,
removes delimiter quotes without performing substitution, and suppresses body
expansion for backslash, single, double, partial and empty quotation. The
118-case shell run passes with 14 pre-existing expected failures.

| Shell surface | Account-repaired image | Repaired image | Change |
| --- | ---: | ---: | ---: |
| a.out text | 33,328 | 30,104 | -3,224 bytes |
| data | 2,608 | 2,412 | -196 bytes |
| BSS | 7,532 | 7,152 | -380 bytes |
| raw a.out | 35,968 | 32,548 | -3,420 bytes |
| packed root blocks | 32 | 29 | -3 blocks |

The repaired 54,086-byte selected libc archive is 172 bytes smaller than the
account-repaired archive. These linked results matter more than the source-line
count: removing one archive dependency eliminates the conflicting allocator
and reduces every byte surface of the shell.

## Repaired image receipt

The image source is base `e6baf2aa9db699d45ea4eecbfefe0b1bdfc01094`
plus source patch SHA-256
`d88b0d64871b5f702b4eda8cbe83a897a615706fffa2990ec4024fd7b7edcd16`.
GCC 16.2.0, bmake 20260912, Python 3.14.7 and picotool 2.3.1 build the pair.
The PICO USB kernel reports text/data/BSS 105142/376/39848 bytes. The root
stages the 7,840-byte `stdiocap`, 6,872-byte `streamtest` and 54,086-byte
selected libc. The account-image checker accepts locked root, empty operator,
0600 shadow and the packed set-id modes. The test manifest returns to its
original SHA-256 after packaging.

| Repaired image | SHA-256 | Verified payload interval |
| --- | --- | --- |
| `unix.uf2` | `e16ae39aa8492a8462884223369cd719e6aef6aa13e5cc96e3f24ec351815e24` | `[0x10000000, 0x10019d00)` |
| `flash.uf2` | `60f6fcd0ca64698e41adc81b4c7095ac07491acc83d3f5cd3027eb91216c9232` | `[0x10020000, 0x101a0000)` |

The committed source differs from the measured image only in the passwd
lookup behind prompt `\u` and one comment in `edit.c`. `bin/sh/pwname.c`
carries the parser out of `main.c`, requires at least one uid digit so an
empty field never matches uid 0, and stores `?` on a miss instead of the last
non-matching record's partial name. The linked shell text grows 25 bytes to
30,129; data and BSS are unchanged. The `bin/sh/tests` host test, with a
mutant for each rule, and `check-sh-heap-boundary` cover the committed
source; the board capture covers the image named above. The image bundle was
written under `/tmp` and is no longer retained, so the hashes above and the
tables below are its record.

The calibrated UF2 validator (`tools/check_rp2040_uf2.py`) accepts the two
images and rejects all eight metadata mutations. The attended helper loads both images through the unique
RP2040 PICOBOOT interface and the board re-enumerates as the unique DiscoBSD
CDC console.

## Attended board result

Operator login renders the allocation-free `operator@pico:~$` prompt. `id`
reports operator group 5 and wheel group 0; `su` renders the
`root@pico:/home/operator#` prompt. The exact workload script is read back
before execution. A deliberately overlong bulk serial transfer truncates
`machdep.storage_stats` to `machdep.storage_sts` and returns status 1; the
capture retains that rejected transport control. Pacing the script one line at
a time produces an exact readback and all 43 expected status-zero records.

The sampled process extents are:

| Probe | Observed delta |
| --- | ---: |
| live 8,192-byte trap frame | 6,516 bytes |
| unwound trap frame | 0 bytes |
| `fprintf`, lengths 1--256 | 752 bytes |
| `fprintf`, lengths 257--2049 | 1,304 bytes |
| `vfprintf`, lengths 1--256 | 752 bytes |
| `vfprintf`, lengths 257--513 | 1,304 bytes |
| `vfprintf`, lengths 1023--2049 | 1,300 bytes |
| `fputs`, length 1 | 724 bytes |
| `fputs`, lengths 255--256 | 728 bytes |
| `fputs`, lengths 257--2049 | 800 bytes |

Length zero reports delta zero for all three output APIs. The trap control
crosses the 8,192-byte calibration threshold; the leaf result demonstrates
the sampler's unwind blind spot. These records establish allocation extents at
the observation syscalls, rather than touched-stack peaks.

The stdio workload leaves every table peak unchanged. `streamtest` then opens
and reads all 12 streams, including slots 9--12 from `_f_morefiles`, and exits
zero. The final post-stream snapshot reports:

| Resource | Limit | Peak | Minimum free | Failures |
| --- | ---: | ---: | ---: | ---: |
| process | 25 | 8 | 17 | 0 |
| inode | 24 | 22 | 2 | 0 |
| file | 24 | 15 | 9 | 0 |
| clist | 32 | 22 | 10 | 0 |
| buffer | 4 | 3 | 1 | 0 |

The kernel stack watermark reaches 1,828 of 2,048 bytes and leaves 220 bytes
untouched without saturation. The 220-byte observation is narrow headroom;
sentinel collisions, reserved unwritten frames and unexercised interrupt or
fault paths prevent a smaller `USIZE` conclusion.

The unsaturated storage snapshots bracket the stdio workload and explicit
`sync`:

| Counter | Delta |
| --- | ---: |
| root reads / bytes | 2,941 / 3,011,584 |
| root writes / bytes | 38 / 38,912 |
| root program pages / bytes | 1,216 / 311,296 |
| root erase sectors / bytes | 76 / 311,296 |
| swap reads / bytes | 240 / 1,491,648 |
| swap writes / bytes | 240 / 1,491,904 |
| swap program pages / bytes | 5,894 / 1,508,864 |
| swap erase sectors / bytes | 400 / 1,638,400 |
| map writes | 38 |
| checkpoint attempts / successes / errors | 38 / 38 / 0 |
| dirty transitions / rewrites / evictions | 2 / 2 / 0 |
| buffer waits | 0 |
| SwapRAM attempts / admissions | 83 / 3 |

A 45-second quiet interval after `streamtest` emits zero bytes. The following
prompt and capacity query remain responsive, report zero allocation failures
and contain no HardFault, panic or trap marker. The capture closes the
shared-break regression for the exercised sequence. The broader process,
inode, clist and buffer exhaustion controls, deliberate MPU fault, signal and
SwapRAM recovery corpus remain required before any capacity reduction.

## Merged-source board result

A second attended run measures the merged repair at
`93b709fc43d12022bb965dd79b1d915926ca5950`, so the committed `pwname()`
rules run on silicon. The only change to the tree is the two test-manifest
records for the staged fixtures. The build stages `stdiocap` and
`streamtest` exactly as above, sets `STORAGE_STATS=yes` for every step and
restores `distrib/rp2040/mi.rp2040` afterwards. GCC 16.2.0, bmake 20260912,
Python 3.14.7 and picotool 2.3.1 build the pair with zero warnings. The PICO
USB kernel reports text/data/BSS 105145/376/39848 bytes; the shell reports
30129/2412/7152.

| Image | SHA-256 |
| --- | --- |
| `unix.uf2` | `04c0b0868ab4fd29d11a93843bf738a822f488da43671238c8b8121548303530` |
| `flash.uf2` | `d3afc144344bb2cf149bd9bc89fd4b84187e38eba789eef6c2dd95dc04a6f219` |
| `sdcard.img` | `21cd02783cc8614f2eafbe3e1fe1168216ba263c0d4cb7518d6541357f413376` |
| selected `libc.a` | `be98affb2d058ab60e07131b177712ffc8dbb36fded12133808995e3fda97e00` |
| test manifest | `a68ca9511db55cc4eacb135850bf2a800ac9b934c5bfac2de6c63bbd5263b46c` |
| raw console capture | `7375c4ff85b1e93fa7d59d4e15b38b39707cc7e15f2746eef5626b396506876b` |

The account-image checker accepts the root. `discobsd-flash` loads both
images and the board re-enumerates as the DiscoBSD CDC console. A pyserial
driver sends one byte per 20 ms and ends each command on a shell-computed
sentinel. `board-capacity-acceptance-93b709fc.txt` holds the command output
with prompt redraws removed; the raw capture is not retained.

Operator login renders `operator@pico:~$` and `su` renders
`root@pico:/home/operator#`, so `pwname()` resolves uid 2 and uid 0 from the
packed `/etc/passwd`. `id` reports `uid=2(operator)` in groups 5 and 0, then
`uid=0(root)` after `su`. All 43 workload records report `status=0`.
`streamtest` exits zero, and the 45-second quiet interval after it emits zero
bytes. On the board, `cat <<\EOF` prints a literal `$x` and an unquoted
delimiter prints the expanded value. Tab completion extends `/et` to `/etc`
through the allocation-free directory reader. The capture contains no
HardFault, panic or trap marker.

The sampled extents match the first run within 4 bytes: trap control 6,512,
leaf 0, `fprintf` 752 and 1,304, `vfprintf` 748 to 752 and 1,300 to 1,304,
`fputs` 724 to 728 and 800, and zero at length zero. The final snapshot
reports peaks of 6 processes, 22 inodes, 15 files, 8 clists and 3 buffers,
with zero failures. The kernel stack watermark reaches 1,732 of 2,048 bytes,
316 untouched, unsaturated. Between the snapshots that bracket the stdio
workload and its `sync`, storage counters move as follows:

| Counter | Delta |
| --- | ---: |
| root reads / writes | 3,181 / 45 |
| root program pages / erase sectors | 1,440 / 90 |
| swap reads / writes | 267 / 267 |
| swap program pages / erase sectors | 6,154 / 445 |
| map writes; checkpoint attempts / successes / errors | 45; 45 / 45 / 0 |
| dirty transitions / rewrites / evictions; buffer waits | 9 / 8 / 0; 0 |
| SwapRAM attempts / admissions | 98 / 9 |

The run puts the committed source behind the first run's conclusion. It
leaves the `?` miss path unexercised, because every resolved uid exists in
the packed `/etc/passwd`; the run below covers that path. The
capacity-reduction corpus above remains open.

## Passwd lookup edge cases on the board

A separate attended run builds the same source with the same two
test-manifest records and `STORAGE_STATS=yes`. Its image differs in bytes
from the run above; both builds come from one revision and staging, so the
difference lies in build-time content such as embedded timestamps.

| Artifact | SHA-256 |
| --- | --- |
| `unix.uf2` | `90077af62ffc871b3c27a2b6ca706f80f002715dbb9ccf67fda7d25b2dcd0def` |
| `flash.uf2` | `abe48c65f002caaab513dbfc3ef8b940d7c694dbaf307db3a8b8d8693674ba38` |
| `sdcard.img` | `6d67a33fbbbd01ddc4b82c02c04d52b7c547fcfdeee3560f41ada5d889819b24` |
| `bin/sh` | `4637e7e3611a9c8e21af20188b664100061f9480e33c0fa4dd111f436b83f363` |
| raw console capture | `ef97eee9be490f9bf63489e1966f3fe6317f4ccf38c676c6ee7a1569d93787d7` |

`tools/check_rp2040_uf2.py` reports 413 kernel blocks covering
`[0x10000000, 0x10019d00)` and 6,144 root blocks covering
`[0x10020000, 0x101a0000)`. The run writes the stdio workload to
`/tmp/w.sh`, matches its board `cksum` (3188318211, 693 bytes) against the
host, and executes it: all 43 records report `status=0`, `streamtest` exits
zero and the 45-second quiet window emits no bytes.

As root, the run saves `/etc/passwd` (cksum 2009316444, 331 bytes) and
rewrites it with `ghost:*::0::/:/bin/sh` first, followed by every original
record except operator's. A fresh root `sh` then renders `root@pico`, so the
empty uid field never stands for uid 0. A background job restores the file
30 seconds later; before it does, a fresh operator `sh` renders `?@pico:~$`
for the uid that no record carries. After restoration the file's cksum
matches the saved value and a fresh operator `sh` renders `operator@pico`.
Both `pwname()` rules that the host test calibrates therefore hold on
silicon for that image. `board-capacity-acceptance-passwd-edges.txt` holds
these steps with prompt redraws collapsed; the raw capture is not retained.
