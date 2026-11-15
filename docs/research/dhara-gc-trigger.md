# Where Dhara's collector starts

## Finding

Starting collection later lowers the pages programmed per write at every
live share below full, with no trim. In the host model, the trigger
`T = journal capacity - safety margin - live / 5` cuts the compile-like
workload from 6.08 to 4.00 pages per file block. Fixed triggers of 1,150
and 1,250 user pages fill the journal under an adversarial workload. The
decision to change the vendored cleaner (`sys/arch/rp2040/dhara/map.c`,
`auto_gc()`) belongs to the owner.

## The source

`auto_gc()` collects `gc_ratio + 1 = 5` tail pages per write once
`dhara_journal_size() >= dhara_map_capacity()`.

- `dhara_journal_capacity()` counts the user pages of the good blocks less
  one block. That is 1,316 until the journal first wraps, because the
  initial bad-block estimate `bb_last` is 192 >> 6 = 3 blocks. After the
  first wrap it is 1,337.
- `dhara_map_capacity()` is that capacity less a reserve of capacity / 5
  and a safety margin of `DHARA_MAX_RETRIES << log2_ppb` = 64 pages. That
  gives 989 before the first wrap, which the filesystem fills exactly, and
  1,006 after.
- `dhara_journal_size()` counts user pages between `tail_sync` and the
  head.

The reserve exists so that the journal never fills: the head must not reach
the block of `tail_sync` (the `DHARA_E_JOURNAL_FULL` check in
`prepare_head()`) while live pages remain uncollected. Changing the trigger
does not change the capacity the map advertises, which `prepare_write()`
checks.

The lead uses 1,337, the capacity after the first wrap; before it the
capacity is 1,316. Otherwise the lead reads the code correctly.

## Peak journal size

Collecting a run of R live tail pages copies 5 pages per write, so the
journal grows by one page per write: R / 5 pages over the run. In steady
state with L live sectors, each write must free one garbage page among the
5 it scans, so the journal settles near L x 5 / 4. Allowing up to 7 pages
of checkpoint lag at `tail_sync`, the derived bound is:

    peak <= max(T + R/5, 5L/4) + 7, with R <= L

This bound is derived, not formally proven. The adversarial runs land
within a few pages of it.

| Trigger | Bound at L = 989 | Adversary peak | Result |
| --- | --- | --- | --- |
| stock (989, 1,006 after the first wrap) | 1,243 | 1,240 | no failure |
| fixed 1,043 | 1,248 | 1,244 | no failure |
| cap - 64 - L/4 | at most cap - 64 for L >= 140 | 1,240 | no failure |
| cap - 64 - L/5 | cap - 64 + 7 | 1,256 | no failure; enters the margin by up to 7 pages |
| fixed 1,150 | 1,355 | -- | journal full at write 344 |
| fixed 1,250 | 1,455 | -- | journal full at write 343 |

The adversary writes all 989 sectors in order and then rewrites only the
last one, which keeps the tail live for as long as possible. One run makes
30,000 rewrites. A second run makes 3,000, each followed by a checkpoint
and a reset with resume, and checks every sector after each reset. The
reset runs found no lost sector or failed resume for any trigger, but
checkpointing every write keeps the journal lower, so they did not reach
the peaks above.

## Cost

Uniform random rewrites, 20,000 writes with a checkpoint every 20. Each
cell is pages programmed per write, then the least free journal space seen.

| Trigger | L = 758 | 850 | 912 | 989 |
| --- | --- | --- | --- | --- |
| stock | 2.55, 319 | 3.90, 316 | 5.85, 292 | 5.93, 210 |
| fixed 1,043 | 2.33, 266 | 3.30, 266 | 4.74, 254 | 5.91, 212 |
| cap - 64 - L/5 | 2.00, 208 | 2.73, 227 | 3.68, 232 | 5.92, 211 |
| cap - 64 - L/4 | 2.15, 246 | 3.11, 267 | 4.67, 272 | 5.93, 212 |
| fixed 1,150 | 1.92, 161 | 2.42, 161 | 2.97, 158 | 4.29, 154 |

The filesystem workloads of `trim-cost-model.md`, arm B (inode first, no
trim), from 758 live sectors, in pages per file block:

| Trigger | W1a | W1b | W2 | W3 first 500 | W3 next 3,000 | W4 |
| --- | --- | --- | --- | --- | --- | --- |
| stock | 6.05 | 6.63 | 6.08 | 1.65 | 1.42 | 2.00 |
| fixed 1,043 | 5.48 | 6.48 | 5.39 | 1.49 | 1.41 | 2.00 |
| cap - 64 - L/5 | 4.60 | 5.76 | 4.00 | 1.36 | 1.35 | 2.00 |
| cap - 64 - L/4 | 5.01 | 6.50 | 5.36 | 1.42 | 1.36 | 2.00 |

Erases follow the pages: 0.76 per block for W2 with the stock trigger,
0.50 with cap - 64 - L/5. `tests/rp2040/dhara_trigger/run.sh` reproduces
the first two tables. The filesystem rows came from the same `trim_cost.c`
arms, linked against a copy of `map.c` with the trigger replaced; that build
is not committed. The model counts Dhara pages, not board wear.
