# DPUCP-2024 multicall and packed-root composition

The retained DPUCP-2024 measurement covers seven RP2040 multicall executables
with 80 hard-linked applet names. DPUCP-2024 treats that recorded set as a
finite composition surface rather than as a POSIX or UNIX conformance claim.
The machine-readable ledger in
`dpucp-2024-multicall-composition.json` binds the dispatch tables, manifest,
link maps, packed sizes, root arithmetic, and source identities used by the
measurement.

## Finite shipped surface

The retained root measurement contains 32 packed executable inodes and 84 hard
links.
Seven of those inodes are multicall boxes, and 80 hard links name their
applets. The verifier reads each box's `PROG`, `TOOLS`, `BUILTIN_TOOLS`, and
`ALIASES` values through bmake and compares the resulting names with
`distrib/rp2040/mi.rp2040`. Built-in applets and aliases own no standalone
code image, so the retained standalone comparison includes the 71 primary applet
executables that have a separately linked form.

| Executable | Shipped packed blocks | Primary standalone packed blocks | Saved blocks |
| --- | ---: | ---: | ---: |
| box | 29 | 134 | 105 |
| sysbox | 27 | 68 | 41 |
| adminbox | 25 | 53 | 28 |
| textbox | 33 | 156 | 123 |
| utilbox | 45 | 162 | 117 |
| grepbox | 11 | 18 | 7 |
| gamebox | 12 | 24 | 12 |
| **Total** | **182** | **615** | **433** |

The block comparison measures complete packed a.out images in 1,024-byte root
blocks. `dpucp-2024-multicall-sizes.tsv` retains all seven multicall rows and
all 71 primary standalone rows. The verifier binds the TSV digest, checks every
row against the ledger, and runs `hsaout -s` over the current artifacts during
the live-map gate. The ledger records the exact hsaout command pattern,
captured host-tool digest, ARM compiler version, ARM linker version, and bmake
version. Exact replay applies when the live ARM compiler, ARM linker, and
bmake versions match the recorded versions and every retained source digest
matches the live file. Exact replay compares live box membership, manifest
counts, archive members, byte and block measurements, standalone comparison,
and root counters with the ledger.

A toolchain difference or any changed retained source selects live mode. The
gate prints the changed source paths and reports a toolchain difference when
present. The gate discovers every Makefile that includes
`share/mk/multicall-bss-overlay.mk` and reads `BINDIR` and `PROG` through
bmake. In both modes, each installed path must equal `BINDIR/PROG`, its
basename must equal `PROG`, and the path must be packed in the live manifest.
The manifest's packed hard-link targets must equal those installed paths
after excluding the explicitly declared non-multicall targets
`/usr/bin/compress` and `/usr/bin/stevie`. A new target requires a declaration
or a discovered box; a box without manifest links fails the join.

Live mode reads `TOOLS`, `BUILTIN_TOOLS`, `ALIASES`, and `UNSHIPPED_TOOLS`
from each discovered box Makefile. Dispatch names must be unique within and
across boxes. Every hard link targeting a box must name a dispatch entry,
each shipped basename must belong to one box, and the dispatch names absent
from shipped basenames must equal `UNSHIPPED_TOOLS`. Every box declares an
empty `UNSHIPPED_TOOLS` list in the measured tree. The archive consumer set
uses the same discovered boxes plus the separately linked `/bin/sh`.

The `--list-boxes` command prints the discovered box directories, one per
line, for the root Makefile's sequential relink loop. The loop deletes each
map before relinking, requires a nonempty new map, and timestamps the map
after successful a.out conversion. The checker rejects maps older than the
binaries they describe and requires `${LINK_MAP_FLAG}` in recipe lines;
comments cannot satisfy the recipe check. The shell follows the same map
deletion and publication rules.

The gate derives standalone paths using the applet directory search in the
box Makefiles, requires the resolved directory to contain a Makefile,
builds each live primary tool through its directory's `all`
target, and runs `hsaout -s` over each box and every live primary standalone
image, including tools absent from the retained TSV. Each live box must use
strictly fewer packed blocks than the sum of its live primary standalone
tools. Built-in tools and aliases remain outside the standalone sum.
The live-mode pass line prints the discovered box count, shipped-name count,
and measured root free blocks beside the retained values. Exact replay
prints the retained denominator.

The JSON ledger stays the retained evidence of its recorded commits. Both
modes validate its structure, inventory digests, receipt hashes, internal
arithmetic, and retained source-blob identity. Live mode validates the
ledger's membership and denominator counts against its recorded manifest
blob, while live acceptance uses the live Makefiles and manifest. The ledger's
recorded membership, sizes, counts, standalone comparison, and root counters
do not constrain the changed live tree. Host binary digests remain capture
identities because another supported host compiler may produce a different
executable. The comparison measures complete packed a.out images rather than
inferring savings from source line counts or object sizes.

## Static archive closure

The optional `LINK_MAP` seam leaves normal links unchanged and emits a GNU ld
map only when the caller names a destination. The composition gate relinks the
seven boxes and `/bin/sh`, then compares the maps with the committed archive
inventory when exact replay applies. Source or toolchain differences can
change selected libc helpers. Live mode parses every map, rejects an empty
member set, and reports each added or removed member by consumer. The retained
inventory remains evidence for the recorded sources and toolchain. The measured
eight-consumer closure has 669 consumer edges across 192 distinct libc members: 130 members serve multiple consumers
and 62 serve one consumer. Per-consumer counts are 116 for adminbox, 112 for
box, 37 for gamebox, 46 for grepbox, 57 for sh, 101 for sysbox, 87 for textbox,
and 113 for utilbox.

Static linking shares one selected archive member inside one executable. It
does not share that member across separate process images. Equal private symbol
names in localized applet objects also do not prove equal behavior, state
ownership, or lifetime. The measured result therefore retains seven process
images and the existing BSS overlays instead of admitting unproved
cross-applet or cross-process folding.

## Packed-root delta

Two clean distributions isolate the `/usr/bin/logname` manifest addition.
Both roots contain 95 files, 692 data blocks, and 287 free blocks. The
candidate contains 84 links instead of 83. The hard link points at the existing
utilbox inode and consumes zero additional data blocks or inodes.

The base and candidate receipt files retain the source commit, command,
terminal result, manifest digest and link count, image digest, partition
geometry, inode use, and root block counts. The composition verifier hashes
each receipt, compares every summarized terminal field with the retained
payload, derives used inodes from the retained image counters, binds candidate
links to the recorded manifest blob, and rejects positive retained candidate
data-block or inode growth. The live gate also reads the current candidate
image through fsutil. Exact replay requires every retained root counter. Live
mode requires the recorded partition and inode-list geometry, consistent root
block arithmetic, and positive free block and inode capacity. The retained
receipts remain the zero-growth evidence for their recorded commits.

The DPUCP `logname` row remains `partial`, with host evidence from
`check-id-aliases` and composition evidence from this gate. Packed-image
execution must still observe session identity, operand rejection, the
missing-login diagnostic, and exit status before the residual can close.

## Verification

Run a logged clean RP2040 build before a directly selected composition gate
because libc and public header changes must reach every linked program. The
gate declares `sdcard.img` as a prerequisite and builds the image from the
installed tree before reading it. `logged-check` supplies the same build-then-
check order automatically:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-multicall-composition
```

The cross graph separates the three composition gates with `.WAIT` boundaries
because every gate cleans or relinks shared products. The composition recipe
runs the shell clean and link in separate recursive makes and relinks the other
consumers sequentially. It then validates the source identities, dispatch and
manifest denominators, archive inventory and live deltas, per-program image
measurements, retained receipts, and root-image accounting. Nineteen document
mutations and seven Makefile structure controls reject conformance claims,
denominator drift, unmeasured sizes, manifest and root drift, missing or forged
receipts, source identity drift, stale link maps, and unsafe build ordering.
Document-level calibration also rejects byte drift when both sources and
toolchain match, accepts source drift with an added live tool and changed
measurements, and rejects live manifest names outside dispatch, loss of strict
packed-block savings, a missing link-map seam, and zero free root blocks.
Additional live fixtures reject an unlisted packed target, a box without a
manifest entry, an undeclared unshipped dispatch name, a `PROG`/installed-path
mismatch, a comment-only link-map flag with a stale map, a stale map with a
valid recipe, overlapping dispatch sets, and a `program2` directory without
a Makefile. An added-box fixture verifies the expanded denominator and
archive consumers, then rejects that box's empty map. Positive controls
also exercise a changed `BINDIR` with matching manifest paths and an explicit
unshipped declaration.
`check-dpucp-profile` separately rejects removal or weakening of the `logname`
compatibility binding.

Runtime peak stack and heap remain unmeasured. Renode execution remains a
separate evidence class. Physical-board execution remains unrun and requires
explicit authorization immediately before flash or serial access.

The `retained_source_commit` field identifies the Git blobs that match all 17
recorded source hashes. The shared retained-source validator verifies those
bytes and reports live differences separately. The live-map gate still
evaluates dispatch variables, manifest membership, selected archive members,
packed sizes, block savings and root capacity. Historical source identity and
current composition acceptance remain separate claims.
