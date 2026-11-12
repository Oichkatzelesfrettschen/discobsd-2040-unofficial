# Bounded-string native archive resource review

The four string interfaces add 692 bytes to the shipped native libc archive.
The matched distribution comparison preserves all other 157 measured surfaces,
including executable packed bytes, loaded sizes, kernel resources and root
block counts. The default resource gate rejects the added archive members;
resource acceptance remains pending.

The governing [profile rule](dpucp-2024.md) states: "An exception records exact
deltas, examined alternatives, retained margins, user benefit, rollback point
and reviewer approval." The [contract analysis](libc-bounded-string-contracts.md)
owns behavior, donor provenance and calibration. This record owns the resource
decision for the exact pair below.

## Source and receipt identity

| Artifact | Source revision | Retained evidence |
| --- | --- | --- |
| Parent | `3b6a5585ced167a0913b1c691ab152c609d2b0d2` | [Parent resources](libc-bounded-string-resource-parent.json) |
| Complete candidate | `b57da5bac273bd4ba382c17ccdb3327eb0061f6d` | [Candidate resources](libc-bounded-string-resource-candidate.json) |
| Default comparison | Both revisions above | [Rejected comparison](libc-bounded-string-resource-comparison.json) |

Both captures use clean distributions and complete dependency attribution.
Their collector, measurement tools and toolchain identities match: ARM GCC
16.2.0, GNU ld 2.47.20260726 and bmake 20260912. The receipts bind source,
distribution completion, log, closure and artifact hashes. The comparison
remains a rejection under the unchanged default policy.

## Exact exception and retained margins

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 43094 | 43786 | +692 |
| Native archive members | 151 | 155 | +4 |
| Root allocated data blocks | 744 | 744 | 0 |
| Root free blocks | 235 | 235 | 0 |
| Root free inodes | 32 | 32 | 0 |

The native archive adds exactly `stpcpy.o`, `stpncpy.o`, `strndup.o` and
`strnlen.o`. Every preexisting selected member retains its content hash.
Every shipped executable retains its archive selection, raw size, packed size,
packed block count and loaded size. Both kernel resource vectors remain
unchanged. The largest static process occupancy remains 75,324 bytes in the
196,608-byte window; the remaining 121,284 bytes precede stack and heap admission.
The root allocation result measures filesystem blocks, separately from the
archive's byte extent. Runtime stack, heap peaks and storage traffic remain
unmeasured.

The preliminary 776-byte estimate uses a 43,010-byte archive from a different
baseline. The matched parent log and receipt both establish 43,094 bytes, so
that estimate supplies no delta for this pair. The retained incomplete
candidate at `f302b5ef` also contains 43,094 archive bytes and omits the four
members; its zero-growth result serves as a packaging negative control.

## Alternatives, benefit and rollback

Omitting the four native members preserves storage but prevents native
applications from linking the advertised interfaces. The archive-link fixture
rejects each individually missing member in both archive forms. Separate
members preserve ordinary static selection: programs that do not call the
interfaces incur no additional executable occupancy in this pair.

Combining the four functions in one member changes selection granularity and
can pull unused code into consumers. Wrapping `strncpy` for `stpncpy` requires
another scan to recover the end pointer. Neither alternative supplies measured
resource superiority. Bytewise implementations preserve bounded access without
alignment assumptions or word reads beyond the caller's object.

The user benefit is native and cross-linked access to the four tested POSIX
interfaces. The rollback point is the parent revision, which lacks them.
The requested exception covers only the 692-byte archive increase and four
named members. Future growth and other resource surfaces require their own
comparison. Review approval remains pending; aggregate qualification and
reviewed integration continue independently. Physical-board execution remains
outside this evidence.
