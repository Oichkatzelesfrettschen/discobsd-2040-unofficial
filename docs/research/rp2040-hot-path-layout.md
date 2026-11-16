# RP2040 kernel hot-path layout and the XIP cache

This note records the investigation of the pipe-throughput change that
follows from compiling the swap path at -O2 while the rest of the kernel
stays at -Os, and the layout mechanism that removes it. Predictions are
recorded before each measurement and kept as written; deviations are
findings.

## Observation

UnixBench `pipe` (one process writing and reading 512 bytes through one
pipe, no context switch) on the bare Pico, GCC 14.2.1, base 4b3eaf6c, rates
per second over three alternated passes that repeat within 0.02%:

| Kernel | all -Os | swap path -O2 | change |
|---|---|---|---|
| PICO | 21,201 | 23,712 | +12% |
| PICOCALC_PSRAM (union) | 23,592 | 20,958 | -11.2% |
| PICO, whole kernel -O2 | | 13,391 | -37% |

The swap-path files (heatshrink_encoder, hsx_decoder, hsx_stream, swapram,
swapram_pool, flash_swap, vm_swap, vm_swp, swap) run nowhere on the pipe
path. The benchmark session placed the syscall and pipe path in SRAM and
measured syscall -6.2% and pipe -5.9% with 44 long-branch veneers.

## Governing facts

- XIP cache, RP2040 datasheet 2.6.3.1 (page 122): 16 KB, two-way
  set-associative, one-cycle hit; 2.6.3.2 gives 8-byte lines and 1,024 sets
  (16 KB / 8 B / 2 ways). The set index is therefore address bits [12:3]:
  two flash addresses compete for one set exactly when they are equal
  modulo 8 KB after rounding down to 8 bytes, and three or more hot lines
  in one set cannot all stay resident. The datasheet names no replacement
  policy.
- Counters: XIP CTR_HIT (0x1400000c) and CTR_ACC (0x14000010) are 32-bit
  saturating counters (datasheet tables 158-159), exported as
  `machdep.xip_hit` and `machdep.xip_acc` (sys/arch/rp2040/rp2040/sysctl.c,
  hwcount_sysctl); a write clears them. `machdep.xip_cache=0` turns the
  cache off (flash_xip_cache in dev/flash.c).
- boot2 programs the SSI divider 2 (62.5 MHz SCK at 125 MHz) and
  continuous-read quad I/O, so a miss costs one QSPI transaction of eight
  data bytes.
- The kernel links with -ffunction-sections and --gc-sections; kern.ldscript
  places `*(.text*)` in input order, so a size change in one object moves
  every function linked after it.

## Static evidence from the benchmark images

Images: the benchmark session's union stock (restore-union.elf, the ELF of
unix-U-stock.uf2, sha256 2078468b...) and union swap-path -O2 (union-sel.elf),
and PICO -Os / swap-path -O2 (unix-PICO-Os.elf, unix-PICO-sel.elf).
Commands: `arm-none-eabi-nm -S -n` and `arm-none-eabi-objdump -d
--no-show-raw-insn` with addresses normalized to symbols.

- Outside the nine swap-path objects every function is instruction-identical
  between stock and swap-path -O2 in both configurations; the only
  differing words are literal-pool constants holding addresses of moved
  objects. No veneer, literal-pool distance or instruction-count change
  reaches the pipe path.
- Kernel RAM (.data, .bss, pool, u area) sits at identical addresses in each
  pair, so data-side SRAM bank placement does not differ.
- Most of the pipe path (rwuio, readp, writep, rwip, bmap, getblk, bread,
  uiomove, wakeup, userret) keeps its address. The objects linked after the
  swap path move: SVC_Handler, PendSV_Handler, syscall, copyin, copyout,
  bcopy, baduaddr and SysTick_Handler move by 0x3ec (1,004 bytes, 4 mod 8)
  in the union kernel and by 0x380 (896 bytes, 0 mod 8) in PICO. .rodata
  (syscall_handlers, Fops, cdevsw) moves as well.

## Hypotheses, each with its falsifier

H1, set conflict. The moved functions land on cache sets that other pipe-path
lines already fill twice, so each iteration evicts and refetches lines.
Falsifier: on the board, the miss count (CTR_ACC - CTR_HIT) per pipe
iteration does not rise from union stock to union swap-path -O2 by the amount
the time difference requires; or, with the cache off, the -11% persists.

H2, line phase. The 4-mod-8 shift changes how many 8-byte lines each moved
function and loop spans. Separated from H1 by layout-only arms that shift by
4 bytes (phase only) and by a multiple of 8 KB-aligned amount that keeps
sets (not affordable in text) or a shift that changes sets but not phase.

H3, veneers and literal distance. Retired by the instruction comparison
above.

H4, SRAM data bank contention. Retired by identical RAM addresses.

H5, flash read path. A different number of lines per moved function would
show as a change in CTR_ACC per iteration; falsified if CTR_ACC per
iteration matches between the two kernels within 1%.

## Predictions before the board run (recorded 2026-11-16, before measuring)

Time difference per pipe iteration: 125e6/20,958 - 125e6/23,592 = 666
cycles. With a miss costing 50-70 cycles at SCK = 62.5 MHz:

- P1: misses per iteration rise by 9 to 14 from union stock to union
  swap-path -O2.
- P2: CTR_ACC per iteration matches within 1% (same instruction stream).
- P3: with `machdep.xip_cache=0`, the swap-path/stock pipe ratio lies
  within 0.98 to 1.02.
- P4: syscall (-0.7%) shows a miss rise of at most 1 per call.

## Board run 1: XIP counters on the benchmark union kernels (2026-11-16, 17:40Z-17:50Z)

Bare Pico on USB port 1-3, hostname pico, root fs-benchsuite (4b3eaf6c), images
unix-U-stock.uf2 and unix-U-sel.uf2 of the benchmark session, two alternated
passes. Each counter row clears `machdep.xip_acc` and `machdep.xip_hit`, runs
the program and reads both; per-iteration values subtract the 1-second run from
the 11-second run, which cancels exec and exit.

| Kernel | pipe/s (11 s) | misses per pipe iteration | XIP accesses per iteration | syscall/s | misses per syscall iteration |
|---|---|---|---|---|---|
| union stock | 23,590 / 24,157 / 23,568 / 24,423 | 0.51 / 0.41 / 0.51 / 0.41 | 1,917 / 1,898 / 1,919 / 1,861 | 44,175 / 44,176 | 0.017 / 0.016 |
| union swap-path -O2 | 21,013 / 21,378 / 20,945 / 21,376 | 14.30 / 14.20 / 14.30 / 14.22 | 1,888 / 1,875 / 1,900 / 1,879 | 43,155 / 43,141 | 0.448 / 0.451 |

- P1 holds: misses rise by 13.8 per iteration (predicted 9 to 14). The time
  difference, 125e6/21,013 - 125e6/23,590 = 650 cycles, gives 47 cycles per
  miss, one QSPI transaction at SCK = 62.5 MHz.
- P2 holds within 1.6%: accesses per iteration overlap between the kernels.
- P4 holds: syscall gains 0.43 misses per call, 20 cycles of 2,830, the
  measured -0.7%.
- P3 is falsified, in the other direction: with `machdep.xip_cache=0`, ub-pipe 4
  ran 312 and 332 loops on the stock kernel and 4,859 on the swap-path -O2
  kernel. The stock figure implies about 830 cycles per XIP access, against
  54 for the -O2 kernel and about 50 for one QSPI transaction, so the stock
  kernel spends most of the uncached window outside the pipe loop. This
  anomaly is separate from the cached loss (whose sign it opposes) and stays
  open: hypotheses are swap work during the window, which -O2 speeds up
  (falsified if the hot-region kernels split by layout rather than by swap
  flags), and a layout-dependent uncached cost (falsified if they split by swap
  flags).

## Instruction traces and the cache model

`trace_run.py` (beside this note) boots the union kernel with a UART0
descriptor under Renode 1.17.0 from the real boot ROM, logs in, starts
`/usr/bin/ub-pipe 60` from the benchsuite root, and after 4 virtual seconds
records 0.02 virtual seconds of program counters and memory accesses: 866
pipe iterations, 2.48 million events. `ub-syscall 60` gives a second trace of
1,776 iterations. `xipsim.py` replays the instruction fetches (one 32-bit word
per new aligned word) and the XIP data reads through a 1,024-set, two-way,
8-byte-line cache, relocating each address input section by input section
through the link map of another layout, under LRU, FIFO and random
replacement.

| Layout (union) | misses per pipe iteration, LRU / FIFO / random | board |
|---|---|---|
| stock | 0.56 / 0.56 / 0.53 | 0.41-0.51 |
| swap path -O2 | 10.09 / 10.17 / 7.91 | 14.2-14.3 |
| all -Os, 4-byte pad after vm_swap.o | 0.57 / 0.57 / 0.54 | |
| all -Os, 1,000-byte pad | 3.91 / 3.96 / 2.95 | |
| all -Os, 1,004-byte pad | 7.09 / 7.18 / 5.92 | pending |
| all -Os, 1,008-byte pad | 4.03 / 4.10 / 3.82 | |
| pads after the last text object (rodata shift only) | 0.56 | |

The pipe path executes 37 input sections, 6,043 bytes; the syscall
benchmark adds 8 sections. In the swap-path -O2 layout the conflicts sit in
two sets: set 484 holds rwip+0x178, the line after dumpsys (the start of the
baduaddr helpers in machdep.o) and the `nsysent` word of init_sysent.o;
set 486 holds rwip+0x188, baduaddr_span+0xc and `syscall_handlers`+0xc. Each
system call touches all three lines of each set, so two-way LRU misses three
times per set per call: 6 and 3 misses per iteration. The PICO configuration cannot be replayed whole through this
trace: rwip, writep and pipe_rw have other sizes there, so 444,258 of the
2,457,675 traced XIP events (18.1%) have no counterpart and `xipsim.py` now
names them and exits 1 unless `--allow-omitted` is given. On the remaining
81.9% the PICO layouts give 6.62 misses for stock, 0.53 for swap path -O2 and
0.34 for both hot-region kernels; rwip is one of the conflicting sections, so
these figures neither bound nor predict the PICO board counts, and the PICO
conclusions rest on the board counters alone (13.74 misses per iteration
stock, 1.44 swap path -O2). Every union replay cited here relocates all
2,457,675 events with none omitted, and the virtual pads move sections without
resizing them, so the union figures and the set analysis are unchanged.
Calibration of the omission report: the union stock map replays with no
omission (exit 0); the same map with `.text.rwip` one word longer omits
exactly rwip's 360,256 events, 14.7% (exit 1).

Evidence classes: the cause, the pad experiment, the repair and every
before/after figure rest on board measurements (rates and XIP counters). The
replay supplies only the identity of the conflicting sets and lines and the
ranking of union layouts, which the counters confirm in sign and order.

The model undercounts the board by about 4 misses per iteration on both
conflicting union layouts (swap path -O2: 10.1 modeled, 14.3 measured; pad:
7.1 and 11.4) and by none on the clean ones, so hot lines the trace does not
reach (the USB interrupt the emulator lacks, or board-only paths) fall into the
same sets; the ranking of layouts and the sets named above stand.

H2 is refuted as a separate cause: a 4-byte shift changes nothing, while
shifts that move whole lines between sets reproduce the loss. H1 is the cause:
three hot lines one way apart in one two-way set.

## Predictions before board run 2 (recorded before the hot-region passes)

- The hot-region kernels miss at most 0.6 times per pipe iteration in both
  configurations, with and without swap-path -O2 (model 0.48 union, 0.34
  PICO), and run pipe at or above the faster of stock and swap path -O2.
- The all -Os pad kernel (1,004 bytes after vm_swap.o, every hot text address
  equal to the swap-path -O2 kernel's) misses about 10 times per iteration
  (model 7.1 scaled by the measured 14.3/10.1) and runs pipe near 21,700/s.
- Context switch and spawn move with swap-path -O2 (+48% to +50%) and not
  with the hot region; syscall stays within 1% of stock.

## Board run 2: hot region, swap path -O2 and the layout-only arm (17:52Z-18:37Z)

Same board and root; kernels built from this branch at GCC 14.2.1 on the
benchmark base (every kernel source identical to 4b3eaf6c; symbol addresses
equal to the benchmark images). Three alternated passes, two for the pad arm;
each program runs 10 s. Misses per iteration come from counters cleared before
`ub-pipe 10` and include its exec. Rates are per second.

| Kernel | pipe | pipe passes | misses/iter | syscall | context1 | spawn |
|---|---|---|---|---|---|---|
| union stock | 23,870 | 23,658 / 23,793 / 24,158 | 1.10 | 44,177 | 3.37 | 3.23 |
| union swap path -O2 | 21,271 | 21,271 / 21,272 / 21,271 | 14.92 | 43,848 | 4.97 | 4.53 |
| union hot region | 23,959 | 23,856 / 23,767 / 24,255 | 0.61 | 44,177 | 3.37 | 3.27 |
| union hot region, swap path -O2 | 24,255 | 24,256 / 24,255 / 24,255 | 0.54 | 44,177 | 4.97 | 4.43 |
| union all -Os, 1,004-byte pad | 21,586 | 21,659 / 21,513 | 11.43 | 43,848 | 3.40 | 3.30 |
| PICO stock | 21,222 | 21,530 / 21,044 / 21,092 | 13.74 | 44,176 | 3.37 | 3.30 |
| PICO swap path -O2 | 24,082 | 24,082 / 24,082 / 24,082 | 1.44 | 44,175 | 4.93 | 4.30 |
| PICO hot region | 23,945 | 24,283 / 23,784 / 23,769 | 0.72 | 44,175 | 3.27 | 3.23 |
| PICO hot region, swap path -O2 | 24,283 | 24,282 / 24,283 / 24,284 | 0.61 | 44,175 | 4.97 | 4.17 |

Spawn varies 4.0 to 4.9 between passes of every -O2 kernel, and the -Os
kernels' pipe rate varies by up to 2% between passes while the -O2 kernels
repeat to one loop; neither spread follows the hot region.

The 0.34 PICO model figure above came from the truncated PICO replay
described under the cache model and carries no weight.

Against the predictions: the hot-region kernels miss 0.54 to 0.72 times per
iteration (predicted at most 0.6 from the differential method; these figures
include exec). With swap path -O2 they run pipe above the faster of the two
input-order layouts in both configurations (24,255 and 24,283 against 23,870
and 24,082). The -Os hot-region kernels sit within the -Os pass-to-pass spread
of the faster layout rather than above it: union 23,959 against stock 23,870,
PICO 23,945 against swap path -O2 24,082, with -Os passes 23,769 to 24,283. The pad arm misses 11.4 times per iteration and runs
21,586/s (predicted about 10 and 21,700): every file at -Os, one 1,004-byte
pad, and the loss returns. Syscall, context switch and spawn follow the
predictions.

## Cause

Three hot lines one cache way apart share one two-way set and evict each other
on every system call. The XIP cache maps addresses equal modulo 8 KB to one
set (datasheet 2.6.3.1-2.6.3.2); the input-order layout placed rwip
(sys_inode.o) where it stays, while the machdep.o address checks, the system
call entry and the init_sysent tables move with every byte the objects linked
before them gain or lose. Swap path -O2 adds 1,004 bytes before machdep.o in
the union kernel, which lands two pairs of those lines on rwip's sets: 14.3
misses per pipe iteration on the board against 0.4-0.5, at 47 cycles each, the
whole 11% loss. In the PICO kernel the stock layout already held such sets
(13.7 misses) and the -O2 shift cleared them, which is the +12%. The
deciding experiment is the pad arm: the stock -Os objects with 1,004 bytes of
padding after vm_swap.o put every hot text address where swap path -O2 puts
it, and pipe loses 9.6% with 11.4 misses per iteration. The repair removes it
with a layout-only change while keeping swap path -O2: the union -O2 kernel
with the hot region misses 0.54 times and runs 24,255/s, 14% above the same
objects in input order.

## Repair and cost

`conf/kern.ldscript` opens .text with the input sections the two traces
execute, in path order, followed by the read-only tables they load
(`nsysent`, `syscall_nargs`, `syscall_handlers`, `Fops`, `pipeops`):
6,264 to 6,520 bytes depending on the configuration, under one 8 KB way, so
each of its lines owns a set and its addresses no longer depend on the size of
any other object. GCC 14.2.1 text: PICO +8 bytes (100,819 to 100,827),
PICOCALC +8, PICO_UART 0, PICOCALC_PSRAM 0, union swap path -O2 0, PICO swap
path -O2 +8; RAM unchanged; long-branch veneers unchanged (9 union, 10 PICO).
`tools/check_hot_layout.py` (`check-hot-layout`, with its validator) fails a
kernel whose span exceeds one way, whose listed symbols or their compiler
clones lie outside it, or whose listed lines share a set.

The list covers the system call, pipe, `close(dup())`/getpid/getuid/umask and
clock-tick paths that the traces execute. A path the list omits keeps its
input-order placement; the context switch and spawn paths run in the swap
codec and stay outside, and the cache model and the counters are the tools for
extending the list.

## Open: throughput with the XIP cache off

With `machdep.xip_cache=0`, ub-pipe 4 ran 78 to 214 loops per second on -Os
kernels (stock 78, 83, 214; hot region 98) and 367 to 1,215 on swap-path -O2
kernels (1,204, 1,215; hot region 367). About 1,900 XIP accesses per iteration
at about 54 cycles each predict 1,200/s, which only some -O2 runs reach, so the
other runs spend most of the window elsewhere. The split follows neither
layout nor swap flags cleanly. Untested hypothesis: a periodic daemon (update) wakes
inside some windows and forces a swap round trip of ub-pipe, which uncached
takes seconds at -Os; the same phase effect would explain the 2% pass-to-pass
pipe spread of the cached -Os kernels. Falsifier: cache-off runs with update
stopped, or with the swap counters read around the window, keep the spread.

## Board session

Taken 17:37:42Z (identity: hostname pico, PICOCALC_PSRAM#1), flash saved
17:39Z (2 MiB, sha256 d7aaf62d922a318109b129cbb860e7be28902b6778434526fb752733f884a971),
released 18:39:26Z after restoring origin/main b3f293a6: union kernel with the
PICO descriptor (restore-unix.uf2, sha256 a8d883dc912dc3693a8ebf095da60fae371d9e88dab49de80d5d7792ae0445a3)
and root (restore-flash.uf2, sha256 1fc5b15ceda66647cdff8154eb145396d16710329b708c3747ae41922e46ed23),
both loaded in BOOTSEL and `picotool verify` OK before the first boot; the
boot check reads PICOCALC_PSRAM#1, hostname pico, 72 /usr/bin entries. No
filesystem benchmark ran.
