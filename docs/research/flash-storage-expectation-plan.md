# Flash storage expectations: verified findings and build-out plan

Status: proposal, revised after review. Drafted with assistant help from a
submitted storage expectation audit. Every claim was re-read against live
source at `316b1d7`; the audit's Graft map was stale (`dbd8bf0c`), and the
Graft MCP reader did not connect in the drafting session, so no claim rests
on a cached summary. Evidence classes follow `sys/arch/rp2040/doc/TESTING.md`:
"source" means read from the tree, "derived" means computed from source and
not yet executed, and every prediction names the gate that can falsify it.

## 1. What the audit gets right, and what changes the plan

The audit's frame holds: Dhara and raw swap are sound at the lowest layer,
and inherited UFS/userland policy decides how often they are exercised. Five
source findings change its priorities.

### F1 -- The Dhara unit is 1024 bytes, not 256

`FLASH_UNIT_BYTES` is `1024UL` and `FLASH_LOG2_UNIT` is 10
(`sys/arch/rp2040/dev/flash.h:52,55`). `flstrategy()` computes
`nsect = b_bcount / FLASH_UNIT_BYTES`, so one 1 KiB buffer is one
`dhara_map_write()` call. The audit's "four 256-byte `dhara_map_write()`
calls" is inherited from two stale statements that contradict the header:
`sys/arch/rp2040/doc/STORAGE.md:106` and `tools/flashimg/flashimg.c:26`
("writes every 256-byte sector"). STORAGE.md:166-168 states the correct
value, so the document contradicts itself. Programming one Dhara page is
four 256-byte program-page equivalents (`FLASH_PROG_BYTES`).

### F2 -- Checkpoint-group padding is the amplification hypothesis

Units. A *Dhara page* is 1024 bytes. A *program-page equivalent* is 256
bytes requested of `flash_program()`; the ROM's actual command count is not
established. A *Dhara block erase* is one logical 8 KiB erase, equal to two
4 KiB *sector equivalents*; the ROM may issue a different number of
physical erase commands (STORAGE.md, "Optional storage counters").

Geometry (source): Dhara block `FLASH_ERASE_BYTES` = 8192 B, so
`log2_ppb = 3`. `choose_ppc()` (`dhara/journal.c:142`) picks the largest
`ppc <= log2_ppb` with `(2^ppc - 1) * 132 + 20 <= 1024`; `7 * 132 + 20 = 944`
fits, so `log2_ppc = 3`. One checkpoint group is the whole Dhara block:
seven user slots and one metadata page.

Mechanism (source): `dhara_map_sync()` (`dhara/map.c:481`) loops while the
journal is dirty. Each iteration either garbage-collects the tail (`raw_gc`,
a page copy when the page is live, no program when it is garbage) or calls
`pad_queue()`, which re-copies the root page. The dirty flag clears only in
`push_meta()` when the group's metadata page is programmed. `fl_sync()` runs
this after every write request (`flash.c:777-782`).

Expectation (derived), for an aligned, successful, non-recovery trace below
the `auto_gc()` threshold: one isolated 1 KiB write request programs 8 Dhara
pages (1 user, 6 GC or pad copies, 1 metadata), which is 32 program-page
equivalents and one Dhara block consumed. For `N` requests followed by one
sync, the idealized no-GC count, final padding included, is

    P(N) = 8 * ceil(N / 7)

Twenty requests predict `P(20) = 24` Dhara page programs against
`20 * 8 = 160` with a sync per request, about 6.67 times fewer. Filesystem
metadata, synchronous allocation writes, `auto_gc()` (up to
`FLASH_GC_RATIO + 1 = 5` collection steps per write once the journal reaches
map capacity, `map.c:303`) and failure recovery all perturb the count, so
the ratio does not transfer to `cp` or `cc` without measurement.

### F3 -- `sync()` excluded clean-superblock mounts (repaired)

`sync()` (`sys/kern/ufs_subr.c`) skipped a mount when `fs_fmod == 0`,
before `ufs_sync()` could inspect it. `fs_fmod` is set only by the
allocator, free paths and mount (`ufs_alloc.c`, `ufs_mount.c:236`); an
in-place overwrite takes `bdwrite()` (`sys_inode.c:299`) and marks the inode
`IUPD|ICHG` without setting it. update(8) therefore never flushed that data
or inode; it reached flash only on eviction (`NBUF = 4`), a later
allocation or free, `fsync`, an `IO_SYNC` write, unmount or shutdown
(`rp2040/shutdown_sync.c:66` forces `fs_fmod = 1`).

The audit's phrase "preserve the 30-second durability bound" was wrong:
update(8) provides a scheduling cadence, and the filter kept that cadence
from covering overwrites. Within `ufs_sync()` the separation already
existed: `syncinodes()` and `bflush()` run before the conditional superblock
write. P1 below removes the filter and is implemented on this branch.

### F4 -- `NBUF = 4` constrains, but does not remove, UFS coalescing

Delaying a buffer does not reduce the checkpoint cost of a request that
eventually reaches the driver, but repeated overwrites that merge in the
cache eliminate requests, and their checkpoints, entirely. Four buffers
bound the opportunity, and allocation metadata, buffer age (`B_AGE`) and
busy buffers affect retention. `STORAGE_DIRTY_REWRITES` counts merges under
the current policy; it cannot count the full-block merges the current
`bawrite()` path prevents.

### F5 -- Smaller corrections

| Audit claim | Live source | Effect on plan |
| --- | --- | --- |
| tar default at `tar.c:235` | `magtape[] = "/dev/rmt8"` at 314, digit rewrite at 438, `MTIOCGET` probe in `backtape()` at 2513; the tape path is gated by `#ifndef __APPLE__`, a platform proxy for a capability | Replace the proxy with a capability macro |
| Only one tape comment in reads | `sys_inode.c:261-265` zeroes a buffer when `b_resid == DEV_BSIZE`; `flstrategy()` sets `b_resid = b_bcount` for a read exactly at the partition end (`flash.c:715-719`) | Reachable on flash: keep, rewrite as the end-of-device invariant |
| Removing `d_flags` saves storage | Two maintained `bdevsw` tables (rp2040, stm32) and legacy pic32 use positional initializers; no driver sets `B_TAPE` | Repurpose the field at its width; savings are tens of bytes |
| update swaps wear raw flash | Raw swap allocates with a cyclic next-fit cursor (`subr_rmap.c:271`) and SwapRAM sits in front | Wear spreads; the cost is swap latency and program/erase volume, to be measured |
| MBR removal recovers a block | True (one of 989); block 0 is written once at image build | Defer (section 4) |

## 2. Dependency-ordered plan

Each phase is one PR, and every commit builds and bisects. P1 lands first;
the host oracle and documentation corrections proceed alongside it; device
flush semantics are defined before P3 is implemented; P4 follows P3. Board
measurements qualify performance and persistence claims and do not gate
P1. P5-P7 are independent.

```
P1 sync coverage (this branch)
P0a host oracle + doc fix (alongside)
        |
P2 device capability + flush contract --> P3 group commit --> P4 dirty-gated update
P0b board baseline (qualifies P1, P3, P3b, P4)      P3b UFS write policy (paired experiment)
P5 tar   P6 artifacts   P7 comments/C17   (independent)
```

### P1 -- `sync()` covers clean-superblock mounts (implemented)

Change: `sync()` calls `ufs_sync()` for every mounted filesystem whose
superblock lists are unlocked; `fs_fmod` gates only the superblock write
inside `ufs_sync()`. A clean filesystem costs an `NINODE` scan and a
free-list walk with no device request.

Contract, as narrow as the implementation:

- Successful writeback of the eligible dirty state: unlocked inodes with
  `i_count > 0` and `IMOD|IACC|IUPD|ICHG`, and delayed buffers on the free
  lists for the mount's device.
- Skipped objects stay dirty and eligible for the next call: inodes
  `syncinodes()` finds `ILOCKED`, buffers `bflush()` finds busy (off the
  free lists), and a whole mount while `fs_ilock` or `fs_flock` is held.
- Error state is retained: a failed inode update stays dirty
  (`iupdat()` leaves its flags, and the release in `irele()` folds them into
  `IMOD` through `ITIMES()`), `bflush()` still runs, the superblock write
  is withheld, and device write errors latch in `m_write_error` through
  `biodone()`. `sync()` returns nothing; `ufs_sync()` reports the latched
  error to `fsync`, `IO_SYNC` writes and unmount.

Gate: `check-storage-correctness` (host). The fixture failed 11 checks
against the unfixed source and passes after the change. Cases: a
clean-superblock mount with dirty data, dirty metadata, both and neither; a
modified superblock; a locked inode; `fs_ilock` and `fs_flock`; a failed
inode update followed by a retry; a latched `m_write_error`; a clean
read-only mount; `MNT_ASYNC` restoration. Three mutations calibrate it:
restoring the `fs_fmod == 0` skip, visiting a mount with one superblock
list locked, and an `ITIMES()` that drops the retry state. Busy buffers lie outside the stubbed buffer cache; a fixture
linking `ufs_bio.c` would cover them.

Predicted board movement (P0b): an in-place overwrite followed by idle
reaches flash at the next update tick instead of never; clean idle intervals
are unchanged.

### P0a -- Host Dhara amplification oracle and documentation repair

1. `check-dhara-amplification` (host). Link the vendored
   `sys/arch/rp2040/dhara` sources against a counting NAND model derived
   from `tools/flashimg`, with geometry and GC ratio from `dev/flash.h`.
   Traces: (a) `N` isolated write+sync from an aligned journal, (b) `N`
   writes then one sync, (c) both at the `auto_gc()` threshold. Assert
   `8N` and `P(N)` Dhara page programs for (a) and (b) and report (c).
   Calibrate with a known-bad model that forces `log2_ppc = 1` and a variant
   that omits the sync loop; both must fail.
2. Correct `STORAGE.md:106` and `flashimg.c:26`. The class gate is a
   geometry check, not a prose lexer: the oracle prints the geometry it
   derived (`FLASH_UNIT_BYTES`, `log2_ppb`, `log2_ppc`, group size), and a
   test compares those values with a small machine-readable geometry table
   that STORAGE.md cites. A lexical rule around "Dhara page" would reject
   legitimate historical comparisons and physical-page explanations.

### P0b -- Board baseline (hardware, explicit opt-in)

`STORAGE_STATS=yes` kernel, paired captures per STORAGE.md: idle 300 s at a
prompt with update running and killed; a multi-file `cc`; a 20 KiB `cp`; a
100-byte overwrite followed by 60 s idle, before and after P1. Paired
differences cancel the capture's own process cost. Predictions: about 32
program-page equivalents per isolated root write request; idle swap or
SwapRAM traffic attributable to update about twice per 30 s while a process
is resident; the overwrite reaching flash within one tick after P1. A
deviation is a stop point.

### P2 -- Device capability field and flush contract

Remove `B_TAPE` (`buf.h:190`) and its `bdwrite()` branch (`ufs_bio.c:139`).
Keep `bdevsw.d_flags` at its width as a capability mask, for example
`D_INLINE` (strategy completes before return) and `D_WRITEBACK`
(acknowledged writes may be uncommitted until an explicit flush). Express
the flush as a kernel-internal `d_ioctl` command so `struct bdevsw` keeps its
size. Define the flush contract here, before P3 uses it: success means a
completed Dhara checkpoint; failure returns an errno, latches the mount's
`m_write_error`, and leaves the device dirty so the next flush retries.
Measure `unix.elf` text and rodata for PICO and PICO_UART before and after.

### P3 -- Dhara group commit in `flstrategy()`

This executes contract A04 of `rp2040-memory-wear-engineering-program.md`.

Policy. Writes that may defer return after `dhara_map_write()` without
`fl_sync()`; writes that must be durable call `fl_sync()` before
`biodone()`, as now. A full group checkpoints itself in `push_meta()`. The
P2 flush calls `fl_sync()`; `sync()`, `fsync`, `IO_SYNC` writes, unmount and
`rp2040_shutdown_sync()` invoke it after `ufs_sync()`.

Selecting durability. `B_ASYNC` describes completion and release behavior
today; using it to choose durability is a new contract. Before
implementation, audit every write caller (`bwrite`, `bawrite`, `bdwrite`
eviction in `getnewbuf()`, `physio()`/`rawrw()`, `iupdat()`, directory and
allocation paths using `B_SYNC`, swap) and record which must commit before
completion. Ordering-sensitive synchronous writes keep their checkpoint.

Required behavior:

- The decisive case: clean UFS buffers with an uncommitted Dhara group
  must still checkpoint on `sync()` and `fsync`; P1's visit-every-mount
  selection plus a device-dirty query provides the trigger.
- Flush failure reaches `fsync()` and remains observable through
  `m_write_error`; device-dirty state survives a failed flush and triggers
  later service.
- Defined behavior for read-only remount (flush first, refuse on failure),
  device close and shutdown.
- Flush runs only in process context; a callout cannot flush, because flash
  programming disables XIP.

Crash semantics (source, to be tested). Dhara resumes at the last completed
checkpoint, and `dhara_journal_dequeue()` takes effect only at a checkpoint
(`journal.h:179-181`, `tail_sync` in `push_meta()`). The expected device
state after power loss is therefore one permitted by the completed
checkpoint history, automatic group checkpoints included. Bounds, stated
separately:

- The incomplete journal group holds at most six unpadded user pages; that
  is the normal device-level exposure, not the filesystem loss bound.
- Dirty UFS buffers and inodes remain additional volatile state.
- A prefix of device writes is not necessarily a prefix of application
  operations, and does not by itself guarantee a filesystem fsck accepts.

Tests. (1) The A04 decisive host case. (2) A failure-injection simulator
that interrupts inside program and erase operations (torn 256-byte
programs, partially erased blocks), not only between completed calls,
resumes the map and checks that recovered contents match a state permitted
by the completed checkpoint history. Checkpoint completion is defined as
the metadata page's program returning success; a torn metadata program
counts as not completed. (3) Flush-failure propagation and retry. (4)
`check-dhara-amplification` trace (b) as the default flash behavior.
Calibration: dropping the flush from `fsync` fails (1); erasing a block
before checkpoint fails (2); clearing device-dirty on failure fails (3).

Predicted board movement: a 20 KiB sequential write approaches `P(20) = 24`
Dhara page programs plus metadata and allocation writes, instead of about
160 plus the same; isolated synchronous writes are unchanged. Persistence
after power cut is a hardware claim that stays open until a cut test runs.

### P3b -- UFS full-block write policy (paired experiment)

Coalescing remains an independent lever (F4). Compare paired kernels, current
`bawrite()` against delayed full-block writes, on the same image and
workloads, reporting root write requests, program-page equivalents,
`STORAGE_EVICTION_WRITES` and `STORAGE_BUFFER_WAITS`. Where a direct merge
count is wanted, add a narrowly scoped observation of full-block reuse
before eviction. No outcome is predicted. Read-ahead (`breada()`) is
evaluated the same way: with inline completion it moves work rather than
overlapping it and occupies one of four buffers.

### P4 -- Dirty-gated update

| Design | Idle wakeups attributable to update | New obligation | Residual |
| --- | --- | --- | --- |
| A. Sync from proc 0 in `sched()` | none | Swapper sleeping in `getblk()`/`biowait()` stalls all swap-in | Reject without a deadlock proof |
| B. Flush on return to user mode after a deadline | none | `sync()` in whatever resident process returns from a trap | Nothing flushes if no process returns to user mode |
| C. update(8) sleeps in the kernel until dirty and deadline elapsed | none when clean | Synchronization proof below | One swap-in per dirty period |

C is preferred initially because `sync()` stays in an ordinary process and
introduces no filesystem sleep into the swapper. Its proof obligations:

- Atomic predicate check and sleep, with no lost wakeup between a
  clean-to-dirty transition and the sleep.
- A monotonic deadline set at the first clean-to-dirty transition;
  continued writes do not postpone it.
- Dirtying during a flush re-arms a new deadline rather than being lost.
- Signal interruption, daemon restart and flush failure leave the predicate
  and deadline consistent, with retry after failure.

Target: no idle wakeups or swap traffic attributable to update, not zero
system-wide swap traffic. A hard 30-second completion guarantee would also
require bounded scheduling and flash latency, which the system does not
provide. The primitive (system call or blocking sysctl) is an interface
decision for review.

### P5 -- tar defaults for a tapeless target

- Replace `#ifndef __APPLE__` with a `TAR_HAVE_MTIO` capability defined by
  the target's Makefile; rp2040 builds without it, so `backtape()` uses
  `lseek()` only and the digit options are rejected.
- Archive precedence: explicit `-f`, then the chosen environment policy
  (`$TAPE`, if adopted), then the target default `TAR_DEFAULT_ARCHIVE`
  (`-` on rp2040). Refuse to write an archive to a terminal; require a
  seekable regular file for `r` and `u`.
- Host and qemu-user tests for each precedence level, terminal refusal,
  append to a regular file and `r` on a pipe; report tar text and root
  free-block deltas.

### P6 -- Artifact names and release manifest

- Make `FSIMG` machine-overridable and name the RP2040 intermediate
  `rootfs.img`; stm32 keeps `sdcard.img`. Touch only maintained references.
- `installfs` fails with a message for `MACHINE=rp2040`; flashing stays
  behind `discobsd-flash` or explicit `picotool`.
- Release: distinct kernel artifacts, for example `PICO/unix.uf2` and
  `PICO_UART/unix.uf2` in separate directories or renamed
  `unix-pico.uf2` and `unix-pico-uart.uf2`; `flash.uf2`; `rootfs.img`
  labeled as an inspection artifact; README and a SHA-256 list, checked by a
  release-manifest gate.

### P7 -- Comments and C17, after the deletions

Rewrite rationale, keep behavior: `physio()` short-transfer termination
(`vm_swp.c:157,163`), end-of-device zeroing (`sys_inode.c:261-265`),
last-close invalidation for removable media (`sys_inode.c:419-421`).
Convert K&R definitions such as `rawrw()` to C17 prototypes in separate
formatting commits after P2.

## 3. Gates

| Gate | Class | Phase | Known-bad input | State |
| --- | --- | --- | --- | --- |
| `sync()` cases in `check-storage-correctness` | host | P1 | clean-superblock skip; one-list-locked visit | landed |
| `check-dhara-amplification` | host | P0a | `log2_ppc = 1`; no sync loop | proposed |
| Dhara geometry/documentation check | host | P0a | a table entry disagreeing with `flash.h` | proposed |
| Flush contract, failure injection, flush-error propagation | host | P2, P3 | flush dropped; early erase; dirty cleared on failure | proposed |
| update predicate/deadline state machine | host | P4 | deadline reset on each write; lost wakeup | proposed |
| tar precedence contracts | host, qemu-user | P5 | `/dev/rmt8` default | proposed |
| RP2040 release manifest | build | P6 | one `unix.uf2` overwriting the other | proposed |

Each gate is registered in TESTING.md, the root Makefile and the CI owner.
Board captures stay behind explicit opt-in and are reported separately.

## 4. Deferred or declined

- MBR removal: gains one 1 KiB block of 989 and some parsing text in
  `fl_setup()`. Block 0 is written once at image build, but GC can still
  relocate an unchanged logical sector, so its physical wear contribution
  is small rather than zero. Defer to any later on-flash format migration.
- `FLASH_GC_RATIO` is not stored in the journal (source: `dhara_map_init()`
  takes it as an argument); it sets `dhara_map_capacity()`'s reserve, so a
  change alters exposed capacity, which the image's partition sizes depend
  on (`flashimg -c`). Whether that is a persistent-format break or a
  capacity and policy change needs a resume test against an image written
  with the old ratio before it is bundled with a format migration.
- `mkfs`, `fdisk`: built, not installed; root cost already zero.
- Tape, floppy and dump manuals, `mtio.h`: retained history; excluded from
  RP2040 API claims.
- STM32 SD drivers: real storage, untouched.

## 5. Open questions and residuals

- F2 is derived from source; P0a is its first execution and P0b its first
  board observation.
- P1's busy-buffer skip is argued from `bflush()`'s free-list walk, not
  exercised by the stubbed fixture.
- Power-cut persistence for P1 and P3 is a hardware claim.
- The P3 caller audit may find writes whose durability class is ambiguous;
  those default to synchronous until reviewed.
