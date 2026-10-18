# Recovery path utilities growth exception

Status: approved.

The DPUCP-2024 recovery utility work adds tested `readlink`, `realpath`, and
`uudecode -o -` compatibility subsets without claiming POSIX or UNIX
conformance. The shipped textbox grows by 2,916 loaded bytes and three packed
blocks. The standalone uudecode grows by 32 loaded bytes while retaining its
11-block allocation. The complete root grows by four blocks and retains 289
free 1 KiB blocks.

The positive deltas require the exception because no valid decomposition
produced nonpositive process-image and complete-root growth. Shipping readlink
and realpath as separate executables would consume 46 blocks for the textbox
and two path utilities instead of 33 blocks for the shipped textbox. The
multicall composition therefore saves 13 root blocks.

## Compared states

The clean base is `cabcb4a2ff85d4438e893cff737595175674b046`. The measured
implementation is `7b8450f55c0060ce9d8d7d5acca05e6ca22795a9`. The measurement
host ran Linux 7.2.9-2-cachyos on x86-64 with bmake 20260912, GNU Arm Embedded
GCC 16.2.0, GNU binutils 2.47.20260726, host GCC 16.2.1, and Python 3.14.7.
The caller selected and exported `PYTHON` before each build and gate.

The candidate clean distribution used the repository's supervised target:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-distribution
```

The terminal receipt is
`.build-logs/rp2040-distribution-local-20261018T023644Z-2996950-c05beb12/status.json`.
The supervisor records exit status zero, 103.065 seconds elapsed, 1,620,231
child-output bytes, zero terminal-queue dropped bytes, a 600-second
output-idle deadline, and a 3,600-second hard deadline. Thirty-second
heartbeats report elapsed time, quiet time, byte count, process identity, and
the latest output while the command runs. A diagnostic search of the retained
log returned no warning, error, fatal, timeout, or dropped-output record.

`tools/bin/rp2040/size` supplies OMAGIC text, data, and BSS measurements.
`tools/bin/rp2040/hsaout -s` supplies raw, packed, and allocated-block
measurements. `tools/bin/rp2040/fsutil --check --partition=1` supplies the
complete-root counters. GNU `arm-none-eabi-size` supplies the kernel sections.
The machine-readable companion binds every arithmetic relation.

| Constrained surface | Base | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO kernel text | 104,598 | 104,598 | 0 | 26,224 loaded-span bytes |
| PICO kernel data | 248 | 248 | 0 | unchanged |
| PICO kernel BSS | 39,848 | 39,848 | 0 | unchanged |
| Textbox text | 35,972 | 38,888 | +2,916 | n/a |
| Textbox data | 544 | 544 | 0 | n/a |
| Textbox BSS | 1,168 | 1,168 | 0 | n/a |
| Textbox process-window occupancy before stack | 37,684 | 40,600 | +2,916 | 106,856 bytes |
| Textbox packed payload | 29,532 | 32,200 | +2,668 | n/a |
| Textbox allocated blocks | 30 | 33 | +3 | n/a |
| Uudecode text | 10,980 | 11,012 | +32 | n/a |
| Uudecode process-window occupancy before stack | 11,572 | 11,604 | +32 | 135,852 bytes |
| Uudecode packed payload | 9,657 | 9,746 | +89 | n/a |
| Uudecode allocated blocks | 11 | 11 | 0 | unchanged |
| Largest process, warp | 101,300 | 101,300 | 0 | 46,156 bytes before stack |
| Packed-root data blocks | 686 | 690 | +4 | 289 free blocks |
| Packed-root files | 95 | 95 | 0 | unchanged |

The new standalone readlink image measures 6,728/212/120 text/data/BSS bytes,
7,060 bytes before stack, 6,085 packed bytes, and seven blocks. The new
standalone realpath image measures 9,036/212/120 text/data/BSS bytes, 9,368
bytes before stack, 8,122 packed bytes, and nine blocks. The process window
retains 140,396 bytes before readlink stack and 138,088 bytes before realpath
stack. These standalone files are build and comparison surfaces; the root
ships hard links to the textbox instead.

The complete-root delta exceeds the textbox file-allocation delta by one
block. The complete-image measurement admits that block rather than assigning
it to a program. The two new hard-link directory entries and the rebuilt root
remain inside the observed complete-root result.

## Link composition

The first readlink map attempt replaced `LDFLAGS`, omitted the DiscoBSD linker
script and startup object, and selected GNU newlib. That map was rejected. A
command-line `LDFLAGS+=` probe also expanded to only the map option under
bmake, so append assignment is not a valid recovery mechanism.

Readlink and realpath now expose the same `LINK_MAP` input as textbox. The
input adds only `-Wl,-Map,<path>` after the target-owned flags. The emitted
links retain `-nostartfiles`, `lib/elf32-arm.ld`, `lib/crt0.o`, and the local
`lib` search directory. These commands regenerate the accepted standalone
maps without replacing target state:

```sh
bmake -C usr.bin/readlink MACHINE=rp2040 \
    LINK_MAP=/tmp/dpucp-recovery-readlink.map clean all
bmake -C usr.bin/realpath MACHINE=rp2040 \
    LINK_MAP=/tmp/dpucp-recovery-realpath.map clean all
```

The standalone binaries remain byte-identical after mapped relinking. The
readlink SHA-256 is
`3cb70671bed6928c019e6a9b56a877f21920a7a32a23c9c1f6a38f1626aa6aa6`.
The realpath SHA-256 is
`2d65e4fc47d6b6e385da25b05b2d501889503b8aaf48192e8e92003dd55d4f4b`.
Their accepted map hashes are
`c17eb13ee87650ddcc87ba43e89f326fcca573078b770a6eb692f521e7bf6bf8`
and `390ef5491da6d673acd40ee12b5b94609596e1912f378f31046c24d3ef1e8759`.

The exact final textbox contains 87 libc members. The same final applet set
without readlink and realpath contains 73 libc members. Standalone readlink
contains 37 libc members, and standalone realpath contains 54 libc members.
`recovery-path-utilities-archive-members.tsv` retains every selected member.
The final textbox and its map have SHA-256 values
`e6056dd11f7096b44477f06f668188bf5ef3c078fe49a96255d0c7e007347208`
and `a5bbaaee7c7aaa973a88c06f53ec3b52ba5253e85cdb842bcd0fb0ce01a738f9`.
The alternative textbox and its map have SHA-256 values
`2cbb7b9f72efd486751d80e7dc927f785f47c17ec3d97f18a5041174ad84b064`
and `336e1d20440455299d158cd41a49959030f7587ee008aeeea09c13ccfcab7e87`.

| Composition | Textbox blocks | Readlink blocks | Realpath blocks | Total blocks |
| --- | ---: | ---: | ---: | ---: |
| Shipped multicall | 33 | 0 | 0 | 33 |
| Separate executables | 30 | 7 | 9 | 46 |

The separate-executable form costs 13 additional blocks. It also duplicates
libc state across three process images and cannot improve the largest-process
margin because warp remains the largest process in both states.

## Examined offsets

The path applets call the existing readlink system interface and libc realpath
resolver. A private path resolver would duplicate kernel and libc behavior,
increase test surface, and violate the accepted ownership boundary. The
realpath wrapper adds bounded final-symlink traversal only because the legacy
resolver can loop on that terminal case. Its Cortex-M0+ static frame is 672
bytes against a 704-byte ceiling; a 1,032-byte known-bad fixture and a dynamic
classification fixture calibrate the gate.

Removing error diagnostics, newline control, one-operand validation,
`-e`/`-E` behavior, dangling-link checks, or output-failure
propagation would reduce the tested recovery contract rather than offset its
cost. Shipping separate path executables fails the packed-root comparison.

Uudecode already ships as a standalone file and retains the same 11-block
allocation. The 32-byte loaded growth preserves binary-clean `-o -`, checked
write failures, complete traditional encoded-line validation, missing
traditional-footer rejection, and signed base64 sentinels under Cortex-M0+
unsigned-char mode. Removing any of those repairs reintroduces a witnessed
data-corruption or error-reporting failure. Complete base64 padding and `====`
footer validation remains open: incomplete base64 input can emit decoded bytes
and return success. No examined valid decomposition produces nonpositive deltas
on every constrained surface.

## User-visible benefit and rollback

Recovery scripts gain symbolic-link inspection and canonical path resolution
without importing a second resolver. File-transfer recovery gains binary-clean
stdout decoding and explicit failures for malformed input and failed writes.
Textbox sharing preserves 13 packed-root blocks relative to standalone path
utilities.

The rollback point is base commit
`cabcb4a2ff85d4438e893cff737595175674b046`. Reverting the implementation and
evidence commits restores the base textbox, uudecode, and root allocation but
removes the three compatibility subsets and their executable witnesses.

Static section sizes remain lower bounds on runtime process-window use. The
realpath compiler report bounds one static frame; runtime peak stack, heap,
symlink depth, and unrelated caller depth remain unmeasured. Host, sanitizer,
cross-build, clean-image, Qemu-user, Renode, and physical-board observations
remain separate evidence classes. This work includes no Renode or hardware
execution.

## Approval

Reviewer: Codex independent reviewer (`review_recovery_root_fix`).

The reviewer reproduced HEAD `3cf8c07f8036f4a1cc95f1a2b88c827d79f512ba`,
including corrected a.out and packed sizes, map and binary hashes, archive-member
sets, the focused native, sanitizer and cross gates, the retained kernel,
process-window and root margins, and the rejected standalone composition. The
approval retains runtime peak stack and heap as unmeasured surfaces and complete
base64 padding and footer validation as an open decoder residual.
