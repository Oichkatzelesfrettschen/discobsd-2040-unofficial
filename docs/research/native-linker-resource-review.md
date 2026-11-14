# Native linker resource review

## Calendar baseline qualification

Candidate `66ce7c5b57a691fed604f26d14449996f0887743` includes main
`9561fd1cf05d823d1b93b9236448a65ff1c99abc`. The matched
[baseline](native-linker-calendar-resource-baseline.json),
[candidate](native-linker-calendar-resource-candidate.json), and
[comparison](native-linker-calendar-resource-comparison.json) bind separate
clean distributions and complete shipped dependency closures to identical
measurement tools. Historical receipts remain unchanged.

The five positive surfaces reproduce the image-clock comparison exactly:
1,324 additional linker loaded/raw bytes, 918 additional packed bytes, one
additional linker allocation block and one additional root data block. The
last two counts describe the same stored growth at different levels. The
other 157 of 162 measured surfaces remain unchanged. Every selected archive
member and hash matches the paired baseline. The candidate has 239 free root
blocks. Resource approval remains pending; the default comparator rejects the
five positive surfaces as designed.

The shared syscall and ROM selectors remain disabled in shipped static builds.
Their implementation adds no measured static-image cost beyond the linker
candidate. Eleven program hashes and both kernel hashes differ from the older
image-clock candidate. The older runtime qualification remains evidence for
its recorded artifacts. The
[shared-ROM integration receipt](xip-shared-libc/object-abi/rom-main-qualification.json)
records focused contract and calendar checks after the main merge.


The refreshed flash image build and PICO_UART Renode gate pass. The
[runtime receipt](native-linker-calendar-runtime.json) binds the loaded kernel,
root image and peripheral-model assembly to SHA-256 identities. The gate checks
MPU faults, exec residue, core-1 pings and injected stall reporting. Its warning
classifier accepts 82,887 model warnings across 19 bounded classes with zero
errors. This execution covers the static emulator image; shared executables and
physical-board behavior remain separate qualifications.

## Image-clock baseline qualification

Candidate `6c08eaab4f14eb02ccdddc2cdc4c13ad50aec2bf` includes main
`641d40b297eaeb3d7468a08d6a2be3c3ce9014e1`, whose filesystem image tools support
the extended unsigned clock range. The matched
[baseline](native-linker-image-clock-resource-baseline.json),
[candidate](native-linker-image-clock-resource-candidate.json), and
[comparison](native-linker-image-clock-resource-comparison.json) bind fresh
clean distributions, complete shipped closures, and identical measurement
tools within the pair.

All five positive deltas exactly match the byte-reuse table below; the other
157 measured surfaces remain unchanged. Selected archive members and hashes
match the paired baseline. Every measured program hash also matches candidate
`a435ac8d`; both kernel hashes differ, so the earlier emulator run remains
historical evidence. The requested limits remain unchanged and approval
remains pending.

The logged build, native-linker gate, native-call veneer gate, filesystem-clock
gate, C17 source accounting, profile calibration, and flash image build pass on
this baseline. The PICO_UART Renode boot gate passes on the refreshed images,
including signal/u-area behavior and injected core-1 stall detection. Its
warning classifier accepts the run with zero errors. The veneer
gate establishes the instruction mechanism described in
[native-call-veneers.md](native-call-veneers.md), with automatic shared-link
insertion still open. Main `641d40b2` supplies this pair's rollback point.

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
accounting passes and its calibration rejects 21 faults. The flash image build
and PICO_UART Renode boot gate pass, including image identity, MPU faults,
exec residue, signal/u-area behavior, and core-1 supervision. The emulator
uses Renode `1.17.0+20260907gitf1dd1b4af` and RP2040 models pinned at
`205a5e4b25440582008a4292074bb07f80a72328` with the repository's patches.
The model build reports 27 third-party warnings and zero errors. The runtime
warning classifier accepts 82,775 warnings under its 19 bounded classes and
reports zero errors. A sandboxed
attempt aborts before kernel loading because Renode cannot lock its user
configuration; the host-authorized rerun passes. Changed-comment and
changed-text gates pass through documentation commit `a8bdb9c1`. Physical-board
execution remains unrun. Review, resource approval, and integration remain
open. Shared-call veneers and the complete
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

The [authorized read-only review](native-linker-approved-review.json) covers
main `9561fd1c` through candidate `66ce7c5b` and reports no actionable
correctness defects. The reviewer runs no tests; the qualification records
above own executed evidence. Resource approval and integration remain open.
Shared-library veneers and the wider XIP ABI retain their own implementation
and qualification work.
