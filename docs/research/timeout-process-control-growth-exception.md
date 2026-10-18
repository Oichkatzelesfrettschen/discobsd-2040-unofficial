# Timeout process-control growth exception

The DPUCP-2024 recovery image selects an Issue 8 `timeout` compatibility
subset without making a POSIX or UNIX conformance claim. The utility provides
`-f`, `-k`, `-p`, and `-s`, supervisor-led process-group forwarding,
escalation, and the selected 124/125/126/127 status behavior. The selected
adminbox composition increases two constrained root blocks and therefore needs
the measured exception recorded here.

The clean comparison uses base commit
`beba2cd6c9765a14a67dc99751f185d49a72d4ad`. The candidate sources are bound
by SHA-256 in the machine-readable companion. The clean candidate distribution
ran under the repository supervisor with a 30-second heartbeat, a 600-second
silence deadline, and a 3,600-second hard deadline. The command completed with
status zero after 120.021 seconds and retained 1,636,837 output bytes. The
runner reported zero terminal-queue dropped bytes.

## Process-control mechanism

The supervisor becomes the process-group leader before `fork()`. The child
inherits that group. Group-directed catchable signals temporarily use
`SIG_IGN` in the supervisor, and the supervisor unblocks and reblocks the
signal while ignored before restoring its handler. That sequence drains the
supervisor's pending self-directed instance and prevents a forwarding storm.
The parent keeps handled signals blocked outside `sigsuspend()` and uses a
derived wait mask so an inherited blocked `SIGALRM` cannot disable timeout
delivery. Independent deadline-expiry and escalation-timer state preserves the
deadline after external signal forwarding. A zero `-k` duration preserves
`SIGCONT` delivery and disables `SIGKILL` escalation. The child restores its
inherited mask and selected dispositions before `execvp()`.

The native oracle covers usage, duration parsing, symbolic signals, zero
duration, child status, execution errors, timeout, preserve-status,
escalation, stop-and-continue behavior, process-group delivery, inherited
signal state, external forwarding, catchable and uncatchable child-signal
status, inherited ignored `SIGCHLD`, and output silence. Fifteen source
mutations calibrate the deciding behaviors, including pre-fork `SIGCHLD`
installation, initialized signal-action flags, and separate temporary-ignore
and pending-signal-drainage controls, external-signal deadline preservation,
and zero-duration escalation disabling. The Cortex-M0+ gate compiles the exact
source as strict C17 and checks compiler-reported static frames.

## Constrained surfaces

The a.out header defines the flat process image. Adminbox uses a linker BSS
overlay, so GNU size reports every input BSS section while `elf2aout` records
the overlaid loaded extent. Both measurements remain explicit.

| Surface | Base | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,768 | 104,768 | 0 | 26,304 bytes |
| PICO data | 248 | 248 | 0 | unchanged |
| PICO BSS | 39,848 | 39,848 | 0 | unchanged |
| PICO_UART loaded flash span | 92,284 | 92,284 | 0 | 38,788 bytes |
| Adminbox a.out text | 24,580 | 26,720 | +2,140 | n/a |
| Adminbox a.out data | 1,716 | 1,716 | 0 | unchanged |
| Adminbox a.out BSS extent | 2,524 | 2,524 | 0 | unchanged |
| Adminbox ELF diagnostic BSS sum | 5,550 | 5,558 | +8 | n/a |
| Adminbox process occupancy before stack | 28,820 | 30,960 | +2,140 | 116,496 bytes |
| Adminbox packed bytes | 21,653 | 23,573 | +1,920 | n/a |
| Adminbox packed blocks | 23 | 25 | +2 | n/a |
| Largest process, `warp` | 101,300 | 101,300 | 0 | 46,156 bytes |
| Packed-root data blocks | 690 | 692 | +2 | 287 free 1 KiB blocks |

Both roots contain 95 files and nine inode-list blocks. The candidate identity
is `692 + 287 + 9 = 988`. The root checker completes all five phases and
reports `95 files 692 blocks 287 free`.

The standalone timeout image occupies 9,548 raw bytes, 8,253 packed bytes,
and ten packed blocks. Adminbox grows from 23 to 25 packed blocks. Shipping
`/usr/bin/timeout` as a hard link to adminbox therefore saves eight root blocks
against the standalone composition. The baseline and candidate adminbox maps
pull 112 and 116 libc members. The added members are `setpgid.o`, `setpgrp.o`,
`setrlimit.o`, and `sigsuspend.o`. The retained archive-member table records
all 228 state/member rows and the machine companion authenticates its bytes.

The local Arm GCC 16.2.0 stack report measures a 128-byte `main` frame, a
40-byte `parse_duration` frame, and a 168-byte pre-fork chain. Hosted Arm GCC
13.2.1 measures the same 128-byte `main` frame and a 48-byte `parse_duration`
frame for a 176-byte chain. The gate bounds those components at 128 and 48
bytes and derives its 176-byte chain ceiling from their sum. The known-bad
duration structure produces a 2,176-byte `main` frame. These values establish
static compiler frames only. Whole-program runtime stack and heap peaks remain
unmeasured.

## Alternatives and decision

The standalone executable loses ten root blocks. The adminbox form loses two.
No tested no-inline, IPA, jump-table, switch-conversion, or combined size
variant was smaller than default `-Os`. ASCII digit comparisons removed
the locale ctype dependency and saved 16 object-text bytes. Child-led groups
and child-only default signaling fail the selected Issue 8 topology and the
calibrated descendant witness. Removing execution errors, escalation, signal
status propagation, or pending-self-signal drainage would trade measured
behavior for bytes and is therefore rejected.

The retained margins bound the accepted exception: 26,304 PICO kernel bytes,
38,788 PICO_UART kernel bytes, 46,156 bytes before stack in the largest
process, 116,496 bytes before stack in adminbox, and 287 free root blocks.
The user-visible result is a compact recovery timeout with the selected Issue
8 options and status behavior.

The rollback point removes timeout from adminbox and the root manifest,
restores the adminbox limit to 23 blocks, removes the standalone registration,
removes `setpgid.c` from the two libc archive registrations and the RP2040
board-libc registration, removes the timeout-only wrapper and declarations,
and removes the timeout profile, denominator, and donor-ledger accounting.

## Approval and residuals

An independent staged review found two Important signal-state defects. The
repaired external-forwarding and zero-duration kill-after paths passed a
focused follow-up review with zero Critical and zero Important findings. The
follow-up reviewer approved the measured two-block packed-root exception after
verifying the repaired source identities, refreshed binaries, exact 116-member
linker-map inventory, root accounting, retained margins, fifteen calibrated
behavior mutations, and successful supervised clean-distribution receipt.

Renode execution, physical-board execution, and whole-program runtime peak
measurements remain open. Physical-board work requires an exact command,
artifact hash, resolved device, reason, and immediate owner approval before
flash or serial access.
