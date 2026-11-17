# Bounded sorting resource exception

The bounded ordinary and context sorts add 656 native archive bytes, 320
loaded bytes to each shipped qsort consumer, and one root data block. The
[compatibility profile](dpucp-2024.md) requires reviewer approval for these
positive constrained deltas. The owner approves this exact vector on
2026-11-15 UTC and directs continued qualification and review. Earlier signal,
native nm and sorting-interface approvals retain their exact historical scope.

## Matched source and receipts

The baseline is main `a849d47ace2d8a6a9c264bf9ee43d6dc8db9786e`.
The measured candidate is `0aa86d7d95a26b00e7d8bf21c68c58d0fd333ef3`,
which includes that main and production change `6d5e3714`. Both clean,
instrumented distributions complete successfully with matching collector,
toolchain and measurement-tool identities. The retained receipts are:

- [Baseline resources](libc-bounded-sort-resource-parent.json).
- [Candidate resources](libc-bounded-sort-resource-candidate.json).
- [Paired comparison](libc-bounded-sort-resource-comparison.json).
- [Maintained unshipped programs](libc-bounded-sort-resource-optional.json).

The comparison's complete object, including all 162 numeric deltas and archive
selection changes, equals the earlier `efb4a05a..6d5e3714` comparison. Refreshing
main incorporates the sparse flash-image mechanism without changing this
sorting vector. The generic nonpositive-growth comparator rejects the positive
entries as designed; that rejection supplies no passing resource verdict.

## Exact requested limits

| Shipped surface | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 49,286 | 49,942 | +656 |
| `/bin/box`, loaded/raw bytes | 37,012 / 31,296 | 37,332 / 31,616 | +320 / +320 |
| `/bin/box`, packed bytes / blocks | 26,404 / 27 | 26,662 / 28 | +258 / +1 |
| `/bin/ps`, loaded/raw bytes | 18,240 / 16,284 | 18,560 / 16,604 | +320 / +320 |
| `/bin/ps`, packed bytes / blocks | 13,956 / 15 | 14,214 / 15 | +258 / 0 |
| `/usr/bin/menu`, loaded/raw bytes | 32,520 / 10,200 | 32,840 / 10,520 | +320 / +320 |
| `/usr/bin/menu`, packed bytes / blocks | 8,886 / 10 | 9,151 / 10 | +265 / 0 |
| Root allocated data blocks | 749 | 750 | +1 |
| Root free blocks | 230 | 229 | -1 |

The box block and root block describe the same 1 KiB allocation and count
once. The three positive packed-byte changes total 781; loaded/raw growth
totals 960. The other 150 of 162 measured numeric surfaces remain unchanged,
including both kernels, native startup, every data/BSS extent and the other
30 shipped program records. Archive membership and selection sets remain
unchanged. Selected `qsort.o` bytes change in the three shipped consumers;
native libc changes both `qsort.o` and `qsort_r.o` without adding dependencies.

The maintained unshipped callers have additional, separately enumerated costs:

| Program | Loaded bytes, baseline to candidate | Raw-byte delta | Packed bytes, baseline to candidate | Raw/packed block delta if installed |
| --- | ---: | ---: | ---: | ---: |
| `nm` | 11,660 to 11,980 | +320 | 9,863 to 10,122 | 0 / 0 |
| Directory `diff` | 23,764 to 24,084 | +320 | 18,208 to 18,480 | 0 / +1 |
| `uuq` | 20,284 to 20,604 | +320 | 14,829 to 15,083 | 0 / 0 |

Each adds 320 text bytes and preserves data/BSS. These executables remain
outside the shipped manifest; the optional diff block supplies no additional
root-image allocation. Raw and packed forms are alternative installations.
The full cross-development archive grows from 515,610 to 517,230 bytes
(+1,620); it remains an unshipped host build artifact. The reduced native
archive above is the installed target cost.

The largest affected shipped static occupancy is box's 37,332 bytes, leaving
159,276 bytes in the fixed 196,608-byte window before stack and dynamic
allocation. Loaded occupancy includes text, data and BSS; loaded/raw/packed
figures describe overlapping representations rather than additive RAM costs.
Runtime process peaks and storage traffic remain unmeasured.

## Stack lifetime and benefit

The [compiler frame receipt](libc-bounded-sort-stack.json) binds local frame
sizes to source hashes and actual target flags. Recursive frames remain 48
bytes. Entry frames decrease from 56/64 to 24/32 bytes for ordinary/context
sort. A separate 56-byte partition frame returns before recursive children
enter. Sift and swap frames are 40 and 12 bytes. The frame gate admits these
exact maxima and rejects compiled enlarged recursive scratch. Comparator
frames, nested sorts and caller chains contribute separately. These are
compiler frame limits, not measured whole-process stack peaks.

Ordered samples, equal bands and a per-partition budget bound worst-case
comparator work through allocation-free heap fallback. The old production
sort uses 265,582 comparisons on 1,024 alternating keys and rejects the
finite work budget. A disabled-budget candidate uses 205,644 calls on the
separate alternating-half permutation and also rejects. The selected candidate
passes all 64 pattern cases and preserves the 708,596-case ordering,
permutation, byte-integrity and context matrix on each qualified execution
configuration. The [contract record](libc-bounded-sort-contracts.md) owns the
commands, controls and remaining qualification. These observations establish
comparison work for the exercised inputs; they supply no board timing claim.

Exact callback repairs preserve typed calls in ls, directory diff and uuq.
Signed reverse state restores ls reverse ordering on unsigned-char ARM.
The real-module QEMU controls distinguish that repair from library behavior.

## Alternatives and admission

Keeping the baseline retains quadratic comparison behavior and the ls reverse
failure. Equal-band partitioning without a budget leaves the adversarial
permutation quadratic. The existing public heapsort allocates a record-sized
temporary and cannot supply a non-failing qsort fallback without additional
ownership design. The selected byte-swap heap path adds no allocator or
divider import and preserves arbitrary record representation.

Keeping partition scratch in the recursive function produces a 64-byte frame;
state-only factoring also retains that size while increasing text. Separating
the scratch lifetime preserves the 48-byte frame at the measured text cost.
Shared XIP libc supplies no present size credit. General workload timing,
whole-process runtime peaks and physical-board qualification remain separate
open evidence leaves. Physical-board execution remains unrun and unauthorized.

The requested exception covers exactly the positive shipped and optional
program deltas above, unchanged archive selection sets, and the documented
compiler frame limits. The approval binds the request recorded at `caee317e`
and measured candidate `0aa86d7d`; further growth needs a new measured
decision. Aggregate/image qualification, review and integration remain open.
Main `a849d47a` is the rollback source.


## Complete maintained optional consumer amendment

The complete source/disassembly inventory contains twelve target executables.
The initial optional-program table omits standalone ls, tclsh and four games;
its approval retains the exact original three-program scope. Candidate
`5b823bb3e304e5db151ed40373d078beafd9deee` repairs the additional incompatible
atc, boggle and wump callback definitions. Tcl and robots already have exact
callback parameter types. The expanded real-module cross and ARM witnesses
pass; the follow-up source review reports no actionable findings in the repair.

The [complete clean candidate](libc-bounded-sort-resource-complete.json) and
[paired comparison](libc-bounded-sort-resource-complete-comparison.json) retain
the new source identity against the same main `a849d47a`. The complete
comparison object equals the approved comparison, including every one of 162
numeric deltas and all archive selections. Root free space stays 229 blocks.
The generic comparator continues to reject the same approved positive entries.
The [complete optional receipt](libc-bounded-sort-resource-optional-complete.json)
adds exact matched measurements for all nine unshipped target consumers.
The nm, directory diff and uuq deltas equal their approved limits.

The additional request covers only these six previously omitted programs:

| Maintained optional program | Loaded bytes, baseline to candidate | Loaded/raw delta | Packed bytes, baseline to candidate | Raw/packed block delta if installed |
| --- | ---: | ---: | ---: | ---: |
| Standalone ls | 20,744 to 21,064 | +320 / +320 | 13,197 to 13,467 | +1 / +1 |
| tclsh | 65,536 to 65,856 | +320 / +320 | 54,069 to 54,375 | 0 / +1 |
| atc | 48,092 to 48,412 | +320 / +320 | 33,421 to 33,669 | 0 / 0 |
| boggle | 32,188 to 32,508 | +320 / +320 | 13,242 to 13,502 | 0 / +1 |
| robots | 28,544 to 28,864 | +320 / +320 | 19,273 to 19,534 | 0 / +1 |
| wump | 10,056 to 10,376 | +320 / +320 | 7,614 to 7,884 | 0 / 0 |

Each program adds 320 text bytes and preserves data/BSS. Their six packed
increments are 270, 306, 248, 260, 261 and 270 bytes (1,615 total).
Loaded/raw increments total 1,920 bytes. Raw and packed installations are
alternatives: the raw form adds one optional ls block, while the packed form
adds four optional blocks across ls, tclsh, boggle and robots. These programs
remain outside the shipped manifest and add no root-image block. The shipped
libc/archive vector and existing nm/diff/uuq limits require no new allowance.

The expanded [frame receipt](libc-bounded-sort-stack.json) includes all added
caller modules. Tcl's comparator and Tcl_LsortCmd have 8/32-byte local frames;
tclsh main has 8. Atc's comparator has 8 bytes and its inherited log_score
frame has 56,496. Boggle main/comparator/wordcomp frames have 48/8/16 bytes;
robots score/comparator have 40/0; wump main/comparator have 40/0. These local
measurements exclude transitive calls and runtime process peaks. The added
frame inventory preserves the approved library frame limits.

The owner approves these exact additional limits on 2026-11-15 UTC for
measured candidate `5b823bb3` and the amendment recorded at `f4ec52c6`.
The existing shipped and nm/diff/uuq approval retains its exact scope.
Retaining the old sort in these programs
leaves the same comparison-work defect and introduces a second library
selection policy. Excluding them from the shipped manifest already contains
their root cost, but their maintained target output still needs truthful
resource admission. Main `a849d47a` remains the rollback source. Qualification,
workload timing and physical-board boundaries remain as recorded above.
