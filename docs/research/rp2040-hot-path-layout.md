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
