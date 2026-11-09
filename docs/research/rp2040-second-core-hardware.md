Every figure carries its evidence class and nothing here has run on a board.

# RP2040 core 1: what the hardware offers and where the port spends core-0 time

## Result

- Core 1 is idle hardware the port can launch with a six-word FIFO handshake (datasheet 2.8.2). It shares XIP flash with core 0, so it runs from SRAM or it stalls whenever core 0 erases or programs flash. Core 1 cannot hide flash-write latency: `flash_erase()` and `flash_program()` in `sys/arch/rp2040/dev/flash.c` take the shared XIP interface down for every reader.
- The largest core-0 cost found is the packed-executable decoder, and single-core changes (a table CRC, a word-buffer bit reader) plausibly remove more than half of it. An instruction-level emulation of the shipped decoder and CRC (method and limits in "Method and evidence classes") costs 387 cycles per expanded byte on a Cortex-M0+: 54 percent in the bit reader `get_bits()`, 22 percent in the decoder state machine, 20 percent in the bit-serial CRC-32. That is 144 ms per pass for a 46,540-byte image at 125 MHz. The sample, packer, harness and model are in `docs/research/rp2040-second-core-emulation/`, and its `build.sh` rebuilds them from tree sources and reproduces these figures. `exec` runs two passes per stream and every swap-in of a packed process runs one more over its whole text, so the model puts exec of a 40 KB box in the hundreds of milliseconds, against "tens of milliseconds" in `sys/arch/rp2040/doc/STORAGE.md` and 7-22 ms in `docs/research/ram-compression.md`. The deviation is the finding; the board baseline below decides it.
- A second core cannot shorten a serial decode that core 0 waits on. Offloading one stream to core 1 gains nothing unless core 0 has other work or the stream is split. Two measurements precede any core-1 design: the board cost of the decode, and the single-core ceiling (table CRC, a word-buffer bit reader, hardware divide). Core 1 earns its place after those, as a splitter of independent segments, as a display and I/O service on the PicoCalc, and as a diagnostic sentinel for the unexplained console wedge.
- The port keeps nothing on core 1 today. `sys/arch/rp2040/dev/usb.c` states "core 1 is never launched" and relies on it. A core-1 design collides with five existing claims: the 8 KB USB transmit ring that fills SRAM4 and SRAM5, `AIRCR.SYSRESETREQ` as the reboot path (it resets only the requesting core, so core 1 survives a software reboot), the divider ownership gate, the MPU region that grants user mode only the divider, and a flash budget with about 22 KB left on PICO.

## Method and evidence classes

Source revision: worktree `rp2040-second-core`, HEAD `5b6e319aa0f86d9664156f1c71c2deb74d3179a3`, clean. Datasheet: `rp2040-datasheet.txt` in the notes repository (line numbers below refer to it; sections resolve through `sys/arch/rp2040/doc/DATASHEET-INDEX.md`). Pico SDK: tag 2.3.1, commit `079c6f39023649b154152db30f1d781e884879bc` (2026-09-04), shallow clone read for technique; no SDK code is reproduced and the files cited are BSD-3-Clause.

Each claim carries one class.

- **Observed (source):** read from the tree at the revision above.
- **Observed (documented):** stated in a datasheet section or a repository document; the document's own measured-or-derived label is kept.
- **Observed (board):** a retained board capture in the repository.
- **Observed (emulated):** produced here by running the shipped Thumb-1 code under Unicorn with a Cortex-M0+ cycle table. Output bytes and CRC match the input exactly, which calibrates correctness; cycle counts rest on the timing table, not on silicon.
- **Inferred:** derived from the above; the sentence names what would raise or lower the rating.
- **Not run:** no board, Renode or hardware test was executed for this report.

The emulation costs: ALU 1 cycle, LDR/STR 2, LDM/STM/PUSH/POP 1+N (+2 for POP into PC), branch taken 2 and not taken 1, BL/BX/BLX 3, MRS/MSR/DSB/ISB 4. It assumes instruction fetch hits the XIP cache (1 cycle, datasheet 2.6.3.1), no bus contention and no interrupt. Rating of the resulting cycle counts: medium, plus or minus 30 percent. A board timer measurement of the same call raises it; a deviation above 30 percent lowers it and becomes the finding. Calibration points: the hand count of the bit-serial CRC loop (9 cycles per bit, about 79 per byte) matches the model's 79.4; the decoder reproduces the input bytes and the zlib CRC-32 of the 46,540-byte sample. The sample is the flat image of a static newlib program linked for `-mcpu=cortex-m0plus` (text 40,708 bytes), packed with the vendored encoder at window 9 and lookahead 3, ratio 0.784. It stands in for a DiscoBSD a.out text stream; it is not one. The appendix gives the recipe, the input hashes and the recorded run output; the files are in `docs/research/rp2040-second-core-emulation/`.

## Hardware, from the RP2040 datasheet

### Launching core 1

Both cores leave reset together and enter the boot ROM; core 1 reads `SIO.CPUID`, parks in `WFE` with `SCR.SLEEPDEEP` set, and waits on its inbound FIFO (2.8.1, lines 9496-9520). The launch handshake (2.8.2, lines 9625-9680) is a lockstep exchange over the two FIFOs. Core 0 sends the sequence 0, 0, 1, VTOR value, stack pointer, entry address (Thumb bit set) and treats the echoed word as the acknowledgement; any other reply restarts the sequence. Before each 0 it drains its read FIFO and executes `SEV`, because core 1 may be waiting on FIFO space. The procedure is valid at any time after core 1 reset, including a core 1 that was reset alone through `PSM.FRCE_OFF.PROC1` (2.13, lines 12404-12420). The SDK implements it in `multicore_launch_core1_raw()` and brings core 1 back to the parked state with `multicore_reset_core1()`, which sets and then polls `FRCE_OFF.PROC1` (`src/rp2_common/pico_multicore/multicore.c`).

Core 1 starts at the supplied entry with the supplied stack and VTOR. The SDK passes core 0's own VTOR, so core 1 shares the vector table; this port needs a separate table in SRAM (below).

### Inter-core FIFOs and events

Two FIFOs, 32 bits wide and 8 deep, one per direction. `FIFO_ST` (SIO offset 0x050) reports VLD, RDY and the sticky ROE and WOF errors; `FIFO_WR` is 0x054 and `FIFO_RD` is 0x058 (2.3.1.4, lines 2080-2120). Each core's FIFO interrupt is the OR of VLD, ROE and WOF: IRQ 15 (`SIO_IRQ_PROC0`) conventionally for core 0 and IRQ 16 (`SIO_IRQ_PROC1`) for core 1, because a core cannot read the other core's `FIFO_ST` (2.3.2, lines 4331-4380). `WFE` and `SEV` are cross-wired between the cores with a sticky event flag (2.3.3, lines 4401-4418). The kernel's vector table already carries `SIO_IRQ_PROC0_Handler` and `SIO_IRQ_PROC1_Handler` defaults (`sys/arch/rp2040/rp2040/locore0.S` lines 252-253). The FIFOs carry the boot ROM handshake, so a protocol on top starts with both FIFOs drained.

### Hardware spinlocks

Thirty-two one-bit locks at SIO `SPINLOCK0`-`SPINLOCK31` (0x100 and up): a read claims (nonzero on success), any write releases, core 0 wins a same-cycle tie, `SPINLOCK_ST` (0x05c) shows all 32 (2.3.1.3, lines 2053-2078). The datasheet in the notes repository lists no SIO spinlock erratum; its RP2040-E2 entry is the USB abort bug. The kernel uses none. The 2.11BSD kernel guards state with `spl`, and on ARMv6-M every blocking `spl` collapses to `PRIMASK`, which masks the local core only (`sys/arch/rp2040/include/intr.h`). A structure shared with core 1 needs a spinlock, or a single-writer ring that needs none.

### Divider and interpolators: per-core state

Each core has its own 8-cycle signed and unsigned divider and two interpolators (2.3.1.5, lines 2121-2202; 2.3.1.6, lines 2203-2465). Divider result registers are writable so interrupt code can save and restore a division in flight; `DIV_CSR.DIRTY` marks one. Neither core can see the other's divider, so a core-1 image can divide without touching the state user processes rely on. Blend mode exists on INTERP0 only (2.3.1.6.2) and clamp mode on INTERP1 only (2.3.1.6.3), each on lane 0 of its interpolator, so a routine that blends and clamps uses both interpolators of the core.

### NVIC, SysTick and the MPU: per core

"Each core is equipped with a standard ARM NVIC" with the same 26 connected interrupts routed to both, except GPIO bank interrupts which are per core (2.3.2). SysTick, SCB, VTOR and the MPU sit in each core's private peripheral bus: eight MPU regions per core (2.4.1.2, line 4561; 2.4.6, lines 5477-5527). Core 1 therefore powers up with its MPU disabled, no vector table, and SysTick stopped. Kernel MPU programming in `sys/arch/rp2040/rp2040/mpu.c` configures core 0 only.

Reset scope matters here. The kernel reboots with `AIRCR.SYSRESETREQ` (`machdep.c` lines 748-776). The datasheet states the NVIC path "only resets the Cortex-M0+ processor core" while the Power On State Machine resets the subsystem (2.4.2.9, lines 4893-4906, observed documented). A running core 1 therefore survives a software reboot, keeps executing SRAM that the next kernel boot copies over and zeroes, and must be stopped through `PSM.FRCE_OFF.PROC1` at the top of `boot()` and in the panic and fault paths. The same step belongs at kernel startup, because a previous kernel's core 1 may still run. Inferred from the documented scope and the call site; a board reboot with a parked core 1 counter would confirm it.

### Bus fabric and SRAM

Four masters (core 0, core 1, DMA read, DMA write) reach ten crossbar slaves in parallel; contention arises only when two masters address the same slave in the same cycle, resolved by a round-robin tie inside a priority class (2.1.1, lines 888-936). `BUSCTRL.BUS_PRIORITY` (base 0x40030000, offset 0) sets PROC0, PROC1, DMA_R and DMA_W high or low; every field resets to low (lines 1189-1215). A high-priority master "will never observe any slowdown" from low-priority ones on a zero-wait-state slave (line 955). Setting PROC0 high makes core 0 immune to core 1 at the cost of core 1 stalling instead. The four 64 KB banks are word-striped across 0x20000000-0x2003ffff (Table 153, lines 8908-8935), so two cores walking different buffers collide on one access in four on average if both issue an SRAM access every cycle; a Cortex-M0+ issues at most one data access per instruction plus the instruction fetch, so the realistic penalty is a few percent (inferred; the bus performance counters selected through `BUSCTRL.PERFSEL`, lines 970-1060, measure it directly: SRAM0-SRAM3 contested versus total). SRAM4 and SRAM5 (0x20040000, 0x20041000, 4 KB each) map directly to single banks and can hold a core's stack and hot code without contention (2.6.2, line 8940). The 16 KB XIP cache is addressable as SRAM at 0x15000000 only with the cache disabled (2.6.2.1, 2.6.3.1), which the port cannot afford with kernel text in XIP.

### XIP flash while core 0 erases or programs

The boot ROM flash helpers `connect_internal_flash`, `flash_exit_xip`, `flash_range_erase`, `flash_range_program`, `flash_flush_cache` and `flash_enter_cmd_xip` (Table 166, lines 9842-9900) take the SSI out of XIP mode. The datasheet states the code calling them "must be located in SRAM" because "the SSI is not in a state where it can handle XIP accesses" in between. The datasheet defines no result for a fetch or load from any flash address, by either core, during that window, including a vector fetch whose table lives in flash. Inferred: cache hits may still return data and misses do not, and no software controls which accesses hit. A cache flush "whilst another core may be executing code from flash" is safe but stalls that core until the flush ends (2.6.3.2, lines 9057-9070).

The SDK's answer is lockout: `multicore_lockout_victim_init()` on core 1 installs a FIFO-IRQ handler marked `__not_in_flash_func` that, on a request word, disables interrupts and spins in `WFE`; `multicore_lockout_start_blocking()` on core 0 sends the request and waits for the echo before the flash call (`pico_multicore/multicore.c`, `pico_flash/flash.c`). The alternative that needs no lockout is a core 1 that never touches flash: code, data, stack and vector table in SRAM or ROM, every exception vector pointing at SRAM code. ROM is a separate slave and stays readable during the window (2.6.1, line 8865).

`flash.c` in the port already follows the SDK shape: `__ramfunc` erase and program, a copied boot2 to re-enter XIP, `splhigh()` for the whole operation (lines 296-340).

## What the port already claims in the SIO, SRAM and MPU

| Resource | Owner today | Evidence | Consequence for core 1 |
| --- | --- | --- | --- |
| SIO divider, offsets 0x060-0x07f | user mode through the boot ROM float routines; kernel excluded | `mpu.c` region 3, `tools/verify_rp2040_divider_ownership.py`, `float-libs.md` 4.1 | Core 1's divider is separate state. A core-1 image is linked outside the kernel gate or the gate learns the image. |
| SIO CPUID, FIFO, spinlocks, interpolators | closed to user mode (MPU region 3 enables only subregion 3) | `mpu.c` lines 33-52, `MPU.md` | Kernel-only. A user-visible interface needs a new MPU grant or a syscall. |
| SIO GPIO set/clear/enable | kernel (`picocalc_reg.h`, `machdep.c`) | source | Atomic aliases; core 1 may toggle other pins safely (datasheet 2.3.1.2). |
| SRAM4 + SRAM5 (8 KB at 0x20040000) | USB transmit ring on PICO and PICOCALC; Dhara page, boot2 copy and program-page buffer on PICO_UART | `RP2040.ld`, `kern.ldscript` ASSERT | Full on USB kernels. PICO_UART leaves about 6.5 KB free (0x20040500-0x20041eff: 8,192 less 1,024 Dhara page, 256 boot2 copy, 256 program buffer, inferred from the layout; the linked map confirms). |
| USB DPSRAM tail (0x50100240, 3,520 bytes) | Dhara page, codec work, program-page buffer on USB kernels | `BOOT-MAP.md`, `kern.ldscript` | Execute-never peripheral region; data only. |
| Kernel RAM region (106 KB at 0x20024000) | data, bss, `.ramfunc`, SwapRAM pool (16 KB on PICO and PICOCALC) | `RP2040.ld`, `compile/PICO/Makefile` | The natural home of a core-1 image (the `__ramfunc` copy loop in `locore0.S` exists); its load image counts against kernel flash. |
| Watchdog scratch 0-2, 4-7 | swap cursor, interrupt-site diagnostics, boot ROM reboot state | `watchdog.h` | Launch uses FIFO only; no scratch collision. |
| DMA controller | unused (only default vector entries) | grep of `sys/arch/rp2040` | Free for the SPI and CRC candidates below. |
| NVIC IRQ 26 | PicoCalc renderer software interrupt | `PICOCALC.md`, `lcdrender.S` | Per core; core 1 has its own pending bit. |
| `AIRCR.SYSRESETREQ` reboot | `boot()` | `machdep.c` lines 748-776 | Does not reset core 1; add `FRCE_OFF.PROC1`. |
| Flash budget | PICO 108,480 of 130,816 text bytes; PICOCALC about 124,300 (caller figures; `PICOCALC.md` line 36 recorded 119,316 at an earlier revision) | caller, `PICOCALC.md` | PICOCALC has about 6.5 KB left; PICO about 22 KB; PICO_UART about 36 KB (`security-c17-frontier.md` line 1130, 36,504 at that revision). A core-1 image stored in kernel flash spends the same bytes twice, once in flash and once in SRAM. |

## Where core-0 time goes

### Packed executables: exec and every swap-in

Observed (source): `exec_hsaout_check()` expands the text and data streams twice, once into a discard buffer (preflight, while the old image is intact) and once into the user window after `exec_estab()` commits (`sys/arch/rp2040/rp2040/exec_hsaout.c` lines 145-190). `swapin()` calls `exec_text_restore()` for any process with clean text, and for a packed executable that expands the whole text stream again (`sys/kern/vm_swap.c` line 71, `sys/kern/exec_aout.c` lines 89-117). One process is resident at a time and a switch swaps (`RP2040.ld` comment), so each switch to a packed process pays one text decode.

Observed (source): `hsx_expand()` (`hsx_stream.c`) calls the vendored heatshrink decoder, compiled with window 9 and lookahead 3 (`hsx_decoder.h`), and folds `crc32_update()` over every expanded byte. `crc32_update()` in `sys/kern/subr_crc32.c` is bit-serial, and its header comment defers a table "until a measurement says the time matters". The vendored decoder reads input through `get_bits()`, which loops per bit.

Observed (emulated), 46,540 expanded bytes from 36,509 packed, `-O` as the kernel builds, cycle-model limits as above:

| function | cycles | share | cycles per expanded byte |
| --- | ---: | ---: | ---: |
| `get_bits` | 9,732,857 | 54.1% | 209.1 |
| `hsx_decoder_poll` | 3,902,551 | 21.7% | 83.9 |
| `crc32_update` | 3,687,316 | 20.5% | 79.2 |
| `bcopy` (decoder input copy) | 298,584 | 1.7% | 6.4 |
| `fill` (harness input copy) | 293,347 | 1.6% | 6.3 |
| everything else | 89,250 | 0.5% | 1.9 |
| total (one pass) | 18,003,905 | 100% | 386.8 |

At 125 MHz that is 144.0 ms per pass. The first 8 KB alone (more literals, ratio 0.889) costs 420.6 cycles per byte. The discard pass costs within 0.5 percent of the committing pass. Instruction count in `get_bits` is 6.57 million for 292,072 input bits, 22.5 instructions per bit.

Inferred (medium): a 40 KB-class multicall box costs about 125 ms per pass, so exec is about 250 ms of decode and CRC and each switch to that process about 125 ms, before file reads, which are word copies out of XIP and small beside the decode (the harness's 6.3 cycles per byte for the input copy is a lower bound for `rdwri()` overhead). `docs/research/ram-compression.md` (lines 176-190) estimated 5-30 cycles per byte by reasoning from operation counts; the emulation puts decode of literal-heavy Thumb text 13 to 77 times higher. Data segments that are mostly zeros decode faster per byte (long matches); the text stream dominates a box.

Board evidence on the consequence: the measured pipe throughput is about 5 KB/s, with a 31 KB piped `tail` taking 12.9 s against 6.2 s for the pipe alone (`docs/research/memory-ownership-plan.md` lines 211-213, observed board). The pipe buffer is 4 KB (`MAXPIPSIZ = NDADDR * MAXBSIZE`, `fs.h` line 127), so one writer-reader round trip moves 4 KB in about 0.8 s. Hypothesis, rated low until counted: that round trip is two context switches, each a swap-out plus a swap-in with a text restore. Falsifier: `STORAGE_STATS` swap counters and a timer around `swapin()` across one pipe round trip; if the swap-in share is under 30 percent of the 0.8 s the hypothesis fails.

### Flash swap writes and erases

Observed (board): in the stdio capacity workload the swap unit took 400 erase sectors and 5,894 program pages, and SwapRAM admitted 3 of 83 swap-out attempts (`docs/research/board-capacity-acceptance.md` lines 424-434); a second run took 445 sectors and 6,154 pages with 9 of 98 admissions (lines 491-496). So 80 of 83 swap-outs went to flash, about 5 sectors and 74 pages each. `flash_erase()` and `flash_program()` run with `splhigh()` (`flash.c` lines 303 and 329), which is `PRIMASK`, for each sector or page call.

Inferred (medium; the chip datasheet is not in the repository): at the 45 ms sector erase the repository already quotes (`swapram.h` line 8) each flash swap-out masks interrupts for about 5 x 45 ms plus about 74 page programs, so roughly a quarter second of core-0 stall per switch, in 45 ms uninterrupted blocks. SysTick pends one exception, so a masked 45 ms loses about 44 ticks of the 1 kHz clock; `docs/research/picocalc-console-review.md` lines 146-150 already counts the same effect for a 200 ms console scroll. Nothing in `machdep.c` or `clock.c` catches the lost ticks up. Falsifier: over a swap-heavy workload, compare the timer's `TIMERAWL` delta with the kernel's tick count.

Core 1 cannot shorten this: the erase runs inside the flash chip, and both cores stall on XIP meanwhile. What helps is fewer flash swap-outs (a larger SwapRAM pool, more admissions) or an erase that yields. The W25Q-series erase-suspend command would let XIP reads proceed between erase slices; the repository holds no chip datasheet, so that route is not verified.

### PicoCalc display

Observed (source): `lcd_draw_cell()` in `st7365p.c` pushes 6x10 pixels as three bytes each (RGB666) through SPI1 with `spi_put()`, which spins on `SSPSR.TNF` (lines 118-165). Observed (documented): a cell is 180 pixel bytes plus an 11-byte window, about 122 us of wire time at 12.5 MHz, so output is bounded near 8,000 characters per second "before CPU overhead", and "measured costs are owed to the board run" (`PICOCALC.md` lines 185-187). A kernel `putc` draws three cells (cursor hide, glyph, cursor show), about 366 us; a full-screen scroll adds a 6 ms row fill; a scrolling-region newline redraws up to about 200 ms (`PICOCALC.md` lines 164-172, `picocalc-console-review.md` N4). The renderer runs in IRQ 26 above thread priority and "holds off every process, system call and the PendSV context switch while it draws", at most 8 ms per pass (`PICOCALC.md` lines 138-143).

Inferred (high): because `spi_put()` waits for FIFO space, core 0 spends essentially the whole wire time inside the render loop; the instructions per pixel are a small fraction of the 1.92 us the three bytes take on the wire. The figure is not measured. Falsifier: toggle a spare GPIO around `lcd_draw_cell()` (the suggestion in `PICOCALC.md` line 261) and compare the width with 122 us.

### USB CDC, UART and keyboard

USB CDC is interrupt-driven through a 64-byte-endpoint controller with an 8 KB transmit ring; no throughput measurement exists in the tree (searched `docs` and `sys/arch/rp2040/doc`). The 3 ms-per-byte upload pacing in `docs/research/invariant-map.md` lines 232-236 is a host-side limit, not a capacity. During a masked flash operation the USB controller keeps its endpoint buffers and NAKs; the UART fallback has no such buffer beyond the PL011 FIFO, which (32-entry receive FIFO, datasheet 4.2, and 115,200 baud; inferred) overruns after about 2.8 ms, so a 45 ms erase can drop UART input. Falsifier: type at the UART console during a forced flash swap and count lost characters. The keyboard poll costs a small fixed callout every 16 ms idle or 2 ms active (`PICOCALC.md` lines 189-206, documented); its CPU cost is not measured and is small by construction.

### Integer division in user mode

Observed (source): `lib/libc/arm/gen/aeabi_div.S` implements `__aeabi_uidiv` and relatives as a shift-and-subtract loop; the hardware divider is used only inside the boot ROM float code. Observed (emulated): 56 cycles for 7/3, 87 for 99/10, 195 for 12,345/10, 286 for 1,000,000/10 and 458 for 0xffffffff/10; signed adds about 30. The divider needs about 8 cycles plus the call (SDK `divider.S`, datasheet 2.3.1.5), so a call costs roughly 20-25 cycles (inferred, not measured).

## Accelerators and libraries

### Divider

Used today only by the ROM float provider (`lib/libc/arm/gen/rom_float_*.S`). The ownership argument in `float-libs.md` 4.1 is single-owner: no kernel, interrupt, NMI or callout code divides, and a process switch cannot land inside a healthy user instruction sequence, so a quotient read after the dividend write is never interleaved. The SDK shows the portable protocol for the case where interrupts divide: save the state when `DIV_CSR.DIRTY` is set, read the quotient last (`hardware_divider/divider.S`, `pico_divider/divider_hardware.S` with `PICO_DIVIDER_DISABLE_INTERRUPTS`). `__aeabi_idiv` and `__aeabi_uidiv` in libc can use the same grant under the same invariant, which turns every `/` and `%` in userland (decimal conversion in `_doprnt`, `awk`, `bc`, `dc`, `expr`) from 56-458 cycles into about 20-25. The kernel stays excluded by the existing gate. A core-1 image keeps its own divider and is not constrained by it. The 20-25 cycle figure covers 32-bit `/` and `%` only; a 64-bit operand or a Q16.16 divide beyond the fast path of "Fixed-point arithmetic" below does not map onto one divider operation.

### Interpolators

Two per core, user-closed by the MPU. They shift, mask and sign-extend an accumulator and add bases in one cycle. INTERP0 adds blend mode (8-bit alpha between BASE0 and BASE1, 2.3.1.6.2) and INTERP1 adds clamp mode (lane 0 held between BASE0 and BASE1, 2.3.1.6.3); the other interpolator of the pair has no such mode, so blend work is configured on INTERP0 and clamp work on INTERP1. Cortex-M0+ has single-cycle shift, AND and add, so bit-field extraction gains nothing from them: the heatshrink bit reader, the CRC and the cell renderer's glyph bit tests do not need an interpolator. Where one wins is saturating or blending arithmetic in fixed point: clamping a Q16.16 coordinate to a range (INTERP1 clamp), linear interpolation between two samples (the datasheet's 2.3.1.6.4 use case) and alpha blending two colors (INTERP0 blend), and affine texture addressing (2.3.1.6.5). These matter for a future `/dev/lcdgfx` frame composer or game physics, kernel-side or behind a new MPU grant. The port uses none today.

### Boot ROM routines

The ROM carries single-precision float on every revision, double from V2, bulk fill and copy (`_memset`, `_memset4`, `_memcpy`, `_memcpy44`), and bit operations (2.8.3.1). The port already routes float and double through `lib/libc/arm/gen/rom_float_*.S`. The ROM word-copy routines are listed as an available but unused option in `sys/arch/rp2040/doc/research/audit-findings.md` (lines 555-570); the kernel `bcopy()` and `bzero()` in `machdep.c` are C word loops. The ROM is a separate bus slave, readable by both cores during a flash operation.

### Pico SDK components read for technique

- `pico_float` and `pico_double` (RP2040 variants): the SDK copies the ROM's `SF` and `SD` function tables into RAM at init and wraps each AEABI entry (`float_init_rom_rp2040.c`, `float_aeabi_rp2040.S`). The port's resolver does the equivalent with a demand-filled cache cell per target.
- `pico_divider` and `hardware_divider`: the save, restore and read-quotient-last protocol above, the 8-cycle wait idiom, and `PICO_DIVIDER_IN_RAM`.
- `hardware_interp`: lane configuration, blend and clamp helpers; usage patterns for table lookup and linear interpolation.
- `hardware_flash` and `pico_flash`: the ROM sequence with a copied boot2 that `flash.c` already mirrors, and `flash_safe_execute()` with its lockout of the other core.
- `pico_multicore`: launch, FIFO helpers, reset of core 1, lockout. Mapped in the next section.

### Fixed-point arithmetic for `lib/libcalc` and game physics

`lib/libcalc` is double-precision throughout (`mathematics.h`), and the ROM double add costs 91 cycles, multiply 155, divide 183 and sine 1,618 (`float-libs.md` 2.2, datasheet 2.8.3.2, documented). Q16.16 add and subtract are one instruction; a 32x32 to 64-bit multiply is a few-dozen-cycle software routine on Thumb-1 (no `UMULL`), so Q16.16 multiply costs less than a ROM double multiply but more than a native multiply; Q16.16 divide needs `(x << 16) / y`, a 48-bit dividend. The SIO divider takes a 32-bit dividend and a 32-bit divisor (`DIV_UDIVIDEND` and `DIV_SDIVIDEND` are bits 31:0, datasheet 2.3.1.5 and the SIO register table), so its 8 cycles apply only to a range-checked 32/32 fast path: when the raw numerator fits 16 bits signed (a magnitude below 0.5) the shifted dividend fits 32 bits and one hardware division gives the Q16.16 quotient. Outside that range the shift overflows: `1.0 / 2.0` is `(0x10000 << 16) / 0x20000`, whose true quotient 0x8000 becomes 0 once the dividend is truncated to 32 bits. The general case takes a data-dependent number of 32/32 steps (each step yields `32 - bitlength(divisor)` fractional bits) or a software 64/32 routine, so no figure near 8 cycles holds for it; its cost is not measured. Where fixed point pays: bounded-range hot loops (screen coordinates, scroll and animation steps, PID terms, FFT butterflies with Q15 twiddles) where `libcalc` currently pays 91-183 cycles per double operation. `float-libs.md` section 4 already recommends hand-written Q16.16 for these cases; nothing in `games/` or `lib/libcalc` uses it yet (searched `fix16`, `Q16`, `fixed.point` in `games`, `lib`, `usr.bin`). The interpolator's blend and clamp are the two operations in that list that save instructions over plain Thumb-1.

## The SDK's multicore patterns and what this kernel needs

| SDK pattern | What it does | Kernel mapping |
| --- | --- | --- |
| `multicore_launch_core1` / `_with_stack` / `_raw` | six-word FIFO handshake with VTOR, stack and entry | The kernel supplies its own SRAM vector table and stack, so it uses the raw form. Disable the core-0 FIFO interrupt around the handshake (the SDK does the same) and drain the FIFO first. About 100 bytes of text. |
| `multicore_fifo_push_blocking`, `pop_blocking`, `drain`, `rvalid`, `wready` | FIFO access with `SEV` after each push | Mailbox words for request, completion and status. Pass pointers to a shared descriptor in SRAM, not payload. |
| `multicore_reset_core1` | `FRCE_OFF.PROC1` set, poll, clear | Call before launch, from `boot()` ahead of `SYSRESETREQ`, and in the panic path. |
| `multicore_lockout_victim_init`, `_start_blocking`, `_end_blocking` | core 1 parks in a RAM handler while core 0 touches flash | Needed only if core 1 executes or reads flash. A core 1 confined to SRAM and ROM needs no lockout; it simply must not read flash or the XIP window, and every exception vector must target SRAM. |
| `flash_safe_execute` | interrupts off on the caller, lockout of the other core | `flash_erase()` and `flash_program()` already mask interrupts; they would gain a lockout call only for a flash-executing core 1. |
| core 1 stack and per-core initializers | `core1_wrapper` sets a stack guard, runs per-core init | Core 1 needs its own MPU region set (a stack guard) and its own VTOR; core 0's `mpu_init()` does not apply. |
| spinlocks and mutexes | `hw_claim_lock`, `mutex_t` | Use a hardware spinlock or single-writer rings. The kernel's `spl` protects only core-0 interrupt races. |

Requirement common to all of them: while core 0 is inside `flash_enter_xip()`'s bracket (connect, exit XIP, erase or program, flush, boot2), core 1 must execute and load only from SRAM and ROM and take no exception whose vector or handler is in flash. Nothing in the datasheet permits a flash read during that window.

## Candidates, in three tiers

Gain classes follow the evidence rules above. "Text/RAM" is kernel flash text, SRAM, or user-window cost.

### Tier 1: core 0 only (bare Pico, Thumb-1)

| Workload | Where code runs | Handoff | Kernel changes | Text/RAM | Expected gain | Board measurement | Falsifier |
| --- | --- | --- | --- | --- | --- | --- | --- |
| CRC-32 of expanded bytes with a nibble table | core 0, `subr_crc32.c` | none | replace one function; same results as zlib | +64 B table, about +40 B text | inferred: 79 to about 25 cycles per byte, 14 percent of one pass | `TIMERAWL` bracket of `hsx_expand()` before and after (Step 1); or emulation | exec or swap-in time falls under 8 percent |
| CRC-32 through the DMA sniffer | DMA controller | descriptor plus completion poll | claim a channel; verify CALC and output flags against a zlib vector first | about +150 B text | inferred: removes the remaining 25 cycles per byte for words, needs word-aligned runs | same | sniffer result differs from zlib on a known vector (calibrate before trusting) |
| Word-buffer bit reader for packed-executable decode | core 0, a port-owned decoder for the HSX streams | none | new decoder file; bit-identical output to the vendored decoder; the SwapRAM codec stays upstream | text about neutral | inferred: `get_bits` is 54 percent at 22.5 instructions per input bit; a 32-bit bit buffer plausibly halves total decode | emulation harness, then board | round-trip against the vendored encoder fails, or emulated gain under 1.5x |
| Hardware divider in `__aeabi_idiv` family | user mode, libc | none | none (MPU grant exists); extend the divider-ownership gate to libc | smaller than the software loop | observed (emulated): 56-458 cycles to about 20-25 | `bc`, `awk` division loops | quotient wrong after a forced SysTick between operand write and read (the attended-hardware falsifier in `float-libs.md` 4.1) |
| LCD cell render through SPI1 DMA, optionally RGB565 | DMA plus core 0 for the pixel buffer | 180-byte cell buffer, IRQ or poll | claim a channel; double buffer; COLMOD 0x55 option | about 300-400 B text, 360 B RAM | inferred: core-0 time per cell from about 122 us to about 5 us; RGB565 cuts the wire bytes per cell from 191 to 131 (31 percent) | GPIO width around the draw; characters per second of `cat` | core-0 time per cell stays above 50 us |
| Tick catch-up after masked flash operations | core 0, `TIMERAWL` | none | read the free-running timer around each flash call, advance the clock | tens of bytes | correctness: removes about 44 lost ticks per sector erase (inferred) | `date` drift over a swap-heavy run | no drift to correct |
| ROM `_memcpy44` and `_memset4` for aligned kernel copies | core 0 | none | selected `bcopy`/`bzero` paths | smaller | small, unmeasured | `bcopy` benchmark | no speedup on aligned 1 KB |

### Tier 2: core 1 as a service processor

| Workload | Where code runs | Handoff | Kernel changes | Text/RAM | Expected gain | Board measurement | Falsifier |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Launch, reset and bring-up skeleton | core 1 in SRAM | FIFO words | `core1_launch()`, `core1_stop()`, `FRCE_OFF.PROC1` in `boot()`, panic and startup | about 300-500 B text in flash, 2-3 KB SRAM | enabling step, no direct gain | counter in SRAM keeps advancing during a flash erase; stops after `reboot` | counter stalls during erase, or survives reboot |
| Diagnostic sentinel for the console wedge | core 1, polled UART0 TX | heartbeat counter in SRAM written by core 0's `hardclock` | heartbeat store (4 bytes), launch | about 600 B image in SRAM and the same 600 B as its flash load image, 1 KB SRAM | diagnostic: prints core-0 state when the heartbeat stalls; targets the field wedge `invariant-map.md` section 3 leaves unexplained | induce a `PRIMASK` hang; sentinel reports within its timeout | sentinel silent during an induced hang, or it disturbs USB timing |
| CRC stage of the packed decode, pipelined behind core 0 | core 1 | two 1 KB ring halves, FIFO tokens | change `hsx_expand()` consumer | about 300 B | at most the 20.5 percent CRC share, and tier 1's table CRC removes most of it | board exec time | gain under 8 percent once the table CRC is in |
| Single-stream decode on core 1 while core 0 does other swap-in work | core 1 | descriptor plus completion | `swapin()`/`exec` dispatch | about 1.3 KB SRAM image | inferred: little, because the text stream is the critical path and core 0 has no other substantial work | swap-in time | gain under 10 percent |
| PicoCalc renderer on core 1 (alternative to DMA) | core 1 with fonts in SRAM (855 B) | draw-command ring, single writer | move `lcdrender` consumer | about 2 KB SRAM | core 0 returned about 95 percent of render time, same as DMA | as the DMA row | DMA row already meets the goal; this costs a core |
| Interrupt service and timekeeping through flash windows | core 1 | shared tty rings, spinlock | tty state shared across cores; high risk | SRAM ISR copies | removes lost ticks and UART overrun during erase | UART loss count, tick drift | any kernel structure corrupted by concurrent access |

### Tier 3: both cores (PicoCalc and other advanced boards)

| Workload | Where code runs | Handoff | Kernel changes | Text/RAM | Expected gain | Board measurement | Falsifier |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Split-stream decode for exec and swap-in | cores 0 and 1, one text segment each | descriptor pair plus FIFO completion | packer emits independent segments (new `HSX_VERSION`); `hsx_expand()` dispatch; per-segment CRC | about 300 B text, 1.3 KB SRAM, one extra decoder state (590 B) | inferred: up to 2x on the decode portion; ratio loss measurable on the host | exec and swap-in time, packed ratio | measured speedup under 1.5x, or packed size grows more than 3 percent |
| Graphics frame composer on core 1 with interpolators, DMA pushing frames | core 1 plus DMA | frame ring | new `/dev/lcdgfx`; MPU grant or kernel-side use | 20 KB or more | frame wire time alone is 131 ms at 320x320 RGB565 and 12.5 MHz (204,800 bytes x 8 bits / 12.5 MHz, computed); composing on core 1 keeps core 0 free | frames per second | frame time bound by SPI, so composing gains nothing |
| Data-parallel compute (FFT, matrix) through a kernel service | core 1 | pointer and length validated against the process window | new syscall | substantial | up to 2x on `libcalc` kernels | `calc` benchmarks | syscall and cache effects exceed the gain |

## Recommended first experiment

Goal: decide with measurements whether any core-1 decode work is worth designing, using steps that each cost little and revert completely.

**Step 1, board baseline of the decode cost (no core 1).** The shipped `time` prints elapsed time truncated to 100 ms (`usr.bin/time/time.c` line 61 divides `tv_usec` by 100000), so two runs that print the same tenth can differ by nearly 100 ms, which is larger than the 50 ms falsifier below; a single `time` of one exec cannot decide this step. The step has two measurements.

- Step 1a, repeated-exec timing (no kernel change). Install a program whose text is near 40 KB both raw and packed through the manifest `pack` entries the exec_hsaout test uses (`textcrc` is small and `bigtest` is packed only, so neither fits; pick the multicall box or build a 40 KB test program), then time a shell loop of 20 executions of each under `time`. The loop overhead (fork, wait, shell) is identical for both variants, and dividing the difference by 20 resolves it to 5 ms. Prediction from the model: for 40 KB of text the packed exec costs about 250 ms more than the raw per execution, so 5 s more over the loop; one extra 125 ms per swap-in. Falsifier: a per-exec difference under 50 ms for a 40 KB text (1.25 ms per KB of text).
- Step 1b, direct bracket (diagnostic kernel change). A kernel built for this step reads `TIMERAWL` (the free-running microsecond counter, `machdep.c` `TIMER_TIMERAWL`, a 32-bit difference that survives wraparound) before and after each `hsx_expand()` call in `exec_hsaout_check()` and in the restoration `swapin()` runs, and accumulates the microseconds beside the `STORAGE_STATS` counters (`STORAGE_STATS=yes`, readable through `sysctl machdep.storage_stats`). This resolves single passes to 1 microsecond and compares directly with the model's 144 ms per 46,540 bytes. It is a diagnostic change reverted before any commit, so the earlier "no kernel change" constraint holds for Step 1a and not for Step 1b.

Deviation rule: if either measurement contradicts the model, the model's cycle table (not the conclusion) is investigated first, by comparing the Step 1b bracket with the model's cycles per expanded byte; and if the Step 1a difference and the Step 1b bracket disagree with each other, the loop overhead assumption is investigated before either is trusted. Both steps need the board and the owner's flash opt-in.

**Step 2, core-1 bring-up microbenchmark as a standalone image loaded to SRAM.** A UF2 addressed to SRAM runs from the boot ROM without writing flash (datasheet 2.8.1 and 2.8.4: the USB boot interface loads "directly into SRAM and run[s]"), so the DiscoBSD flash content stays intact and the experiment reverts by power cycle. The image launches core 1 with the datasheet handshake, a vector table in SRAM and a stack in SRAM4, and measures: FIFO round-trip latency; SRAM-resident `hsx_expand()` over the same 36,509-byte stream (calibrating the 387 cycles per byte model against silicon, where Step 1b calibrates it inside the kernel); core 0's slowdown running the same decode while core 1 streams SRAM, with and without `BUS_PRIORITY.PROC0`; and a progress counter on core 1 while core 0 runs `connect_internal_flash`, `flash_exit_xip`, `flash_flush_cache` and `flash_enter_cmd_xip` with no erase or program, the non-destructive version of the XIP-down window. Predictions: handshake completes; decode within 30 percent of 387 cycles per byte; core-0 slowdown under 5 percent; the counter keeps advancing during the XIP-down window. Falsifiers: the handshake fails under boot ROM V3, decode deviates above 30 percent, core-0 slowdown above 10 percent, or the counter stalls (which would mean something core 1 touches reaches XIP). A `check` target and a PICO_UART bring-up follow only when this passes. Board execution requires the explicit opt-in.

**Step 3, only if Step 1b shows decode of 100 ms or more and Step 2 passes: in-kernel split decode on PICO_UART.** PICO_UART has the most free text and about 6.5 KB of free SRAM4 and SRAM5. Add the packer-side segment split and a kernel path that decodes segment 0 on core 0 and segment 1 on core 1, behind a compile-time option, and compare against the single-core table-CRC baseline from tier 1, not against today's code. The comparison to beat is the cheapest single-core change; a core-1 result that does not clear it by 1.5x does not ship. Rollback: remove the option and reflash.

Order of work after Step 1b: tier-1 CRC table and bit reader first (largest gain, no new hardware state), then Step 2, then Step 3. A core-1 design measured against the unoptimized decoder would overstate its value by the factor the single-core changes remove.

## Open items

- Not run: every board and Renode check. The emulated cycle counts are the only new numbers, rated medium; `docs/research/rp2040-second-core-emulation/build.sh` reproduces them on the host and says nothing about silicon.
- Not verified: W25Q16JV erase and program times and erase-suspend behavior (no chip datasheet in the repository); the free-SRAM figure for PICO_UART (derived from `kern.ldscript`, confirmed by the linked map); that `SYSRESETREQ` leaves core 1 running on this silicon (documented scope, no board capture); the DMA sniffer's flag combination for the zlib CRC (needs a known-vector check).
- Decision for the owner: the HSX packed format is byte-compatible with upstream heatshrink and the vendored decoder is "byte-identical to upstream". A word-buffer bit reader for the packed-executable stream departs from that for the executable path only. Choose whether the port keeps the byte-identical rule or owns a faster decoder for the HSX streams; the SwapRAM codec is unaffected.
- RP2350 boards (Pico 2) are outside this report: the repository holds no RP2350 datasheet, and their SIO, spinlocks and flash arbitration differ.

## Appendix: emulation harness

The files are in `docs/research/rp2040-second-core-emulation/`; `PYTHON=python3 build.sh OUTDIR` rebuilds the workload from tree sources and fails on any difference from the recorded hashes (`SHA256SUMS`) or the recorded model output (`expected-output.txt`). A deliberate mutant of `sample.c` (one more `puts` call and an added term in the return value) makes it exit nonzero, which calibrates the comparison.

- Toolchain: `arm-none-eabi-gcc` 16.2.0 with newlib 4.6.0.20260123 (sample program only), Unicorn 2.1.4, Capstone 5.0.7, Python run with `-I`, a host `cc` for the packer.
- Sample: `sample.c` compiled and linked as `arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os --specs=nosys.specs`, then `objcopy -O binary`, giving `sample.bin` of 46,540 bytes (text 40,708, data 1,736 in the ELF), SHA-256 `ea03fbc95db66673e7e21d271904d4978b29926f377cf0d8fc1d313d95fd435b`. A different compiler or newlib changes these bytes and with them the cycle count.
- Packed stream: `encode.c` linked with the vendored `heatshrink_encoder.c` at window 9, lookahead 3 and the search index off (`-DHEATSHRINK_STATIC_WINDOW_BITS=9 -DHEATSHRINK_STATIC_LOOKAHEAD_BITS=3`), giving `sample.hs` of 36,509 bytes (ratio 0.784), SHA-256 `3330724f5e79ab9873647d3cf394ae32d02ef2d5c140ac8b1ebcfe9cc46114d8`.
- Harness: `harness.c`, `hsx_stream.c`, `hsx_decoder.c` and `subr_crc32.c` compiled with `-DKERNEL -mcpu=cortex-m0plus -mthumb -ffreestanding -fno-builtin -fno-common -std=gnu17 -O -nostdinc` and the include paths `sys`, `sys/arch`, `sys/arch/rp2040/include` (as `machine/`), `sys/arch/rp2040/dhara/compat`, `sys/arch/rp2040/heatshrink` and its `compat`, linked at 0x20000000 by `harness.ld`. `harness.c` places the packed bytes at 0x30000000 and the output at 0x30200000 and supplies byte-loop `memset`, `memcpy`, `memmove`, `bzero` and `bcopy`.
- Model: `cycles.py` hooks every instruction under Unicorn, classifies it with Capstone using the cost table in "Method and evidence classes", and charges a branch two cycles when the next executed address is not the fall-through address, otherwise one. It checks the output bytes and the zlib CRC-32 of the sample and prints the five costliest functions per entry point.

Recorded output (`expected-output.txt`):

    run: ok=True raw=46540 packed=36509 cycles=18003905 cycles/rawbyte=386.8 ms@125MHz=144.03
       get_bits                9732857 cycles  54.1%  6566460 instructions
       hsx_decoder_poll        3902551 cycles  21.7%  2645663 instructions
       crc32_update            3687316 cycles  20.5%  3262536 instructions
       bcopy                    298584 cycles   1.7%  186097 instructions
       fill                     293347 cycles   1.6%  183395 instructions
    run_discard: ok=True raw=46540 packed=36509 cycles=18075599 cycles/rawbyte=388.4 ms@125MHz=144.60
       get_bits                9733351 cycles  53.8%  6566720 instructions
       hsx_decoder_poll        3949119 cycles  21.8%  2674997 instructions
       crc32_update            3695740 cycles  20.4%  3266280 instructions
       bcopy                    298584 cycles   1.7%  186097 instructions
       fill                     293347 cycles   1.6%  183395 instructions

The 8 KB comparison in the result table (420.6 cycles per byte for the first 8 KB, ratio 0.889) used the first 8,192 bytes of `sample.bin` packed with the same encoder; it is not part of the recorded run and reproduces by passing that prefix and its packed stream to `cycles.py`. The sample is a stand-in: input composition changes the cycles per byte, so the board comparison must use the same `sample.bin` and `sample.hs`, or the model must be rerun on the actual a.out text streams.
