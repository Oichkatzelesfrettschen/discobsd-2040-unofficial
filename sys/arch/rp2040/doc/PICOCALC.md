<!--
Copyright (c) 2026 DiscoBSD

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
-->

# ClockworkPi PicoCalc: the PICOCALC kernel and the board it drives

The PICOCALC configuration (`sys/arch/rp2040/compile/PICOCALC/Config`)
runs DiscoBSD on a Raspberry Pi Pico (RP2040) seated in the ClockworkPi
PicoCalc mainboard V2.0. Its console is the board's own 320x320 panel and
keyboard. Root and swap stay on the Pico's QSPI flash, the same layout as
the PICO board (`STORAGE.md`). The PICOCALC_PSRAM configuration
(`sys/arch/rp2040/compile/PICOCALC_PSRAM/Config`) is the same kernel with the
board's PSRAM driver linked in; swap moves to the PSRAM when it identifies
itself at boot (below). This document is the authority for the pin
map in `sys/arch/rp2040/dev/picocalc.h`, the console drivers' timing
choices, and the evidence each driver has.

The vendor documents live in the sibling repository
`discobsd-2040-notes` under `docs/picocalc/`, copied from
github.com/clockworkpi/PicoCalc at commit `fdcfe2c8`. Citations below
name a file there, with a page for a PDF.

## Evidence status

| Claim | Class | State |
| --- | --- | --- |
| PICOCALC compiles and links with `-Wall -Wextra -Werror` and carries no PSRAM driver | build | observed, GCC 13.2.1 (Ubuntu `gcc-arm-none-eabi` 15:13.2.rel1-2, the Linux CI compiler) at `-Os`: text 111,551, data 368, bss 48,824 with the pixel device (Pixel device, below); the flash image `check-kernel-headroom` measures is 111,664 of 130,816 bytes (`RP2040.ld` FLASH), 19,152 bytes of headroom; before the pixel device the same compiler measured text 109,406, bss 48,752 and 21,296 bytes of headroom, and GCC 16.2.0 (local) text 108,942; no `psram` symbol in `unix.elf`; at `-O`, which preceded `-Os`, GCC 13.2.1 measured text 126,100 and 4,604 bytes of headroom; an earlier `-O` build measured the console alone at 118,268 text, 304 data and 45,888 bss, and the sysctls (+916), the bell (+260) and the SD card with its multi-mount kernel (+3,888) add 5,064 text to it |
| PICOCALC_PSRAM compiles and links with `-Wall -Wextra -Werror` and carries the SD driver of PICOCALC | build | observed, GCC 13.2.1 at `-Os`: text 113,709, data 424, bss 48,976 with the pixel device; flash image 113,880 of 130,816 bytes, 16,936 bytes of headroom; before the pixel device text 111,564, bss 48,896 and 19,084 bytes of headroom, and GCC 16.2.0 (local) text 111,004; before the pixel device it exceeded PICOCALC by 2,158 text, 56 data and 144 bss bytes, where `psram.o` and `psram_xfer.o` hold 2,120, 8 and 146 and the swap selection, the block-device entry and the longer configuration name in `version` and `osversion` hold the rest; `psram_xfer.c` compiles at `-O` through `COMPILE_CYCLE_C` because its receive loop is cycle counted |
| `lcdterm.c`, `lcdfont.c`, `picokbd_map.c`, `st7365p.c` compile as strict ISO C17 (`-std=c17 -pedantic-errors`) | build | observed, arm-none-eabi-gcc and host gcc/clang |
| The console draws cell codes 0x80-0xff from a 128-glyph RAM bank, `ESC ( 0` maps 0x5f-0x7e onto bank codes 0x80-0x9f holding line-drawing shapes whose strokes meet the cell borders, and `LCDIOC_SETGLYPH` replaces a glyph and redraws the cells that show it | host | observed by `check-picocalc-host` against the model panel, which records the nine bank rows of every bank cell: shapes, joins, upload, redraw, rejection, raw-byte rule, resets; eight mutants fail it; the production `ioctl()` classifier copies the ten-byte payload under the same `PICOCALC_ENABLED` define as the PICOCALC kernels (`check-kernel`, one mutant) |
| The tenth cell row of a bank glyph repeats its ninth, and `lcd_draw_cell()` clocks the bank rows to the panel | build | compiles and links into PICOCALC and PICOCALC_PSRAM; `st7365p.c` has no host model, so the repeated row and the pixels are read from the source, not run |
| The bank on the panel: box drawing in `mc` or curses, a tile program, redraw after an upload | board | not run |
| `/dev/lcdgfx` (character major 10, minor 1) takes the panel from the text console on open, refuses a second open with `EBUSY`, draws fills, pixels, lines, frames, circles, streamed windows in three formats, palette, scroll, scroll definition and color-format commands, rejects every out-of-range field without drawing, and on the last close restores COLMOD 0x66, VSCRDEF 0/320/160, the margin columns, the scroll start of the ring and every cell of the store | host | observed by `check-picocalc-host` (`lcdgfx_test`) against a model of the controller's write path and the real terminal: window, byte, scroll and format protocol checked on every call, fills and frames compared with their rectangles, lines with the ideal line, circles with their radius and symmetry, windows pixel by pixel in all three formats and in 2-, 130- and 520-byte writes, 51 range cases, the close restore compared with a text-only redraw of the same bytes; 37 mutants fail it (9 more fail `lcdterm_test` at the terminal) |
| The ioctl classifier copies each `LCDG_*` payload (at most 12 bytes) into aligned kernel storage under `PICOCALC_ENABLED` and stages `LCDG_INFO` and `LCDIOC_GETKEY` (14 bytes) as zeroed output, `LCDIOC_KEYEVS` as an integer input | host | observed by `check-kernel` (`fionread_test`, 688 checks, was 605): ownership, upper span edge, crossing span, provider error and reserved-bit cases for all thirteen `LCDG_*` commands, `LCDIOC_GETKEY` and `LCDIOC_KEYEVS`; seven mutants (one per command family, the tile commands and the two key commands included) fail it |
| `LCDG_TILESET` stores tiles into a 128-tile atlas that is zero at every open, bounded by `first + count <= 128`; `LCDG_TILEWIN` draws a block of tile numbers as one panel window per tile row, with the eight pixel lines of every tile in the row interleaved, through the palette and COLMOD in force; a field out of range or a tile number of 128 or more draws nothing further | host | observed by `check-picocalc-host` (`lcdgfx_test`) against the model panel, which logs each window with its pixel count and stores each pixel at its raster position: 128 tiles uploaded in nine write sizes, blocks from one tile to the 40 by 40 grid (exactly one window of `tw * 8` by 8 per tile row, every pixel compared with a reference render), palette and COLMOD 0x55 draws, 20 rejected commands, upload bounds and abandonment, a copy fault resumed, and the zero atlas at open; 24 mutants fail it |
| `games/gametile.h` sends one `LCDG_TILEWIN` per maximal run of changed tiles in a tile row, none for a clean tile, and leaves a failed run owed | host | observed by `check-picocalc-host` (`gametile_test`) against a model of the tile commands: 300 random rounds and scripted cases compared with runs counted by an independent loop; ranges near the limits of int are refused without an overflowing sum (`gtile_fill`, `gtile_load`, run under UBSan); 29 mutants fail it |
| A raw key event session records nothing outside itself: `LCDIOC_KEYEVS` starts it only for a descriptor open for reading in the tty's foreground group, the queue is empty and its bytes cleared at its start and end, it needs a RAW or CBREAK tty, belongs to the process that started it and ends on that process's exit or exec, on any change of the tty's mode word (so a getpass() read cannot be recorded), on a mode-setting ioctl by another process, on the owner's last close, on request and when the foreground group changes, and `LCDIOC_GETKEY` drains oldest first, six per call, from the owning, readable, foreground caller only, drops the oldest event when full and reports the drops; the xterm bytes the poller types are those of `picokbd_translate()` alone | host | observed by `check-picocalc-host` (`kbdevq_test`): write-only, background and non-owner starts, drains and stops refused with the queue and drop count intact, a second owner refused, the owner's exit ending the session while another process holds the file, pid and slot reuse refused, nothing held and no key byte in memory outside a session, a getpass-like ECHO clear ending the session, restored flags not reviving it, RAW to CBREAK and ECHO and CRMOD toggles ending it, cooked starts refused, session end by close, exit, request and foreground change, every state against every key, a 20,000-event run, overflow at 33, 40, 50 and 1,000 events, the count saturating at 255; 44 mutants of `picokbd_evq.c` fail it, and `wiring.sh` fails sixteen cuts in the kernel glue it reads (the poller's `picokbd_event()` and `lcdcons_keycheck()` calls, the `lcd_modeset()` hook of the five mode-setting ioctls, `lcdcons_fileclose()` and `lcdcons_procexit()`, the `FREAD`, process-group and pid arguments of both requests, the hooks in `vn_closefile()`, `exit()` and `exec_clear()`); `kbd_poll()`, `lcd_ioctl_file()` and the close hook themselves run on no host |
| The tile atlas and `LCDIOC_GETKEY` compile into PICOCALC and PICOCALC_PSRAM only; PICO and PICO_UART keep their text, data and bss | build | observed, GCC 13.2.1 at `-Os`: PICOCALC text +1,432, bss +4,264 (112,975 / 368 / 53,088); PICOCALC_PSRAM text +1,432, bss +4,264 (115,133 / 424 / 53,240); flash headroom 17,728 and 15,512 bytes; the kernel's data and bss end 62,408 bytes below the end of the RAM region (Tile atlas) |
| The tile atlas on the panel: the tiles appear, a 40-tile row costs 4.92 ms plus a window, a one-row vertical scroll step costs 4.9 ms, the raw key events arrive at the keyboard's rate | board | not run: `tests/rp2040/tiledemo` is built by `check-board-build` (text 7,379 bytes) and a board run is opt-in |
| `lcdgfx.c` compiles into PICOCALC and PICOCALC_PSRAM only; PICO and PICO_UART keep their data and bss | build | observed, GCC 13.2.1 at `-Os`: PICOCALC text +2,145, bss +72; PICOCALC_PSRAM text +2,145, bss +80; PICO 95,403 / 248 / 39,848 and PICO_UART 85,618 / 192 / 14,832, 9 text bytes above the measurement before the pixel device, which was taken in a different build path, the offset `version[]` carries (not traced further); at `-O` under GCC 16.2 the PICO and PICO_UART images differed only inside `version[]` (`objcopy -O binary`) |
| `st7365p.c` clocks the pixel primitives and `lcd_gfx_reset()` to the panel | build | compiles and links; `st7365p.c` has no host model, so the SPI bytes, the VSCRDEF value and the COLMOD 0x55 pixel format are read from the source and from the model's protocol, not run |
| `/dev/lcdgfx` on the panel: the shapes appear, the timings match the research note's table, a killed program leaves the text visible | board | not run: `tests/rp2040/lcdgfxtest` is built by `check-board-build` and a board run is opt-in |
| `picokbd.c`, `lcdcons.c` compile as strict ISO C17 (`-std=c17 -pedantic-errors`) under the PICOCALC definitions | build | observed by `check-console-c17-cross` (`TESTING.md`); `sys/sys/exec.h` no longer declares an empty struct and `sys/sys/inode.h` includes `uio.h` instead of forward-declaring `enum uio_rw`, and `struct exec_params` keeps its 284-byte layout; the objects still build under the kernel's GNU17 profile |
| The terminal draws what its cell store holds, scrolls, wraps and parses the xterm-basic sequences | host | observed by `check-picocalc-host` against a model panel |
| The PICOCALC console path reaches a login on a modeled panel, and keys typed through a modeled keyboard controller log in and run a command | Renode | observed by `check-renode-picocalc` (`TESTING.md`, Renode PicoCalc tier); the models record MADCTL, INVON and the keyboard timing without applying them |
| The panel initializes, shows text and scrolls | board | not run: no PicoCalc has booted this kernel |
| The keyboard controller answers at 0x1F and its events type | board | not run |
| The board runs from USB-C with no cells | board | observed by the owner: it stays off with no cells and runs with them (below) |
| `machdep.picocalc.*` compiles into PICOCALC and PICOCALC_PSRAM only; PICO and PICO_UART keep their text, data and bss sizes and differ from the earlier build only inside `version[]` and `osversion[]` | build | observed: PICOCALC text +916 bytes, bss +16 over 118,268 and 45,888; PICO 107,432 text, 248 data, 39,848 bss and PICO_UART 95,970, 192, 14,832 equal the sizes recorded when the PSRAM driver landed |
| The register mailbox (`picokbd_req.c`) encodes reads and writes as the vendor firmware's `receiveEvent` decodes them, keeps a request off the key exchange, retries a faulted exchange three times, and drops a reply that outlives its deadline | host | observed by `check-picocalc-host` against a model of the firmware's register handler; eight mutants fail it |
| BEL (0x07) reaches the bell primitive once per byte, from the ground state and inside a control sequence, and draws, moves and scrolls nothing; other controls stay silent | host | observed by `check-picocalc-host` against the model panel; two mutants fail it |
| `picoaudio.c` compiles into PICOCALC and PICOCALC_PSRAM only and keeps the PWM divider and wrap value inside their register fields | build | observed: PICOCALC text +260 bytes, bss +8 over the sysctl build; two `_Static_assert`s bound DIV.INT and TOP; PICO and PICO_UART unchanged |
| The tone is audible on the speaker, 1 kHz for 100 ms at 25 percent duty, and the outputs stay low afterward | board | not run |
| `sd.c` and `sdspi.c` compile into PICOCALC and PICOCALC_PSRAM only, with `NMOUNT=2` and without `SINGLE_UFS_ROOT`; PICO and PICO_UART keep their sizes | build | observed: PICOCALC text +3,888 bytes (2,887 in the two SD objects, the rest the multi-mount inode and mount code) and bss +1,688 over the audio build |
| `umount` and a failed `mount` of `sd0a` close the partition only at the last close, and a block descriptor opened before the mount keeps reading after it | host | observed by `check-kernel-ilp32` (`kernel.ufs-disk.ilp32`) against the production `ufs_mount.c`, `sys_inode.c` and `ufs_inode.c` and a model of the driver's open mask; the parent revision fails 13 checks of the same gate, and two mutants fail it |
| `umount` of a read-write `sd0a` whose card was pulled succeeds without write-back when `sdioctl` answers `DIOCMEDIAGONE` from the replaced flag, closes the partition, leaves no valid or delayed buffer of the device, and lets a replacement card mount; a write error with the card present, a driver that fails the command and an open file keep `umount` failing | host | observed: `check-kernel-ilp32` (`kernel.ufs-disk.ilp32`) against the production `ufs_mount.c`, `ufs_bio.c` and `ufs_syscalls2.c` and a model of the driver, with 19 of 303 checks failing on the preceding `ufs_mount.c` and eight mutants rejected; `check-picocalc-host` runs the production `sdioctl` through the card swap (the flag reads 0 before the debounce interval ends, 1 after, 0 after the next open) and three mutants of `sd.c` fail it |
| The pulled-card sequence on the board: mount read-write, write, pull, `umount`, reinsert, mount | board | not run: `tests/rp2040/sd_detach/board_sd_detach.py` is written, closed unless `DISCOBSD_BOARD_SD_DETACH` is exactly `run`, and has no recorded result |
| At securelevel 1 a write open of `rsd0`, of `sd0`, of an MBR entry that overlaps the mounted `sd0a`, and of the flash node `fl0` while the root `fl0a` is mounted is refused with `EBUSY`, while a partition that only touches the mounted one and the swap unit `fl1` open | host | observed: `check-kernel-ilp32` (`kernel.ufs-disk.ilp32`) runs the production `openi()`, `ufs_mountedon()` and the `blkoverlap()` extracted from `conf.c` over the production `flioctl` and a model of `sdioctl`; the preceding `ufs_fio.c` fails 11 checks and seven mutants (exact comparison, unit ignored, major ignored, touching extents counted, start offsets compared, the whole-device node disjoint, an undescribable node disjoint) fail the gate; `check-picocalc-host` holds the production `sdioctl` to the `DIOCGETPART` extents the model returns |
| The SD protocol layer frames commands, initializes SDSC version 1, SDSC version 2 and SDHC cards, decodes both CSD versions, addresses by byte or block, and fails with a bounded wait on an absent, stuck, unsupported or erroring card | host | observed by `check-picocalc-host` against a card model replaying recorded command transcripts; twenty mutants fail it |
| `fsutil --repartition=fs=8m` and `fsutil --new --partition=1` make an image whose sector 0 holds the 0x55AA signature and one type 0xB7 entry at sector 2 in the 16-byte layout `sd.c` copies into `struct diskpart`, and `fsutil --check --partition=1` accepts it | host | observed on a scratch image; the kernel side of the parse is not run |
| A UFS partition on the card mounts beside the flash root, `fdisk` and `mkfs` reach the card through `rsd0`, and the card-detect change fails open transfers | board | not run |
| The SD socket's card-detect polarity, the fast clock at 12.5 MHz on this board's traces, and the busy times of real cards | board | not run |
| The crash-dump writer (`sddump()`, `rp2040/dump.c`, `dumpsys()`) links into PICOCALC and PICOCALC_PSRAM only | build | observed by `check-dump-writer-link` on the symbol tables of all four kernels, with a swapped-role control that exits 1 |
| `sddump()` writes a complete dump to the first type 0xb9 entry inside the card, writes nothing outside it, refuses a missing card, table, signature or entry, a partition past the card, an undersized partition and one that a block open of the accepted card overlaps, accepts one beside a mount or on a replaced card, and leaves sector 0 zero after a failed sector; the span transfers keep their window and sum the bytes sent while the source changes | host | observed by `check-picocalc-host`: `sdstrat_test` (56 mutants of `sd.c` and `dump.c` rejected) and `sdspi_test` (23 mutants of `sdspi.c` rejected) |
| `savecore` saves a dump the kernel's stream wrote, names a different running kernel, advances the bounds file, and leaves the dump on the card for a damaged segment, header checksum, version, segment count or length, one byte too little space with and without a reserve, and a copy that cannot be written; an unfinished dump reads as none | host | observed by `check-picocalc-host` (`dump_test`, 9 mutants of `savecore.c` rejected) |
| On Renode's SD card model, `reboot -d` writes a dump, the next boot's `savecore /mnt/crash` saves `dump.0` and a second run finds none, and a panic entered from the idle loop prints `dump: sd0b, 1024 kbytes: succeeded` and `halted`; the panic's dump on the card and `dump.0` pass an independent reader of the format | Renode | observed by `check-renode-picocalc-dump`: message 12 bytes, kernel data 48,136, two u areas of 3,072, `dump.0` 55,808 bytes; a kernel that dumps only on the reset path fails the panic stage |
| A panic on the board writes a dump that `savecore` saves | board | not run: the bench board is a Pico without the PicoCalc's SD socket |
| The controller answers the battery, backlight and power-off registers, the sysctl write reaches the right register, and the split-phase poller keeps polling keys around a request | board | not run |
| `psram.c`, `psram_xfer.c` and the PIO program in `psram_pio.h` execute frames against a timing model of the ESP-PSRAM64H: waveforms, tCEM, tCSP, tCHD, tCPH, clock period, read sampling margin, 40 random transfers, four injected faults, the four placements of the two register stages and thirteen mutants | host | observed by `check-psram-pio` (1,197 checks); the model's pad delays and the clock of the two register stages are assumptions |
| PICO, PICO_UART and PICOCALC carry no PSRAM driver | build | observed: no `psram` symbol in the `unix.elf` of any of the three; PICO and PICO_UART sizes equal those recorded when the driver landed |
| The Renode RP2040 model runs the PSRAM program | Renode | not run: the pinned model has PIO but no input synchronizer latency, the tree has no PSRAM peripheral, and `renode-test` cannot run on the build host (`docs/research/picocalc-psram.md`) |
| The part answers Read ID 0x0D with a passing die, enters quad mode and returns what was written at 31.25 MHz | board | not run |
| The kernel swaps to PSRAM and falls back to `fl1` when the part is absent | board | not run |

## Pin map

From the schematic's connectors J301 and J302, checked against the vendor
code (`docs/picocalc/INDEX.md` in the notes repository holds the full
table and the sources that agree):

| Function | Pins | Driver |
| --- | --- | --- |
| UART0 to the CH340C USB-serial bridge | GP0 TX, GP1 RX | `uart.c`, `/dev/tty0` |
| PSRAM, ESP-PSRAM64H 8 MB | GP2-GP5 (SIO0-3), GP20 CS, GP21 SCK | `psram.c`, `psram_xfer.c`, PIO0 state machine 0, block major 3 |
| Keyboard controller, I2C1 at 0x1F | GP6 SDA, GP7 SCL | `picokbd.c` |
| UART1, expansion header | GP8 TX, GP9 RX | none |
| ST7365P panel, SPI1 | GP10 SCK, GP11 TX, GP12 RX, GP13 CS, GP14 D/C, GP15 reset | `st7365p.c` |
| SD card, SPI0 | GP16 RX, GP17 CS, GP18 SCK, GP19 TX, GP22 card detect | `sd.c`, `sdspi.c`, `sd0` and `rsd0` |
| Audio, PWM slice 5 | GP26 left (5A), GP27 right (5B) | `picoaudio.c`, the BEL tone |

The vendor's `sd_boot/config.h` puts left audio on GP28; the schematic
wires PWM_L to GP26, and GP28 reaches only the expansion header.

## USB-C and power

DIP switch SW701 selects, through two WAS7227 switches, whether USB-C
reaches the CH340C bridge and the Pico's UART0 (default, both selects
pulled low) or the keyboard controller's USB and UART1 for its own
firmware updates. In the default position, the USB-C port carries
`/dev/tty0` at 115200 baud, so a PICO_UART kernel also has a working
console on this board. The Pico's micro-USB port stays the PICO board's
CDC-ACM console and the `picotool reboot -u` path, `/dev/ttyUSB0` here.

The AXP2101 PMU powers the system from USB-C alone: its power path
connects VBUS to VSYS through the linear charger (AXP2101 datasheet 6.4,
page 18), it runs from the higher of VBUS and the battery (6.5.1), and it
starts the system when a good VBUS appears (6.5.3). The vendor assembly
guide asks for a 5 V 2 A supply "to power and charge the product".

The owner's PicoCalc stays off on USB-C with no 18650 cells installed and
runs with cells, which contradicts the reading above. The keyboard
firmware does not gate the Pico: its `setup()` raises PA13, the Pico
enable, unconditionally (`picocalc_keyboard.ino`), so the inference is
that no rail comes up at all, including DCDC1 (MCU_3V3) for the STM32.
The schematic wires R701 and R702, 5.1 kOhm each, from the receptacle's
CC1 and CC2 to ground, so a USB-C-to-C supply sees a sink and switches
VBUS on; a missing CC pull-down explains the observation only on a board
that leaves R701 and R702 unfitted, which a look at the two sites
settles. The explanation the schematic leaves standing:

- Hypothesis: the AXP2101 needs a power-key press or a battery to start.
  Its datasheet (V1.4, 6.5.4.2) lists VBUS rising as a power-on source
  only "configured by customization", and the power key SW102 sits on its
  PWRON pin; this board's customization is unrecorded. A cell-less board
  that stays off on USB-C and starts after a press of the power key
  confirms it; one that starts on USB-C alone refutes it.

`docs/research/hardware-reference-coverage.md` holds the schematic's
designator-to-part map behind this reading.

The Pico's own micro-USB port feeds the Pico module's VSYS through the
module's Schottky diode; whether the mainboard's PICO_VSYS net, the STM32
and the panel run from it is not measured. With cells installed, the
micro-USB port is the console and BOOTSEL path.

## Display: ST7365P on SPI1

`st7365p.c` runs the PL022 in Motorola mode 0 with eight-bit frames at
12.5 MHz (prescale 2, SCR 4 from the 125 MHz `clk_peri`). The ST7365P
specification's minimum serial write cycle is 66 ns (page 54), which
caps the clock at 15.1 MHz; the vendor drivers request 25 MHz and get
20.8 MHz, outside that limit. Chip select, D/C and reset are SIO outputs;
D/C changes only after the PL022 reports idle.

Pixels are RGB666, three bytes (COLMOD 66h), as every vendor driver for
this panel sends them. The controller also accepts RGB565 over four-wire
SPI (page 96); switching would cut a glyph from 180 bytes to 120 and is
worth measuring once the board has run the RGB666 path.

The panel shows 320 of the controller's 480 lines. VSCRDEF sets a top
fixed area of 0, a scroll area of 320 and a bottom fixed area of 160,
which sums to 480 as page 184 requires, with MADCTL MV clear as scrolling
requires (page 184). VSCSAD (37h) then picks the RAM line shown at the
top of the panel. The initialization values (gamma, power, VCOM, frame
rate, inversion on, display function, entry mode, adjust control,
MADCTL 48h) come from the vendor's PicoMite ILI9488P (320x320) sequence
for this panel, with three deliberate differences: command E9h, which the
vendor sends but the ST7365P command table does not list, is omitted; the
scroll definition is programmed before the display turns on; and DISPON
follows lcdterm's first full clear, so power-on RAM contents never show.
A board capture decides whether E9h matters.

The board drives the backlight from the keyboard controller, which turns
it on at reset; the kernel does not touch it.

## Console: lcdterm.c and lcdcons.c

The console is a 53x32 character-cell terminal: 6x10 cells, the public
domain Misc-Fixed 6x9 font (`dev/fonts/6x9-ascii.bdf`, NOTICE section
15) with its tenth row as line spacing and the underline row. It
implements what the `xterm-basic` termcap entry emits, because
`/etc/ttys` names the console `xterm`; curses, `more`, `ls` and readline
take the 53x32 size from `TIOCGWINSZ`, which the driver sets at open.
`login(1)` sets a zero window size on every console login
(`usr.bin/login/login.c`), and a zero row or column count means the size is
unknown, so the driver puts the panel's size in its place; a nonzero size
a program sets is kept.
The DEC line-drawing set (`ESC ( 0`) draws real lines from the glyph bank
(below). BEL calls `lcd_bell()`
in `picoaudio.c`, which starts a short tone from the output path without
waiting for it (Audio, below).

The cell store is 3,392 bytes: one character and one attribute per cell,
kept as a ring of physical rows. A full-screen scroll blanks the row that
leaves, advances the ring and moves VSCSAD, so it costs one row of fill.
A scroll confined to a region, and line insertion or deletion, move the
store and redraw the affected rows.

The tty layer calls a start routine with `spltty()` held, which on this
port is PRIMASK, and `softclock()` runs callouts inside the SysTick
exception, which cannot preempt itself, so neither may draw a queue.
`lcdstart()` marks the tty busy and pends IRQ 26, which the RP2040 leaves
unconnected for software to pend through NVIC_ISPR (datasheet 2.3.2). Its
handler draws at the `IPL_SOFTCLOCK` priority byte, 128: SysTick (0) and
the UART and USB interrupts (64) preempt it, and PendSV (192), which
switches processes, waits for it, so no switch lands inside a panel
window. The handler masks interrupts only to take a byte off the output
queue, so a region redraw of hundreds of cells never holds the clock off.
Because it runs above thread mode, it also holds off every process, system
call and the PendSV context switch while it draws, so it draws for at most
8 ms per pass and, with output still queued, lets thread mode run for a
tick before a callout pends it again. One escape that redraws a scrolling
region cannot be split and can exceed the slice.

ARMv6-M has one handler stack, MSP, and this kernel keeps it in the
current process's u-area, where the measured kernel-stack peak leaves about
220 of 2,048 bytes (`CAPACITY.md`). The renderer is an extra exception
level that SysTick, USB and a fault report can nest above, so
`SPARE_IRQ_0_Handler` (`dev/lcdrender.S`) switches to a 2 KB render stack
in kernel bss before calling `lcdrender()` and switches back before the
exception return; every exception that preempts the renderer stacks there.
The render stack is painted at boot, and `boot()` prints how much of it no
exception ever reached ("lcd0: render stack N of 2048 bytes never used")
on halt or reboot. Terminal reports (cursor position, device attributes)
queue during a draw, whole or not at all, in a 64-byte ring that holds
eight of the longest report, and reach the tty input queue after the
drawing flag is released, so `ttyinput()` never runs inside a draw.

A cell is one panel write window. `lcdcons.c` serializes drawing with an
owner flag: the renderer and the kernel's console putc each take the
flag, draw, and release it; a putc from an interrupt nested over the
owner appends to a 128-byte deferred ring that the owner drains in order
before releasing; once the ring overflows, later bytes are dropped as well
until the drain prints "[console output lost]", so the marker stands at
the gap. Kernel putc draws synchronously at its caller's priority: three
cells per character (cursor hide, the glyph, cursor show), about 366 us,
plus one row fill, about 6 ms, at a newline that scrolls the full screen.
With a scrolling region set by a program, a newline at the region's
bottom redraws the region cell by cell, up to about 200 ms at the caller's
priority; that is the price of keeping kernel messages synchronous.

The first kernel fault or panic resets the terminal (parser, character
set, attributes, scrolling region, scroll start) whether or not a drawer
was active, so a program's colors or an unfinished escape cannot shape or
swallow the report. A kernel fault (`arm_fault()`, before it prints)
abandons the owner, which will never resume: `lcdabandon()` lets the byte in the PL022 shifter
finish, raises chip select to end the panel's write window, resets the
terminal parser, character set, attributes and scrolling region, rewrites
the panel's scroll start and frees the flag, so the report draws in plain
ASCII from a clean bus. Once `panicstr` is set, `panic()`'s own output
draws with interrupts masked, so neither the renderer, which stops drawing
tty output, nor nested output can interrupt or abandon the panic message.

Measured costs are owed to the board run. At 12.5 MHz a cell is 180 bytes
of pixels plus an 11-byte window, about 122 us of wire time, which bounds
output near 8,000 characters per second before CPU overhead.

### Glyph bank

The cell store is also a tile map. A cell code of 0x20-0x7e draws the ASCII
glyph of `lcdfont.c`; a code of 0x80-0xff draws one of 128 glyphs from a RAM
bank in `lcdterm.c`, nine rows of six pixels each, the leftmost column in
bit 7, bits 1-0 zero. `lcd_draw_cell()` in `st7365p.c` asks `lcdterm_glyph()`
for the rows and repeats the ninth as the tenth cell row (the underline
still wins), so a tile fills its 6x10 cell and a vertical stroke on glyph
rows 0 and 8 joins the cell above and below. Text and tiles share the
attributes (16 foreground colors, 8 background), the scroll ring, the cursor
and the model panel.

`ESC ( 0` maps 0x5f-0x7e to bank codes 0x80-0x9f, so curses box drawing,
which sends `l q k x m j t u v w n`, draws lines. The initial shapes are the
32 DEC special graphics: blank, diamond, checkerboard, the six control
symbols HT, FF, CR, LF, NL and VT, degree, plus-minus, the corners, crossing,
tees, the five scan lines, vertical line, less-or-equal, greater-or-equal,
pi, not-equal, pound and bullet. Strokes are one pixel wide, vertical in
column 2 and horizontal in row 4, so any two glyphs that face each other
across a cell border join. A raw byte of 0xa0-0xff draws its own bank code;
a raw byte of 0x80-0x9f is a C1 control that the terminal does not interpret
and draws `?`, as every byte above 0x7e did before the bank, which keeps the
line-drawing codes reachable only through the escape and a UTF-8 continuation
byte from drawing a box edge. The bank starts with the glyph of `?` in every
code 0xa0-0xff, so text with such bytes looks as it did. The 0x7f byte is
ignored as before. A bank code reaches the terminal only through a tty in
RAW or LITOUT mode: `ttyoutput()` (`sys/kern/tty.c`) keeps the low seven bits
of every other byte, so a program that draws codes 0xa0-0xff sets LITOUT
(`TIOCLBIS` with `LLITOUT`) or RAW and restores its mode on exit; the
`ESC ( 0` set sends seven-bit codes and needs neither.

`LCDIOC_SETGLYPH` (`sys/sys/lcdcons.h`, `_IOW('l', 1, struct lcd_glyph)`) takes
`{ u_char code; u_char rows[9]; }` on the console tty. It returns `EINVAL` for
a code below 0x80 or a row with a bit set in bits 1-0, and changes nothing in
that case; otherwise it replaces the glyph and redraws every visible cell
that holds the code, so a tile set can be edited under a drawn screen. It
takes the drawing flag first, so no draw reads a half-written glyph, and
returns `EAGAIN` if the flag is held, which only an interrupt nested over a
drawer can leave and a thread-mode caller cannot reach. `ESC c` restores the
initial bank. The soft reset of termcap's `is` and `rs` (`ESC [ ! p`) and the
fault path keep uploads, because the fault report is ASCII. The syscall
classifier (`ioctl()` in `sys_generic.c`) copies the ten bytes into aligned
kernel storage under `PICOCALC_ENABLED`, so the PICO and PICO_UART images are
byte-identical to the build before the bank (`objcopy -O binary`) except for
the timestamp inside `version[]`.

| Kernel | Before (text / data / bss) | After (text / data / bss) | Delta |
| --- | --- | --- | --- |
| PICOCALC | 123,636 / 368 / 47,600 | 124,284 / 368 / 48,752 | +648 text, +1,152 bss |
| PICOCALC_PSRAM | 125,744 / 424 / 47,752 | 126,400 / 424 / 48,904 | +656 text, +1,152 bss |
| PICO | 107,696 / 248 / 39,848 | 107,696 / 248 / 39,848 | none |
| PICO_UART | 96,226 / 192 / 14,832 | 96,226 / 192 / 14,832 | none |

The sizes are `arm-none-eabi-size` on `unix.elf` after a clean `bmake all` in
each `compile/<BOARD>` directory, with arm-none-eabi-gcc from the build host
and parent commit `122ec634`. The bss delta is the 128x9 bank; the text delta
holds the 288-byte shape table, the copy of defaults, `lcdterm_setglyph()`,
the ioctl case and the bank branch of `lcd_draw_cell()`. Redrawing the cells
of one code walks the 1,696-cell store and costs about 122 us of wire time per
matching cell (about 207 ms when the whole screen shows it), at the priority of
the caller.

## Pixel device: lcdgfx.c

`/dev/lcdgfx` is minor 1 of the console's character major 10 (`LCDGFX_MINOR` in
`dev/lcdgfx.h`; `lcdcons.c` hands that minor to the driver in `lcdopen`,
`lcdclose`, `lcdread`, `lcdwrite`, `lcdselect` and `lcdioctl`, so the
`cdevsw` table and the major-number tests of `conf.c` stay as they were). A
program opens `/dev/tty` for the keyboard and `/dev/lcdgfx` for the panel.
`sys/sys/lcdgfx.h` is the user interface: thirteen commands (`LCDG_FILL`,
`PIXEL`, `LINE`, `FRAME`, `CIRCLE`, `WINDOW`, `PALETTE`, `SCROLL`, `SCRDEF`,
`COLMOD`, `INFO`, `TILESET`, `TILEWIN`) of group `l`, numbers 16 to 28, each
with a fixed payload of at most 12 bytes. The tile atlas commands are the
subject of Tile atlas, below.

Opening takes the panel from the text console: `lcdcons_gfx(1)` takes the
console's drawing flag, `lcdterm_gfx(1)` sets `gfx_active` so that `lcdterm.c`
keeps updating its cell store, ring and cursor but calls none of
`lcd_draw_cell`, `lcd_fill_cells` and `lcd_set_top`, and the driver programs
scroll start 0, loads the 16-entry palette from the console's colors and
assumes RGB666 with a 320-line scroll area. A second open fails with `EBUSY`
before it touches the console; an open that finds the drawing flag held
returns `EAGAIN` and leaves the device closed. The last close, which a
process exit also performs, ends a window that still wants pixels and calls
`lcdterm_gfx(0)`: `lcd_gfx_reset()` programs COLMOD 0x66, VSCRDEF 0/320/160
and clears the two margin columns right of the last cell (the cell draws never
reach them), then `lcdterm_repaint()` programs VSCSAD from the ring top, draws
all 1,696 cells from the store (about 195 ms on the bus) and the cursor. Text
written to the console meanwhile therefore appears at the close. A fatal fault
runs the same restore: `lcdabandon()` ends the transfer when the device is
open and `lcdterm_abort()` leaves the mode before the report draws.

The decisions the research note left open:

- Coordinates are rejected, not clipped. Every payload field is an unsigned
  16-bit value, so a negative coordinate is a large one and fails the same
  range test. A command whose point, corner, circle extent or window leaves the
  320x320 panel returns `EINVAL` before the first panel write, which keeps
  every command atomic and the driver free of clipping code; a program clips
  its own shapes. The panel model fails the test on any window outside the
  panel, so a skipped check cannot pass unnoticed.
- `LCDG_WINDOW` carries the foreground and background palette indexes for the
  1 bpp format (`fg`, `bg`; 11 bytes of payload, 12 with alignment), where the
  note said "the current foreground and background". Formats: RGB565 (two
  bytes per pixel, low byte first, even byte counts per `write(2)`), palette
  index (one byte per pixel, high nibble ignored) and 1 bpp (most significant
  bit first, rows padded to bytes).
- The palette holds RGB565 entries (32 bytes), converted from the console's
  8-bit table at every open and by `LCDG_PALETTE`; the panel keeps six bits per
  component, so red and blue keep five bits and green six.
- `LCDG_SCRDEF` also moves the scroll start to the first line of the scroll
  area, and `LCDG_SCROLL` accepts lines inside the area the last definition
  gave. A definition must leave at least one scroll line (top + bottom < 480);
  the controller's own minimum scroll area was not checked, because the
  specification PDF is not in the tree.
- `LCDG_INFO` reports 320, 320, 128 tiles and 18 or 16 bits per pixel.
- A window opened by `LCDG_WINDOW` keeps chip select low between `write(2)`
  calls; any other command, and the close, ends it. Nothing else touches SPI1
  while the device is open, because the terminal draws nothing and
  `st7365p_abort()` ends the transfer on the fatal path.
- Lines use Bresenham's algorithm with one panel window per pixel, except an
  axis-parallel line, which is one window; a frame is four windows with no pixel
  drawn twice; a circle is the midpoint algorithm.

Sizes, from `arm-none-eabi-size` after a `bmake MACHINE=rp2040 kernel`,
GCC 13.2.1 (the Linux CI compiler) at `-Os`, against main before the pixel
device; headroom is what `check-kernel-headroom` measures against the
130,816-byte FLASH region:

| Kernel | Before (text / data / bss) | After (text / data / bss) | Headroom after |
| --- | --- | --- | --- |
| PICOCALC | 109,406 / 368 / 48,752 | 111,551 / 368 / 48,824 | 19,152 |
| PICOCALC_PSRAM | 111,564 / 424 / 48,896 | 113,709 / 424 / 48,976 | 16,936 |
| PICO | 95,394 / 248 / 39,848 | 95,403 / 248 / 39,848 | 35,420 |
| PICO_UART | 85,609 / 192 / 14,832 | 85,618 / 192 / 14,832 | 45,260 |

At `-O` under GCC 16.2 the device cost 2,408 bytes of text on PICOCALC and
2,400 on PICOCALC_PSRAM; at `-O` under GCC 13.2 PICOCALC_PSRAM did not fit
the region. Per object, measured at `-O` under GCC 16.2, the text delta is
`lcdgfx.o` 1,740 bytes (the ioctl dispatcher with the shape code
1,132, the write path 304, open, close and three helpers 304), `sys_generic.o` +164 (eleven
classifier cases), `lcdterm.o` +188 (gating, repaint, mode switch),
`st7365p.o` +184 (the pixel primitives) and `lcdcons.o` +128 (the minor
dispatch and `lcdcons_gfx()`). The bss delta is the 32-byte palette, the
window and mode state, and `gfx_active`. The research note estimated 2.1 KB of
text.

### Tile atlas

`LCDG_TILESET` and `LCDG_TILEWIN` (`sys/sys/lcdgfx.h`, staged plan item 9 of
`docs/research/picocalc-calculator-and-gui.md`) add a 128-tile atlas to the
pixel device. A tile is 8x8 pixels of four bits, 32 bytes: eight lines of four
bytes, the leftmost pixel in the high nibble, each nibble an index into the
16-entry palette. The atlas is the 4,096-byte `atlas[]` of `lcdgfx.c`, cleared
at every open.

- `LCDG_TILESET { first, count }` needs `count >= 1` and
  `first + count <= 128`; the following `write(2)` calls move `count * 32`
  bytes through `uiomove()` straight into the atlas, may end inside a tile,
  stop at the end of the upload (the excess stays unread) and draw nothing.
  Another command, rejected or not, and the close abandon an upload; the bytes
  that arrived stay.
- `LCDG_TILEWIN { tx, ty, tw, th }` needs `tw, th >= 1` and
  `tx + tw, ty + th <= 40`; the following `write(2)` calls supply `tw * th`
  tile numbers, one byte each, row by row. `tw_row[]` (40 bytes) collects one
  row; at its last number `tile_row()` opens one panel window `tw * 8` wide
  and 8 high at (`tx * 8`, `ty * 8`) and sends line 0 of every tile in the
  row left to right, then line 1, and so on, each pixel through the palette
  in force at that moment and the COLMOD in force. The window closes before
  the `write(2)` returns, so a tile stream never holds chip select between
  calls (a `LCDG_WINDOW` stream does). A tile number of 128 or more fails the
  `write(2)` with `EINVAL` before its row is drawn, keeps the rows already
  drawn and ends the block.
- Every field is range checked in the ioctl before the first panel write,
  and the three stream kinds (pixel window, tile upload, tile block) exclude
  one another: starting one abandons another.

The library is `games/gametile.h`: a 40x40 shadow map, a bitmap of unsent
tiles and `gtile_flush()`, which sends one `LCDG_TILEWIN` per run of marked
tiles in a row (1,808 bytes of structure). A run never crosses the row edge
and never bridges a clean tile: a tile costs 123 us on the bus against 7 us
for a window setup, so a bridged tile costs more than the window it saves
unless the system-call overhead of a run exceeds 123 us, which no one has
measured. `gtile_set()` compares with the shadow, not with the panel, so a
tile changed and changed back before the flush is still sent. The header is
host-testable because its two calls into the device are macros.

Sizes, from `arm-none-eabi-size` after `bmake MACHINE=rp2040 kernel` in a
scratch copy, GCC 13.2.1 (the Linux CI compiler) at `-Os`, before and after
the atlas and the raw key events (Keyboard, below); headroom is what
`check-kernel-headroom` measures against the 130,816-byte FLASH region:

| Kernel | Before (text / data / bss) | After (text / data / bss) | Headroom before | Headroom after |
| --- | --- | --- | --- | --- |
| PICOCALC | 111,543 / 368 / 48,824 | 112,975 / 368 / 53,088 | 19,160 | 17,728 |
| PICOCALC_PSRAM | 113,701 / 424 / 48,976 | 115,133 / 424 / 53,240 | 16,944 | 15,512 |
| PICO | 95,395 / 248 / 39,848 | 95,395 / 248 / 39,848 | 35,428 | 35,428 |
| PICO_UART | 85,610 / 192 / 14,832 | 85,610 / 192 / 14,832 | 45,268 | 45,268 |

The "before" column is main at the merge of the `-Os` headroom change, built in
a scratch tree; its PICOCALC text is 8 bytes below the 111,551 the pixel
device table records, the offset `version[]` carries between build paths. The
change adds 1,432 bytes of text and 4,264
of bss to each PicoCalc kernel. Per object: `lcdgfx.o` +440 text and +4,164
bss (4,096 of atlas, a 40-byte row buffer, 28 bytes of stream state),
`picokbd_evq.o` +592 text and +100 bss (the session and its checks), `lcdcons.o`
+344 text (the requests, the descriptor lookup, the mode check and the close and exit hooks),
`sys_generic.o` +36, `picokbd.o` +4 and `vfs_vnops.o`, `kern_exit.o` and
`exec_subr.o` +4 text each. At the measured revision, the kernel's data and
bss ended 62,408 bytes below the end of the then-current 106 KB RAM region on
PICOCALC_PSRAM (0x2002f438 against 0x2003e800), so the bss addition preserved
that revision's layout. PICO and PICO_UART compile no PicoCalc file and keep
their text, data and bss.

## Keyboard: picokbd.c and picokbd_map.c

The keyboard controller is an STM32F103 running the vendor's
`picocalc_keyboard` firmware as an I2C target at 0x1F on I2C1. A register
read is a one-byte write of its number, then a two-byte read; FIFO
register 0x09 yields one event as (state, key), and (0, 0) when empty.
The board routes no interrupt line to the Pico, so a callout polls. Each
exchange is split across callouts and never waits on the bus. While idle
the phases run 16 ms apart, the gap the vendor's reference driver leaves
between write and read; once an event arrives they run 2 ms apart until
eight exchanges in a row come back empty, about 250 events per second.
The rate matters because the controller's FIFO holds 31 events and drops
new ones when full, and a dropped Ctrl release would leave Ctrl applied
to every later key. Two rules bound that: an I2C restart, which may have
discarded the release, clears Ctrl, and a latched Ctrl expires 600 ms
after the last Ctrl event, because the firmware reports a held key again
every 100 ms once it has been held for 300 ms (`keyboard.ino`,
KEY_STATE_HOLD), so a Ctrl still held is never silent that long. Whether
the controller answers a read issued 2 ms after the register write is a
board check.

TX_ABRT (no acknowledge) and a phase that makes no progress for eight
polls restart the I2C block and count in `picokbd_errors()`; an abort after
the FIFO register write loses that one event, because the controller
dequeues it when the write arrives. A disable that does not complete in
10 ms escalates to IC_ENABLE.ABORT (datasheet 4.3.10.4), written after
ENABLE because the block honors ABORT only while enabled, and then to a
reset of the block. The poll callout runs inside the SysTick exception, so
it takes those steps one per poll rather than waiting; only the polled
exchange of the probe and of the console's getc, which runs with
interrupts masked anyway, waits them out. Reading register 0x08 resets the keyboard
controller, so the driver never reads it.

A register request (battery, backlight, power-off) shares the poller. The
write phase asks the mailbox in `picokbd_req.c` for a waiting request and
sends it in place of register 0x09; a read is the register number, a write
is the number with bit 7 set and one value byte, and the two-byte reply of
either is (register, value), so a write reads back the value the firmware
kept. The reply goes to the mailbox instead of the key map, and a reply
whose first byte is not the requested register counts as a fault. A fault
retries the request up to three exchanges before it fails with `EIO`. The
caller of `picokbd_reg()` sleeps uninterruptibly on the mailbox, and a
one-second callout (`HZ` ticks) ends a request the poller never answers
with `ETIMEDOUT`, so no sysctl waits forever; a reply that arrives after
its deadline is discarded because the mailbox no longer holds a request in
flight. A request waits for the next write phase, which is at most one poll
interval away (16 ms idle), and takes three phases, so a read answers in
about 50 ms.

`machdep.picocalc` (`machine/picocalcvar.h`, served by `picocalc_sysctl()`
in `picokbd.c`, PICOCALC kernel only) exposes the registers. `__sysctl()`
admits a write only from root before any handler runs.

| Name | Register | Read | Write |
| --- | --- | --- | --- |
| `battery` | 0x0B | percent, bits 0-6 | refused (`EPERM`) |
| `charging` | 0x0B | bit 7 | refused (`EPERM`) |
| `lcd_backlight` | 0x05, 0x85 | level | level, clamped to 0-240; the firmware raises it to a multiple of 16, at least 16, so the panel never goes dark |
| `kbd_backlight` | 0x0A, 0x8A | level | level, clamped to 0-240; the firmware rounds down to a multiple of 32 and turns the light off below 32 |
| `poweroff` | 0x8E | the last countdown requested, 0 if none | seconds, clamped to 6-255, after a `sync()`; the firmware then waits that long and cuts power through the AXP2101, and nothing cancels it |

The firmware's `sync_bat` stores 0 when the PMU reports no battery
(read from source, not observed), which `battery` would show as 0 percent
and `charging` as 0, so a USB-C-only run could not be told from an empty
cell by these two nodes alone.

The firmware applies Shift, Caps Lock and the Alt symbol layer and
repeats held keys, but reports Ctrl as key 0xA5 changing state without
applying it, so `picokbd_map.c` applies Ctrl. Enter sends CR and
Backspace sends DEL, matching the tty's ICRNL mapping and `CERASE`.
Arrows send `ESC [ x` or, after an application sets cursor-key mode,
`ESC O x`; Home, End, Insert, Delete, Page Up/Down and F1-F10 send the
xterm termcap entry's `kh`, `@7`, `kI`, `kD`, `kP`, `kN` and `k1`-`k;`.

Raw key events. Keystrokes include passwords, so the driver records
nothing until a program asks. `LCDIOC_KEYEVS` (`_IOW('l', 3, int)`) with 1
starts a session owned by the calling process and the open console file it is
called on, and with 0 the owner ends it; `LCDIOC_GETKEY` (`_IOR('l', 2, struct lcd_keyevs)`) then returns up to six
`{ state, key }` events (`LCD_KEY_PRESSED`, `HOLD`, `RELEASED` and the
controller's key byte) from a queue of 32 that `picokbd_evq.c` keeps. A start
needs a descriptor open for reading (`EBADF` otherwise), a caller whose
process group is the tty's foreground group (`EIO` otherwise, the answer
`ttread()` gives a background reader that ignores `SIGTTIN`; the call never
sends `SIGTTIN` or sleeps), a tty in RAW or CBREAK mode (`EINVAL` otherwise;
cooked input is never recorded) and no other owner (`EBUSY`); a drain needs
the first two properties, returns `EINVAL` without a session and `EPERM` to a
process that is not the owner (a parent shell that shares the file included),
and leaves the queue as it was when it fails. The queue is emptied and its bytes cleared when the
session starts and when it ends, and `picokbd_event()` records only between
the two. A session ends when its owning process exits or execs
(`lcdcons_procexit()`, called from `exit()` before the files close and from
`exec_clear()` after the exec commits, so a new image never inherits it), when
its open file is closed for the last time (`lcdcons_fileclose()`, called from
`vn_closefile()` under `PICOCALC_ENABLED`, because a character driver's close
entry runs only when the whole device is closed and the console stays open),
when the owner ends it, when the tty's foreground group differs from the
owner's, and when the tty's mode word differs from the one it had at the
start. The mode word is `t_flags` without `PENDIN` and `FLUSHO`, the two bits
the line discipline toggles on its own (`ttypend()`, `ttyinput()`); the
rest, one 32-bit word that holds RAW, CBREAK, ECHO, CRMOD and the local mode
bits, all count, because each changes what a keystroke does or shows. The
exit hook is needed on its own: a program that starts a session
on an inherited standard input shares the open file with its parent shell, so
its death releases only one reference and the file close never runs, while
`/bin/sh` keeps its commands in the shell's own foreground group. The owner is
the pair of the process table entry and its pid, so a slot reused by a new pid
and a pid reused in another slot are both strangers. The foreground and mode checks are made before every
event the poller records (`lcdcons_keycheck()`, from `kbd_poll()`) and at the
start of every request, against the live `t_pgrp` and `t_flags`, so no key is
recorded or read after a change; they are comparisons, not hooks in the tty's
setters, because `TIOCSPGRP`, `setpgrp()`, the tty close and every
mode-setting ioctl (`TIOCSETP`, `TIOCSETN`, `TIOCLBIS`, `TIOCLBIC`, `TIOCLSET`,
from any descriptor, `stty` and getpass()'s `/dev/tty` included) change one of
the compared words. The one mode-setting call that changes nothing is
`getpass()` in an owner that already has RAW and no ECHO: it clears ECHO,
which is already clear, so `lcdioctl()` calls `lcd_modeset()` before those five
ioctls and a call from any process other than the owner ends the session
whatever it sets. A password read through `getpass()` therefore never reaches
the queue: the flag change or the foreign ioctl ends the session, and the
read is cooked. The residual is a process that reads the owner's raw mode
without a mode-setting ioctl and without `getpass()`. No shipped program does:
`getpass()` is the only password reader of `bin`, `sbin`, `usr.bin`,
`usr.sbin`, `libexec` and `games` (`login`, `passwd`, `phantasia`, picoc), and
the programs that set RAW or CBREAK themselves (tip, uucp, zmodem, the
editors) do it with `TIOCSETP`. A program the owner runs that reads secrets
in its own raw mode with no ioctl would be heard. The open file is found from the system call's descriptor
(`u.u_arg[0]`, which must carry the flags the driver was given; any other
state fails closed). `kbd_poll()` calls `picokbd_event()` for every
controller event; that function records the event for an active session and
returns `picokbd_translate()`'s result, so the xterm bytes and their Ctrl
tracking are those of the translate-only path and the queue never delays
them. A full queue drops its oldest event and counts it: the call returns the
count since the previous call in `lost` (255 at most), so a game that holds
key state keeps the newest events and learns when to resynchronize. The
ioctls do not block, run at `spltty()` against the poller's callout, and are
classified in `ioctl()` (`LCDIOC_KEYEVS` as a four-byte input, `LCDIOC_GETKEY`
as zeroed output). The polled `picokbd_getc()` of the console's fatal path does not
queue. Cost: 592 bytes of text and 100 of bss in `picokbd_evq.o`, 344 of text
in `lcdcons.o` (Tile atlas, above).

The counts are programmed for 100 kHz, between the vendor helloworld's
10 kHz and its FUZIX port's 400 kHz; datasheet 4.3.14.1 adds one ic_clk
to the low count and SPKLEN + 7 to the high count, so SCL runs near
96 kHz.

## SD card: sd.c and sdspi.c

The socket is SPI0 (GP16 RX, GP18 SCK, GP19 TX) with GP17 as a SIO chip
select and GP22 as card detect. `sdspi.c` speaks the SD card's SPI mode over
a byte transport and includes no kernel header, so `tests/picocalc` links
it against a card model; `sd.c` supplies the transport (the PL022 of
datasheet 4.4) and the disk driver. The specification is the SD Physical
Layer Simplified Specification, Part 1: its SPI Mode chapter and the CSD
register description. The specification is not vendored in this tree, so
the source and this section cite its sections by title.

Initialization (Mode Selection and Initialization; Card Initialization
and Identification Process): 80 clocks with chip select high, CMD0
(CRC 0x95) until R1 reads idle, CMD8 with 0x1AA (CRC 0x87), ACMD41 until the
card leaves idle within one second, CMD58 for the OCR, CMD16 to 512 bytes for
a byte-addressed card, and CMD9 for the capacity. CMD0 and CMD8 are the
only commands that carry a checked CRC in SPI mode (Bus Transfer
Protection); every other
command sends a dummy CRC and CRC checking stays off. CMD8 answered as an
illegal command marks a version 1.x card and the OCR's CCS bit marks
block addressing for a version 2.0 or later card; an echo that does not
return the voltage range and check pattern, an illegal CMD55 or ACMD41 (a
MultiMediaCard), or an OCR that never reports power-up complete ends
initialization with an error, and a card that never leaves idle ends it
after one second. The clock runs at 398 kHz until initialization finishes
(SCR 156 with prescale 2 from the 125 MHz peripheral clock, under the
400 kHz identification limit), then at 12.5 MHz (prescale 10), half the
25 MHz default-speed ceiling.

A transfer is one 512-byte sector per command, CMD17 for a read and CMD24
for a write. A read waits 100 ms for the 0xFE start token and fails on a
data error token; a write checks the data response token and then waits for
the card to stop signaling busy. Both busy waits, the wait for ready before
each command and the initialization loop have a time limit in the model's
and the kernel's microsecond clock, so a stuck card fails the transfer
(`EIO`) instead of hanging the caller. The driver runs a transfer in the
caller's context with interrupts enabled: the kernel does not switch
processes inside a system call and no interrupt touches SPI0, so a transfer
needs no lock. A write that hits the card's worst-case programming time
holds the caller for up to 500 ms; ticks still run.

`sd0` is block major 5 and `rsd0` is raw (character) major 11, minor 0 for
the whole card and 1 to 4 for the MBR partitions a to d, as `fl` numbers
its partitions. The partition table is read at the first open from sector 0
(`fdisk` rewrites it and calls `DIOCREINIT`); an entry that reaches past
the card's end is dropped. `DIOCREINIT` builds the new table apart and
replaces the live one only after sector 0 reads, so a read error returns
`EIO` and leaves the open partitions' mappings as they were. It returns
`EBUSY` while any block open holds a partition (a mount, or a descriptor on
`sd0` to `sd0d`), because the buffers and inodes of that filesystem are
addressed through the entries it would replace; the raw open `fdisk` makes
does not count. It returns `EIO` once the card has been replaced. A transfer is any whole number of 512-byte
sectors, and its start is a 1 KiB block number because `physio()` shifts the
file offset right by `DEV_BSHIFT`, which drops the low ten offset bits. Raw
I/O therefore works at 1 KiB-aligned offsets: `sdread` and `sdwrite` (the
`rsd` entries in `conf.c`, in front of `rawrw()`) refuse with `EINVAL` a
request whose offset is not a multiple of `DEV_BSIZE`, or whose segments
before the last are not whole blocks, because a 512-byte transfer at offset
512 would otherwise address the sector before it. Use `dd bs=1k` or a
multiple (`dd` defaults to 512-byte blocks and fails at its second read);
the one-sector transfers `fdisk` makes at offset 0 reach sector 0. A request
that crosses the partition end moves the sectors that fit and reports the
rest in `b_resid`. `physio()` advances the file offset by the count minus
`b_resid` even when `B_ERROR` is set, so every outcome states the bytes that
did not move: the whole count for a request refused before any card command
(`ENXIO`, `EINVAL`, a replaced card), and for a transfer that stops on a
card error or a replaced card, the failed sector and every sector after it
plus any partition-end truncation. A retry then starts at the first sector
that did not transfer. Block and raw opens
are tracked apart so that closing `rsd0a` leaves a mounted `sd0a` open. `umount` and a
failed `mount` close the partition only when no block descriptor names it, the last-close
rule of `closei()`, because the open state is a bit and one close drops it: a descriptor
opened before the mount keeps reading after the unmount and closes the partition itself.
`chrtoblk()` maps `rsd0` to `sd0`, and `blkoverlap()` in `conf.c` compares the
sector extents that `DIOCGETPART` reports for two nodes of one unit. At
`kern.securelevel` 1, which `multiple()` in `sbin/init/init.c` sets when it
enters multiuser mode from level 0, `ufs_mountedon()` therefore refuses with `EBUSY` a write open of `rsd0` or
`sd0`, the whole card, while any partition is mounted, a write open of an MBR
entry whose extent overlaps a mounted one, and every open of a mounted block node;
a partition that only touches the mounted one opens. Every other pair of block
devices keeps the exact comparison of device numbers. The same rule covers the
flash root: `fl0`, the whole filesystem region, cannot be opened while `fl0a` is
mounted. `isdisk(dev, IFCHR)` classifies major 11 as a disk, so
securelevel 2 refuses every write open of `rsd0`. The card is
initialized at the first open and forgotten at the last close, which is how
a replaced card gets a fresh initialization.

A card pulled under a read-write mount leaves the superblock and inodes dirty,
and `ufs_sync()` cannot write them to a card whose transfers fail with `EIO`,
so `umount` would fail on every attempt and `sdopen` would answer `EIO` until a
reboot (a read-only mount recovers). `umount` therefore asks the driver after a
failed write-back: `DIOCMEDIAGONE` (`sys/sys/disk.h`) stores the replaced flag the
card-detect callout keeps, and when it is set `umount` releases the mount without
write-back, clears the latched `m_write_error`, invalidates the device's buffers
(`ufs_sync()` has already dropped the delayed writes it failed to deliver, so none
can reach the next card) and closes the partition at the last close, which lets
the next open initialize the replacement card. A write error with the card still
present, a driver that does not answer the command (`fl` returns `EINVAL`) and a
file still open on the mount (`EBUSY`) keep `umount` failing as before. Data written
since the last successful write-back is lost with the card.

The keyboard poll callout samples GP22 (`sd_detect_poll()`). The driver keeps
the level the open card was accepted at (the committed level) and the level
last seen (the candidate). A candidate that differs from the committed level
and holds 50 ms counts as a change; a level that returns to the committed
level sooner, or flickers (each change restarts the interval), changes
nothing. A change while any partition is open makes every transfer fail with
`EIO` until the last close, because buffers cached for the old card must not
reach the new one. `sdstrategy()` reads that flag before every sector
command, so a change that lands between two sectors of a request stops it
there. Accepting an initialized card at the first open sets both levels to
the pin's present level and clears the flag in one `splbio()` section, so
the debounce of that card's own insertion does not expire against it. The
driver does not interpret the polarity; the first insertion on a board
settles which level means present.

`NMOUNT` is 2 and `SINGLE_UFS_ROOT` is unset in PICOCALC and PICOCALC_PSRAM
(the two Configs differ in the board line and `device psram` alone), because
`sys/mount.h` requires `NMOUNT=1` with that option. Without it the kernel keeps
`i_dev` and `i_fs` in each of the 24 inodes and the mount names, and the
generic mount code replaces the single-root shortcuts; that, with the
`conf.c` entries and the detect hook, is the roughly 1,001 of the 3,888 added
text bytes that the two SD objects (2,887) do not account for; the figure is
the difference of object sizes, not a separate build. Userland is
still built with the PICO kernels' layout (`KERNEL_LAYOUT_CFLAGS` in
`share/mk/sys.mk`), which is harmless for the shipped programs: `ps` reads
the process table, `df` and `mount` use `statfs` and `mount(2)`, and no
shipped program reads the in-core inode or mount tables from `/dev/kmem`
(checked in the sources of `fsck`, `mount`, `umount`, `df` and `ps`). A tool
that did would misread them on PICOCALC.

The picocalc profile carries the nodes in the `sdcard` closure
(`distrib/rp2040/mi.rp2040`): `/dev/sd0`, `sd0a`, `sd0b`, `rsd0`, `rsd0a`,
`rsd0b` and the mount point `/mnt`. The root has 128 inodes, so the other
two partitions' nodes are left to `mknod /dev/sd0c b 5 3` and the like. The
PICO board's `full` profile has none of them, and its kernel has no driver
behind those majors.

### Putting a filesystem on a card

The tree ships `sbin/mkfs` and `sbin/fdisk` as source but neither is in any
root image, and adding them would spend flash blocks and inodes on tools
no board run has exercised against the driver. The documented route is a
host-side image with `tools/bin/rp2040/fsutil` (built with the tools),
written to the card from the host. The partition layout is fsutil's: the
table in sector 0, the first partition at sector 2.

    fsutil --repartition=fs=64m card.img
    fsutil --new --partition=1 card.img
    dd if=card.img of=/dev/sdX bs=1M conv=fsync

`/dev/sdX` is the card in a host reader. `fsutil --new --partition=1 card.img DIR`
copies a directory tree in and `fsutil --check --partition=1 card.img`
verifies it. Then, on the board:

    mount /dev/sd0a /mnt

and `umount /mnt` before removing the card. FAT is out of scope. An
on-board `fdisk /dev/rsd0` and `mkfs -s BLOCKS /dev/rsd0a` are the second
route; they use only the disk ioctls the driver answers
(`DIOCGETMEDIASIZE`, `DIOCGETPART`, `DIOCREINIT`; `umount` adds
`DIOCMEDIAGONE`) but have not run against it, so a closure that ships them waits on a board run.

### Crash dumps

A PICOCALC kernel on a PicoCalc takes a crash dump on the SD card. The
probe sets `dumpdev` to the card (`sdprobe()`), and `boot()` calls the dump
routine for a panic, which asks for `RB_HALT | RB_DUMP`, and for
`reboot -d`, before it halts or resets. `sddump()` initializes whatever
card is in the socket on a card state of its own, reads the partition
entries of sector 0, and writes to the first entry of type 0xb9 that lies
inside the card, unless a block open of the accepted card (a mount, or the
whole card) overlaps it. It polls the card with every interrupt masked
and allocates nothing. The format is `sys/arch/rp2040/include/dump.h`: a
header and segment table in sector 0, then the panic message, kernel RAM
from `.data` to the end of the SwapRAM pool, and the two u areas, each
with a CRC-32 computed over the bytes as they leave for the card, which
keeps the current u area, where the writer's own stack runs, consistent.
Sector 0 is zeroed first and written last, so an unfinished dump reads as
none. A PICOCALC dump is 109 sectors (55,808 bytes), 48,136 of them kernel
RAM; a PICOCALC_PSRAM dump is 110.

    fsutil --repartition=fs=64m:dump=1m card.img
    fsutil --new --partition=1 card.img

makes a card with a file system on `sd0a` and a dump partition on `sd0b`.
After the reboot that follows a panic, `savecore` (in the `sdcard`
closure) copies the dump off the card:

    mount /dev/sd0a /mnt
    mkdir /mnt/crash
    savecore /mnt/crash

`savecore(8)` checks every checksum while it copies, writes `dump.N` and
`info.N`, whose image CRC32 names the `unix.elf` build whose symbols decode
the dump, and zeroes sector 0 only after both files, the bounds file and
the directory are on stable storage. It refuses to copy unless 1024 KB (or
the kilobytes in the directory's `minfree` file) stay free afterward. At
securelevel 2 every write open of the card is refused, so `savecore` runs
before the level rises.

## Building and flashing

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON
    bmake MACHINE=rp2040 kernel        # PICO, PICO_UART, PICOCALC, PICOCALC_PSRAM

`sys/arch/rp2040/compile/PICOCALC/unix.uf2` holds the kernel; the root
image is any board's `flash*.uf2`, since every kernel boots every root.
`BOARD=PICOCALC` builds the `picocalc` profile root as
`distrib/rp2040/flash-PICOCALC.uf2`, and `BOARD=PICOCALC_PSRAM` builds
`distrib/rp2040/flash-PICOCALC_PSRAM.uf2`; `distrib/rp2040/flash.uf2` is the
PICO board's.
`sys/arch/rp2040/compile/PICOCALC_PSRAM/unix.uf2` holds the same kernel with
the PSRAM driver. A board is selected by name: `BOARD=PICOCALC` pairs the
driverless kernel with the `picocalc` root profile, and `BOARD=PICOCALC_PSRAM`
pairs the PSRAM kernel with the same profile.

    bmake MACHINE=rp2040 BOARD=PICOCALC distribution        # no PSRAM driver
    bmake MACHINE=rp2040 BOARD=PICOCALC_PSRAM distribution  # PSRAM swap

Each board writes its own `sdcard-${BOARD}.img`, `flash-${BOARD}.bin` and
`flash-${BOARD}.uf2` under `distrib/rp2040` (the bare `sdcard.img`,
`flash.bin` and `flash.uf2` belong to `BOARD=PICO`), so the three boards'
images coexist in one tree.

Each kernel boots either root. The PSRAM kernel is a separate configuration
because config(8) writes a Makefile from one Config with no include
directive: a make-level switch would need a second generated Makefile or a
regeneration per build. `tools/config/check_generated_sync.py` requires the
two Config files to differ in the board line and the `device psram` line
alone.
Flashing follows `distrib/rp2040/host/DEVELOPMENT.md` and needs the
board in BOOTSEL mode; a board run is an explicit opt-in.

## Next work, with designs and blockers

Each item names what it needs, the design, and the check that would
establish it. Board items share one blocker: a PicoCalc on the bench and
the owner's flash opt-in.

### Board bring-up of the console

Flash PICOCALC with a root image and record, in order: panel shows the
boot banner (SPI, reset and init sequence); text scrolls past line 32
without redraw tearing (VSCRDEF/VSCSAD); `kbd0:` prints a firmware
version (I2C timing, address); typing at the login prompt echoes
(poll, keymap, tty path); `vi` and `more` draw correctly at 53x32
(escape coverage). A blank panel with a version line implicates SPI or
init; a version line of "not answering" implicates I2C speed or pull-ups.
Then measure: characters per second for `cat` of a large file, and
cell-draw time by toggling a spare GPIO, to decide on RGB565. After a
workload of `vi` region scrolling, held-key typing and a deliberate
`mputest` fault, read `sysctl kern.capacity` (`stack_minimum_free` for
the u-area stack) and halt to read the render stack's unused depth; a
u-area figure below the 220 bytes `CAPACITY.md` records for PICO, or a
render-stack figure near zero, means the stack budget needs work before
the console ships.

### SD card on SPI0

The driver ships (see SD card above). The board checks that remain: the
boot line `sd0: SPI0 on GP16-GP19, card detect GP22`; a first open with an
SDSC, an SDHC and an SDXC card (the type and size line); a partition
table from `fsutil` and then `mount /dev/sd0a /mnt` with files copied both
ways and `fsck` clean afterward; the level of GP22 with and without a card;
a card pulled while mounted read-write, which must fail transfers with `EIO`,
let `umount` succeed without write-back and mount again after reinsertion
(`tests/rp2040/sd_detach/board_sd_detach.py`, closed unless
`DISCOBSD_BOARD_SD_DETACH` is exactly `run`); and the largest raw read rate at 12.5 MHz,
which decides whether the clock can rise toward 25 MHz. Not built: a
multi-block transfer (CMD18 and CMD25), which would cut the per-sector
command overhead, and an on-board `mkfs`/`fdisk` closure.

### PSRAM as swap

The PICOCALC_PSRAM kernel carries the driver; PICOCALC does not. The
ESP-PSRAM64H (8 MB) on GP2-GP5, GP20 and GP21 is not a hardware SPI pin
set, so PIO0 state machine 0 clocks it; `docs/research/picocalc-psram.md`
holds the datasheet pages, the program, the timing derivation and the
evidence. `psramprobe()` runs from `config()`, before `init_main()` opens
`swapdev`: it resets the part, reads its ID (manufacturer, known-good die
and the 48-bit EID, printed as `psram0: read id 0d/5d eid` and twelve hex
digits), requires manufacturer 0x0D and a passing known-good die, enters
quad mode and writes and reads back a pattern. A part that passes becomes
block device major 3 and replaces `fl1` as `swapdev`, so swap is 8 MB
behind the SWAPRAM tier and the 384 KB flash swap region goes unused; a
part that does not pass prints a second `psram0:` line and leaves
`swapdev` on `fl1`. The EID tells the ESP-PSRAM64H, LY68L6400 and APS6404L
apart only against reads of reference parts: the APS6404L sheet alone
defines a field, EID[47:45] = 010 for 64 Mbit. SCK runs at 31.25 MHz
(`PSRAM_CLKDIV` in `psram_pio.h`), a frame holds CS low for under 5 us
against the 8 us tCEM, and interrupts are masked for that long per frame.

Open for the board run: the read sampling margin depends on pad delays no
datasheet gives and on whether the output and input register stages run on
clk_sys or on the state machine cycle (a scope capture of SCK against a read
data line decides both, and whether the clock can rise;
`docs/research/picocalc-psram-capture.md` orders the instruments), whether
the part ignores a serial command cut
short, and how a module in the shared header J703 loads the lines. The owner
chose replacing `fl1` over a second tier behind PSRAM, and chose the driver
as a PicoCalc build option; the research note's swap integration table
records the second-tier alternative.

### Battery, backlight and power-off

The sysctls ship (see Keyboard above). The board checks that remain:
`sysctl machdep.picocalc.battery` against the PMU's own reading, a
`lcd_backlight` write that dims the panel in 16 steps, a `kbd_backlight`
write that lights the keys, `poweroff=6` that cuts power six seconds
later, and keys typed while a `sysctl` loop runs, which settles that a
request never costs a key event. The firmware's `check_pmu_int` sleeps for the whole countdown before it
shuts the PMU down, so keys typed in those seconds may be lost; the board
run settles whether they are.

### Audio

BEL ships (see Console above): `lcd_bell()` in `picoaudio.c` starts a
1 kHz square wave at 25 percent duty on both channels of PWM slice 5 and a
callout stops it 100 ms later; a BEL that arrives while the tone
sounds is absorbed, and a panic suppresses it because callouts no longer
run. The 25 percent duty and the 100 ms length are unmeasured choices:
the board run sets the loudness and checks that the amplifier is silent
between tones. A `/dev/audio` with a PCM ring is the larger piece that
remains; it needs a DMA or interrupt-fed sample path on the same slice and
a decision on sample rate and format.

### Calculator application and tile graphics

`docs/research/picocalc-calculator-and-gui.md` surveys the calculators in
this tree and in the other BSDs, the PicoMite firmware's math functions,
and frame-buffer-less tile and sprite techniques, and orders an import
plan with licenses and size estimates. The kernel side is `/dev/lcdgfx`
(Pixel device, above), a pixel ioctl set modeled on RetroBSD's `gpanel`
interface on a second minor of the console's major, with a 128-tile atlas
(Tile atlas, above) and the library `games/gametile.h`; the text console
restores itself from its cell store when the graphics client closes.

The first item ships OpenBSD `bc` and `dc` (ISC, Otto Moerbeek; the manual
pages and `bc.library` under the Caldera license, `NOTICE` sections 16 and 3)
as the `calc-bignum` closure group (`bc` and `dc`) of the `picocalc` profile. They link
`lib/libbn`, the tree's clean-room `BN_*` subset, in place of libcrypto; dc
allocates its register stacks in 256-entry pages on first touch, because the
extended table is 789,504 bytes against the 192 KB process window, and bc
keeps a name table in place of a 65,536-entry hash table and reads its input
with `getc` in place of libedit. `bc` is 31,176 bytes packed, `dc` 18,644 and
`/usr/share/misc/bc.library` 5,125; the closure costs 60 blocks and the
`picocalc` profile leaves 147 free (`PROFILES.md`). `bc` forks a child that
runs dc's evaluator on a pipe, which needs the 39 KB bc image twice in swap:
the arithmetic fits the 384 KB raw swap, and no board run has forked it.
`tests/dcbc` (`check-dcbc`, and `check-qemu` for dc) is the host evidence,
`tests/rp2040/dcbc_smoke/board_dcbc_smoke.py` the closed board script, and
`docs/research/openbsd-dc-bn-port.md` the port record.
`docs/research/c17-shipped-resource-baseline.json` records the `full`
profile image (`distrib/rp2040/sdcard.img`) and the `PICO` and `PICO_UART`
kernels, as the C17 collector measures them from a clean distribution run
(`docs/research/c17-shipped-build-baseline.md`). `bc`, `dc`, `bc.library`,
`calc` and the SD device nodes belong to the `picocalc` profile, so none of
them appears in that record, because the collector selects neither that
profile nor the per-board `sdcard-BOARD.img` images. The record's kernel
sizes come from the local GCC 16.2.0 and carry no flash-budget authority;
`check-kernel-headroom` owns that figure.
