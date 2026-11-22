# Panel output for i86: the D198 and D202 terms, what survives, and the staged design

Status: decision record, 2026-11-21. The owner chose core-0 SPI1 DMA
(D198), then core-1 scan-out (D202) on a comparison whose figure was wrong,
and then the staged design below (D213), with a read-back-verified sweep
of the wire rate in the next board session (D214). This note splits both options into the
terms of the cost they attack, refutes the part of D198 that the tree
cannot carry, prices the part of D202 that it carries only by pinning
i86 in core, and keeps what survives as a staged design in which every
stage has its own measurement and falsifier. Nothing here is built. Every
figure is labeled measured (board), computed or unmeasured.

Provenance: tree e45e5c3c6 (main); PICOCALC_SD linked from it with
`arm-none-eabi-gcc` 16.2.0 at `-Os`; cycle counts read from
`arm-none-eabi-objdump -d` of that `unix` against the Cortex-M0+
instruction timings (one cycle for an ALU operation, two for a load,
store or taken branch, three for `BL`, one plus the register count for
`PUSH`, three plus it for `POP` with the PC).

## The cost of one panel line, term by term

A frame costs core 0 the sum of four terms, and every lever attacks one:

| Term | What sets it | Lever |
| --- | --- | --- |
| Bytes sent | the dirty lines i86 finds and 2 bytes a pixel (COLMOD 55h) | dirty marking granularity |
| Wire time | bytes x 8 / bit rate | the PL022 divider |
| Core-0 work per pixel | the code that turns an index into wire bytes | the expansion loop |
| Core-0 waiting | the wire time the work does not cover, spent polling `SSPSR.TNF` | DMA, or another core |

A fifth property sets whether any freed time reaches the guest: the
writer. `graphic_refresh()` (`usr.bin/i86/gfx.c`) sends every dirty line
of a frame in one call, and `refresh()` runs it on each 55 ms tick
(`pc.c`). A blocking `write(2)` returns to i86 only after its bytes have
left, so i86 regains emulation time from an offload only if it writes
without blocking and resumes the frame on a later poll.

### What i86 sends

`i86 -g` draws through `/dev/lcdgfx` (`sys/arch/rp2040/dev/lcdgfx.c`) with
the panel in RGB565 (`LCDG_COLMOD` 55h, `gfx_open()`): a run of dirty
graphics lines is one `LCDG_WINDOW` of `LCDG_WIN_INDEX` and one
`write(2)` of 320 palette indexes a line; text cells go as `LCDG_WIN_BITS`
windows. Display memory is watched a page at a time (`mem.h`), so a
single guest byte marks every line of its page dirty.

- Computed: at 12.5 MHz a 320-pixel RGB565 line is 640 bytes, 409.6 us
  on the wire; i86's 320 by 200 frame is 128,000 bytes, 81.9 ms, longer
  than the 54.9 ms timer period.
- Measured (`PICOCALC.md`, kernel 4399): a 320 by 320 fill takes 131 ms
  under 55h against 131,079 us of bit time, so the fill loop keeps the
  FIFO fed.

### The work per pixel today (computed)

The `LCDG_WIN_INDEX` path in the linked kernel runs, for each index:
the format dispatch, palette load and call in `lcdgfx_write()` (25
cycles), `put_color()` with its COLMOD test and two calls (27),
`lcd_gfx_byte()` twice (11 each) and `spi_put()` twice (13 each plus
two peripheral accesses). That is 100 + 4w cycles a pixel, where w is the
wait each access to the SPI1 registers adds on the APB bridge
(unmeasured), plus the 64-byte `uiomove()` chunks. The wire allows 160
cycles a pixel at 12.5 MHz (409.6 us x 125 MHz / 320). Core 0 therefore
works 63 to 70 percent of a line for w from 0 to 3 and polls for the rest.

For a full i86 frame of 64,000 pixels: 51 to 57 ms of work and 25 to 31 ms
of polling inside the 81.9 ms wire time. Four of the five calls per pixel
are call overhead; the expansion itself (load the index, load the palette
entry, store two bytes) is about a tenth of the path.

- Computed alternative: a loop that sends 16-bit SPI frames (`SSPCR0`
  DSS 15) and expands inline costs 15 + 2w cycles a pixel polled, and about
  12 cycles a pixel into a memory buffer, 9 when unrolled.
- The earlier figure of "about 45 ms recovered per full screen" came from a
  `lcdexttest` write that skipped the palette and timed its own pattern
  generator; it measures neither term and stays withdrawn.

## The two options as worded, and what each wording costs

**D198, core-0 SPI1 DMA.** Worded as `write()` returning while the
transfer runs, from a bounce buffer or by holding the caller's buffer.

- Refuted: holding the buffer. i86 writes palette indexes, which DMA cannot
  expand; the one 192 KB user window is exchanged between processes, so a
  held buffer needs the swapper's `SLOCK`; and a DMA master reads around the
  MPU (`MPU.md`), so a user address handed to it escapes the fence.
- Survives: a bounded wire-format buffer in kernel RAM drained by one DMA
  channel, which frees the polling term.
- Its gain reaches i86 only with a non-blocking writer (above).

**D202, core-1 scan-out.** Worded as core 1 reading i86's guest frame buffer,
expanding with its interpolators and feeding SPI1 by DMA.

- Costed, not refuted: reading the guest frame buffer in place. Core 1 runs
  privileged with the MPU unprogrammed (`core1.c`), a ring entry's
  `ce_arg` can carry an SRAM address, and the swapper leaves a process
  with `SLOCK` set in core (`vm_sched.c`). Three costs come with it. i86
  stays pinned for the whole session, and with one user window every other
  process stays swapped out until the pin is released (inferred from the
  swapper's single in-core slot). Core 1 must stop reading before the pin is
  released on every exit path: `exit`, a signal, a fault, and the
  Control-] menu giving the panel back. And i86 must publish its frame
  layout, which is two fields of packed CGA pixels with the odd lines in
  frames lent by the pager, together with the mode, the palette and the
  dirty lines.
- The sibling emulators that scan out from core 1 (`picocalc_x86`
  `second_core()`, `pico-286`) run one program in one address space,
  where none of these three costs exists.
- Survives either way: moving the expansion off core 0. With the copied
  queue of D198, D202's increment is the expansion term alone: about 12
  cycles a pixel with the inline loop, 6 ms a full frame, 7 percent of the
  wire time. With the pinned in-place read, the increment also covers the
  copy-in and the system calls, which leaves core 0 close to nothing a
  frame. Both forms carry core-1 `.ramfunc` code and a doorbell, since
  core 1 wakes from `WFI` on a 10 ms SysTick. Both also need fault parking
  and the CORE1 option on a PicoCalc kernel, which those kernels omit today.
- Falsifier for leaving D202 out of the first stages: board measurement 2
  showing the inline expansion above 30 percent of the wire time, or
  measurement 1 showing copy-in and system calls above 10 percent of a
  redraw-heavy session.

## The lever neither option named: the wire rate

The PL022 bit clock is 125 MHz / (2 x (1 + SCR)): 62.5, 31.25, 20.83,
15.63 or 12.5 MHz for SCR 0 to 4. `st7365p.c` uses SCR 4, because the
ST7365P's serial write cycle is at least 66 ns (page 54), a 15.15 MHz
ceiling. Other software on this board runs the same glass faster:

| Software | Bit rate | Source |
| --- | --- | --- |
| Clockwork sample code | 25 MHz asked, 20.83 MHz from the divider | `PicoCalc/Code/picocalc_helloworld/lcdspi/lcdspi.h` (`LCD_SPI_SPEED`) |
| PicoMite | 25 MHz asked, 20.83 MHz | `PicoCalc/Code/pico_multi_booter/picomite/SPI-LCD.h` |
| `picocalc_x86` | 62.5 MHz: a PIO program of two instructions a bit, divider 2.0, 250 MHz system clock | `picocalc_display/picocalc_display.pio`, `CMakeLists.txt` (`PICO_SYS_CLOCK_MHZ=250`) |

These are field use, a separate evidence class from the specification: they
show the glass accepting writes above the specified cycle on the units their
authors used, and leave the margin across units, voltage and
temperature unknown. The read-back below turns that into a measurement on this
board.

Frame time for i86's 128,000 bytes, by rate (computed):

| SCR | Bit rate | Wire time | With today's loop (100-112 cycles a pixel) | With the inline loop (15-21) |
| --- | --- | --- | --- | --- |
| 4 | 12.5 MHz | 81.9 ms | 81.9 ms, wire-bound | 81.9 ms |
| 3 | 15.63 MHz | 65.5 ms | 65.5 ms | 65.5 ms |
| 2 | 20.83 MHz | 49.2 ms | 51-57 ms, work-bound | 49.2 ms |
| 1 | 31.25 MHz | 32.8 ms | 51-57 ms | 32.8 ms |
| 0 | 62.5 MHz | 16.4 ms | 51-57 ms | 16.4 ms (8-11 ms of work) |

Two consequences follow. Above 20.83 MHz today's loop, not the wire,
sets the frame time, so the inline loop is the precondition for any faster
rate. And at 31.25 MHz with the inline loop the wire and the kernel's
expansion of a full frame take 32.8 ms, inside one 54.9 ms timer period.
The rest of a refresh is unmeasured: `line_expand()` on each of 200 lines,
200 `write(2)` calls with their `uiomove()` copies, and the window
`ioctl`. Whether a whole refresh fits one period, and the bunched
interrupts end, is measurement 1's question.

The same table gives a cheap split of measurement 2: time a full-width
`LCDG_WIN_INDEX` line at SCR 4 and at SCR 1. Today's loop is work-bound
at SCR 1, so the second time is core 0's work per line, and the first time
less the second is the polling.

## Reading back what the glass shows

What is wired (`display-memory-path.md` section 5; `st7365p-register-reference.md`):
the panel's SDO reaches GP12 as SPI1 RX, so the controller's memory and
registers read back. TE lacks a net label at J401 and was not traced; the
backlight is driven by the keyboard controller (PA8, reachable from the
RP2040 only through its register 05h).

- Measured: the memory read (2Eh) reproduced 153,600 of 153,600 written
  pixels under 66h (kernel 4399) and under 55h (kernel 4401), at 3.125 MHz.
- The read returns the controller's frame memory, from which the glass is
  scanned (inferred: page 248 and the board's scan-line reads), not light
  from the glass. A capture records the scroll start beside the pixels:
  `scrshot` takes the console's top line from `LCDG_XINFO`.
- `usr.bin/scrshot` writes PPM, BMP or raw RGB565 from those reads and has
  not been run on a board.

Gaps, each with its route:

1. `/dev/lcdgfx` admits one opener (`EBUSY`), so `scrshot` cannot capture
   a running i86 or libwin frame, and i86's close hands the panel back to
   the console, which repaints. Route: a capture by the program that holds
   the descriptor, through `LCDG_READ` on it and the `scrfmt.h` writers
   (an i86 menu entry under Control-]).
2. The Renode panel model answers no memory read, so every capture needs the
   board. Route: answer 2Eh from the model's frame store in
   `tools/renode/picocalc/st7365p.cs`; a Renode stage can then compare a
   capture with the host render.
3. Reads run at 3.125 MHz (`LCD_READ_SCR` 19) against the 6.67 MHz the
   150 ns read cycle allows (page 54). `PICOCALC.md` records the prediction
   for SCR 9 before any board run: the wire time halves, and a 55h read-back
   of the 153,600 memory pixels takes 0.66 to 0.81 s against 1.32 s
   measured; a read-back with any pixel different keeps SCR 19.

The read-back is the oracle for every stage below: the host build of i86
renders a guest frame into RGB565 with the same palette, the board draws
it, a capture reads it, and the two must agree in the five, six and five
bits a 55h write sets. A faster wire rate, a new loop, a DMA path or a
core-1 path passes only through that comparison.

## The staged design

Each stage removes one term, stands alone, and is measured before the next
is built.

| Stage | Term removed | Where | Measured by | Falsifier |
| --- | --- | --- | --- | --- |
| 1. Resumable refresh: i86 keeps its dirty-line set across polls and sends at most a time budget of lines per poll | redraw time held inside one poll | `usr.bin/i86` | host gate of the dirty set; board: no bunched ticks during a redraw | a redraw still delays a tick past one period |
| 2. Read-back oracle: in-process capture, Renode 2Eh, host-render comparison | (verification) | i86, `st7365p.cs` | a capture equal to the host render | any pixel different |
| 3. Inline expansion: 16-bit frames, palette loaded once per write | work per pixel, about 100 to 15-21 cycles | `lcdgfx.c`, `st7365p.c` | line time at SCR 1 (work-bound today) | line time at SCR 1 above 1.2 times its 163.8 us of wire time |
| 4. Wire rate, per unit: the default stays 12.5 MHz, inside the specification. A faster rate applies only to a unit that qualifies it at each boot: the driver writes a pattern to the off-screen memory lines 320-479 at the fastest step and reads it back at the read clock, then backs off one divider step from the fastest exact one; any difference, or a failed read, keeps 12.5 MHz | wire time | `st7365p.c` divider | the board sweep of measurement 3, then the boot self-test | any difference at the kept step or the step above it |
| 5. DMA drain: two bounce buffers of one RGB565 line each, a completion interrupt, `O_NONBLOCK` with `EAGAIN` | polling | `lcdgfx.c`, `st7365p.c` | core-0 time per frame against stage 3's | gain under 10 percent of the frame's wire time |
| 6. Core-1 scan-out, from the copied queue or from the pinned guest buffer. The pinned form needs an ownership protocol, because i86 keeps writing the banks while core 1 reads them: core 1 copies a line, then checks a generation count that i86 advances on each write to the line's page (a data memory barrier on each side), and retries the line if the count moved | expansion, and with the pinned buffer copy-in and system calls | core 1 | measurements 1 and 2; read-back compared only with the guest stopped | expansion under 30 percent of the wire time and copy-in under 10 percent of a session: stage not built |

Budget (computed from the linked PICOCALC_SD of e45e5c3c6): kernel RAM
holds 6,976 bytes past `__kernel_dump_reserve_end`
(`0x2003e800 - 0x2003ccc0`); stage 5's buffers take 1,280. Flash headroom
is 33,400 bytes (`check_kernel_headroom.py`). DMA fills (read address held)
also take `LCDG_FILL` off core 0: the 131 ms full-panel clear i86 issues at
every mode set becomes one transfer.

Stage 1 lies in the i86 lane (D200). Stages 3 to 6 change the kernel and
fall under D109 (new implementation after the rename) unless the owner
admits them earlier; stage 2's Renode change is test tooling.

## Requirements on any offload (stages 5 and 6)

The reviews of the first version of this note established each of these
from the source; an implementation answers every one before code review.

- Format and chunking. `/dev/lcdgfx` starts in RGB666 and admits
  `LCDG_WIN_BITS`, `LCDG_WIN_RGB565` and `LCDG_RAMWIN` windows, with writes
  of any length that split or cross rows (even lengths only for RGB565). An
  offload either implements every one or covers `LCDG_WIN_INDEX` at COLMOD
  55h alone and hands every other operation, and any COLMOD change, back
  to core 0 with the bus idle; its progress record is (x, y, bytes).
- Blocking. A bounded queue lets the writer run only as far ahead as the
  queue; i86 keeps emulating during a frame only with `O_NONBLOCK`,
  `EAGAIN` and the resumable refresh of stage 1.
- Reads. `LCDG_READ`, the scan-line wait and the panel test retune the
  divider and use RX; each waits for the offload to go idle first.
- Bus teardown, on every path including `panic()` and `arm_fault()`
  (`lcdcons.c`, `fault.c`): stop the DMA channel in the RP2040-E13 order
  (clear its interrupt enable, abort, poll BUSY, clear the status), clear
  `SSPDMACR.TXDMAE`, drain the RX FIFO and clear the overrun interrupt, and
  raise CS and D/C before `st7365p` reopens a window. `st7365p_abort()`
  cannot wait for a handshake.
- Core 1 (stage 6 only). A doorbell interrupt, since the 10 ms SysTick
  delays every bound; a ring pair for a thread-mode producer, since the
  existing rings have one producer and consumer in `core1_poll()`; a fault
  path that parks core 1 also stops the supervisor relaunching it. The
  observer adds 576 bytes of bss and 1,828 bytes of flash (`CORE1.md`),
  and its stack has 8 bytes of margin.
- Bounds. With a reply bound of twice the queued wire time, four RGB565
  lines are 1.64 ms on the wire at 12.5 MHz and the bound 3.3 ms.

## Measurements (board, PICOCALC_SD)

1. i86's redraw: wall time from `write(2)` entry to return for a graphics
   line and a text-cell window, and the share of a redraw-heavy session spent
   there. Falsifier for any offload: a share under 10 percent.
2. Work against polling for an `LCDG_WIN_INDEX` line: the two-rate split
   above (SCR 4 and SCR 1), or cycle counts on each side of copy-in,
   expansion and FIFO wait. Prediction from the count: 100 to 112 cycles a
   pixel, a line of 256 to 287 us at SCR 1.
3. The wire-rate sweep of stage 4 with full-frame read-back at each step.
4. For stage 6 only: the interpolator's expansion rate on core 1 and the
   CORE1 observer on a PicoCalc kernel with the panel renderer running.

Each result goes to `sys/arch/rp2040/doc/PICOCALC.md` with its kernel,
commit and method.

## Decisions

- D213: the staged design, each stage measured before the next is built.
  Stages 1 and 2 run on the host and in Renode; the kernel stages follow D109.
- D214: the wire-rate sweep of measurement 3 in the next board session.
  The driver's default stays 12.5 MHz; a faster rate applies only to a unit
  that qualifies it at boot (stage 4), with commands and window addresses
  sent at 12.5 MHz and only the pixel payload at the candidate rate.
