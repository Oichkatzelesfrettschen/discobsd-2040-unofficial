# Repository coverage, storage recovery, and pull-request acceptance

The audit baseline is commit `1e9864463c57f11c3a45db37d393652023843171`
(6,743 tracked paths). The baseline remains fixed; later source changes require
explicit delta review. A classification counts paths, while a component review
records contracts, failure classes, evidence, findings, and limitations.
Neither operation establishes the absence of other defects.

The executable denominator is `tools/audit/repository-coverage.json`.
`tools/repository_coverage.py` binds each path to its Git blob and mode, one
component, and one artifact class. The C17 source denominator and execution
inventory remain separate inputs with pinned blob identities. A reviewed
component requires a SHA-256-bound JSON witness covering exactly its assigned
paths. The validator establishes identity and structural consistency; the
reviewer establishes the substance of the source analysis.

## Ordered work

Only one item carries active implementation focus. Product jobs use the
worktree's logged execution lock. Physical-board access, raw-device writes,
power interruption, production keys, and provider execution remain outside
the authorized offline execution surface.

| Work | Status | Acceptance and next action |
| --- | --- | --- |
| Establish baseline and ownership | Complete for audit worktree | Remote main and worktree heads/statuses recorded; isolated baseline build passed. Preserve unrelated dirty worktrees. Recheck pull-request ownership separately before mutation. |
| Create coverage denominator | Complete; reviews pending | 6,743 unique paths and 654 pending component rows validate; 15 fixtures calibrate identities, missing/duplicate paths, references, closure and overwrite protection. |
| Revalidate historical v7fs and assertion findings | Complete for named contracts | Baseline host regressions and import checks pass. The optimized-Python oracle counterexample is repaired at `602915a2`. Additional source candidates remain separate probes rather than historical closure claims. |
| Map storage contracts | Complete for named write paths | `storage-recovery-contracts.md` traces units, volatile state, return conventions, acknowledgements and ordering through descriptor, kernel, flash, Dhara and ROM boundaries. Pull request 435's retained experiment was inspected at its recorded head. |
| Implement shared failure interface | Complete for host prefix model | Typed transfer callbacks execute the production descriptor backend; inactive injections fail. Positive shorts, errors before/after transfer, frozen interruption prefixes and paired compensation errors are calibrated. |
| Extend persistent-image oracle | Complete for exercised image range | Independent ownership, free-list, inode, namespace, link-count and metadata checks read fresh exported bytes. Corrupted-image controls and valid-but-changed metadata distinguish invariant failures from state differences. Larger sparse files retain a bounded-reader extension requirement. |
| Repair supported recoverable v7fs defects | Open; two repairs verified | Formatter cache and hard-link writeback/compensation repairs are committed at `167e856d`, with import patches and manifest. Verify remaining candidates: CLI parent-growth preflight, symlink writeback, rename compensation and out-of-range inode address encoding. |
| Compare atomic recovery policies | Complete offline; production admission open | Bounded policies pass 22,854 transaction/recovery cuts and negative controls. Dhara replay supplies operation counts and separate committed-recovery work. `storage-recovery-comparison.md` records the representation/reclamation/migration/interruption architecture stop. |
| Review remaining components | Active: reproduce kernel device flush route | Execute the independent-device `rwip`/`fsync` host probe, then review the remaining component rows with exact path witnesses and explicit delta reviews. All 654 baseline component rows still require ledger dispositions. |
| Determine takeover eligibility | Pending | Record UTC decision and exact five-hour cutoff, attributable activity and ownership, then recheck head and ownership immediately before mutation. |
| Complete eligible pull requests | Pending | Apply the user's separate contracts for pull requests 435, 436, 437 and later eligible requests; preserve experimental defaults and keep unmet readiness criteria visible. |
| Verify and integrate | Pending | Focused checks, logged-check and logged-ilp32 on final revisions; separate CI watchers; fresh review and merge-commit integration; retain evidence and remove only clean, pushed task-owned worktrees. |

## Recorded observations

The initial remote fetch advanced `origin/main` from `b9d64cbe` to
`1e986446`. The canonical checkout's local main remained `b9d64cbe` during
baseline acquisition. The audit worktree started at the remote revision.
The ownership inventory recorded unrelated tracked edits in the kernel-memory
device-group and ps/SwapRAM worktrees; those worktrees remain outside the edit
surface. The available app-server task inventory establishes only the tasks
that server exposes. Other agents and unpublished work need separate
attribution before takeover.

The initial lint command, `sh tools/check-lint.sh`, passed ShellCheck for
187 tracked scripts and Ruff for 307 tracked Python files. The baseline
`bmake MACHINE=rp2040 logged-build` passed with exit status zero after
131.852 seconds. The invocation ID is
`rp2040-build-local-20261112T182023Z-2716984-a3105cc6`; the invocation directory
under `.build-logs` retains `gate.log` and `status.json`. The caller selected
the Python interpreter explicitly. These results establish host lint and
cross compilation for the baseline, with build outputs local to the audit
worktree. They establish neither board execution nor power-loss behavior.

`bmake MACHINE=rp2040 check-repository-coverage` passes all 15 calibration
fixtures and the baseline ledger. `--require-reviewed` deliberately rejects
the ledger while component reviews remain pending. Initialization uses
exclusive creation and preserves an existing snapshot.

At the initial GitHub observation, pull requests 435, 436 and 437 last received
commits on November 11, 2026 UTC, while pull request 451 received a commit at
16:58:59 UTC on November 12. GitHub timestamps alone establish neither
takeover eligibility nor local inactivity. Pull request 435 reports Linux
and macOS gate failures and conflicts; pull request 436 reports successful
historical checks; pull request 437 reports only a skipped automated review.
Every reconciled candidate needs revision-bound acceptance checks.

The baseline assertion, v7fs, and import-provenance invocation
`audit-v7fs-assert-20261112T182405Z-2765539-54f2f9c3` retains its full log and terminal status locally. The selected command was
`bmake MACHINE=rp2040 check-libc-assert check-v7fs-core check-netbsd-imports`;
the runner reports exit zero after 72.2 seconds. The v7fs run exercised both
byte orders, the user backend, the fixed-pool descriptor backend with
`NDEBUG`, alignment sanitization, and the suite's calibrated reversals and
mutants. The import gate matched all 37 files. Linux linker-wrapped failure
cases carry Linux host evidence; optional variants retain their own limits.

The assertion harness used Python `assert` statements for verdicts, so
`PYTHONOPTIMIZE=1` removed the negative-control checks. A compiler wrapper
that returned success for the deliberately false static assertion made the
baseline harness print `assert contract: PASS` and exit zero. The dedicated
oracle regression observed that false acceptance before the repair. Explicit
verdict checks replace removable Python assertions; the same optimized-Python
control then reports `false static_assert compiled` and exits with failure.
The positive optimized-Python case and both existing assertion configurations
pass. The compiler wrapper carries its compiler selector as quoted arguments;
the generated shell text contains only a static `exec "$@"` dispatch.

Read-only source analysis identified three further v7fs probes rather than
establishing completed repairs: the CLI parent-growth preflight counts a data
block while indirect growth can also require index blocks; the core hard-link
helper ignores its final inode-writeback result; and rename failure propagation
does not establish namespace compensation. Each candidate requires an activated
injection, a fresh image read, and a claim-specific oracle. Existing root-block
ownership checks cover data/index/free-list partitioning for their fixtures;
they leave directory reachability, link counts, and parent-directory creation
at indirect growth boundaries for the new oracle.

The persistent-image reader exposed an additional formatter invariant: a
64-inode image received a 100-entry cache containing candidates through inode
101. The formatter now bounds its initial cache by the existing inode count.
The hard-link matrix activated errors at the final inode write in both byte
orders and observed success before repair. The corrected helper removes the
new directory reference before restoring the saved inode.

The candidate tree `fbb382598d73c3c434de095c4f741af5a96b6098` became commit
`167e856d`. Its logged build passed; invocation
`audit-v7fs-persistence-20261112T185137Z-2850298-ce4f16d8` passed the new
storage gate, historical v7fs gate and all 37 import entries after 83.2 seconds.
Matched GCC 16.2 Cortex-M0+ object measurements add 64 text bytes for
`v7fs_file_util.c` and 16 for the formatter, with unchanged object data/BSS.
The hard-link function's static frame changes from 24 to 128 bytes; the
formatter's frame remains 592 bytes. Those measurements cover static compiler
frames, rather than runtime call-chain peaks.

Invocation `audit-v7fs-prefix-20261112T185532Z-2861646-2880cb0c` retains its full log and terminal status locally. The matrix completed 49,248
ordinary-error prefixes and 49,248 interrupted prefixes across both byte
orders, regular/directory creation and full 1-, 10- and 138-block parents.
All ordinary-error cases restored the then-declared semantic checks. The
reader rejected 43,854 interrupted images; 5,394 matched the old state.
The subsequent metadata-comparison extension requires a fresh final matrix
before extending that receipt's claim to owners, permissions and timestamps.

The paired-failure extension activates 20 second errors during actual core
compensation. All operations report EIO; the reader accepts 12 resulting
inspection states and rejects eight. A successful later superblock writeback
does not establish that failed compensation restored the image.

The storage write-path review also identified a kernel probe for the next
component review. A partial cooked-device write addresses `i_rdev`, while a
zero-size block inode gives `syncip` an empty data loop and `fsync` selects
the filesystem containing the node. The independent-device host fixture must
keep the descriptor open, observe dirty target buffers immediately after
`fsync`, and use explicit target `bflush` plus a same-device case as controls.
`share/man/man2/fsync.2` supplies the interface requirement. The source route
is established; the actual kernel host reproduction remains pending.

## Replay

The public-header review reproduced a separate C17 limits defect. The
RP2040 `ULONG_MAX` macro had the value `0xffffffff` with type `unsigned int`;
C17 5.2.4.2.1 requires the corresponding promoted `unsigned long` type.
The new `_Generic` assertion failed against the baseline spelling. The
`UL` suffix repairs the type, and `check-c17-headers-cross` passes the
value/type checks, all header controls and object-import checks. The
`limits_ulong_uint` mutation restores the defective spelling and must fail
the type assertion. The change affects a preprocessing constant and adds
neither target storage nor executable instructions.

Select and export `PYTHON`, then run:

```sh
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-repository-coverage
"$PYTHON" tools/repository_coverage.py --require-reviewed
```

The final command remains a failing completion gate until all component
reviews carry valid witnesses. A later main revision changes the audit
frontier through a reviewed delta; initializing over the baseline ledger is
prohibited. Local invocation logs remain under `.build-logs` and carry their
own environment and timing boundaries.
