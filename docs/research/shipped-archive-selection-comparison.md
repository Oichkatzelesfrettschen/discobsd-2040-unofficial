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
cross tier and firmware CI. Its 70 tests include 34 resource tests. The resource
controls reject new kernel members, non-libc members and whole archives with
zero numeric growth; malformed member evidence, missing groups, inconsistent
projections, missing closure binding and changed collectors also reject. Two
local collector mutations calibrate the internal-archive and path-ownership
controls by dropping non-libc selections and admitting parent-directory paths.
The known-good collector passes both deciding tests.

The clean parent at `5c5f1b26` supplies 36 closure groups and 68 selected archive
entries. The schema-2 collector successfully remeasured those artifacts.
Paired candidate resource acceptance and the logged aggregate remain the
integration gates for the unit. Runtime stack, heap, storage traffic and
hardware qualification retain their independent acceptance states.
