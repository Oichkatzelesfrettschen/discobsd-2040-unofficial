# Shell C17 interface contracts

The RP2040 Bourne shell keeps its production GNU17 Cortex-M0+ command and
also compiles every production translation unit under strict ISO C17. This
language result covers compilation and the exercised shell behavior. It makes
no POSIX or UNIX conformance claim and does not remove the shell's explicit
ILP32 dependency.

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

## Verification

Select and export `PYTHON` before every command. Run each command through
`tools/run_logged_gate.py` with a 30-second heartbeat, a 120-second output-idle
deadline, and a bounded wall deadline.

```sh
bmake MACHINE=rp2040 check-sh-c17-contracts
bmake MACHINE=rp2040 -C bin/sh/tests clean test
bmake MACHINE=rp2040 check-posix-sh
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

## Residuals

`execexp()` still transports an argument-vector address through `int`. The
source converts through `intptr_t`, and the maintained target and behavior
gate are ILP32, but the representation remains unsuitable for a wider-pointer
host. Removing that representation requires a separate parse-input ABI change.
Runtime peak stack and heap remain unmeasured, so static occupancy does not
establish peak process-window use. The source-only UUCP and tip families
compile their repaired `access()` and `alarm()` call sites under the production
command; their complete strict-C17 migrations remain open.
