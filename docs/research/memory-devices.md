# Memory devices of the Pico and the PicoCalc: wiring, drivers and plan

This note lists every RAM, ROM and storage device the RP2040 can reach on a
bare Raspberry Pi Pico and on the ClockworkPi PicoCalc with a Pico in its
socket. It checks how each one is wired against the schematic and the data
sheets, audits the drivers in the tree, and decomposes the remaining driver
work into slices of one pull request each. It extends
`docs/research/picocalc-device-first-memory.md` and its cycle model on the
branch of pull request #644 (cited as `77e33cf9:path`), and
`docs/research/picocalc-psram.md`. It does not repeat them. Owner decisions
cited by number (D9, D42, D44, D97, D98) are quoted where they bind.

Evidence classes: schematic (S), data sheet (DS), source (SRC), build (B),
host simulation or model (M), and inference (I). The one execution for this note
is the host gate `check-psram-pio`; no board, Renode or qemu run happened. The PSRAM driver has never run on a board:
`sys/arch/rp2040/doc/PICOCALC.md` rows "The part answers Read ID ..." and
"The kernel swaps to PSRAM ..." read "not run". Every PSRAM latency below is
therefore a model figure or arithmetic from the data sheet. The tree was read
at `origin/main` 4b3eaf6c. Kernel sizes come from a build of that commit with
arm-none-eabi-gcc 14.2.1 (Debian package `15:14.2.rel1-1`) on a separate
Linux build host, and the same compiler built #637's head 812ea3e1 for the
PICOCALC_LEAN figures.

## Finding and short answer

The PicoCalc's PSRAM, U301, is an ESP-PSRAM64H (8 MB). All four of its data
lines are wired to the Pico, so it is quad-capable as wired: SIO0-SIO3 on
GP2-GP5, CE# on GP20 and SCLK on GP21 (S). Those six pins are general GPIOs,
not the RP2040's QSPI execute-in-place pads, and no hardware SPI pin
assignment fits them. Only a PIO state machine can clock the part, so it works
as a block device that copies data in and out, never as memory the processor
addresses (S, DS).

It is therefore more than a /tmp disk. At the shipped 31.25 MHz quad clock, a
64-byte frame holds chip select low for 4.45 us on a write and 4.72 us on a
read. With setup, a frame costs at most 5.2 us with interrupts masked (M),
about 12 MB/s. A shell-sized swap image of 16,776 bytes moves in about 1.4 ms
each way (M). The 16 KB compressed pool takes 256 ms to admit the same image
(M, `77e33cf9:docs/research/picocalc-device-first-memory.md`), and `fl1`
needs 257 ms of erase and program time (DS arithmetic). Those are figures per
image, not per page. The ordering survives a tenfold software overhead that
the simulator does not count. Swap is the best fit, a /tmp RAM disk the second
(pull request #624 splits the part 2 MB swap, 6 MB /tmp), and a second-level
cache for SD blocks the third. Process memory reaches it only by copying,
which is what swap already does.

The schematic matches every pin assignment in the tree (method under "Pin
cross-check"), so no contradiction leads this note. Three wiring facts lead it
instead; a search of `sys/arch/rp2040/doc/PICOCALC.md`, `docs/research` and
the notes repository's `docs/picocalc/INDEX.md` for ALDO2, R302 and DAT1
finds none of them recorded before:
- **Supply.** U301 is powered from the AXP2101's ALDO2 rail, not from the
  Pico's 3V3. The kernel cannot switch that rail, and its voltage is an EFUSE
  default that no document states.
- **Chip-select pull-up.** CE# is pulled up through R302 (10 kOhm) to the
  Pico's 3V3_OUT. During an RP2040 reset the pad's default pull-down
  (50-80 kOhm) holds CE# at 2.75-2.93 V. VIH(min) is Vcc - 0.4 V, which is
  2.9 V at 3.3 V, so the part is not guaranteed deselected while the RP2040
  is in reset.
- **SD socket.** The socket wires DAT1 and DAT2 to nothing, so the card runs
  in SPI mode, or at most in 1-bit SD mode, never in 4-bit SD mode.

## Inventory

### Bare Pico (RP2040 B2 and the Pico module)

| # | Device | Part, source | Capacity | Path from the cores | Access time and throughput bound | Volatility, endurance | Tree use |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Striped SRAM0-3 | RP2040 (DS 2.6.2) | 256 KB, four 64 KB banks | 0x20000000, AHB-Lite crossbar | crossbar bandwidth bound of one word per bank per clock at 125 MHz (DS); a Cortex-M0+ LDR or STR takes two cycles on zero-wait-state SRAM (`docs/research/rp2040-second-core-hardware.md:25`) | volatile, unlimited | user window 192 KB, kernel data, u areas, SwapRAM pool (`conf/kern.ldscript:135-137`) |
| 2 | SRAM4, SRAM5 | RP2040 (DS 2.6.2) | 2 x 4 KB | 0x20040000, 0x20041000 | as row 1, unstriped | volatile | USB transmit ring, 8 KB, or the Dhara page and boot2 copy on UART kernels (`kern.ldscript:159-190`) |
| 3 | Boot ROM | RP2040 mask ROM (DS 2.6.1, 2.8) | 16 KB | 0x00000000, executable | one cycle per fetch (DS) | fixed at manufacture | flash erase and program, `reset_usb_boot` (`dev/flash.c:118-122`, `dev/usb.c:156`), float routines for user code (`doc/MPU.md` region 0) |
| 4 | XIP cache | RP2040 (DS 2.6.3.1) | 16 KB | cache for 0x10000000; usable as SRAM at 0x15000000 only with the cache disabled (DS text "If flash XIP caching is disabled, the cache becomes available as a 16kB memory") | cache hit one cycle; with the cache off every kernel instruction fetch goes to the flash chip | volatile | caches the kernel's text; `flash_xip_cache()` turns it off for a measurement only (`dev/flash.c:318`) |
| 5 | USB DPSRAM | RP2040 USB controller (DS 4.1.2.7) | 4 KB | 0x50100000, peripheral bus, execute-never under the MPU map | word access, no set or clear aliases (DS) | volatile | endpoints, Dhara page, codec workspace 1,040 B at 0x50100740, flash scratch (`kern.ldscript:172-185`) |
| 6 | QSPI flash array | W25Q16JVUXIQ (Pico Datasheet section 2; JEDEC `ef 40 15` read on the measured bare Pico, notes `DEVICE.md:28`) | 2 MB | XIP window 0x10000000 through the SSI, quad continuous read, SSI divider 2 (62.5 MHz) (`dev/flash.c:338-352`) | uncached sequential read bound 4 bits x 62.5 MHz = 31 MB/s before command overhead (I); 4 KB sector erase 45 ms typical, 400 ms maximum; 256 B page program 0.4 ms typical, 3 ms maximum (DS AC table) | non-volatile; 100,000 program-erase cycles per sector, 20-year retention (DS feature list) | kernel 128 KB, Dhara root `fl0` 1536 KB, raw swap `fl1` 384 KB |
| 7 | Flash identity and OTP areas | W25Q16JV: 64-bit unique ID (4Bh), SFDP table (5Ah), three 256 B security registers with OTP lock bits (48h, 42h, 44h) (DS sections 8.2.x) | 8 B ID; 768 B security registers | SSI command frames with XIP down (`dev/flash.c:432`) | one command frame | security registers non-volatile and lockable forever | unique ID drives the USB serial number (`flash_identity()`, `dev/flash.c:472`); SFDP and the security registers are unused |
| 8 | Watchdog scratch | RP2040 WATCHDOG_SCRATCH0-7 (DS 4.7) | 8 x 32 bit | APB registers | register access | survive a watchdog or software reset, lost at power-on | scratch0 swap cursor (`rp2040/machdep.c:259-307`), scratch1-3 masked-site record (`machdep.c:631-640`), scratch4-7 boot ROM reboot state |
| 9 | PIO instruction memory and FIFOs | RP2040 PIO0, PIO1 (DS 3.4, 3.7) | 2 x 32 instruction slots, 8 FIFO words per state machine | APB, INSTR_MEM write-only | one write per word | volatile | PIO0 holds the 19-word PSRAM program on PSRAM kernels (`dev/psram_pio.h`); PIO1 unused (`docs/research/picocalc-psram-capture.md`) |
| 10 | CYW43439 memory (Pico W only) | Infineon CYW43439 (notes `rp2040/infineon_cyw43439_datasheet_002_30348_rev_d_lcsc_c5359496.pdf`) | internal SRAM and ROM of the radio | gSPI over GPIO23/24/25/29, firmware download first | not a store for the host | volatile | none: the port does not drive the radio |

### PicoCalc mainboard V2.0, Pico seated in J301/J302

| # | Device | Part, source | Nets and pins as wired | Bus as wired against best mode | Capacity | Throughput bound | Volatility | Tree use |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 11 | PSRAM | U301 ESP-PSRAM64H (S; DS V1.1) | RAM_TX GP2 to pin 5 SI/SIO0; RAM_RX GP3 to pin 2 SO/SIO1; RAM_IO2 GP4 to pin 3 SIO2; RAM_IO3 GP5 to pin 7 SIO3; RAM_CS GP20 to pin 1 CE#, pulled up through R302 10 kOhm to 3V3_OUT; RAM_SCK GP21 to pin 6 SCLK; VCC pin 8 from ALDO2 with C301 100 nF (S, 600 dpi render). GP2-GP5 and GP21 also reach header J703 pins 2-6 (S) | quad SPI-family protocol, all four SIO lines wired, so the part's best mode is reachable; no hardware SPI assignment fits (GP21 is SPI0 CSn, GP2 SPI0 SCK, GP3 SPI0 TX: datasheet table 1.4.3), so PIO only; not on the QSPI pads, so no XIP | 8 MB, 1 KB pages | quad at 31.25 MHz: 15.6 MB/s on the wire, 12 MB/s per 64 B frame at 5.2 us (M); 62.5 MHz (`PSRAM_CLKDIV` 1) doubles the wire rate; the part allows 133 MHz, 84 MHz for a burst across a page (DS) | volatile; DRAM cells refresh only while CE# is high (DS 5.5), and CE# low longer than tCEM 8 us blocks refresh | swap device on PICOCALC_PSRAM when it probes (`dev/psram.c:100`) |
| 12 | microSD socket | J303 TF-03A (S) | CD/DAT3 SPI0_CS GP17; CMD SPI0_TX GP19; CLK SPI0_SCK GP18; DAT0 SPI0_RX GP16; DAT1 and DAT2 not connected; VDD from ALDO1; DET to SD_DET GP22, pulled up through R301 10 kOhm to 3V3_OUT; WP not connected (S) | SPI mode on hardware SPI0; 1-bit SD bus mode would be possible through PIO on the same pins (DAT3 is driven), 4-bit SD mode is not | the card's | SPI at 12.5 MHz: 1.56 MB/s on the wire; SPI's 25 MHz default-speed limit gives 3.1 MB/s (SD Physical Layer, held under its license) | non-volatile; flash translation layer endurance, unmeasured | `sd0` partitions, crash dump, swap on a 0xb8 partition with #615 |
| 13 | Panel frame memory | ST7365P on FPC J401 (S; DS V1.0) | SPI1: SCK GP10 to WR/SCL, TX GP11 to SDA, RX GP12 to SDO, CS GP13, DCX GP14, RESET GP15; IM2, IM1 and IM0 all tied to ALDO3, which selects 4-line SPI with SDA and SDO (DS IM table); TE not connected (S) | 4-line SPI, read-back wired | 320 x 480 x 18 bit = 345,600 B; lines 320-479, 115,200 B, outside the 320-line glass if the glass is 320 lines (I) | write cycle 66 ns, read cycle 150 ns (DS), about 1.2 MB/s write, 0.6 MB/s read (I) | volatile, lost with ALDO3 | console and `/dev/lcdgfx` write it; no read path (`dev/st7365p.c` issues no RAMRD 2Eh) |
| 14 | Keyboard controller flash | U201, printed CKS32F103Rx; vendor names STM32F103R8T6 (S; README) | not on any RP2040 net; USART1 system bootloader reached through SW701 and the CH340C | not reachable from the RP2040 | 64 KB (R8) or 128 KB (RB); larger for RC or RE (DS5319) | n/a | non-volatile | none; `tools/picocalc/kbd_bootloader_id.py` reads the bootloader's ID from the host |
| 15 | Keyboard controller SRAM | U201 | internal | not reachable: no I2C register reads or writes it (SRC, firmware below) | 20 KB for the 8 and B suffixes (DS5319) | n/a | volatile | none |
| 16 | Keyboard controller backup domain, system ROM, unique ID | U201: ten 16-bit backup registers and RTC on VBAT (DS5319 2.3.14); system-memory bootloader; 96-bit unique ID | internal | not reachable; the firmware uses neither the RTC nor the backup registers (SRC) | 20 B backup | n/a | backup registers survive while VBAT holds | none |
| 17 | Keyboard controller register file and key FIFO | firmware `reg.h`, `fifo.ino` (SRC) | I2C1 GP6/GP7, target 0x1F, about 96 kHz, no interrupt line | the only part of U201 the RP2040 sees | 15 register bytes; 31-entry key FIFO | about 10 KB/s on the wire; about 50 ms per request through the shared poller (`PICOCALC.md`) | volatile | `dev/picokbd.c`, `dev/picokbd_req.c` |
| 18 | PMU data buffer | AXP2101 registers 0x04-0x07 (DS 6.13.1) | on U201's second I2C bus (PB10/PB11), not on the RP2040 | not reachable | 4 B | n/a | held by the PMU | none; the firmware only prints them on a short power-key press |

The CH340 data sheet (WCH CH340DS1) gives a configuration EEPROM to the
CH340B alone, for the serial number and USB descriptors, reachable only from
its USB side. The schematic prints CH340C, and
`docs/research/hardware-reference-coverage.md` leaves CH340B unexcluded until
the top mark is read. The WAS7227 switches, the amplifiers and the backlight
converters were not read for memory, and their functions suggest none (I).

### Alternate modules in the socket

| Module | What changes | Source |
| --- | --- | --- |
| Pico W | Same RP2040 and W25Q16JV. The CYW43439 takes GPIO23, GPIO24, GPIO25 and GPIO29, none of which the PicoCalc uses for memory. Rows 1-9 and 11-18 stand. | Pico W Datasheet appendix B (notes) |
| Pico 2, Pico 2 W (RP2350) | 520 KB SRAM and 4 MB flash. The QMI's second chip select, `xip_ss_n_1`, exists only on GPIO0, GPIO8, GPIO19 and GPIO47, and the QMI clock and data lines are dedicated QSPI pads that the module routes only to its own flash. U301 on GP2-GP5, GP20 and GP21 therefore stays a PIO device on these modules too. The RP2350 is an Armv8-M (or RISC-V) part and needs a port of its own. | Pico SDK 2.3.1 `src/rp2350/hardware_regs/include/hardware/regs/io_bank0.h` (the only `xip_` function select is 0x09 `xip_ss_n_1`, on those four GPIOs); `src/boards/include/boards/pico2.h` (`PICO_FLASH_SIZE_BYTES` 4 MB) |
| Pimoroni Pico Plus 2 | 8 MB of on-module PSRAM on QMI chip select 1 (GPIO47), memory-mapped by the RP2350. U301 on the mainboard stays a PIO device. | SDK `pimoroni_pico_plus2_rp2350.h:34,99` |
| Any RP2040 module | The RP2040 SSI drives one chip select, so no RP2040 module can memory-map a second QSPI device. | DS 2.6.3, 4.10 |

## Pin cross-check

Method: each net on the 600 dpi render of the schematic around J301, J302,
U301, J303, J401 and J703 was compared with the constants in
`sys/arch/rp2040/dev/picocalc.h:30-56` and `dev/psram_pio.h:92-94`, and with
the uses in `dev/sd.c`, `dev/st7365p.c` and `dev/picokbd.c`.

| Function | Schematic | Tree | Agree |
| --- | --- | --- | --- |
| Panel SPI1 SCK, TX, RX, CS, DC, RST | GP10, GP11, GP12, GP13, GP14, GP15 | `PICOCALC_LCD_*` 10-15 | yes |
| Keyboard I2C1 SDA, SCL | GP6, GP7 | `PICOCALC_KBD_SDA` 6, `_SCL` 7 | yes |
| SD SPI0 RX, CS, SCK, TX, detect | GP16, GP17, GP18, GP19, GP22 | `PICOCALC_SD_*` 16-19, 22 | yes |
| PSRAM SIO0-SIO3, CS, SCK | GP2-GP5, GP20, GP21, with SIO0 on GP2 | `PSRAM_PIN_SIO0` 2, `PSRAM_PIN_CS` 20, `PSRAM_PIN_SCK` 21 | yes |
| Audio PWM left, right | GP26, GP27 | `PICOCALC_AUDIO_L` 26, `_R` 27 | yes |

The schematic's net names `RAM_TX` and `RAM_RX` are SPI-mode names for SIO0
and SIO1. The vendor's demonstration program drives the part in 1-bit SPI
mode through a PIO library, with `PSRAM_PIN_MOSI=2` and `PSRAM_PIN_MISO=3`
(notes `picocalc/code/picocalc_helloworld/CMakeLists.txt:29-32`). The port
uses quad mode on the same wires.

## The PSRAM in detail

### What the wiring permits

| Mode | Clock | Wire rate | 64 B frame | Source |
| --- | --- | --- | --- | --- |
| 1-bit SPI through PIO (vendor demo) | 31.25 MHz | 3.9 MB/s | about 18 us, which exceeds tCEM unless frames shrink to about 24 B | arithmetic (I) |
| Quad through PIO, shipped | 31.25 MHz (`PSRAM_CLKDIV` 2) | 15.6 MB/s | write 4.45 us, read 4.72 us, at most 5.2 us masked | `check-psram-pio` model (M) |
| Quad through PIO, `PSRAM_CLKDIV` 1 | 62.5 MHz | 31 MB/s | about 2.3 us; 128 B frames fit inside tCEM | arithmetic (I); needs the capture of `docs/research/picocalc-psram-capture.md` |
| Quad at the part's limit | 133 MHz | 66 MB/s | not reachable: the two-instruction pass of the PIO program clocks SCK at clk_sys / 2 at most | DS; `psram_pio.h` |

A single random word costs one frame of 2 command, 6 address, 6 wait and 2
pipeline clocks plus 8 data nibbles, about 0.8 us of bus time and 1.5 to 2 us
with setup (I). SRAM answers in 8 ns. That gap of about 200 times, and the
absence of any bus mapping, rule out kernel data structures, user data
accessed in place and execute-in-place. The MPU grants permissions and
translates no address (`doc/MPU.md`), so it cannot fault a PSRAM page in on
demand either.

### Suitability

| Use | Fit | Reason |
| --- | --- | --- |
| Swap images | best | whole segments move sequentially; 1.4 ms per shell image against 256 ms for the pool and 257 ms for `fl1`; no wear |
| /tmp RAM disk | good | #624 (`8ed99d4a`) gives minor 1 the 6 MB above a 2 MB swap minor; a file system block of 1 KB costs about 83 us |
| Second-level cache for SD blocks | plausible, unmeasured | a 1 KB SD read at 12.5 MHz is at least 0.66 ms on the wire; the PSRAM copy is about 83 us. The flash root is faster to read than the PSRAM (XIP at up to 31 MB/s), so the cache helps SD alone |
| Crash dump store | conditional | it survives a watchdog or software reset only if CE# stays high through the reset (finding above) and ALDO2 stays on; see slice M14 |
| Process memory | copy-in only | that is swap |
| Kernel data, execute-in-place | no | no bus mapping, about 200 times SRAM latency |

### Supply and chip select

ALDO2's voltage and its enable are EFUSE defaults the PicoCalc's AXP2101 was
customized with. Register 0x93 holds the voltage and register 0x90 bit 1 the
enable (AXP2101 data sheet V1.4, 6.13.2.75-78). The keyboard firmware sets
neither (`picocalc_keyboard.ino` calls no ALDO function), and no document
states the value. The ESP-PSRAM64H accepts 2.7-3.6 V with input high
Vcc + 0.2 V at most (DS Table 10-4). A rail below 3.1 V would put the
RP2040's 3.3 V outputs outside that limit (I). A voltmeter on C301 decides
it.

The kernel cannot power the PSRAM down: the rail belongs to the controller's
PMU, and the ESP sheet defines no sleep command (a text search for "sleep" in
the V1.1 sheet returns nothing). Standby is 200 uA at most with CE# high and
CLK low (DS). While the controller holds the Pico off (PICO_EN low) and ALDO2
stays on, 3V3_OUT falls and R302 drags CE# low. That selects the part with no
clock, which is harmless for data the kernel does not keep across power, but
it draws more than standby current (I, unmeasured).

The pad state during an RP2040 reset is the open question for any data the
kernel wants to keep across a reset. `psram_hw_init()` takes CS from the
state machine before it routes the pin (`dev/psram_xfer.c:101-115`) and
leaves the CS pad without a pull. Between reset and that call, and during the
reset, the pad default (PDE set, PADS_BANK0 reset value 0x56, DS 2.19.6)
fights R302:

| RP2040 pull-down (DS 5.5.3: 50-80 kOhm) | CE# voltage at 3.3 V | VIH(min) at Vcc 3.3 V | Deselected? |
| --- | --- | --- | --- |
| 50 kOhm | 2.75 V | 2.9 V | not guaranteed |
| 80 kOhm | 2.93 V | 2.9 V | marginal |

The probe resets the part (66h then 99h, in quad and in serial format), so swap
and /tmp are unaffected (SRC). A crash dump kept across a reset is affected
(slice M14). The kernel arms the watchdog to reset every power-on state
machine block but the oscillators (`PSM_WDSEL_ALL_BUT_OSC`,
`rp2040/machdep.c:661`, `rp2040/core1.c:186`). Because a selected block resets
every block after it (DS 2.13.4), the pads return to their defaults.
Hypothesis: a narrower PSM selection, with the RESETS WDSEL register
(DS 2.14) leaving PADS_BANK0 and IO_BANK0 out, keeps CE# at R302's 3.3 V
through a watchdog reset. That changes the reset path the watchdog recovery
relies on, which is M14's own risk. A pattern written before a watchdog
reboot and read back after it decides the hypothesis.

### Evidence held

`bmake MACHINE=rp2040 check-psram-pio` at 4b3eaf6c exits 0, reports
"psram: 1226 checks, 0 failed" and kills 17 mutants (run for this note on the
Linux build host).
`docs/research/picocalc-psram.md` still prints 766 checks and both eight and
thirteen mutants; `PICOCALC.md` carries the current 1,226 and 17. The
divergence is recorded here and the fix belongs to slice M3.

## The keyboard controller firmware, read in full

Every file of `Code/picocalc_keyboard` in the ClockworkPi repository at
`master` (last commit e36ee963b4, 2026-09-04, "Fix CKS Potential Bug") is
byte-identical to the notes copy: the git blob SHA-1 of each of the 16 files
equals the upstream one. Read in full: `picocalc_keyboard.ino` (the whole I2C
register path), `reg.h`, `reg.ino`, `conf_app.h`, `keyboard.h`, `fifo.ino`,
`battery.ino`, `backlight.ino`, `port.ino` and `pins.h`. `keyboard.ino` was
read at its modifier and hotkey path (lines 139-200) and searched for every
timer, backlight, battery and matrix call; `fifo.h`, `battery.h`,
`backlight.h` and `port.h` were searched only.
`conf_app.h` says `BIOSVERSION 0x16`. Upstream `Bin/PicoCalc_BIOS_v1.7.bin`
was added on 2026-09-04 (5ae1660fce, "update PicoCalc_BIOS_v1.7,check_hp_det
add pinMode to fix CKS potential bug"), so the binary named 1.7 was built from
source that still reports 0x16, or from an unpublished bump. The `kbd0:` boot
line prints the version a board answers.

### Registers

A write sets bit 7 of the first byte. `receiveEvent()` keeps at most two
bytes and prepares the reply that `requestEvent()` sends
(`picocalc_keyboard.ino:109-223`).

| Id | Name in `reg.h` | Served by `receiveEvent` | Read reply | Write effect | Kernel use |
| --- | --- | --- | --- | --- | --- |
| 0x01 | VER | yes | 0, BIOSVERSION | none | probe line (`picokbd.c:114`, `:539`) |
| 0x02 | CFG | no: default case | 0, 0 | none; the value `reg_init()` stores (overflow interrupt, key interrupt, use and report modifiers) is unreachable | none |
| 0x03 | INT | no | 0, 0 | none | none |
| 0x04 | KEY | yes | FIFO count with bit 5 caps lock and bit 6 num lock, then 0 | none | none |
| 0x05 | BKL | yes | 0x05, LCD backlight | stores, then rounds to steps of 16 between 16 and 240 and drives PA8 PWM at 10 kHz | sysctl `lcd_backlight` |
| 0x06 | DEB | no | 0, 0 | none | none |
| 0x07 | FRQ | no | 0, 0 | none | none |
| 0x08 | RST | yes | none: the controller waits 1 s (read) or the written count of seconds, then `NVIC_SystemReset()` | same | never touched (`PICOCALC.md`) |
| 0x09 | FIF | yes | key state, key code from the 31-entry FIFO | none | polled every 16 ms idle, 2 ms busy |
| 0x0A | BK2 | yes | 0x0A, keyboard backlight | stores, steps of 32, values under 32 or over 240 give 0, PC8 PWM | sysctl `kbd_backlight` |
| 0x0B | BAT | yes | 0x0B, percent with bit 7 charging; 0 for no cell | none | sysctls `battery`, `charging` |
| 0x0C | C64_MTX | yes | 0x0C then the nine-byte matrix (ten bytes) | none | none |
| 0x0D | C64_JS | yes | 0x0D, joystick bits | none | none |
| 0x0E | OFF | yes | 0x0E, 1 | stores the delay (at least 6); the main loop waits that many seconds, then shuts the PMU down | `poweroff` |

Timing state the firmware keeps: `millis()` (the Arduino core's 1 ms
SysTick), `receiveEventTick` and `requestEventTick`, the key hold time of
300 ms and repeat interval of 100 ms (`keyboard.h`, `keyboard.ino:245-266`),
and a 20 s battery refresh (`picocalc_keyboard.ino:310`). The controller
also changes two registers on its own: with Alt (or num lock) held,
releasing "," or "." steps BKL down or up by 16, space steps BK2, and B
blinks the battery level on its LED (`keyboard.ino:150-182`). A kernel that
caches a backlight value therefore reads it back from the register rather
than trusting its last write. After 10 s of
uptime, a gap of 2.5 s without a receive or a request restarts its I2C target
(`:636-640`). The kernel's 16 ms idle poll stays well inside that window.

### No millisecond counter is exposed

No register returns `millis()`, an RTC value or any other count. The 32.768
kHz crystal X202 sits on the controller, but the firmware starts no RTC
(SRC: no RTC or backup register call in any file). The battery register is the
only measurement the RP2040 can read. A millisecond register therefore needs
a firmware change on U201 (slice M17), flashed through the USART1 bootloader
path that SW701 selects.

## Clocks for benchmarks

What user programs have today (SRC):
- `gettimeofday(2)` returns `time` with `tv_usec = lbolt * usechz`
  (`sys/kern/kern_time.c:61-81`). With `HZ` 1000
  (`sys/arch/rp2040/include/machparam.h:44`) the resolution is 1 ms. SysTick
  on the 125 MHz core clock drives it, and the 1 MHz TIMER corrects it after
  masked windows (`rp2040/clock.c`, `clock_catchup()`). It is wall time, and
  `settimeofday` and `adjtime` step or slew it.
- The kernel keeps a monotonic clock, `monosec` and `monotick`
  (`sys/kern/kern_clock.c:19-20`), for interval timers only. No system call
  or sysctl returns it, and libc has no `clock_gettime`.
- `getrusage` and `times` count in 1 ms ticks.
- The kernel's own 1 MHz counter, TIMERAWL at 0x40054028 (DS 4.6), is read in
  `clock.c`, `machdep.c` and `picocalc_reg.h:105` (`pc_micros()`). User mode
  cannot read it: the MPU map grants unprivileged access to the boot ROM, the
  user window and the divider alone (`doc/MPU.md`, "The map"), and an
  unprivileged access outside a region faults.

| Option | Resolution, monotonic | Read cost | Kernel text | Kernel RAM | Risk |
| --- | --- | --- | --- | --- | --- |
| A. sysctl `machdep.uptime_us` returning TIMERAWH:TIMERAWL as 64 bits | 1 us, monotonic from reset | one `sysctl` system call (unmeasured; tens of microseconds, I) | +40 to +80 B (I: one case in `cpu_sysctl` and `sysctl_rdstruct`) | 0 | none beyond the read |
| B. An MPU region giving user mode read-only access to the TIMER block's 256 B | 1 us, monotonic | one load over the peripheral bus: at least the two cycles of an SRAM load, 16 ns, and more behind the bus bridge (I) | +16 to +32 B in `mpu.c` (I) and one of regions 4-7 | 0 | MPU change; `MPU.md` requires written and read-back values, a privilege test and a deliberate fault test; user reads of TIMELR latch TIMEHR, which the kernel does not use |
| C. `clock_gettime(CLOCK_MONOTONIC)` system call and libc stub | 1 us | one system call | +120 to +200 B (I: `sysent` entry, handler, syscall-name entry) | 0 | ABI addition |
| D. Firmware register on U201 returning `millis()` | 1 ms, from the 8 MHz crystal X201 | one I2C exchange through the poller: three callout phases (write, read, collect), about 50 ms per request at the 16 ms idle poll (`dev/picokbd.c:567`, `KBD_POLL_MS`), of which 0.4 ms is on the wire (I) | +40 B for a sysctl | 0 | firmware fork and flash of U201; jitter worse than the resolution |

Recommendation: option A first. It is the smallest change that gives every
program a monotonic microsecond clock with no protection change, and libc's
existing `sysctl()` reaches it. Option B follows if a benchmark needs reads
under a microsecond and the owner accepts an MPU region. Option D buys
nothing the RP2040's own crystal-derived TIMER does not give better.

## Driver audit

| Device | Driven today | Missing or partial | Gates |
| --- | --- | --- | --- |
| SRAM, SRAM4/5, DPSRAM | link map and assertions (`conf/kern.ldscript:130-239`) | about 1.2 KB of DPSRAM unclaimed on SwapRAM USB kernels (I, `77e33cf9:docs/research/picocalc-device-first-memory.md`) | `check-kernel-headroom`, `check-swapram-link-verifier`, `check-cache-footprint` |
| Boot ROM | flash entry points and USB reboot looked up by code (`dev/flash.c:250-295`) | no ROM version check beyond Boot ROM V3 measured | `tests/rp2040/romprobe`, a board program `check-board-build` builds (run opt-in) |
| XIP cache | enable and disable for measurement (`dev/flash.c:318-335`) | cache-as-SRAM unusable while the kernel runs from flash, by mechanism | none needed |
| W25Q16JV | identity (`dev/flash.c:472`), erase, program, read (`:544`, `:582`, `:607`), Dhara root, raw swap append (`dev/flash_swap.c:21`), loader-stamp write limit (`flash.c:512`) | SFDP, security registers and deep power-down (B9h) unused; erase-suspend unused | `check-flash-swap`, `check-flash-sparse`, `check-flash-checkpoint`, `check-flash-reset-cells`, `check-dhara-*`, `check-flash-device-bounds` |
| Watchdog scratch | swap cursor, masked-site record | none | none; `check-clock-catchup` covers the clock code beside it |
| PSRAM | probe, reset, Read ID with 48-bit EID, quad entry, pattern test, extent test, block device minor 0 (`dev/psram.c:63-129`, `dev/psram_xfer.c:88-433`) | never run on a board; no integrity check on swap images; no probe of the supply; `psram.c` has no host test (issue #508); /tmp minor only on #624; no DMA, frames poll the FIFO with interrupts masked; 31.25 MHz pending a capture | `check-psram-pio` (host, 1,226 checks, 17 mutants); no Renode or board gate |
| SD card | SPI mode at 400 kHz then 12.5 MHz, CMD0/8/59/41/58/16/9, CRC7 and CRC16 on, single-block CMD17/CMD24 (`dev/sdspi.c:224-395`), MBR partitions, card detect with debounce, crash dump (`dev/sd.c:520`) | no multi-block CMD18/CMD25 and CMD12; no CMD6 or 25 MHz; no CMD10 card identity; no erase or TRIM (CMD32/33/38); no DMA | `check-fat32-tail`, `check-dump-writer-link`, `tests/rp2040/sd_detach` (board, opt-in, no result) |
| Panel frame memory | write path: RAMWR (`dev/st7365p.c:187`), init (`:345`), vertical scroll | no RAMRD; no read-back test; hidden lines unmeasured | `check-board-build` builds `lcdgfxtest`; no host model |
| Keyboard controller | VER, FIF, BKL, BK2, BAT, OFF through the poller and mailbox (`dev/picokbd.c`, `dev/picokbd_req.c`) | KEY, C64_MTX and C64_JS unused; RST deliberately unused; no tick register exists | `check-picocalc-host` |
| Controller flash and SRAM, PMU buffer | none | unreachable by wiring and firmware | none |

## Storage tiers that follow from the hardware

Constraints already decided, quoted from the owner's record:
- Replace `fl1` when the PSRAM probes (`docs/research/picocalc-psram.md`).
- D23 (revised): "PICOCALC = full robust kernel: PSRAM, every granular DEV_*
  driver ... PICOCALC_PSRAM retires into PICOCALC".
- D42: "keep 16KB for those devices without PSRAM but with PSRAM PRESENT:
  Swap is fast anyway".
- D44: "NMOUNT=3: full PICOCALC + PICOCALC_LEAN_DIAG; plain LEAN stays at
  its current mount count".
- D9: "Fall back to the compressed RAM pool and a small compressed flash tier
  (about 32 KB); research and adversarial review before building".
- D98: "Init's image stays in the pool when SD swap is configured".

Two decisions wait on this note:
- D95, PSRAM against pool ordering: "deferred" until the hardware question
  is answered.
- D96, pool scope on boards without PSRAM: "WAIT FOR THIS DESIGN UNTIL THE
  HARDWARE ELUCIDATIONS".

D97 asks for the compression early stop: "eluciaate solutions along with
the architecture being developed".

| Board state | Swap order | Pool role | Recommendation |
| --- | --- | --- | --- |
| A. PicoCalc, PSRAM up | PSRAM | exec spools and packed-text decode; images bypass it | bypass (slice M2), as `77e33cf9:docs/research/picocalc-device-first-memory.md` recommends |
| B. PicoCalc, PSRAM down, card with 0xb8 | pool, then SD (#615) | images, init kept (D98) | early-stop slices apply |
| C. PicoCalc, PSRAM down, no card | pool, then `fl1` | images, for wear | early-stop slices apply |
| D. Bare Pico | pool, then `fl1` | images, for wear | early-stop slices apply |
| E. State B after a card pull | pool, then a compressed `fl1` tier (D9) | images | unbuilt |

### Compression early stop (D97)

The cost of a pool attempt comes from `swapram_reserve()`. It runs a counting
encode of the data and stack segments to their end before it asks the pool for
a run (`rp2040/swapram.c:478-494`). An admitted image is then encoded again
into the run (`swapram_put()`, `:530-550`), and the u area is encoded once,
after `ru_nswap` changes it (`sys/kern/vm_swap.c:700-717`). The cycle model
(`77e33cf9:docs/research/picocalc-device-first/run-output.tsv`, GCC 16.2.0,
production flags) gives these encode costs per pass at 125 MHz:

| Segment | Bytes | Cycles per byte | One pass |
| --- | ---: | ---: | ---: |
| sh data+bss | 9,608 | 1,388 | 106.7 ms |
| stack, 4 KB synthetic | 4,096 | 235 | 7.7 ms |
| u area, 3 KB | 3,072 | 1,105 | 27.2 ms |
| 4 KB random | 4,096 | 11,765 | 385.5 ms |

Random input costs the most because the unindexed encoder searches the whole
512-byte window for every byte without finding a match.

| Option | Mechanism | Saving (model arithmetic) | Text | RAM | Exactness |
| --- | --- | --- | ---: | ---: | --- |
| E0. Admission pre-check | refuse before any encode when `swapram_pool_largest()` (`swapram_pool.c:160`) is below `SR_WORST(USIZE)` = 3,460 B plus a minimal segment | the whole refusal (sh 114 ms, 4 KB random 386 ms) when the pool is nearly full | +8 to +16 B (I) | 0 | exact: such an image can never fit |
| E1. Capped count | pass the largest run, less the u area's worst case, as `cap` to the counting `sr_encode()`, which today ignores `cap` when `dst` is NULL (`swapram.c:279-290`) | proportional to the remaining segment budget: random 4 KB against a 2 KB budget (a free run of 5,508 B less the 3,460 B u-area reservation) stops near 1.8 KB, 170 ms instead of 386 ms | +20 to +40 B (I) | 0 | exact |
| E2. Incompressibility guard | after the first 512 input bytes of a segment, refuse when output exceeds 15/16 of input | random 4 KB: 512 B x 11,765 cycles = 48 ms instead of 386 ms; sh data compresses about 6:1 and never trips it | +30 to +50 B (I) | 0 | heuristic: may refuse an image whose tail compresses; the image then goes to the device, slower but correct |
| E3. Single pass into the run | take the largest run at reserve, encode data and stack into it with `cap` set, trim the run, leave only the u area for `swapram_put()` | admitted sh: 141.6 ms instead of 255.9 ms (-45%) | +40 to +100 B (I), minus the count-mismatch checks | 0 | exact only if the bytes at reserve equal those at put. Today `swapram_put()` panics when they differ, but E3 would store reserve-time bytes; whether every caller keeps them equal (the source and owner split of `swap_image_reserve()`, cancellation through `swap_image_cancel()`) is unverified |
| E4. Refusal memo per process slot | skip the pool for the next N swapouts of a slot whose last attempt failed on ratio | repeated refusals of the same incompressible process | +20 B (I) | +NPROC bytes | heuristic |
| E5. Indexed encoder | heatshrink's index cuts match search | unmeasured, several times faster (I) | small | +2 KB for window 9, which the DPSRAM's 1.2 KB spare cannot hold | exact |
| E6. Bypass on PSRAM (state A) | no image attempt while `swapdev` is the PSRAM | all of it: 256 ms becomes about 1.4 ms | +12 to +24 B (I) | 0 | exact |

Recommendation: E6 for state A (slice M2), and E0 with E2 for states B to D
(slice M1), are the smallest exact-or-safe set. E1 adds exactness to E2 for
fragmented pools at small cost. E3 halves admitted-image cost and should
follow a board measurement of the pool time (falsifier 1 of the device-first
note). E5 waits on RAM that no configuration has.

## Driver plan

Each slice is one pull request. Text and RAM costs are estimates from
comparable code unless marked measured; every measured size names GCC 14.2.1
at 4b3eaf6c. "Board" names the measurement that finally decides the slice,
under the hardware opt-in.

| Slice | Work | Files | Text, RAM | Evidence without a board | Board measurement | Depends on |
| --- | --- | --- | --- | --- | --- | --- |
| M1 (start here) | Early stop E0 and E2, and optionally E1, in `swapram_reserve()` and `sr_encode()` | `rp2040/swapram.c`, `tests/rp2040/swap_tier_model`, the cycle model | +40 to +90 B, 0 | host: `check-swap-tier-model` loads the unmodified `swapram.c` through cffi, with a known-bad mutant that ignores the cap; cycle model rows for refusals | pool refusal time for a random-data process with `pc_micros()` | none |
| M2 | Pool bypass for swap images while `swapdev` is the PSRAM (E6) | `rp2040/swapram.c` | +12 to +24 B, 0 | host model with a PSRAM-major `swapdev` | falsifier 1 of the device-first note: pool against PSRAM time for one sh image | #637 for LEAN |
| M3 | Host test of `psram.c` (issue #508) and the stale counts in `picocalc-psram.md` | `tests/psram`, `docs/research/picocalc-psram.md` | 0 | host: probe lines for each error code, `swapdev` selection, minor bounds | none | none |
| M4 | Monotonic clock, option A | `rp2040/sysctl.c`, `include/cpu.h`, `lib/libc` contract test, `sysctl(8)` name | +40 to +80 B, 0 | host: `check-libc-sysctl`; Renode: two reads 1 s of virtual time apart | drift against `gettimeofday` over 60 s | none |
| M5 | Per-image CRC-32 on PSRAM swap writes, checked on swapin | `dev/psram.c` or `vm_swap.c`, the existing `crc32_update()` | +80 to +150 B, +4 B per process slot (I) | host: model with an injected bit flip | none beyond the PSRAM bring-up | M3 |
| M6 | PSRAM board bring-up record: probe line, EID, ALDO2 voltage at C301, swap of a known image | `sys/arch/rp2040/doc/PICOCALC.md` evidence rows | 0 | none | the probe line, a voltmeter reading, one swap round trip | board time |
| M7 | PIO1 self-capture of the PSRAM bus (DIAG kernels only) | new capture routine, sysctl readout, host decoder | +300 to +500 B and a 4 KB buffer, DIAG only (I) | host decoder over `pio_sim.py` frames | stage-gating placement and pad delay (`picocalc-psram-capture.md`) | #637 DIAG kernels |
| M8 | `PSRAM_CLKDIV` 1 (62.5 MHz) and 128-byte frames | `dev/psram_pio.h`, `dev/psram_xfer.h` | 0 B, +128 B bss (`psram_tx` and `psram_rx` each gain 16 words, `psram_xfer.c:84-85`) | `check-psram-pio` at the new divider | M7's margins | M7 |
| M9 | SD multi-block read and write (CMD18, CMD25, CMD12) | `dev/sdspi.c`, `dev/sd.c` | +200 to +300 B (I) | host: an SD SPI model (`docs/research/renode-sd-card-model-probe.md`) | sequential read and write rate on one card | #615 benefits |
| M10 | SD clock to 20.83 MHz after identification: CPSDVSR 6, SCR 0, because the 125 MHz peripheral clock divides by an even CPSDVSR times (1 + SCR) and has no 25 MHz setting (a divisor of 5 fails the static assertion at `dev/sd.c:117-118`) | `dev/sd.c:110-118` | 0 | build | CRC16 error count and rate over 64 MB | M9 |
| M11 | PSRAM /tmp minor | as #624 (`8ed99d4a`) | measured on that branch | as that branch | as that branch | #624 |
| M12 | SD block cache in PSRAM | `sys/kern/ufs_bio.c` hook, `dev/psram.c` | +400 to +800 B (I) | host model of hit rate over a recorded block trace | `STORAGE_STATS` SD read counts for a compile, with and without | M11, measurement 4 of the device-first note |
| M13 | GP20 pull-up enabled and pull-down cleared in early board setup on PicoCalc kernels | `rp2040/board.c` | +8 to +16 B (I) | build | CE# voltage before the probe | none |
| M14 | PSRAM dump target kept across a watchdog reset (WDSEL without pads) | `rp2040/dump.c`, `dev/psram.c` | +300 to +500 B (I) | none | pattern survives `watchdog_reboot` | M13 |
| M15 | Panel RAMRD probe, falsifier 5 of the device-first note | a board test program and a read ioctl in `dev/lcdgfx.c` | +150 to +250 B (I) | none | read-back of lines 320-479 and whether they light | none |
| M16 | Kernel tables moved to files (next section) | per candidate | per candidate | per candidate | per candidate | owner decision |
| M17 | U201 firmware tick register (option D) | out of tree: a fork of `picocalc_keyboard` | +40 B kernel | none | not recommended | owner decision |
| M18 | Process 1 kept in the pool while `swapdev` is removable (D98): admission gives it priority and evacuation skips it | `rp2040/swapram.c`, `sys/kern/vm_swap.c` | +16 to +40 B (I), 0 | host: `check-swap-tier-model` with a removable-device flag and a card-pull event | card pull with init swapped out | #615 (`450d8a65`), #626 (`6f3a5c5f`) |
| M19 | Compressed `fl1` tier of about 32 KB after a card pull (D9), after its research and adversarial review | per `docs/research/swap-tier-designs.md` (compressed writeback) | unestimated until the review | host model first | card pull under load | #626, M1, M18 |

The flash's SFDP, security registers and deep power-down have no slice: none
answers a need of the port today.

## Kernel text the PicoCalc kernel could move to files

Owner decision D102 asks for kernel space savings "especially for the
picocalc mode where a lot can be moved onto that 32MB partition", and D101
settles panic strings: "shorten, keep a code table in docs". Sizes come from
`sys/arch/rp2040/compile/PICOCALC/unix.map` and `nm -S` at 4b3eaf6c,
GCC 14.2.1. PICOCALC text is 121,163 B, PICO 100,819 B and PICOCALC_PSRAM
123,691 B (`arm-none-eabi-size`). D23 (revised) retires PICOCALC_PSRAM into a
full PICOCALC. Main carries no PICOCALC_LEAN. At #637's head 812ea3e1
PICOCALC_LEAN text is 117,589 B; its `nm` lacks `syscall_name_pool` and
`capacity_init` and keeps `errno_message_pool`, `nlist` and
`exec_elf_check`.

The tree executes nothing from SD or from a file, so moving code means loading
it into RAM, which costs RAM the kernel lacks. A data table read on demand
through the file system costs one transient buffer. Data tables are therefore
the realistic candidates, and the root on `fl0` serves them better than an
SD partition: it is present on every board and with the card out.

| Candidate | Measured bytes | Reader | Needed before root mount, with card absent, at panic | Move cost | Verdict |
| --- | ---: | --- | --- | --- | --- |
| errno message table (`errlist.o`: pool 874, rules 256, offsets 168, `kernel_errmsg` and expander 92) | 1,390 | libc `strerror()` through sysctl `CPU_ERRMSG` (`lib/libc/string/strerror.c:47`, `rp2040/sysctl.c:348`) | no; no; no | a message file on the root and a libc fallback to "Error N"; 0 kernel RAM | candidate |
| syscall name table (`syscalls.o`: pool 819, spans 326, `syscall_name` 52) | 1,197 | `SYSTRACE` printing only (`rp2040/syscall.c:252`) | no; no; no | print numbers and decode them in user space | absent from LEAN (measured at 812ea3e1), candidate for PICOCALC |
| kernel symbol table for `knlist` (`nlist` 288, `sysctl_nlist` 136, names 259 in the merged string section) | 683 | libc `knlist()` through `CPU_NLIST` (`lib/libc/gen/knlist.c`) | no; no; no | a symbol file written by the build and checked against the kernel's version string | candidate |
| `SYSTRACE` format strings | 220 | trace output | no; no; no | goes with the option | absent from LEAN |
| `CAPACITY_STATS` (`capacity_*`) | about 600, +100 bss | sysctl | no; no; no | goes with the option | absent from LEAN (measured) |
| ELF exec (`exec_elf_check` and strings) | 496 + about 30 | execve of an ELF file; the port ships a.out | no; no; no | removal from the PicoCalc execsw after checking that no shipped program is ELF | removal candidate, not a move |
| Boot and probe messages (string classification) | 1,430 | console at boot | yes; yes; no | message codes and a user-space decoder | not recommended: a cold board loses its diagnostics |
| Remaining strings, mostly panic and consistency messages (string classification: what the boot, fault, trace and symbol patterns leave, including a few short names such as "script" and "(null)") | about 2,970 | `panic()` and others | at panic | none on SD; D101 shortens them and keeps the code table in docs | follow D101 |
| Fault dump strings (string classification) | 434 | HardFault handler | at fault | none | keep |
| Console font `lcdfont` | 855 | console before root | yes | none | keep |
| Panel init `st7365p_init` and its table | 408 + 125 | boot | yes | none | keep |

The three file-table candidates total 3,270 B of PICOCALC text, 2,073 B on a
kernel without `SYSTRACE`. Each needs a file format, a writer in the build,
a reader in libc and an identity check against the running kernel. Those are
the cost lines an owner decision weighs.

## Residual owner decisions

1. **PICOCALC_LEAN with the PSRAM up (D95).**
   - Bypass the pool for images (M2): exact, +12 to +24 B, saves about 254 ms
     per sh swapout.
   - Keep the pool in front as built: no change, but every image pays the
     encode for nothing.
   - Remove SWAPRAM from LEAN: reverses D42, frees at least 16,384 B of RAM,
     and leaves PSRAM-absent boards unpooled.
2. **Early-stop set for pooled states (D96, D97).**
   - E0 + E2 (M1, recommended): small and safe; E2 can refuse an image whose
     tail compresses.
   - E0 + E1: exact, but saves little on a free pool.
   - E3 as well: halves admission cost, so measure first.
3. **Benchmark clock.**
   - Sysctl microsecond counter (option A, recommended): smallest, no
     protection change.
   - User-readable TIMER through an MPU region (option B): fastest read, but
     an MPU-map change with its own evidence burden.
   - `clock_gettime` system call (option C): standard interface, largest
     kernel cost.
4. **U201 firmware.**
   - Keep the vendor firmware (recommended): the RP2040 TIMER is a better
     clock.
   - Fork it to add a tick register: an independent clock, but ms jitter and a
     controller reflash.
5. **errno table.**
   - Move it to a root file with a libc fallback: saves 1,390 B of text;
     `strerror` needs the root.
   - Keep it: no change.
6. **Kernel symbol table.**
   - Build-written symbol file: saves 683 B, but needs an identity check.
   - Keep it.
7. **ELF exec on PicoCalc kernels.**
   - Remove it: about 526 B, after an inventory of shipped formats.
   - Keep it for future ELF programs.
8. **PSRAM swap integrity.**
   - CRC-32 per image (M5): detects silent corruption at the unmeasured
     sampling margin, for about +100 B and 4 B per slot.
   - Trust the bus until M7 measures the margin.
9. **Board time.**
   - Grant a PicoCalc session for M6 and M7: the PSRAM has never run on a
     board, and every latency here is a model figure.
   - Defer: the plan stays model-backed.

## Unverified sources and gaps

- ALDO1, ALDO2 and ALDO3 voltages: EFUSE customization, no document;
  a board reading decides.
- `PicoCalc_BIOS_v1.7.bin` against source that says 0x16: unread binary.
- U201 suffix, and so its flash and SRAM size: top mark or flash-size word
  (`docs/research/hardware-reference-coverage.md`).
- W25Q16JV data sheet revision G: its download source was not recorded
  (notes `docs/rp2040/README.md`). Its identity rests on its content: table
  9.1 gives `EFh 4015h`, the ID the measured board returns.
- SD Physical Layer Specification: held locally under its license, not
  republished; the SD figures cite it by section name only.
- Pico 2 and RP2350 facts rest on the Pico SDK 2.3.1 register headers and
  board files; the RP2350 datasheet itself was not read for this note.
- Panel glass height (320 or 480 lines) and RAMRD over 4-line SPI: unmeasured.
- The PSRAM driver: no board run; pad delays and register-stage clocking are
  simulator assumptions.

## Sources

Notes repository `discobsd-2040-notes` at 5167978, with the SHA-256 of each
PDF:

| File | Revision | Retrieved from | SHA-256 |
| --- | --- | --- | --- |
| `docs/picocalc/clockwork_Mainboard_V2.0_Schematic.pdf` | title block 2024-12-20; git blob 0a56f401 equals the copy at the root of github.com/clockworkpi/PicoCalc `master` | https://github.com/clockworkpi/PicoCalc (repository root) | b6fd79c6cbe9b0825525211d0f2e3e0fd269a2f9a6fc22580452de3012156ede |
| `docs/picocalc/esp_psram64_esp_psram64h_datasheet_en_v1.1_wayback.pdf` | V1.1, 2020-10, Wayback copy of Espressif's URL of 2025-10-09 | https://web.archive.org/web/20251009210421id_/https://www.espressif.com/sites/default/files/documentation/esp-psram64_esp-psram64h_datasheet_en.pdf | ab68624cdecfc6a768260747bec907aa20940f14129f269b58c3b2a52acd0605 |
| `docs/picocalc/ST7365P_SPEC_V1.0.pdf` | V1.0, 2023/02 | https://github.com/clockworkpi/PicoCalc (repository root) | 8971993df5a55c72180f700f9cd04dcb2a4305b86a87c11ba3de401d3eadfb79 |
| `docs/picocalc/AXP2101_Datasheet_V1.4.pdf` | V1.4 | https://github.com/clockworkpi/PicoCalc (repository root) | 9e51d16d84d905c32a001a123be8e6c58213ee21e42d5bdb1458b731dba10f6d |
| `docs/picocalc/parts/st_ds5319_stm32f103x8_xb_datasheet.pdf` | DS5319 Rev 20 | https://www.st.com/resource/en/datasheet/stm32f103c8.pdf | 1e54ce18e6c34c5b4a78986f26e245e143748c721009fc9af5953795c30686bc |
| `docs/rp2040/rp2040-datasheet.pdf` | build 3184e62-clean, 2025-02-20 | https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf | be56fbb75ba0ae9e26558a73c93ac3e75c2ad4e6878d3b6703de2a76d886ea8c |
| `docs/rp2040/pico-datasheet.pdf` | build d4e0f1799616 | https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf | 757ff485227493b9fcc0c2c96c4dea9de020e1d8b2b11e2aa4f9ee8b25aa89eb |
| `docs/rp2040/W25Q16JV_Datasheet.pdf` | revision G, 2018-03-22 | not recorded | 6d23cdf57fd63fc2c2de517707e293beaf70f3f47b424bf58510666294c04d82 |

ClockworkPi repository, github.com/clockworkpi/PicoCalc, read through the
GitHub API on 2026-11-16: `Code/picocalc_keyboard` at e36ee963b4, with blob
SHA-1s equal to the notes copy; `Bin/` listing; commit 5ae1660fce.
Pico SDK 2.3.1 as installed: `src/rp2350/hardware_regs/include/hardware/regs/io_bank0.h`,
`src/boards/include/boards/{pico,pico_w,pico2,pico2_w,pimoroni_pico_plus2_rp2350}.h`.

Commands behind the measured sizes, at 4b3eaf6c with GCC 14.2.1:
`bmake MACHINE=rp2040 kernel`; `arm-none-eabi-size` on each `unix.elf`;
`arm-none-eabi-nm -S --size-sort -t d` on `compile/PICOCALC/unix.elf`; a
per-object sum of the `.text` and `.rodata` input sections of
`compile/PICOCALC/unix.map`; `strings -n 5` over `objcopy -O binary -j .text`
for the string categories.
