# RP2040 block ledger

The ledger lists every peripheral and memory block that the Pico SDK 2.3.1 address map names (`hardware/regs/addressmap.h`, 53 `*_BASE` entries; the SRAM banks and the XIP cache aliases share the SRAM and XIP rows, and `USBCTRL` and `USBCTRL_DPRAM` the USB rows, which gives 42 rows), with the datasheet section that describes it, the SDK register header, the port source that touches it and how the port uses it. It establishes the register-level footprint of the source tree at the revision below and says nothing about run-time behavior: a block marked `used` has code that addresses it, and a block marked `unused` has none outside the default interrupt vector stubs.

Source revision: main at `2cca67efe`. SDK: the `pico-sdk` 2.3.1 installation that `tools/pico-sdk/sdk-path.sh` resolves (the tag `tools/pico-sdk/fetch-pico-sdk.sh` pins). Datasheet sections and pages come from `sys/arch/rp2040/doc/DATASHEET-INDEX.md` (printed page numbers).

## Use states

| State | Meaning |
| --- | --- |
| used | the port runs the function the datasheet section names (clocks run, a block released from reset, a watchdog armed, a core power-cycled, a bus served) |
| partial | the port reads or writes registers of the block for a purpose other than that function (an identification read, a probe, a pin sampled through a pad, a counter, a random bit) and runs none of the block's own function |
| boot only | only the boot stage (boot2) touches it |
| test only | only a board test program touches it |
| unused | no register access; at most the default interrupt handler of the block |

Counts, from the rows below: 29 used, 4 partial, 1 boot only, 1 test only, 7 unused (42).

## Ledger

| Block | Base | Datasheet section | SDK header | State | Port use |
| --- | --- | --- | --- | --- | --- |
| ROM | `0x00000000` | 2.6.1 ROM (also 2.8 Bootrom), p.120 | `addressmap.h (no register header)` | used | mpu.c grants region 0 (the 16 KB boot ROM, r-x) so that user code reaches the float routines through lib/libc/arm/gen/rom_float_resolver.S; flash.c reads the function table pointers at 0x14 and 0x18 (ROM_FUNC_TABLE_ADDR, ROM_TABLE_LOOKUP_ADDR) to find the flash erase and program routines. |
| XIP_MAIN | `0x10000000` | 2.6.3 Flash (XIP), p.122 | `addressmap.h` | used | the kernel text runs through the cached XIP window (linker scripts, boot2). |
| XIP_CTRL | `0x14000000` | 2.6.3 Flash (XIP cache), p.122 | `xip.h` | used | flash.c disables and flushes the cache around erase and program; sysctl.c reads the cache hit and access counters. |
| XIP_SRAM | `0x15000000` | 2.6.3 Flash (XIP cache as SRAM), p.122 | `addressmap.h` | test only | usr.bin/mputest probes the window; no kernel use. |
| XIP_SSI | `0x18000000` | 4.10 SSI, p.567 | `ssi.h` | used | boot2 programs the SSI for quad read; flash.c writes BAUDR and RX_SAMPLE_DLY through XIP_SSI_REG (0x18000000) to re-time the flash clock and restores them after the boot ROM routines (SSIENR is 0 for the write), and its flash_cmd() drives the SSI data register (SSI_DR0) for the JEDEC and unique-ID reads of flash_identity(). |
| SRAM | `0x20000000` | 2.6.2 SRAM, p.121 | `addressmap.h` | used | kernel text-relative data, bss and the process window (linker scripts conf/RP2040.ld and RP2040-sd.ld); usr.bin/mputest reads four words of the unstriped aliases (SRAM_ALIAS, `0x21000000` through the bank 3 window) to show that no MPU region covers them: word 0 of banks 0 and 3 and the kernel-data word of each. |
| SRAM4 | `0x20040000` | 2.6.2 SRAM (unstriped banks), p.121 | `addressmap.h` | used | SRAM4 and SRAM5 hold boot2 and the scratch region (conf/RP2040.ld, RP2040-sd.ld SCRATCH). |
| SYSINFO | `0x40000000` | 2.20 Sysinfo, p.305 | `sysinfo.h` | partial | machdep.c reads CHIP_ID. |
| SYSCFG | `0x40004000` | 2.21 Syscfg, p.306 | `syscfg.h` | partial | core1.c uses PROC0_NMI_MASK to probe core 1; machdep.c defines the base. |
| CLOCKS | `0x40008000` | 2.15 Clocks, p.178 | `clocks.h` | used | machdep.c SystemClock_Config() disables clk_peri, clk_usb, clk_adc, clk_rtc and clk_gpout0-3, locks the PLLs and re-enables clk_peri and clk_usb; clk_ref and clk_sys switch through their glitchless muxes. |
| RESETS | `0x4000c000` | 2.14 Subsystem Resets, p.172 | `resets.h` | used | machdep.c and the drivers (adc.c, gpio.c, uart.c and others) release their block from reset. |
| PSM | `0x40010000` | 2.13 Power-On State Machine, p.168 | `psm.h` | used | core1.c forces core 1 off and on (FRCE_OFF) and selects the watchdog reset set (WDSEL); watchdog.h defines the base. |
| IO_BANK0 | `0x40014000` | 2.19 GPIO (IO_BANK0), p.236 | `io_bank0.h` | used | function select in machdep.c, dev/uart.c and the PicoCalc register macros (dev/picocalc_reg.h). |
| IO_QSPI | `0x40018000` | 2.19 GPIO (IO_QSPI), p.236 | `io_qspi.h` | used | dev/flash.c flash_cmd() overrides the SS output (IO_QSPI_SS_CTRL_REG, the GPIO_QSPI_SS control register) to hold chip select low for the transfers of flash_identity(); machdep.c bootsel_pressed() floats the same pin to read the BOOTSEL button (the Pico SDK picoboard/button sequence, run from RAM). |
| PADS_BANK0 | `0x4001c000` | 2.19 GPIO (PADS_BANK0), p.236 | `pads_bank0.h` | used | pad configuration in dev/uart.c and the PicoCalc register macros (dev/picocalc_reg.h). |
| PADS_QSPI | `0x40020000` | 2.19 GPIO (PADS_QSPI), p.236 | `pads_qspi.h` | boot only | boot2 sets the QSPI pad drive and slew. |
| XOSC | `0x40024000` | 2.16 Crystal Oscillator (XOSC), p.216 | `xosc.h` | used | machdep.c starts the crystal and waits for STABLE. |
| PLL_SYS | `0x40028000` | 2.18 PLL, p.228 | `pll.h` | used | machdep.c locks the system PLL at 125 MHz. |
| PLL_USB | `0x4002c000` | 2.18 PLL, p.228 | `pll.h` | used | machdep.c locks the USB PLL at 48 MHz. |
| BUSCTRL | `0x40030000` | 2.1 Bus Fabric, p.14 | `busctrl.h` | partial | sysctl.c exposes the bus performance counters (machdep.bus_perfctr0-3 and bus_perfsel0-3). |
| UART0 | `0x40034000` | 4.2 UART, p.417 | `uart.h` | used | dev/uart.c (the console fallback on GP0/GP1). |
| UART1 | `0x40038000` | 4.2 UART, p.417 | `uart.h` | used | dev/uart.c, second unit table entry. |
| SPI0 | `0x4003c000` | 4.4 SPI, p.501 | `spi.h` | used | dev/sd.c: the PicoCalc SD card on SPI0. |
| SPI1 | `0x40040000` | 4.4 SPI, p.501 | `spi.h` | used | dev/st7365p.c: the PicoCalc panel on SPI1. |
| I2C0 | `0x40044000` | 4.3 I2C, p.440 | `i2c.h` | unused | only the default interrupt handler stub in locore0.S. |
| I2C1 | `0x40048000` | 4.3 I2C, p.440 | `i2c.h` | used | dev/picokbd.c: the PicoCalc keyboard controller. |
| ADC | `0x4004c000` | 4.9 ADC and Temperature Sensor, p.557 | `adc.h` | used | dev/adc.c (machdep.adc.* sysctls). |
| PWM | `0x40050000` | 4.5 PWM, p.521 | `pwm.h` | used | dev/gpio.c (user PWM duty), picoaudio.c and picopcm.c (audio). |
| TIMER | `0x40054000` | 4.6 Timer, p.534 | `timer.h` | used | rp2040/clock.c reads TIMERAWL for the clock; dev/usb.c timestamps start-of-frame and transmit; core1.c uses it for bounded waits. |
| WATCHDOG | `0x40058000` | 4.7 Watchdog, p.544 | `watchdog.h` | used | machdep.c sets the watchdog tick (TICK 12 cycles), reads REASON, disables and re-arms the counter (CTRL, LOAD with the pause bits) and keeps the swap cursor in SCRATCH0 across a software reset; core1.c writes SCRATCH1-3 (the stall site records) and triggers a reset through CTRL; sysctl.c reports the reason, site and arguments (machdep.watchdog_*); include/watchdog.h defines the registers. |
| RTC | `0x4005c000` | 4.8 RTC, p.548 | `rtc.h` | unused | machdep.c gates clk_rtc off; time of day comes from the timer. |
| ROSC | `0x40060000` | 2.17 Ring Oscillator (ROSC), p.221 | `rosc.h` | partial | machdep.c swap_cursor_init() samples RANDOMBIT (seven bits per try, up to eight tries) to pick the first swap allocation when watchdog scratch0 holds no tagged cursor. |
| VREG_AND_CHIP_RESET | `0x40064000` | 2.10 Core Supply Regulator, 2.12 Chip-Level Reset, p.155 | `vreg_and_chip_reset.h` | unused | the boot ROM leaves the regulator at its default. |
| TBMAN | `0x4006c000` | 2.22 TBMAN, p.309 | `tbman.h` | unused | - |
| DMA | `0x50000000` | 2.5 DMA, p.91 | `dma.h` | unused | only the default interrupt handler stubs; the asynchronous panel DMA of the PicoCalc plan is not built. |
| USBCTRL_DPRAM | `0x50100000` | 4.1 USB, p.383 | `usb_device_dpram.h` | used | dev/usb.h defines USB_DPRAM_BASE; dev/usb.c is the CDC-ACM console. |
| USBCTRL_REGS | `0x50110000` | 4.1 USB, p.383 | `usb.h` | used | dev/usb.h defines USB_REGS_BASE; dev/usb.c. |
| PIO0 | `0x50200000` | 3 PIO (3.7 List of Registers), p.368 | `pio.h` | used | dev/psram_pio.h and psram_xfer.c: the PicoCalc PSRAM on PIO0. |
| PIO1 | `0x50300000` | 3 PIO (3.7 List of Registers), p.368 | `pio.h` | unused | - |
| XIP_AUX | `0x50400000` | 2.6.3 Flash (streaming), p.122 | `xip.h (XIP_AUX)` | unused | - |
| SIO | `0xd0000000` | 2.3.1 SIO, p.27 | `sio.h` | used | core1.c and corering.c (inter-core FIFO), lib/libc/arm/gen/aeabi_div.S and rp2040/syscall.c (hardware divider and its per-core save and restore), dev/gpio.c, dev/sd.c and dev/picocalc_reg.h (GPIO input, output, set, clear and xor), machdep.c (BOOTSEL read), rp2040/mpu.c (the SIO region the divider needs) and usr.bin/mputest (probes of the SIO window); usr.bin/ulisp/builtins.def names the SIO GPIO registers as Lisp register constants. |
| PPB | `0xe0000000` | 2.4 Cortex-M0+ (2.4.4 System control, 2.4.5 NVIC, 2.4.6 MPU), p.62 | `m0plus.h` | used | include/intr.h (NVIC set-enable), include/scb.h (SCB and SysTick registers), include/mpu.h and rp2040/mpu.c (MPU and the break guard); boot2 sets VTOR. |

## Unused blocks

DMA, I2C0, PIO1, RTC, TBMAN, VREG_AND_CHIP_RESET and XIP_AUX have no register access in the tree. The interrupts of those blocks that have one (DMA, I2C0, PIO1 and RTC) have only the default handler stub in `sys/arch/rp2040/rp2040/locore0.S`; `machdep.c` gates `clk_rtc` off, which touches the CLOCKS block and not RTC.

## Method

The base addresses and header names come from the SDK; the first two commands below check that the resolved SDK is 2.3.1 and carries the 53 entries the ledger counts, and a different release or entry count invalidates the denominator. Each block was searched in the port with its `*_BASE` name and its hexadecimal base, and every hit was read in place before a state was recorded:

```sh
sdk=$(sh tools/pico-sdk/sdk-path.sh)
grep -E 'set\(PICO_SDK_VERSION_(MAJOR|MINOR|REVISION) ' "$sdk/pico_sdk_version.cmake"   # 2, 3 and 1
grep -c '_BASE _u(' "$sdk/src/rp2040/hardware_regs/include/hardware/regs/addressmap.h"      # 53
grep -n '_BASE _u(' "$sdk/src/rp2040/hardware_regs/include/hardware/regs/addressmap.h"
git grep -n -i -E '(NAME_BASE|0xBASE)' -- sys/arch/rp2040 usr.bin lib sbin bin ':!sys/arch/rp2040/doc' ':!sys/arch/rp2040/compile' ':!*.md' ':!*.tsv' ':!*.json'
```

The seven unused blocks were searched over the whole tree (`git grep -l -i -E '(NAME_BASE|0xBASE)' -- . ':!docs' ':!legacy' ':!third_party' ':!sys/arch/rp2040/doc' ':!*.md' ':!*.tsv' ':!*.json' ':!tools/audit'`): I2C0, PIO1, TBMAN, VREG_AND_CHIP_RESET and XIP_AUX have no hit; the DMA and RTC names hit only the STM32 HAL headers of another architecture and the `0x50000000` constant of `tests/kernel/balloc_test.c`, none of them an RP2040 access.

A block reached through a macro with a different spelling (an offset base such as `PC_IO_BANK0_CTRL(n)` in `dev/picocalc_reg.h`, or a full register address such as `IO_QSPI_SS_CTRL_REG` at `0x4001800c` in `dev/flash.c`) escapes a search for the block name or its exact base. `tools/rp2040_block_scan.py` closes that gap: it reads the block bases of the same SDK map, gives each block the address range its datasheet section covers (16 KB for an APB block, 64 KB for the AHB blocks and SIO, 1 MB for the PPB), and classifies every eight-digit hexadecimal literal of the tracked sources under `sys/arch/rp2040`, `usr.bin`, `lib`, `sbin` and `bin` by the block that contains it. Its `--selftest` rejects a range that reaches past a block and a literal pattern that misreads a suffix or a longer literal. A register-level audit would repeat the search per register name; this ledger stops at the block.

## Appendix: files with a literal address in each block

Output of `python3 tools/rp2040_block_scan.py` at the recorded revision, one row per block that has a hit (paths under `sys/arch/rp2040` drop that prefix). A hit is a lead to read, not a use: the SYSINFO range holds `lib/libc/gen/calloc.c` (a comment about 0x40000001 elements), `dev/sdspi.c` (0x40000000, the host-capacity bit of the SD operating condition) and `usr.bin/pforth/pfinnrfp.h` (a floating-point constant), none an access; the SIO hit in `usr.bin/smlrc/tests/run.sh` is a comment about an address that `qemu-arm` never maps.

| Block | Files |
| --- | --- |
| XIP_CTRL | dev/flash.c, rp2040/sysctl.c |
| XIP_SSI | boot2/boot2_w25q080.S, dev/flash.c |
| SYSINFO | lib/libc/gen/calloc.c, dev/sdspi.c, rp2040/machdep.c, usr.bin/pforth/pfinnrfp.h |
| SYSCFG | rp2040/core1.c, rp2040/machdep.c |
| CLOCKS | dev/adc.c, rp2040/machdep.c |
| RESETS | dev/adc.c, dev/gpio.c, dev/picocalc_reg.h, dev/uart.c, dev/usb.c, rp2040/core1.c, rp2040/machdep.c |
| PSM | include/watchdog.h, rp2040/locore0.S |
| IO_BANK0 | dev/adc.c, dev/gpio.c, dev/picocalc_reg.h, dev/uart.c, rp2040/machdep.c |
| IO_QSPI | dev/flash.c, rp2040/machdep.c |
| PADS_BANK0 | dev/adc.c, dev/gpio.c, dev/picocalc_reg.h, dev/uart.c, rp2040/machdep.c |
| PADS_QSPI | boot2/boot2_w25q080.S |
| XOSC | rp2040/machdep.c |
| PLL_SYS | rp2040/machdep.c |
| PLL_USB | rp2040/machdep.c |
| BUSCTRL | rp2040/sysctl.c |
| UART0 | dev/uart.h |
| UART1 | dev/uart.h |
| SPI0 | dev/sd.c |
| SPI1 | dev/st7365p.c |
| I2C1 | dev/picokbd.c |
| ADC | dev/adc.c |
| PWM | dev/gpio.c, dev/picoaudio.c, dev/picopcm.c |
| TIMER | dev/picocalc_reg.h, dev/usb.c, rp2040/clock.c, rp2040/core1.c, rp2040/machdep.c |
| WATCHDOG | include/watchdog.h |
| ROSC | rp2040/machdep.c |
| USBCTRL_DPRAM | conf/kern.ldscript, dev/usb.h |
| USBCTRL_REGS | dev/usb.h |
| PIO0 | dev/psram_pio.h |
| SIO | lib/libc/arm/gen/aeabi_div.S, dev/gpio.c, dev/picocalc_reg.h, dev/sd.c, rp2040/corering.c, rp2040/machdep.c, rp2040/mpu.c, rp2040/syscall.c, usr.bin/mputest/mputest.c, usr.bin/smlrc/tests/run.sh, usr.bin/ulisp/builtins.def |
| PPB | boot2/boot2_w25q080.S, dev/lcdcons.c, dev/picopcm.c, include/intr.h, include/scb.h |

## Open

- The per-register use inside the `partial` blocks (SYSINFO, SYSCFG, ROSC, BUSCTRL) and the `used` blocks that touch a few registers (PSM, WATCHDOG, IO_QSPI) is listed in the Port use column as the registers the search found; it is not audited against each register of the SDK header.
- The Port use column names the principal users of a block, read in place. The appendix lists every tracked file that holds a literal address inside the block, which is the complete set of leads at the recorded revision; a block shared by many drivers (RESETS, IO_BANK0, PADS_BANK0, SIO) has users in the appendix that its row does not name.
- PWM, TIMER and SIO have several users; the column names the principal ones.
