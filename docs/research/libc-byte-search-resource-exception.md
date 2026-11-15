# Byte search resource exception

The byte-search candidate adds 576 installed native archive bytes, 212 loaded
bytes to textbox, and one root block against main
`cfe41205235bfc677e6fdf1a53a395aae2f6cb71`. The compatibility profile
[resource policy](dpucp-2024.md) requires reviewer approval for these positive
constrained deltas. This request remains pending; qualification continues.

The measured source is `64b42d7816798725c4653470a5f2ab781c25a411`.
Matched clean distributions use identical toolchain, collector and measurement
identities. The [baseline](libc-byte-search-resource-parent.json),
[candidate](libc-byte-search-resource-candidate.json),
[comparison](libc-byte-search-resource-comparison.json) and
[optional cut receipt](libc-byte-search-resource-optional.json) retain the
complete inputs and all 162 measured surfaces. The generic comparator rejects
added native members and positive deltas as designed; that rejection grants
no resource admission.

| Shipped surface | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native libc archive, bytes | 49,942 | 50,518 | +576 |
| Textbox loaded bytes | 39,704 | 39,916 | +212 |
| Textbox raw bytes | 38,568 | 38,780 | +212 |
| Textbox packed bytes | 31,397 | 31,607 | +210 |
| Textbox packed allocation blocks | 32 | 32 | 0 |
| Cpio loaded/raw bytes | 11,196 / 10,344 | 11,192 / 10,340 | -4 / -4 |
| Cpio packed bytes / blocks | 8,555 / 10 | 8,554 / 10 | -1 / 0 |
| Root allocated data blocks | 750 | 751 | +1 |
| Root free blocks | 229 | 228 | -1 |

The native archive adds memchr.o and memmem.o. Textbox drops memchr.o because
the tableless provider owns every needle length and imports no helper.
Cpio retains memchr.o with the corrected unsigned conversion. The other 154
numeric surfaces and 31 shipped program size records remain unchanged,
including both kernels and native startup. Textbox's data/BSS stay unchanged.
The native allocation and root block are one 1 KiB cost counted once. Loaded,
raw and packed bytes describe overlapping representations. Cpio's saving
supplies no RAM credit for textbox.

Standalone cut remains outside the shipped manifest. Its loaded/raw sizes
increase by 212 bytes, from 10,348/10,244 to 10,560/10,456. Packed size grows
211 bytes, from 8,806 to 9,017. Raw/packed block counts remain 12/10. These
are alternative installation forms and add no optional root allocation.
Source search finds no other maintained optional consumer; picoc remains
outside the RP2040 selection. The compiled closure and existing calibrated
cut/textbox map gate supply separate checks of actual archive selection.

The [frame receipt](libc-byte-search-stack.json) records 40 local bytes for
memmem and zero for memchr. Baseline memmem uses 24 local bytes and calls
memchr or memcmp; baseline memchr uses eight and memcmp uses twelve. The conservative provider
chain sum grows from at most 36 to 40 bytes under these flags. Caller frames, transitive
application peaks, heap occupancy and storage traffic remain separate
unmeasured surfaces. The implementation allocates no memory and introduces
no persistent writable state. Textbox's static occupancy leaves 156,692
bytes in the fixed 196,608-byte process window before stack and allocations.

The [contract](libc-byte-search-contracts.md) preserves first-match and byte
extent semantics, records the established Two-Way algorithm premise, and
calibrates independent result and work checks. Baseline search rejects the
work budget and baseline memchr rejects int conversion. The production
candidate passes the finite host/ILP32/sanitizer/ARM matrices; clean-head
focused and aggregate qualification remain open. The first native distribution
rejects a missing source-registry entry; the measured candidate includes the
registry repair and its clean distribution completes.

Keeping the baseline retains quadratic repeated-prefix work and incomplete
memchr conversion. Retaining optional skip tables adds a large automatic
allocation absent from the baseline. The no-memory first-match variant has
an independent proof obligation and a worse measured frame. The selected
remembered-prefix provider retains constant auxiliary storage at the exact
text/frame cost above. Shared XIP supplies no present size credit.

Approval covers exactly these positive shipped and optional costs, the two
native members and measured local-frame limits. Further growth requires a
new measured decision. Main cfe41205 is the rollback source. Physical-board
execution and timing remain unrun and unauthorized.
