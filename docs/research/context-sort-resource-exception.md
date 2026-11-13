# Context-aware sorting resource review

The context-last `qsort_r` candidate adds exactly 668 bytes to the shipped native
libc archive and one 1,024-byte root data block relative to the approved combined
lifecycle candidate. The other 156 of 158 numeric resource surfaces remain
unchanged. Root storage retains 233 free blocks and 32 free inodes. The owner
approves this exact additional vector and directs continued qualification
and review.

The [profile rule](dpucp-2024.md) states: "An exception records exact deltas,
examined alternatives, retained margins, user benefit, rollback point and
reviewer approval." The [combined lifecycle approval](libc-lifecycle-integrated-resource-exception.md)
owns its earlier costs. Its approval supplies no allowance for this addition.

## Source identities and measurements

| Artifact | Source revision | Retained receipt |
| --- | --- | --- |
| Combined lifecycle parent | `a3c7e651258bb008bebc5c491a70818674ed656e` | [Parent resources](libc-lifecycle-integrated-resource-candidate.json) |
| Context-sort candidate | `e0db6c50379f6537e9c5af5e03520282a55cbe51` | [Candidate resources](context-sort-resource-candidate.json) |
| Default admission | Both revisions above | [Rejected comparison](context-sort-resource-comparison.json) |

Both receipts bind clean distributions with complete dependency accounting for
37 shipped artifact groups. The collector, measurement tools and toolchain
identities match: ARM GCC 16.2.0, GNU ld 2.47.20260726 and bmake 20260912.
The candidate distribution completes with exit zero at `2026-11-13T00:19:29Z`.
The default comparator rejects the added archive member and positive costs.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 44,410 | 45,078 | +668 |
| Native archive members | 158 | 159 | +1 |
| Root data blocks | 745 | 746 | +1 |
| Root free blocks | 234 | 233 | -1 |
| Root free inodes | 32 | 32 | 0 |

The sole added selected member is `qsort_r.o`. Every previously selected member
retains its hash. All executable raw, packed, loaded and allocated-block sizes
remain unchanged, as do both kernel resource vectors and native startup size.
The parent archive has 646 bytes of unused space in its last data block;
the 668-byte addition exceeds that space by 22 bytes. The measured root image
confirms the additional block. The archive's existing indirect block count
remains unchanged.

## Alternatives and benefit

The [context-sort contract](context-sort-contracts.md) owns typed callback
selection, smaller-side recursion, source provenance and object experiments.
Independent typed objects preserve ordinary `qsort` callers. A typed adapter
over one context-aware core adds 60 text bytes to an ordinary-only consumer
and a call per comparison, while saving 452 bytes when both interfaces link.
That alternative exchanges existing consumers' costs for combined-use savings;
its object estimate establishes no whole-distribution admission.

Passing context separately to the partition helper saves 10 text bytes while
adding eight bytes per recursive partition frame and removing eight entry-frame
bytes. Peak RAM priority retains the candidate's 48-byte partition frame.
Omitting `qsort_r.o` from the native archive prevents native applications from
linking the advertised interface. No examined alternative establishes a smaller
qualified implementation under all caller and resource constraints.

The benefit is the POSIX Issue 8 context-last interface with independent nested
callback state, complete cross/native archive selection and target declarations.
The full converted archive also selects its existing machine-specific integer
divider, completing both sorting functions' native dependency. The reduced
shipped archive already contains that divider; its bytes remain identical.

The rollback point is the parent source. The approved limits are exactly 668
archive bytes, the one named member and one root data block. The approval covers
no additional executable, kernel or runtime growth. Runtime stack peaks,
physical-board timing and swap traffic remain unmeasured.

## Qualification boundaries

Focused native, ILP32, sanitizer and ARMv6-M QEMU execution passes 78,740 cases
and rejects five behavioral mutations. The positive harness compiles the shipped
`qsort_r.c` wrapper. Target header/native-input controls and twenty full/reduced
archive omission controls pass. The ordinary-sort regression also passes.
The complete local ILP32 inventory passes 196 of 196 variants at
`6d47f36208a08489ca628de6793671cc6fd4a5b8`; its terminal receipt records
exit zero at `2026-11-13T00:27:18Z`. Automated review at that head reports
zero findings. Aggregate and hosted qualification expose the header-fixture
search-path defect described in the context-sort contract. Repaired aggregate,
hosted CI and reviewed integration retain their separate completion requirements.
