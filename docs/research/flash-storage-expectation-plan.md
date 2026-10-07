# Flash storage expectations: verified findings and build-out plan

Status: proposal. Drafted with assistant help from a submitted storage
expectation audit. Every claim below was re-read against live source at
`316b1d7`; the audit's Graft map was stale (`dbd8bf0c`), and the Graft MCP
reader did not connect in the drafting session, so no claim here rests on a
cached summary. Evidence classes follow `sys/arch/rp2040/doc/TESTING.md`:
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
("writes every 256-byte sector"). STORAGE.md:166-168 already states the
correct value, so the document contradicts itself. Programming one Dhara
page is four 256-byte NOR programs (`FLASH_PROG_BYTES`).

### F2 -- The cost driver is checkpoint-group padding, not the write count

Geometry (source): Dhara page 1024 B, Dhara block `FLASH_ERASE_BYTES` 8192 B,
so `log2_ppb = 3`. `choose_ppc()` (`dhara/journal.c:142`) picks the largest
`ppc <= log2_ppb` with `(2^ppc - 1) * 132 + 20 <= 1024`; `7 * 132 + 20 = 944`
fits, so `log2_ppc = 3`. One checkpoint group is the whole 8-page Dhara block:
seven user slots and one metadata page.

Mechanism (source): `dhara_map_sync()` (`dhara/map.c:481`) loops while the
journal is dirty. Each iteration either garbage-collects the tail
(`raw_gc`, a page copy when the page is live, nothing when it is garbage) or
calls `pad_queue()`, which re-copies the root page. The dirty flag clears only
in `push_meta()` when the group's metadata page is programmed
(`journal.c`, `j->flags &= ~DHARA_JOURNAL_F_DIRTY`). `fl_sync()` runs this
after every write request (`flash.c:777-782`).

Consequence (derived): from a checkpoint-aligned journal, one isolated 1 KiB
write request programs at least 8 Dhara pages (1 user + 6 GC/pad copies +
1 metadata) = 32 NOR 256-byte programs = 8 KiB, and consumes one Dhara block,
amortizing to one 8 KiB (two-sector) erase per request. When `auto_gc()`
(`map.c:303`) is active it adds up to `FLASH_GC_RATIO + 1 = 5` collection
steps before the user page; a request then fills 8 or 16 pages. Batching
`k <= 7` requests before one sync divides the per-request page cost by
roughly `k`.

This reorders the audit's top item. Converting full-block `bawrite()` to
`bdwrite()` does not change the per-request cost: every evicted delayed
buffer is still one request followed by one full-group sync. The lever is
when `flstrategy()` checkpoints.

### F3 -- `sync()` does not flush overwrite-in-place data

`sync()` (`sys/kern/ufs_subr.c:37`) skips a mount when `fs_fmod == 0`.
`fs_fmod` is set only by the allocator, free paths and mount
(`ufs_alloc.c:102,115,169,287,311`, `ufs_mount.c:236`); `rwip()` and inode
timestamp updates never set it. A partial-block overwrite of an allocated
block takes `bdwrite()` (`sys_inode.c:299`) and marks the inode `IUPD|ICHG`
without touching `fs_fmod`. update(8) therefore never flushes that data or
inode; it reaches flash only on eviction (`NBUF = 4`,
`include/machparam.h:53`), a later allocation or free on that filesystem,
`fsync`, unmount or shutdown (`rp2040/shutdown_sync.c:66` forces
`fs_fmod = 1`). TESTING.md:654-659 already relies on this skip as a
constraint on the allocator.

So the "30-second durability bound" the audit wants to preserve does not hold
for in-place overwrites today, and making full-block writes delayed before
fixing this would extend the unbounded window to full blocks. 4.4BSD's
`ffs_sync()` walks dirty inodes and buffers regardless of `fs_fmod` and uses
the flag only to gate the superblock write; that is the principle to
backport.

### F4 -- `NBUF = 4` bounds every UFS-level coalescing claim

With four buffers, delayed-write coalescing can only merge rewrites whose
reuse distance is under four distinct blocks. The `ld` scratch-file comment
at `sys_inode.c:289-294` describes a working set that usually exceeds that.
`STORAGE_DIRTY_REWRITES` already counts exactly the merges that would occur.

### F5 -- Smaller corrections

| Audit claim | Live source | Effect on plan |
| --- | --- | --- |
| tar default at `tar.c:235` | `magtape[] = "/dev/rmt8"` at 314, digit rewrite at 438, `MTIOCGET` probe in `backtape()` at 2513; the tape path is gated by `#ifndef __APPLE__`, a platform proxy for a capability | Replace the proxy with a capability macro |
| Only one tape comment in reads | `sys_inode.c:261-265` zeroes a buffer when `b_resid == DEV_BSIZE`; `flstrategy()` sets `b_resid = b_bcount` for a read exactly at the partition end (`flash.c:715-719`) | Reachable on flash: keep, rewrite as the end-of-device invariant |
| Removing `d_flags` saves storage | Two maintained `bdevsw` tables (rp2040: 5 rows + terminator; stm32) and legacy pic32 use positional initializers; no driver sets `B_TAPE` anywhere | Repurpose the field, keep its width; savings are tens of bytes |
| update swaps wear raw flash | Raw swap already allocates with a cyclic next-fit cursor (`subr_rmap.c:271`, `malloc3_contiguous_next`) and SwapRAM sits in front | Wear spreads; the cost is swap latency and program/erase volume, to be measured |
| MBR removal recovers a block | True (one of 989), but block 0 is written once at image build and never rewritten | No wear benefit; defer (section 4) |

## 2. Dependency-ordered plan

Each phase is one PR. Every commit builds and bisects. Phase 0 changes no
behavior; phases 1-3 are the storage core and must land in order; phases 5-7
are independent and can proceed in parallel at any time.

```
P0 measure + doc fix --> P1 dirty predicate --> P2 device capability --> P3 group commit
                                         \                                     |
                                          +--> P4 dirty-gated update <---------+
P3 measured --> P3b UFS write policy (conditional)      P5 tar   P6 artifacts   P7 comments/C17
```

### P0 -- Baseline oracle and documentation repair (no behavior change)

1. Host Dhara amplification oracle, `check-dhara-amplification` (host class).
   Link the vendored `sys/arch/rp2040/dhara` sources against a counting NAND
   model built from `tools/flashimg`'s memory model, with the geometry and
   GC ratio taken from `dev/flash.h`. Replay traces: (a) N isolated
   write+sync from an aligned journal, (b) N writes then one sync, (c) the
   same at the `auto_gc` threshold. Assert, per trace, programs and erases
   per request.
   - Prediction: (a) exactly 8 page programs per request below the GC
     threshold; (b) `ceil(N/7) * 8` programs plus final padding.
   - Calibration: a known-bad variant that sets `log2_ppc` to 1 (forcing
     2-page groups) or skips the sync loop must fail the assertion.
2. Board baseline (hardware class, explicit opt-in only). `STORAGE_STATS=yes`
   kernel, paired captures per STORAGE.md "Optional storage counters":
   idle 300 s at a prompt with update running and with it killed; `cc` of a
   multi-file program; `cp` of a 20 KiB file; a 100-byte overwrite in an
   existing file followed by 60 s idle. Use a paired-difference design so
   the capture's own process cost cancels between arms.
   - Predictions: `ROOT_PROGRAM_PAGES / ROOT_WRITES ~= 32` for isolated
     writes; idle-with-update shows swap or SwapRAM traffic about twice per
     30 s while a process is resident; idle-without-update shows none; the
     overwrite arm shows zero root writes during the 60 s idle (F3).
   - A deviation from any prediction is a stop point: investigate before P1.
3. Correct `STORAGE.md:106` and `tools/flashimg/flashimg.c:26`. Prefer the
   symbol (`FLASH_UNIT_BYTES`) to a literal in prose. The class gate: a
   lint check that rejects a literal byte count adjacent to
   `dhara_map_write` or "Dhara page" unless it equals `FLASH_UNIT_BYTES`,
   calibrated against the current stale text (must fail) and the corrected
   text (must pass).

### P1 -- Dirty predicate decoupled from `fs_fmod`

Change `sync()` to call `ufs_sync()` when `fs_fmod` is set, or any buffer on
the mount's device is `B_DELWRI`, or any in-core inode on that device carries
`IUPD|ICHG|IACC|IMOD`. `ufs_sync()` keeps `fs_fmod` as the superblock gate.
Cost is an `NBUF + NINODE` scan per call; the clean path still writes
nothing.

- Invariant: after `sync()` returns, no delayed buffer or modified inode for
  a mounted read-write filesystem remains unwritten.
- Test: extend `check-storage-correctness`. Overwrite an allocated block
  partially, call `sync()`, assert the strategy stub saw the block and the
  inode block. The current source must fail this test (calibration). Assert
  that a clean mount still produces zero strategy calls, and that a
  read-only mount with `fs_fmod` clear never reaches `panic("sync: rofs")`.
- Predicted board movement: the P0 overwrite arm shows its writes at the
  next update tick instead of never; idle-clean counters unchanged.

### P2 -- Device capability field replaces the tape flag

Remove `B_TAPE` (`buf.h:190`) and its `bdwrite()` branch
(`ufs_bio.c:139`). Keep `bdevsw.d_flags` at its width and redefine it as a
capability mask, for example `D_INLINE` (strategy completes before return)
and `D_WRITEBACK` (the device holds acknowledged-but-uncommitted state and
needs an explicit flush). Express the flush as a kernel-internal
`d_ioctl` command so `struct bdevsw` does not grow; rp2040 `fl` sets both
bits, stm32 and legacy tables keep `0`. Measure `unix.elf` text/rodata for
PICO and PICO_UART before and after; expect a change of tens of bytes, and
state that the value is a smaller state space, not capacity.

### P3 -- Dhara group commit in `flstrategy()`

This executes contract A04 of `rp2040-memory-wear-engineering-program.md`.

- Policy: a write with `B_ASYNC` (bawrite and eviction) returns after
  `dhara_map_write()` without `fl_sync()`. A synchronous write (no
  `B_ASYNC`: `bwrite()`, including the writes made for `IO_SYNC` and for
  `B_SYNC` allocations) calls `fl_sync()` before `biodone()`, as now. A full group checkpoints itself through
  `push_meta()`. The `D_WRITEBACK` flush ioctl calls `fl_sync()`; `sync()`,
  `fsync`, unmount and `rp2040_shutdown_sync()` invoke it after
  `ufs_sync()`, and P1's dirty predicate includes "device has an
  uncommitted group" so a clean filesystem with pending Dhara pages is still
  flushed. Raw swap is unchanged.
- Crash semantics (source, to be tested): Dhara recovery resumes at the last
  checkpoint, and `dhara_journal_dequeue()` takes effect only at a
  checkpoint (`journal.h:179-181`, `tail_sync` in `push_meta()`), so blocks
  referenced by the last checkpoint are not reclaimed early. A power loss
  therefore restores a prefix of the request sequence: ordering survives,
  recency within one group and one sync period does not. That loss window
  (at most 6 asynchronous Dhara pages, bounded by the update period) is the
  same class the buffer cache already accepts for delayed writes. Every
  write acknowledged as synchronous remains committed before `biodone()`.
- Context: flush runs only in process context. A callout cannot flush
  because flash programming disables XIP.
- Tests: (1) the A04 decisive host test -- clean buffers, pending Dhara
  pages, call the durability operation, assert a checkpoint; (2) a host
  crash-prefix simulator that truncates NAND programs after each operation,
  resumes the map and asserts the recovered contents equal the state at the
  last checkpoint; (3) `check-dhara-amplification` trace (b) becomes the
  default flash behavior. Calibration: a variant that drops the flush from
  `fsync` must fail (1); a variant that erases a block before checkpoint
  must fail (2).
- Predicted board movement: sequential 20 KiB write drops from about 20
  syncs and 160 Dhara page programs to about 20 data pages, 3 metadata pages
  and at most 6 final pad pages (5-7x fewer programs and erases); isolated
  synchronous metadata writes are unchanged; idle counters unchanged.
- Board persistence after power cut is a separate hardware claim and stays
  open until a cut test runs on the measured board.

### P3b -- UFS full-block write policy (conditional)

After P3, `bawrite()` no longer forces a checkpoint, so `bdwrite()` for full
blocks buys only merges within the four-buffer window and costs eviction
waits and read-cache occupancy. Decide from P0/P3 captures:
`STORAGE_DIRTY_REWRITES` against `STORAGE_EVICTION_WRITES` and
`STORAGE_BUFFER_WAITS` under the `cc`/`ld` workload. Expected outcome:
reject unless rewrites exceed a stated fraction of root writes. Read-ahead
(`breada()`) is evaluated the same way: with inline completion it moves work
rather than overlapping it and occupies one of four buffers; gate a
`D_INLINE` suppression on measured `ROOT_READS` and wall time for sequential
and random reads.

### P4 -- Dirty-gated update instead of a kernel syncer

Three designs, ranked:

| Design | Idle swaps | New context-safety obligation | Residual |
| --- | --- | --- | --- |
| A. Sync from proc 0 in `sched()` | none | Swapper sleeping in `getblk()`/`biowait()` stalls all swap-in; owner of a busy buffer may be swapped out | Reject without a deadlock proof |
| B. Flush on return to user mode after a deadline | none | Runs `sync()` in whatever resident process returns from a trap or syscall | Nothing flushes if no process returns to user mode |
| C. update(8) blocks in the kernel until dirty and deadline elapsed | none when clean | None: `sync()` keeps running in update's own process context, as today | One swap-in per dirty period, which coincides with real flash work |

Recommend C first: a primitive (a dedicated system call or a blocking
sysctl read) that sleeps until the P1/P3 dirty predicate is true and 30 s
have passed since the clean-to-dirty transition, after which update calls
`sync()`. Clean idle produces no wakeups; dirty state keeps the 30 s bound
that P1 makes real. B can follow as an optimization if P0 shows the dirty
swap-in matters. Tests: host test of the deadline/dirty state machine; the
P0 idle arm must move to zero swap traffic with update running.

### P5 -- tar defaults for a tapeless target

- Replace `#ifndef __APPLE__` with a `TAR_HAVE_MTIO` capability defined by
  the target's Makefile; rp2040 builds without it, so `backtape()` uses
  `lseek()` only and the digit options are rejected.
- Default archive: `$TAPE` if set (BSD convention), else a per-target
  `TAR_DEFAULT_ARCHIVE`; rp2040 uses `-`. Refuse to write an archive to a
  terminal; require a seekable regular file for `r` and `u`.
- Tests: host and qemu-user cases for the default stream round trip,
  terminal refusal, append to a regular file, and `r` on a pipe failing.
  Report the tar text delta and the root's free-block delta.

### P6 -- Artifact names and release manifest

- Make `FSIMG` machine-overridable and name the RP2040 intermediate
  `rootfs.img`; keep `sdcard.img` for stm32, which is an SD card. Touch only
  maintained references (root `Makefile`, `etc/Makefile`,
  `distrib/rp2040/{Makefile.inc,README.md,.gitignore}`,
  `sys/arch/rp2040/doc/{PROFILES,BOOT-MAP}.md`, `AGENTS.md` plus its
  synchronized copy); legacy and research text stays as captured.
- `installfs` fails with a message for `MACHINE=rp2040`; flashing stays
  behind `discobsd-flash` or explicit `picotool`.
- RP2040 release: `PICO/unix.uf2`, `PICO_UART/unix.uf2`, `flash.uf2`,
  `rootfs.img` labeled as an inspection artifact, README and a SHA-256 list;
  a gate checks the release directory against that list.

### P7 -- Comments and C17, after the deletions

Rewrite the rationale, keep the behavior: `physio()` short-transfer
termination (`vm_swp.c:157,163`), end-of-device zeroing
(`sys_inode.c:261-265`), last-close invalidation for removable media
(`sys_inode.c:419-421`). Convert K&R definitions such as `rawrw()` to C17
prototypes in separate formatting commits, after P2 removes dead branches.

## 3. Gates added

| Gate | Class | Phase | Known-bad input |
| --- | --- | --- | --- |
| `check-dhara-amplification` | host | P0 | `log2_ppc = 1`, or no sync loop |
| Dhara unit lint | lint | P0 | current STORAGE.md:106 text |
| sync dirty-predicate case in `check-storage-correctness` | host | P1 | current `sync()` |
| A04 flush and crash-prefix tests | host | P3 | flush dropped from fsync; early erase |
| tar default-stream contracts | host, qemu-user | P5 | `/dev/rmt8` default |
| RP2040 release manifest | build | P6 | release missing `flash.uf2` |

Each is registered in TESTING.md, the root Makefile and the CI owner.
Board captures stay behind explicit opt-in and are reported separately.

## 4. Deferred or declined

- MBR removal: gains one 1 KiB block of 989 and some parsing text in
  `fl_setup()`; no wear gain. It is an on-flash format break, as is a
  `FLASH_GC_RATIO` change; bundle any such breaks into one migration with
  old-image rejection and conversion tests.
- `mkfs`, `fdisk`: built, not installed (`distrib/rp2040/mi.rp2040`); root
  cost already zero. The cylinder text in `mkfs.8` is cross-target
  documentation.
- Tape, floppy and dump manuals, `mtio.h`: retained history; exclude from
  RP2040 API claims.
- STM32 SD drivers: real storage, untouched.

## 5. Open questions and residuals

- F2's per-request figure is derived from source; P0 item 1 is its first
  execution and P0 item 2 its first board observation.
- Whether delayed-write loss plus group-commit loss changes any fsck outcome
  on the measured board is a power-cut claim, not covered by host tests.
- The P4 primitive adds ABI; a system-call number versus a sysctl node is an
  interface decision for review.
- The P3b and read-ahead decisions have no predicted direction strong enough
  to act on without measurement.
