# Bidirectional equal-band sorting experiment

The bidirectional candidate passes 708,596 ordering, byte-preservation,
permutation and callback-context cases in each selected execution configuration.
It reduces duplicate-key and sorted-input comparison work in the measured
families, while increasing ARM text and partition-frame costs. Production
`qsort` and `qsort_r` remain unchanged. Target timing, full caller/resource
admission, and native archive integration remain open.

## Partition mechanism and bounds

`bidirectional.c` supplies typed ordinary and context-last comparator variants.
It orders the first, middle and last records, then places their median at
index zero. The pivot remains an actual array element during comparisons.
No record-sized temporary or allocation participates in sorting. Byte swaps
preserve arbitrary object representations without typed-word aliasing.

During partitioning, equal records occupy `[0,a)` and `[d,count)`, lesser
records occupy `[a,b)`, greater records occupy `[c,d)`, and the unknown range
is `[b,c)`. The pivot belongs to the first equal range. A left scan consumes
lesser/equal elements; a right scan consumes greater/equal elements. When
both stop, swapping their records consumes two unknown elements. The unsigned
right endpoint is exclusive, so `c-1` is evaluated only while `b<c` and `b>=1`.
Each iteration therefore shrinks the unknown range and preserves a valid pivot.
The final vector swaps place equal records between the two strict partitions.

The child sizes are `left=b-a` and `right=d-b`. Their sum is below the parent
size because the pivot's equal band contains at least one record. Recursing
only into the smaller child bounds simultaneously active partition calls by
`O(log n)`. The larger child continues through the loop.

The initial partition budget is `2*floor(log2(n))`. Every partition consumes
one unit, including an iterative continuation. At each budget level the
active ranges are disjoint and total at most `n`, so partition comparisons
cost `O(n log n)`. Budget exhaustion invokes allocation-free binary heapsort;
the sum of its disjoint leaf costs is also `O(n log n)`. Insertion leaves hold
at most eight records, adding `O(n)` comparisons overall. These algorithmic
bounds assume a comparator with a consistent ordering and a valid array whose
`count*width` extent exists. The finite tests independently exercise concrete
inputs; they do not establish target cycle counts.

## Pivot sampling experiment

Selecting the median record without ordering its sample positions passes the
initial correctness matrix but uses 175,759 comparisons on 4,096 descending
records. Ordering those three positions before moving the median changes the
partition's record arrangement and reduces that observed count to 40,967.
Median value selection alone therefore fails to explain the cost: sample
placement affects later partition geometry. This is a port experiment with
established sorting techniques, not a claim of a new algorithm or universal
performance improvement.

| 4,096 records | Production | Ordered-sample candidate |
| --- | ---: | ---: |
| Equal keys | 45,071 | 4,098 |
| Ascending | 45,071 | 40,968 |
| Descending | 45,072 | 40,967 |
| Alternating two keys | 4,204,550 | 6,149 |
| Symmetric halves | 133,610 | 127,062 |

The production counts retain their calibrated source in
[`qsort-partition-cost.md`](../qsort-partition-cost.md). `counts.c` separately
checks seven pattern families at powers of two from 32 through 4,096. It
checks order, key multiplicities, a live comparator counter, and the finite
budget `8*n*(floor(log2(n))+1)+64*n`. The production duplicate-heavy input
rejects that budget at 1,024 records with 265,582 comparisons. This expected
baseline rejection is evidence for the identified cost, not a general
correctness failure in production sorting.

## Execution, calibration and resource costs

The expanded existing context fixture enumerates ternary arrays through ten
records, across widths 1, 3, 7 and 48, ascending/descending order and displaced
bases. This crosses the eight-record partition cutoff. Larger families,
null context and nested ordinary/context callbacks remain included. Complete
record identity checks cover payload bytes in addition to key multiplicities.
Both the normal partition entry and a forced zero-budget heap entry pass
708,596 cases in host, ILP32, Clang address/undefined sanitizer, and ARMv6-M
QEMU execution. QEMU links newlib and the repository's Linux-EABI support;
it supplies instruction execution separately from DiscoBSD exec admission.

Five host controls compile warning-clean and reject through named behavior:
unchanged input violates order, a null substituted context loses identity,
a changed last key loses multiplicity, an omitted heap fallback violates
order, and a disabled comparison counter reports insufficient calls. The
positive comparison run requires its complete 56-pattern marker. The initial omitted-fallback control reaches the earlier no-comparator
assertion; its revised construction retains comparator calls to reach the
intended ordering oracle. Compiler errors and unrelated failures supply no
calibration. LeakSanitizer remains disabled for sandbox compatibility;
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

Matched ARM GCC 16.2.0, strict C17, `-Os -fno-builtin`, Cortex-M0+, Thumb and
soft-float object measurements report:

| Surface | Production | Candidate |
| --- | ---: | ---: |
| Ordinary text bytes | 480 | 784 |
| Context text bytes | 512 | 818 |
| Writable persistent bytes | 0 | 0 |
| Partition local frame | 48 | 64 |
| Heap sift local frame | Not selected | 40 |
| Byte-swap local frame | Inlined | 12 |
| Ordinary entry local frame | 56 | 24 |
| Context entry local frame | 64 | 24 |

The candidate imports no external provider. Entry, partition, sift and swap frames compose along call
paths, with comparator frames added separately. The table reports local
frames rather than total process stack measurements. A separate ordinary-sort
state-structure experiment keeps the 64-byte partition frame and increases
text to 796 bytes; that factoring receives no admission. The selected candidate
still needs complete stack composition, callback costs, archive selection,
packing, root blocks and workload timing before production replacement.

## Donor identity and replay

DragonFlyBSD `94448f622a06cde90ba42d688e9cab2448ad9ab3`,
`lib/libc/stdlib/qsort.c`, supplies the inspected equal-band and bidirectional
scan mechanism. Its SHA-256 is
`1bffe62f9898b7a380207e0e2cf5d1277edf47ab193c3500d83abcf3a6e43abf`.
The candidate implements the described mechanisms with exclusive integer
indices, byte swaps, smaller-side recursion and a partition budget. It imports
no donor source text, typed-word swaps, or historical context-first callback
signature. The baseline source remains the maintained DiscoBSD implementation
at main `efb4a05a42f1e0c146625271b80ed0e9f1705212`.

`run.py` freezes the candidate, expanded fixture and count source in a unique
`.build-logs/bidirectional-sort-*` directory. Each compile, link and execution
has its own JSON result. Expected status, diagnostics and complete matrix
markers decide acceptance. The final receipt records source hashes, the immutable baseline commit, tool
versions and whether ARM execution occurs. The checked-in `evidence.json`
retains the compact result and hashes of its local artifacts. An unavailable selected tool fails;
unselected ARM execution records `not run`. The frozen successful replay is
`.build-logs/bidirectional-sort-c6z3g5pc/`. This research replay remains outside
the required production execution inventory.

```sh
set -eu
: "${PYTHON:?select the interpreter}"
export PYTHON
"${PYTHON}" docs/research/sort-bound-experiment/run.py --arm
```

The replay needs a C17 host compiler, a working ILP32 compiler/runtime, Clang
with address/undefined sanitizer runtimes, and Python 3.10 or newer. `--arm`
adds ARM GCC with newlib, ARM binutils and `qemu-arm` with the `cortex-m0` model.

The maintained logged build, donor-ledger gate and source-denominator gate
pass for the experimental tree. Source accounting records 153 accepted,
543 excluded, 62 migration-open and 2,279 unreviewed paths; both new research
C files remain unreviewed for production migration. The donor frontier retains
13 roots and 23 selected investigations. Analyzer-only clang-tidy checks pass
for both comparator variants with warnings treated as errors; this scoped
analysis supplies no broad clang-tidy or runtime verdict. Ruff lint and format
checks pass for the replay driver.

## Production admission sequence

1. Preserve the inherited `lib/libc/gen/qsort.c` provenance and expose the
   candidate through its existing ordinary/context-last provider selection.
   `qsort_r.c` retains the shared-source build and its exact public signature.
2. Extend the existing context fixture across the partition cutoff and register
   the 56-pattern comparison-bound fixture in the owning libc sort gate. The
   current production source reaches the known comparison-budget failure.
3. Adapt the source mutations in `tests/libc_contracts/qsort_context.py` to the
   new mechanism. Preserve separate rejection of lost context, reversed order,
   skipped sorting, shared recursive state, and damaged record bytes. Add the
   budget/fallback and disabled-counter controls without treating compilation
   failure as rejection.
4. Run ordinary/context behavior, strict C17 and GNU17 compilation, ILP32,
   sanitizer, ARMv6-M QEMU, public target headers and Smaller C declarations.
   Verify native archive consumers and symbol selection separately.
5. Compose entry, recursive partition, heap/swap and comparator stack paths.
   Include multicall consumers and largest admitted process records; local
   `.su` figures alone supply no peak-stack admission.
6. Capture matched clean distributions against refreshed main. Compare each
   kernel, executable, native artifact and root-block surface independently.
   Any positive constrained delta follows the profile's measured exception
   process before integration.
7. Retain separate workload-operation counts, emulator/runtime checks and
   timing measurements. Physical-board execution requires its own authorization.
   Shared-libc qualification keeps its separately agreed 10% workload limit.
8. Update the donor, source and standards records with the delivered behavior,
   register any new required gate with its Makefile/testing/CI owners, and
   integrate through a reviewed PR with current-head checks.
