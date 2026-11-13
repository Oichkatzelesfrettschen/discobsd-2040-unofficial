# Callable integer magnitude resource review

The callable `abs` and `labs` providers add exactly 240 bytes to the shipped
native libc archive, from 45,078 to 45,318 bytes, and add the two members
`abs.o` and `labs.o`. The other 157 of 158 numeric resource surfaces remain
unchanged. The root image retains 746 allocated data blocks, 233 free blocks
and 32 free inodes. The owner approves these exact limits and directs continued
qualification and review.

The [profile rule](dpucp-2024.md) requires an exception with exact deltas,
examined alternatives, retained margins, user benefit, rollback point and
reviewer approval. Existing lifecycle and sorting approvals retain their
original limits and supply no allowance for these additional bytes.

## Measurements and source identities

| Artifact | Source revision | Receipt |
| --- | --- | --- |
| Parent with sorting and shared-libc closure checks | `a8e103eacb770a4c5da0bf68e3c2da7f9b4f0522` | [Parent resources](libc-absolute-value-resource-parent.json) |
| Callable providers | `bdbe4fb0bea49ead51f442b3b918140cb44e0df8` | [Candidate resources](libc-absolute-value-resource-candidate.json) |
| Default admission | Both revisions above | [Resource comparison](libc-absolute-value-resource-comparison.json) |

Both instrumented distributions begin after a complete clean and supply dependency
accounting for all 37 shipped artifact groups. Collector and measurement-tool
hashes match. Both captures use ARM GCC 16.2.0, GNU ld 2.47.20260726 and
bmake 20260912. The candidate distribution completes with exit zero at
`2026-11-13T02:37:55Z`. The comparator rejects the added archive members and
archive-byte growth under default admission.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 45,078 | 45,318 | +240 |
| Native archive members | 159 | 161 | +2 |
| Root allocated data blocks | 746 | 746 | 0 |
| Root free blocks | 233 | 233 | 0 |
| Root free inodes | 32 | 32 | 0 |

Every existing native archive member retains its hash. All 33 program records,
including hashes, remain identical. Both kernel resource vectors and native
startup size remain equal. The archive occupies 45 data blocks in both cases;
the parent has 1,002 bytes of unused capacity in its final data block and the
candidate has 762 bytes. The root image confirms unchanged block allocation.
These measurements concern static storage and loaded extents; runtime stack,
heap, timing and swap traffic retain their separate evidence requirements.

## Benefit, alternatives and admission limit

The [callable-provider contract](libc-absolute-value-contracts.md) records the
missing public symbols, typed representability domain, donor comparisons,
archive source selection and calibrated tests. Programs that take `abs` or
`labs` addresses gain callable definitions in both cross and native toolchains.
Omitting the new native members leaves that advertised interface unavailable
for board-built programs. Compiler built-ins alone supply no addressable symbol.

Separate archive members let an application select either eight-byte Thumb
provider independently. Combining them can reduce archive metadata while
forcing a caller of one function to select the other body. The separate members
preserve that caller isolation and have no data, BSS or runtime imports.
No alternative has qualified lower cost across all constrained surfaces.

The requested exception admits exactly 240 additional native archive bytes and
the two named members. It admits no growth in root blocks, executable packed or
loaded sizes, kernel sizes, startup size or other measured surfaces. The parent
source supplies the rollback point. The owner approves these exact limits;
additional growth requires its own measurement and decision.

Focused native, ILP32, sanitizer and ARMv6-M QEMU behavior passes 131,088 cases
per run and rejects both named sign-preservation mutations. Full/reduced native
archive linkage passes all 24 member-omission controls. Smaller C passes the
21-fixture suite, including public address-taking. Strict C17 header/import
checks, Clang static analysis and repository lint pass. The complete ILP32
inventory passes 199 of 199 variants at `73460affa4cb18c07d613423e82a2297f01b0ad1`.
Aggregate, hosted CI and reviewed integration remain open. Physical-board
execution remains unrun and requires separate authorization.
