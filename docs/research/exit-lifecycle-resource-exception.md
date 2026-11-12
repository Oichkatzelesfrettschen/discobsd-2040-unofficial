# Exit lifecycle packed-byte exception review

The candidate repairs callback registration during exit. The matched static
resource gate rejects fifteen packed-byte increases even though every executable
retains its allocated block count and loaded size. The default policy remains
active; the owner approves the exact candidate and limits below.

The governing [profile rule](dpucp-2024.md) states: "An exception records exact
deltas, examined alternatives, retained margins, user benefit, rollback point
and reviewer approval." The machine-readable comparison retains the rejected
default verdict; its positive deltas remain visible.

## Exact candidate and evidence

- Parent source: `fc71a27927812e835dea232d327c287da7f3c094`.
- Candidate source: `fead09b59b5bc4a8607dad9305e7a6a6e2523a90`.
- Behavioral repair: `5ee48c29`; its parent source reproduces `BA!` instead of
  `BDCA!` when a running handler registers more handlers.
- Parent receipt: [exit-lifecycle-resource-parent.json](exit-lifecycle-resource-parent.json),
  copied byte-for-byte from `504deccb82ad4091c782734db02027e0c01b820d` at
  `docs/research/libc-string-header-resource-parent.json`.
- Candidate receipt: [exit-lifecycle-resource-candidate.json](exit-lifecycle-resource-candidate.json).
- Rejected default comparison: [exit-lifecycle-resource-comparison.json](exit-lifecycle-resource-comparison.json).

Both receipts authenticate the same collector, measurement tools and toolchain.
The clean candidate distribution and complete shipped dependency closure pass.
All 37 archive-selection groups preserve their selected member sets; 34 groups
contain changed member bytes. The source change adds no archive dependency.

## Requested exception limits

| Executable | Parent packed bytes | Candidate packed bytes | Increase |
| --- | ---: | ---: | ---: |
| `/bin/box` | 27895 | 27896 | +1 |
| `/bin/ed` | 7237 | 7239 | +2 |
| `/bin/sysbox` | 26129 | 26130 | +1 |
| `/bin/tar` | 23792 | 23793 | +1 |
| `/usr/bin/as` | 23300 | 23302 | +2 |
| `/usr/bin/du` | 6956 | 6959 | +3 |
| `/usr/bin/sed` | 12311 | 12312 | +1 |
| `/usr/bin/su` | 11795 | 11796 | +1 |
| `/usr/bin/tee` | 954 | 958 | +4 |
| `/usr/bin/textbox` | 32176 | 32177 | +1 |
| `/usr/bin/utilbox` | 44977 | 44980 | +3 |
| `/usr/games/gamebox` | 21657 | 21658 | +1 |
| `/usr/libexec/getty` | 13971 | 13975 | +4 |
| `/usr/libexec/smlrc` | 41767 | 41768 | +1 |
| `/usr/sbin/update` | 1213 | 1214 | +1 |

The fifteen positive changes total 27 packed bytes. Other executables decrease
by 41 packed bytes, but that decrease supplies no offset under the policy.
Each positive entry is a separate requested exception. All 158 comparison
surfaces outside these increases and the fifteen packed-byte decreases remain
unchanged. The exception admits only the exact values and identities above;
future growth requires its own review.

The root image retains 744 allocated data blocks, 235 free blocks and 32 free
inodes. Each program's raw bytes, loaded bytes and packed block count remain
unchanged. Both kernel resource vectors and native artifact byte sizes remain
unchanged. The largest loaded executable occupies 75,324 bytes of the 196,608-byte
process window, leaving 121,284 bytes before stack and heap admission. This is
static occupancy headroom, not a measured runtime allocation allowance.

## Alternatives and limits

The saved traversal preserves the parent bytes but fails the behavioral
regression by dropping callbacks. A fixed callback array changes storage policy
and introduces a new layout/capacity decision; the source-derived cost comparison
appears in [the Coherent witnesses](coherent-boundary-lifecycle-witnesses.md).
The live-head repair preserves existing node types, public declarations,
archive separation and allocation policy. Its standalone Cortex-M0+ object
retains 36 text bytes and 8 BSS bytes, while compiler-reported local stack
usage decreases from 16 to 8 bytes. Correct callbacks can perform additional
work; the record supplies no bound on user-handler allocations or execution time.

The benefit is correct execution of handlers registered during termination.
The rollback point is the parent source, with its known callback-order defect.
The alternative disposition keeps the repair unmerged while further measured
encoding/compression work seeks nonpositive packed bytes on every executable.
No finite encoding search establishes that such a variant exists.

Review status: the owner approves these exact deltas on 2026-11-12 and directs
continued qualification and review. The approval covers only the fifteen
positive packed-byte entries and candidate identity above. Aggregate
qualification and reviewed PR integration remain required.
Physical-board execution remains outside this evidence.
