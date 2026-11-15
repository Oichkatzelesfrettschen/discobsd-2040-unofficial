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
is the target's. Trailing blanks were removed from every line; nothing else
was edited.

`board/arms/`: one file per kernel and root image, each a fresh flash
followed by `cc -o d d.c; sync` repeated.

| File | Kernel | Compiles |
|---|---|---|
| `B.txt`, `L-B.txt` | cleaner trigger of main before the change | 8, 24 |
| `G4.txt`, `L-G4.txt` | cleaning starts at a quarter of the live sectors | 8, 24 |
| `G5.txt`, `L-G5.txt` | cleaning starts at a fifth of the live sectors | 8, 24 |
| `T.txt` | trim at free (branch `rp2040/trim-freed-blocks`) | 8 |
| `A-B.txt`, `A-G4.txt` | as `B` and `G4`, root image with the in-memory assembler and linker streams | 24 |
| `A-F.txt` | the merged trigger, same root image | 24 |

`python3 board/arms/sum.py FILE...` prints writes, erases and seconds per
compile for each file; `table.py` prints the per-step differences. `sum.py`
divides by the number of counter intervals, which in the 8-compile files
includes the two file write and delete steps (`n=10`).

`board/wl*.txt`: workload traffic before the checkpoint change (`wl.txt`,
`wl-cc.txt`, `wl-cc3.txt`, `wl-base.txt`: one checkpoint per map write) and
with two prototypes that group checkpoints (`wl-p3.txt`, `wl-ab.txt`:
12 and 13 checkpoint attempts against 431 and 579 map writes). The
prototypes' source revisions were not recorded; the merged policy is the one
`sys/arch/rp2040/doc/STORAGE.md` describes.

`board/hard-*.log`: the same three kernels across a forced warm reset. The
file without `-after` holds the writes made before the reset, the `-after`
file the state `fsck -p` and the next boot left. The logs are raw terminal
output and keep the line editor's escape sequences and character echo.
