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
archive selection and resource admission. The tableless provider owns its
single-byte path, so `memchr` correctness remains independently testable.
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
persistent state and imports no helper function in the measured ARM object.

The standard Two-Way proof provides the algorithmic premise. Crochemore and
Perrin, JACM 38(3), 651-675 (1991), DOI 10.1145/116825.116845 identifies
that prior art; its full paper remains unread. The inspected Charras/Lecroq
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
Current strict target objects contain 312 text bytes/40 local frame bytes for
memmem and 22 text bytes/zero frame bytes for memchr. Source hashes, complete
clean resource measurements and clean-head qualification remain required.

Rulings and residuals: the scope retains existing strstr and keeps shared XIP,
physical timing, transitive caller-stack peaks and storage traffic visible as
separate obligations. An incorrect scope choice delays strstr's worst-case
work improvement; it supplies no unsupported claim for that function.
