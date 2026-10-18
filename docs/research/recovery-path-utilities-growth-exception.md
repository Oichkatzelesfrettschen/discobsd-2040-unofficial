# Recovery path utilities growth exception

Status: pending independent review.

The DPUCP-2024 recovery utility work adds tested `readlink`, `realpath`, and
`uudecode -o -` compatibility subsets without claiming POSIX or UNIX
conformance. The shipped textbox grows by 2,896 loaded bytes and three packed
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
implementation is `90eeede4533e49ddd02cdd9d70c12a2d48e9474d`. The measurement
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
`.build-logs/rp2040-distribution-local-20261018T021529Z-2852043-09d22013/status.json`.
The supervisor records exit status zero, 196.251 seconds elapsed, 1,619,370
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
| Textbox text | 35,972 | 38,868 | +2,896 | n/a |
| Textbox data | 544 | 544 | 0 | n/a |
| Textbox BSS | 1,168 | 1,168 | 0 | n/a |
| Textbox process-window occupancy before stack | 37,684 | 40,580 | +2,896 | 106,876 bytes |
| Textbox packed payload | 29,532 | 32,171 | +2,639 | n/a |
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
standalone realpath image measures 9,016/212/120 text/data/BSS bytes, 9,348
bytes before stack, 8,105 packed bytes, and nine blocks. The process window
retains 140,396 bytes before readlink stack and 138,108 bytes before realpath
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
`f9659b4e76062f863ac82f77de1f407b9b1bd1db2a8d16384faad8831a9098ad`.
Their accepted map hashes are
`c17eb13ee87650ddcc87ba43e89f326fcca573078b770a6eb692f521e7bf6bf8`
and `9749f561a449d8a6cf56a9e3c25f7eae3fa3b34cddc784d0d3eefbdaf9a186a9`.

The exact final textbox contains 87 libc members. The same final applet set
without readlink and realpath contains 73 libc members. Standalone readlink
contains 37 libc members, and standalone realpath contains 54 libc members.
`recovery-path-utilities-archive-members.tsv` retains every selected member.
The final textbox and its map have SHA-256 values
`a779a175d1b3c562e743beb192e848a76328bd533dcafed6500f49ffd5bfc333`
and `95182fb15a036410e5f25c66bed71d83b394a5d1cbd15e6bb9f623535581e5c6`.
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

Removing error diagnostics, newline control, multiple-operand status
accumulation, `-e`/`-E` behavior, dangling-link checks, or output-failure
propagation would reduce the tested recovery contract rather than offset its
cost. Shipping separate path executables fails the packed-root comparison.

Uudecode already ships as a standalone file and retains the same 11-block
allocation. The 32-byte loaded growth preserves binary-clean `-o -`, checked
write failures, complete encoded-line validation, truncated-input rejection,
and signed base64 sentinels under Cortex-M0+ unsigned-char mode. Removing any
of those repairs reintroduces a witnessed data-corruption or error-reporting
failure. No examined valid decomposition produces nonpositive deltas on every
constrained surface.

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

Reviewer: pending.

Approval requires an independent read-only review of the implementation
commit, checked-in measurements, archive inventory, target-native map seam,
retained margins, rejected alternatives, rollback point, and residual runtime
uncertainty.
