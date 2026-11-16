# Native nm resource exception

The compatibility profile in `dpucp-2024.md` requires reviewer approval for
positive growth on a constrained surface. This request covers the separately
built native `nm`; the shipped manifest excludes that executable.

## Source and measurement identity

The baseline is main `f602046341801b77feff61a2c3549c119ffb8d37`.
The production candidate is `785395bce94ad5a61f8832cf5a079e9d96ce1031`.
PR head `df938bda2ef534d3035371ba99e1b04603fb573c` adds tests and records;
its production `usr.bin/nm/nm.c` matches the measured candidate.
Both resource runs use clean instrumented distributions. The corresponding
`native-nm-resources-f6020463.json` and `native-nm-resources-785395bc.json`
retain toolchain and distribution identities. Their paired comparison records
zero change across all 162 shipped numeric surfaces.

`tools/bin/rp2040/hsaout -s usr.bin/nm/nm` measures the standalone executable
in each build. Decoding the eight little-endian header words supplies text,
data, and BSS. File SHA-256 identities are:

- Baseline: `0fe78625340fe6429a4b18467ee7a52810558460eec337b3439fa893afe39a77`.
- Candidate: `e4e3ab415ee7ca450a0a4d276fc63d395119b95c7cf06acbd4fa2bc8fe554b69`.

| Surface | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Text bytes | 10,064 | 11,196 | +1,132 |
| Data bytes | 204 | 204 | 0 |
| BSS bytes | 252 | 260 | +8 |
| Loaded text/data/BSS bytes | 10,520 | 11,660 | +1,140 |
| Raw file bytes | 10,300 | 11,432 | +1,132 |
| Packed file bytes | 9,048 | 9,863 | +815 |
| Raw allocated 1 KiB blocks, if installed | 12 | 13 | +1 |
| Packed allocated 1 KiB blocks, if installed | 10 | 11 | +1 |

Raw and packed installation are alternative representations. Their block costs
are not additive. Text is part of loaded occupancy and raw file size; those
figures also describe overlapping storage. The 196,608-byte process window
leaves 184,948 bytes before stack and dynamic allocation for this candidate.
That subtraction establishes static occupancy only. Runtime stack, heap peaks,
and storage traffic remain unmeasured. The shipped root retains 233 free
blocks and receives no additional allocation from this patch.

## Benefit, alternatives, and admission

The reader admits archive members and object sections only within checked
file extents, detects incomplete symbol records and stream failures, releases
partial allocations, and preserves reverse sorting on unsigned-char ARM.
The test suite passes 219 host/ILP32 cases, seven mutation controls, and 183
Cortex-M0 QEMU cases. Those execution surfaces do not establish DiscoBSD
kernel or physical-board behavior.

Keeping the baseline preserves the smaller executable and retains the failing
reader contracts. Dropping extent or failure checks removes required behavior.
Reusing the native linker's reader directly requires a separate ownership and
interface refactor: its input stream, symbol storage, and error handling belong
to the linker. The current patch retains nm's local ownership and bounded
cleanup. Further size optimization needs its own measured candidate.

The requested exception covers exactly the positive deltas above for the
optional native executable, with no growth on the 162 shipped measures.
The owner approves these exact limits on 2026-11-14 and authorizes continued
review and qualification. The approval covers this measured candidate and
these limits; additional growth requires a new decision. Main `f6020463`
supplies the rollback point.
