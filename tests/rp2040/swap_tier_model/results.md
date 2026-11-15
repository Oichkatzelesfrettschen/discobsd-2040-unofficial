# Swap tier variants: results

Model results, not board measurements. Every row replays the four
calibrated compile traces of `calibrate.FITS` (README.md, "Calibration")
through the shipped pool, swap map and flash writer; a range is the spread
over those four fits, a single number means they agree. The baseline
misses one fitted counter (12 refusals short by 5 to 6 KiB), so a variant
whose effect runs through that pool state carries that uncertainty too.

Reproduce: `$PYTHON tests/rp2040/swap_tier_model/variants.py --sessions 200`.

Columns: admissions and erases per compile come from one session per fit.
"Hottest sector" sums each sector's erases over 200 sessions, each booted
with the swap cursor at a uniformly drawn sector as `swap_cursor_init()`
draws it, and divides the largest sum by 200 x 24 compiles: the long-run
wear of the part, not one session's spread (the board's single sessions
spread to 27 percent above the mean, flash-traffic-per-workload.md). Life
is 100,000 rated cycles over the hottest figure. The baseline's 159,000
compiles is that long-run figure at 59 erases a compile; the board note's
104,000 is the hottest observed sector of single sessions at 69 to 75
erases a compile on an older tree, and the two do not compare directly.
Peak blocks is the largest number of raw-swap blocks in use at once.
Sessions refused is the share of sessions in which the swap map could not
hold an image. The kernel then refuses the fork (`newproc()` returns -1)
or keeps the outgoing process resident and defers the swap-in (`sched()`),
so the traced workload does not complete: the replay stops at that record
and a row with any refused session carries no erase or wear figure; its
peak blocks are those reached before the refusal.

Changed from the first published table: the refusal column was a count
summed over every session and fit, so it scaled with `--sessions`; it is
now the share of sessions. The 96 KB and 64 KB rows replayed on past a
refused image as if the transition had succeeded and reported 58.4 and
57.7 to 57.9 erases a compile with lives of 39,240 and 25,540 compiles;
they now refuse the workload in every session and report no wear figure.
No other row changed. Extra RAM is exact for a
buffer size; text is an estimate whose basis `variants.TEXT_BASIS` states.

## Variants

| Variant | Admitted | Erases/compile | Hottest sector erases/compile | Life (compiles) | Peak blocks | Sessions refused | Extra RAM | Text (est.) | Erases saved/compile | Saved per KB RAM | Saved per KB text |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| baseline (PICO kernel) | 62 to 63 | 59.0 | 0.627 to 0.629 | 159,217 | 104 | 0% | 0 | 0 | 0.0 | -- | -- |
| 1 u area reserved at compressed size + 64 | 159 to 160 | 34.8 | 0.369 | 271,186 | 104 | 0% | 0 | 120 | 24.2 | -- | 206.6 |
| 2 compaction on refusal | 154 to 159 | 35.2 to 36.4 | 0.374 to 0.387 | 264,718 | 104 | 0% | 0 | 200 | 23.5 | -- | 120.5 |
| 3a pool 24 KB | 110 to 113 | 48.6 to 49.2 | 0.517 to 0.527 | 191,121 | 84 | 0% | 8192 | 0 | 10.0 | 1.25 | -- |
| 3b pool 32 KB | 210 to 298 | 2.2 to 24.0 | 0.026 to 0.255 | 1,175,750 | 60 | 0% | 16384 | 0 | 51.2 | 3.20 | -- |
| 4 refused images compressed on flash | 62 to 63 | 20.3 | 0.219 to 0.221 | 454,438 | 32 | 0% | 0 | 600 | 38.7 | -- | 66.1 |
| 5 zero-sector elision on flash | 62 to 63 | 59.0 | 0.627 to 0.629 | 159,217 | 104 | 0% | 32 | 80 | 0.0 | 0.00 | 0.0 |
| 6 cc driver shell never swaps out (upper bound) | 54 to 56 | 11.5 to 11.7 | 0.125 to 0.127 | 797,011 | 80 | 0% | 0 | 0 | 47.5 | -- | -- |
| 1+2 | 116 to 209 | 24.6 to 47.9 | 0.261 to 0.509 | 309,528 | 84 | 0% | 0 | 320 | 28.6 | -- | 91.5 |
| 1+2+4 | 116 to 209 | 8.1 to 15.9 | 0.088 to 0.172 | 916,905 | 24 | 0% | 0 | 920 | 49.0 | -- | 54.5 |
| 8 swap off NOR (PSRAM or SD) | 62 to 63 | 0.0 | 0.000 | unbounded | 0 | 0% | 0 | 0 | 59.0 | -- | -- |

| Raw swap | Sectors | Erases/compile | Mean sector erases/compile | Hottest | Hottest/mean | Life (compiles) | Peak blocks | Sessions refused |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 384 KB | 95 | 59.0 | 0.621 | 0.628 | 1.01 | 159,217 | 104 | 0% |
| 256 KB | 63 | 59.0 | 0.937 | 0.991 | 1.06 | 100,920 | 104 | 0% |
| 192 KB | 47 | 59.0 | 1.256 | 1.273 | 1.01 | 78,566 | 104 | 0% |
| 128 KB | 31 | 59.0 | 1.905 | 1.940 | 1.02 | 51,538 | 104 | 0% |
| 96 KB | 23 | refuses the workload | -- | -- | -- | -- | 68 | 100% |
| 64 KB | 15 | refuses the workload | -- | -- | -- | -- | 44 | 100% |

Variant 5 saves nothing in the model because the traces carry no all-zero
sector; the board bounds it instead. `swapram_refuse_zero_chunks` counted
4 all-zero 4 KiB data chunks in 782 over the 24 compiles, so eliding them
saves at most 0.17 erases a compile, for 32 bytes of RAM and about 80 of
text: not worth building. The images are 69 percent zero bytes, but the
zeros lie in runs shorter than a sector; the codec already takes them.

Variant 8 removes every NOR erase and changes no admission
(`test_swap_off_nor_changes_nothing_but_erases`): the pool never reads the
flash path's state.

Combining variants is not additive. 1+2 gives 24.6 erases a compile in
three fits and 47.9 in the fourth (`FITS[3]`), against 34.8 for variant 1
alone in every fit. The same fit is the one where a 32 KB pool leaves 24.0
erases a compile against 2.2 to 3.0 in the others. Which pool state makes
`FITS[3]` differ is not established; it marks how far the fitted layouts
leave these two results open.

## Raw swap size

The ring rotates every image over fewer sectors as the region shrinks, so
the erases a compile stay at 59 while each sector takes more of them.
Shrinking saves no erase; it trades part life for root capacity.

Under the compile trace at most 104 blocks are in use at once, so 128 KB
(124 usable blocks) is the smallest size that never refuses; at 96 KB
every session refuses an image and the compiles do not complete. That bound covers this workload only. `SWAP_IMAGE_GUARD`
(`machine/machparam.h`) is 196 blocks, the largest image a fork can ask
for, so a region below about 200 KB cannot hold a fork of a full 192 KiB
process at all; and `dev/flash.h` records 256 KB failing a 70 KiB image
with 115 KiB free before the ring allocator existed.

## Ranking

Erases saved per compile, per KB of kernel RAM or text:

| Rank | Variant | Erases saved/compile | Cost | Saved per KB |
| ---: | --- | ---: | --- | ---: |
| 1 | 6 cc driver shell never swaps out | at most 47.5 | no kernel bytes; what replaces the shell is open | -- |
| 2 | 1 u area at compressed size + 64 | 24.2 | about 120 B text | 207 per KB text |
| 3 | 2 compaction on refusal | 23.5 | about 200 B text | 121 per KB text |
| 4 | 4 refused images compressed on flash | 38.7 | about 600 B text | 66 per KB text |
| 5 | 1+2+4 | 49.0 | about 920 B text | 55 per KB text |
| 6 | 3b pool 32 KB | 51.2 (24 to 56.8) | 16,384 B RAM | 3.2 per KB RAM |
| 7 | 3a pool 24 KB | 10.0 | 8,192 B RAM | 1.25 per KB RAM |
| 8 | 5 zero-sector elision | at most 0.17 | 32 B RAM, about 80 B text | -- |
| -- | 7 smaller raw swap | none | frees root flash, shortens part life | -- |
| -- | 8 swap off NOR | 59.0 | PSRAM or SD hardware | -- |

A text estimate twice too large or too small reorders ranks 2 and 3 (207
against 121 per KB) but not their place above variant 4 or the pool sizes:
a KB of text saves more than 15 times what a KB of pool RAM saves.

## Build order

1. Measure the u area's compressed length at capture against its counted
   length on the board (`swapramdebug`). Variant 1 rests on the slack: the
   clock interrupt writes the current process's u area between the counting
   pass and the capture, which is why `swapram_reserve()` reserves the
   worst case. With a measured bound, reserve the counted length plus that
   bound and fall back to flash on overflow. 0 RAM, about 120 B text, 41
   percent of the erases.
2. Variant 4, refused images compressed on flash: the largest saving that
   costs no RAM, and it shrinks peak flash use from 104 to 32 blocks.
3. Variant 6 in userland: the cc driver's shell is the class every
   compile refuses eight times. The model deletes those eight swap-outs,
   so 47.5 is an upper bound: a compiled driver still forks four passes
   and waits, so it still swaps out eight times, with a smaller image the
   pool may admit. Its saving depends on that image's compressed size,
   which no capture records.
4. Variant 2 only after 1 and 4, measured on the board: added to 1 it
   saves 10 more erases a compile in three fits and costs 13 in the
   fourth.
5. A larger pool when RAM can be spared: 32 KB nearly ends flash swap for
   this workload in three of the four fits.

Leave the raw swap at 384 KB unless root capacity is worth the life: 256 KB
cuts the modeled life from about 159,000 to 101,000 compiles.
