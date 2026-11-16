# Context-last sorting contracts and resource isolation

POSIX Issue 8 [qsort](https://pubs.opengroup.org/onlinepubs/9799919799/functions/qsort.html)
adds `qsort_r(base, count, width, compare, context)` with the comparator type
`int (*)(const void *, const void *, void *)`. The context reaches every
comparison unchanged. An empty array causes neither comparison nor rearrangement.
Each comparison receives element addresses within the array. Consistent
comparisons define the ordering; records retain their complete byte contents,
and the relative order of equal records remains unspecified.

## Public and ownership boundaries

`include/stdlib.h` exposes `qsort_r` in default/BSD and Issue 8 modes, including
`_POSIX_C_SOURCE=202405L` and `_XOPEN_SOURCE=800`. Explicit ISO, Issue 7 and
XSI Issue 7 modes keep the new identifier available to the application.
`_featuretest.h` owns selector precedence. Existing declarations keep their
visibility. Standalone, repeated and reordered inclusion, exact callback types,
rejected opposite visibility and rejected context-first signatures supply the
header witnesses. Smaller C separately compiles and assembles declarations
under the same eight selected modes.

The caller owns the record array and context for the complete call. The
implementation allocates no storage and stores no callback state globally.
`lib/libc/gen/qsort.c` defines one source mechanism with compile-time typed
variants; `qsort_r.c` selects the context-last variant. Its automatic call state
contains the record width, thresholds, comparator and context. The original
variant retains its two-argument callback. Function-pointer casts and
function/object-pointer conversion supply no ABI bridge.

The implementation retains the existing smaller-side recursion and insertion
cleanup. Every recursive child has at most half the parent's records, giving
at most logarithmic active partition depth. This statement bounds the sort's
own recursion; a user comparator can recurse or allocate independently.
Worst-case comparison work remains quadratic. Duplicate grouping and
introspective comparison bounds belong to the separate search/sorting package.
Inputs must designate an actual array with consistent comparisons. No overflow
check can validate an arbitrary caller pointer or establish that array's size.

## Donor mechanisms and competing costs

Source discovery uses `git show REV:lib/libc/stdlib/qsort.c` and
`rg -n 'cmpnocookie|cookie|qsort_r|I_AM_QSORT_R'`. The immutable revisions are:

| Donor | Revision | Mechanism and target treatment |
| --- | --- | --- |
| DragonFlyBSD | `805ca132aededa6ae4c8b3499a2d5c934d78fe56` | Typed variants and equal-key partitioning; its context-first ABI and unconditional left recursion require separate target treatment |
| FreeBSD | `f3ec447a1c0697f024dcf12929552e4ee9a420e0` | Compile-time typed ordinary/context-last variants preserve direct callback types |
| NetBSD | `dc5f6b2d8a00d0780c9a3f1ae12a02d78d60e745` | Shared context-last core; its ordinary wrapper passes a function pointer through an object pointer, outside the strict C17 assignment constraints |

A typed struct containing the ordinary comparator removes the NetBSD adapter's
conversion constraint. Extracted GCC/Clang native and ILP32 probes execute that
struct adapter, while all four strict C17 controls reject direct function/object
pointer assignment. These controls classify portability; they make no claim
about NetBSD's intentional compiler extensions.

The ordinary baseline is `eacedc9f15733fabd6a71fd64846a3da0be0e3d6`.
ARM GCC 16.2.0 at `-Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft` produces:

| Mechanism | Text bytes | Data/BSS bytes | Compiler-reported frame bytes |
| --- | ---: | ---: | ---: |
| Existing ordinary sort | 480 | 0/0 | 56 entry; 48 per active partition |
| Independent context sort | 512 | 0/0 | 64 entry; 48 per active partition |
| Typed ordinary adapter experiment | 28, plus the 512-byte context core | 0/0 | 24 wrapper; 8 comparator adapter, plus context-core frames |

The adapter saves 452 text bytes when both interfaces link, while adding 60
text bytes to an ordinary-sort-only consumer and an adapter call per comparison.
Independent variants preserve the complete ordinary-sort object byte-for-byte
under the matched cross invocation. That property isolates existing callers
from this interface addition; native archive and root storage still require
matched whole-distribution measurement. No timing result establishes the adapter's
workload cost. Five placements of the context field retain the same 512 text
bytes and frame sizes. Hoisting context into a local variable increases text
to 516 bytes and partition frames to 56 bytes. A fourth helper argument reduces
text to 502 bytes and the entry frame to 56 bytes, while increasing partition
frames to 56 bytes. For D active partition frames, the latter changes the
compiler-reported sort frames by 8(D - 1) bytes. The candidate retains the
48-byte recursive frame because peak RAM precedes text reduction. These
experiments change neither the public ABI nor the underlying algorithm.
Both mechanisms have prior art; target composition and measured
caller isolation supply the engineering contribution.

## Calibrated behavior and caller closure

`qsort_context_test.c` checks 78,740 cases: every ternary key array through eight
elements, four record widths, both ordering directions, unaligned arrays,
large equal/sorted/reverse/periodic/mixed inputs, and nested context/ordinary
sorts in both directions. Independent ordering, multiplicity and original-record
byte checks avoid requiring stability for equal keys. Comparators return
`INT_MIN`, zero and `INT_MAX` to expose arithmetic on comparison results.
A null context reaches its comparator unchanged. Empty input triggers no calls.

Five compiled mutations drop context, reverse comparisons, skip sorting,
share call state and lose a record byte. The shared-state mutation can retain
a nested comparator's expired automatic context; AddressSanitizer's specific
`stack-use-after-return` diagnostic in `compare` is an accepted rejection for
that mutation. Successful production execution still requires exit zero and
empty standard error. Compiler errors and unrelated runtime failures never
satisfy a behavioral control.

The focused GCC, Clang ILP32, sanitizer and ARMv6-M QEMU runs pass. The target
gate passes header, Smaller C and frame checks. QEMU uses newlib/Linux-EABI
fixture support; physical-board execution remains unrun. The full libc, full
converted a.out archive and reduced native archive each select `qsort_r.o`.
The a.out contract takes both sort functions' addresses and rejects each
function's removal from either archive alongside the existing string/lifecycle
controls. Linking both functions exposes a missing integer-divide provider in
the full converted archive: `__aeabi_uidiv` remains undefined. The reduced
archive already contains `aeabi_div.o`. The full archive selects the existing
RP2040 divider or Cortex-M4 instruction provider according to the same machine
choice as the cross libc. Both archives then link, and twenty member-removal
controls reject, including removal of each divider provider. This repair changes
archive membership; divider assembly and hardware behavior remain unchanged.
Clang-tidy core and Unix analyzer checks report no diagnostics for either sort
translation unit under the ARMv6-M target.

Replay after the required logged build, with caller-selected `PYTHON`:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-libc-qsort-context check-libc-qsort-context-cross
bmake MACHINE=rp2040 check-libc-qsort check-libc-aout-contracts
bmake -C tests/libc_contracts MACHINE=rp2040 REQUIRE_QEMU=1 check-qemu
```

The logged target build and native archive omission gate pass. The
[resource review](context-sort-resource-exception.md) records matched clean
distributions: 668 added native archive bytes and one root data block, with the
other 156 numeric surfaces unchanged. The owner approves the exact vector;
the complete ILP32 inventory passes 196 of 196 variants. Aggregate and hosted
qualification after the header-fixture repair and reviewed integration remain open.
The compatibility ledger records partial implementation with host evidence.

## Relative header fixture closure

`tests/libc_tempfiles/header_contract.c` includes the target `stdio.h` and
`stdlib.h` by relative path. The latter includes `_featuretest.h` through its
installed include search path. The fixture's original compiler recipe omits
that path. Local and hosted Linux execution report
`fatal error: _featuretest.h: No such file or directory`; hosted macOS reports
`error: '_featuretest.h' file not found with <angled> include`. The complete
ILP32 execution inventory covers its enumerated runtime variants and supplies
no result for this separate header-only fixture.

The fixture Makefile supplies `-isystem ../../include` and names both public
headers, `_featuretest.h` and `sys/cdefs.h` as prerequisites. The unmodified
recipe reproduces the named missing-header failure; the repaired temporary-file
suite and strict C17 GCC/Clang header compilations pass. The installed public
header retains the same lookup contract as `string.h`. Source search uses
`rg -n 'include/stdlib.h' tests` to identify both relative-header consumers;
`tests/libc_contracts` already supplies the include path and header prerequisites.
