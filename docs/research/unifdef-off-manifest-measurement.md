# Bounded unifdef off-manifest measurement

The RP2040 build accepts a strict-C17 `unifdef` while the root manifest leaves
the executable unshipped. The implementation keeps the classic BSD command
surface, replaces recursive conditional processing with a fixed frame stack,
and rejects `#elif` rather than silently approximating expression evaluation.

## Source selection

The comparison used three repository snapshots from `BSD_Workspace`:

| Source | Commit | Lines | SHA-256 |
| --- | --- | ---: | --- |
| 4.4BSD-Lite2 `usr.bin/unifdef/unifdef.c` | `b10da34408c8a0f32f51d79a7f16bd75337115d2` | 638 | `fb1bc8b39a36fe1f99c2a87508efb6c50b8efddeddd202a22fa1c7f95ef5e6ba` |
| 2.11BSD_X44 `usr.bin/unifdef/unifdef.c` | `31710fa6903bdb36a9ba605175b1cee96ec0d931` | 1,037 | `61ae06fbd51e0dd490de3197f8064ff5f0d02eeb2e430f9b3bb2def29203c1e6` |
| NetBSD `usr.bin/unifdef/unifdef.c` | `1e8e727e461bb760bf82162f33b64afea02da2f3` | 1,037 | `61ae06fbd51e0dd490de3197f8064ff5f0d02eeb2e430f9b3bb2def29203c1e6` |

The 2.11BSD_X44 copy and the NetBSD copy are byte-identical. They add a
preprocessor expression engine and 4,096 symbols. The smaller 4.4BSD program
already has the required 100-symbol interface, but it recursively processes
conditionals and holds only 256 input bytes. The port retains its interface and
license while replacing those two mechanisms.

## Resource invariants

- One 4,096-byte buffer owns the current logical preprocessing line. Physical
  lines join only across phase-2 backslash-newline splices or a block comment
  between directive tokens. A 4,096-byte final line is accepted; a longer
  logical line returns status 2 before any bytes from that line are emitted.
- One 64-frame iterative stack owns conditional nesting. Frame scans derive
  repeated-symbol activity, removing a duplicate active-symbol table and the
  stale-state path an early error would create.
- One 100-entry pointer table refers to argument storage. The unifdef source
  makes no direct allocation call, file mapping, temporary-file write, or input
  rewrite. Target stdio can allocate stream buffers on first input or output.
- The lexical state distinguishes code, block comments, strings, characters,
  and line comments. Every nonignored arm updates input lexical state; the
  inactive arm selected by an ignored option is copied without lexical
  interpretation.
- Directive recognition removes backslash-newline splices before classifying
  tokens and carries block-comment spacing across physical lines. Output keeps
  the original bytes of every retained fragment.
- `#elif` returns status 2. Supporting it correctly requires the expression and
  branch-selection machinery that this bounded port deliberately excludes.
- Status 0 means byte-identical output, status 1 means transformed output, and
  status 2 means an argument, structure, bound, input, output, or flush error.

The host gate mutates `#elif` into a plain directive and removes the final
`fflush` verdict. Both mutated sources fail the same contract suite that the
production source passes.

## RP2040 measurement

The measurement used a clean `bmake MACHINE=rp2040 distribution`. The build
completed with status 0 and composed the board libc from 149 members totaling
55,970 bytes.

| Surface | Measured value |
| --- | ---: |
| target object text | 3,625 bytes |
| target object data | 0 bytes |
| target object BSS | 5,112 bytes |
| OMAGIC text | 10,984 bytes |
| OMAGIC data | 464 bytes |
| OMAGIC BSS | 5,172 bytes |
| raw a.out | 11,480 bytes |
| packed a.out | 9,386 bytes |
| raw root cost | 13 blocks |
| packed root cost | 11 blocks |

The production image reports 107 free blocks and 31 free inodes. The manifest
contains no `/usr/bin/unifdef` entry, and image inspection finds no executable
by that name. Hypothetical admission as a packed executable would leave 96 free
blocks and consume one inode.

The off-manifest state adds zero executable blocks to the flashed root and
creates no `unifdef` process that could dirty swap. If admitted later, one
running process owns 464 initialized data bytes plus 5,172 BSS bytes, for 5,636
bytes of measured static mutable storage. Stdio buffers, argument storage, the
stack, the user area, and swap metadata add runtime pressure outside that
static measurement. The program trades CPU for a smaller and more stable
source-owned state: it scans at most 64 frames for a repeated symbol and
streams one line at a time. It writes only the selected stdout stream, so flash
wear from redirected output or process swapping remains an execution-policy
effect rather than an internal temporary-file mechanism.

## Validation boundary

The following checks passed:

```text
bmake MACHINE=rp2040 check-unifdef-contracts
bmake MACHINE=rp2040 check-unifdef-contracts-cross
shellcheck -S error tests/unifdef_contracts/unifdef_cli_test.sh
bmake MACHINE=rp2040 distribution
tools/bin/hsaout -s usr.bin/unifdef/unifdef
tools/bin/fsutil --verbose --partition=1 distrib/rp2040/sdcard.img
```

The host suite also passes under address and undefined-behavior sanitizers. It
pins ignored-arm lexical suppression, comments between directive tokens,
comments that close before a directive, complete trailing comments on removed
directives, phase-2 splices across comment delimiters and directive tokens,
multiline directive comments, preprocessing whitespace, source-order `-l`
line preservation, and byte-exact complemented status in addition to the
listed command surface. The exact 4,096-byte spliced boundary terminates and
round-trips. The Cortex-M0+ object imports no compiler division helper. These
results prove the source contracts, target compilation, bounded object storage,
clean image construction, and hypothetical block cost. They do not prove board
execution; the measurement neither accesses nor flashes a board.
