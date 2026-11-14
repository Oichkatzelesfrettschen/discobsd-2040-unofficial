# Native linker resource review

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
