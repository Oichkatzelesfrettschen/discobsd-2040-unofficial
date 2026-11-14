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
38.4 million sector erases if Dhara spreads them evenly, which these
counters do not observe. With a checkpoint per request, a 1 KiB write
spends two of them, so the budget is 19.2 million writes, and one compile
of the two-line program spends 324 to 350: about 110,000 such compiles.
The swap region is 384 KB, 96 sectors, written in place with no leveling;
one compile erases 73. Spread evenly that budget is 9.6 million erases,
about 130,000 compiles; how the swap allocator distributes its extents
over the sectors is unmeasured, and a concentration on a few sectors
shortens it in proportion.

## Open

- The checkpoint policy: the owner chose a checkpoint on `sync(2)` and on
  group fill in place of one per request; the change and its reset
  evidence are separate work.
- The assembler's and linker's temporary files.
- Erases per physical sector, for the root and for swap.
