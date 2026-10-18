# Utility printf growth exception

Status: pending independent approval.

The DPUCP-2024 utility `printf` implementation adds a tested compatibility
subset without claiming POSIX or UNIX conformance. The shipped utilbox grows by
1,148 text bytes and one packed-root block. The standalone program retains a
smaller loaded process footprint because its BSS falls by 124 bytes, while its
packed file stays at nine blocks. Shipping that second executable beside a
utilbox linked without the applet would consume seven additional packed-root
blocks and therefore fails the complete-image constraint.

## Compared states

The clean base is `91c9338a3854b4950da752f3825b2026d34d1dfc`. The production
implementation is `86c762d704c512c294836871234a1c77ca2d9ffc`. Exact parent
relinks and candidate relinks used the same applet set, GNU17 Cortex-M0+ flags,
DiscoBSD startup object, linker script, and libc archive.

The measurement host ran Linux 7.2.9-2-cachyos on x86-64 with bmake 20260912,
GNU Arm Embedded GCC 16.2.0, GNU binutils 2.47.20260726, host GCC 16.2.1, and
Python 3.14.7. The caller selected and exported `PYTHON` before the build. The
clean candidate command was:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-distribution
```

The clean-base receipt identifier is
`.build-logs/rp2040-distribution-local-20261018T003736Z-41-753681c9/status.json`.
The detached clean-base build recorded exit status 0, 265.296 seconds elapsed,
1,357,073 child-output bytes, and zero terminal-queue dropped bytes. Its
ignored local receipt is absent from the feature worktree; the base artifact
measurements below retain the result used for the comparison.

The candidate receipt is
`.build-logs/rp2040-distribution-local-20261018T004146Z-2099126-3c07b8e4/status.json`.
The receipt records exit status 0, 78.848 seconds elapsed, 833,049 child-output
bytes, zero terminal-queue dropped bytes, a 600-second output-idle deadline,
and a 3,600-second hard deadline. Thirty-second heartbeat records updated
elapsed time, quiet time, byte count, process ID, and the latest output. The
supervisor exposed active compilation and bounded silent execution. A search
of the retained candidate log for compiler warnings, errors, fatal diagnostics,
timeout stops, incomplete cleanup, and dropped-output records returned an empty
set.

The final build-plus-check receipt is
`.build-logs/rp2040-check-local-20261018T004814Z-2143840-0e9a1ff3/status.json`.
Its build stage completed in 50.062 seconds with 823,037 child-output bytes;
its check stage completed in 541.779 seconds with 704,247 child-output bytes.
Both stages recorded exit status zero, zero terminal-queue dropped bytes, and
scheduled heartbeats while active. Diagnostics that contain `FAIL`, sanitizer
errors, or timeout language belong to named calibrated negative controls whose
enclosing gates pass.

`tools/bin/rp2040/size` supplies a.out text, data, and BSS measurements.
`tools/bin/rp2040/hsaout -s` supplies raw, packed, and allocated-block
measurements. `tools/bin/rp2040/fsutil --check --partition=1` supplies the
complete-root counters. GNU `arm-none-eabi-size` supplies the kernel sections.
The machine-readable companion retains the arithmetic and calibrated verifier
inputs.

| Constrained surface | Base | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO kernel text | 104,598 | 104,598 | 0 | 26,224 loaded-span bytes |
| PICO kernel data | 248 | 248 | 0 | unchanged |
| PICO kernel BSS | 39,848 | 39,848 | 0 | unchanged |
| Standalone text | 8,236 | 8,252 | +16 | 138,932 process-window bytes before stack |
| Standalone data | 212 | 200 | -12 | included above |
| Standalone BSS | 196 | 72 | -124 | included above; loaded occupancy falls from 8,644 to 8,524 bytes |
| Standalone raw a.out | 8,480 | 8,484 | +4 | n/a |
| Standalone packed payload | 7,400 | 7,418 | +18 | n/a |
| Standalone packed blocks | 9 | 9 | 0 | n/a |
| Utilbox text | 53,316 | 54,464 | +1,148 | 88,156 process-window bytes before stack |
| Utilbox data | 581 | 581 | 0 | included above |
| Utilbox admitted BSS | 4,255 | 4,255 | 0 | included above |
| Utilbox raw a.out | 53,929 | 55,077 | +1,148 | n/a |
| Utilbox packed payload | 43,803 | 44,773 | +970 | n/a |
| Utilbox packed blocks | 44 | 45 | +1 | n/a |
| Largest process, `warp`, loaded bytes | 101,300 | 101,300 | 0 | 46,156 bytes before stack |
| Packed-root data blocks | 685 | 686 | +1 | 293 free 1 KiB blocks |

The root identity is `686 + 293 + 9 = 988`, where nine blocks hold the inode
list. The candidate retains 300,032 free root bytes. The process-window margins
exclude runtime stack and heap and therefore establish neither a runtime peak
nor board behavior.

## Composition and dependency closure

The correct map-producing link appends the map option to `LIBS`, because a
command-line `LDFLAGS` assignment would replace the target linker script,
startup object, and DiscoBSD archive path:

```sh
bmake -C usr.bin/printf MACHINE=rp2040 clean all \
    'LIBS=-lc -Wl,-Map,printf.link.map'
bmake -C sbin/utilbox MACHINE=rp2040 clean all \
    'LIBS=-lc -Wl,-Map,utilbox.link.map'
bmake -C sbin/utilbox MACHINE=rp2040 clean all \
    'TOOLS=expr id more uniq head tr wc cmp sort basename env touch xargs tty uname md5 resize md' \
    'LIBS=-lc -Wl,-Map,utilbox-without-printf.link.map'
```

The standalone map selects 36 DiscoBSD libc members. The shipped utilbox map
selects 113, and the utilbox-without-printf map selects 111. The checked-in
`utility-printf-archive-members.tsv` retains every selected member. Its 260
rows bind those three exact inventories to the machine-readable count checks.
The standalone, shipped-utilbox, and alternative map SHA-256 values are
`1c9d64109f836824b7cc8b020ceaa2fde2b89c220f17831494767240cd77c943`,
`829543d6eb5bc709eef6e39dbb775feaad11b6739926946e51f46f566304e83f`,
and `a35a99983ce4bc216c605c0435b1cb524e6445d2b5df9ed546a6a7f57ad66f07`.
The implementation uses `fwrite.o`, `strtol.o`, `strtoul.o`, `strchr.o`,
`printf.o`, `vfprintf.o`, and `doprnt.o`; it adds no private runtime library.
The source keeps the applet in one relocatable member, and the established BSS
overlay admits 4,255 bytes rather than utilbox ELF's unoverlaid 10,969-byte
sum.

The utilbox-without-printf alternative occupies 43 packed blocks, and the
standalone executable occupies nine. Their 52-block total exceeds the shipped
45-block utilbox by seven blocks. The shipped hard link remains the smaller
complete-root composition.

## Examined offsets

The first strict-C17 implementation increased utilbox text by 1,984 bytes.
The accepted parser reduced that growth to 1,148 bytes through these bounded
mechanisms:

- Global ILP32 argument indices replace heap-backed conversion state.
- One conversion parser normalizes repeated flags into a fixed 16-byte buffer.
- Width and precision parse as `long`, reject values outside 65,535 before
  narrowing, and share one range diagnostic.
- `%b` measures and emits decoded bytes without a second allocated buffer.
- Signed and unsigned target-width conversion use archive-isolated `strtol()`
  and `strtoul()` members instead of importing `intmax` or locale helpers.
- Float, wide-character, and locale-table dependencies remain outside the
  selected byte-locale subset.

Further removal of `%b`, `\c`, numbered conversions, format reuse, error
accumulation, embedded-NUL emission, or output-failure reporting would remove a
tested requirement rather than offset its cost. Moving `printf` out of utilbox
would add seven net packed-root blocks. A separate shared escape engine would
need another callable boundary and has no second admitted consumer in this
change. No examined valid composition produces nonpositive utilbox and root
deltas.

## Behavior and residuals

Native strict C17, native ASan/UBSan, and ILP32 executions each pass 68 checks.
The exact production source also compiles as strict C17 for Cortex-M0+. The
witnesses cover `%b`, `\c`, embedded NUL bytes, byte precision, field width,
ordinary and numbered format reuse, malformed and nonnumeric continuation,
quoted-byte operands, full target-width unsigned decimal and hexadecimal
operands, missing operands, width and precision bounds, narrowing wrap,
malformed formats, repeated flags, and failed standard output.

The requirement remains `partial` and `open`. The deciding next action is to
audit every required XBD File Format Notation facet and utility-specific
exception against executable witnesses, then measure any unsupported required
facets against the byte-locale, process-window, and packed-root limits. Issue 8
permits omission of floating conversions and defines `%c` in bytes. Positional
`*m$` width and precision selection is an optional implementation extension,
and mixing numbered and unnumbered argument-consuming conversions has
unspecified behavior. Locale behavior remains outside the first-release
byte-locale policy.

Recovery scripts gain binary-clean `%b`, immediate `\c` termination, numbered
conversion selection, bounded dynamic fields, repeated format application, and
truthful failure status. Reverting the feature commits from
`6879c95f318e6232993e53bf2dbaadfd4d4a35c3` through
`86c762d704c512c294836871234a1c77ca2d9ffc` restores the base utilbox and one
packed-root block while removing those behaviors.

## Approval

Reviewer approval remains pending. The reviewer must reproduce the standalone,
utilbox, largest-process, kernel, and complete-root measurements; inspect the
selected behavior witnesses and archive composition; and approve the retained
margins, rejected offsets, user-visible benefit, rollback point, and runtime
stack and heap uncertainty before merge.
