# Bounded sorting contracts

The production baseline at main `efb4a05a` uses 265,582 comparator calls for
1,024 alternating keys. The independent order/permutation fixture succeeds,
but `qsort_bounds_test.c` rejects the comparison budget with exit 4 and the
`comparison bound:` diagnostic. The retained observation is the production
red log for this baseline; the research replay pins its historical source.

The implementation hypothesis combines ordered pivot samples, bidirectional
equal bands, smaller-side recursion and a budget consumed on every partition.
Allocation-free heap fallback bounds worst-case comparator work without
introducing a failure return or record-sized allocation. The existing typed
ordinary and POSIX context-last callbacks preserve their declarations and
per-call state. Byte swaps preserve arbitrary object representations.

The deciding commands are the ordinary/context sort gates, their ILP32,
sanitizer and ARMv6-M executions, native declaration/archive checks, and the
new comparison-bound fixture. The predicted movement is the named duplicate
comparison failure from exit 4 to complete 56-case success. Existing ordering,
permutation, record-byte, nested-callback and visibility witnesses continue to
pass. A calibrated omitted-heap control and disabled-counter control reject
through their named oracles. A different failure refutes this prediction.

The [research mechanism](sort-bound-experiment/README.md) derives partition,
recursion and work bounds and records the source inspection. Production
admission still requires complete callback stack analysis, matched clean
resources, profile approval for positive constrained deltas, and runtime
qualification. The research object/frame costs provide no implicit resource
allowance. Physical-board execution retains its separate authorization.


## Comparator caller closure

The selected archive closure imports ordinary qsort in `/bin/box`, `/bin/ps`
and `/usr/bin/menu`. The maintained build additionally contains direct calls
in `nm`, directory `diff`, and `uuq`. The csh sources contain qsort calls but
`bin/Makefile` explicitly leaves csh outside the selected build. Commented
picoc registration is separate from an active call. Source search uses
`rg -n` for qsort calls and each comparator definition; archive-member receipts
supply the shipped selection independently.

The ps, menu and nm callbacks already use exact `const void *` parameters.
The ls, diff and uuq definitions use old-style record-pointer parameters.
An exact callback prototype prepended to the real ls translation unit rejects
with a prototype mismatch. A Cortex-M0 QEMU probe executes its existing
comparator: ascending names compare correctly, while reverse names fail
with exit 2. The direction flag stores -1 in target-unsigned plain char.
The hypothesis gives that flag signed-char storage and gives all three
callbacks exact prototypes with read-only record access. Type probes predict
acceptance; the reverse witness predicts exit zero; a plain-char substitution
must reproduce exit 2. This fixes a caller contract rather than assigning
library correctness to an incompatible callback.


The real-module target probes pass after the caller repair. Mutable-void
callback controls reject all three exact prototypes. ARMv6-M QEMU executes
name order, reverse order, timestamp order, directory grouping, job grade and
job-number ordering through the real callbacks and production qsort. A
plain-char direction substitution reproduces the ls reverse failure with
exit 2. These witnesses retain the existing warning profiles of the legacy
caller modules; they establish their callback contracts without claiming a
complete C17 migration of each translation unit. Uucp's existing C90 build
selection remains an explicit migration prerequisite.

The initial seven work-count families fail to detect a disabled partition
budget. The eighth family places even keys in alternating lower/upper halves,
then the odd keys. It permutes the admitted integer range. At 1,024 records,
the unbudgeted mutant uses 205,644 comparisons and rejects the same finite
budget; the production candidate passes all 64 pattern cases. This deciding
input strengthens the production gate beyond the retained research matrix.


## Maintained consumer closure

The read-only review identifies an omitted library-mediated consumer: tclsh
registers Tcl_LsortCmd, which calls qsort in libtcl. A complete source search
and linked disassembly inspection also identify maintained atc, boggle,
robots, wump and standalone ls. The initial six-program caller inventory
therefore supplies an incomplete maintained closure. The shipped archive
receipt remains valid for its three selected consumers.

The expanded target set contains box, ps, menu, standalone ls, nm, directory
diff, uuq, tclsh, atc, boggle, robots and wump. Eleven final disassemblies
contain `<qsort>:`; box's independently captured archive selection supplies
the twelfth executable. Source search covers every qsort occurrence under
lib, bin, usr.bin, sbin, usr.sbin, libexec, games and benchmarks. Csh stays
outside the selected build. Fortune's strfile uses HOST_CC and host libc;
fixture-only calls and commented picoc registration supply separate exclusions.

Atc, boggle and wump have incompatible record-pointer callback definitions.
Prepended exact prototypes reject all three real modules with argument/prototype
diagnostics before repair. The governing qsort declaration requires two
const-void pointers. The hypothesis gives each callback that exact definition
and preserves its record comparison. Boggle uses the actual pointer-element
width in its qsort call. The deciding cross/QEMU caller probes predict exact
prototype acceptance and unchanged game-domain ordering; mutable-void
prototypes must reject through the calibrated conflicting-type diagnostic.
Tcl and robots already have compatible callback parameters and receive
positive/negative type witnesses without a source behavior change. These
checks preserve each module's existing warning/language profile and supply no
whole-module C17 claim. All added optional costs require their own measured
amendment; the existing owner approval retains its exact scope.

## Partition scratch lifetime

The initial integrated candidate keeps partition variables in each 64-byte
recursive frame. A state-only factoring retains that frame size and raises
text; an unannotated separate partition helper also inlines into the recursive
frame. Those variants supply no stack saving. The selected implementation
keeps a separate partition call on the qualified GNU/Clang profiles. Its
56-byte partition scratch frame returns before either recursive child call.
The recursive frame returns to 48 bytes. Ordinary/context entry frames are
24/32 bytes; the heap sift and byte-swap frames are 40/12 bytes. ARM text is
800/826 bytes, with zero persistent data/BSS. No divider or allocator import
participates in the new sort.

A guarded noinline attribute controls this resource boundary on the qualified
compilers. The underlying sorting semantics remain C17 when another compiler
omits that attribute; that compiler's stack profile requires separate
qualification. The target gate reads static `.su` records, admits every known
frame only within its measured limit, and rejects an actual compiled control
with an additional 64-byte recursive stack object passed to an external
opaque function. The control never links or enters production. A new or
nonstatic frame fails qualification rather than acquiring an assumed cost.

The entry, recursive, partition, heap/swap and comparator frames compose along
call paths. Within one sort invocation, partition scratch returns before a recursive
child enters. Callback-driven nested sorts contribute their own chains. The
static measurements establish compiler-local frames, separately from actual
process peaks. The shipped selection is box/ls, ps and menu. Ps has a
2,424-byte main frame and a 16-byte comparator frame; menu's main/comparator
frames are 256/8 bytes; ls main/formatd/comparator frames are 40/56/16 bytes.
Recursive directory traversal adds its existing formatd frames. Runtime peaks
and matched process resources retain separate deciding measurements.

The inherited qsort header stays byte-identical. Its historical performance
note belongs to the retained source provenance and supplies no timing claim
for this implementation. The retained [research experiment](sort-bound-experiment/README.md)
keeps its 64-byte-frame candidate and exact historical results; the production
lifetime factoring receives its own measurements.

## Native divider witness

Removing qsort's divider dependency makes the original archive omission
fixture link successfully without the divider member. That observation
refutes the fixture's implicit dependency assumption. The repaired fixture
performs an explicit division of volatile unsigned operands. Both native
archives then reject all 24 member-omission controls, including their divider
providers, while the optimized qsort remains independent of division helpers.
The inherited native-linker archive-date warnings remain visible; these
results supply no warning-free native metadata claim.


The selected candidate passes the registered ordinary/context sort gates,
the cross/header/Smaller C gate and calibrated frame check, both native
archive gates, source accounting and its 21 controls. The separate direct
host, ILP32, Clang sanitizer and ARMv6-M QEMU gates also pass. Retained logs
and hashes reside under `.build-logs/bounded-sort-qualification/`; the
[static frame receipt](libc-bounded-sort-stack.json) binds the compiler facts
to exact source hashes. Analyzer-only clang-tidy checks pass for both provider
variants; Ruff passes for the changed Python gates. Full broad clang-tidy
and physical-board results remain separate claims.

The [matched resource comparison](libc-bounded-sort-resource-exception.md)
records the exact positive costs against main `a849d47a`. Current-image
emulator qualification, aggregate gates, final head checks
and reviewed integration remain open. Additional behavior and
stronger evidence are recorded separately from the existing qsort interfaces;
this work introduces no standards entity or full-conformance claim.

The expanded caller gate accepts all eight real modules and rejects each
incompatible declaration. ARMv6-M QEMU executes every added comparator and
its call through production qsort; the ls unsigned-direction control still
rejects. Static frame collection uses each module's own evaluated CC/CFLAGS,
including libtcl's own warning profile. These measured caller frames remain
separate from transitive runtime peaks.

## Conditional stack envelope

Let n be the record count and D(n) the conservative number of simultaneously
live recursive sort frames. Every partition removes at least its pivot's
equal band. Its smaller strict side therefore contains at most
floor((n-1)/2) records. Tail iterations reuse their current frame. A leaf of
at most eight records uses insertion sort in that same frame.

For 2 <= n <= 8, D(n)=1. A second live frame first occurs at n=9. For a
third or later frame, the smaller child itself requires a partition, so the
minimum record counts satisfy N(2)=9 and N(d)=2*N(d-1)+1. Thus
N(d)=5*2^(d-1)-1 for d>=2 and

    D(n) <= 1 + floor(log2((n+1)/5)), n >= 9.

An independent recurrence calculation checks this closed bound at every
integer count from 2 through 196,608. The resulting maximum is sixteen sort
frames. This limit conservatively allows one-byte records to occupy the
entire fixed process window; actual executable and data occupancy reduce
that available record count.

Let C be the complete transitive stack cost of one comparator invocation,
including any nested sort. The qualified ordinary/context entry frames are
24/32 bytes; each recursive frame is 48 bytes. Partition scratch occupies
56 bytes and returns before a recursive child enters. The sift frame is
40 bytes and its swap leaf is 12 bytes; that path fits inside the larger
partition-plus-leaf bound. The resulting conditional library envelopes are

    S_ordinary(n,C) <= 24 + 48*D(n) + 56 + max(C,12)
    S_context(n,C)  <= 32 + 48*D(n) + 56 + max(C,12).

At the conservative process-window record limit these become 848/856 bytes
plus max(C,12). These expressions use measured compiler-local frames and a
source-derived recursion bound. They exclude the caller's stack, heap, kernel
execution and interrupt context, and they supply no runtime peak measurement.
Arbitrary nested callbacks keep C caller-dependent. The target contribution
is the lifetime composition: partition scratch contributes once to the active
call path rather than once per recursive level. The algorithm and recurrence
retain established sorting prior art.

The [finite recurrence receipt](libc-bounded-sort-stack-envelope.json) binds
that calculation to the provider source hash and its complete count domain.
