# Processor-reset recovery cells of the flash root

W03 of `rp2040-memory-wear-engineering-program.md` (the Dhara persistent-state
reset matrix, issue 573) asks for the root to recover after a reset in a page
program, a sector erase and a Dhara checkpoint, with at most the unsynced
writes lost. This note fixes the prediction for each cell before any board
run, records the chip facts the cells rest on, and reports what the host oracle
found. The oracle is `check-flash-reset-cells` (`tests/rp2040/flash_reset_cells`);
`cells.tsv` beside its test is the table below as data.

## Status and evidence boundary

Evidence class: host model over the production `flash.c` driver text and the
vendored Dhara, built at the revision that carries this note. The chip
semantics are derived from the W25Q16JV datasheet revision G and the RP2040
datasheet (`discobsd-2040-notes`, `docs/rp2040/W25Q16JV_Datasheet.txt` and
`rp2040-datasheet.txt`; `sys/arch/rp2040/doc/DATASHEET-INDEX.md` locates the
RP2040 sections). The boot ROM's command sequence was not read; the claims
about it are inferences and are marked. No board ran. The power-interruption
half of W03 is not modeled.

## Finding

The finding below holds for `journal.c` before the change in
`dhara-torn-checkpoint.md`, which makes `find_root()` accept a checkpoint
page only when the sector tree it roots is whole; every row now passes and
the gate fails the unchanged journal on the 18 tear rows and the wrap row.

A reset between the four chip commands of one Dhara checkpoint page program
leaves a page that `dhara_journal_resume()` accepts, because `find_root()`
tests the magic and the epoch in the page's first four bytes and nothing
else. The page's metadata for the group's last user page lies in the last chip
page, which the reset left erased, so the recovered root reads every sector as
unmapped (a cut inside the last chip command leaves a partly written path and
a mixed state). The root appears blank: data that an earlier `fsync()` made durable
is gone, which the recovery contract forbids. `fl_setup()` discards the
`dhara_map_resume()` status, so nothing reports it.

Smallest history that shows it (row `Kf-min`): write block 0, checkpoint,
write block 7, checkpoint, and reset after the first of the four chip commands
of the second checkpoint page. The first checkpoint's write is lost.

Of the 44 cuts the oracle placed in a checkpoint page program (four commands
by eleven byte prefixes; a twelfth, 46, now also cuts inside the root's
sector id), 40 left the permitted set. All three cuts that fall
between commands fail (after one, two or three of the four commands); of the
cuts inside a command, a prefix of at most three bytes passes because the
epoch byte is still erased, and a prefix of 255 bytes of the last command
passes because the metadata ends before it.

## What a processor reset does to the chip

| Fact | Source | Class |
| --- | --- | --- |
| A page program takes one command per 256-byte page and a sector erase one command per 4 KiB sector; the Dhara page program (1 KiB) is four commands and the Dhara block erase (8 KiB) two | RP2040 datasheet 2.8.3.1.3 (`flash_range_program`: 256-byte multiples; `flash_range_erase`: 4 KiB multiples); `flash.h` | datasheet |
| A command whose /CS has risen after a full byte is self-timed inside the part and completes: page program 0.4 ms typical, 3 ms maximum; sector erase 45 ms typical, 400 ms maximum | W25Q16JV 9 (instructions), Page Program (02h), 9.3 Sector Erase (20h), AC characteristics | datasheet |
| Only the dedicated /RESET pin (SOIC-16 and TFBGA packages, absent from the 8-pin part) or the 66h/99h reset command stops an operation in progress; the boot ROM's XIP exit sequence reads as two FFh no-op instructions to a part already in SPI mode | W25Q16JV 4.1 and 6.2; RP2040 datasheet 2.8.1.2 | datasheet |
| While busy the part ignores every instruction but Read Status Register and suspend; the boot ROM retries its second-stage read 128 times at about 4 ms (about 0.5 s) before USB boot, more than the 400 ms sector erase maximum | W25Q16JV 7.1 (BUSY), Read Data (03h); RP2040 datasheet 2.8.1 and 2.8.1.3.1 | datasheet |
| A program or erase instruction whose /CS rises inside a byte is ignored; a page program closed on a byte boundary programs the bytes clocked and no others | W25Q16JV 9 (instructions), Page Program (02h) | datasheet |
| The QSPI /CS pad resets with its pull-up enabled, so a processor reset raises /CS; where in the byte it lands is not controlled | RP2040 datasheet 2.19.4 (`GPIO_QSPI_SS`, PUE reset 1) | datasheet and inference |
| The boot ROM runs the 256-byte pages of `flash_range_program` as separate commands and waits for each | not read | inference |

A reset therefore leaves each chip command either not started or complete, and
a page program in the clocking window leaves a byte prefix of the page. Bits
partly cleared by a program and partly set by an erase need the part's supply
to fail; they belong to power loss (H09's third injection class), so the
brief's suggestion to model them for a reset does not apply and the oracle
omits them.

Measured on the board (`research/flash-traffic-per-workload.md`): 375
microseconds per page program and 29 to 30 ms per sector erase.

## The oracle

`tests/rp2040/flash_reset_cells/flash_reset_cells_test.c` includes the NOR
model of `tests/rp2040/flash_checkpoint`, whose hooks run before a model
program or erase takes effect. For each row the script runs to the operation, a
snapshot of the chip and the driver's RAM is taken, and the operation runs
once to record its chip commands. The operation then runs again for every cut: a cut stops it
before chip command c, with 0 bytes of that command programmed, and for a page
program command also after a prefix of 1, 3, 4, 7, 8, 16, 20, 46, 64, 128 or
255 bytes of its page (46 ends inside the root's sector id in the last chip
page of a checkpoint). Commands before c apply fully, as the part finishes them. After the cut the oracle remounts through
`fl_setup()` and Dhara recovery and gives two verdicts.

- Map: `dhara_map_resume()` succeeds, and the resumed map accepts two writes
  and a checkpoint, survives a further reset and breaks no NOR rule (a program
  over unerased flash). A fault counts against the cell.
- Content: every logical block the history touched, plus three never written,
  equals the state after some prefix of the write history. The prefix must lie
  in the bounds the row's `must` and `may` columns give, and equal the prefix
  the last finished checkpoint page covers (`last_cover`, taken from the chip
  commands, not from the table). A cut inside a checkpoint page program may
  recover that prefix or the one the page would have made durable (`tear`
  column, H09's permitted synchronization-point states).

The `must` tokens: `base` (the fixture checkpoint), `ack` (writes covered by a
returned acknowledgment), `group` (all but the six requests of the open
group). The `may` token `prefix` allows any prefix up to the writes started;
`none` allows only the floor.

Calibration. With every checkpoint page programmed as one command
(`FLASH_RESET_CELLS_ATOMIC_CKPT`) every row passes, including the tear rows.
Nine mutations each fail the rows named in `check.sh` (a sanitizer abort is given exit status 70 and a calibration case shows it cannot pass for a rejection; block payloads carry the whole write version, so a state rolled back by 768 writes is not mistaken for a recent one; the swap isolation check covers all 96 swap sectors): a checkpoint that never
reaches Dhara (`E-H2`, `P-H2`, `Kf-H2`), a checkpoint that reports success
without programming its page (`E-H2`, `Kf-H2`), a block erase over one sector
(`Ed-H1`, which needs a block holding a previous lap's pages, so the script
wraps the journal), a page copy one chip page short (`Gk-H1`), a swap
rewrite that skips the spare copy (`T-H1`), a swap append that erases a sector
20 sectors into the extent (`S-H1`), a journal that skips the sector-tree test
(`Kf-min`, `Kf-H2`, `Kg-H2`, `Gk-H2`, `F-H2`, `Kw-H2`), one that takes a page
with an erased tail for a checkpoint (`Kw-H2`), and one that accepts a one-sector root whose
id has erased high bytes (`K1-H2`).

## Cell table

Histories: H0 nothing acknowledged since the fixture checkpoint; H1 device
requests returned, no acknowledgment (write(2) returning fixes nothing at the
device, which sees requests when buffers flush); H2 `fsync()` returned; H3
`sync()` returned; H4 the periodic sync fired. H2, H3 and H4 run one identical
device trace (`DIOCCHECKPOINT` returned) and differ in who acknowledges it: a
`sync()` that skips a locked mount, or an update(8) call that has not run yet,
acknowledges nothing for that mount, so their `must` terms hold only when the
checkpoint ran, which the checkpoint counter of `STORAGE_STATS` shows.

Result of the shipped driver per cell: `pass`, `n/a` (no such cell by
construction). The four tear operations (`Kf`, `Kg`, `Gk`, `F`) were
`defect` before the change in `dhara-torn-checkpoint.md`.

| Operation in flight | H0 | H1 | H2 | H3 | H4 | Recovered state permitted |
| --- | --- | --- | --- | --- | --- | --- |
| `P` page program inside an open group | n/a | pass | pass | pass | pass | the last finished checkpoint's state; nothing the cut write held |
| `E` first write of a block: two sector erases, then the page program | pass | pass | pass | pass | pass | the same |
| `Ed` the same over a wrapped journal | n/a | pass | pass | pass | pass | the same |
| `Kf` forced checkpoint: padding copies, then the checkpoint page | pass (no commands) | pass | pass | pass | pass | the previous checkpoint's state or the new one |
| `Kg` seventh write: data page, then the group's checkpoint page | n/a | pass | pass | pass | pass | the same |
| `G` write whose collection copies pages | n/a | pass | pass | pass | pass | the last finished checkpoint's state |
| `Gk` such a write whose copies close a group | n/a | pass | pass | pass | pass | the previous checkpoint's state or the new one |
| `F` hot-root repair writes, then `sync()` (sbin/fsck/main.c), on the boot after a reset | pass | pass | pass | pass | pass | prefixes of the repair as in `Kf`, the rerun converging |
| `S` raw swap image append | pass | pass | pass | pass | pass | root bytes unchanged; no other swap sector touched |
| `T` raw swap rewrite through the spare | pass | pass | pass | pass | pass | the same, and each page the request leaves alone intact in the destination or the spare |

Bounds by history (`cells.tsv` columns `must` and `may`): H0 `base`; H1
`base`, `group`; H2 to H4 `base`, `ack`, `group`. The documents fix them: the
open-group bound and the checkpoint-return rule are in `STORAGE.md` "Checkpoint
cost" (a reset discards the writes since the last checkpoint, forced or
written by a group fill, and `dhara_map_resume()` returns the state that
checkpoint recorded; at most six requests of the open group are lost); H09
fixes the synchronization-point set for a reset during a checkpoint. A write
that fills a group survives although unacknowledged, which the `prefix` term
permits.

A hot-root repair ends in `sync()` and `exit(4)` (`sbin/fsck/main.c`), and
`etc/rc.rp2040` then runs `reboot -n`; the mount holds the node open, so the
repair's own close is not the last close and the acknowledgment is the sync's
checkpoint. The `F` rows run the three repair writes and the checkpoint.

Two rows beyond the table, both `Kf` under H2 and both passing: `Kw-H2`
writes until the journal head wraps to page 0, so the checkpoint in flight
lies in block 0 at the next epoch and the one before it in the last block;
`K1-H2` rewrites the one sector of a one-sector map, so a cut inside the
root's sector id leaves a tree of one node either way.

### Rows the documents do not fix

- `U-fsck-ufs`, unspecified: which files, directories and inodes survive a
  reset inside the repair. `STORAGE.md` records seven forced resets that
  repaired and checked clean (issue 569), which is an observation. The oracle
  judges the block device only (a recovered prefix of the writes, the repair's
  rerun converging); UFS consistency after a cut is not checked here.
- `P`, `E`, `Ed`, `G`, `Gk`, `S`, `T`: the checkpoint rule is applied to
  operations `STORAGE.md` does not name (derived). The swap rows rest on source
  statements: `flash.h` says a swapped image is never read back after a
  reboot, and `flash_swap.c` says a rewrite keeps every untouched page
  available after the destination erase.
- `G` cannot see a damaged copy, because a copy is not durable before the
  remount; `Gk` reads the copies back through the checkpoint that covers them,
  which is why the short-copy mutation fails `Gk` and not `G`.
- `U-resume`, n/a: `dhara_map_resume()` and `fl_setup()` read the chip and
  issue no erase or program, so a reset during recovery repeats the previous
  one.
- The power domain: not modeled; H09 keeps it a separate oracle.

### Board only

- `E-boot`: the model assumes the boot ROM waits out an erase in progress
  (up to 400 ms against its 0.5 s retry); a board reset during an erase must
  still boot the kernel and not fall to USB boot.
- `K-torn`: the checkpoint tear needs an asynchronous reset, below.

## What the board run needs

By timing alone a forced reset through the USB reset interface cannot land
inside any operation of the table. `usb_reset_to_flash()` runs from
`usb_setup()` inside `USBCTRL_IRQ_Handler`; `splbio()` and `splhigh()` are
`cpsid i`; `flstrategy()` and `fl_checkpoint()` hold the mask from their first
chip command to their last and print nothing between; so the interface reset
is serviced after the request returns. It reaches only the points before and
after an operation, which the oracle's first and last cut positions cover.
Core 1's watchdog trigger needs a 4 s stall of the tick record (`CORE1.md`,
"The observer"); each `flash_erase()` and `flash_program()` ends in
`clock_catchup()`, and the longest single call is about 0.4 s (one sector
erase at its maximum), so it cannot fire inside a request either. The routes into a window are the RUN pin, the
debug port, or a reset hook in the kernel.

Windows, with the datasheet figures beside the board's (375 microseconds per
page program, 29 to 30 ms per sector erase):

| Cell | Window | Measured | Typical | Maximum |
| --- | --- | --- | --- | --- |
| `E`, `Ed`: block erase (two sectors) | /CS closed to busy clear, twice | 59 ms | 90 ms | 800 ms |
| `P`, `Kg`, `F`: data page program (four commands) | four page programs | 1.5 ms | 1.6 ms | 12 ms |
| `Kf`, `Kg`, `Gk`: checkpoint page (four commands) | the same; the tear needs a cut after the first, second or third command | 1.5 ms | 1.6 ms | 12 ms |
| `Kf`, `G`, `Gk`: a padding or collection copy | four 256-byte programs | 1.5 ms | 1.6 ms | 12 ms |
| byte-prefix cut inside one page program | clocking 260 bytes at clk_sys/6 (`flash_exit_xip`, RP2040 datasheet 2.8.3.1.3) and 125 MHz | about 100 microseconds | | |
| `T`: swap rewrite | two erases, thirty-two programs | about 70 ms | | |

A RUN-pin pulse at a random phase lands in a 1.5 ms checkpoint window with
probability about that window over the checkpoint period: a workload that
checkpoints once a second hits it in about one of 670 trials. The erase
window is about 40 times longer and its cells pass in the model, so a
campaign aimed at the defect spends most of its trials elsewhere unless a
second board times the pulse from the first board's activity. The byte-prefix
cuts need a pulse timed to within a fraction of the 100 microseconds, which
the RUN pin cannot give without a trigger on /CS.

Deterministic alternative: a `STORAGE_STATS`-only hook in `flash_erase()` and
`flash_program()` of `sys/arch/rp2040/dev/flash.c`, which counts chip commands
of one kind and, at the Nth, requests `SYSRESETREQ` as `usb_reset_to_flash()`
does. Between-command cuts need the hook to call the ROM once per 256-byte page
and 4 KiB sector (a loop of about ten lines, each ROM call unchanged); the
count and the kind come from a `machdep` sysctl (about 25 lines in
`machdep.c`, a few in `sysctl.h` and the `sysctl(8)` tables), and `flash.c`
gains about 30. Estimate: 70 to 90 lines and one `TESTING.md` entry. It
reaches the between-command cuts, which reproduce the defect; the byte-prefix
cuts stay hardware. The hook is not written.

## Open questions for the owner

- The fix reads the existing format: `dhara-torn-checkpoint.md` gives the
  rule, why it is sound, and the format-change, ordering and second-program
  alternatives it was weighed against.
- Whether `fl_setup()` should refuse a root whose resume reports an error
  while a checkpoint exists, instead of mounting an empty map.
- Whether to build the reset hook.

## Narrowings

The content verdict is device-level prefix consistency. The filesystem check
the brief asks for (fsck clean, UFS invariants after a cut) is not run: the
host has no UFS image model for this root. The prediction of a cut inside a
byte is an inference at /CS. The rows run one fixture geometry (seven pages
per group, three base writes) and 23 to 1,400 writes of history.
