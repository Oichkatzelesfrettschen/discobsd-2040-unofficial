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
| Revalidate historical v7fs and assertion findings | Active | Baseline host regressions pass. Repair the reproduced optimized-Python assertion-oracle gap, record source/test boundaries, and carry v7fs compensation candidates into persistent-image probes. |
| Map storage contracts | Pending | Trace descriptor, buffer cache, flash, Dhara and ROM operations with units, acknowledgements, ordering and persistence assumptions. Inspect pull request 435's experiment before reuse. |
| Implement shared failure interface | Pending | Trace typed events; exercise short/error/partial transfers and a second compensation failure; require activated injections. |
| Extend persistent-image oracle | Pending | Reopen images with fresh volatile state; account independently for ownership, free lists, inodes, directories and links; reject specified mutants. |
| Repair supported recoverable v7fs defects | Pending | Reproduce before editing; test allocation boundaries and failure classes; update import patches and manifest with source repairs. |
| Compare atomic recovery policies | Pending | Exercise undo, redo and copy-on-write models against identical traces and oracles; publish counterexamples, costs, assumptions and hardware residuals. |
| Review remaining components | Pending | Review all 654 components, record exact path witnesses, keep unsupported or blocked completion open, and add delta reviews. |
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

## Replay

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
