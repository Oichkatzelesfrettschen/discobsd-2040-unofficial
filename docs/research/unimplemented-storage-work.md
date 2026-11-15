# Storage work built and not merged

One record of storage mechanisms that were implemented, measured and left
out of `main`, with the measurement that decided each and the condition
that would reopen it. The code is retained on two archive branches:

- `archive/rp2040-trim-freed-blocks` (tip 2d154315, 7 commits from
  df48d76d);
- `archive/rp2040-free-list-sweep` (tip 2c35eff2, 11 commits), which
  contains the first as its prefix.

## What reached main from those branches

Six of the eleven commits are on `main` in later revisions, through other
pull requests: the Dhara checkpoint on sync and group fill (fd5c5e6b), its
policy tests over a NOR model (e30f1f17, now
`tests/rp2040/flash_checkpoint`), the checkpoint policy text in
`sys/arch/rp2040/doc/STORAGE.md` (7da13433), the forced root checkpoint at
shutdown (6b839f10), the trim cost record (1e273f42, now
`trim-cost-model.md`) and the later Dhara collector trigger with its bound
(2c35eff2, now `sys/arch/rp2040/doc/research/dhara-gc-trigger.md` and
`tests/rp2040/dhara_trigger`).

## Unimplemented items

Each of the five remaining commits depends on the first, so none merges
alone.

| Item | Commit | What it does | Deciding measurement | Reopens when |
|---|---|---|---|---|
| Trim at free | 8aa41175 | `itrunc()` writes the inode before the frees; `free()` trims each freed block through `DIOCTRIM` | Bare Pico, 8 compiles: 1,592 root erase sectors with trim, 1,374 without, 1,128 with the collector trigger `main` carries (`trim-cost-model.md`) | The backing store prices a discard below a write: an SD card or eMMC with a native discard, or a NOR translation layer whose trim skips collection |
| Inode-before-free order alone | 8aa41175 | The ordering half without the trim | Safe under Dhara by call order (`rp2040-flash-trim-ordering.md`); it changes no wear figure, so it has no measured benefit | Issue #571 decides it; a file system with a discard needs it first |
| Reset-cut tests of truncate trims | b28daef4 | `tests/kernel/trim_order_test.c` replays a truncate that trims with a reset at every cut point over Dhara | Passes on the branch; exercises only the unmerged trim | Trim at free reopens |
| Lookup before trim | 00f5c04d | `fl_trim()` calls `dhara_map_find()` so that a trim of an unmapped sector runs no collection; `tests/kernel/trim_cost.c` | Removes the cost of trimming unmapped sectors, not the 4.4 pages a mapped trim costs | Any trim path reopens: this is its first commit |
| Deferred free-list sweep and trim cost by arm | bf1f3981 | Sweeps the free list at sync and trims blocks that stayed free; arms A to E in the host model | The sweep never beat leaving freed blocks mapped in any modeled workload (`trim-cost-model.md`) | A workload whose freed blocks stay free across many root rewrites, measured first with the same arms |
| Trim-on-free document section | 2d154315 | Per-caller audit of `free()` and the model cost | Superseded by `trim-cost-model.md` | -- |

## Related records

- `log-structured-store-scope.md`: an F2FS-inspired store for the NOR root
  and an SPI SD card, scoped and not built; SPI mode offers no discard,
  which keeps the first row's condition unmet on the PicoCalc card.
- `trim-survey/`: the survey reports and board captures behind the rows
  above.

## Reproducing a row

`git worktree add ../trim archive/rp2040-free-list-sweep`, then the
commands each note names (`bmake -C tests/kernel trim-cost-report` for the
arms). The branches predate later `main` changes to `flash.c`; they build
at their own tips and need a rebase before any reuse.
