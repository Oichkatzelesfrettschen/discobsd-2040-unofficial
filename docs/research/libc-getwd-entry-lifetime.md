# getwd borrowed directory-entry lifetime

`readdir()` returns an entry inside its owning DIR buffer. `getwd()` copies
that entry's name into its automatic pathname buffer before closing the
stream. The ordering removes a heap-use-after-free without a name allocation,
a second copy buffer or a new permanent object. Both local function
definitions use typed C17 parameters; the public getwd interface stays fixed.

The source baseline is `9e633976d9a636c4d0e35db420a134e051e869ef`.
The former ordering calls closedir before prepend reads `dir->d_name`.
A fixture that clears the directory block and frees the stream on close
returns the wrong pathname with that source. ASan independently reports a
one-byte heap-use-after-free in prepend, 40 bytes into the fixture's
1,056-byte native DIR allocation. The target layout remains 1,040 bytes;
those native and target sizes describe different ABIs.

## Contract and deciding tests

The exact-source fixture exercises a same-device child, a child across a
device boundary, the root path and an empty parent. The child cases return
`/leaf`; the root returns `/`; the empty parent returns NULL with the
historical diagnostic. Every successful open has one close. The fixture
uses a real allocation and frees the owning stream, so ASan observes the
lifetime rather than a simulated ownership flag.

`check-getwd-lifetime` records native, ILP32 and sanitizer execution. A
release-before-copy mutation restores the failed ordering and must exit 1
with the borrowed-entry diagnostic. `check-getwd-lifetime-cross` compiles the
exact implementation as C17 for Cortex-M0+. The root host/cross aggregates
and required ILP32 execution own these gates; the execution inventory records
their command paths. GCC and Clang also compile and execute the fixture with
`-std=c17 -pedantic-errors -Wall -Wextra -Werror -Wold-style-definition`.

Select and export PYTHON, then run:

```
bmake MACHINE=rp2040 check-getwd-lifetime check-getwd-lifetime-cross
bmake MACHINE=rp2040 check-test-execution check-lint
bmake -j4 MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check
```

The focused native, ILP32, ASan/UBSan, target and mutation checks pass.
The clean distribution and complete serial non-hardware aggregate pass,
including all 18 Smaller C cases and the damaged/restored archive control.
Existing host shell/libtcl links emit glibc diagnostics for tmpnam, getwd
and mktemp; production C compilation treats warnings as errors. Paired production measurements use arm-none-eabi GCC
16.2.0, host GCC 16.2.1 and bmake 20260912. The 275-row
`libc-getwd-linked-extents.csv` records every installed OMAGIC executable.
All 275 text/data/BSS/file extents compare equal. Eight file hashes change:
seven getwd consumers and the timestamp-bearing pdc binary.

| Surface | Before | After |
| --- | ---: | ---: |
| getwd object text/data/BSS | 490/0/4 | 490/0/4 |
| getwd / prepend static frames | 672 / 12 | 672 / 12 |
| Full ELF libc bytes / members | 406816 / 380 | 406816 / 380 |
| Full a.out libc bytes / members | 115934 / 370 | 115934 / 370 |
| Selected board libc bytes / members | 56058 / 151 | 56058 / 151 |
| Root free blocks | 265 | 265 |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

All 150 extracted selected-board objects compare byte-identically. getwd
is absent from that selected archive. The packed box payload grows by one
byte; its aliases share the same storage and do not multiply that charge.
Instruction ordering changes compression even with equal raw executable
extents. The observed root free-block count remains equal.

The source revision, CSV, JSON comparison receipt and extracted objects
retain the comparison evidence. Fresh clean worktrees at the recorded
baseline and candidate revisions replay the binary measurements. Archive
container hashes alone include metadata and do not establish member changes.

## Boundary

The repair closes the borrowed-entry lifetime contract. Historical getwd
still assumes a caller buffer of MAXPATHLEN, retains shared pathsize state,
and has independent path-depth and unchecked metadata-result limitations.
Those mechanisms require a separate getwd/getcwd compatibility design and
bounded-path fixtures. The lifetime fix does not establish whole-interface
SUS conformance or reentrancy. Physical-board execution remains unmeasured.
