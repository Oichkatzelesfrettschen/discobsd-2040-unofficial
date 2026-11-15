<!--
Copyright (c) 2026 DiscoBSD

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
-->

# RP2040 flash traffic per workload

Counters of a diagnostic PICO kernel on the measured Pico, Renode counts of
flash commands, and the flash part's rated endurance, read together for the
workloads of a compile on the board.

## Evidence base

- Board: the measured Pico (`DEVICE.md` in discobsd-2040-notes: Winbond
  W25Q16JV, JEDEC `ef 40 15`), USB console, PICO kernel of `0c021420`
  built with `STORAGE_STATS=yes`, root image of the same commit. Counters
  are `sysctl machdep.storage_stats` captures before and after each
  workload, subtracted (`sys/arch/rp2040/doc/STORAGE.md`, "Optional
  storage counters"). They count requested 256-byte program pages and
  4096-byte erase sectors at the NOR boundary, root and swap apart; they
  are software counts, with no readback and no count per physical sector.
- A capture is itself a process launch. Two captures in a row differ by 0
  root writes and by 3 swap writes, 67 swap program pages and 5 swap erase
  sectors; the rows below include that overhead once.
- Renode: `check-renode` on the PICO_UART kernel, the count of the
  `Unhandled operation: 0xN` warning class, one per program or erase
  command, with one workload in place of the clock stage's compile.
- Part: W25Q16JV datasheet revision G (discobsd-2040-notes,
  `docs/rp2040/W25Q16JV_Datasheet.txt`): minimum 100,000 program-erase
  cycles per sector and more than 20 years of data retention (features
  list, lines 177-178); page program 0.4 ms typical and 3 ms maximum,
  4 KB sector erase 45 ms typical and 400 ms maximum (AC characteristics,
  lines 3003-3005). The board measured 375 microseconds for a page program
  and 29 to 30 ms for a sector erase (`DEVICE.md`).

## Counters on the Pico

The source is two lines, 137 bytes; `d.s` is 704 bytes and `d.o` 224.

| Workload | Root writes | Root program pages | Root erase sectors | Swap writes | Swap program pages | Swap erase sectors | SwapRAM attempts | SwapRAM admissions |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| two captures in a row | 0 | 0 | 0 | 3 | 67 | 5 | 2 | 1 |
| twenty launches of `expr` | 2 | 64 | 4 | 68 | 1,538 | 110 | 45 | 23 |
| `/usr/libexec/smlrc d.c d.s` | 4 | 128 | 8 | 6 | 138 | 10 | 4 | 2 |
| `as d.s -o d.o` | 66 | 2,112 | 132 | 6 | 140 | 10 | 6 | 4 |
| `ld -o d crt0.o d.o libc.a` | 86 | 2,752 | 172 | 6 | 134 | 10 | 4 | 2 |
| `cc -o d d.c`, driver with `expr`, `basename`, `sed` | 175 | 5,600 | 350 | 39 | 993 | 72 | 21 | 10 |
| `cc -o d d.c`, driver of built-ins (three runs) | 162 to 164 | 5,184 to 5,248 | 324 to 328 | 50 to 51 | 1,013 | 73 | 14 | 3 |
| `dd` of 24 KB to `/tmp`, then `sync` | 34 | 1,088 | 68 | 9 | 217 | 15 | 6 | 3 |

## What the counters establish

Every root write request of 1 KiB programs 32 pages, 8 KiB, and erases two
sectors, in every row: 2 writes and 64 pages, 66 and 2,112, 175 and 5,600.
`flstrategy()` calls `fl_sync()` after each write request
(`sys/arch/rp2040/dev/flash.c`), and `dhara_map_sync()` pads the journal
to the end of its checkpoint group, one checkpoint page per eight 1 KiB
units, so a write that arrives alone consumes a whole group. The comment
above `fl_sync()` and STORAGE.md describe the cost as a checkpoint page
per request and as four program pages; the measurement is eight times
that.

The assembler issues 66 root writes for a 224-byte object and the linker
86 for a program of a few kilobytes: `as` keeps its sections in
`/tmp/tasXXXXXX` (`usr.bin/as/as-thumb.c`) and `ld` in `/tmp/ldaXXXXXX`
(`usr.bin/ld/ld.c`), and the buffer cache reports 33 and 236 rewrites of
a buffer that was already dirty during the two runs. Root writes carry
350 of the 422 sector erases of one compile, 83 percent.

A launch from the shell costs swap traffic that depends on whether the
compressed pool in RAM admits the image: of 45 attempts during twenty
launches of `expr`, 23 were admitted and the rest went to flash, 110
sector erases in all. The driver of built-ins starts three processes
fewer (`expr`, `basename`, `sed`); the compile takes 25.14 seconds where
it took 30.86, issues about 12 root writes fewer, and erases as many swap
sectors as before, 73 against 72, because the launches it removed were
admitted to the pool and the swap-outs that reach flash occur while `as`
and `ld` run.

## Renode counts

| Workload in place of the compile | Flash commands | Added |
| --- | ---: | ---: |
| the two `echo` lines that write the source | 7,850 | -- |
| twenty launches of `expr` | 11,193 | 3,343 |
| `/usr/libexec/smlrc d.c d.s` | 8,106 | 256 |
| the same, then `as d.s -o d.o` | 9,955 | 1,849 |
| the same, then `ld` against `libc.a` | 12,516 | 2,561 |
| `cc -o d d.c`, driver with `expr`, `basename`, `sed` | about 14,400 | about 6,550 |

The Renode count carries no flash offset and counts on another kernel
configuration; the Pico counters above are the measurement that separates
root from swap and erases from programs.

## Against the rated endurance

The root region is 1,536 KB, 384 sectors; at the rated minimum that is
38.4 million sector erases. Dhara writes its journal as one circular log
over those sectors, so the erases fall on them in turn; the counters here
sum the region and do not observe a sector. The swap region is 384 KB, 96
sectors, 9.6 million erases at the rated minimum, written in place with no
leveling.

One compile of the two-line program, `cc -o d d.c; sync`, on the measured
Pico. The first row is the kernel and tools of `0c021420`; each later row
adds one change to the row above and is the mean of 24 compiles after a
fresh flash, read from `sysctl machdep.storage_stats`:

| Tree | Root writes | Root sector erases | Compiles in the root budget | Mean time |
| --- | ---: | ---: | ---: | ---: |
| checkpoint per request, every image sector mapped | 162 to 175 | 324 to 350 | about 110,000 | 25 s |
| checkpoint on sync; free blocks left out of the image's map | 163.8 | 183.9 | about 209,000 | 18.3 s |
| assembler and linker streams in memory | 44.1 | 45.6 | about 842,000 | 10.8 s |
| collection started by the live sector count, less one checkpoint group (the merged trigger) | 44.2 | 39.3 | about 977,000 | 10.4 s |

The last row is a run of its own. `dhara-gc-trigger.md` records the same
image under the trigger without the checkpoint-group term at 44.3 writes,
38.3 erases and 10.33 s; the merged `gc_trigger()` gives up one more group
to stay inside the safety margin and measured the row above. The two
kernels differ by that term alone, and one erase per compile lies inside
the spread of a run (14 to 80 erases across its 24 compiles).

Swap erases stay at 69 to 75 per compile in every row. Spread evenly over
the 96 swap sectors that is about 137,000 compiles, so swap, not the root,
now bounds the life of the part for this workload.

The spread was measured. A kernel of main (`a706c0b8`) with one added
counter per swap sector, incremented in `flash_erase()` and printed by
`boot()`, ran the 24 compiles three times on the measured Pico, each after
a fresh kernel flash. The board reset before any print finished, so the
first two runs printed from sector 0 and the third from sector 32:

| Run | Swap erases | Sectors printed | Erases per printed sector | Mean of the unprinted sectors |
| --- | ---: | --- | --- | ---: |
| 1 | 1,781 | 0 to 63 | sector 0: none; others 15 to 21, mean 18.2 | 19.9 over 32 |
| 2 | 1,794 | 0 to 47 | sector 0: none; others 17 to 23, mean 19.1 | 18.7 over 48 |
| 3 | 1,799 | 32 to 89 | 14 to 22, mean 18.8 | 19.2 over 37, sector 0 taken as none |

The counters run from boot, so each count also holds that sector's share
of the 31 to 36 swap erases the boot and login made before the first
compile and of the few after the last, about 2 percent of a run's total; a
sector's rate per compile is therefore at most its count over 24.

Ninety of the 96 sectors were observed singly in at least one run. The
first swap sector was never erased; every other observed sector took 14 to
23 erases in 24 compiles, within 27 percent of its run's mean over the 95
sectors other than the first (the widest: 14 against 18.9 in the third
run), so the swap
allocator already spreads its extents. The hottest observed sector, at
most 23 erases in 24 compiles, reaches the rated minimum after no fewer
than about 104,000 compiles and the mean sector after about 128,000. Sectors 90 to 95 were
never printed; they are bounded only through the unprinted means, which
match the printed ones, and a hotter sector among those six would lower
the unit's life below the observed sectors' 104,000. Swap bounds the part's life at
roughly a ninth of the root's 977,000 compiles, if the observed rate
persists. Perfect leveling of the observed sectors would move the hottest
from 23 erases to the mean, 104,000 compiles to 128,000, a gain of at most
a quarter; whether the six unprinted sectors leave more for placement to
recover is open. Fewer or smaller swap-outs reaching flash lengthen the
life in proportion.

The counter patch and the three console captures are
`docs/research/trim-survey/board/swap/`.

Two changes were measured and left out. Unmapping a block when the file
system frees it (trim at free) cost 1,592 root erases over eight compiles
against 1,374 without, because a trim runs Dhara's collector as a write
does and the free list hands the block out again at once; a sweep of the
free list at sync did no better in the host model
(`docs/research/trim-cost-model.md`). A reserve of a fifth of the live
sectors in the collection trigger saved more than the shipped quarter
(138.3 against 153.0 erases per compile on the second row's tree) and can
reach into Dhara's safety margin (`dhara-gc-trigger.md`).

## Why swap-outs reach flash

A `STORAGE_STATS` kernel also counts, for every SwapRAM reservation that is
refused, the reason, the shortfall of the largest free run, and the span of
each completed swap-out in 4 KiB sectors (`sys/sys/storage_stats.h`). Two
runs of the 24 compiles on the measured Pico, each from a freshly flashed
root with the same kernel, differ only in `bin/sh`: the shell before the
break-growth fix and the shell with it.

| Over 24 compiles | Shell before the fix | Shell with the fix |
| --- | ---: | ---: |
| Swap sectors erased | 1,604 | 1,412 |
| Swap bytes written | 5,865,824 | 4,986,464 |
| SwapRAM attempts, admitted | 303, 61 | 305, 63 |
| Refused: no single free run, too few free bytes | 240, 2 | 196, 46 |
| Refused swap-outs by span | 192 of 7 sectors, 49 of 5, 1 of 12 or more | 192 of 6 sectors, 49 of 5, 1 of 12 or more |
| Shortfall under 1 KiB, 1 to 2 KiB, 3 to 4 KiB | 0, 184, 48 | 172, 8, 47 |
| Root sectors erased | 934 | 930 |

Eight swap-outs a compile are one image class, refused every time; a second
class of 5 sectors is admitted about half the time, and one swap-out of 12
or more sectors occurs once in each run. The probe records no
process name. The `cc` driver is a shell script, so the dominant class is
most likely its shell, swapped out while it waits on a pass; that is an
inference from the size. Closed admission, a held codec and encoder failure
never occur. The pool (`SWAPRAM_KB=16`) refuses for want of one free run:
the request is the compressed data and stack plus `SR_WORST(USIZE)`, 3,460
bytes held for the u area, which compresses to about 1 KiB when admitted
(64,012 bytes over 63 admissions). The dominant class misses by under
2 KiB, so reserving the u area at its compressed size would admit it; that
change is not built.

The shell with the fix erases 12 percent fewer swap sectors because its
dominant image is one sector smaller. The per-sector counters were not in
this kernel, so the hottest-sector bound above is not re-measured; if the
spread is unchanged, 58.8 erases a compile in place of 66.8 lengthens it in
proportion. These two runs start from a fresh root and admit a fifth of the
attempts; the three runs above started from a used root and admitted over a
third, so absolute counts compare only inside one table.

The captures are `docs/research/trim-survey/board/swap/admission/`.

## Open

- Swap traffic: 69 to 75 sector erases per compile, more than the root
  spends, on a quarter of the sectors. The first swap sector takes none;
  the erases of the other 89 observed sectors lie within 27 percent of
  their run's mean, so the admission policy of
  SwapRAM and the size of what is swapped decide most of it; placement can
  add at most a quarter there.
- Erases of swap sectors 90 to 95 singly, and of the root's sectors, which
  are even by the journal's construction and unobserved.
- Workloads other than the two-line compile.
