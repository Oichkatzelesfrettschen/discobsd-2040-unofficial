# Selected 2.11BSD semantic changes and recipient decisions

The fork already contains the inode-rescan correction, sized tar hard-link
paths, the stronger tar allocation-failure cleanup, the umount status repair,
and substantial sysctl and stdio contracts. Preserve those mechanisms. A
textual distance or numbered patch does not decide whether a behavior is
present, equivalent, missing or safe to transplant.

`211bsd-semantic-ledger.json` is the canonical selected-change inventory.
Each stable key names one bounded invariant, original patch/hash/commit/parent,
donor and recipient paths/symbols, disposition, evidence, regression variants,
dependency and resource/ABI/licensing consequences, separate recipient
decisions, and the next action. Shared patch records supply provenance only.
The inventory deliberately leaves other hunks and the wider series open.

## Snapshot and execution identity

The recipient snapshot is merge `cc67531a2830fbd99b2c3da6097b8bcd51da1eb9`.
Its sysctl correction merged through PR #213. Source presence and ancestry
are separate: `present` records the reviewed mechanism, not proof that the
recipient imported a particular historical hunk.

`211bsd-execution-evidence.json` retains selected actual invocations from
[firmware run 35677329437](https://github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial/actions/runs/35677329437),
artifact `10673107931`, job `firmware/posix-sh`. The complete report records
62/62 PASS at tested integration revision
`261f1233bc1b1b20d9f44585f6f45fc830473adb`, with tracked files unmodified.
`211bsd-execution-report.sanitized.json` is a deterministic derivative of the
hosted artifact. The original report SHA-256 is
`aef91b1f4ae8cfbc10b47bc5b2d7cbd01a24cf077ce4b94e05a40cd890885101`.
The derivative SHA-256 is
`7c11a19241ebcbc5c41a6278bfad840102c401c6dea13fe02214c5e0fb155d7b`.
`tools/analysis/sanitize_bsd211_execution_report.py` version 1 validates the
original hash and known GitHub workspace and temporary-output path shapes,
then replaces them with repository-relative paths or explicit temporary-path
tokens. Unexpected absolute paths and harness forms fail. Sorted compact JSON
with one trailing newline fixes the derivative bytes. The verifier authenticates
those bytes and requires exactly the 62 pinned variant IDs, each with a PASS
receipt, the designated owner and width, actual invocation and executable hash.
The 13 selected projections join those same report rows. The original bytes
remain available from the named hosted artifact while GitHub retains it and
from the earlier repository history; the current tree retains the derivative,
not the original. A retained copy of the signed integration commit object
reconstructs that revision and its tree.
The tested integration revision and landed merge both name tree
`e43c0a1508c826a05337a70fe0d259c7bb516642`. The verifier reads and hashes each
named source and the execution inventory through both authenticated endpoints.
Those comparisons cover the listed source/test files, not every build input.

`211bsd-donor-witness.json.gz` retains 70 commit, 31 tree and 16 blob objects
from the curated donor as a 409,357-byte deterministic archive. Stored DEFLATE
blocks make its gzip bytes independent of the host zlib version. The verifier
recomputes each Git object ID from its type and bytes. Parent edges connect
the pinned donor tip to every selected reconstructed patch commit, including
patch 499 through a merge parent rather than an assumed linear patch chain.
The tip tree identifies `PATCHES.md`; its blob hash matches the recorded donor
ledger SHA-256. Each selected patch row then matches that ledger's abbreviated
commit and raw-patch-hash attestation. Each original source path resolves
through the selected commit's tree to a retained source blob containing the
named symbol. The archive keeps the donor source bytes and notices unchanged.
This authenticates what the curated repository records; it does not
independently hash the unavailable original numbered patch texts or establish
that a donor implementation satisfies the recipient invariant.

Regenerate the donor archive from the pinned checkout with
`"$PYTHON" tools/analysis/bsd211_donor_witness.py --donor <pinned-211bsd-checkout> --ledger docs/research/211bsd-semantic-ledger.json --output <temporary-directory>/witness.json.gz`,
then compare it byte for byte with the tracked archive. The ordinary verifier
uses only the retained Git objects and requires no donor checkout or network.

Replay the derivative from artifact `10673107931` in run `35677329437` with
`gh run download 35677329437 --repo Oichkatzelesfrettschen/discobsd-2040-unofficial --name ilp32-execution --dir <temporary-directory>`,
then set `PYTHON` to the selected interpreter and run
`"$PYTHON" tools/analysis/sanitize_bsd211_execution_report.py <temporary-directory>/ilp32-execution.json <temporary-directory>/derivative.json --original-sha256 aef91b1f4ae8cfbc10b47bc5b2d7cbd01a24cf077ce4b94e05a40cd890885101`.
Compare the derivative to the tracked JSON byte for byte. The replay depends on
the hosted artifact remaining available; the retained derivative and its hash
do not independently reconstruct unavailable original bytes.

`executed` means the stated bounded assertions have execution receipts.
`open` retains a missing validation witness or an unresolved implementation
question. Source-reviewed presence can remain open: tar's failure cleanup
exists, but its allocation-refusal test has yet to execute. Neither state
claims complete patch coverage, universal conformance or board behavior.

<!-- semantic-ledger-summary:start -->
| Mechanism | Disposition | Validation |
| --- | --- | --- |
| `inode-rescan-distinct-cache` | present | executed |
| `tar-sized-hardlink-path` | present | open |
| `tar-path-allocation-cleanup` | present | open |
| `umount-operand-exit-status` | present | executed |
| `sysctl-swapmap-entry-export` | present | executed |
| `sysctl-swap-block-count` | present | executed |
| `sysctl-bounded-old-value` | present | executed |
| `sysctl-dispatch-length-error-order` | present | executed |
| `sysctl-exact-structure-input` | present | executed |
| `stdio-string-one-byte-pushback` | equivalent | executed |
| `stdio-update-stream-mode` | equivalent | executed |
| `ufs-clean-state-transition` | unresolved | open |
| `ufs-superblock-clock-order` | unresolved | open |
| `shutdown-pre-reset-flush` | unresolved | open |
| `shutdown-busy-drain-report` | missing | open |
<!-- semantic-ledger-summary:end -->

The allowed dispositions also include `superseded`, `not-built` and
`not-applicable`. `not-built` says the implementation is excluded from a
particular build; a maintained unshipped utility can still need a portable
fix. `not-applicable` requires an incompatible or absent target mechanism,
rather than an absent matching pathname. Neither replaces investigation.

## Donors have different purposes

| Repository | Pinned observation | Use |
| --- | --- | --- |
| `Oichkatzelesfrettschen/2.11BSD` | `41cb29e6f8e3939a9a30122b9f635aa905e7b450`, `VERSION` advertises 431 | Historical comparison baseline |
| `Oichkatzelesfrettschen/211bsd` | `0a38e59d742ff4371b78ea65c5bcc50477b31708`, `VERSION` advertises 499; `PATCHES.md` records individual raw hashes | Curated reconstruction and provenance |
| `AaronJackson/2.11BSD` | `dff9640c6e8d4e7fcc3ca83b0ec7716cfe9f447d`, `VERSION` advertises 479 | Additional comparison source, without assuming coverage through 499 |
| `Oichkatzelesfrettschen/2.11BSD_X44` | `31710fa6903bdb36a9ba605175b1cee96ec0d931`, `README.md` describes VM/vnode restructuring and warns its documented release build fails | Architectural research and isolated candidates |

The VERSION observations were read at the full IDs through GitHub's contents
API. The curated donor and X44 observations use pinned local Git blobs.
Advertised versions and README claims describe repository material, not
verified implementation correctness or a fresh build result.

The curated `PATCHES.md` blob has SHA-256
`2fb98757700f8f6e14d0afa3ae616569c4edf69896291710c5666873973823e8`.
The raw hashes in the JSON are **donor-ledger attestations**. The originals
were outside the inspected checkout; independent raw hashing remains open.
The ledger points to titor-labs `archives/bsd/2bsd/patches/` and
`provenance/CHECKSUMS-2bsd-patches.tsv` for the original material.

Patch 437 is a placeholder with attested raw hash
`80b07f37e72a7f2723f85f4119fb4e58cea610d31e73790776eae55e45c49204`
and no implementation commit. The ledger describes September 15, 2026 text
numbered 500 as an announcement and supplies neither a code commit nor a raw
hash. Neither adds an unapplied implementation to the queue.

The donor's `_fwalk` correction is already recorded at
`8657c689c6fcfb3c495488e0227091acfcb03bd4`, parent
`ab5dc1df2f4a94439d11f212ca6d6725f72586ed`. It advances the cursor and visits
active streams, deliberately differing from published patch 499. Preserve
the published defect as provenance; the correction is known work rather than
a newly discovered port. The retained report remains unsent.

## Mechanism boundaries and residual work

Patch 433's `fs_ninode` reset survives relocation from `sys/sys/ufs_alloc.c`
to `sys/kern/ufs_alloc.c`. Its ILP32 tests cover overlapping scans, near-full
cache state, exhaustion and subsequent allocation. Other patch-433 hunks
remain outside that disposition.

Patch 446's tar and umount changes have separate rows. A local
`bmake MACHINE=rp2040 check-tar-host` run at `cc67531a` executed 64 hard-link
identities, bounded archive storage, restored inode sharing and missing-link
path reporting. The tar row stays open for explicit allocation-extent
enforcement; the cleanup row needs deterministic allocation-refusal coverage.
PDP-11 arithmetic and RK-device changes stay in native or explicitly isolated
legacy review, outside the ARM implementation queue.

Patch 463's attempted map export is followed by patch 464's source-pointer
correction. Retain the recipient's corrected entry copy. Patch 475 supplies
bounded old-value and error-ordering contracts but keeps the short-input
structure bug. The exact-sized replacement is a local follow-up, with
production helper call sites still unestablished. Failure doubles test
propagation and ordering, not partially completed real-copy atomicity.

Patches 456, 458 and 461 frame separate questions about clean state,
superblock time and shutdown ordering. Trace dirty inodes/superblocks through
sync, buffer completion, `flstrategy`, `fl_sync`, `dhara_map_sync` and reset.
`sync()` can skip locked state and discards `ufs_sync()` errors; `B_BUSY`
clears after failed writes as well as successful writes. `m_write_error`
records an error since mount, not necessarily one caused by shutdown.
Unconditional `done` is a reporting defect; physical data loss remains
unestablished. Keep IRQ/reset requirements and avoid importing PDP-11 shutdown
or filesystem-format changes as a bundle.

Retain #207's distinct contracts: `check-root-noatime` checks Config/root
mount/fstab policy structure with mutations; required ILP32 explicit-time
tests link `ufs_setattr()` and exercise explicit timestamp requests. The
metadata-traffic policy says neither how many programs/erases are avoided
nor whether shutdown data becomes durable.

Retain the smaller stdio core. The Torek prototype's median +534 loaded
bytes, +256 initialized data bytes, and adminbox 21-to-23 packed blocks
against a 22-block budget belong to baseline
`39b1bf774df2b858222c3f8978610c739b6d6798` in
`stdio-torek-evaluation.md`. The earlier textual report's measurements belong
to `728677b8ff4330decf579b8ea03693ee904bb592`. Neither measurement is rewritten
as a current result or a universal lower bound. Reopen replacement only for
a concrete unmet guarantee or a materially different measured prototype.

The remaining terminal, compiler, utility, networking and date queue is
provisional. Trace terminal behavior across getty, login, application setup
and the active driver. Compare compiler changes to the compiler actually
built. Separate common utility correctness from default-image inclusion.
Establish network implementation maintenance/build/shipping before assigning
network-only dispositions. Rewritten implementations need contract comparison.

Common libc, utility and kernel-algorithm changes are candidates for common
source. Compare `chettrick/discobsd/master` directly before proposing upstream
submission. Flash policy, target shutdown integration and resource choices
belong to RP2040; PDP-11 assembly/controllers/overlays belong to native or
isolated legacy work. Measure multiplicative stream, inode and static-libc
costs against the 144 KB process window and packed-image budgets. X44's VM
and vnode design supplies research material, not free architectural capacity.

Keep donor attribution and applicable notices on adaptations. New original
RP2040 files retain NOTICE section 1's ISC requirement; that convention does
not relabel imported material.

## Replay and verifier limits

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-analysis
${PYTHON} tools/analysis/bsd211_semantic_ledger.py --summary
```

The host-tier verifier enforces the declared key set, unique dispositions,
pinned recipient witnesses, selected execution/source hashes, regression
sources, actual invocation/owner/width/PASS joins, raw-hash qualifications,
dependency constraints and open next actions. Its summary projection must
match the canonical JSON. Missing Git objects are infrastructure ERROR,
distinct from a rejected ledger. Mutation controls reject dropped rows,
receipts or witnesses, wrong widths/owners, SKIP, cycles, invented whole-series
closure and changed summary dispositions.

The verifier checks provenance joins and lexical symbol presence. It cannot
prove semantic equivalence, inspect unavailable raw patches, establish
production reachability, rerun historical executables or measure hardware.
Future ports still require a missing invariant, exact recipient implementation,
executed regression with a designated owner, and measured resource effects.
