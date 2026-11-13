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

Repair witnesses retain the baseline path identities and name an immutable
tested descendant commit plus its exact tree. The validator checks that a
repaired finding's commit precedes the passing test revision. Calibration
rejects mutable revision names, unrelated roots, stale trees, tests preceding
the repair, failing verdicts and substituted evidence. The 22 fixtures cover
the original denominator checks and those descendant-evidence boundaries.

## Ordered work

### Active delivery scope

The owner's scope revision limits active delivery to this session's owned
changes, including the v7fs repairs and device write-error lifetime work on
`audit/storage-contracts`. Other agents retain their pull requests. Inactive
pull-request takeover and the remaining repository-wide component reviews
leave this session's execution queue; their open ledger entries remain
retained obligations rather than completed reviews.

Delivery proceeds through bounded storage regression closure, reconciliation
with origin main, focused checks, final logged build/check and ILP32 gates,
then review and merge-commit integration of the owned changes. Replacement
rename tests must model caller reclamation explicitly. Production atomic
recovery remains a separate architecture decision. Further experimental
expansion requires a concrete defect or an acceptance requirement.

The fixed baseline and coverage counts remain evidence for the broader audit;
they do not measure completion of this narrower delivery scope.

### Owned-change completion checklist

The delivery objective is to verify, publish, review and integrate this
session's v7fs, storage-test and device write-error lifetime changes through
origin pull requests. Ownership follows the session's branch and commits,
rather than the shared GitHub account's authorship field.

- [x] Enumerate every byte prefix of the nonreplacement rename control writes
  in both byte orders and retain separate compensation/interruption verdicts.
- [x] Complete replacement-rename caller reclamation tests, including the
  intermediate unreachable inode, successful release, live-inode rejection,
  repeated-release rejection and bounded rename-write error compensation.
- [x] Reconcile the storage tests, import patches, manifest and evidence
  documents; retain explicit limits for failed compensation and power loss.
- [x] Review the shared device first-error lifetime through last close,
  per-active-inode replicas, alias propagation, cached-inode reuse and unit
  admission against the retained regression and mutation evidence.
- [x] Fetch origin main, inspect its delta and merge it into the owned branch
  while preserving the fixed audit baseline and other agents' work.
- [x] Run focused gates for reconciled changes and resolve their failures.
- [x] Run final logged-build, logged-check and logged-ilp32; bind receipts to
  the tested head, base, configuration and resulting tree.
- [ ] Review the final diff, push to origin and open the owned pull request;
  keep it in draft while acceptance requirements remain open.
- [ ] Watch hosted CI, address review findings and refresh affected checks
  after every relevant source or base change.
- [ ] Merge through the required merge-commit workflow after acceptance,
  update canonical main and retire only clean, pushed task-owned worktrees.

Other agents own their pull requests. Inactive-PR takeover, the remaining
repository-wide reviews, production atomic-recovery design, provider jobs and
physical-board experiments remain outside this delivery queue. Retained open
audit entries remain open; integration of the owned changes does not establish
repository-wide audit completion.

### Integrated validation evidence

Commit `e71483dfdaf3f1f10d4a3e56c20f4b83a66fc3ca` integrates main
`11a3f97045aa5631533d0209217eda288c1f9391`; its tree is
`cdfb7c95b6ac3751f35fa4b05218e97920a3b55d`. A locked world clean
precedes the logged build so the libc and sysctl interface changes reach
relinked consumers. With `PYTHON` selected as the host Python 3 interpreter
and `MACHINE=rp2040`, the following local receipts complete with exit zero:

- `rp2040-build-local-20261113T012620Z-3747964-9780e678`: clean-world
  build, including the kernel links, in 156.311 seconds.
- `storage-device-focused`: `check-ufs-disk-ilp32` passes 1,968 checks;
  `check-ufs-disk-mutants` rejects all 47 mutants through named assertions.
- `storage-integrated-focused`: import verification matches 37 files;
  v7fs core, persistent-image compensation, bounded transaction models and
  the fixed-baseline coverage ledger pass. The ledger's 22 calibrations pass
  and its 658 unreviewed components remain pending.

The seven source hashes in `block-device-write-error-resources.json` match
the integrated kernel sources. Those retained object and static-frame
measurements retain their original compiler/configuration scope. Source review
traces `biodone`, `bdev_write_error`, `openi`, `closei`, `vn_closefile` and
`iget` through error publication, alias inheritance, final-close drain and
cached-inode reactivation. The deciding fixture exercises those lifetimes;
physical persistence and arbitrary scheduling interleavings remain separate
claims. Final aggregate and ILP32 receipts remain required before publication.

Commit `78861103e04e750c6fd19db7ef3d20d1eea3db75` records that evidence
without changing production or test code. Its logged ILP32 invocation
`rp2040-ilp32-local-20261113T021500Z-1425242-c73384db` passes all 189
execution-inventory entries in 758.151 seconds. Its aggregate invocation
`rp2040-check-local-20261113T013437Z-15170-48c83867` passes the build stage
and fails the check stage: `games/pzcommon/tests/mutants.py` reports
`baseline flood: the real sources do not pass: timeout` at its unchanged
30-second execution bound. The standalone Flood baseline earlier in that
aggregate passes 3,227 checks.

An instrumented reproduction invokes the mutation runner's `build_support`
and `run_logic` with its address/undefined sanitizer selection and existing
timeout. It passes the same 3,227 checks in 14.051 seconds wall time and
7.150 seconds child CPU, including compilation. The host reports a one-minute
load of 22.01 with 12 online CPUs. Scheduling pressure remains a hypothesis;
the passing reproduction does not replace the failed aggregate. The aggregate
requires a complete passing rerun, with its timeout and acceptance unchanged.

The parallel aggregate on `1e1628ff` passes the complete puzzle gate,
including all four rule baselines and its logic and terminal mutation controls.
It reaches `check-cache-footprint`, which rejects the mount array because its
expected sizes omit the four-byte device-operation guard. Commit `b60a2aa9`
binds the single-record array to 1,040 bytes and the two-record array to
2,448 bytes, with historical cache savings retained separately. The focused
footprint gate passes all four kernels and both wrong-layout rejection controls.

The parallel run also exposes closed jobserver descriptors in detached make
queries. Commit `bb8bc182` removes inherited `MAKEFLAGS` and `MFLAGS` from
the kernel-metadata and MSDOS queries. Their focused parallel gates pass
without invalid-jobserver warnings; ShellCheck, Ruff and changed-text checks
pass. The incomplete MSDOS mutation trees retain their preexisting missing-Git
and missing-patch diagnostics. On `bb8bc182`, separate logged runs also pass
the QEMU, host-package, board-build, cross-kernel and assembler checks that
the failed aggregate had not completed. Final aggregate and ILP32 validation
remain open until the clean candidate receives their terminal passing receipts.

The aggregate on `56f662fa50054da7a0071a07f67019c33286ad76` completes
both build and check stages with exit zero. Its invocation
`rp2040-check-local-20261113T025324Z-2891325-dd0222b1` takes 1,041.851
seconds overall, including 1,008.2 seconds in the check stage. The passing
run preserves the original puzzle timeout and includes the corrected footprint
and detached-query gates.

Merge `d8f96ce5698831b6f2a17b8ab03c9acde630038c` incorporates main
`88abf7151e25b4fc1c01682587561f7c9197f7bc`; its tree is
`f90fa23411be17dbf5d94fac59070a58ba769c8d`. The incoming termination and
context-sort changes alter libc, public declarations and execution inventories.
The workflow comment resolution retains both the storage coverage gate and
the libc execution classes. A locked world clean precedes logged build
`rp2040-build-local-20261113T031154Z-3746428-35858314`, which passes in
59.769 seconds with `MACHINE=rp2040` and the exported host Python interpreter.

Invocation `storage-libc-focused-20261113T031306Z-3880074-5ad48c66` passes
in 22.912 seconds. Its targets cover exit lifecycle, quick exit and context
sorting on host and cross configurations, C17 headers on host and cross,
a.out linkage, the source denominator, execution inventory and retained
coverage ledger. These checks establish the affected interface contracts;
final aggregate and ILP32 receipts remain required for the combined candidate.

Candidate `e9a808d909874de082b7ae563c0c22ccafb40603` has tree
`d832665a865709e73d720a07d1d56372affd5e43` and integrated main base
`88abf7151e25b4fc1c01682587561f7c9197f7bc`. Its final aggregate receipt
`rp2040-check-local-20261113T031404Z-3932925-591a2915` records completed
build and check stages with exit zero in 1,097.232 seconds overall. Its final
ILP32 receipt `rp2040-ilp32-local-20261113T033238Z-452143-86bca3fd` records
196 of 196 passing execution-inventory entries in 369.355 seconds. The
worktree remains clean throughout both runs. The subsequent acceptance-record
edit changes documentation only; product and test inputs retain these tested
identities. Publication, hosted checks, review and integration remain open.

Only one item carries active implementation focus. Product jobs use the
worktree's logged execution lock. Physical-board access, raw-device writes,
power interruption, production keys, and provider execution remain outside
the authorized offline execution surface.

| Work | Status | Acceptance and next action |
| --- | --- | --- |
| Establish baseline and ownership | Complete for audit worktree | Remote main and worktree heads/statuses recorded; isolated baseline build passed. Preserve unrelated dirty worktrees. Recheck pull-request ownership separately before mutation. |
| Create coverage denominator | Complete; reviews pending | 6,743 unique paths validate. Refining libc by source directory yields 664 components: six reviewed and 658 pending. Twenty-two fixtures calibrate identities, references and revision-bound repair evidence. |
| Revalidate historical v7fs and assertion findings | Complete for named contracts | Baseline host regressions and import checks pass. The optimized-Python oracle counterexample is repaired at `602915a2`. Additional source candidates remain separate probes rather than historical closure claims. |
| Map storage contracts | Complete for named write paths | `storage-recovery-contracts.md` traces units, volatile state, return conventions, acknowledgements and ordering through descriptor, kernel, flash, Dhara and ROM boundaries. Pull request 435's retained experiment was inspected at its recorded head. |
| Implement shared failure interface | Complete for host prefix model | Typed transfer callbacks execute the production descriptor backend; inactive injections fail. Positive shorts, errors before/after transfer, frozen interruption prefixes and paired compensation errors are calibrated. |
| Extend persistent-image oracle | Complete for exercised image range | Independent ownership, free-list, inode, namespace, link-count and metadata checks read fresh exported bytes. Corrupted-image controls and valid-but-changed metadata distinguish invariant failures from state differences. Larger sparse files retain a bounded-reader extension requirement. |
| Repair supported recoverable v7fs defects | Open; eight mechanisms repaired | Formatter cache and hard-link compensation are committed at `167e856d`; rename hierarchy validation precedes target replacement at `ff07aeab`; parent-growth admission includes index blocks at `7a9fa7e3`; symlink writeback reports its final error at `640fe310`; inode serialization rejects addresses outside 24 bits at `eb27bc90`; rename publication failures restore the source namespace and a displaced target at `8ca92acd`; and same-inode rename aliases remain unchanged at `64d8410d`. Imported-source changes carry synchronized patches and manifest hashes. Failed compensation, fail-after-transfer behavior and interruption atomicity remain open. |
| Compare atomic recovery policies | Complete offline; production admission open | Bounded policies pass 22,854 transaction/recovery cuts and negative controls. Dhara replay supplies operation counts and separate committed-recovery work. `storage-recovery-comparison.md` records the representation/reclamation/migration/interruption architecture stop. |
| Review remaining components | Active: verify device error lifetime and unit admission | The host probe reproduced queued-buffer routing and unmounted asynchronous error loss. The candidate retains shared first-error replicas in active inode storage through last close and drains busy writes. The mount-publication schedule reproduces raw/whole-device admission races; the unit guard passes the deciding host schedules. The extended fixture at `7b171dce` passes 1,968 checks and rejects 47 mutants, including stale transient device state across cached-inode reactivation; the buffer-pressure fixture passes 53 checks and rejects its four behavior mutants. Selected Clang analyses pass, and matched Cortex-M0+ resource captures retain source hashes. Final clean-revision logged gates remain required before publication. `block-device-write-error-lifetime.md` owns the contract and evidence limits. Six source-review components cover 101 paths; 658 components remain pending. |
| Determine takeover eligibility | Pending | Record UTC decision and exact five-hour cutoff, attributable activity and ownership, then recheck head and ownership immediately before mutation. |
| Complete eligible pull requests | Pending | Apply the user's separate contracts for pull requests 435, 436, 437 and later eligible requests; preserve experimental defaults and keep unmet readiness criteria visible. |
| Verify and integrate | Pending | Focused checks, logged-check and logged-ilp32 on final revisions; separate CI watchers; fresh review and merge-commit integration; retain evidence and remove only clean, pushed task-owned worktrees. |

## Recorded observations

The rename fail-after-transfer probe at `b14c9324` finds a mismatched parent
entry in both byte orders. The seventh write (trace sequence 28, image byte
offset 5632) persists the moved directory's new `..` entry before reporting
`EIO`. Namespace compensation restores `/source`, while the independent image
reader rejects inode 3's destination-parent entry. The regression
`test_rename_write_prefix_failures_restore_persistent_invariants` exercises the complete
transfer followed by error, requires the old namespace and valid independent
ownership/link accounting, and reopens the exported image for continued
allocation. The candidate restores `..` before namespace compensation;
failure of that restoration returns its error and retains the inspection
boundary. Commit `39d2cc28` carries the repair.

The expanded nonreplacement rename matrix covers every one of seven control
writes in both byte orders at every transferred length from 0 through 512.
All 7,182 reported-error cases restore namespace, inode
metadata, sizes, link counts and free totals, and accept continued allocation
after a fresh mount. Directory-entry byte ordering remains outside compensated
semantic identity. The same 7,182 frozen interruption positions yield 404 old,
992 new, 688 other reader-valid and 5,098 reader-invalid reports. Two fresh mounts
leave each interrupted image unchanged; mount acceptance does not repair the
image or establish recovery. All 54 paired failures, selected from every
observed compensation write after a full-transfer error, report `EIO`; eight
inspection states pass the reader and 46 fail it. Prefix enumeration is complete
for these seven control writes; paired failures retain their separate bounded
selection. The host storage gate passes in this configuration, with the fault
matrix completing in 75.253 seconds.

The successful replacement probe leaves the displaced inode allocated with
zero links, so the independent reader reports an unreachable inode. The
imported `v7fs_file_deallocate()` drops namespace/link ownership and leaves
reclamation to its caller, as `usr.sbin/v7fs/v7fs.c` documents for removal.
The test bridge models the caller's release after a successful replacement:
it contracts the displaced inode's data and deallocates the zero-link inode.
Both byte orders reject release of a linked inode and repeated release, restore
one free inode and one free block, and support continued allocation after a
fresh mount. Every replacement-helper control write receives errors after
zero-byte and full-byte transfers; successful compensation restores namespace,
inode metadata and free totals. The full storage gate passes with six fault
matrix tests. Reclamation-write failure recovery remains untested; the adapter
establishes successful caller release rather than a recovery guarantee.

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

`bmake MACHINE=rp2040 check-repository-coverage` passes all 22 calibration
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

## Reconciled kernel and verification dependencies

The cooked-device implementation is retained at `70ea03e2`. Merge
`da4b2b15` reconciles remote main `e734a4ea`, adding the swap-ring allocator,
pooled-process accounting and retained board observations to the candidate;
the 6,743-path audit baseline remains fixed. The merge's logged build passes
in 108 seconds. Its aggregate then fails workflow-observability validation
because the storage-prefix runner has an unregistered label.
`82300c60` registers the exact one-block-parent command and adds a substituted
parent-range control; all 38 observability cases pass.

The shared-source review identifies STM32's minor-bit-3 unit selector.
`efaaa74a` uses that selector for its guard and extends the extracted-function
probe. The lower-level buffer-pressure fixture also needs a link boundary:
`2212531d` keeps its real buffer-call closure with function sections, while
the disk fixture retains the device-admission closure. The two fixtures keep
their respective calibrated behavior checks. The design and resource limits
remain in `block-device-write-error-lifetime.md`. A fresh aggregate and
ILP32 inventory on the resulting clean head remain pending; the failed
aggregate supplies no aggregate pass for its preceding revision.

The fresh aggregate
`rp2040-check-local-20261112T213213Z-2026801-69edab66` completed its build
stage and failed the check stage when the V7 format fixture linked
`sys_inode.c` without the device-lifetime helper closure. Commit `37626590`
adds fail-closed special-device stand-ins and the host interrupt state. The
focused format gate then passes 1,017 checks and rejects all 23 source
mutants. The failed aggregate remains a failed result; a later aggregate is
required for the repaired revision.

The later aggregate
`rp2040-check-local-20261112T225909Z-630958-d6e1f2b5` also completed its build
stage and failed the check stage. The storage-correctness fixture linked
`ufs_subr.c` after `syncip()` gained the shared block-device synchronization
boundary, but its host closure supplied no `bdevsync()` implementation. The
linker reported both unresolved calls after 1,074.6 seconds of check-stage
execution. Commit `95ce84ad` adds the fixture model and block-inode cases that
bind synchronization to `i_rdev`, propagate the shared device error and keep
inode publication behind successful synchronization. The focused gate passes
5,911 baseline I/O checks and rejects a containing-filesystem alias mutant.
The aggregate receipt remains failed; focused repair evidence does not replace
a fresh aggregate on the final revision.

The persistent-image probe found that `v7fs_file_rename()` deallocated an
existing target before `can_dirmove()` rejected a directory move into its
descendant. Empty-directory targets eliminate the source/target type mismatch
alternative: both little-endian and PDP-11 images returned `EINVAL`, issued
four writes, lost the target name and reopened with an unreachable inode.
Commit `ff07aeab` moves hierarchy validation ahead of target lookup and
deallocation. The fixed-pool regression now observes zero operation writes,
byte-identical persistent state, fresh-mount source and target names, and an
independent valid image in both byte orders. A valid cross-parent rename
remains reachable. The core gate separately injects source-inode,
hierarchy-parent and target-name read failures and rejects a mutant that
restores late validation. `tools/netbsd_imports.py verify` matches all 37
entries. The required logged build
`rp2040-build-local-20261112T224926Z-488837-192ffb9c` passes in 82.572 seconds;
the storage-recovery and import gates pass after it. These results establish
ordinary execution and host prefix-error behavior, rather than interruption
atomicity for the remaining rename publication sequence.

The command-line capacity probe creates root directories at exactly 10 and
138 blocks, the direct-to-single and single-to-double indirect transitions.
The former one-block estimate admitted a payload whose operation returned
`ENOSPC` after changing persistent bytes. Commit `7a9fa7e3` derives the parent
increment through the same `blocks_for()` model used for the payload. Both
byte orders now refuse the operation with byte-identical images, and the
under-count mutant changes those images and fails.

The symlink probe injects the second inode-sector write, after target data and
the first inode update have persisted. The prior helper returned success.
Commit `640fe310` returns the final `v7fs_inode_writeback()` result; a fresh
mount confirms the already-persisted target bytes and the independent reader
confirms ownership for the exercised non-root inode. The ignored-error mutant
fails the regression. This result establishes error observability after that
write; it does not make the multi-write operation atomic.

The inode serializer previously passed each 32-bit in-memory address directly
to a 24-bit codec, so `0x01000000` became zero on disk. Commit `eb27bc90`
checks every one of the 13 address slots before the inode sector is read or
written. The regression admits and round-trips `0x00ffffff`, rejects the next
value with `EFBIG`, and compares the raw sector byte-for-byte in PDP-11 and
little-endian formats through both host backends. The truncation mutant fails,
the v7fs core gate passes, and the import verifier matches all 37 manifest
entries. These finite endpoint checks establish the exercised serializer
boundary, rather than arbitrary corruption detection.

The cross-parent rename regression enumerates every write position issued by
successful directory moves with and without an existing empty-directory
target. A failure before transfer at each position returns `EIO`; a fresh
mount then finds the source at its original name, the original `..` entry,
the original parent and target link counts, and any displaced target at its
original name and inode. The independent reader accounts the exercised root,
source, target-parent and displaced-target data and index blocks against the
free list. Each recovered image accepts a successful retry. Four mutants
remove source or target compensation at the add, source-removal and parent
replacement boundaries, and each fails its named regression. The compensation
identity is semantic: directory entry ordering and unreachable bytes beyond a
restored logical directory size are outside it. Commit `8ca92acd` carries the
repair, synchronized NetBSD patch and manifest identity. Logged build
`rp2040-build-local-20261113T001616Z-1986941-e04c30ec` completed with exit zero;
the v7fs core gate passed both byte orders and both descriptor backends, the
import gate matched all 37 entries, and the ShellCheck error-level gate passed.
These checks establish ordinary fail-before-transfer recovery. The
implementation gives a compensation error precedence over the initiating
error, while systematic second-error enumeration remains open alongside
fail-after-transfer behavior and interruption atomicity.

The direct v7fs rename helper lacked the kernel rename path's identical-inode
short circuit. An exact source/target path issued writes, removed its only
directory name and changed its link count. Two hard-link names for one inode
also collapsed to the target name and lost one link. The regression observed
both counterexamples before the repair. Commit `64d8410d` returns success when
the target lookup resolves to the source inode, before target deallocation.
Both cases now issue zero writes, retain every name and the original link
count after a fresh mount, and pass independent block/free-list ownership
accounting. The identity-guard mutant fails both named cases. Logged build
`rp2040-build-local-20261113T003031Z-2286976-650d48a8` completed with exit zero;
the v7fs core gate passed both byte orders and both descriptor backends, the
import gate matched all 37 entries, and the ShellCheck error-level gate passed.
This result establishes ordinary same-inode rename identity for the imported
helper; it adds no interruption-atomicity guarantee.
