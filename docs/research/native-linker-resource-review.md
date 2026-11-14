# Native linker resource review

## Byte-reuse candidate admission

Candidate `a435ac8deba2c9e71a0adb1ad0591ea4ba3ebc57` includes main at
`d4db62ccd196efda906c6e1da24fde9ac5ec21bc`, the matched baseline. Both
clean distributions pass with complete shipped dependency captures. The
[baseline receipt](native-linker-byte-reuse-resource-baseline.json),
[candidate receipt](native-linker-byte-reuse-resource-candidate.json), and
[comparison](native-linker-byte-reuse-resource-comparison.json) retain their
source identities and common measurement tools. These receipts govern this
candidate; the earlier receipts below remain historical evidence.

| Surface | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Linker text bytes | 18,384 | 19,708 | +1,324 |
| Linker raw file bytes | 18,688 | 20,012 | +1,324 |
| Linker packed bytes | 15,231 | 16,149 | +918 |
| Linker allocated blocks | 16 | 17 | +1 |
| Root data blocks | 739 | 740 | +1 |
| Root free blocks | 240 | 239 | -1 |

Loaded linker occupancy rises from 54,040 to 55,364 bytes, leaving 141,244
bytes of the 196,608-byte process window before stack and dynamic allocation.
Data remains 272 bytes and BSS remains 35,384 bytes. Root free inodes remain
29. The other 157 of 162 measured numeric surfaces remain unchanged, including
both kernels and native archives. All selected archive members and their
hashes match the baseline. Linker storage is included in root storage and
counts once. Runtime stack, heap peaks, and storage traffic remain unmeasured.

The candidate adds correct absolute branch imports and partial-link lifetime
while enforcing input section and symbol extents. Reusing decoder bytes removes
the additional `ungetc.o` dependency and saves 312 loaded bytes, 261 packed bytes,
and one root block relative to the earlier candidate. Keeping the baseline
avoids growth but retains the failing absolute-import and truncation contracts.
A larger direct symbol-index table trades the bounded lookup's repeated scans
for additional process storage and remains outside this candidate.

The default nonpositive-growth comparator rejects the five positive surfaces
above as expected. The requested exception covers exactly those deltas;
reviewer approval remains pending. Baseline `d4db62cc` supplies the rollback
point. The current-head reader probe passes 456 cases and rejects 12 mutations;
the branch probe passes 126 commands and rejects six compiled mutations. Ruff
and the logged build pass. The registered native-linker and cross-assembler
gates pass. All 403 assembled libc units agree with GNU assembler; Unicorn
executes the linked program and observes its required output. The
cross-assembler fixture reports an out-of-date archive index warning. The
baseline linker reproduces that warning on the same input archive; the native
linker changes leave the archive timestamp check unchanged. C17 source
accounting passes and its calibration rejects 21 faults. Review, resource
approval, Renode, and integration remain open. Shared-call veneers and the complete
shared-library ABI retain separate requirements.

## Historical candidate

Candidate `c4ecca8c9e2025418d03becc4e64d5254c89c7ed` adds native absolute
branch imports, preserves them across partial links, and bounds input decoding.
The matched baseline is `f8f536b9f001861e6665431eaa7c9e33ce01f536`.
The resource exception remains unapproved. The compatibility profile's measured
exception process governs admission.

## Measured distribution costs

The [baseline receipt](native-linker-resource-baseline.json),
[candidate receipt](native-linker-resource-candidate.json), and
[comparison](native-linker-resource-comparison.json) bind successful clean
distributions and their complete shipped dependency closures. The collector's
36 calibration tests pass. The comparator rejects the added archive dependency
and positive costs.

| Surface | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Linker text bytes | 18,384 | 20,020 | +1,636 |
| Linker raw file bytes | 18,688 | 20,324 | +1,636 |
| Linker packed bytes | 15,231 | 16,410 | +1,179 |
| Linker allocated blocks | 16 | 18 | +2 |
| Root data blocks | 739 | 741 | +2 |
| Root free blocks | 240 | 238 | -2 |

The linker loaded occupancy grows by 1,636 bytes. Its 272 data bytes and 35,384
BSS bytes stay unchanged. The linker gains `ungetc.o` from libc; existing
selected member hashes remain identical. Of 162 measured numeric surfaces,
157 remain unchanged, including kernel costs and native archive sizes. Root
free inodes remain 29. Linker blocks form part of root blocks and count once.
Runtime stack, heap peaks, and storage traffic remain unmeasured.

## Measurement tool identity

The first comparison rejects different `hsaout` measurement-tool hashes.
The candidate checkout retains a host `hsaout.o` built before the common host
flags enable `_FORTIFY_SOURCE=3`; the new baseline object imports fortified
printf functions. All five packing-library objects match byte-for-byte.
A clean rebuild of the candidate's standalone measurement tool produces the
baseline SHA-256 `e25557bf3f3c2f389c8d6b3957236b0f250c7f68b931fcd75be6e1a36cf28c35`.
Resource recapture retains the original distribution image and dependency
closure. Both resource receipts then identify identical measurement tools.
The original captures remain retained as diagnostic evidence.

## Admission and deciding experiments

The [mechanism investigation](native-absolute-imports.md) owns correctness and
qualification. The fallback absolute-symbol lookup preserves the bounded
undefined-symbol table, trading repeated scans for fixed temporary storage.
The added `ungetc.o` supports decoder lookahead. A deciding optimization
experiment passes an already-read length or flags byte into the decoder and
removes redundant reads, retaining every truncation and extent witness. Its
acceptance requires the full native-linker gate and a new matched resource
capture. This receipt grants no allowance to that future candidate.

The byte-reuse implementation removes both pushback calls and the redundant
absolute-symbol seek. Its target object text decreases from 11,725 to 11,598
bytes. A target relink produces 20,012 raw bytes and 19,708 text bytes, saving
312 raw bytes relative to the recorded candidate while preserving data and BSS.
Target undefined-symbol inspection confirms that `ungetc` disappears. These
object and relink results establish no packed-block or whole-distribution
claim; a clean captured distribution remains the deciding measurement.

Native execution regression, resource approval, review, and integration remain
open. The baseline commit supplies the rollback point. Shared-library veneers
and the wider XIP ABI retain their own implementation and qualification work.
