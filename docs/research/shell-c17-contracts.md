# Shell C17 interface contracts

The RP2040 Bourne shell keeps its production GNU17 Cortex-M0+ command and
also compiles every production translation unit under strict ISO C17. This
language result covers compilation and the exercised shell behavior. It makes
no POSIX or UNIX conformance claim. The arena and tagged argument-chain
representations retain the shell's explicit ILP32 dependency, while the
execution-input interface carries every pointer through its pointer type.

## Initial compiler frontier

The initial target census compiled the 28 production translation units with
`-std=c17 -Wall -Wextra -Wpedantic -Wstrict-prototypes
-Wold-style-definition -Wmissing-prototypes`. The complete compiler log
contained 4,419 warning instances at 437 unique file-line sites:

- 4,171 strict-prototype instances;
- 248 old-style-definition instances;
- 3,727 repeated instances originating from the shared `defs.h` declaration
  table.

The counts describe diagnostic instances, not independent defects. The shared
header repeated the same missing contracts in each consumer.

## Implemented boundary

`bin/sh/Makefile` owns `check-strict-c17`. The target compiles the 28
production translation units with `-std=c17 -pedantic-errors`, the complete
warning profile, and syntax-only output. `check-sh-c17-contracts` runs that
gate beside the optimized production GNU17 compile and link. The host editor
and passwd-name tests compile their termios and file-reader branches under the
same strict-C17 declaration warnings.

The migration supplies complete parameter and callback types, makes private
helpers internal, supplies every `execute()` pipeline argument, preserves
tagged parse-tree projections, and routes signal callbacks through a typed
adapter. The signal table retains its original external linkage. Its
`SIGABRT` entry uses the default disposition, which is the terminal action
that the former incompatible `abort(void)` callback reached after receiving
the same signal. The ILP32 behavior suite exercises that disposition and
requires status 134.

The host directory adapter defines `direct` as the host `dirent` record.
`expand.c` and `pwd.c` include `<sys/dir.h>` before the shell declaration
table, so the `getdir()` declaration, definition, and callers use one record
type on both the host and target builds.

`execute_string_input()`, `execute_argument_input()`, and
`execute_descriptor_input()` name the three parser-input ownership modes.
String and argument-vector callers lend storage through parsing. The argument
mode starts at the first eval word and advances a private `fileblk.feval`
cursor over the remaining words. The descriptor mode transfers the opened
descriptor to the pushed input frame; `pop()` closes the descriptor on normal
completion, and `command_unwind_input()` closes it during recoverable parser
unwind. Exit and signal traps use string input and preserve their existing
error context.

The compiler contract binds all three declarations to exact function-pointer
types under GNU17 and strict C17. Its deliberately incompatible fixture must
fail compilation. The ILP32 behavior suite exercises multiple eval arguments,
nested eval, multi-line sourced input, exit-trap eval, parser failure,
allocation failure, interruption contexts, and cleanup. Five one-object
mutants calibrate command dispatch, getopts publication, eval mode selection,
eval argument advancement, and sourced-descriptor selection.

## Verification

Select and export `PYTHON` before every command. Run each command through
`tools/run_logged_gate.py` with a 30-second heartbeat, a 120-second output-idle
deadline, and a bounded wall deadline.

```sh
bmake MACHINE=rp2040 check-sh-c17-contracts
bmake MACHINE=rp2040 -C bin/sh/tests clean test
bmake MACHINE=rp2040 check-posix-sh
HOST_CC=clang bmake MACHINE=rp2040 check-posix-sh
```

The focused migration measurements against the shell already shipped at
`cabcb4a2ff85d4438e893cff737595175674b046` are:

| Surface | Base | C17 migration | Delta |
| --- | ---: | ---: | ---: |
| Shipped OMAGIC text | 33,904 | 33,848 | -56 bytes |
| Shipped OMAGIC data | 2,432 | 2,432 | 0 |
| Shipped OMAGIC bss | 7,176 | 7,176 | 0 |
| Process-window occupancy before stack | 43,512 | 43,456 | -56 bytes |
| OMAGIC file | 36,368 | 36,312 | -56 bytes |
| Packed payload | 31,109 | 31,067 | -42 bytes |
| Allocated root blocks | 32 | 32 | 0 |

The linked ELF reports 33,846 text bytes. The shipped OMAGIC header reports
33,848 bytes after target-format alignment, so process-window accounting uses
the latter value. The clean parent and candidate distributions both contain a
104,598/248/39,848-byte kernel text/data/bss image, 410,054-byte ELF libc,
43,010-byte board libc, 114,458-byte a.out libc, 101,300-byte largest process
and 686 allocated root data blocks. Both roots retain 293 free blocks; the
identity `686 + 293 + 9 = 988` includes the reserved filesystem blocks.
`alarm.o` remains 52/0/0 text/data/bss bytes, 852 bytes as an ELF object and
120 bytes as an a.out archive member.

The post-repair clean distribution receipt
`.build-logs/rp2040-distribution-local-20261018T041614Z-3763412-9f1169c5/status.json`
records exit 0, 122.952 seconds, 1,600,749 output bytes and zero dropped
terminal bytes. Post-build `arm-none-eabi-size`, target `size`, `hsaout -s`,
`fsutil --check --partition=1` and file-size inspections produced the candidate
measurements above. The independent clean parent receipt is identified by
`rp2040-distribution-local-20261018T040034Z-53-c37e477c`; the same inspections
of its artifacts produced the base measurements. The shell and complete image
therefore satisfy the measured per-surface non-growth rule.

## Typed execution-input resource acceptance

Matched clean schema-2 distributions compare the parent source at
`16c86b819381e65e66a9f32142a9d837d8ef997a` with the staged typed-input
candidate based on that parent. The source snapshots bind the complete tracked
file and index state; their tracked-file digests are
`c6f500a54cec4785d6e29ccaec0d6e2af5fb55c2de8c6a6477e1645a6b221027`
and `afc0dfb0a61d2dd5c3835ea8269dca66a9df5f774d547a37bf31575f1dc375d9`.
The receipt narrative and its plan cross-reference were appended after the
capture. Those two documentation edits are absent from the candidate snapshot
and are not inputs to any captured artifact; every closure-owned input retains
the identity recorded by the receipt.
Both captures use bmake 20260912, Arm GCC 16.2.0, GNU ld
2.47.20260726, and Python 3.14.8 on Linux 7.2.11 x86-64. The parent clean and
distribution stages completed in 4.906 and 124.290 seconds. The candidate
stages completed in 4.976 and 125.769 seconds.

| Receipt | Parent SHA-256 | Candidate SHA-256 |
| --- | --- | --- |
| Source snapshot | `0e5a751f4f8f863386ac8141b5b587fb0c635ef016afec91644a7cbd27c9d7dc` | `43c55fdc913e02787a0688f6cb60fce01b5c096cd1b358b5bcf5499e03ed21a6` |
| Shipped closure | `c23859ce8cc89e3f714ef70b38426f3e92c963232b758c7a56c13a12d47e88ed` | `7c7c988d3b401887373073d2e42d4a8f7850971bcbe13b11b8373f78e30c0f9c` |
| Static resources | `9be246d9becfa4e0e4cc366d37c830be82f3f4614cd7d90411237f32ae459a73` | `5ddf10f12b7507a7e3da9af907b6b32ee5f9bc765ba65c0896d0ec2694d09edc` |

The delta receipt has SHA-256
`282bafa9cf18eb5f41c44d4eea783cd7ae7ef8a07abc4beb55fbbc0b172d6130`.
The resource comparer rejects its one positive constrained field,
`program:/bin/sh:packed_bytes`, until the measured exception below receives
review. Every archive-member selection remains equal.

| Constrained surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO loaded flash span | 105,828 | 105,828 | 0 |
| PICO_UART loaded flash span | 93,352 | 93,352 | 0 |
| Native board libc bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 |
| Shell OMAGIC text | 33,848 | 33,844 | -4 bytes |
| Shell OMAGIC data | 2,432 | 2,432 | 0 |
| Shell OMAGIC bss | 7,176 | 7,176 | 0 |
| Process-window occupancy before stack | 43,456 | 43,452 | -4 bytes |
| Shell OMAGIC file | 36,312 | 36,308 | -4 bytes |
| Shell packed payload | 31,067 | 31,070 | +3 bytes |
| Shell allocated root blocks | 32 | 32 | 0 |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 |
| Packed-root free inodes | 33 | 33 | 0 |

The final interface uses one three-register implementation and a typed union
whose active member follows the selected entry point. A three-wrapper
out-of-line design added 64 raw shell bytes. A four-argument common function
restored most raw flash but added eight bytes to the deepest input frame. A
four-argument discriminated union restored the frame and retained four raw
bytes of growth. The selected three-register union removes four raw bytes and
preserves the established frame. Exact production commands with
`-fstack-usage` report 184 bytes for both `execexp()` and `execute_input()`;
both prologues save `r4` through `r6` and `lr` and reserve 168 stack bytes.
The corresponding `xec.o` text decreases from 5,761 to 5,757 bytes.

The implementation review accepts the three-byte packed-payload exception.
Heatshrink's token choices can increase a compressed stream when the raw
instruction stream shrinks. The exception consumes no additional filesystem
block, process-window byte, kernel byte, archive member, static input-frame
byte, or inode. Reverting the typed seam would restore a pointer transported
through `int`; the measured alternatives consume more raw flash or stack.
Runtime stack, heap, and storage traffic retain their unmeasured states, and
physical-board execution retains its separate authorization boundary.

## Residuals

The shell arena, parse-tree projections, and tagged argument chains retain
other pointer-width assumptions, so this unit does not claim complete LP64
shell execution. Runtime peak stack and heap remain unmeasured, so static
occupancy does not establish peak process-window use. The source-only UUCP and
tip families compile their repaired `access()` and `alarm()` call sites under
the production command; their complete strict-C17 migrations remain open.
