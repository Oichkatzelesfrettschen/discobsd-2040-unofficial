# macOS host tier profile and split

Hosted CI ran `check-host` on macOS as one step under a 1,500 s hard limit.
Steps measured 1,354, 633, 1,349 and 1,225 s (runs 43813452020, 43808560953,
43808015893, 43804666980; median 1,286 s) and run 43820008555 reached the limit
while `usr.bin/calc` tests were still running. The owner chose to profile, then
split: move the slowest independent gates into a second macOS step with its own
budget and keep each limit near 1.5 times its median.

## What the retained logs resolve

The retained `macos-host.log` of each run carries no per-gate timestamps. It
carries a `[logged-gate] heartbeat` line every 30 s and a `--- target ---`
header whenever `bmake -j3` switches the output stream to another target. A
gate's macOS seconds below are an attribution: each 30 s heartbeat bucket is
divided equally among the targets that printed output in it. The method has a
resolution of 30 s, credits a quiet gate only with the moments it printed, and
leaves sub-make targets (`test-play`, `test-mutants`, `check`, per-test
object headers) as their own headers. Across the four completed runs the host
gates account for 321 to 685 s of the elapsed time; sub-make headers hold the
rest. The column is therefore a lower bound that ranks the output-heavy gates
(the calculator, FAT32, libcalc, ridge, fourfall, v7fs and msdosfs suites) and
cannot rank a gate that computes quietly.

A second, clean measurement supplies the cost of every gate: `bmake
MACHINE=rp2040 <gate>` run one gate at a time on a loaded Linux x86-64 WSL2 host
(host cc GCC 15.2.0, Python 3.14.4, tree at the base commit, built first), so
the column is user plus system CPU seconds rather than wall time. The 145
host gates sum to 2,741 CPU seconds. Two exit nonzero on that host, for reasons
the hosted jobs do not share: `check-sort-host` fails its C-locale oracle for
`-d` and `check-cpio-host` finds no host `cpio`, so their times are not
meaningful. Four gates skip most or all of their work on macOS and are weighted
at 0 to 10 percent when balancing: `check-puzzlebox-host` (needs GNU ld),
`check-dcbc` (Linux only), `check-fsck-inode-range` (ILP32) and
`check-as-ld-scratch` (strace).

## Per-gate table (seconds)

| gate | macOS median | macOS max | run 43820008555 (limit reached) | Linux CPU | step |
| --- | --- | --- | --- | --- | --- |
| `check-libc-host-contracts` | 6 | 8 | 12 | 254 | A |
| `check-libcalc` | 52 | 68 | 61 | 197 | B |
| `check-ridge-host` | 47 | 56 | 74 | 190 | B |
| `check-calc-host` | 110 | 123 | 55 | 164 | B |
| `check-v7fs-core` | 21 | 31 | 32 | 139 | B |
| `check-libtcl-contracts` | 2 | 3 | 3 | 114 | A |
| `check-fat32-tail` | 63 | 77 | 92 | 104 | B |
| `check-kernel` | 2 | 3 | 4 | 98 | A |
| `check-storage-recovery` | 5 | 8 | 5 | 89 | B |
| `check-architecture-isolation` | 2 | 2 | 2 | 80 | A |
| `check-archive-stored-times` | 4 | 5 | 5 | 66 | A |
| `check-picocalc-host` | 2 | 3 | 4 | 58 | B |
| `check-fourfall-host` | 44 | 52 | 39 | 58 | A |
| `check-msdosfs-core` | 20 | 29 | 33 | 56 | B |
| `check-psram-pio` | 1 | 2 | 2 | 51 | B |
| `check-flash-reset-cells` | 0 | 3 | 7 | 38 | A |
| `check-picofetch-host` | 13 | 18 | 14 | 36 | A |
| `check-lex-tables` | 2 | 2 | 3 | 34 | B |
| `check-compiler-matrix-harness` | 5 | 5 | 5 | 31 | A |
| `check-coil-host` | 13 | 18 | 13 | 26 | B |
| `check-c17-source-denominator` | 5 | 8 | 6 | 25 | A |
| `check-tar-host` | 5 | 8 | 8 | 22 | B |
| `check-storage-recovery-models` | 6 | 8 | 10 | 20 | A |
| `check-dcbc` | 1 | 1 | 1 | 182 | B |
| `check-bubble-host` | 4 | 5 | 6 | 16 | A |
| `check-libbn` | 1 | 1 | 1 | 14 | B |

## The split

`HOST_TIER_B_GATES` in the root Makefile names 15 gates (calc-host, libcalc,
libbn, ridge-host, v7fs-core, fat32-tail, storage-recovery, picocalc-host,
msdosfs-core, psram-pio, lex-tables, coil-host, tar-host, storage-correctness,
dcbc). `HOST_TIER_A_GATES` is every other host gate, computed with a
`.for` loop, so `check-host-a` and `check-host-b` partition `check-host`
by construction. The Linux job still runs `check-host` whole.

By Linux CPU with the skip weights applied, tier A carries about 1,106 s and
tier B about 1,176 s, an even split. The predicted macOS medians are therefore
about 640 s per step (half the 1,286 s whole-tier median) and each hard limit is
960 s, 1.5 times that, with a 300 s idle limit unchanged and an 18 minute step
deadline (the 960 s hard limit plus the 120 s that the former 27 minute step
kept above its 1,500 s). The prediction is unconfirmed until firmware.yml runs
on the branch; a step median far from 640 s moves gates between the lists and
changes the limit with it.

`check-host-split` (`tools/check_host_tier_split.py`) rejects a gate in
neither tier, in both, an unknown name in either list, an empty tier and a
duplicate, and its `--selftest` runs each of those as a mutant first.
`check-build-observability` pins the labels `macos-host-a` and
`macos-host-b` and their commands.
