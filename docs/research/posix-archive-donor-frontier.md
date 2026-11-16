# C17/POSIX archive donor investigation frontier

The archive corpus extends the pinned BSD workspace with historical implementations, portable utility libraries, microkernel mechanisms and standards copies. DragonFlyBSD remains the preferred current implementation reference. Standards contracts and independent target witnesses decide correctness; agreement among related source copies supplies one ancestry lineage rather than independent corroboration.

`posix-archive-donor-frontier.json` owns the 13-root inventory and 23 selected investigations. The root names below are relative to a caller-selected `ARCHIVE_CORPUS`. The inventory observes 262,043 ripgrep-enumerated regular-file paths, excluding `.git` directories through the recorded ripgrep selector. Directory entries and filesystem objects omitted by that enumeration remain outside its counts. Pathset digests identify the sorted NUL-delimited path lists, including the `./` prefix; regular-file counts and byte totals describe the observation. Selected sources carry SHA-256 identities. Root-wide content hashes and complete semantic review remain open.

Git donors retain remote authority, development branch, full commit and file hash in `bsd-donor-inventory.json` and `dpucp-2024-donor-ledger.json`. Archive donors retain relative source path and file hash; packed sources additionally retain container hash, member path and member hash. A version string, directory label or archive timestamp never substitutes for those identities. The seL4 ZIP comment naming a commit is metadata; the container/member hashes authenticate the inspected local bytes.

| Corpus root | Regular files | Recorded bytes | Next investigation |
| --- | ---: | ---: | --- |
| `extracted/archives/bsd` | 53,456 | 458,639,685 | Compare 3BSD, 4.2, 4.3, 4.3RENO and Net2 libc lineage by source hash and semantic delta; prioritize sort, numeric conversion, stdio and ABI history. |
| `extracted/archives/att` | 56,275 | 341,236,007 | Compare V7 through System V Release 4 mechanisms and shared-libc lineage; separate per-file permission evidence, public contracts and historical behavior before independent implementation. |
| `extracted/archives/osf` | 10,408 | 437,823,472 | Audit exec arithmetic, generated request checks and usercopy error ownership; identify packed OSF source containers separately from extracted files. |
| `extracted/archives/xinu` | 2,542 | 10,493,179 | Audit first-fit allocation, semaphore transitions and callback typing; distinguish Sun3/68K/SPARC assumptions from portable invariants and resolve source permissions. |
| `extracted/collections/schily/schily-2024-03-21` | 5,540 | 5,205,769,444 | Inspect selected members of the content-pinned 2024-03-21 release and compare Schillix libschily ancestry; inspect SFIO closure before admitting memory-stream adapters. |
| `extracted/collections/mach` | 1,809 | 16,816,546 | Compare bounded interface definitions and generated serialization; derive flat-buffer contracts without assuming Mach port or out-of-line memory services. |
| `sources/sun/os/opensolaris/schillix/collection` | 53,436 | 1,101,287,916 | Audit libc/libschily/libast mechanisms, per-file licenses and full dependency closure; treat process-cwd emulation as an adversarial case for native directory-relative calls. |
| `sources/sun/os/sunos` | 27,473 | 254,236,505 | Inspect SunOS source and packaged artifacts separately; compare numeric and ABI lineage with pinned BSD sources and resolve each source grant. |
| `sources/sun/os/solaris` | 41,936 | 359,831,914 | Inspect Solaris source and packaged artifacts separately; distinguish historical restricted files from later openly licensed versions and compare error/locale contracts. |
| `archives/mach` | 7,362 | 1,497,439,815 | Deduplicate CMU and mirror paths by selected source hash; inspect MIG and libmach callback/varargs seams and packaged-source member identities. |
| `archives/minix` | 2 | 5,884,232 | Reconcile package README restrictions with per-file grants and top-level license; compare NetBSD-derived utility files to the existing pinned NetBSD donor before semantic review. |
| `archives/research` | 1,782 | 39,550,318 | Bind local SUS documents to the existing standards denominator; inspect research-Unix source/media by specific ABI or algorithm question, preserving edition identities. |
| `archives/sel4/SeL4-src` | 22 | 25,193,243 | Prioritize seL4 ownership admission and BSD-licensed ABI generation; compare package versions and verified configuration boundaries without transferring proof claims to Cortex-M0+. |

## Mechanisms and conflicts

The selected source observations support the following investigation priorities. Every candidate remains outside target resource acceptance; the JSON record supplies its exact path, hash, permission observation and deciding experiment.

| Mechanism | Useful invariant | Port conflict and deciding evidence |
| --- | --- | --- |
| seL4 ABI generation | Generate public widths and compile-time size assertions from one interface description | Reuse the design principle for DiscoBSD syscall/XIP records; ILP32 and Thumb-1 fixtures reject wrong widths, alignment and counts. The inspected generator carries a BSD 2-Clause header. |
| seL4 object admission | Check destination capacity, ownership, object size and budget before mutation | Kernel GPL notices remain distinct from the generator grant. Capability revocation and verified address-space assumptions do not transfer to the RP2040 process window; rejected-state snapshots test an independently specified target mechanism. |
| Historical V7 sorting | Equal-key grouping and smaller-side recursion coexist | Global comparator and element-size cells break nested-call isolation. A nested sort with different record widths becomes a negative control for per-call state. |
| BSD/AT&T/Sun numeric scanning | Separate digit grammar, overflow admission and continued digit consumption | Signed-minimum asymmetry, ctype domains and historical errno rules need explicit tests. Preserve DiscoBSD's four 16-bit limbs rather than replacing them with wide multiply/divide. |
| Schily/Schillix buffered line reading | Scan existing FILE buffer spans before refilling | Private buffer, lock and orientation fields enlarge closure. Inject realloc failure before publishing caller pointers and compare CPU work plus simultaneous allocation peaks. |
| Schily directory-relative emulation | Directory identity must remain stable throughout lookup | fchdir-based emulation changes process cwd and can fail during restoration. The native kernel design needs descriptor/namei ownership; emulation supplies counterexamples, not a truthful replacement. |
| SFIO memory-stream wrapper | Separate the public interface from its backing stream mechanism | The short fmemopen wrapper delegates storage, mode and lifetime semantics to a larger dependency graph; measure full linked closure before any footprint claim. |
| Mach/OSF generated interfaces | Widths and bounded arrays belong to an authoritative description | Optional TypeCheck blocks can remove checks; flat-buffer decoders need unconditional validated boundaries and malformed-message controls. Mach ports and out-of-line memory stay separate prerequisites. |
| Xinu allocation and synchronization | Conserve free extents and semaphore count/queue relationships | Caller-supplied free sizes, integerized pointers, unchecked rounding and local IRQ masking require target-specific correction. Local interrupt exclusion alone does not prove cross-core synchronization. |
| Xinu/Mach callback formatting | Separate formatting from output transport | Stack argument walking and foreign assembly do not establish C17 va_list/AAPCS behavior; typed callbacks and target-ABI tests decide the adaptation. |

## Source identity and reuse decisions

Per-file notices control source-import review. The inspected Schillix tree contains CDDL and Common Public License files; the historical Solaris and OSF trees also contain explicit proprietary notices. Directory membership supplies no uniform grant. Source can still identify a contract question or counterexample, while a code import needs resolved permission and compatibility. Independent implementation derives from the selected normative contract, target invariants and separately recorded experiments.

The MINIX container illustrates why package labels need inspection. Its README labels internal unpublished work and describes utility synchronization; its top-level license describes MINIX and excludes aggregated software. The inspected cat source names NetBSD ancestry and a separate Berkeley grant. Member inventory contains utilities and Git metadata rather than the expected kernel/libc source tree. The investigation resolves those conflicting records and compares each selected utility against the pinned NetBSD donor before considering an import. The README's future-date prose supplies no verified chronology.

The Schily collection is largely packed releases. The selected 2024-03-21 tarball is content-pinned; its member-specific analysis remains open. Schillix unpacked files carry identifiers later than the adjacent 2017 archive label, so archive-to-tree correspondence requires a member comparison. The OSF `OSCBKERNEL200` object is a tar archive despite residing under an extracted root. Xinu `lib/libx.a` is an ar library; `ar t` exposes object members such as `autostart.o` and `chprio.o`. File/container classification precedes binary analysis.

## Ordered investigation and implementation

1. Preserve the 13-root path inventory and all 20 BSD repository dispositions; record source or package identity for every selected leaf. Keep inaccessible objects and unresolved authorities visible.
2. Read member lists and selected payloads without executing imported programs. Classify source, object/archive, executable, filesystem/media and documentation by format rather than extension.
3. Group identical selected bytes and then shared ancestry. Compare meaningful source deltas; retain build switches, ABI assumptions and per-file grants.
4. Decompose each selected mechanism into declarations, domains, state transitions, ownership, errors, concurrency, build/caller/kernel dependencies and algorithmic bounds.
5. For binary-only leaves, identify architecture, sections, symbols, relocations and entry points, then recover call/data flow around the named mechanism. Bind every inference to artifact hash and offsets. Source/binary equivalence requires a separate comparison; source similarity alone does not establish equivalence.
6. Translate a useful invariant into an independent DiscoBSD experiment with a known-good and known-bad control. Record established prior art, port-specific composition and a falsifier for each claimed improvement.
7. Implement eligible mechanisms in the canonical C17 semantic migration queue, preserving caller closure and source licensing. Clean rebuilds and matched resource receipts precede integration.
8. Review remaining leaves after each integration. Archive inspection, C17 compilation, semantic coverage, native-input acceptance, emulator execution and authorized physical measurements retain separate states.

Existing libc, headers and XIP work continue in dependency order. The expanded sources enrich those work packages; broad archive inventory does not delay a caller-complete implementation whose required evidence already exists. Every unresolved root keeps the concrete next experiment in the JSON frontier.

## Reproduction

Local execution requires Git, ripgrep, and the caller-selected Python interpreter. The existing `check-dpucp-donor-ledger` host target runs both frontier validators and their calibrated controls. `.github/workflows/firmware.yml` owns the firmware aggregate that includes the host gate. The ordinary gate needs only checked-in records. An explicit external-corpus check reads the selected files and archive members; it neither extracts payloads nor runs imported code.

```sh
set -eu
: "${PYTHON:?select the intended interpreter}"
: "${ARCHIVE_CORPUS:?select the archive corpus root}"
export PYTHON
"$PYTHON" tools/check_archive_donor_frontier.py \
    --corpus "$ARCHIVE_CORPUS" --verify-pathsets
```

The optional command verifies source/container/member digests and replays pathset/count/size observations. Changed corpus contents produce a failed identity check and require a newly reviewed observation; the command never silently refreshes the retained record. The host fixtures reject 18 schema mutations and fourteen identity/inventory failures, including cross-member hash substitution, absent members, changed container bytes, renamed paths and changed size totals. Duplicate members, oversized payloads, tar symlink members and corpus-escaping source symlinks also reject; exact 8 MiB member fixtures pass.
