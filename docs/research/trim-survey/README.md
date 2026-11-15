# Discard ordering and flash traffic: survey reports and board captures

The source material behind `../rp2040-flash-trim-ordering.md`,
`../trim-cost-model.md` and the wear table of
`../../../sys/arch/rp2040/doc/research/flash-traffic-per-workload.md`. Those
documents state the conclusions; the files here are the inputs they were
drawn from, kept so a number can be recomputed and a citation reopened.

## Survey reports

| File | Content | Standing |
|---|---|---|
| `bsd-discard-ordering.md` | where each BSD lineage writes the inode relative to freeing blocks, and where FFS issues a discard | a delegated survey; the citations `../rp2040-flash-trim-ordering.md` repeats were reopened, the rest are leads |
| `embedded-discard-policy.md` | discard and cleaning policy in embedded and log-structured stores, with the cleaning-cost formulas from the literature | the same standing |

A third survey searched the owner's local reference library for
write-amplification and cleaning-cost formulas. It found standard textbook
material and no closed form beyond the ones `embedded-discard-policy.md`
cites; it indexes private directories and is not kept here.

## Board captures

Every capture is the console transcript of one bare Raspberry Pi Pico (RP2040,
W25Q16 flash, DiscoBSD `PICO` kernel built with `STORAGE_STATS=yes`), taken
on 2026-11-14 and 2026-11-15. A `### command` line precedes the output of
each command; `sysctl machdep.storage_stats` prints the cumulative counters
and a result is the difference of two prints. The home directory in a prompt
is the target's. Trailing blanks were removed from every line; the reset
captures were reduced as described below and nothing else was edited.

`board/arms/`: one file per kernel and root image, each a fresh flash
followed by `cc -o d d.c; sync` repeated.

| File | Kernel | Compiles |
|---|---|---|
| `B.txt`, `L-B.txt` | the vendored Dhara trigger main carried before the change | 8, 24 |
| `G4.txt`, `L-G4.txt` | cleaning starts at journal capacity - 64 - live sectors / 4 | 8, 24 |
| `G5.txt`, `L-G5.txt` | cleaning starts at journal capacity - 64 - live sectors / 5 | 8, 24 |
| `T.txt` | trim at free (branch `rp2040/trim-freed-blocks`) | 8 |
| `A-B.txt`, `A-G4.txt` | as `B` and `G4`, root image with the in-memory assembler and linker streams | 24 |
| `A-F.txt` | the merged trigger (the `G4` threshold less one checkpoint group of 8 pages; `sys/arch/rp2040/doc/research/dhara-gc-trigger.md`), same root image | 24 |

`python3 board/arms/sum.py FILE...` prints, for each file, the root erases
summed over its compile steps and the writes, erases and seconds per compile;
the file write and delete steps of the 8-compile captures are left out.
`table.py` prints the differences of every step.

`board/wl*.txt`: workload traffic before the checkpoint change (`wl.txt`,
`wl-cc.txt`, `wl-cc3.txt`, `wl-base.txt`: one checkpoint per map write) and
with two prototypes that group checkpoints (`wl-p3.txt`, `wl-ab.txt`:
13 and 12 checkpoint attempts against 431 and 579 map writes). The
prototypes' source revisions were not recorded; the merged policy is the one
`sys/arch/rp2040/doc/STORAGE.md` describes.

`board/hard-*.txt`: the same three kernels across a forced warm reset. The
file without `-after` holds the writes made before the reset, the `-after`
file the state `fsck -p` and the next boot left. These were recorded from
the raw terminal; each line is reduced to its final state, without the
escape sequences and the per-keystroke redraws of the line editor.

`board/swap/`: the erase count of each swap sector over 24 compiles, three
runs on main's kernel (`a706c0b8`, `STORAGE_STATS=yes`) with
`swap-wear-probe.patch` applied, which adds one counter per swap sector in
`flash_erase()` and prints 64 of them from `boot()`. The patch is the third
run's, which prints from sector 32; the first two runs printed from sector
0, 16 and 8 counters to a line. Each `runN.txt` ends with the lines that
reached the console before the board reset. The result is in
`sys/arch/rp2040/doc/research/flash-traffic-per-workload.md`.
