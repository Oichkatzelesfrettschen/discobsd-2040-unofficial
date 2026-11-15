Source reading against datasheet text at `152803ce`, 2026-11-15; no board, Renode or qemu-user run backs any verdict here.

# Drivers against their datasheets: the Pico and the PicoCalc

The claim under test: every part of the Raspberry Pi Pico and of the
ClockworkPi PicoCalc that software can reach has a driver in this tree, and
each driver's constants agree with the part's datasheet. The claim holds for
the parts the kernels drive, with the exceptions listed under "Findings".
It fails as a statement about completeness: at the audited revision the
ADC, RTC, DMA, I2C0 and PIO1 blocks had no driver in any kernel (the ADC
has gained one since; its row says so), and no PicoCalc driver has run
on a PicoCalc
(`picocalc-pending-hardware.md`). `hardware-reference-coverage.md` holds
the document for each part; this file holds the driver for each part and
what the comparison found.

## Method

The denominator is the designator tables of
`hardware-reference-coverage.md` for the two boards and the peripheral
list of `RP2040.svd` (Pico SDK 2.3.1, `src/rp2040/hardware_regs`) for the
chip. Three read-only passes, one per group (the bare Pico, the PicoCalc
panel, keyboard and audio, the PicoCalc SD card and PSRAM), listed each
hardware-facing constant and sequence of the driver files and looked up the
governing line in the `.txt` extraction beside each PDF in the sibling
`discobsd-2040-notes` repository (`docs/SHA256SUMS` files there pin the
documents). Each finding below was then reread at the driver line and the
datasheet line named; one reported finding did not survive that reread
and is recorded under "Refuted". A row marked "match" means the values a
pass listed agree with the quoted document; it says nothing about the
board, and the passes' match lists were not reread one by one.

Not audited: `core1.c`, `mpu.c`, `fault.c`, `sysctl.c`, `board.c` and the
locore files, which `CORE1.md` and `MPU.md` cover with their own evidence;
`lcdcons.c`, `lcdterm.c`, `lcdgfx.c` and `picokbd_evq.c` beyond their calls
into the register-facing files and the scroll bounds of `lcdgfx.c`; the CDC and PSTN class documents, the boot ROM source, and the
schematic at resistor level, whose text extraction does not carry net-level
detail. The PSRAM Read ID value 0x0D and the EID density bits sit in figures
of the PSRAM datasheets and are absent from their text, so those two
constants are unchecked against a document here. The SD figures come from
the Physical Layer Simplified Specification 9.10 under the notes
repository's ignored `docs/local/specs`.

## Coverage: RP2040 blocks

| Block (SVD) | Driver | Kernels | State |
| --- | --- | --- | --- |
| XOSC, PLL_SYS, PLL_USB, CLOCKS, RESETS | `rp2040/machdep.c` (`SystemClock_Config`) | all | match |
| WATCHDOG | `machdep.c`: 1 us tick, scratch words, reset reason; the timer itself stays disabled | all | match; no watchdog reset is armed |
| TIMER | `machdep.c`: `TIMERAWL` as a microsecond counter; no alarm | all | match |
| PPB (SysTick, NVIC, MPU, SCB) | `systick.c`, `mpu.c`, `fault.c`, `locore0.S` | all | SysTick match; the rest not audited here, see `MPU.md` |
| SIO | GPIO25 LED, core 1 FIFO and launch (`core1.c`), the divider the boot ROM uses | all | LED match; `core1.c` not audited here, see `CORE1.md` |
| IO_BANK0, PADS_BANK0 | per-driver pin setup; `dev/gpio.c` for the pins a board leaves free (`GPIO.md`) | all | GPIO device added after this audit; register path unexecuted |
| UART0 | `dev/uart.c`, PL011 | all | match; 115,207 baud from IBRD 67, FBRD 52 |
| UART1 | pin setup exists in `uart.c`; no kernel configures `uart1` | none | no device |
| USB, USB_DPRAM | `dev/usb.c`, CDC-ACM | PICO, PICOCALC, PICOCALC_PSRAM | match, with the two risks below |
| SSI, XIP_CTRL, IO_QSPI, PADS_QSPI | `boot2/boot2_w25q080.S`, `dev/flash.c` through the boot ROM routines | all | match |
| SPI0 | `dev/sdspi.c`, `dev/sd.c` | PICOCALC, PICOCALC_PSRAM | match |
| SPI1 | `dev/st7365p.c` | PICOCALC, PICOCALC_PSRAM | bus match; panel register values below |
| I2C1 | `dev/picokbd.c` | PICOCALC, PICOCALC_PSRAM | match |
| PWM | `dev/picoaudio.c`, slice 5; `dev/picopcm.c`, slices 5 and 6 | PICOCALC, PICOCALC_PSRAM | match; the sample device was added after this audit |
| PIO0 | `dev/psram_xfer.c`, `psram_pio.h` | PICOCALC_PSRAM | match |
| SYSINFO, SYSCFG, BUSCTRL, PSM, ROSC | `machdep.c`, `sysctl.c`, `core1.c` | all | not audited here |
| ADC (four pins, temperature sensor, VSYS/3 on GPIO29) | `dev/adc.c`: one-shot conversions behind `machdep.adc_temp_mc` and `machdep.adc_vsys_mv`; `clk_adc` and the block start at the first read | all | added after this audit; conversions host-checked (`check-adc-conversion`), register path unexecuted |
| RTC | none; `clk_rtc` stays off | none | no driver |
| I2C0, PIO1, DMA | none | none | no driver |
| VREG_AND_CHIP_RESET, TBMAN | none; reset values kept | none | no software surface needed |

## Coverage: board parts with a software surface

| Part | Surface | Driver | State |
| --- | --- | --- | --- |
| Pico U3 W25Q16JV | erase, program, XIP | `flash.c`, boot2 | match; no kernel reads the JEDEC ID |
| Pico U2 RT6150B, PS on GPIO23 | PWM or PFM mode | none | pin left at reset; the Pico datasheet reserves it |
| Pico VBUS sense, GPIO24 | cable presence | none; `usb.c` forces `VBUS_DETECT` through `USB_PWR` | the datasheet describes that override for boards whose VBUS reaches no GPIO; the Pico's reaches GPIO24, which stays unused |
| Pico D2 LED, GPIO25 | activity | `led_control()` in `machdep.c` | match |
| Pico SW1 BOOTSEL | read through `IO_QSPI` | `machdep.c` | match |
| PicoCalc panel, ST7365P | SPI1, GP10-GP15 | `st7365p.c`, `lcdterm.c`, `lcdcons.c`, `lcdgfx.c` | timing match; register values below |
| PicoCalc U201 keyboard controller | I2C1 target 0x1F | `picokbd.c`, `picokbd_req.c`, `picokbd_map.c`, `picokbd_evq.c` | match for registers VER, BKL, FIFO, BK2, BAT, OFF |
| PicoCalc U101 AXP2101, U401 and U601 backlight boosts | reached only through U201 | `sysctl machdep.picocalc` | battery, backlights, power-off |
| PicoCalc U502, U503 AW8010 and PA_EN, J501 detect | U201 drives PA_EN from HP_DET | none on the Pico, by the board's design | no surface |
| PicoCalc U501 and the PWM pair GP26, GP27 | PWM slice 5 | `picoaudio.c` | match; no sample device |
| PicoCalc J303 microSD, detect on GP22 | SPI0 | `sd.c`, `sdspi.c`, `fat32tail.c`, `dump.c` | match |
| PicoCalc U301 ESP-PSRAM64H | PIO0 on GP2-GP5, GP20, GP21 | `psram.c`, `psram_xfer.c` | match at 31.25 MHz |
| PicoCalc U702 CH340C through U703 | UART0 | `uart.c` | match |

## Findings

Each finding names what decides it. "Board" findings change no code here;
`picocalc-pending-hardware.md` carries those that concern the PicoCalc.

### Panel register values written from another controller's table

`st7365p.c` `init_seq` carries the vendor PicoMite ILI9488P values.
Against ST7365P_SPEC_V1.0 section 9.3:

| Bytes sent | Datasheet | Difference |
| --- | --- | --- |
| `E0h` and `E1h` with 15 parameters each | PGC and NGC take 14 (9.3.19, 9.3.20, pages 240-241) | one byte past the table, and the field layout is the ILI9488's; the eighth `E0h` byte 78h sets a bit the table fixes at 0 |
| `C1h 41h` | PWR2, `VRH[6:0]`, default 13h; the table defines 00h-27h and reserves 28h-3Fh (9.3.11, page 231) | 41h is past the last listed value; the table says nothing of 40h and above |
| `C5h 00h 12h 80h` | VCMPCTL takes one parameter, default 1Ch for 1.0 V (9.3.13, page 234) | three bytes; the first sets the lowest VCOM step |
| `C0h 17h 15h` | PWR1 first parameter has a zero low nibble (9.3.10, page 230) | low nibble 7 |
| `F7h A9h 51h 2Ch 82h` | no `F7h` in the command table (searched the text for "F7h": no match) | undefined command; the file header and `PICOCALC.md` name E9h as the only undefined vendor command |

The reset pulse (1 ms against 10 us), the 120 ms waits after reset and
SLPOUT, the 12.5 MHz clock against the 66 ns write cycle, COLMOD 66h,
MADCTL 48h and the 0 + 320 + 160 scroll definition all match. Page 245
gates Command Table 2 behind CSCON `F0h` C3h/96h, which the driver never
sends, so the listed writes may have no effect at all. The branch
`rp2040-picocalc-panel-register-test` builds the board experiment that
decides this (register sets with and without the unlock, and a readback
over SPI1 RX). Decided by: board. Until then the header comment's "E9h is
the only omission" is the one statement a source edit can correct.

### USB controller keeps its state across a warm reboot

`boot()` restarts through `AIRCR.SYSRESETREQ`, which resets the core
alone (RP2040 datasheet 2.4.2.9; `CORE1.md` relies on the same fact).
`usbinit()` releases `RESETS_USBCTRL` and never asserts it, and the boot
ROM's flash boot path resets only `IO_QSPI`, `PADS_QSPI` and `TIMER`
(`bootrom_main.c` `main()`, B2 source in the notes repository; it resets
`USBCTRL` only in `_usb_boot()`). So after a
warm reboot the controller still holds the previous device address and
endpoint state while `SystemClock_Config()` restarts `pll_usb` under it.
Reboots re-enumerate on the measured board (`STORAGE.md`, seven warm
resets), so the stale state is survivable in the exercised cases. Hypothesis: an
assert of `RESETS_USBCTRL` before the release removes one source of
state the driver did not write. `usb-outwedge.md` records a bulk-OUT wedge
that only a full chip reset clears and eliminates three source-level
causes; whether that wedge survives a warm reboot today, and stops
surviving it with the assert, is the measurement that connects the two. Decided by:
board, under its opt-in.

### Flash operations mask interrupts longer than a USB control stage

`flash.c` erases under `splhigh()`: 8,192 bytes for a Dhara block, two
sector erases of 45 ms typical and 400 ms maximum each (W25Q16JV revision
G, AC characteristics, tSE). USB 2.0 section 9.2.6.4 allows 50 ms for a
status stage of a standard request with no data stage. A control transfer in flight during an erase can time out. The boot ROM
routines poll the busy bit with no bound and the watchdog is disabled, so
a flash part that never clears busy stops the kernel. Decided by: board
(`machdep.usb_*` counters during a write load).

### Unbounded waits before the console exists

`SystemClock_Config()` loops on `XOSC_STATUS.STABLE` and on each PLL lock
with no limit, as the boot ROM's own clock setup does. A crystal that
fails to start leaves no output. The 1 ms
startup delay (count 47) matches datasheet 2.16.3.

### SD card: checks the specification permits skipping

`sdspi.c` leaves CRC off (CMD59 is never sent; the specification, section
7.2.2, says the host should enable it), ignores the 16-bit data CRC on
reads, and does not send CMD13 after a write. A flipped bit on the 12.5 MHz
bus reaches the filesystem unseen. `INIT_US` (1 s) and `BUSY_US` (500 ms)
equal the specification's "more than" figures; the write limit is twice
the 250 ms standard figure. Decided by: host for the logic
(`check-picocalc-host` drives `sdspi.c` through its ops table), board for
the error rate that would justify CRC code, whose size is unmeasured,
against PICOCALC_PSRAM's 10,640 bytes of flash headroom.

### PSRAM: margins that documents cannot close

At `PSRAM_CLKDIV` 2 the clock is 31.25 MHz. A 64-byte read holds CE# low
for 144 clocks, 4.6 us, inside the 8 us tCEM of the ESP-PSRAM64H (v1.1,
table 10-5) and outside the 3 us of an extended-grade APS6404L, which the
Read ID bytes cannot tell apart. GP20 keeps its reset pull-down until
`psram_hw_init()`, so CE# is low through boot unless the board pulls it
up; the reset pair at initialization and the empty swap make that
harmless to data. `psram_read_chunk()` rounds a length up to four bytes,
so a read ending within three bytes of 8 MB would clock past the last
address; `psram.c` reads whole blocks and never reaches that case.
Decided by: board (`picocalc-psram-capture.md`).

### Keyboard: bus speed and registers left unused

The I2C bus runs at 96 kHz where the vendor's reference driver uses
10 kHz, and a fast-phase read follows its register write by 2 ms where the
vendor waits 16 ms; `PICOCALC.md` already lists both as board checks.
Registers CFG, INT, KEY, DEB, FRQ and RST are never accessed. Decided by:
board.

### Parts trusted without identification

No kernel reads the flash JEDEC ID (`9Fh`) or unique ID (`4Bh`); the
SDK-built probe of `tools/pico-sdk/check-flash-id.sh` does, outside the
kernel. `flash.h` fixes 2 MB, and the USB serial-number string is the constant
"rp2040". Two boards on one host are told apart only by port.

## Refuted

A pass reported that held keys do not repeat on the console, because
`picokbd_translate()` types only on the pressed state. The controller
firmware reports each repeat of a printable key, Enter, Tab, Delete,
Backspace and the arrows as a pressed event (`keyboard.ino`, the
`KEY_STATE_HOLD` branch of the key callback) and sends the hold state only
for the remaining keys, Ctrl among them. The driver's handling and
`PICOCALC.md`'s "repeats held keys" agree with that source.

## What has no driver, and what each would cost

Flash headroom of the kernels built in the primary checkout on
2026-11-13, before this revision (`tools/check_kernel_headroom.py`): PICO 29,304, PICO_UART 38,576, PICOCALC 12,832,
PICOCALC_PSRAM 10,640 bytes of 130,816.

| Missing | What it would give | Constraint |
| --- | --- | --- |
| ADC | die temperature, VSYS on the bare Pico, three analog pins | `clk_adc` from `pll_usb`; erratum RP2040-E11, DNL error peaks in the ADC |
| GPIO device | user access to the header pins | which pins a kernel owns differs per board descriptor |
| RTC | none on these boards: no backup supply keeps it across power loss | `clk_rtc` |
| PCM audio | `/dev/audio` on the PicoCalc | DMA or an interrupt-fed ring on slice 5; a sample format decision |
| I2C0, SPI, PIO, DMA as user devices | header peripherals | no consumer in the tree |
| Pico W CYW43439 | Wi-Fi | no Pico W kernel exists; a network stack does not fit 128 KB |

The choice of which of these to build is the owner's and is open.
