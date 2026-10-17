# Utility printf growth exception

Status: pending independent approval.

The DPUCP-2024 utility `printf` implementation adds a tested compatibility
subset without claiming POSIX or UNIX conformance. The shipped utilbox grows by
752 text bytes and one packed-root block. The standalone program becomes
smaller, but shipping a second executable would consume eight additional
packed-root blocks and therefore fails the complete-image constraint.

## Compared states

The clean base is `91c9338a3854b4950da752f3825b2026d34d1dfc`. The production
implementation is `6879c95f318e6232993e53bf2dbaadfd4d4a35c3`. Exact parent
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

The terminal receipt is
`.build-logs/rp2040-distribution-local-20261017T225126Z-1047320-4d2b7c7f/status.json`.
The receipt records exit status 0, 321.627 seconds elapsed, 1,574,408 child
output bytes, zero terminal-queue dropped bytes, a 600-second output-idle
deadline, and a 3,600-second hard deadline. Thirty-second heartbeat records
updated elapsed time, quiet time, byte count, process ID, and the latest output.
The supervisor therefore exposed active compilation instead of leaving a
silent, unbounded build. The retained log contains 1,575,977 bytes. A search
for compiler warnings, errors, fatal diagnostics, timeout stops, incomplete
cleanup, and dropped-output records returned an empty set.

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
| Standalone text | 8,234 | 7,856 | -378 | 139,328 process-window bytes before stack |
| Standalone data | 212 | 200 | -12 | included above |
| Standalone BSS | 196 | 72 | -124 | included above |
| Standalone raw a.out | 8,480 | 8,088 | -392 | n/a |
| Standalone packed payload | 7,400 | 7,150 | -250 | n/a |
| Standalone packed blocks | 9 | 8 | -1 | n/a |
| Utilbox text | 53,316 | 54,068 | +752 | 88,552 process-window bytes before stack |
| Utilbox data | 581 | 581 | 0 | included above |
| Utilbox admitted BSS | 4,255 | 4,255 | 0 | included above |
| Utilbox raw a.out | 53,929 | 54,681 | +752 | n/a |
| Utilbox packed payload | 43,803 | 44,514 | +711 | n/a |
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
```

The standalone map selects 35 DiscoBSD libc members. The utilbox map selects
112. The implementation continues to use the archive members already shared by
the box, including `fwrite.o`, `strtol.o`, `strchr.o`, `printf.o`,
`vfprintf.o`, and `doprnt.o`; it adds no private runtime library. The source
keeps the applet in one relocatable member, and the established BSS overlay
admits 4,255 bytes rather than utilbox ELF's unoverlaid 10,969-byte sum.

Standalone installation would recover one block inside the standalone file
but add the complete eight-block packed executable beside utilbox. The shipped
hard link remains the smaller complete-root composition.

## Examined offsets

The first strict-C17 implementation increased utilbox text by 1,984 bytes.
The accepted parser reduced that growth to 752 bytes through these bounded
mechanisms:

- Global ILP32 argument indices replace heap-backed conversion state.
- One conversion parser normalizes repeated flags into a fixed 16-byte buffer.
- Width and precision parse as `long`, reject values outside 65,535 before
  narrowing, and share one range diagnostic.
- `%b` measures and emits decoded bytes without a second allocated buffer.
- Numeric conversion preserves the established `strtol()` path instead of
  importing `intmax` or locale helpers.
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

Native strict C17, native ASan/UBSan, and ILP32 executions each pass 63 checks.
The exact production source also compiles as strict C17 for Cortex-M0+. The
witnesses cover `%b`, `\c`, embedded NUL bytes, byte precision, field width,
ordinary and numbered format reuse, malformed and nonnumeric continuation,
quoted-byte operands, missing operands, width and precision bounds, narrowing
wrap, malformed formats, repeated flags, and failed standard output.

The requirement remains `partial` and `open`. The deciding next action is to
add Issue 8 `*m$` width and precision selection, define and test mixed numbered
and unnumbered behavior, then remeasure utilbox and the complete packed root.
Floating, wide-character, and locale behavior remain outside the first-release
byte-locale policy.

Recovery scripts gain binary-clean `%b`, immediate `\c` termination, numbered
conversion selection, bounded dynamic fields, repeated format application, and
truthful failure status. Reverting implementation commit
`6879c95f318e6232993e53bf2dbaadfd4d4a35c3` restores the base utilbox and one
packed-root block while removing those behaviors.

## Approval

Reviewer approval remains pending. The reviewer must reproduce the standalone,
utilbox, largest-process, kernel, and complete-root measurements; inspect the
selected behavior witnesses and archive composition; and approve the retained
margins, rejected offsets, user-visible benefit, rollback point, and runtime
stack and heap uncertainty before merge.
