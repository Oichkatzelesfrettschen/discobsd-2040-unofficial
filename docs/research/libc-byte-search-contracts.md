# Byte search contracts and implementation scope

The production baseline is `cfe41205235bfc677e6fdf1a53a395aae2f6cb71`.
`memmem` scans candidate offsets and calls `memcmp`, so repeated prefixes
require quadratic byte comparisons. `memchr` compares a promoted plain char
with the original int argument. C17 7.24.5.1 requires both the search value
and accessed bytes to have unsigned-char semantics. The exact input
`memchr("", 0x100, 1)` therefore distinguishes the required conversion.

The package covers explicit-length byte searches and their cross/full-native/
reduced-native availability. `strstr` retains a separate lazy-NUL frontier.
Existing Issue 8/BSD visibility for `memmem` remains the interface contract.
The Issue 8 page `functions/memmem.html` has SHA-256
`993cd332641227de92d6ae1e63274643f4a0df1678efc899835e470a94e7b928` in
the retained standards archive. Its CX mark denotes an extension to ISO C.
Embedded NULs participate in matches; an empty needle returns the haystack;
an oversized needle returns NULL; every successful result names the first
occurrence. No error is defined.

The selected mechanism is the remembered-prefix Two-Way search from the
[retained experiment](search-bound-experiment/README.md), with an explicit
remaining extent instead of pointer subtraction. Both maximal-suffix passes,
the critical split, period check, right/left scans and remembered prefix
remain necessary parts of that mechanism. Tableless search has established
prior art. The adaptation claims bounded storage and local integration.

Ruling: the package retains remembered-prefix state and explicit remaining
length. This avoids the no-memory variant's separate first-match proof and
the end-pointer variant's ptrdiff_t representability assumption. The measured
experiment costs four more text bytes than the end-pointer form and retains
its 40-byte local ARM frame; final production costs require a fresh build.

Pre-flight: `memmem` and `memchr` share the byte-domain contract, native
archive selection and resource admission. The single-byte path binds the independent memchr provider, so its byte
conversion and extent contracts remain independently testable.
Both native archives need explicit `memmem` selection; the reduced archive
also needs `memchr`. The full native archive already contains `memchr`.

The prediction precedes production changes: the byte-conversion witness
rejects the baseline; a scalar oracle accepts the baseline's ordinary
`memmem` results; repeated-prefix inputs reject its linear comparison budget.
The replacement passes each oracle and budget. Controls must reject wrong
results, skipped candidates, removed prefix validation, disabled counting,
and omitted native members through their named diagnostics.

The validation command is `tests/libc_contracts/byte_search.py` with an
explicit source root and compiler. Host, ILP32, sanitized and ARMv6-M QEMU
executions retain separate results. Guard pages belong to host variants.
Source inclusion, target objects, native parser input, archive omission
controls, caller maps, clean resources and emulator images remain independent
qualification surfaces. Physical-board execution remains unauthorized.

Source discovery uses `rg -n 'memmem|memchr'` over `lib/libc`,
`lib/libc_aout`, `distrib/rp2040`, `usr.bin` and `tests/libc_contracts`.
The first native caller frontier is cut/textbox. Shared-library sizing,
`strstr`, locale and the remaining program retain their roadmap entries.

## Source and algorithm invariants

The immutable donor is FreeBSD `f3ec447a1c0697f024dcf12929552e4ee9a420e0`,
`lib/libc/string/memmem.c`, SHA-256
`cb0fd325441b8268d6aca15020c7561db019fb13bb5fc9d7022ac853b067d02c`.
The retained tableless adaptation supplies the exact MIT notice; the production
file also retains every line of its inherited OpenBSD provenance. NOTICE
section 18 carries the donor's redistribution terms.

Both suffix passes use unsigned SIZE_MAX as mathematical -1. The condition
`k < length - candidate` bounds the next pair of byte indices without forming
an overflowing sum. The chosen suffix normalizes through `split_suffix+1`.
The suffix-period theorem supplies the critical factorization: the tested
prefix lies within the needle, right-half mismatches exclude every skipped
candidate, and a period shift preserves the remembered prefix. The selected
nonperiodic shift is `max(split-1, length-split)+1`. Valid shifts are positive
and bounded by the admitted needle length, so subtracting each shift from
remaining bytes preserves extent containment. Empty and oversized needles
return before preprocessing. The implementation allocates no memory, owns no
persistent state and imports no helper function in the original measured ARM object. The
guarded-prefix revision imports memchr through its independent byte contract.

The standard Two-Way proof provides the algorithmic premise. Crochemore and
Perrin, JACM 38(3), 651-675 (1991), DOI 10.1145/116825.116845 identifies
that prior art; its original proof sections and a port correspondence are now available in
[the factorization record](libc-byte-search-factorization.md). The inspected Charras/Lecroq
[exposition](https://www-igm.univ-eiffel.fr/~lecroq/string/node26.html) and
pinned donor use the critical-factorization/period mechanism. The exposition's
suffix-length prose and code disagree on `001`; normalized code positions
govern the adaptation. Its nonperiodic shift differs by one from the donor.
Neither difference supplies an algorithm-priority claim.

A potential for each maximal-suffix pass is 2j+k+i, with i=-1 at entry.
Every comparison raises the potential enough to pay for one equality or two
ordered tests; its maximum is 3m-3. Two passes therefore cost at most 6m-6
byte-order evaluations. Period validation adds at most m-1. Conditional on
the critical-factorization theorem, remembered-prefix search charges at most
2n additional evaluations to position advances and saved matched bytes.
The finite executable budget is the conservative 2n+7m. The test counter
counts byte-order evaluations, including preprocessing; it supplies neither
cycles nor a whole-process timing bound. The independently scanned scalar
oracle determines the first matching offset. Protected-page tests and
sanitizers expose different memory failures while sharing that result oracle.

The initial baseline counter omits the scan's first-byte comparison. A private
instrumented baseline counts that comparison as well as scalar memcmp/memchr
work and rejects at n=32, m=2, count=93, above budget 78. The corrected
counter instrumentation is a test preparation change; production baseline
behavior remains unchanged. The conversion baseline rejects c=-512, n=1.
The native baseline rejects the new archive fixture for missing memmem.o.

Native, ILP32, sanitized and ARMv6-M QEMU matrices pass their result/domain
checks and six mechanism mutations in the initial candidate. A seventh control
sets the work budget to zero and requires the named comparison-bound failure.
The original strict target objects contain 312 text bytes/40 local frame bytes for
memmem and 22 text bytes/zero frame bytes for memchr. Source hashes, complete
clean resource measurements and clean-head qualification remain required.

Rulings and residuals: the scope retains existing strstr and keeps shared XIP,
physical timing, transitive caller-stack peaks and storage traffic visible as
separate obligations. An incorrect scope choice delays strstr's worst-case
work improvement; it supplies no unsupported claim for that function.

The clean distribution at 64b42d78 completes after the explicit native source
registry receives memmem. Focused qualification passes all byte-search host,
ILP32 and sanitizer matrices with seven calibrated controls, both native
archive contracts with 28 omission controls, the complete board sink, public
string visibility and cross-header checks, and all 24 Smaller C tests with
ARM execution. The first focused run stops at the C17 classification ledger:
memchr's prototype repair removes its old-style-definition hazard. The source
ledger generator also discovers the new function-pointer fixture hazards and
two new test translation units. Its exact delta retains every existing row,
refreshes those two classifications and adds two test rows: the host fixture stays unreviewed, while the native
fixture follows the existing migration-open Smaller C classification.
The replacement focused run passes on clean head e608a3f1 in 61.8 seconds,
after a successful 18.9-second logged build. The required execution-tool
calibration, profile controls, changed text/comments and ARM static analysis
also pass. The ARM matrix includes the seventh zero-budget control.
Aggregate, ILP32 inventory and Renode qualification remain open.

## Guarded-prefix admission

The original aggregate passes on clean source 5a74d74a: 67.020 seconds of
build and 3,051.870 seconds of checks, 3,118.892 seconds total. All configured
skips remain in its immutable logs. Independent instruction simulation then
exposes a common-case cost: an absent one-byte needle in 4,096 bytes uses
94,254 instructions versus 28,699 at baseline. The repeated-prefix case
uses 52,002 versus 1,755,811. These counts separate common work from the
worst-case comparison guarantee; they establish no board timing.

The guarded-prefix revision adds single-byte memchr, cached two-byte matching,
a first-byte filter guarded by the ptrdiff_t extent domain, and one complete
initial probe. Each maximal-suffix comparison retains its order once.
The [primary proof correspondence](libc-byte-search-factorization.md) includes
all wrapper work in the existing 2n+7m budget. A forced unfiltered variant
preserves the full valid-object domain. Native, ILP32, sanitizer and ARM
experiments pass their complete case matrices; their maximum comparison
measurement becomes 8,190 through the two-byte path. The per-input budget
and nonzero-counter checks remain authoritative; the observed maximum is an
informational value.

The experimental target object uses 454 text bytes and a 48-byte local frame,
with memchr as its sole import. One-byte long scans use 28,694 instructions
versus 28,699 at baseline. The long binary match uses 7,331 versus 7,314;
the repeated-prefix case uses 51,217 versus 1,755,811. The
check-libc-byte-search-instructions gate enforces baseline+96 instructions on
its common cases and one quarter of baseline on its adversarial cases. Its
13-case corpus includes scalar result checks, read-only inputs and callee
state. Four controls reject bypassed fast paths, quadratic scanning, wrong
results and disabled instruction counting. The pinned baseline is cfe41205;
this is a controlled algorithm reference, distinct from the current resource
baseline on main e64e2150.

The original resource request and source-review scope bind their exact earlier
candidate. They grant no admission for the guarded-prefix revision. The clean
distribution on d1b55dd3 and the refreshed main e64e2150 establish the
[revised resource vector](libc-byte-search-resource-exception.md): 748 native
archive bytes, 376 loaded textbox bytes and two root blocks, leaving 227 free
blocks. Standalone cut adds 376 loaded bytes with unchanged allocations.
The 48-byte direct provider chain replaces the baseline's conservative
36-byte sum; process runtime peaks remain unmeasured.

The 19.012-second logged build passes on clean d1b55dd3. The 77.342-second
focused host qualification passes on clean e6b2b417 with identical provider
hashes and no configured skips. The earlier sandbox run skips ILP32 and
sanitizer execution after blocked compiler probes; its narrower receipt stays
retained. The [qualification record](libc-byte-search-qualification.json)
binds source hashes, script and full-log hashes to the host, ILP32, sanitized,
cross, ARM QEMU and modeled-instruction results. The
[instruction receipt](libc-byte-search-instructions.json) retains all 13
case counts, independent results and four rejecting controls. The owner
approves the exact revised resource vector and the bounded immutable source
review. The review finds no actionable valid-object defect in pinned d1b55dd3;
it executes no gates and independently retrieves neither the external proof
nor the donor. Parent source inspection and executable qualification supply
their separate evidence.

Clean db0ea8e7 passes all 216 required inventory variants in 631.332 seconds.
The configured i386-time32 archive-display skip remains outside those required
variants. The matched flash build passes in 1.784 seconds. PICO_UART and
PICOCALC_PSRAM Renode gates pass in 558.272 and 477.505 seconds respectively,
including native compilation and ctime output in 2046. Their receipts bind
kernel and image hashes separately from byte-search fixture execution. Hosted
CI run 43689715154 passes on the same source. The parallel aggregate fails
at a detached make query after a successful clean build. Its complete receipt
remains in the [aggregate record](libc-byte-search-aggregate.json).
The documented serial aggregate passes on frozen clean db0ea8e7 in
3,563.426 seconds: build and every configured non-hardware tier exit zero,
with no terminal queue loss. Its three explicit four-byte timestamp SKIP
records, nine existing shell XFAILs and enumerated reference/namespace limits
retain their narrower claims. Ownership releases
with no pending process groups. Reviewed integration remains the delivery
step. Physical-board execution stays unauthorized.

The namespace gate checks 39 programs and skips coremark because its source
lookup omits the benchmarks family. Shared-XIP namespace acceptance retains
that unresolved owner and its deciding experiment. The host complex reference
grid and 30 exact-tie formatting comparisons retain their documented reference
limits. The ARM object census and optional calculator namespace scan separately
filter objects built for the host. These observations narrow their respective
claims; byte-search fixture execution and static resource receipts retain
their own complete selected denominators.

The parallel aggregate exposes an existing make-query diagnostic in
tests/rp2040/usercopy_span/check.py. Its unchanged subprocess helper closes
jobserver descriptors while retaining MAKEFLAGS. A query on clean main with
closed descriptors reproduces the warning; clearing MAKEFLAGS and MFLAGS
removes it and returns identical compiler flags. The helper environment
correction uses the existing detached-query environment policy from
tests/warning_policy/check.py and tests/kernel_metadata/dispatch_contracts.py.
The pre-change make-query regression rejects four query cases through the
named jobserver diagnostic. The prediction is that clearing both MAKEFLAGS
and MFLAGS for detached queries makes all selected environment variants pass,
while the unsanitized direct-make control retains its named warning.
The explicit MACHINE=rp2040 route and source Makefiles own these default
compiler profiles; inherited parent scheduling flags supply no query input.
This diagnostic supplies no application or compiler failure claim.
The repaired helpers pass their owning gates under actual parallel make,
including all caller-type controls, user-copy cases, the known-bad direct
query and source/execution accounting. Clean 768be134 passes the complete
parallel cross tier in 509.161 seconds and its clean focused checks in
28.242 seconds. The complete production aggregate and helper qualification
retain separate source and execution identities. The original independent
review covers the production provider. The host-only repair receives local
adversarial review and calibrated execution; its optional external follow-up
remains unlaunched while the additional transmission approval is unanswered.

The deliberate shell SIGABRT case waits in the host core collector and then
passes with SHTEST OK. Five-second probes with a zero core limit and a
header-only core filter each retain the delay. These results refute those
two environment remedies. Host settings and shell source remain unchanged;
the completed signal test supplies its behavioral evidence.
