# Flash telemetry, interruption recovery and wear policy

## Evidence-first decision

No checkpoint batching, write-back acknowledgement, compressed-flash eviction
or RAM-staged rewrite policy changes before the write and erase request trace
and the persistent states produced by interruption are established. Policy
follows measured amplification and a recovery contract.

Dhara geometry and expected amplification are in
`../flash-storage-expectation-plan.md`; the larger memory and durability
program is `../rp2040-memory-wear-engineering-program.md`; processor-reset
cells of the checkpoint driver are in `../flash-reset-recovery-cells.md`. This
sheet fixes the next evidence package and its stop conditions.

## What exists and what it proves

With `STORAGE_STATS=yes` (default `no`; see `sys/arch/rp2040/doc/STORAGE.md`),
`machdep.storage_stats` exposes saturating 32-bit counters (`sys/sys/storage_stats.h`,
version 2): root and swap reads and writes, requested program pages and bytes,
requested erase sectors and bytes, map writes, checkpoint attempts, successes
and errors, dirty-buffer activity and the SwapRAM admission probe.
`flash_erase()` and `flash_program()` in `sys/arch/rp2040/dev/flash.c` count
the request after the write-admission check and before the ROM call.

The counters give software-request equivalents for a paired workload. They do
not give the chip's internal command count, electrical completion,
per-physical-sector wear, retention or a successful operation after power was
lost. A saturated snapshot cannot produce an exact delta. Checkpoint counters
describe `dhara_map_sync()` results, not whole-filesystem durability.

## Ordered evidence package

### 1. Repeatable telemetry capture

One diagnostic image (PICO_DIAG with `STORAGE_STATS=yes`) is recorded with its
commit, kernel hash, configuration, root image hash and board identity. An
unsaturated before and after snapshot brackets each workload:

- clean idle for 300 seconds, with update running and stopped;
- one aligned 1 KiB overwrite with an explicit `fsync`;
- the same overwrite followed by 60 seconds idle;
- a 20 KiB `cp`, a multi-file native `cc` and a swap-heavy process cycle;
- 20 adjacent logical writes as individual requests, then the closest
  user-visible batched equivalent available without a policy change.

Deltas are reported as physical equivalents (256-byte programs, 4 KiB erases)
and as Dhara pages and blocks. Paired controls attribute the counter reads'
own process and swap cost. A run is rejected on saturation, reset, workload
error, image mismatch or unexplained background traffic. This package has not
run; no board measurement stands behind it.

### 2. Deterministic host interruption oracle

`tests/rp2040/dhara_amplification/check-recovery.sh` (part of
`check-dhara-amplification`) runs the production journal and map over the
calibrated NOR traffic model. A full map holds 989 sentinel sectors. Sixteen
overwrite-and-sync requests exercise collection. The transaction is cut at
each of 464 program, erase and acknowledgement boundaries; every replay
discards the map and page buffer, resumes, and checks every logical sector.
All 464 complete-operation cuts keep the acknowledged-new,
unacknowledged-old-or-new and unchanged-sentinel contracts. Early
acknowledgement and a single flipped byte fail the recovered-payload oracle.
Omitting a metadata page is rejected during the next collection write with
`DHARA_E_RECOVER` (dhara error 7); the workload does not silently complete.

The partial-prefix run programs half of a 1 KiB page or erases half of an
8 KiB block before the cut. At `d81b1788` it fails at cut 26 of 464 with a
mixed sector before the first acknowledgement, and the script reports
`LIMIT`. Production NOR reads copy XIP bytes without ECC, so a successful map
resume alone establishes no payload integrity. Against the journal of branch
`rp2040/dhara-torn-checkpoint` (`30fed2a1`, checkpoint acceptance requires a
whole sector tree) the same run passes, so the oracle discriminates the
repair; that branch is not on `main`. The result is a model counterexample,
not a frequency on silicon. Copy operations aggregate the production adapter's
256-byte subprograms; arbitrary electrical corruption, filesystem mounting and
board power cuts remain outside the test.

`check-flash-reset-cells` covers the processor-reset cells of the checkpoint
driver (every chip command and byte prefix of one page program or sector
erase); the two oracles overlap in the question but differ in operation scope
and in model, and neither replaces the other.

The deciding next evidence is a byte and phase capture from the production
adapter under controlled interruption. The alternatives are explicit payload
integrity with a recovery fallback, a validated atomic-program assumption or
a weaker documented durability contract. Choosing a persistent
representation or weakening acknowledgement semantics is an architecture
decision. The passing atomic-operation matrix supports no batching or
write-back change.

The conservative allowed-state contract: recovery may expose the last
completed checkpoint or a later completely valid checkpoint, and never a mixed
logical sector, an out-of-range map, silent unrelated-sector corruption or a
map that cannot be reopened. A reset after an acknowledgement retains it when
per-request sync makes the acknowledgement the durability boundary.

### 3. Board recovery evidence

Hardware interruption needs explicit opt-in. Watchdog resets at named flash
phases with retained phase and offset breadcrumbs come first; an external
power cut follows only with a reproducible jig and a disposable image. After
each reset the capture records boot status, the Dhara open result, a
filesystem check, the target file, unrelated sentinel files and telemetry. A
software reboot is a control, not power-loss evidence.

Sector-level endurance distribution is not claimed until physical erase
addresses are observed. Aggregate root and swap erase counts establish
amplification, not even wear.

## Policy admission

A candidate policy needs all of the following:

1. the host interruption matrix passes its stated acknowledgement contract;
2. paired board counters show which program or erase component it reduces;
3. every error path leaves retryable dirty state or reports failure;
4. shutdown, `fsync`, unmount and update semantics are explicit;
5. RAM, text, data/BSS, packed-root and latency deltas are recorded for every
   affected configuration;
6. the previous policy stays as a calibrated comparison.

Checkpoint grouping trades fewer programs for a larger volatile window. RAM
staging trades RAM and copy cycles for fewer ordinary rewrites. Compressed
eviction changes both program volume and recovery representation. They remain
separate experiments.
