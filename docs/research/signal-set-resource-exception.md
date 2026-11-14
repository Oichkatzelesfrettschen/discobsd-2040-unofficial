# Signal-set resource exception

The compatibility profile requires reviewer approval for positive constrained
resource deltas and newly selected archive dependencies. The baseline is main
`d01ef4a02eeaf3030df6d6f0586f5bb21c18c270`; the measured candidate is `0abfb33d`.
`signal-set-resources-d01ef4a0.json`, `signal-set-resources-0abfb33d.json`, and
`signal-set-resources-d01ef4a0-0abfb33d.json` retain the exact source identities,
clean instrumented distributions, dependency closures, and comparison.

## Exact requested limits

The native libc archive grows from 48,294 to 48,326 bytes (+32).
Eighteen executables select the existing `sigsetops.o` member. Their positive
loaded and raw byte deltas total 1,140; packed byte deltas total 754.
Each row below fixes the per-executable limit. Loaded and raw deltas are equal
in every affected program; data and BSS sizes remain unchanged.

| Executable | Loaded/raw byte delta | Packed byte delta | Packed block delta |
| --- | ---: | ---: | ---: |
| `/bin/ed` | +40 | +31 | +0 |
| `/bin/sh` | +40 | +32 | +0 |
| `/bin/sysbox` | +40 | +18 | +0 |
| `/bin/tar` | +40 | +29 | +0 |
| `/sbin/adminbox` | +168 | +66 | +1 |
| `/sbin/fsck` | +40 | +32 | +0 |
| `/sbin/init` | +40 | +30 | +0 |
| `/usr/bin/awk` | +60 | +50 | +0 |
| `/usr/bin/compress` | +96 | +65 | +0 |
| `/usr/bin/ld` | +40 | +35 | +0 |
| `/usr/bin/login` | +40 | +32 | +0 |
| `/usr/bin/passwd` | +40 | +24 | +0 |
| `/usr/bin/tee` | +40 | +31 | +0 |
| `/usr/bin/utilbox` | +40 | +34 | +0 |
| `/usr/games/gamebox` | +172 | +114 | +0 |
| `/usr/games/puzzlebox` | +124 | +79 | +0 |
| `/usr/libexec/getty` | +40 | +29 | +0 |
| `/usr/sbin/update` | +40 | +23 | +0 |

The adminbox allocation gains one 1 KiB block. Root data blocks rise from
746 to 747 and free blocks fall from 233 to 232; these describe the same
additional allocation and count once. Kernel sizes remain unchanged. The
other 105 of 162 measured numeric surfaces remain unchanged. Loaded occupancy
and file bytes overlap and are not additive RAM requirements. Runtime stack,
heap peaks, and storage traffic remain unmeasured. Timeout's separate static
stack check reports 128 bytes for main and 168 for its pre-fork chain; that
check does not measure whole-program runtime peaks.

## Benefit and examined alternatives

The public callable membership operation tests the requested bit, and all five
functions have exact public declarations. Add, delete, and membership reject
invalid numbers before shifting and preserve the set on failure. Host/ILP32
and Clang calibration, real target headers, Smaller C compilation, generated
caller execution, and Cortex-M0 witnesses cover these contracts separately.

The earlier candidate `ec6f9773` incurs 88 additional loaded bytes and 43
additional packed bytes beyond this candidate. Preserving constant mask
construction inside abort and system recovers those bytes while compile-time
assertions admit the fixed signal numbers. Target text and relocations match
main for these two callers; six instrumented scenarios and three mutations
qualify masks and ordering. The predicted decrease in archive-selection count
is refuted: signal() itself calls validated sigismember(), retaining the
sigsetops.o dependency in all 18 programs. This finding narrows the optimization
claim to the measured byte savings.

Keeping the baseline preserves size but retains incorrect callable membership,
missing declarations, and unchecked invalid-number shifts. Removing public
validation defeats the selected error contract. Compiler-specific constant
folding macros introduce another header/compiler mechanism and do not establish
savings without a separate calibrated candidate. Shared XIP libc remains a
separate program and supplies no present resource credit.

The owner approves exactly this vector and the 18 new member selections on
2026-11-14. The approval applies to candidate `0abfb33d` against `d01ef4a0`;
additional growth requires its own measured admission. Main `d01ef4a0` supplies
the rollback point. Physical-board execution remains unrun; this exception
does not authorize it.

## Integrated main comparison

Candidate `907b47fb1b4062aedecf4bf50db23d0a95f44a36` includes main
`0c021420f631cd69522fba7c51c263ca98d6bec6`. Its matched clean receipts are
`signal-set-resources-907b47fb.json` and
`signal-set-resources-0c021420-907b47fb.json`. Every numeric delta and
per-program libc member change equals the approved `d01ef4a0..0abfb33d`
vector. The native archive rises from 48,846 to 48,878 bytes; root data blocks
rise from 746 to 747, leaving 232 free blocks. The same exact approved limits
therefore cover this integration without an additional resource allowance.
The generic nonpositive-growth comparator still rejects the added dependencies;
its rejection remains distinct from the owner's measured exception.
