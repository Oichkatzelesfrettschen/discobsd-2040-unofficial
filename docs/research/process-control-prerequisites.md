# Process-control prerequisites

The DPUCP-2024 process-control tranche repairs interval-timer validation and
the public `waitpid()` type contract without adding `timeout`. The work
establishes partial host and cross-build evidence for prerequisites while the
`timeout` requirement remains open. The profile makes no POSIX or UNIX
conformance claim.

The clean comparison uses base commit
`b7d570fd9bc1a4a84532f0011745d76730894bc2`. The machine-readable companion
identifies the exact candidate production sources by SHA-256 because the
measurement preceded the evidence commit. The measured host ran Linux
7.2.9-2-cachyos on x86-64 with bmake 20260912, GNU Arm Embedded GCC 16.2.0,
GNU binutils 2.47.20260726, host GCC 16.2.1, and Python 3.14.7.

## Interface and kernel boundaries

`waitpid()` and `wait4()` now use `pid_t` in the public header.
`waitpid()` uses a strict-C17 definition and forwards the process selector,
status pointer, option bits, null resource-usage request, result, and
`errno` behavior to `wait4()`. The native contract performs eight checks.
An old-style-definition mutation must fail strict C17 compilation, and a
changed-selector mutation must fail the forwarding assertion. The
Cortex-M0+ gate compiles the exact wrapper with the target ABI flags.

`setitimer()` validates both `itimerval` members before changing timer
state. The checks reject negative seconds, microseconds outside the valid
range, whole-second CPU-timer overflow, fractional overflow at the exact tick
limit, and interval overflow. A failed old-timer copyout stops the operation
before the new value is installed. A successful old-value copyout uses a
separate buffer and preserves the original new-value pointer. The
timer/I/O/storage harness performs 149 assertions, and three source mutations
calibrate the added failure paths and pointer ownership.

The generated PICO and PICO_UART Makefiles compile `kern_time.c` through the
shared size-oriented rule. A calibrated generated-file check requires both
variants to retain that rule. The rule offsets the validation code without
changing the kernel ABI or removing failure handling.

## Supervised build evidence

The caller selected and exported `PYTHON` before every build and gate. Four
terminal receipts identify the clean distribution, focused gates, aggregate
build, and aggregate check. Each receipt records the exact command, process
identity, start time, elapsed time, child-output byte count, final exit status,
last output, and terminal state. The supervisor retains complete stdout and
stderr, emits a heartbeat every 30 seconds, stops a command after 600 seconds
without output, and enforces a hard deadline. Every measured receipt reports
`state=completed`, exit status zero, and zero terminal-queue dropped bytes.

| Receipt | Command | Elapsed | Logged bytes | Hard deadline |
| --- | --- | ---: | ---: | ---: |
| clean distribution | `bmake MACHINE=rp2040 cleanall distribution` | 139.546 s | 1,670,547 | 3,600 s |
| focused gates | lint, comment, generated-file, storage, libc, cross, C17, profile, and donor gates | 56.734 s | 98,395 | 1,800 s |
| aggregate build | `bmake MACHINE=rp2040 build` | 85.727 s | 846,002 | 3,600 s |
| aggregate check | `bmake MACHINE=rp2040 check` | 456.902 s | 763,098 | remaining 3,514.271 s |

The aggregate check emitted heartbeats during quiet lint intervals at 30 and
60 seconds. Those heartbeats distinguish a live quiet process from a stalled
runner. The hard deadline prevents a silent command from retaining a session
for four hours. The receipt hashes in the machine record bind the observed
terminal states without committing workstation-local paths.

The replayable aggregate entry point is:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-check
```

`logged-check` runs `build` before `check`, stores separate stage logs and
statuses, and propagates either stage failure. A missing tool remains an unrun
gate rather than a passing gate.

## Constrained surfaces

GNU `arm-none-eabi-size` supplies ELF section sizes. `unix.bin` supplies
the loaded flash span, including alignment. The 128 KiB region arithmetic
uses the loaded span rather than the smaller section sum.

| Surface | Base | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO text | 104,598 | 104,518 | -80 | n/a |
| PICO data | 248 | 248 | 0 | n/a |
| PICO BSS | 39,848 | 39,848 | 0 | unchanged |
| PICO loaded flash span | 104,848 | 104,768 | -80 | 26,304 bytes |
| PICO_UART text | 92,172 | 92,092 | -80 | n/a |
| PICO_UART data | 192 | 192 | 0 | n/a |
| PICO_UART BSS | 14,832 | 14,832 | 0 | unchanged |
| PICO_UART loaded flash span | 92,364 | 92,284 | -80 | 38,788 bytes |
| `kern_time.o` text | 1,264 | 1,180 | -84 | n/a |

The clean builds contain 269 OMAGIC executables in each tree. Every executable
preserves text, data, BSS, loaded size, and raw size. `games/warp/warp`
remains the largest process at 101,300 loaded bytes and retains 46,156 bytes
before stack use in the 147,456-byte process window.

`process-control-programs-parent.tsv` and
`process-control-programs-candidate.tsv` retain the sorted 269-row
denominators. Each row records the repository-relative executable path, text,
data, BSS, loaded and raw byte counts, and image SHA-256. The donor verifier
authenticates both tables, requires identical path sets, recalculates every
size and hash comparison, and derives the largest process from the candidate
rows.

One executable hash differs. `usr.bin/pdc/pdc` embeds
`23:28:57 Oct 17 2026` in the base and `23:46:37 Oct 17 2026` in the
candidate. The five measured size fields remain equal. The differing
compiled-time string does not represent a process-image size change.

Both packed roots contain 95 files, 690 data blocks, and 289 free 1 KiB
blocks. The nine inode-list blocks preserve
`690 + 289 + 9 = 988`. The candidate retains 295,936 free bytes.

The full ELF libc remains byte-identical at 410,054 bytes. The installed a.out
libc retains 371 ordered members and 114,458 bytes. The reduced board archive
retains 152 ordered members, including its symbol table, and 43,010 bytes.
Both a.out archives preserve their ordered member-list and extracted-payload
manifest hashes. Their container hashes differ because archive metadata
records rebuild time. The extracted `waitpid.o` payload is byte-identical
between the two trees.

The zero-growth rule therefore passes on kernel data and BSS, both kernel
loaded spans, all process images, both libc payload sets, and the complete
packed root. The kernel text and `kern_time.o` text decrease. No exception
record is required.

## Verification and residuals

The focused run covers ShellCheck for 132 scripts, Ruff for 130 files,
generated-Makefile synchronization, 149 timer/I/O/storage assertions, all 19
kernel mutations, the native and Cortex-M0+ waitpid contracts, finite C17
accounting, the 1,518-row DPUCP profile, and the 11-candidate donor ledger.
The aggregate adds host, ILP32, sanitizer, cross, Qemu-user, Smaller C,
host-package, and board-build classes. Build and board-build evidence does not
establish physical execution.

The machine record validator checks every arithmetic relation and current
source hash. Four process-control measurement mutations reject a changed
kernel delta, a nonzero program-size count, inconsistent root free space, and
changed archive payloads.

The remaining process-control work is explicit:

- `timeout` remains absent and its DPUCP requirement remains open.
- Kernel wait-status transitions, process-group forwarding, signal
  escalation, and timeout status mapping lack target execution witnesses.
- Whole-program runtime stack and heap peaks remain unmeasured.
- Renode execution remains unrun.
- Physical-board execution remains unrun and requires explicit owner
  authorization immediately before any flash or serial action.

The companion record's `retained_source_commit` identifies Git blobs whose
bytes match the recorded candidate source hashes. The donor verifier checks
those retained blobs and reports live differences separately. The recorded
measurement values and original execution receipts retain their original
scope; later source changes require separate current acceptance evidence.
