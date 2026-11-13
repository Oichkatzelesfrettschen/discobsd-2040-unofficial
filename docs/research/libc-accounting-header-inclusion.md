# Accounting public-header inclusion and record layouts

The installed `grp.h`, `lastlog.h`, `mtab.h`, `ttyent.h`, `tzfile.h` and
`utmp.h` headers define records without include guards. Repeated inclusion
redefines those records. `grp.h` additionally requires `gid_t` and `size_t`,
and `lastlog.h` requires `time_t`, without including their declaring header.
Header guards give each record one declaration per translation unit;
`sys/types.h` supplies the two headers' prerequisites.

The source observation uses `rg -n 'struct|typedef|ifndef|define'` on those
six installed headers at `88abf7151e25b4fc1c01682587561f7c9197f7bc`.
The compiler screen enumerates `STD` and `LINKS` from `include/Makefile` and
tests standalone, repeated and type-header inclusion orders. Its 53-header,
212-case denominator exposes twelve headers with at least one rejected form.
The screen disables strict-prototype warnings to isolate inclusion defects;
the maintained six-header gate enables that warning and warnings as errors.
Remaining rejected headers retain independent migration units.

## Preserved declarations and ABI

The change adds guards and prerequisites while keeping every record field,
constant and function declaration. The headers define no storage and execute
no runtime operation. Existing record consumers retain their parsing,
ownership, lookup, file-format and concurrency contracts.

| Header and record | ARM size / alignment, bytes | Field offsets, in declaration order |
| --- | --- | --- |
| `grp.h`, `group` | 16 / 4 | 0, 4, 8, 12 |
| `lastlog.h`, `lastlog` | 28 / 4 | 0, 4, 12 |
| `mtab.h`, `mtab` | 68 / 1 | 0, 32, 64 |
| `ttyent.h`, `ttyent` | 24 / 4 | 0, 4, 8, 12, 16, 20 |
| `tzfile.h`, `tzhead` | 44 / 1 | 0, 32, 36, 40 |
| `utmp.h`, `utmp` | 44 / 4 | 0, 8, 23, 40 |

`group.gr_gid` retains its signed `int` type. `getgrgid` receives `gid_t`,
which the target defines as unsigned int. `lib/libc/gen/getgrgid.c` converts
the record field for comparison. Inclusion acceptance preserves this BSD ABI;
the POSIX group-field type and negative group-file values require a separate
caller and format audit. `lastlog.ll_time` retains `time_t`; the gate keeps
host long-width differences separate from the target's four-byte long.
`tzhead` retains the historical zone-file representation and its independent
parser contract.

Include guards have established prior art. The target contribution consists
of calibrated inclusion and record-layout checks across the cross and native
compiler paths. No algorithmic novelty claim accompanies the change.

## Compiler evidence and calibration

`check-accounting-header-contracts` exercises five mandatory profiles:
the selected host compiler in strict C17, native Clang in strict C17,
Clang targeting Cortex-M0+ in strict C17, and the evaluated ARM libc compiler
in GNU17 and strict C17. Each profile accepts four inclusion forms for each
header and rejects removal of each guard. Removing `sys/types.h` from each
dependent header must produce its named unknown-type diagnostic.
The fixed denominator contains 120 accepted cases and 40 expected rejections.
ARM profiles additionally check every record size, alignment and offset.
Group member and lookup types receive explicit type checks.

Four aggregate inclusion forms pass host preprocessing under Smaller C's
target identity, its Thumb compiler and the ARM assembler. Compile-time
array bounds check all six record sizes; callable group declarations remain
type-correct. The compiler build and a rejected invalid-input control bring
that denominator to fourteen operations. This path establishes acceptance of
expanded input. On-device preprocessing and runtime execution remain unrun.

The receipt records compiler identities, header hashes, source fixtures,
commands and diagnostics. Firmware CI owns the gate through `check-cross`
on Linux and macOS. Replay follows the repository's logged build prerequisite:

```sh
: "${PYTHON:?select the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-accounting-header-contracts
```

The focused gate passes 160 modern compiler cases and fourteen Smaller C
operations. Clean consumer rebuilds, matched static-resource captures,
aggregate qualification, hosted CI and reviewed integration remain open.
Default visibility defines this unit's namespace scope; ISO/POSIX selector
visibility and complete runtime interface conformance remain separate leaves.
Physical-board execution retains its separate authorization boundary.
