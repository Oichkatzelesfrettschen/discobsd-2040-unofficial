# Byte search resource exception

The current request covers guarded-prefix candidate `d1b55dd3` against main
`e64e2150`; its exact vector appears under Guarded-prefix revision below.
The original request and its immutable receipts retain their measured scope.

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

## Guarded-prefix revision

The revised candidate is `d1b55dd38634469ae281c1ff68ce340821e43f4c`.
The refreshed baseline is main `e64e2150f3d4e4c570efc9cfbabb950fe3739ce2`.
Matched clean distributions pass with identical collector, compiler, linker,
measurement-tool and manifest identities. The
[parent](libc-byte-search-prefix-resource-parent.json),
[candidate](libc-byte-search-prefix-resource-candidate.json),
[comparison](libc-byte-search-prefix-resource-comparison.json) and
[optional cut](libc-byte-search-prefix-resource-optional.json) receipts bind
the complete 162-surface denominator and archive member identities.

| Shipped surface | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native libc archive, bytes | 49,942 | 50,690 | +748 |
| Textbox loaded bytes | 39,704 | 40,080 | +376 |
| Textbox raw bytes | 38,568 | 38,944 | +376 |
| Textbox packed bytes | 31,397 | 31,759 | +362 |
| Textbox packed allocation blocks | 32 | 33 | +1 |
| Cpio loaded/raw bytes | 11,196 / 10,344 | 11,192 / 10,340 | -4 / -4 |
| Cpio packed bytes / blocks | 8,555 / 10 | 8,554 / 10 | -1 / 0 |
| Root allocated data blocks | 750 | 752 | +2 |
| Root free blocks | 229 | 227 | -2 |

The other 153 measured surfaces and 31 shipped program size records remain
unchanged, including both kernels and native startup. The native archive
adds `memchr.o` and `memmem.o`. Textbox retains both members because its
single-byte and optional first-byte filter use the independent byte provider.
The archive and textbox each consume one additional root block; the two
root blocks count those allocations once. Cpio selects the repaired memchr.
Textbox data and BSS remain unchanged. Its static occupancy leaves 156,528
bytes in the 196,608-byte process window before stack and allocations.

Standalone cut remains unshipped. Its loaded/raw sizes rise by 376 bytes,
from 10,348/10,244 to 10,724/10,620. Its packed size rises by 375 bytes,
from 8,806 to 9,181. Raw/packed block counts stay 12/10. The refreshed
baseline and candidate optional builds bind the exact source revisions.

The [frame record](libc-byte-search-stack.json) retains the original candidate
and adds the revised 48-byte memmem and zero-byte memchr local frames.
The conservative direct provider chain grows from 36 to 48 bytes under the
recorded compiler and flags. This bound excludes caller frames, interrupts
and transitive application peaks. Heap occupancy and storage traffic remain
unmeasured. The implementation owns no allocation or persistent state.

The [instruction receipt](libc-byte-search-instructions.json) binds a
13-case Cortex-M0+ instruction model, its source/image hashes, tool versions
and four rejecting controls. Common cases satisfy baseline plus 96
instructions; adversarial cases satisfy one quarter of baseline. These
finite observations establish neither a universal timing bound nor board
performance. The comparison bound remains independently calibrated at
`2n+7m` including wrapper work.

The current source passes a 19.012-second logged build and a 70.881-second
focused run covering host, ILP32, sanitizer, ARM QEMU, native archives,
visibility, Smaller C, source accounting, instruction controls and static
analysis. The earlier aggregate remains bound to the original implementation.
Complete revised qualification and independent source review remain open.

The resource decision remains pending. Approval covers exactly this vector,
the two added native members, the optional cut limits and the 48-byte local
provider-chain limit. Further growth requires a new measured decision.
Static libc and existing multicall composition remain selected; shared XIP
supplies no capacity credit. Main e64e2150 is the rollback source.
Physical-board execution remains unauthorized.
