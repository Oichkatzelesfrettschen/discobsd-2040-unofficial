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
| PICOCALC compiles and links with `-Wall -Wextra -Werror` and carries no PSRAM driver | build | observed: text 123,332 of 130,816 bytes (`RP2040.ld` FLASH; 7,484 bytes of headroom), data 368, bss 47,600; no `psram` symbol in `unix.elf`; the console alone measured 118,268 text, 304 data and 45,888 bss, so the sysctls (+916), the bell (+260) and the SD card with its multi-mount kernel (+3,888) add 5,064 text, and 118,268 + 5,064 equals the merged build |
| PICOCALC_PSRAM compiles and links with `-Wall -Wextra -Werror` and carries the SD driver of PICOCALC | build | observed: text 125,448 of 130,816 bytes (5,368 bytes of headroom), data 424, bss 47,752; it exceeds PICOCALC by 2,116 text, 56 data and 152 bss bytes, where `psram.o` and `psram_xfer.o` hold 2,068, 8 and 146 and the swap selection, the block-device entry and the longer configuration name in `version` and `osversion` hold the rest |
| `lcdterm.c`, `lcdfont.c`, `picokbd_map.c`, `st7365p.c` compile as strict ISO C17 (`-std=c17 -pedantic-errors`) | build | observed, arm-none-eabi-gcc and host gcc/clang |
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
Two explanations fit, each with the measurement that separates them:

- The USB-C receptacle lacks a 5.1 kOhm pull-down on each CC pin, so a
  USB-C-to-C supply never switches VBUS on. A USB-A-to-C cable, which
  carries VBUS unconditionally, powering a cell-less board confirms it.
- The AXP2101 needs a power-key press or a battery to start, through
  power-on defaults the board does not change. A cell-less board that
  stays off on an A-to-C cable and starts after a press of the power key
  confirms it.

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
The DEC line-drawing set maps to `+`, `-` and `|`. BEL calls `lcd_bell()`
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
sector extents that `DIOCGETPART` reports for two nodes of one unit. Once an
operator raises `kern.securelevel` to 1 (the default is 0 and `init` never raises
it), `ufs_mountedon()` therefore refuses with `EBUSY` a write open of `rsd0` or
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
`swapdev`: it resets the part, reads its ID, requires manufacturer 0x0D and
a passing known-good die, enters quad mode and writes and reads back a
pattern. A part that passes becomes block device major 3 and replaces `fl1`
as `swapdev`, so swap is 8 MB behind the SWAPRAM tier and the 384 KB flash
swap region goes unused; a part that does not pass prints one `psram0:` line
and leaves `swapdev` on `fl1`. SCK runs at 31.25 MHz (`PSRAM_CLKDIV` in
`psram_pio.h`), a frame holds CS low for under 5 us against the 8 us tCEM,
and interrupts are masked for that long per frame.

Open for the board run: the read sampling margin depends on pad delays no
datasheet gives and on whether the output and input register stages run on
clk_sys or on the state machine cycle (a scope capture of SCK against a read
data line decides both, and whether the clock can rise), whether the part ignores a serial command cut
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
plan with licenses and size estimates. The kernel side is a graphics
ioctl set on the console device, modeled on RetroBSD's `gpanel`
interface, plus a tile-blit operation that draws from a tile table the
process passes; the text console restores itself from its cell store
when a graphics client closes.

The first item ships OpenBSD `bc` and `dc` (ISC, Otto Moerbeek; the manual
pages and `bc.library` under the Caldera license, `NOTICE` sections 16 and 3)
as the `calc-bignum` closure group (`bc` and `dc`) of the `picocalc` profile. They link
`lib/libbn`, the tree's clean-room `BN_*` subset, in place of libcrypto; dc
allocates its register stacks in 256-entry pages on first touch, because the
extended table is 789,504 bytes against the 144 KB process window, and bc
keeps a name table in place of a 65,536-entry hash table and reads its input
with `getc` in place of libedit. `bc` is 31,176 bytes packed, `dc` 18,644 and
`/usr/share/misc/bc.library` 5,125; the closure costs 60 blocks and the
`picocalc` profile leaves 147 free (`PROFILES.md`). `bc` forks a child that
runs dc's evaluator on a pipe, which needs the 39 KB bc image twice in swap:
the arithmetic fits the 384 KB raw swap, and no board run has forked it.
`tests/dcbc` (`check-dcbc`, and `check-qemu` for dc) is the host evidence,
`tests/rp2040/dcbc_smoke/board_dcbc_smoke.py` the closed board script, and
`docs/research/openbsd-dc-bn-port.md` the port record.
`docs/research/c17-shipped-resource-baseline.json` records the manifest of
commit `e7040aa1`, from before the `calc-bignum`, `calc`, `puzzles` and
`sdcard` closures existed, so none of `bc`, `dc`, `bc.library`, `calc`,
`puzzlebox` or the SD device nodes appears in its records; re-recording it
from a distribution run is open work
(`docs/research/picocalc-session-handoff.md`).
