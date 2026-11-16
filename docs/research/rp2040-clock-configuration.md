# RP2040 clock configuration: 133 MHz, both cores, and clocking that varies for power

The owner asked for a granular audit of every clock-derived cost across the DiscoBSD tree and the document stores, and a build-out of full speed on both cores plus variable, downclocking, power-adaptive clocks, or at least a manual clock toggle with an application (decision D133). This note is the study; the build-out follows the GizmoBSD rename (D109). The findings lead:

- **133 MHz is the datasheet ceiling for `clk_sys` and `clk_peri`** across all process, voltage and temperature, at the default 1.10 V core supply, and the system PLL reaches it exactly from the 12 MHz crystal: 12 MHz x 133 / (6 x 2), VCO 1596 MHz. Only the feedback divider changes from today's 125. A 133 MHz PICO kernel ran two benchmark passes on a bare Pico with CoreMark +6.4% and kernel tests +6 to +10%.
- **No single clock constant exists.** `CPU_KHZ` and `BUS_KHZ` are two hand-kept options in four Configs and four generated Makefiles, and the PLL fields at `machdep.c:503` and `:507` do not read them. Setting `CPU_KHZ=133000` alone leaves the PLL at 125 MHz and runs every derived rate 6.4% wrong: the UART at about 108,270 baud, the tick at 1.064 ms.
- **Ten consumers derive from the constants; twelve sites do not move with them.** At 133 MHz two derived rows fail their own static assertions today (SD fast clock, audio carrier) and eight pass. After rework every consumer has an exact or in-tolerance value at 133 MHz except where the table says a choice is needed.
- **Both cores share one `clk_sys`.** Full speed on both cores means one clock generator at 133 MHz; the dual-core DVDD current at 133 MHz is about 30 mA (graph reading). The repository already rules out a codec worker that runs while its caller waits; the one core-1 offload that survives that ruling is a split-stream decoder.
- **Run-time changes are cheap on the clock side and expensive on the consumer side.** `CLK_SYS_DIV` changes on the fly without a glitch. USB needs clk_sys of at least 52.8 MHz, PSRAM at the current frame size needs at least 81.25 MHz, and SysTick, the keyboard I2C block and the audio PWM block count clk_sys cycles. Sourcing SysTick from its 1 us external reference is a datasheet-supported hypothesis that removes two re-derivations.
- **Downclocking saves less than the clock ratio suggests.** The DVDD current is about 3.7 mA plus 0.2 mA per MHz (graph reading), so energy per cycle falls as the clock rises; the saving of a lower setpoint depends on the idle current of a WFI at default clock gating, which no source gives.
- **The PicoCalc cannot verify a saving by itself.** Its battery fact crosses the keyboard controller as 1% steps every 20 s.

Evidence classes in this note: DS (datasheet, section and printed page, with the line in the notes repository's text extract), SRC (tree source at `84ff3574`), B (build: `arm-none-eabi-nm -S` on the benchmark session's PICO -Os image, GCC 14.2.1 20241119), M (board measurement, bare Pico), H (host arithmetic by `clockcalc.py` in this directory) and I (inference). No board, Renode or qemu run belongs to this note.

## 1. The clock tree as configured today

### Source

`SystemClock_Config()` in `sys/arch/rp2040/rp2040/machdep.c:419-545` (SRC) runs from `startup()` and assumes nothing about the clocks it inherits.

| Clock | Source and setting | Frequency | Where |
| --- | --- | --- | --- |
| XOSC | 12 MHz crystal, startup delay 47 (about 1 ms), FREQ_RANGE 1-15 MHz | 12 MHz | `machdep.c:446-449` |
| `clk_ref` | glitchless mux to XOSC; the divider is never written (reset value 1) | 12 MHz | `machdep.c:457-461` |
| watchdog tick | 12 cycles of `clk_ref` | 1 MHz | `machdep.c:249`, `:464-465` |
| `PLL_SYS` | REFDIV 1 (reset), FBDIV 125, POSTDIV1 6, POSTDIV2 2; VCO 1500 MHz | 125 MHz | `machdep.c:503`, `:507` |
| `clk_sys` | glitchless mux to the aux mux, aux source 0 (`PLL_SYS`), divider 1 | 125 MHz | `machdep.c:482-519` |
| `clk_peri` | aux source 0 (`clk_sys`), no divider exists | 125 MHz | `machdep.c:525-526` |
| `PLL_USB` | FBDIV 100, POSTDIV1 5, POSTDIV2 5; VCO 1200 MHz | 48 MHz | `machdep.c:534`, `:538` |
| `clk_usb` | aux source 0 (`PLL_USB`) | 48 MHz | `machdep.c:541` |
| `clk_adc` | aux source 0 (`PLL_USB`), enabled by the ADC driver on first read | 48 MHz | `dev/adc.c:83` |
| `clk_rtc` | stopped; no RTC driver | none | `machdep.c:489` |

The kernel never writes `WAKE_EN`, `SLEEP_EN`, the VREG register or `SCR.SLEEPDEEP` (SRC: `rg` of the C, header and assembly sources under `sys/arch/rp2040` for `WAKE_EN`, `SLEEP_EN`, `SLEEPDEEP`, `VREG`, `VSEL` and `40064000` finds none). Every clock gate stays at its reset value of 1, and the only sleep is the `WFI` in `idle()` (`machdep.c:866-885`).

`CPU_KHZ` and `BUS_KHZ` are compile-time options: `options "CPU_KHZ=125000"` and `options "BUS_KHZ=125000"` in `sys/arch/rp2040/compile/{PICO,PICO_UART,PICOCALC,PICOCALC_PSRAM}/Config` (lines 19-20, 19-20, 36-37, 42-43) and `PARAM += -D...` in the four generated Makefiles. `machdep.cpu_khz` and `machdep.bus_khz` return them read-only (`sysctl.c:381-390`).

### Datasheet limits

| Limit | Value | Source |
| --- | --- | --- |
| `clk_sys` and `clk_peri` maximum | 133 MHz "across all process, voltage and temperature variations"; 200 MHz needs DVDD 1.15 V (`VSEL` 1100) | DS 2.15.3.1, p. 183 (L13345) |
| DVDD operating range | 1.05 V min, 1.10 V typical, 1.16 V max; transients within 100 mV | DS 5.6 Table 634, p. 622 |
| Regulator output | 0.80 V to 1.30 V in 50 mV steps, 1.10 V at reset | DS 2.10.3, p. 157; VREG register p. 158 |
| PLL | FREF/REFDIV >= 5 MHz; VCO 750-1600 MHz; FBDIV 16-320; POSTDIV 1-7 each; FREF <= VCO/16 | DS 2.18.2, pp. 228-229 |
| `PLL_SYS` output | 133 MHz maximum | DS 2.18.2, p. 229 (L16852) |
| `clk_usb`, `clk_adc` | must be 48 MHz | DS 2.15.3.1, p. 183 |
| `clk_sys` with USB active | above 48 MHz; at least 10% above `clk_usb` (RP2040-E16) | DS 4.1.2.1, p. 384 (L28754); Appendix B, RP2040-E16 (L47604) |
| Flash (W25Q16JV) | 133 MHz at 3.0-3.6 V, 104 MHz at 2.7-3.0 V; Read Data 03h 50 MHz | W25Q16JV rev G 10.2, 10.6, pp. 60-61 |

The 2024-10 build of the RP2040 datasheet (639 pages, held in the gororoba documents repository) states "up to 133 MHz" without the "across all process, voltage and temperature" sentence or the 200 MHz footnote; the 2025-02 build (642 pages) adds both. The public download of 2026-11-16 returned the 2025-02 build.

PLL jitter against power (DS 2.18.2.1, p. 229): a higher VCO allows larger post dividers and lowers jitter; a lower VCO lowers power. When two post dividers differ, the larger belongs in POSTDIV1 for lower power.

## 2. Every consumer of a clock-derived constant

Counting rule: a constant is derived when it moves with `CPU_KHZ` or `BUS_KHZ` in a recompile, and hardcoded when it does not. Rows marked FAIL name a static assertion in the tree or a datasheet limit that the value breaks. "Today's formula" evaluates the tree's own expression at 133 MHz; "After rework" names the value that exists with a specified change. Values are H, from `clockcalc.py table 125 133`, and cross-checked against the tree's documented 125 MHz figures (SD 398 kHz and 12.5 MHz, LCD 12.5 MHz, carrier 500 kHz, I2C near 96 kHz).

### Derived from `CPU_KHZ` or `BUS_KHZ` (ten)

| # | Driver, constant | Formula (SRC) | 125 MHz | 133 MHz, today's formula | 133 MHz after rework | Silent failure if stale |
| --- | --- | --- | --- | --- | --- | --- |
| D1 | UART baud, `uart.c:129` | `div = 4 * BUS_KHZ * 1000 / 115200`, IBRD = div >> 6, FBRD = div & 63 | IBRD 67 FBRD 52, 115,207 Bd (+0.0064%) | IBRD 72 FBRD 10, 115,201 Bd (+0.0012%) | none needed | console garbled; 6% off is past UART tolerance |
| D2 | SD identification clock, `sd.c:114` | SCR = ceil(BUS / 800 kHz) - 1, prescale 2 | SCR 156, 398,089 Hz | SCR 166, 398,204 Hz | none needed | card refuses CMD0 above 400 kHz |
| D3 | SD data clock, `sd.c:115-118` | CPSDVSR = ceil(BUS / 12.5 MHz), assert even 2-254 | 10, 12.500 MHz | 11, odd: FAIL (build stops) | 12 gives 11.083 MHz; 10 gives 13.300 MHz | none: the assertion catches it |
| D4 | Audio sample clock, `picopcm.c:71`, `:138` | TOP + 1 = CPU / 8000 | 15,625 | 16,625, exact | none needed | pitch off by the clock ratio |
| D5 | Audio carrier, `picopcm.c:88` | assert CPU / PCM_LEVELS == 500,000 | 500 kHz | 532 kHz: FAIL (build stops) | PCM_LEVELS 266 gives 500,000 Hz exactly; or accept 532 kHz | none: the assertion catches it |
| D6 | Bell tone, `picoaudio.c:66-72` | DIV.INT = CPU_KHZ / 1000, TOP 999 | 1 MHz counter, 1 kHz | 1 MHz, 1 kHz, exact | none needed | tone pitch off |
| D7 | Keyboard I2C, `picokbd.c:333-339` | period = round(CPU / 100 kHz), low 3/5, spike filter low/16, hold 300 ns | SCL 95.9 kHz, hold 304 ns | SCL 95.9 kHz, hold 301 ns | none needed | firmware sees a wrong SCL rate |
| D8 | SysTick reload, `clock.c:60` | CPU_KHZ * 1000 / HZ - 1 | 124,999 | 132,999, exact | none needed | tick period wrong; time of day drifts |
| D9 | Core 1 SysTick, `core1.c:93`, `:207` | CPU_KHZ * C1_PERIOD_US / 1000 - 1, assert under 2^24 | 1,249,999 | 1,329,999, exact | none needed | observer cadence wrong |
| D10 | Banner and sysctl, `machdep.c:599`, `sysctl.c:384`, `:390` | CPU_KHZ / 1000, BUS_KHZ / 1000 | 125 / 125 | 133 / 133 | none needed | picofetch, boardinfo and the Renode banner check show the constant, not the PLL |

At 133 MHz, 2 of 10 fail their assertion today (D3, D5) and 8 pass. These are the two the benchmark session met when the union kernel would not build (M; `RESULTS.md`, D127 item 3).

### Hardcoded: the value does not move with the constants (twelve)

| # | Site | Value | Breaks at another clk_sys? | Note |
| --- | --- | --- | --- | --- |
| H1 | `PLL_SYS` FBDIV, POSTDIV1, POSTDIV2, `machdep.c:503`, `:507` | 125, 6, 2 | defines the clock | nothing ties these to `CPU_KHZ`: the root of the missing single constant |
| H2 | `clk_peri` source `clk_sys`, `machdep.c:526` | aux 0 | yes | `BUS_KHZ` must equal `CPU_KHZ` by hand |
| H3 | LCD SPI, `st7365p.c:69-70` | prescale 2, SCR 4 | rate follows: 13.300 MHz at 133 (75.2 ns against the 66 ns minimum cycle; ceiling 15.15 MHz) | still valid at 133, no margin against 150 MHz |
| H4 | PSRAM PIO divider, `psram_pio.h:135` | PSRAM_CLKDIV 2: SCK = clk_sys / 4 | rate follows: 33.25 MHz at 133 | all six commands in use allow 133 MHz on the ESP part |
| H5 | PSRAM frame, `psram_xfer.h:33` | 64 bytes | tCEM budget: frame must stay under 8 us | 5.20 us worst frame at 125 MHz, 4.89 us at 133, 8.1 us at 80 MHz |
| H6 | XIP SSI and boot2 divider, `flash.c:351-352`, `boot2_w25q080.S:100` | divider 2, sample delay 1 | flash SCK 66.5 MHz at 133 | inside the 133 MHz of the part; PicoCalc loader stage uses divider 4 |
| H7 | Audio levels, `picopcm.h:34` | PCM_LEVELS 250 | couples to D5 | the carrier is clk_sys / levels |
| H8 | USB buffer settle, `usb.c:394` | six `nop` | safe to 288 MHz | the datasheet asks for ceil(clk_sys / 48 MHz) = 3 at 133; the SIE_CTRL rule asks for twice that, 6 |
| H9 | `PLL_USB`, `machdep.c:534`, `:538` | 100, 5, 5 | no | crystal-derived, fixed at 48 MHz |
| H10 | Watchdog tick, `machdep.c:249` | 12 cycles | no | crystal-derived; the 1 MHz TIMER does not depend on clk_sys |
| H11 | XOSC startup, `machdep.c:446` | 47 | no | crystal-derived |
| H12 | Spin bounds, `adc.c:56`, `flash.c:417` | 100,000 and 200,000 iterations | the bound's time scales with clk_sys | both far beyond the event they bound |

Constants outside the kernel that restate the clock, each a place that must move with it:

| Site | Literal |
| --- | --- |
| `tools/renode/pico_uart.repl:14` | `frequency: 125000000` (the Renode PL011 takes the platform's frequency, not the programmed clock: `tools/renode/patches/0009-pl011-transmit-fifo-and-trigger.patch`, "uartClockFrequency, not the system clock the firmware programs") |
| `tools/renode/check-boot.py:70` | `"cpu: 125 MHz core, 125 MHz peripheral"` |
| `tools/renode/check-picocalc.py:572` | `"CPU      125.000 MHz, "` |
| `tests/picocalc/Makefile:115`, `:1676` | `-DBUS_KHZ=125000 -DCPU_KHZ=125000` |
| `usr.bin/picofetch/tests/hwinfo_fixture.c:96-97`, `usr.bin/picofetch/tests/render.py:246` | 125000 and "125.000 MHz" |
| `tests/reboot_contracts/boardinfo_test.sh:51`, `:76` | 125000 |
| Prose | `README.md:35`, `:230`, `sd.c:111`, `st7365p.c:23`, `sys/arch/rp2040/doc/PICOCALC.md:177`, `:675`, `BOOT-MAP.md:123`, `tests/psram/pio_sim.py:11` |

### What breaks silently

- **`CPU_KHZ` raised, PLL left at 125:** the UART divisors assume 133 MHz and run at 108,270 baud (-6.0%); SysTick reloads 132,999 against a real 125 MHz and ticks every 1.064 ms; the time of day runs 6% slow, because `clock_catchup()` corrects only masked windows, not the steady tick period (`clock.c:84-109`); the SD and LCD rates fall 6%. The console failure is loud; the time drift is not.
- **PLL raised, constants left at 125:** the UART runs 6.4% fast, the tick comes every 0.94 ms and the time of day runs 6.4% fast. Nothing in the tree compares the PLL registers with the constants, and Renode cannot: its PL011 frequency comes from the platform file.
- **Renode is blind to both.** Its UART baud comes from `pico_uart.repl`, so a kernel with consistent but wrong PLL fields boots.

## 3. PLL settings for 133 MHz and the nearest exact alternatives

`clockcalc.py pll TARGET` enumerates every setting with REFDIV 1 or 2 inside the DS 2.18.2 limits, lowest VCO first, the larger divider in POSTDIV1 (H; reproduces the SDK's search rule at DS p. 229).

| Target | REFDIV, FBDIV, POSTDIV1/2 | VCO | Notes |
| --- | --- | --- | --- |
| **133.000 MHz** | 1, 133, 6, 2 (or 4, 3) | 1596 MHz, 4 MHz under the 1600 MHz limit | one constant changes from today's code, 125 to 133; the lowest-jitter form (highest VCO) |
| 133.000 MHz | 2, 133, 6, 1 | 798 MHz | lowest VCO, lower PLL power, PFD at 6 MHz (above the 5 MHz minimum); higher jitter. Hardware evidence for neither form beyond the first |
| 132.000 MHz | 1, 66, 6, 1 (VCO 792) or 1, 132, 6, 2 (VCO 1584) | | SD identification clock exactly 400,000 Hz (at, not under, the limit); data clock CPSDVSR 11 odd, so 12 (11.000 MHz); carrier 528 kHz, 264 levels give 500,000 Hz |
| 126.000 MHz | 1, 63, 6, 1 (VCO 756) or 1, 126, 6, 2 (VCO 1512) | | gains 0.8% over 125; UART exact (115,200.0 Bd); carrier 504 kHz, 252 levels give 500,000 Hz; CPSDVSR 11 odd |
| 125.000 MHz | 1, 125, 6, 2 | 1500 MHz | today |
| 100.000 MHz | 1, 100, 6, 2 | 1200 MHz | CPSDVSR 8 gives 12.5 MHz; carrier 400 kHz, 200 levels give 500,000 Hz |

Consumer consequences at the candidates (H, `clockcalc.py table`): at 132, 126 and 133 the only rows that change verdict against 125 are the SD data clock (odd CPSDVSR) and the carrier; the PSRAM, flash, I2C, UART, SysTick and USB rows pass at all three. The carrier and the SD rows are the same two decisions at every frequency near 125.

Choice between 132 and 133: the PSRAM read-ID frame is the one place a megahertz matters. At 133 MHz its SCK is 33.25 MHz; the APS6404L datasheet allows 33 MHz for Read ID (`psram_xfer.c:213`) and the ESP-PSRAM64H allows 133 MHz for it (command table, p. 6). The shipped part is the ESP-PSRAM64H (U301), so 133 passes; an APS6404L-fitted board would run its ID read 0.76% over its limit. At 132 MHz the SCK is exactly 33.00 MHz. No other consumer separates 132 from 133 (H).

PSRAM sampling margin (I): the read path samples one SCK period after the launching edge, in clk_sys cycles (`psram_pio.h:50-57`), so latency and period scale together; what does not scale is the pad and device delay: PIO output flop to pad 2.05-7.10 ns (GPIO21), device clock-to-output 2-6 ns (tACLK, ESP-PSRAM64H Table 10-5, p. 17), pad to synchronizer 1.84-5.25 ns (DS 5.5.3.6 Tables 629-630, pp. 618-619; figures at IOVDD 1.8 V, lower at 3.3 V). The worst-case round trip is 18.35 ns before board delay; one SCK period is 32.0 ns at 125 MHz and 30.1 ns at 133 MHz, so, if the sample lands one SCK period after the launching edge as `psram_pio.h` arranges, setup slack falls from 13.65 ns to 11.73 ns (14%) and hold slack stays 5.9 ns. No PicoCalc capture exists (`picocalc-psram-capture.md`), so the margin is an arithmetic bound, not a measurement.

## 4. Both cores

**Core 1 today.** `sys/arch/rp2040/doc/CORE1.md`: core 1 is an observer. It sleeps in `WFI` and wakes on its own SysTick every 10 ms to read core 0's tick record, reports a stall at 100 ms and resets the chip at 4 s. It runs from SRAM only, because a flash erase takes XIP down for both cores. The PICO and PICO_UART kernels carry `options "CORE1"`; the PICOCALC kernels do not. Measured (B, GCC 14.2.1, -Os, PICO): `core1_main` 248 B, `core1_poll` 476 B, `core1_sysctl` 184 B, `core1_init` 80 B; CORE1.md records 1,828 B of kernel flash (544 B of it the SRAM load image) and 576 B of `.bss` (GCC 16.2.0).

**Shared clock.** Both cores take `clk_sys` from one clock generator (DS 2.15.3.1, p. 183: one "System clock"), and SIO, the bus fabric and the SRAM banks sit on it. There is no per-core frequency. Consequences: (1) "full speed on both cores" is one setpoint, 133 MHz or lower; (2) a clock change moves core 1 together with core 0, so core 1's SysTick reload (D9) must follow it; (3) the dual-core current of Figure 172 applies only while both cores run: at 133 MHz about 30 mA of DVDD (graph reading, section 6), with the single-core share not published.

**What "full speed on both cores" needs.** The kernel is uniprocessor by design: one resident process in one 192 KB window, one global `u`, and no atomic exchange on ARMv6-M (CORE1.md "What it is not"; `docs/research/rp2040-second-core.md` "What is ruled out"). The existing mechanism is the request ring and reply ring of `rp2040/corering.c`: `CRING_SLOTS` (8) entries each way, a slot number and an argument per entry, a SIO FIFO doorbell, core 1 running from SRAM, core 0 keeping an inline path for every service so that a dead core 1 degrades the system and never stops it.

**Compression offload.** The repository ruled on this in `rp2040-second-core.md`: a codec worker that runs while its caller waits hides nothing, because the one resident process has nothing else to do during a swap or exec decode. The ruling's falsifier is a second resident process, which the memory layout excludes. What survives is a decode split across both cores:

| Candidate | Gain (I) | Cost (I) | Condition |
| --- | --- | --- | --- |
| Split-stream HSX decode at exec | the emulated 387 cycles per byte (`rp2040-second-core-emulation/`, no board run) put a 40 KB text at 127 ms per expansion at 125 MHz, 119 ms at 133, twice per exec; two cores cut the decode phase by up to half | new `HSX_VERSION` with independent segments, under 3% packed-size growth; the decoder in core 1's SRAM image (size unmeasured; the observer's image is 544 B); a ring op pair; the RAM of a second decoder state | only after the single-core decoder is fixed and exec stays above 100 ms for a 40 KB program (the note's step 5) |
| Console service on core 1 (PicoCalc) | overlaps panel drawing with the running process | the panel draw path and font in SRAM; PicoCalc kernels carry no CORE1 | only if SPI1 DMA on core 0 recovers less than 90% of the render time (the note's step 3) |
| Swap compression (`swapram_pool.c`) | none: the caller waits | | excluded by the ruling |

At 133 MHz the single-core decode gain from the clock is 6.4%; the dual-core split is the larger lever for that one phase, and the clock is the larger lever for everything else. The two are independent.

**Power of the second core.** Core 1 awake doubles the dynamic share of the DVDD current (Figure 172 is a dual-core FFT; the split is not published). The observer's duty is a few hundred instructions 100 times a second, so its effect is below the resolution of any measurement in section 6.

## 5. Variable clocking: what the RP2040 offers and what tolerates it

### Mechanisms

| Mechanism | Property | Source |
| --- | --- | --- |
| `CLK_SYS_DIV` (INT bits 31:8, FRAC 7:0) | changes on the fly; the change synchronizes to the end of the clock cycle and does not glitch; a fractional divisor alternates two integers and jitters (2.4 is three cycles of 2, two of 3) | DS 2.15.3.3, pp. 184-185 |
| `CLK_SYS_CTRL.SRC` glitchless mux | switches between `clk_ref` and the aux mux without a glitch; poll `CLK_SYS_SELECTED`; the aux source changes only while the mux selects `clk_ref` | DS 2.15.3.2, p. 184 |
| PLL reprogramming | the output cannot be used while REFDIV, the post dividers or bypass change; a FBDIV change is allowed and may overshoot; a re-lock therefore runs `clk_sys` from `clk_ref` (12 MHz) in between; lock time is not stated in the extracts read | DS 2.15.2.5, p. 182; 2.18.3 |
| `clk_peri` source | aux source 0 `clk_sys`, 1 `PLL_SYS`, 2 `PLL_USB`, 3 ROSC, 4 XOSC; the aux source changes only with the generator stopped; the table calls it a clock that "allows peripherals to run at a consistent speed if clk_sys is changed" | DS 2.15.3.1 Table 205; CLK_PERI_CTRL, p. 203 |
| Clock gating | `WAKE_EN0/1` and `SLEEP_EN0/1` at 0xa0-0xac, reset value all ones, glitch-free, state preserved; both cores in `WFI` with an idle DMA select the SLEEP mask; "system sleep will do nothing" until the masks are configured | DS 2.11.1-2.11.2, pp. 160-161; 2.15.3.5.2; WAKE_EN0 pp. 210-211 |
| Frequency counter | measures a selected clock against `clk_ref`; the kernel can verify `clk_sys` and `clk_usb` at run time | DS 2.15.4, p. 186 |
| DORMANT | all oscillators stop; zero dynamic power; wakes on a GPIO level or edge or an RTC alarm; the PLLs must be stopped first, or they corrupt | DS 2.11.3, p. 161; 2.15.2.5 |
| `VREG.VSEL` | 0.80 to 1.30 V in 50 mV steps; DVDD rated 1.05 to 1.16 V | DS 2.10.3, 5.6 |
| Memory power-down | SRAM banks retain content but cannot be accessed; the kernel uses nearly all 264 KB | DS 2.11.4, p. 161 |

DORMANT has no keyboard wake here: the keyboard controller has no interrupt line to the RP2040 (the poller reads it over I2C). A GPIO edge does wake it (UART0 RX on GP1, SD detect on GP22), but TIMERAWL and SysTick stop, USB is dead, and the time of day loses the whole sleep, so it fits a deliberate suspend, not an idle state. VREG changes are not recommended: 1.05 V is the lower edge of the rating with a saving of about 4.5% of core current (1.4 mA at 133 MHz, from Figure 172's 10% current change for a 10% voltage change), and 1.15 V for 200 MHz is outside the 133 MHz all-conditions rating. The Pico's core regulator is a linear regulator fed from the 3.3 V rail (Pico datasheet 4.4, build d4e0f1799616), so the 3.3 V supply current equals the core current.

### Floors and ceilings that bound a setpoint (H, `clockcalc.py table`)

| Constraint | Bound | Source |
| --- | --- | --- |
| USB active | clk_sys >= 52.8 MHz (1.10 x 48 MHz) | DS 4.1.2.1; RP2040-E16 |
| PSRAM at `PSRAM_CLKDIV` 2, 64-byte frames | clk_sys >= 81.25 MHz: the 650-cycle worst frame must stay under tCEM 8 us; 32-byte frames lower it to about 50 MHz, 16-byte frames to about 33 MHz (I: 8 cycles per byte over a 78-cycle overhead, from the 125 MHz model times) | ESP-PSRAM64H Table 10-5; `memory-devices.md` rows 11 and the PSRAM table |
| UART with `clk_peri` held fixed | UARTCLK <= 5/3 x PCLK: clk_sys >= 28.8 MHz at 48 MHz | DS 4.2.3.1, p. 420 (L31478) |
| SPI with `clk_peri` held fixed | SSPCLK <= PCLK: clk_sys >= 48 MHz | DS 4.4.3.4, p. 505 |
| Flash | SCK = clk_sys / 2 stays under 133 MHz at every setpoint; the RX sample delay of 1 clk_sys cycle is unaudited below 62.5 MHz | W25Q16JV 10.6; `flash.c:351` |
| Audio sample clock | exact only where clk_sys is a multiple of 8,000 Hz (the assertion at `picopcm.c:84`): 133 MHz is (16,625 cycles); 66.5 MHz (8,312.5 cycles) and 62.5 MHz (7,812.5) are not | `picopcm.c:84` |

### Which consumers tolerate a run-time change

| Consumer | `clk_peri` follows `clk_sys` | `clk_peri` held on `PLL_USB` (48 MHz) |
| --- | --- | --- |
| UART | re-derive IBRD and FBRD with the transmitter drained; a character in flight corrupts | none; 115,246 Bd (+0.040%) at every setpoint |
| SD SPI | quiet between system calls (the kernel switches no process inside a call and no lock covers a transfer, `sd.c:31-34`); re-derive CPSDVSR and SCR | none; CPSDVSR 4 gives 12.000 MHz; the identification clock gives SCR 59, exactly 400,000 Hz, which the tree's at-most rule accepts (SCR 60 gives 393 kHz with margin) |
| LCD SPI | as SD | none; one constant change, SCR 1 for 12.000 MHz (today 12.5) |
| PSRAM PIO | a frame with CS low completes first; the floor above applies | the same: PIO counts `clk_sys` |
| USB | none; the floor above applies | the same |
| Keyboard I2C | counts change only with the block disabled, between exchanges | the same |
| Audio PWM | re-derive TOP and levels, or pin the setpoint while `/dev/audio` is open | the same |
| SysTick, core 1 | rewrite both reloads, or source SysTick from its external reference (below) | the same |
| XIP flash | legal at any lower setpoint; flash writes and a clock change exclude each other | the same |
| ADC, TIMER, watchdog tick | independent (`PLL_USB`, `clk_ref`) | the same |

Decoupling `clk_peri` removes the UART, SD and LCD rows from every setpoint change at the cost of one-time rate changes (SD data 12.0 instead of 12.5 MHz, LCD 12.0 instead of 12.5 MHz) and the loss of the +6.4% on those two buses at 133 MHz. The rate differences are below any measured limit of those buses, which are bounded by the card (25 MHz) and the panel (15.15 MHz).

### SysTick on the external reference (hypothesis)

`SYST_CSR.CLKSOURCE` 0 selects the external reference, which the datasheet describes as the 1 us pulse of the watchdog's timer_tick used as a clock enable (DS 2.4.5.1.1, p. 75; SYST_CSR, p. 78); `SYST_CALIB.NOREF` resets to 0. The tree selects the processor clock (`clock.c:63`, `core1.c:210`). With the reference, the reload for HZ 1000 is 999 at every `clk_sys`, and core 1's 10 ms period is 9,999: D8 and D9 stop depending on the clock. Falsifier: over 10 s, delivered ticks differ from TIMERAWL's microseconds by more than the 1 us quantum, or SysTick fails to count in WFI. Renode's SysTick may not model the reference (unverified); the decisive run is on the board.

## 6. Power at each operating point and what verifies it

### Datasheet current

Figure 172 (DS 5.7.2.1, p. 624) plots DVDD current against core frequency for a typical device running FFTs on both cores, at 12, 50, 100 and 150 MHz, cold, room and hot, at 0.99, 1.10 and 1.21 V. Values below are read from the graph at 1.10 V, room temperature (I, plus or minus 0.3 mA): 4.8, 13.7, 23.6 and 33.4 mA. The 0.99 V series reads 4.3, 12.3, 21.2 and 30.0 mA.

| Point | clk_sys | DVDD current (linear between graph points) | DVDD power at 1.10 V | 3.3 V rail through the LDO | mA per MHz | Valid with |
| --- | --- | --- | --- | --- | --- | --- |
| Full | 133 MHz | 30.1 mA | 33 mW | 99 mW | 0.226 | all consumers after the section 2 rework |
| Legacy | 125 MHz | 28.5 mA | 31 mW | 94 mW | 0.228 | today's tree |
| Mid | 100 MHz | 23.6 mA | 26 mW | 78 mW | 0.236 | PSRAM (6.5 us frame) |
| Half | 66.5 MHz | 17.0 mA | 19 mW | 56 mW | 0.255 | USB; PSRAM only with 32-byte frames; audio pinned |
| Reference | 12 MHz | 4.8 mA | 5 mW | 16 mW | 0.40 | no USB; clk_ref direct, PLLs off |

The line through these points is about 3.7 mA plus 0.2 mA per MHz. Energy per clock cycle therefore falls as the clock rises (0.226 mA per MHz at 133, 0.255 at 66.5), so for a fixed amount of work, running at 66.5 MHz costs about 13% more DVDD charge than running at 133 MHz when the processor is busy either way. A lower setpoint saves energy only when it replaces idle time that is more expensive than the intercept. The datasheet gives sleep figures for a different state: 0.39 mA typical (4.5 mA maximum) in SLEEP with only `clk_rtc` enabled and 0.18 mA typical (4.2 mA) in DORMANT (Table 637, pp. 623-624). The DVDD current of a `WFI` at default gating and 125 MHz is not published and decides between running slow and finishing fast, so it is the first board measurement.

### Gating idle blocks

Table 635 (DS 5.7.1, p. 622) prices each block in μA per MHz of its clock when enabled and idle. On a bare Pico running the PICO kernel the blocks PIO0, PIO1, PWM, I2C0, I2C1, SPI0, SPI1, UART1, DMA and RTC sit at their reset gate value of enabled: 48.4 μA per MHz, 6.4 mA at 133 MHz, about a fifth of the figure above. On a PicoCalc the used blocks leave PIO1, UART1, I2C0, DMA and RTC: 23.8 μA per MHz, 3.2 mA. These are upper bounds (I): blocks held in reset may draw less than the table's clocked-and-idle state, and the table does not say whether its baseline held them in reset. Gating is the only power lever here that needs no re-derivation at all.

### What the PicoCalc can verify

- **The battery facts that cross to the RP2040**: percentage in 1% steps and a charging bit, refreshed every 20 s by the vendor firmware (`8bda3cf2:docs/research/picocalc-battery-charging.md`). A 13 mA saving moves a pack of C mAh by one percentage point in 0.01 x C / 13 hours; for an illustrative C of 3,000 mAh that is 2.3 hours. The pack capacity is the user's choice (the assembly guide has the user buy the 18650 cells), and the gauge's own accuracy is not specified, so the percentage cannot confirm a saving in a session.
- **Cell and system voltage**: the firmware's `printPMU()` prints them, reachable on header J702 or through the switched CH340; the kernel cannot read them without a firmware change (the same note, P4 and P5).
- **A current meter on the Pico's own micro-USB port**, which powers the Pico directly (gororoba-infrastructure `docs/design/picocalc-bench-interface.md`, section "Power facts"), measures the whole board including the panel and backlights, so it needs a fixed screen state to isolate the clock.
- **Software proxies for the mechanism, not the saving**: the idle share (`cp_time`, `kern_clock.c:103`), the frequency counter, and TIMERAWL against SysTick.

### Recommended operating points per board

| Kernel | Full | Low point | Why that low point | Fallback |
| --- | --- | --- | --- | --- |
| PICO, PICO_UART (bare Pico: no PSRAM, SD, panel or audio) | 133 MHz | 66.5 MHz (divider 2 of the 133 MHz PLL) | USB ratio 1.385 against the 1.10 floor; a UART console has no floor; no consumer pins it | 125 MHz, today's validated state |
| PICOCALC (SD, panel, keyboard, audio; no PSRAM) | 133 MHz | 66.5 MHz with `/dev/audio` pinning the point | the audio sample clock is inexact at 66.5 MHz (8,312.5 cycles); SD and LCD re-derive or ride a decoupled `clk_peri` | 125 MHz |
| PICOCALC_PSRAM (adds the PSRAM swap device) | 133 MHz | 100 MHz, a PLL change (12 x 100 / (6 x 2), VCO 1200 MHz, with a re-lock) | the one low point that passes PSRAM (6.5 us frame, 8 us limit), SD data (CPSDVSR 8, 12.5 MHz), the sample clock (12,500 cycles, exact) and USB; the carrier needs 200 levels for 500 kHz | 125 MHz |

A 66.5 MHz point on PICOCALC_PSRAM exists only after slice S6 adds a per-point PSRAM frame size (32 bytes, about 5.9 us at 66.5 MHz by the section 5 model, against 9.8 us for 64 bytes); until then that kernel has two points, 133 and 100 MHz. The PicoCalc kernels are the battery boards; the panel and backlight current is not in any source read, so every saving on them is measured against that fixed load (section 6).

## 7. Designs at three levels

Sizes are estimates (I) from measured comparable functions (B: GCC 14.2.1 20241119, -Os, the benchmark session's PICO image, sha256 prefix 1635904d; its source commit is not recorded): `core1_sysctl` 184 B, `core1_poll` 476 B, `uartinit` 224 B (baud included), `startup` 608 B (the PLL sequence included), `clkstart` 64 B, `clock_catchup` 156 B, `idle` 52 B, `cpu_sysctl` 908 B. Kernel text headroom is not stable across sources: 19,152 B on PICOCALC (`sys/arch/rp2040/doc/PICOCALC.md` row 60, GCC 13.2.1) against under 7 KB in `rp2040-second-core.md` after open driver changes; re-measure at slice time, and report every change in both kernel text and RAM, with the compiler version.

### Level 1: a fixed build-time choice

- **Mechanism.** One header defines the clock (`CLK_SYS_KHZ` and its PLL row: REFDIV, FBDIV, POSTDIV1, POSTDIV2). `SystemClock_Config()` writes the row; `CPU_KHZ` and `BUS_KHZ` derive from it; a `_Static_assert` in the same header proves (12 MHz / REFDIV) x FBDIV / (POSTDIV1 x POSTDIV2) equals the clock and the VCO sits in 750-1600 MHz. The two Config options collapse to one.
- **Kernel text and RAM.** 0 B to plus or minus 16 B of text (immediates replace immediates); 0 B of RAM. Rework of D3 and D5 is a few instructions of constant expression each.
- **Risks.** The four Configs and generated Makefiles regenerate from the generator, PICO and PICO_UART stay in step; a PicoCalc that loads through the UF2 loader keeps its loader flash divider until `flash_ssi_restore()` runs; 133 MHz has no margin above the rating.
- **Test plan without a board.** Compile every consumer at 125,000 and 133,000 kHz and at the mutants (below) in the `tests/picocalc` host harness; the PLL identity assertion; the Renode boot at 133 MHz with the repl frequency set from the same constant; `clockcalc.py selftest`.
- **Board measurements.** An hour of CoreMark and kernel tests at 133 MHz with USB console, SD transfers with CRC16 error counts at the chosen data clock, and a UART loopback; on the PicoCalc the PSRAM capture at 33.25 MHz SCK.

### Level 2: a manual run-time toggle among validated points

- **Mechanism.** A table of validated setpoints (on the bare Pico and PICOCALC 133,000 and 66,500 kHz, each a divider on one PLL setting, so no relock; on PICOCALC_PSRAM 133,000 and 100,000 kHz, a PLL change with a relock, see the per-board table in section 6). `machdep.cpu_khz` becomes read-write and accepts only table values (EINVAL otherwise). The setter runs one function: refuse when a flash write, a PSRAM frame, an SD transfer or an open `/dev/audio` is active; mask interrupts; write `CLK_SYS_DIV` (a pure divider change needs no source switch; the SDK's order for a combined source and divider change is divider first when slowing and source first when speeding up, DS L13696); rewrite the consumer counts D1-D9 through one hook each, or leave D1-D3 alone when `clk_peri` is decoupled; update `cpu_khz`; verify with the frequency counter. A small user program sets and shows the point; `picofetch` and `picotop` already display `cpu_khz`.
- **Kernel text and RAM.** About 350 to 600 B of text (setter 120-180 B, hooks 150-300 B, sysctl case 60-100 B, table 16-32 B) and 12 to 20 B of RAM (current point, a pending flag, the counter result). The user program is a few KB of `a.out` in the root image, outside the kernel budget; its size is measured at the slice.
- **Risks.** An I2C exchange that spans the change sees a different SCL; the flash SSI sample delay below 62.5 MHz is unaudited; the core 1 observer's SysTick; a change inside a declared masked section raises a false stall (`watchdog_site()`); two mutable copies of the clock (`cpu_khz` and the hardware) can diverge, which the frequency counter check closes.
- **Test plan without a board.** A host model of the switch sequence (quiesce, divider, re-derive, resume) with mutants: skip the quiesce, re-derive after resume, wrong divider order, an odd CPSDVSR, a PSRAM frame below the floor; the `clockcalc.py` arithmetic as the oracle for every consumer value; Renode toggling by sysctl with UART echo checked at both points and the tick count against TIMERAWL (the clock model's fidelity is unverified, section 9).
- **Board measurements.** The setpoint current table of section 6 with the ammeter; the SysTick reference test; USB enumeration surviving a switch; SD and PSRAM traffic across 1,000 switches.

### Level 3: an adaptive governor driven by idle time

- **Mechanism.** The kernel already counts ticks by state in `cp_time` (`sys/kern/kern_clock.c:103`). A callout once a second takes the busy fraction over a window and moves one step with hysteresis (up above 70% over 2 s, down below 25% over 5 s, for example), pinned to the top point while `/dev/audio` or a PSRAM transfer is active, the USB console is enumerating, or a flash write runs. With only two points the policy is one comparison.
- **Kernel text and RAM beyond Level 2.** About 250 to 400 B of text and 12 to 16 B of RAM (window counters, state, pin counts).
- **Risks.** A policy cannot save more than the section 6 table allows: moving from 133 to 66.5 MHz while busy costs 13% more charge per job, so the governor pays only on a workload that is idle part of the time and slow enough that a busy loop at the lower point finishes in the same wall time. Responsiveness: a keystroke after an idle second meets the lower point; the switch itself is one register write plus the hooks. Oscillation at the thresholds. Interaction with swap latency (an exec at 66.5 MHz decodes twice as slowly).
- **Test plan without a board.** A trace-replay host model of the policy (known-good: a steady load holds a point, a square-wave load toggles at its period; known-bad mutants: no hysteresis oscillates, no pin lets a PCM underrun through) and the Level 2 plan.
- **Board measurements.** Charge per job at each point and under the governor on a mixed trace, which decides whether the policy earns its text.

## 8. Build-out plan in dependency order

| Slice | Work | Gate or evidence | Depends on |
| --- | --- | --- | --- |
| S1 | One clock constant: `CLK_SYS_KHZ` with its PLL row, `CPU_KHZ` and `BUS_KHZ` derived, the Config options reduced to one, the PLL identity assertion | a host gate (a `tests/picocalc` target and a `TESTING.md` row) that compiles every clock consumer at two or more clock values and the PLL fields against `CPU_KHZ`; known-bad: today's tree at `CPU_KHZ=133000`, which the gate must reject; known-good: 125,000. This differs from the brief's gate, a failure on a hardcoded 125000000: the nine-digit literal occurs once, in `pico_uart.repl:14`, and H1 to H7 contain no such literal, so a search cannot see them. The literal search stays as a secondary check that the Renode and test literals read the constant | none |
| S2 | Consumer rework so every row of section 2 holds at every validated clock: even CPSDVSR rounding (D3), carrier levels (D5), a PSRAM frame budget assertion from `CPU_KHZ` (H5), the Renode and test literals read from the constant | the S1 gate at 125,000, 133,000, 132,000 and 126,000, with the mutants of this note | S1 |
| S3 | 133 MHz as the PICO and PICO_UART default; PICOCALC after the PSRAM capture | one hour soak and the tier set on the board; Renode boot at 133 | S2 |
| S4 | SysTick and core 1 on the external reference | the TIMERAWL comparison on the board; the Renode result recorded as the model's | S1 |
| S5 | Clock gating of unused blocks, measured | ammeter on bare Pico: 125 MHz with all gates open, then gated | S1 |
| S6 | The run-time setter, the read-write `cpu_khz`, consumer hooks, the per-point PSRAM frame size (the 66.5 MHz point of PICOCALC_PSRAM), the frequency-counter check | the host switch model, Renode toggle, then the board table of section 6 | S2, S4 |
| S7 | The toggle program and the `picofetch`, `picotop` display of the live point | host tests for the program's sysctl parse, calibrated with a malformed value | S6 |
| S8 | The governor | trace-replay model; board charge per job | S6, S5 |

Eight slices. S1 to S3 deliver the owner's 133 MHz; S6 and S7 deliver the manual toggle with an application; S8 is the adaptive form. The core-1 split-stream decode has its own dependency chain in `rp2040-second-core.md` and does not depend on any slice here.

## 9. Residual owner decisions

1. **Shipped clock.**
   - 133 MHz for PICO and PICO_UART first (recommended): +6.4% measured on the bare Pico, with no margin above the rating, and the PicoCalc kernels wait for the PSRAM capture because their PSRAM sampling margin shrinks by 14% at 133.
   - 125 MHz everywhere: the datasheet's own nominal and the validated state; gives up the 6.4%.
   - 132 MHz: exact, 0.75% below the ceiling, keeps the APS6404L Read ID at its limit; gives up 0.8% against 133.
2. **SD data clock at 133 MHz.**
   - CPSDVSR 12, 11.083 MHz (recommended): keeps the tree's "under 12.5 MHz" rule and a conservative card margin; 11% below today.
   - CPSDVSR 10, 13.300 MHz: 6.4% above today, still at 53% of the 25 MHz default-speed limit; needs a CRC16 error count to justify.
   - Raise the ceiling to 25 MHz (`memory-devices.md` M10): the larger gain, separate measurement and risk.
3. **Audio carrier.**
   - `PCM_LEVELS` 266, 500,000 Hz exactly (recommended): the carrier stays where the output filter was designed for; the duty resolution rises from 250 to 266 steps.
   - Keep 250 levels and accept 532 kHz: no level change, a carrier 6.4% higher than the one the board's filter was assumed against.
4. **PLL form at 133 MHz.**
   - REFDIV 1, FBDIV 133, post dividers 6 and 2, VCO 1596 MHz (recommended): the datasheet's low-jitter direction and the smallest code change.
   - REFDIV 2, FBDIV 133, post dividers 6 and 1, VCO 798 MHz: lower PLL power at higher jitter, no power figure in the extracts read; decided only by a board measurement.
5. **Hold `clk_peri` on `PLL_USB`.**
   - Yes: UART, SD and LCD never re-derive at a setpoint change, and the UART error is 0.040%; one-time rate changes of about 4% on SD and LCD.
   - No (recommended until Level 2 starts): rates stay on `clk_sys`, and Level 2 carries the hooks for three consumers.
6. **How far to take run-time clocking.**
   - A build-time choice only (S1-S3): the least text and risk.
   - The manual toggle with its program (S1-S7, recommended as the target): about 350 to 600 B of kernel text and 12 to 20 B of RAM for the owner's "at least a manual toggle clock".
   - The governor (S8): adds 250 to 400 B and a policy whose saving rests on the section 6 idle-current measurement that does not yet exist.
7. **Elevated core voltage and 200 MHz.**
   - Do not pursue (recommended): beyond the all-conditions rating, and the 4.5% saving available at 1.05 V sits on the rating's lower edge.
   - Study separately: requires the VREG sequence, the flash and PSRAM limits, thermal data and the owner's acceptance of operation outside the stated PVT.

## 10. What is unverified

- **Hardware at 133 MHz beyond two runs.** Two passes of a seven-command benchmark each, PICO config, bare Pico, room temperature, about 78 s of load per pass, all return codes 0 (M, `RESULTS.md` D127). No soak, no temperature, no PicoCalc, no PSRAM, no SD, no USB-heavy run at 133 MHz.
- **The PicoCalc PSRAM sampling margin** at 33.25 MHz SCK: arithmetic only; the pad delays come from the 1.8 V figures; the board delay is unknown.
- **The fitted PSRAM part.** The ESP-PSRAM64H is a schematic reading (U301); no board has identified it, and Read ID is the frame that exceeds the APS6404L limit at 133 MHz.
- **Figure 172 values** are graph readings, plus or minus 0.3 mA, for a dual-core FFT, not for this kernel's workload; the WFI idle current is not published.
- **PLL lock time and the glitchless mux latency** are not in the extracts read; a PLL re-lock setpoint (as opposed to a divider setpoint) has no timing figure.
- **SysTick on the external reference** has no run in this tree.
- **Renode's clock model.** `tools/renode/vendor` is not fetched on this machine and no patch in `tools/renode/patches` touches the clocks block; the PL022 model takes an `RP2040Clocks` object (patch 0008) and the PL011 a platform frequency (patch 0009). Whether Renode honors `CLK_SYS_DIV` or the PLL registers is unexamined.
- **The notes repository's RP2040 datasheet equals the public download** of 2026-11-16 by SHA-256; the Pico datasheet likewise. The W25Q16JV revision G download source is not recorded (notes `docs/rp2040/README.md`).
- **XPowersLib and the vendor firmware** are the battery note's sources; nothing here adds to them.
- **Sizes** are from one benchmark ELF whose source commit is unrecorded; slice-time builds replace them.

## Sources

Notes repository `discobsd-2040-notes` at 5167978, with the SHA-256 of each file and, where a public URL exists, the URL (fetched 2026-11-16):

| File | Revision | Retrieved from | SHA-256 |
| --- | --- | --- | --- |
| `docs/rp2040/rp2040-datasheet.pdf` | build 3184e62-clean, 2025-02-20, 642 pages, RP-008371-DS-1; the public download returned the same bytes | https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf | be56fbb75ba0ae9e26558a73c93ac3e75c2ad4e6878d3b6703de2a76d886ea8c |
| `docs/rp2040/pico-datasheet.pdf` | build d4e0f1799616; the public download returned the same bytes | https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf | 757ff485227493b9fcc0c2c96c4dea9de020e1d8b2b11e2aa4f9ee8b25aa89eb |
| `docs/rp2040/W25Q16JV_Datasheet.pdf` | revision G, 2018-03-22 | not recorded | 6d23cdf57fd63fc2c2de517707e293beaf70f3f47b424bf58510666294c04d82 |
| `docs/picocalc/esp_psram64_esp_psram64h_datasheet_en_v1.1_wayback.pdf` | V1.1, 2020-10 | Wayback copy of Espressif's URL of 2025-10-09 | ab68624cdecfc6a768260747bec907aa20940f14129f269b58c3b2a52acd0605 |
| `docs/picocalc/aps6404l_3sqr_qspi_psram_datasheet_rev2.7_apmemory.pdf` | rev 2.7 | the notes repository | 8defcc47238ab297a8780220fed57c8a8d39e265b8d7e50661bfbfc5be0e3bb4 |
| `docs/picocalc/AXP2101_Datasheet_V1.4.pdf` | V1.4 | https://github.com/clockworkpi/PicoCalc (repository root) | 9e51d16d84d905c32a001a123be8e6c58213ee21e42d5bdb1458b731dba10f6d |

gororoba-workspace repository (read-only), cited repository-relative:

| File | Use | SHA-256 |
| --- | --- | --- |
| `gororoba-infrastructure/local/discobsd-bench/RESULTS.md` | the 133 MHz and 125 MHz benchmark session, D124, D126, D127 | 122ffb1fb2ca162eb2cda663bcada80516e08d580073689ce3580eff83110344 |
| `gororoba-documents-technical/engineering/devices/raspberry-pi/raspberry_pi_rp2040_datasheet.pdf` | the same 2025-02 build as the notes copy | be56fbb75ba0ae9e26558a73c93ac3e75c2ad4e6878d3b6703de2a76d886ea8c |
| `gororoba-documents-technical/engineering/devices/raspberry-pi/raspberry-pi_rp2040_datasheet.pdf` | the 2024-10 build, 639 pages, without the 133 MHz all-conditions note | 1deb0b9e9ec59c33e41220c434d9857cab64d2d3f922bb54a810a65989066e24 |
| `gororoba-infrastructure/docs/design/picocalc-bench-interface.md` | the Pico's own USB port powers the Pico directly | a6318adfb9f0e0d0fbfd8ae9c49cee794c05fe5207c04e9a8e2efa87cb62c956 |
| `gororoba-infrastructure/bom/picocalc/lines.json` | seven lines; the cells are two protected 18650 cells in parallel with no capacity stated, and no PSRAM line | 6a464c3ab733dacef6bb4acbd6496153c84def3033820405047b7dfc3642ddca |

Search scope in the gororoba-workspace and gororoba-quantum-documents repositories, 2026-11-16: a text search (`rg`, case-insensitive, excluding PDF, JSON, binary and image files) for `rp2040`, `picocalc` and `axp2101` matched 31 files in gororoba-workspace and none in gororoba-quantum-documents. Read for clock, power or cell content: the bench-interface note, the RP2040 product brief transcription, `hardware-open-work.md`, `field-meter-spec.md`, the PSRAM-capture session handoff, and `bom/picocalc/lines.json` and `prices.jsonl` (empty). The remaining matches are bills of materials for other instruments, orchestration boards and session handoffs; none was read for clock content. JSON files other than `bom/picocalc/lines.json` were not searched.

Other repository notes: `d2ffbd83:docs/research/memory-devices.md` (PSRAM, flash, SD rows; not on `main`), `8bda3cf2:docs/research/picocalc-clocks-timers.md` and `8bda3cf2:docs/research/picocalc-battery-charging.md` (PR #657), `docs/research/rp2040-second-core.md`, `docs/research/picocalc-psram-capture.md`, `sys/arch/rp2040/doc/CORE1.md`.

The consumer arithmetic reproduces with `docs/research/rp2040-clock-configuration/clockcalc.py` (`python3 -I clockcalc.py table 125 133`, `pll 133`, `selftest`).
