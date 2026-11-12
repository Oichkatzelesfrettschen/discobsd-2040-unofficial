# Quicksort duplicate partitions and comparison cost

DiscoBSD `lib/libc/gen/qsort.c` at `62b89c39f7a4774cd3c79efe4bbbf926e04ffb72`
has SHA-256 `eac853109c4d2ecb231e91d51ecd1e689458f0a6f0dda44880be104dc2a55c9d`.
Its smaller-partition recursion bounds stack depth, while alternating two-key
inputs expose nearly quadratic comparison growth in the measured domain.
The [migration queue](c17-semantic-migration-plan.md) keeps context-aware
callbacks, duplicate partitioning and worst-case comparison bounds as distinct
contracts. The source search is `rg -n 'qst|qcmp|thresh|compar' lib/libc/gen/qsort.c`.

## Measurement and falsifiers

The fixture compiles the unchanged production source under the private name
`measured_sort`. It tests five input families at eight powers of two from 32
through 4,096 records: equal keys, alternating zero/one, ascending, descending,
and symmetric ascending/descending halves. Each comparator increments a counter
and compares integers without subtraction overflow. Independent adjacent-order
and histogram oracles verify ordering and multiplicity. The fixture requires
at least `n-1` recorded calls for these observed executions to expose a disabled
counter; that requirement makes no universal claim about every possible sort.

GCC 16.2.1 20260810 native and ILP32, and Clang 23.1.4 native, each pass all
40 cases with identical counts under strict C17, `-O2`, and
`-Wall -Wextra -Werror`. These are host source executions. Comparator counts
measure algorithmic work; they supply neither target cycle counts nor board
latency or stack measurements.

| Records | Equal or ascending | Descending | Alternating zero/one | Symmetric halves |
| ---: | ---: | ---: | ---: | ---: |
| 32 | 136 | 137 | 334 | 250 |
| 64 | 329 | 330 | 1,166 | 645 |
| 128 | 778 | 779 | 4,426 | 1,667 |
| 256 | 1,803 | 1,804 | 17,012 | 3,970 |
| 512 | 4,108 | 4,109 | 66,850 | 9,779 |
| 1,024 | 9,229 | 9,230 | 265,582 | 24,227 |
| 2,048 | 20,494 | 20,495 | 1,053,778 | 56,878 |
| 4,096 | 45,071 | 45,072 | 4,204,550 | 133,610 |

A control that compares adjacent records but leaves them untouched rejects
with order-error exit 1. A control that performs those comparisons and then
fills every record with zero rejects with multiplicity-error exit 2. A copy
of the fixture with its unique `comparisons++` removed, linked to the original
sort, rejects with counter-error exit 3. All three compilers reproduce each
specific rejection. The first no-operation control lacks comparator calls and
therefore reaches exit 3 before testing order; that attempt establishes no
order-error rejection. The calibrated controls retain comparator calls so each
failure exercises its intended oracle.

## Partition geometry explains the cost

A separate instrumented source copy logs partition cardinalities immediately
before `i = (j = mid) + qsz`. The insertion records `(max-base)/qsz`,
`(mid-base)/qsz`, `(max-mid-qsz)/qsz`, and the integer pivot. For 32 alternating
records, the first partition has one record left and 30 right. The next
partitions have `(left,right)` sizes `(28,1)`, `(26,1)`, `(24,1)`, `(1,22)`,
`(1,20)`, `(1,18)`, `(1,16)`, and `(1,14)`.

The smaller side stays below the four-record recursive threshold during this
trace. The larger side continues through the loop, so low stack use coexists
with repeated scans of an almost complete partition. Equal-key scans terminate
around a single pivot instead of removing an entire equal-key band. The measured
counts therefore support duplicate-aware partitioning as a targeted experiment.
The finite trace supplies no general asymptotic proof for every input size.

DragonFlyBSD `lib/libc/stdlib/qsort.c` at
`805ca132aededa6ae4c8b3499a2d5c934d78fe56` supplies the complementary
Bentley-McIlroy equal-band mechanism. Its left-side recursive call does not
choose the smaller partition. Preserve DiscoBSD's smaller-side recursion when
adapting equal-band partitioning. A recursion budget alone fails to detect the
observed long iterative chain; an introspective budget decreases on every
partition, including a tail iteration. Allocation-free heapsort supplies a
possible fallback after that budget expires, whereas the existing exported
`heapsort` allocates a record-sized temporary and can fail.

The C17 adaptation also resolves DragonFly's null-pointer subtraction in
`SWAPINIT` and typed-word swap assumptions. Alignment does not establish
permission to access an arbitrary effective type through `long *`; byte swaps
or a separately justified copy primitive retain the object-access contract.
These source constraints require independent target design and measurement.

Equal-band partitioning, smaller-side recursion and introspective fallback
are established techniques. The proposed target composition combines them
under per-call callback state, C17 object rules and the fixed process window.
Acceptance requires exact permutation and ordering witnesses,
nested callbacks, varied record widths/alignment, adversarial comparison
bounds, target stack/object measurements and matched shipped resources.
No replacement implementation or POSIX conformance claim follows from the
host counts alone.

## Adapted donor control and target object costs

The pinned DragonFly source hashes to
`1bffe62f9898b7a380207e0e2cf5d1277edf47ab193c3500d83abcf3a6e43abf`.
A scratch adaptation forces the existing byte-swap path with
`#define SWAPINIT(a, es) swaptype = 2;`, replaces the conditional `__unused`
annotation on `med3` with `(void)thunk;` in its body, and expresses the
nonnegative final extents as
`min((size_t)(pd - pc), (size_t)(pn - pd) - es)`.
The last change resolves a signed/unsigned diagnostic that rejects the first
compilation under `-Werror`. The original notice remains verbatim. Partition
selection, recursion, insertion fallback and callback ordering retain the donor
body. The adapted file has SHA-256 `910385e00ee0a9c11607ce71643222ce6080e67c28a144d6fec356345541b0f4`.

The same 40-case fixture passes for this adapted source under GCC native,
GCC ILP32 and Clang native, with identical counts. At 4,096 records the
adapted donor uses 4,107 comparisons for equal keys, 6,167 for alternating
zero/one, 12,292 for ascending keys, 16,399 for descending keys and 48,723 for
symmetric halves. These paired executions change several donor policies
simultaneously; they establish the complete adapted algorithm's behavior on
the matrix. They do not isolate each policy's individual effect.

ARM GCC 16.2.0 compiles both sources with `-std=c17 -pedantic-errors -Wall
-Wextra -Werror -fno-common -mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian
-mthumb -mfloat-abi=soft -ffreestanding -Os -nostdinc -isystem include
-fstack-usage`. DiscoBSD's object has 480 text bytes; the adapted donor has
568 text bytes. Both have zero data and BSS and import only `__aeabi_uidiv`.
Compiler local frame reports are 56 bytes for DiscoBSD `qsort` and 48 for
`qst`; the adapted donor reports 48 for `qsort`, 24 for `med3.constprop` and
12 for `swapfunc.constprop`. Recursion, callback frames and complete application
stack occupancy remain separate obligations. These standalone measurements
supply no shipped-resource exception, linked-size result or timing acceptance.

The control's 88-byte text increase prevents treating the large comparison
reduction as free capacity. A target replacement preserves bounded recursion,
measures duplicate handling and introspective fallback separately, and obtains
matched linked resources before integration. The default nonpositive-growth
policy remains in force.

## Replay fixture

Save the following fixture as `qsort_comparisons.c` in a caller-owned build
directory. Its SHA-256 is
`8b00e5b2c75a92b8b23593e7579141723e9bf6d80237e222533510970ec026f4`.

```c
#include <stdio.h>
#include <stdlib.h>
static size_t comparisons;
void measured_sort(void *, size_t, size_t, int (*)(const void *, const void *));
static int compare(const void *a, const void *b)
{
    const int x=*(const int *)a, y=*(const int *)b;
    comparisons++;
    return (x>y)-(x<y);
}
int main(void)
{
    int values[4096];
    for (unsigned int mode=0; mode<5; mode++) {
        for (size_t n=32; n<=4096; n*=2) {
            unsigned int before[4097]={0}, after[4097]={0};
            for (size_t i=0; i<n; i++) {
                values[i]=mode==0 ? 0 : mode==1 ? (int)(i%2) :
                    mode==2 ? (int)i : mode==3 ? (int)(n-1-i) :
                    (int)(i<n/2 ? i : n-1-i);
                before[values[i]]++;
            }
            comparisons=0;
            measured_sort(values,n,sizeof(values[0]),compare);
            for (size_t i=0; i<n; i++) {
                if (values[i]<0 || (size_t)values[i]>=n ||
                    (i>0 && values[i-1]>values[i])) return 1;
                after[values[i]]++;
            }
            for (size_t i=0; i<n; i++) if (before[i]!=after[i]) return 2;
            if (comparisons<n-1) return 3;
            printf("%u %zu %zu\n",mode,n,comparisons);
        }
    }
    return 0;
}
```

From the repository root, select the fixture and output directory explicitly:

```sh
set -eu
: "${QSORT_PROBE:?select qsort_comparisons.c}"
: "${QSORT_OUTPUT:?select an existing build directory}"
cc -std=c17 -pedantic-errors -Wall -Wextra -Werror -O2 \
    -Dqsort=measured_sort -c lib/libc/gen/qsort.c -o "$QSORT_OUTPUT/qsort.o"
cc -std=c17 -pedantic-errors -Wall -Wextra -Werror -O2 \
    "$QSORT_PROBE" "$QSORT_OUTPUT/qsort.o" -o "$QSORT_OUTPUT/probe"
"$QSORT_OUTPUT/probe"
```

Repeat both compilation commands with `cc -m32` and then with `clang`.
Compile each negative control with the same compiler and flags. The sorting
controls replace the production member with a `measured_sort` having its
exact prototype and this common first statement:

```c
for (size_t i = 1; i < n; i++)
    (void)cmp((char *)base + (i-1)*size, (char *)base + i*size);
```

The order control returns after that loop. The multiplicity control follows
it with a byte loop setting `n*size` bytes to zero. The counter control retains
the production member and replaces the fixture's unique counter increment
with a null statement. Require exit codes 1, 2 and 3 respectively; compilation
failure, timeout and another runtime error fail calibration.
