# Flash telemetry, interruption recovery and wear policy

## Evidence-first decision

No checkpoint batching, write-back acknowledgement, compressed-flash eviction
or RAM-staged rewrite policy changes in this tranche. First establish the
write/erase request trace and the persistent states produced by interruption.
Only then choose a policy against measured amplification and a recovery
contract.

The detailed Dhara geometry and expected amplification remain in
`../flash-storage-expectation-plan.md`; the larger memory and durability
program remains in `../rp2040-memory-wear-engineering-program.md`. This sheet
defines the next evidence package and its stop conditions.

## What exists and what it proves

With `STORAGE_STATS=yes`, `machdep.storage_stats` exposes saturated 32-bit
counters for root and swap reads/writes, requested program pages/bytes,
requested erase sectors/bytes, map writes, checkpoint attempts/successes/errors
and buffer/dirty activity. `flash_erase()` and `flash_program()` increment the
program/erase counters immediately before calling the ROM operation.

Those counters establish software-request equivalents for a paired workload.
They do not establish chip-internal command count, electrical completion,
per-physical-sector wear, retention or a successful operation after power was
lost. A saturated snapshot cannot produce an exact delta. Checkpoint counters
describe `dhara_map_sync()` results, not whole-filesystem durability.

## Ordered evidence package

### 1. Repeatable telemetry capture

Use one diagnostic image and record its commit, kernel hash, config,
`STORAGE_STATS` setting, root image hash and board identity. Capture an
unsaturated before/after snapshot around each workload:

- clean idle for 300 seconds, both with update running and stopped;
- one aligned 1 KiB overwrite and an explicit `fsync`;
- the same overwrite followed by 60 seconds idle;
- 20 KiB `cp`, a multi-file native `cc`, and a swap-heavy process cycle;
- 20 adjacent logical writes as individual requests, then the closest
  user-visible batched equivalent available without a policy change.

Report deltas in both physical equivalents (256-byte programs, 4 KiB erases)
and Dhara pages/blocks. Attribute the counter reads' own process and swap cost
with paired controls. Reject the run on saturation, reset, workload error,
image mismatch or unexplained background traffic.

### 2. Deterministic host interruption oracle

Extend the production Dhara host model with numbered cut points immediately
before and after erase, page program, metadata program and checkpoint return.
For every cut point, discard volatile state, reopen the map, mount/read through
the production path and compare against the last acknowledged durable state.

The initial allowed-state contract is conservative: recovery may expose the
last completed checkpoint or a later completely valid checkpoint, but never a
mixed logical sector, an out-of-range map, silent unrelated-sector corruption
or a map that cannot be reopened. If current per-request sync makes a request
acknowledgement the durability boundary, a reset after that acknowledgement
must retain it. Record and minimize any counterexample before changing policy.

Calibrate the oracle with controls that skip the metadata program, acknowledge
before `dhara_map_sync()` and corrupt one programmed byte. Each control must be
rejected for the expected reason.

### 3. Board recovery evidence

Hardware interruption is explicit opt-in. Begin with watchdog resets at the
same named flash phases and retained phase/offset breadcrumbs, then use an
external power cut only with a reproducible jig and a disposable image. After
each reset, capture boot status, Dhara open result, filesystem check, the
target file, unrelated sentinel files and telemetry. A normal software reboot
is a control, not power-loss evidence.

Do not claim sector-level endurance distribution until physical erase
addresses are observed. Aggregate root/swap erase counts can establish
amplification, not even wear.

## Policy admission

A candidate policy needs all of the following:

1. the host interruption matrix passes its stated acknowledgement contract;
2. paired board counters show which program/erase component it reduces;
3. every error path leaves retryable dirty state or reports failure;
4. shutdown, `fsync`, unmount and update semantics are explicit;
5. RAM, text, data/BSS, packed-root and latency deltas are recorded for every
   affected configuration;
6. the previous policy remains a calibrated comparison.

Checkpoint grouping is likely to trade fewer programs for a larger volatile
window. RAM staging trades RAM and copy cycles for fewer ordinary rewrites.
Compressed eviction changes both program volume and recovery representation.
They therefore remain separate experiments rather than one wear patch.
