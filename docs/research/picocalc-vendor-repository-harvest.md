# PicoCalc vendor repository harvest

Scope: a source reading of the clockworkpi/PicoCalc repository at revision
`fdcfe2c` (the donor), set against this port's PicoCalc drivers. Nothing was
run: no board, no emulator, no vendor binary. Evidence class is "source
describes the implementation": the vendor firmware text says what the vendor
built, not what the shipped controller does on a given unit. The keyboard
controller's shipped firmware is also not proven to equal this source (the
repository carries a v1.7 binary and v1.6 source; see Open).

The three first-pass donor readings were treated as leads. Every donor
`path:line` below was opened again in this pass, except where a line says
"first-pass reading" or "not re-opened".

## License position

The donor has no license file at its root or in its code directories. The
rule applied here: facts about hardware behavior (register numbers, pin
numbers, timings, byte layouts) are recorded and may inform this tree's own
code; no donor code, table or text is copied or translated into it. The
port's own sources were written from the hardware's behavior and the
datasheets and stay under the DiscoBSD ISC notice.

| Donor directory | License text found | Quoted line |
| --- | --- | --- |
| root, `README.md`, `Code/picocalc_keyboard`, `Code/picocalc_helloworld`, `Code/picocalc_kbd_tester`, `Code/picocalc_lvgl_graphics_demo`, `Code/MP3Player`, `Code/NES`, `Code/FUZIX`, `Code/uLisp`, `Code/scripts`, `wiki/` | none | (none; `fuzix.patch:592` holds only a NetBeans template comment, "To change this license header...") |
| `Code/pico_multi_booter` `boot.c`, `applink.py` | MIT | `boot.c:3` "MIT Licensed : Copyright 2022, "Hippy""; `applink.py:15` `LICENSE = "MIT Licensed"` |
| `Code/pico_multi_booter` `boot.c` credit block | BSD-3-Clause | `boot.c:58-60` "Credit : https://github.com/usedbytes/rp2040-serial-bootloader ... SPDX-License-Identifier: BSD-3-Clause" |
| `Code/pico_multi_booter` `sd_boot/`, linker scripts | none | no license line |
| `Code/picocalc_zero` (Linux keyboard driver patch) | GPL-2.0-only | `kernel.patch:114` "SPDX-License-Identifier: GPL-2.0-only"; `kernel.patch:1258` `MODULE_LICENSE("GPL")` |
| `Code/PicoMite` | MMBasic license | `README.md:29` "# MMBasic License" (not re-opened beyond the heading) |
| `Bin/PicoCalc SD/cc/` | LicenseRef-CCPL and MPL-2.0 | `cc/edit.lua:3`, `cc/internal/menu.lua` LicenseRef-CCPL; `cc/pretty.lua:3`, `cc/internal/syntax/parser.lua:3` MPL-2.0 |
| `Bin/PicoCalc SD/` other trees | none | vendor binaries; the README ends with a PicoMite acknowledgement block |

Gitlinks `rp2040-psram` (helloworld), `uthash` (tester) and
`lib/lvgl/lvgl` (LVGL demo) are empty directories: their sources are not
in the donor.

## The keyboard controller as the firmware serves it

The controller is an STM32F103R8T6 (wiki `Setting-Up-Arduino-Development-for-PicoCalc-keyboard.md:3`)
running the Arduino sketch in `Code/picocalc_keyboard`. All lines below are
in that directory. The host speaks I2C at address 0x1F (`conf_app.h:6`).

### Register map

`receiveEvent()` decodes the first byte: bit 7 (`WRITE_MASK`) marks a write,
the low seven bits name the register (`picocalc_keyboard.ino:128-129`); the
next `requestEvent()` returns `write_buffer` (:214-222). Every register
below answers two bytes unless noted.

| Reg | Name (`reg.h:9-22`) | Read | Write | Source |
| --- | --- | --- | --- | --- |
| 0x01 | VER | (0, 0x16) | ignored | `picocalc_keyboard.ino:136-139`, `conf_app.h:4` |
| 0x02 | CFG | (0, 0): declared, not served | ignored | `reg.h:11`; switch has no case, default :204-209 |
| 0x03 | INT | (0, 0): not served | ignored | same |
| 0x04 | KEY | (count \| 0x20 caps \| 0x40 num, 0); the second byte stays 0, the register number is not echoed | ignored | :170-175, `reg.h:41-43` |
| 0x05 | BKL | (0x05, LCD level) | sets level, then quantizes (below) | :145-152, `backlight.ino:20-32` |
| 0x06, 0x07 | DEB, FRQ | (0, 0): not served; `reg.ino:40-41` stores 10 and 5 that nothing reads | ignored | `reg.ino:40-41` |
| 0x08 | RST | delays 1 s, then `NVIC_SystemReset()`; no reply | delays value x 1 s, then reset | :196-203 |
| 0x09 | FIF | (state, key) of the oldest event, (0, 0) when empty | ignored | :140-144, `fifo.ino:47-58` |
| 0x0A | BK2 | (0x0A, keyboard-light level) | sets level, quantizes | :153-163, `backlight.ino:34-46` |
| 0x0B | BAT | (0x0B, percent in bits 0-6, bit 7 charging) | ignored | :164-169 |
| 0x0C | C64_MTX | 10 bytes: 0x0C then the nine `io_matrix` bytes | ignored | :176-180 |
| 0x0D | C64_JS | (0x0D, joystick bits) | ignored | :181-185 |
| 0x0E | OFF | (0x0E, 1), always | sets the countdown, floor 6 | :186-195, the constant at :194 |

`CFG` is initialized to 0xD2 (`CFG_OVERFLOW_INT | CFG_KEY_INT | CFG_USE_MODS |
CFG_REPORT_MODS`, `reg.ino:39` with `reg.h:24-31`); `CFG_OVERFLOW_ON` is clear,
so a full 31-entry FIFO (`conf_app.h:7`) drops the new event
(`picocalc_keyboard.ino:93-101`, `fifo.ino:22-45`). The register is not
host-writable, so the two bits that decide whether modifiers are reported
(0x40) and applied (0x80) stay set. Register 0x01 reads 0x16, the v1.6
firmware (`BIOSVERSION`).

Register 0x04 does not echo its number in the first reply byte, while the
port's mailbox counts a reply whose first byte differs from the request as a
fault (`sys/arch/rp2040/doc/PICOCALC.md`, "A register request"); an
implementation of 0x04 would need an exemption. Registers 0x01 and 0x09 also
reply without the echo and go through the poller's own exchange, not the
mailbox.

### Scan, key states and repeat

- `keyboard_process()` returns at once unless more than `KEY_POLL_TIME` =
  16 ms passed since the last scan (`keyboard.ino:289`, `keyboard.h:78`). The
  loop also sleeps 10 ms per pass (`picocalc_keyboard.ino:643`), so the scan
  period is about 20 to 26 ms (inference from those two figures; not
  measured). No debounce counter exists (the stored DEB value is unused).
- Each scan drives the eight columns low in turn and reads the seven rows;
  a pressed key reads low (`keyboard.ino:294-351`). Twelve side buttons
  (modifiers, arrows, brackets, 9 and 0) are read directly (:353-400).
- A key walks IDLE, PRESSED, HOLD, RELEASED, IDLE (`keyboard.h:4-10`,
  `keyboard.ino:207-280`). PRESSED turns to HOLD once 300 ms (`KEY_HOLD_TIME`,
  `keyboard.h:83`) pass; a held key repeats a HOLD event every 100 ms (:258-268).
  At HOLD the firmware sends PRESSED again for printable characters
  (32 to 127), Enter, Tab, Delete, Backspace and the four arrows, and HOLD for
  every other key (:195-200). Break, F-keys, Insert, Home, End and Page keys
  therefore do not repeat as PRESSED; the port types only on PRESSED
  (`picokbd_map.c`), which matches.
- Ten keys can be in flight (`KEY_LIST_SIZE`, `keyboard.h:80`).

### Modifiers, locks, shifted layer

- Alt, Shift (both), Sym and Ctrl are reported as 0xA1 to 0xA5
  (`keyboard.h:28-32`) when `CFG_REPORT_MODS` is set (`keyboard.ino:103-126`).
  Ctrl is reported but never applied: only the host applies it.
- Shift selects the second character of a key's `{chr, symb}` entry except
  for letters (:145-146). That table puts the keys the port types through
  the "Shift layer" in one place: Shift+Esc = Break (0xD0), Shift+Tab =
  Home, Shift+Del = End, Shift+Enter = Insert, Shift+Up/Down = Page Up/Down,
  Shift+F1..F5 = F6..F10 (`keyboard.ino:39-62`).
- The Caps Lock key (0xC1) toggles caps on PRESSED (:131-138) and is also
  sent to the host as key 0xC1; the port's map drops it, and the firmware
  already applies the lock to letters.
- Alt+Shift chords: Shift-R with Alt sets caps; Shift-L with Alt sets num;
  any Shift press without Alt clears both (:215-238). Num lock behaves as
  a sticky Alt (`alt = mods[ALT] | numlock`, :142), which is why digits come
  out as their Alt symbols while it is set.
- Alt+comma, Alt+period, Alt+space and Alt+B are consumed by the firmware:
  they step the LCD light down or up, cycle the keyboard light, or flash
  the battery LED, and their PRESSED and RELEASED events are suppressed
  (:150-183). The HOLD event of those keys is not suppressed, so a held
  Alt+comma would repeat a comma (source reading only, not observed).

### Power key and shutdown

- A short press of the AXP2101 power key posts `KEY_POWER` (0x91) with
  state PRESSED, once, to the FIFO (`picocalc_keyboard.ino:387-390`); no
  RELEASED follows. A long press drives PA13 and PA14 low, sets the
  charge-LED mode and calls `PMU.shutdown()` (:404-413).
- PA13 is "pico enable": low at setup start, high once keyboard
  initialization ends (:471-472, :546). PA14 follows the headphone-detect
  input on PC12 (`check_hp_det`, :603-616).
- OFF write: `pmu_online` becomes max(value, 6) (:186-193); `check_pmu_int()`
  then blocks the main loop with `delay(1000 * pmu_online)` and shuts the PMU
  down (:302-308). Key scanning and the 20 s battery refresh stop for the
  countdown; the I2C interrupt handlers keep running.
- `pmu_online` is 1 when the PMU answers at start and 0 when it does not
  (:487-495); at 0, `check_pmu_int()` returns first (:302), so with no PMU
  the power key, the battery refresh and every PMU shutdown path are inert.
- A level-1 battery drop warning also shuts down (:346-351); `low_bat()`
  shuts down at 0 percent after four consecutive low readings
  (`battery.ino:46-56`, `LOW_BAT_VAL` 20 in `conf_app.h:24`).

### Battery byte, backlight, watchdog

- BAT: percent, with bit 7 set when charging; 0 when the PMU reports no
  battery (`sync_bat`, :285-297). The refresh runs every 20 s from the main
  loop (:310-323). A battery-insert event sets bit 7 unconditionally
  (:371-378), so a charge indication right after insertion is not proof of
  charging (source reading).
- LCD backlight on PA8 and keyboard backlight on PC8, both PWM at 10 kHz
  (`backlight.ino:15-16, :43-44`). LCD level: add, divide by 16, floor 16,
  ceiling 240 (:20-32). Keyboard level: divide by 32, 0 below 32 or above
  240 (:34-46). The host write goes through the same functions
  (:145-163), so a write reads back the quantized level. Defaults:
  LCD 32, keyboard 0 (`reg.ino:42-43`).
- The firmware resets its own I2C slave when more than 2.5 s pass without a
  write or without a read, once uptime exceeds 10 s: either timer alone
  triggers it (`picocalc_keyboard.ino:633-641`, `ResetI2CBus` :618-631,
  timers set at :115 and :215).

### Port comparison, keyboard

| Topic | Donor | Port | Gap |
| --- | --- | --- | --- |
| Registers used | all above served | 0x01, 0x05, 0x09, 0x0A, 0x0B, 0x0E (`picokbd_req.h:37-40`, `picokbd.c:118`) | 0x04, 0x0C, 0x0D, 0x08 unused (0x08 resets the controller; never read) |
| OFF read | constant 1 (:194) | the sysctl returns the last countdown the kernel wrote (`picokbd.c:607-649`); the register read is not used | `PICOCALC.md` row corrected in the keyboard commit |
| Break 0xD0, power 0x91 | sent | Break ETX, power types nothing, both recorded for raw sessions | closed by the keyboard commit |
| Ctrl | 0xA5 reported, host applies | `picokbd_map.c` applies it; latch expires 600 ms (`picokbd.c:125`) | none |
| I2C speed | helloworld, tester 10 kHz (`i2ckbd.h:12`: "if dual i2c, then the speed of keyboard i2c should be 10khz"); FUZIX 400 kHz (`fuzix.patch:1736-1741`, first-pass reading) | 100 kHz (`picokbd.c:117`) | firmware's slave speed limit not found; no port failure observed |
| Poll | helloworld waits 16 ms between write and read (`i2ckbd.c:33`) | 16 ms idle, 2 ms for 8 exchanges after an event (`picokbd.c:121-124`) | none |
| Slave watchdog | resets after 2.5 s without a write or without a read | a poll writes and reads every 16 ms; the port resets the bus after 8 stalled polls (`picokbd.c:127`) | inside the firmware's window; the OFF countdown is the one long gap |

Helloworld's `i2ckbd.c:40-45` tests `(buff & 0xff) == 1` for PRESSED, which
is the firmware's state byte, so that test is sound; its Ctrl codes
0x7e02 and 0x7e03 match no key the firmware sends (Ctrl is key 0xA5), so its
`ctrlheld` never sets. This corrects the first-pass reading, which called the
PRESSED test wrong.

## Panel initialization and clocks

Donor: `Code/picocalc_helloworld/lcdspi/lcdspi.{c,h}` (the tester and LVGL
demo carry the same file with additions); port:
`sys/arch/rp2040/dev/st7365p.c`.

| Item | Donor | Port | Note |
| --- | --- | --- | --- |
| SPI | SPI1, pins GP10-GP15 (`lcdspi.h`, first-pass reading; the port's `picocalc.h:30-35` agrees) | SPI1, same | match |
| Write clock | `LCD_SPI_SPEED 25000000` (`lcdspi.h:7`); `spi_init` at `lcdspi.c:696`; `spi_set_baudrate` back to it at :126 | 12.5 MHz, prescale 2, SCR 4 of 125 MHz (`st7365p.c:23-26, :76`) | below |
| Read clock | 6 MHz (`lcdspi.c:119`) | 3.125 MHz (`st7365p.c:210`) | both exceed the 150 ns read cycle: 166.7 ns and 320 ns |
| SPI mode | no `spi_set_format` call in the donor program; the SDK default is in force and the SDK is not in the donor | mode 3, SPO and SPH set (`st7365p.c:90-92`) | donor mode unverified |
| Reset | high, then low 10 ms, then high 200 ms (`lcdspi.c:524-531`) | 1 ms wait, low 1 ms, then high 120 ms (`st7365p.c:459-463`) | datasheet TRW 10 us minimum, TRT 5 ms (120 ms from sleep): both fit |
| Init order | E0, E1, C0, C1, C5, MADCTL 0x48, COLMOD 0x66, B0, B1, INVON, B4, B6, B7, E9 00, F7, SLPOUT, 120 ms, DISPON, 120 ms, MADCTL (`lcdspi.c:541-627`) | the same table without E9; the scroll definition, SLPOUT and DISPON are issued elsewhere (`st7365p.c:136-153`, header :37-47) | gamma bytes, C0, C1, C5, B0-B7, F7 equal the donor's byte for byte |
| COLMOD | 0x66, three bytes a pixel (`lcdspi.c:590-591`, 3-byte writes at :182-201, first-pass reading) | 0x55 for the console, 0x66 selected by `lcdgfx` (`st7365p.c:144, :842, :899`) | the port's console is 120 bytes a glyph where RGB666 took 180 |
| Scroll | reads each line back (RAMRD 0x2E, `lcdspi.c:86, :91`) and redraws (`scroll_lcd_spi`, :358-375) | hardware vertical scroll, VSCRDEF 0x33 and VSCSAD 0x37 (`st7365p.c:101-103`) | port avoids the 6 MHz readback |
| Panel identity | ILI9488 define, 320x320 (`lcdspi.h:17-20`), ILI9341 opcode names | ST7365P (datasheet in the donor root) | the same register sequence drives both |

### The 25 MHz write clock against the datasheet

`ST7365P_SPEC_V1.0.pdf` (donor root, 287 pages, read with `pdftotext -layout`)
states the 4-line serial interface timing in section 7.4.3 on page 54
(the 3-line table is the same on page 53): "TSCYCW  Serial clock cycle
(Write)  66 ns" minimum, with TSHW and TSLW 15 ns minimum, at
VDDI = 1.8 V, VDDA = 2.8 V, 25 C. The serial read cycle TSCYCR is 150 ns
minimum on the same table.

A 66 ns minimum write cycle caps the clock at 1 / 66 ns = 15.15 MHz. 25 MHz
is a 40 ns cycle, which is outside the datasheet's minimum on the cycle
figure, although each half period (20 ns) meets the 15 ns pulse-width
minimum. Whether the vendor drivers actually clock the panel at 25 MHz
depends on the SDK divider arithmetic and the peripheral clock, neither of
which is in the donor; `sys/arch/rp2040/doc/PICOCALC.md` ("Display: ST7365P
on SPI1") already records 20.8 MHz as the rate those drivers get, also
outside the limit. The port runs 12.5 MHz (80 ns), inside the 66 ns minimum.
The port's clock is not changed by this note. That the vendor's panel works
at a rate above the datasheet limit is an observation about margin on their
units, not a specification.

A citation correction for the port: `st7365p.c:207`, `st7365p.c:876`,
`picocalcvar.h:57` and `PANELTEST.md:31` give the 150 ns read cycle as
"page 77". Page 77 of the datasheet is the 16-bit 8080 read data format;
TSCYCR is on pages 53 and 54. The figure was right and the page wrong;
those four citations now read page 54.

## Audio

Donor: `Code/picocalc_helloworld/pwm_sound` and `main.c`. Port:
`sys/arch/rp2040/dev/picopcm.{c,h}`, `picoaudio.c`.

| Item | Donor | Port |
| --- | --- | --- |
| Pins | GP26 left, GP27 right (`pwm_sound.h:10-11`); the schematic nets PWM_L on GP26 and PWM_R on GP27 | the same (`picocalc.h:59-60`) |
| Carrier | clkdiv 6.05, wrap 250 (`pwm_sound.c:24-25`), system clock 133 MHz (`main.c:234`): 133e6 / 6.05 / 251 = 87.6 kHz | 500 kHz, 250 counts a period on slice 5 (`picopcm.c`, `picopcm.h:34`, `PICOCALC.md`) |
| Sample pacing | the wrap interrupt advances the position every wrap and the sample index is `position >> 3` (`main.c:189-193`): one sample per eight wraps, 87.6 kHz / 8 = 10.95 kHz | a separate sample-clock slice 6 at 8000 samples per second (`picopcm.h:8`) |
| Level range | raw sample 0-255 as the compare value against a wrap of 250 (`main.c:190-191`); values 251-255 clip to full duty | `pcm_level` scales 0-255 onto 0-249 |
| Both pins | one slice (GP26 is 5A, GP27 is 5B), the same value on each | one slice, one channel stream |

Both carriers sit above the audible band; the board's RC network filters
them (see `PICOCALC.md`, "Audio"). The donor MP3 player disagrees with the donor's own
helloworld: `Code/MP3Player/config.h:20-21` puts the left channel on GP28 and
the right on GP27, against GP26 and GP27 in helloworld and the schematic.
GP28 reaches only the expansion header (`PICOCALC.md` pin table).

## PSRAM

Donor helloworld builds the `rp2040-psram` submodule (not present) with
`PSRAM_PIN_CS=20`, `PSRAM_PIN_SCK=21`, `PSRAM_PIN_MOSI=2`,
`PSRAM_PIN_MISO=3` (`CMakeLists.txt:26-32`) and starts it on PIO1 at clkdiv
1.0 (`main.c:256`). Port: `psram_pio.h:92-97` names SIO0 on GP2, CS GP20, SCK
GP21 on PIO0 with `PSRAM_CLKDIV 2` (:135), and `PICOCALC.md` lists SIO0-SIO3
on GP2-GP5 (quad mode). The donor's program, command set and speed are not
in the tree, so the two PIO programs cannot be compared; the pin choice
(GP2/GP3 as the first two data pins, GP20 and GP21 for select and clock)
agrees with the port.

## FUZIX, uLisp and the multi-booter

- FUZIX is a patch set, not a tree: `Code/FUZIX` holds `fuzix.patch` and
  `fuzix1.1.patch` against upstream commits f0d56ef and c1ff469 (README).
  It adds `CONFIG_PICOCALC` (`fuzix.patch:80`), three tty drivers (UART, up to
  four USB at :96, one LCD at :98), SD on SPI0 at SCK 18, MOSI 19, MISO 16, CS
  17 (:177-180, the port's pins agree), a 25 MHz LCD clock (:2697) and a
  320x320 window over a 480-line controller (:2710-2712). Its console is a
  pixel renderer with software scroll; the port's `lcdterm.c` cell ring and
  hardware scroll are richer. The patch defines no user-memory or swap
  figure, so no process-window comparison is possible.
- uLisp: a patch to technoblogy ulisp-arm at commit 97e6115 (`uLisp/README.md:39-43`);
  the base header is "uLisp ARM Release 4.4b" (`uLisp.patch:290`). The
  port's `usr.bin/ulisp` carries "uLisp PicoCalc Release 4.8f" (first-pass
  reading of its header). The donor's SD directory listing and `now` clock
  are not in the port's build; the donor has no sound or PWM function.
- Multi-booter: `SD_BOOT_FLASH_OFFSET` is 200 KiB (`sd_boot/config.h:39`) with
  applications linked at 0x10000000 + 200k (`memmap_default_rp2040.ld:3`);
  the menu reads `/firmware` and accepts only `.bin` files
  (`text_directory_ui.c:80`, `main.c:307`), programs by erase-then-write
  (`main.c:194-195`), skips an identical image (:171) and refuses a reset
  vector below the offset (:122). This port's map is a 128 KB kernel at
  0x10000100, a 1536 KB root at 0x10020000 and 384 KB of swap at 0x101A0000
  (`BOOT-MAP.md`); the 200 KiB application region begins inside the port's Dhara root. The vendor UF2 loader (pelrun uf2loader, `pico1-apps`)
  that the port boots through is a different program and is not in the
  donor's source.

## Keyboard firmware update procedure (documented by the vendor; not run here)

`wiki/Setting-Up-Arduino-Development-for-PicoCalc-keyboard.md:77-84`: put
DIP 1 ON, connect USB-C, run
`sudo stm32flash -w Bin/PicoCalc_BIOS_v1.6.bin -v -S 0x08000000 /dev/ttyUSB0`,
put DIP 1 OFF. Lines 109-128 give the STM32CubeProgrammer route on other
hosts (Connect, load the file, Download, Disconnect, DIP 1 OFF, power on).
The `Bin/` directory carries `PicoCalc_BIOS_v1.2`, `v1.4`, `v1.6` and `v1.7`
images with `.md5sum` files for v1.2, v1.4 and v1.6. This project has not
performed the update; `docs/research/picocalc-first-power-up.md` plans the
controller's identity and firmware dump first.

## Donor, port and gap

| Area | Donor | Port | Gap |
| --- | --- | --- | --- |
| Keyboard registers | 0x01-0x0E served | six used | 0x04 Open; 0x0C, 0x0D, 0x08 declined |
| OFF read | constant 1 | sysctl keeps the countdown | document corrected |
| Break, power | sent | ETX, raw event | closed |
| Panel write clock | 25 MHz request | 12.5 MHz | declined; port inside TSCYCW |
| Panel scroll | RAMRD redraw | hardware scroll | none |
| Panel colors | RGB666 | RGB565 console, RGB666 lcdgfx | none |
| Audio | 87.6 kHz carrier, 10.95 kHz samples, clips | 500 kHz, 8 kHz, scaled | none |
| PSRAM | pins GP2/3/20/21, PIO1 | GP2-5/20/21, PIO0 | donor source absent |
| FUZIX ttys | up to four USB | one USB tty | no consumer; declined |
| uLisp | 4.4b | 4.8f | none |
| Flash map | 200 KiB application offset | 128 KB kernel, 1536 KB root | incompatible; declined |
| Keyboard update | wiki procedure | none | not run |

## Disagreements

Between donor programs:

- Audio pins: MP3 player GP28 and GP27 (`MP3Player/config.h:20-21`) against
  helloworld and the schematic GP26 and GP27.
- Keyboard I2C: 10 kHz in helloworld and tester (`i2ckbd.h:12`), 400 kHz in
  the FUZIX patch (first-pass reading of `fuzix.patch:1736-1741`).
- The LVGL demo's tick: the comment says 5 ms, the call is `lv_tick_inc(20)`
  (`main.c:88-90`).
- Helloworld's Ctrl bytes (0x7e02/0x7e03) against the firmware's 0xA5.
- Panel controller named ILI9488 in code and ST7365P in the shipped datasheet.

Between donor and port:

- OFF register read (constant 1 against the port document's "last
  countdown"), settled by the keyboard commit.
- The 25 MHz write clock against 12.5 MHz.
- Panel reset timing, COLMOD default, scroll method.
- Audio carrier and pacing.
- Port Renode model note said it served "lock flags" at 0x04; it serves the
  count only (corrected in the keyboard commit).

## Change record for the keyboard commit

Observation: `picokbd_translate()` returned 0 for key 0xD0 (Break, the
Shift+Esc chord) and 0x91 (power); `picokbd_evq_put()` already records every
PRESSED, HOLD and RELEASED event before translation, so a raw session sees
both; `PICOCALC.md` said the power-off countdown was the register's read
value.
Rule: the firmware reads OFF as the constant 1 (`picocalc_keyboard.ino:194`);
Ctrl-C reaches the tty as ETX from `picokbd_translate()`, so the interrupt
path is the tty's `CINTR` handling of byte 0x03.
Hypothesis: Break sent as ETX in the cooked path reaches the same interrupt
character as Ctrl-C at the cost of a few bytes of text; the power key needs
no kernel change.
Falsifier: a text growth above 100 bytes on either PICOCALC kernel, or a
host test in which a Break or power event leaves the raw queue.
Predicted gate movement: `lcdterm_test` and `kbdevq_test` gain cases and
pass; three new mutants fail them; both kernels grow by well under 100
bytes and keep their headroom.
Result: PICOCALC text 127436 to 127444 bytes and PICOCALC_SD 128916 to 128924
(+8 each); headroom unchanged (PICOCALC 3448, PICOCALC_SD 1912 bytes over the
1024 margin).
Design choice: Break sends ETX, not a separate byte or signal, because the
interrupt character is the path Ctrl-C already takes through the tty and
every program that handles Ctrl-C handles Break; a program that changed its
interrupt character gets ETX as data, exactly as Ctrl-C behaves. A program
that wants the key itself uses a raw key event session, which receives 0xD0
as sent. The power key types nothing on the cooked path and the kernel
carries no shutdown policy for it.

## Adopted, declined, open

Adopted:

- The OFF register reads constant 1: `PICOCALC.md` row and paragraph, the
  Renode model comment and a host-test assertion.
- Break (0xD0, Shift+Esc) types ETX; the power key (0x91) types nothing and
  reaches raw sessions; tests and mutants added.
- The shifted layer, firmware hotkeys, `pmu_online` gating and the OR of the
  two 2.5 s watchdog timers recorded as firmware facts.

Declined with reason:

- Copying any donor code, init table text or key tables: no license.
- The 25 MHz write clock: outside TSCYCW 66 ns (page 54); the port stays at 12.5 MHz.
- Software RAMRD scroll, RGB666 console, 87.6 kHz carrier, PIO1 PSRAM, the
  200 KiB application offset and four USB ttys: each is worse than or
  unrelated to the port's design, or has no consumer.
- Registers 0x08 (resets the controller), 0x0C and 0x0D (C64 matrix and
  joystick bits): no use in this port.

Open:

- Register 0x04 (key count and lock bits): the port never reads it. A read
  would give Caps and Num lock state without tracking Alt chords on the
  host, and the FIFO depth; it needs the mailbox's reply check to accept
  a reply that does not echo the register number. Not implemented.
- The datasheet page citations for TSCYCR in four port files (77 should be 53
  and 54).
- `BIOS v1.7` binary is in `Bin/` without source: whether a unit's
  controller matches this source is read from register 0x01 on a board.
- Alt+comma hold repeating a comma and the battery-insert bit 7 behavior are
  source readings only.
- The SDK's SPI default format and the real vendor panel clock: the SDK is
  not in the donor.
- The firmware's I2C slave speed limit: not found in the sketch.
- The `rp2040-psram` program and `PicoMite.patch` pin tables (physical pin
  numbers, not re-opened) are unread.
- Not re-opened in this pass: `Code/NES`, `Code/MP3Player` beyond
  `config.h`, `picocalc_lvgl_graphics_demo` beyond `main.c` and `lv_conf.h`,
  and the AXP2101 datasheet.

## Corrections to the first-pass readings

- `boot.c` MIT notice is at line 3, not 2.
- Helloworld's PRESSED test `(buff & 0xff) == 1` is correct; only its Ctrl
  codes are dead.
- The port documented the OFF read as the countdown only for the sysctl, which
  keeps its own copy; the register read is the constant 1.
- Shift+Esc, not Alt, produces Break; the first-pass table did not say how the
  key is reached.
- Panel init has INVON (`lcdspi.c:599`); the first-pass list omitted it.
