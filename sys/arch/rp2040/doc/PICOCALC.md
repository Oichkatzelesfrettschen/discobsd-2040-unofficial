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
| PICOCALC compiles and links with `-Wall -Wextra -Werror` and carries no PSRAM driver | build | observed: text 119,316 of 130,816 bytes (`RP2040.ld` FLASH), data 304, bss 45,888, equal to the kernel before the driver existed |
| PICOCALC_PSRAM compiles and links with `-Wall -Wextra -Werror` | build | observed: text 121,432 of 130,816 bytes, data 360, bss 46,032; the driver accounts for 2,096, 56 and 144 of them and the longer configuration name in `version` and `osversion` for 20 more text bytes |
| `lcdterm.c`, `lcdfont.c`, `picokbd_map.c`, `st7365p.c` compile as strict ISO C17 (`-std=c17 -pedantic-errors`) | build | observed, arm-none-eabi-gcc and host gcc/clang |
| `picokbd.c`, `lcdcons.c` compile as strict ISO C17 | build | fails in `sys/exec.h:51` (an empty struct, GNU C), reached through `sys/user.h`; they build under the kernel's GNU17 profile like every kernel file |
| The terminal draws what its cell store holds, scrolls, wraps and parses the xterm-basic sequences | host | observed by `check-picocalc-host` against a model panel |
| The panel initializes, shows text and scrolls | board | not run: no PicoCalc has booted this kernel |
| The keyboard controller answers at 0x1F and its events type | board | not run |
| The board runs from USB-C with no cells | board | not run; the AXP2101 power path supports it (below) |
| `psram.c`, `psram_xfer.c` and the PIO program in `psram_pio.h` execute frames against a timing model of the ESP-PSRAM64H: waveforms, tCEM, tCSP, tCHD, tCPH, clock period, read sampling margin, 40 random transfers, four injected faults, the four placements of the two register stages and nine mutants | host | observed by `check-psram-pio` (1,141 checks); the model's pad delays and the clock of the two register stages are assumptions |
| PICO, PICO_UART and PICOCALC link unchanged by the PSRAM driver | build | observed: `size` equal to the kernels built before the driver existed; PICO and PICO_UART disassembly and read-only data equal except the version timestamp |
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
| SD card, SPI0 | GP16 RX, GP17 CS, GP18 SCK, GP19 TX, GP22 card detect | none yet |
| Audio, PWM | GP26 left, GP27 right | none yet |

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
A board booting with no 18650 cells installed settles the claim.

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
The DEC line-drawing set maps to `+`, `-` and `|`.

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

## Building and flashing

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON
    bmake MACHINE=rp2040 kernel        # PICO, PICO_UART, PICOCALC, PICOCALC_PSRAM

`sys/arch/rp2040/compile/PICOCALC/unix.uf2` holds the kernel; the root
image is the same `distrib/rp2040/flash.uf2` the PICO board uses.
`sys/arch/rp2040/compile/PICOCALC_PSRAM/unix.uf2` holds the same kernel with
the PSRAM driver. A board is selected by name: `BOARD=PICOCALC` pairs the
driverless kernel with the `picocalc` root profile, and `BOARD=PICOCALC_PSRAM`
pairs the PSRAM kernel with the same profile.

    bmake MACHINE=rp2040 BOARD=PICOCALC distribution        # no PSRAM driver
    bmake MACHINE=rp2040 BOARD=PICOCALC_PSRAM distribution  # PSRAM swap

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

Pins GP16-GP19, card detect GP22. Model on `sys/arch/stm32/dev/sd.c` and
`spi.c` (SPI-mode SD): CMD0/CMD8/ACMD41/CMD58 bring-up at 400 kHz, then
12.5 MHz or higher; single-block CMD17/CMD24 through a `bdevsw` entry as
`sd0` with an MBR partition table, `isdisk()` and `NMOUNT` raised to
mount it beside the flash root. Card detect polls in the keyboard
callout. Host-testable piece: the command and response parsing against a
recorded card transcript. Cost estimate: 3-4 KB text, 600 bytes bss.

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

Keyboard-controller registers: 0x0B battery percent (bit 7 charging),
0x05/0x85 LCD backlight, 0x0A/0x8A keyboard backlight, 0x8E power-off
countdown (clamped to at least 6). Expose them as `machdep.picocalc.*`
sysctls; the exchange must go through the same split-phase poller so it
never collides with key polling (queue a register request that the next
write phase sends instead of 0x09).

### Audio

PWM slices 5A (GP26) and 5B (GP27). A bell for BEL (0x07) is the
smallest useful piece; a `/dev/audio` with a PCM ring is a larger one.

### Calculator application and tile graphics

`docs/research/picocalc-calculator-and-gui.md` surveys the calculators in
this tree and in the other BSDs, the PicoMite firmware's math functions,
and frame-buffer-less tile and sprite techniques, and orders an import
plan with licenses and size estimates. The kernel side is a graphics
ioctl set on the console device, modeled on RetroBSD's `gpanel`
interface, plus a tile-blit operation that draws from a tile table the
process passes; the text console restores itself from its cell store
when a graphics client closes.

The first item ships the tree's Plan 9 `bc` and `dc` (MIT license, already
built for rp2040, in no profile) as a manifest closure. It changes the
full manifest, which `docs/research/c17-shipped-resource-baseline.json`
pins by hash and by packed program, so it lands with a re-recorded
baseline from a distribution run and a `NOTICE` section for the Plan 9
Foundation license.

### Strict C17 for the remaining console files

`sys/exec.h:51` declares an empty `struct {} aout;` inside a union, a GNU
extension; nothing references the member. Removing it lets `picokbd.c`
and `lcdcons.c`, and every other file that includes `sys/user.h`,
compile with `-std=c17 -pedantic-errors`. It touches a header every
kernel object includes, so it lands as its own change with the
`check-c17-compile-profiles` gate moved to strict C17 for those files.
