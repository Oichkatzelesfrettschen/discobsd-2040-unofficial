# Trim cost in the Dhara host model

## Status

Trim at free is refuted for compile workloads in the host model and on the
board, and is not merged. The work lives on two unmerged branches:
`archive/rp2040-trim-freed-blocks` (the inode-first order in `itrunc()`, trim in
`free()` through `DIOCTRIM`, and `check-trim-order`) and
`archive/rp2040-free-list-sweep` (a lookup before each trim, `tests/kernel/trim_cost.c`
and the deferred-sweep arms). On the measured Pico, 8 compiles cost 1,592
root erase sectors with trim at free, against 1,374 without it and 1,128
with the later collector trigger that main now carries
(`sys/arch/rp2040/doc/research/dhara-gc-trigger.md`).

## Finding

A trim through `dhara_map_trim()` costs what a write costs: the function
runs `auto_gc()` (`sys/arch/rp2040/dhara/map.c`, `dhara_map_trim`) before
`try_delete()` rewrites a neighbor's page into the journal. Trimming a block
the LIFO free list hands out again within the same burst is therefore pure
loss. Trim pays only for a block that stays free across many later writes,
and then modestly. A deferred sweep at sync never beat leaving freed blocks
mapped. With 2,550 trims in the one-file rounds, trim added 11,182 pages,
4.4 per trim: its own page plus the collection it ran. Write pages
(14,230 against 13,826) and inode writes (600 in both) barely changed.

`auto_gc()` runs before the lookup, so a trim of an unmapped sector also
collects; the `fl_trim()` of `archive/rp2040-free-list-sweep` calls
`dhara_map_find()` first.

## Method

`tests/kernel/trim_cost.c` on `archive/rp2040-free-list-sweep` (`bmake -C
tests/kernel trim-cost-report FSUTIL=...`) runs the production `ufs_inode.c`, `ufs_alloc.c`, `ufs_bmap.c`,
`ufs_bio.c`, `ufs_subr.c` and `ufs_syscalls2.c` over the vendored Dhara map
with the `dev/flash.h` geometry, on a 988 KiB fsutil image loaded as
`flashimg` writes it. Each workload starts from the shipped state: a static
file holds about 758 of the 989 sectors live. The arms:

- A: inode written after the frees (2.11BSD order), no trim;
- B: inode written before the frees, no trim;
- C: trim in `free()`;
- D(K): B, plus a sweep before each sync that trims the free-list entries
  deeper than K from the LIFO top, through chain nodes, never trimming one.

The workloads:

- W1a: 150 rounds, each creating and deleting one 16-block file;
- W1b: 20 rounds of six 30-block files;
- W2: 20 rounds, each writing five 30-block temporaries and deleting them,
  rewriting a 20-block output and syncing once (170 blocks);
- W3: delete a 150-block file, then 500 rewrites of existing blocks with a
  sync every 20, then 3,000 more rewrites;
- W4: a filesystem with 11 sectors live, ten 16-block rounds.

## Results

Each cell is Dhara pages programmed per file block written, then the live
count at the end.

| Arm | W1a | W1b | W2 | W3 first 500 | W3 next 3,000 | W4 |
| --- | --- | --- | --- | --- | --- | --- |
| A | 6.05, 774 | 6.63, 942 | 6.08, 912 | 1.65, 758 | 1.42, 758 | 2.00, 28 |
| B | 6.05, 774 | 6.63, 942 | 6.08, 912 | 1.65, 758 | 1.42, 758 | 2.00, 28 |
| C | 10.82, 757 | 9.99, 757 | 9.11, 758 | 2.62, 607 | 1.23, 607 | 3.00, 11 |
| D(0) | 10.82, 757 | 11.06, 757 | 10.16, 758 | 2.62, 607 | 1.23, 607 | 3.00, 11 |
| D(32) | 6.05, 774 | 10.19, 789 | 8.98, 790 | 2.48, 639 | 1.27, 639 | 2.00, 28 |
| D(64) | 6.05, 774 | 9.89, 821 | 7.94, 822 | 2.35, 671 | 2.45, 671 | 2.00, 28 |
| D(128) | 6.05, 774 | 8.22, 885 | 6.18, 886 | 1.79, 735 | 2.71, 735 | 2.00, 28 |

Erases follow the pages: 0.76 per block for B in W1a, 1.35 for C.

## Reading

- A equals B in every cell: writing the inode before the frees costs
  nothing.
- C and D(0) lose wherever freed blocks are reused within the burst (W1,
  W2, W4). The loss is the trims.
- C recovers its cost only after a large delete followed by long-lived
  free space: 1.23 against 1.42 pages per rewrite once the burst has passed.
  Its 151 trims and the collection after them cost about 490 pages, so it
  breaks even near 2,600 rewrites.
- D(K) with K > 0 approaches B in the bursty workloads, because it leaves
  the hot top mapped, but never beats it.
- In W3's steady state, D(64) and D(128) cost twice what B costs while
  fewer sectors are live (671 and 735 against 758). Their cleaning copies
  rose from 552 to 2,958 and 3,623. Over runs this short, the cost of
  collection depends on where live and dead pages lie in the journal, not
  only on how many sectors are live. The model therefore cannot rank
  deferred-sweep depths reliably.

These are host-model counts for the exercised traces with the upstream
collector trigger. The board measurement above settled the question for
compiles.
