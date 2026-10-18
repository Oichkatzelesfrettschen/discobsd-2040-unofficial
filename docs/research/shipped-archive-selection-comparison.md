# Complete shipped archive-selection comparison

A static image can retain its size while selecting another archive member.
The original resource receipt kept packed-program libc selections and native
archive membership, while the dependency closure also retained kernel libgcc
and non-libc program archives. Comparing only the projections left those
additional dependencies outside the default review gate.

Schema 2 keeps `archive_selections` for every closure group: PICO, PICO_UART,
each packed program and each installed native archive or startup object. The
manifest defines the exact group denominator. Empty groups retain their rows;
a selected archive carries a nonempty map of member names to SHA-256 values.
The receipt also retains the complete closure digest.

Tree archives use `tree:` plus a normalized repository-relative path. External
archives use `external:` plus their basename and retained whole-archive digest.
The content identity keeps installation directories outside the portable key.
Colliding normalized identities reject rather than merging independent maps.
Member names and tree paths preserve their ownership boundaries.

Capture verifies each canonical group against its exact installed artifact
path and SHA-256 before running measurement tools. The kernel, packed-program
and native checks use the named owner throughout capture. A kernel artifact
found under a peer group cannot support that kernel's archive selection. The
attribution controls move kernel, packed-program and native artifacts across
groups and require rejection before measurement.

The existing libc and native member lists remain compatibility projections of
the complete table. Validation checks both projections against their owning
groups. Comparison reports added, removed and changed members for every
archive in every group. Added archives or selected members reject even when
every numeric resource delta is zero. Removed members remain visible; changed
bytes under an existing member name remain distinct from a new dependency.
The numeric constrained-surface policy continues to reject positive deltas.

Paired schema-2 reports require the same collector digest, measurement-tool
hashes and recorded toolchain identities. The collector records the source
identity of the artifacts independently from the collector source digest.
A new collector can remeasure preserved parent artifacts only while the
artifacts, manifest, image, closure and immutable distribution receipt still
match. Remeasure the parent before cleaning products for the candidate.

Schema-1 receipts retain their historical numeric and libc/native projection
claims. `validate` labels those records as historical. Complete `compare`
requires schema 2 on both inputs; the tool never invents missing selections
from an old receipt. The retained baseline JSON remains byte-identical.

`check-c17-tool-capture` owns the compiler/closure/resource test sequence in the
cross tier and firmware CI. Its 72 tests include 36 resource tests. The resource
controls reject new kernel members, non-libc members and whole archives with
zero numeric growth; malformed member evidence, missing groups, inconsistent
projections, missing closure binding and changed collectors also reject. Two
local collector mutations calibrate the internal-archive and path-ownership
controls by dropping non-libc selections and admitting parent-directory paths.
The known-good collector passes both deciding tests.

The clean parent at `5c5f1b260594b9509b04663836dc966f82691dbb` and candidate at
`fa25f1e30efc6ca901fc37883f6e671e8e52c5c6` each supply 36 closure groups and 68
selected archive entries. The schema-2 collector remeasured the preserved parent
artifacts before candidate cleaning. Both captures use collector SHA-256
`5a7b5a1073eb2caf445bfed4bd4c556879bd44e1cf27482b6df46ebe528b3e1f`.
The paired comparison reports 154 zero numeric deltas and unchanged archive
identities, selected member names and selected member hashes in every group.
Both captures retain 2,459 compiler records with equal evaluated-command
multisets and matching compiler executable hashes. Clean distributions passed
in 312.6 seconds for the parent and 138.9 seconds for the candidate; elapsed
build time serves as execution context, rather than a performance claim.

The retained schema-2 parent receipt has SHA-256
`6ed88d5f9234fd80d4810e614e9efc41f5ea4637ee9a76192b68ccb120bbba75`;
the candidate receipt has SHA-256
`382f4a3f4a320ffd7c5ae4107ec1f8a5fd152e30133a5e9e2464f4956c3369c9`.
The comparison receipt has SHA-256
`c9922e8f2a37fc99ecfca6d809d3d2b7309ad6d45e8df05705d099894e9a8a13`.
Raw captures, both measured root images and the command comparison remain
retained with the unit's integration evidence. The logged aggregate remains
an integration gate. Runtime stack, heap, storage traffic and hardware
qualification retain their independent acceptance states.
