# Bounded storage recovery comparison

The executable comparison applies undo, redo and copy-on-write publication to
the same transaction produced by the imported V7 filesystem: create one empty
subdirectory beside one empty file in a 128-block image. The existing
compensation path supplies the format-preserving comparison. The transaction
changes 32 persistent byte positions. That denominator measures changed
metadata bytes, rather than application payload or requested full-block bytes.

`tests/storage_recovery/transaction_models.py` implements two reserved
transaction slots, each admitting eight changed 512-byte blocks. The medium
header and slots begin at 1,024-byte boundaries. A frame records its generation,
length, predecessor/result image hashes, ordered block addresses and payload,
followed by a SHA-256 digest. A separately written publication byte identifies
prepared, committed or aborted state. Recovery checks record identity, bounds,
generation order, predecessor identity and integrity before consuming payloads.

| Mechanism | Publication and recovery |
| --- | --- |
| Existing compensation | Mutate ordinary filesystem records and compensate reported failures. The prefix matrix checks restoration of ownership, namespace, inode attributes and allocation counts. Interrupted execution retains an inspection/repair boundary. |
| Undo | Persist and validate original blocks, publish prepared state, replace home blocks, then commit. A prepared transaction restores original blocks and publishes aborted state after successful restoration. Repeating interrupted restoration is idempotent. |
| Redo | Persist and validate new blocks, then commit before updating home blocks. Recovery replays committed generations in order. Replaying the same generation again preserves the resulting image. |
| Copy-on-write | Persist immutable new block extents and publish their validated delta root. Recovery overlays committed generations on the immutable base. Recovery writes zero persistent bytes in the bounded model. |

The models admit ordered, persistent byte-prefix writes and a publication byte
whose interrupted transfer is absent or complete. The logical medium permits
rewrites. Those assumptions require a separately admitted mapping onto flash;
the model does not equate a CPU store or byte marker with a physical atomic
program. SHA-256 detects the exercised corruptions under its collision-resistance
assumption. A damaged committed record reports unavailable recovery; detecting
damage does not establish a usable prior version that retains every
acknowledged transaction.

The cut matrix enumerates every byte prefix of every write in two sequential
transactions and every persistent recovery write. The second transaction must
preserve the acknowledged first transaction. Positive cases also reopen through
the independent V7 reader. Stale generations, wrong policy identity, corrupted
committed records, premature publication and capacity exhaustion supply explicit
negative controls. An early-publication mutant loses recovery when interruption
exposes its marker before its record. The complete record-prefix enumeration
also covers every truncation position in the exercised records.

## Operation-count comparison

`compare_policies.py` captures logical reads and writes, then replays them
through the existing NOR model and production Dhara journal/map. Each touched
1,024-byte logical unit is read, modified, written and checkpointed synchronously.
The initial map holds all 989 logical sectors; unrelated sectors carry
sentinels. The final inspection reopens the map and checks every sector.
Initialization and final inspection reads stay outside the operation counters.
The adapter supplies one explicit ordering policy rather than modeling every
possible buffer-cache coalescing schedule.
The NOR model aggregates a complete 1,024-byte page program/copy; production
copy's 256-byte subprogram interruption positions remain a separate requirement.

The captured full-map workload reports:

| Mechanism | 256-byte program equivalents | 4,096-byte erase equivalents | Checkpoints | Reserved metadata bytes |
| --- | ---: | ---: | ---: | ---: |
| Compensation | 256 | 16 | 8 | 0 |
| Undo | 288 | 18 | 9 | 11,264 |
| Redo | 256 | 16 | 8 | 11,264 |
| Copy-on-write | 128 | 8 | 4 | 11,264 |

These are software-model request equivalents under the named occupancy,
geometry, workload and synchronization policy. The counts establish neither
electrical completion nor sector wear distribution. The structured comparison
records programmed and erased bytes separately over the 32-byte change
denominator. The raw V7 request trace and the coalesced transaction records
represent different implementation policies over the same before/after image;
their costs deliberately include their respective repeated writes.

The Python models materialize complete images and retain traces. Their measured
Python allocation peaks describe that host implementation; the C NOR model's
simulated flash array is additional host memory. Those measurements establish
neither an RP2040 working set nor target stack peaks. The peak counts new Python
allocations during construction and execution; input images and C allocations
remain outside that measured surface. Firmware policy code is
absent, so loaded firmware policy bytes remain unmeasured. The existing Dhara
map and source geometry supply the replay implementation.

The committed-state cold-recovery replay additionally includes map resumption
and each policy's recovery reads/writes. Undo and copy-on-write request zero
programs in that state. Redo requests another 128 program-page equivalents,
eight erase-sector equivalents and four checkpoints because retained committed
records replay even after successful home writes. Repeated replay preserves
bytes while still producing flash traffic. An applied-record or reclamation
design requires its own interruption contract; the bounded model makes that
cost visible instead of assuming retirement.

Latency needs measured coefficients from a matching board capture:
`T = Np*tp + Ne*te + sum(Nr[k]*tr[k]) + Tcpu`, with read classes separated by
length/cache conditions when necessary. The comparison leaves those coefficients
unmeasured. It adds neither overlapping savings nor unsupported timing constants.

## Admission and replay

Production atomic recovery remains at the architecture stop point. The bounded
slots exhaust after two admitted or aborted transactions. Recycling, durable
capacity reclamation, representation migration, target memory/code costs,
publication guarantees and the production adapter's interruption behavior
remain deciding requirements. Copy-on-write's smaller count for one workload
does not admit a production format or establish a general wear improvement.
The mixed-sector counterexample retained on pull request 435 remains separately
attached to that branch and failure model.

Select and export `PYTHON`, then run:

```sh
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-storage-recovery
bmake MACHINE=rp2040 check-storage-recovery-models
bmake MACHINE=rp2040 check-storage-recovery-prefix
```

The ordinary host tier runs the calibrated interface, image, compensation,
transaction and Dhara-adapter tests. The prefix target runs both byte orders and
all three parent sizes by default. A caller can explicitly select
`STORAGE_PREFIX_PARENT_BLOCKS=1` for the smaller CI matrix; that receipt covers
only the selected parent size. The structured comparison's source input hashes
bind the exercised sources and headers. Formatter timestamps can change image
hashes between captures; the command compares a transaction against its own
recorded before/after image.
