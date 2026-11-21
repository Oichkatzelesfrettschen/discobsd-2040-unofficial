# Panel scan-out on core 1 for i86: design and the measurements before code

Status: design, 2026-11-21. Owner decision D202 ("Core 1 scan-out") chose
this path over core-0 SPI1 DMA on a miscomputed comparison, corrected
below; the choice is back with the owner. Nothing here is built; kernel code waits on
the board measurements below and on the order D109 sets for new kernel
work. Every figure below is labeled measured (board), computed or
inference.

## The problem

`i86 -g` draws the guest's display on the PicoCalc's ST7365P panel through
`/dev/lcdgfx` (`sys/arch/rp2040/dev/lcdgfx.c`). The emulator sends one
line of palette indexes a `write(2)`: 320 bytes for a 320-pixel line
(`usr.bin/i86/gfx.c`). The kernel expands each byte through the palette
into wire pixels and feeds SPI1 by polling its transmit FIFO
(`st7365p.c`), all on core 0, the core that also runs the emulator.

- Measured on a PicoCalc (PICOCALC_SD, `sys/arch/rp2040/doc/PICOCALC.md`):
  a 320 by 320 fill takes 197 ms in RGB666 and 131 ms in RGB565, the bit
  time at 12.5 MHz; a `write(2)` of 153,600 pixels (the controller's
  320 by 480 memory) takes 340 ms and 242 ms.
- Computed from those: the wire is the floor, and about 45 ms of the
  242 ms write is core-0 work beyond the wire.
- Scaled to i86's graphics frame, 320 by 200 (`usr.bin/i86/gfx.c`):
  64,000 pixels are 128,000 wire bytes, 82 ms of wire time in RGB565
  (computed); at the measured 242 ms per 153,600 pixels the write holds
  core 0 about 100 ms (inference: i86 writes a line a call, whose
  per-call cost is unmeasured). Measurement 1 below replaces both figures.

Two ways free core 0, and they differ in what they leave on it:

- Core-0 DMA with bounce buffers hands the FIFO wait (about 197 ms of the
  242 ms write, the wire time) to the DMA engine and leaves the palette
  expansion on core 0: the 45 ms by which the write exceeds the wire is
  that expansion's unoverlapped part, so its whole cost is at least 45 ms
  and is unmeasured (computed and inference).
- Core 1 takes the expansion as well and leaves core 0 the copies.

Correction to D202's basis: the question put to the owner said core-0 DMA
"recovers about 45 ms per full screen". That reverses the figures; it
recovers up to about 197 ms of the 242 and keeps the expansion. The owner
re-decides D198 against D202 with these figures; measurement 2 below,
which splits core 0's time into copy, expansion and FIFO wait, decides the
difference between them.

## Constraints the tree sets

- Core 1 runs only code and data in SRAM. A flash erase or program
  stops execute-in-place for both cores (`sys/arch/rp2040/doc/CORE1.md`);
  every function core 1 runs is `.ramfunc`, which costs SRAM at run time
  and keeps its load image in the kernel's flash (`kern.ldscript`), so the
  scan-out code counts against both budgets.
- One owner per word, rings of slot numbers and arguments, never pointers
  (`CORE1.md`, `corering.c`). Core 1 must not follow a pointer into a
  process's memory.
- One 192 KB user window serves every process and is exchanged at a
  switch; the MPU belongs to core 0 and does not fence a DMA master or
  core 1 (`MPU.md`). Core 1 therefore cannot read i86's frame buffer in
  place: by the time it reads, another process may own the window.
- Kernel RAM: about 7,488 bytes free on a linked PICOCALC_SD image
  (computed from an older link, worker report E); the scratch area is
  full. Any staging buffer comes out of those bytes.
- The PicoCalc kernels do not carry `options "CORE1"` today
  (`CORE1.md`); the observer runs on PICO and PICO_UART. Adding it to a
  PicoCalc kernel costs the observer's own 576 bytes of bss and 544 bytes
  of `.ramfunc` code (SRAM and flash) before any scan-out code.
- SPI1 serves only the panel (the card is on SPI0), but the console
  renderer, `lcdgfx` ioctls, `st7365p_abort` and close all touch SPI1;
  each must wait for the scan-out to go idle or hand the bus over.

## Design

Core 0 keeps the system call; core 1 owns SPI1 and one DMA channel while
a scan-out session is open. A session covers palette-index windows only
(`LCDG_WIN_INDEX`, the form i86 uses for graphics lines). Every other
`/dev/lcdgfx` operation, a `LCDG_WIN_BITS` window (text cells), an RGB565
window, a fill, an ioctl, close and the console renderer, first ends the
session: core 0 asks core 1 to finish the queued lines and release the bus,
waits for the reply as in "Failure and recovery", and then runs today's
core-0 path. Core 1 never sees a format it does not expand.

1. `lcdgfx_write` copies the caller's line of indexes (at most 320 bytes)
   into a kernel line slot and publishes the slot on the core-1 request
   ring. Four slots of 320 bytes: 1,280 bytes. Core 0's cost per line is
   the copy and the publish.
2. Core 1 takes the slot, expands it through the current palette into
   RGB565 with its interpolator (INTERP0 in lane-lookup mode: index to
   table address), into one of two 640-byte wire buffers (1,280 bytes),
   starts DMA from that buffer to SPI1's data register (DREQ 18), and
   expands the next slot into the other buffer while the first goes out.
3. Core 1 returns the slot on the reply ring. With all four slots full a
   write would block; four slots alone therefore let i86 run only four
   lines ahead of the wire, and a 200-line redraw would still hold it for
   most of the wire time. The decoupling comes from the emulator: `i86`
   opens the device with `O_NONBLOCK`, a write with no free slot returns
   `EAGAIN`, and `gfx.c` keeps its set of lines still to send and sends
   what fits at each poll, so emulation runs while core 1 drains. A
   blocking writer keeps today's behavior.
4. A window change, a palette change, a mode change or close is a ring
   request too, so core 1 applies it between lines in order; core 0 never
   touches SPI1 while the service is open.

## Failure and recovery

Core 0 must lose nothing when core 1 does (`CORE1.md`: a dead core 1 costs
no kernel function). Each scan-out session carries an epoch that core 1's
restart advances. Core 0 bounds every wait on the service: a slot with no
reply within twice the wire time of the lines ahead of it (about 2 ms for
four lines, computed), or a changed epoch, ends the session.

Ending it on a fault, core 0 first stops core 1 through the existing
supervisor (park and the forced-off reset of `CORE1.md`), so a core 1 that
was only slow cannot touch DMA or SPI1 again. Then it aborts the DMA
channel in the order erratum RP2040-E13 gives (clear the channel's
interrupt enable, abort, poll BUSY, clear the status) and waits for SPI1
to go idle. Each slot records its line's window row and a done mark that
core 1 sets after the DMA for that line completes and the FIFO drains;
core 0 reopens the window at the first line without a mark and sends the
remaining lines itself, so no line is lost, repeated or shifted. The open
file then stays on today's core-0 path: a writer sees slower lines, never
`EAGAIN` forever, and close and ioctls never wait on a dead core.

RAM: 2,560 bytes of buffers, the service's `.ramfunc` code (estimate
under 1 KB, also in flash), and the CORE1 observer's 576 bytes of bss and
544 bytes of code: about 4.6 KB of the 7,488 free (computed from an older
link; the exact configuration must be relinked before this budget
counts). Inference: core 0's time per i86 frame falls from about 100 ms to
the copies and system calls, a few milliseconds.

The wire time does not change: i86's 320 by 200 frame still takes 82 ms
at 12.5 MHz in RGB565. What changes is that i86, writing without
blocking, keeps emulating during it.

## Alternatives considered

- Core 1 reading the guest frame buffer directly: refused by the window
  exchange and the pointer rule above.
- A whole frame staged in kernel RAM (CGA's 16 KB, or 204,800 wire bytes):
  does not fit the 7,488 free bytes.
- Core-0 DMA with two small bounce buffers (D198): frees the FIFO wait,
  keeps the expansion on core 0, needs no core-1 code, no CORE1 option and
  no cross-core recovery; less SRAM. Its gain against this design's is the
  expansion time, which measurement 2 gives.
- A faster SPI clock: shortens the wire time itself and is a separate
  question; the ST7365P's write-cycle limit against the 12.5 MHz in use is
  not established here.

## Measurements before code (board, PICOCALC_SD)

1. i86's own redraw: the time of a full graphics-mode redraw and of a
   text-mode scroll, from `write(2)` entry to return, with `i86 -v`'s
   instruction rate during a redraw-heavy run. Falsifier for the whole
   design: if the redraw share of a typical session is under 10 percent
   of core 0's time, the service is not worth its SRAM.
2. Core 0's cycles in `lcdgfx_write` per line, split into copy, expansion
   and FIFO wait (a SysTick sample or a cycle counter read on each side).
3. The interpolator's expansion rate on core 1 for 320 indexes
   (a bare-metal probe, no kernel change): it must beat the wire's
   640 bytes in 410 us at 12.5 MHz.
4. The CORE1 observer on a PicoCalc kernel, with the panel renderer
   running: `CORE1.md` records the effect on the tick record as unmeasured.

Each result goes to `sys/arch/rp2040/doc/PICOCALC.md` with its kernel,
commit and method; the code waits on 1 and 3.

## Open questions for the owner

- D198 against D202 again, with the corrected figures above.
- The budget: about 4.6 KB of the 7,488 free SRAM bytes, and the flash
  image of the `.ramfunc` code.
- Whether the PicoCalc kernels take `options "CORE1"`, which the service
  needs.
