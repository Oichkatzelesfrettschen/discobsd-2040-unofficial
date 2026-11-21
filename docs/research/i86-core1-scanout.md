# Panel output for i86 off the emulating core: options, figures and the measurements before code

Status: decision record, 2026-11-21. The owner chose core-1 scan-out
(D202) over core-0 SPI1 DMA (D198) on a comparison that reversed the
figures; the corrected comparison is below, and the choice is back with
the owner. Nothing here is built. Every figure is labeled measured
(board), computed or unmeasured; no core-0 cost of i86's own output path
has been measured.

## What i86 sends

`i86 -g` draws through `/dev/lcdgfx` (`sys/arch/rp2040/dev/lcdgfx.c`) with the
panel in RGB565 (`LCDG_COLMOD` 55h, `usr.bin/i86/gfx.c` `gfx_open()`): a run
of changed graphics lines is one `LCDG_WINDOW` of `LCDG_WIN_INDEX` and one
`write(2)` of exactly 320 palette indexes a line (`graphic_refresh()`); text
cells go as `LCDG_WIN_BITS` windows. The
kernel expands each byte into wire pixels and feeds SPI1 by polling its
transmit FIFO (`st7365p.c`), all on core 0, the core that also runs the
emulator.

- Computed: SPI1 runs at 12.5 MHz (80 ns a bit). A 320-pixel RGB565 line
  is 640 bytes, 409.6 us on the wire; i86's 320 by 200 graphics frame is
  128,000 bytes, 81.9 ms. The ST7365P's write cycle (66 ns, ceiling
  15.15 MHz; `PICOCALC.md`, `st7365p.c`) leaves the next PL022 divider,
  15.625 MHz, out of specification, so the wire time is the floor.
- Measured on a PicoCalc (PICOCALC_SD, `PICOCALC.md`): a 320 by 320 fill
  takes 131 ms in RGB565 at that bit time.
- Not a measure of i86's path: the 242 ms write in `PICOCALC.md` comes
  from `tests/rp2040/lcdexttest`, which opens one 320 by 480 `LCDG_RAMWIN`
  window of `LCDG_WIN_RGB565` (no palette expansion), feeds it in 480
  writes and times its own pattern generator inside the loop. The cost of the palette expansion, of a system call per
  line and of the FIFO wait on i86's path are all unmeasured.
- Inference: a frame holds core 0 in the draw path for at least its wire
  time, 81.9 ms, longer than one 54.9 ms timer period, so the guest's clock
  interrupts bunch up during a redraw. Of that time, the part spent
  spinning on a full FIFO is the wire time less the copying and expansion
  that overlap it.

## The options

| Option | Takes off core 0 | Leaves on core 0 | Cost |
| --- | --- | --- | --- |
| Core-0 SPI1 DMA from small bounce buffers (D198) | the FIFO wait, at most the wire time | the palette expansion and the copies | DMA channel and IRQ handler; two bounce buffers; no core-1 code |
| Core-1 scan-out (D202) | the FIFO wait and the expansion | the copies and a ring publish | the above plus core-1 `.ramfunc` code, the CORE1 option on a PicoCalc kernel, cross-core recovery |
| Both unchanged, i86 drawing less often | part of every frame | everything | none in the kernel |

Correction to D202's basis: the question put to the owner said core-0 DMA
"recovers about 45 ms per full screen". That figure came from the 242 ms
RGB565 test above, which ran no expansion, and reads its excess over the
wire as the gain. Core-0 DMA frees the FIFO wait, up to the wire time, and
keeps the copying and expansion; core 1 frees both. Measurement 2, which
splits core 0's time among them, decides between the options.

A whole frame staged in kernel RAM does not fit: i86's frame is 128,000
wire bytes, and the kernel RAM free on PICOCALC_SD is a few kilobytes
(not measured on a current link; `kern.ldscript` also keeps 512 bytes
free for the dump reserve). A relink of the chosen configuration gives
the figure before any design counts on it.
Reading i86's buffer in place from core 1 or a DMA master is refused by
the tree: one 192 KB user window is exchanged between processes, and a
core-1 entry carries no pointers (`CORE1.md`).

## Requirements on whichever design is chosen

The reviews of this note established each of these from the source; an
implementation answers every one before code review.

- Format and chunking. `/dev/lcdgfx` starts in RGB666 (3 bytes a pixel)
  and admits `LCDG_WIN_BITS`, `LCDG_WIN_RGB565` and `LCDG_RAMWIN` windows,
  with writes of any length that split or cross rows (even lengths only
  for RGB565; `lcdgfx.c`). An offload either
  implements every one or covers only `LCDG_WIN_INDEX` at COLMOD 55h and
  hands every other operation, and any COLMOD change, back to core 0 with
  the bus idle; its progress record is (x, y, bytes), not a line number.
- Blocking. A bounded queue lets the writer run only as far ahead as the
  queue; i86 keeps emulating during a frame only if it writes with
  `O_NONBLOCK`, takes `EAGAIN`, and keeps its unsent lines.
- Bus teardown, on every path including `panic()` and `arm_fault()`
  (`lcdcons.c`, `fault.c`): stop the DMA channel in the RP2040-E13 order
  (clear its interrupt enable, abort, poll BUSY, clear the status), clear
  `SSPDMACR.TXDMAE`, drain the RX FIFO and clear the overrun interrupt, and
  raise CS and D/C before `st7365p` reopens a window. `st7365p_abort()`
  cannot wait for a handshake.
- Core 1 (D202 only). It wakes from `WFI` on a 10 ms SysTick (`core1.c`),
  so it needs a doorbell interrupt or every bound below is 10 ms late. The
  request and reply rings have one core-0 producer and consumer,
  `core1_poll()` in interrupt context; a thread-mode writer needs its own
  ring pair or synchronization with it. A fault path that parks core 1
  must also stop the supervisor from relaunching it. Budget: the observer
  adds 576 bytes of bss and 1,828 bytes of flash (544 of them the
  `.ramfunc` image; `CORE1.md`), its stack has 8 bytes of margin, and the
  scan-out's own code costs SRAM and flash. Kernel RAM free is a few
  kilobytes and must come from a relink of the chosen configuration.
- Bounds. With a reply bound of twice the queued wire time, four RGB565
  lines are 1.64 ms on the wire and the bound 3.3 ms.

## Measurements before code (board, PICOCALC_SD)

1. i86's redraw on the board: wall time from `write(2)` entry to return
   for a graphics line and a text-cell window, and the share of a
   redraw-heavy session spent in them (the session's own clock, since
   `i86 -v` reports polls and pages, not time in output). Falsifier for
   any offload: a share under 10 percent of the session.
2. Core 0's time in `lcdgfx_write` for an `LCDG_WIN_INDEX` line, split into
   copy-in, palette expansion and FIFO wait (a cycle count on each side of
   each part). Its expansion share decides D198 against D202.
3. For D202: the interpolator's expansion rate on core 1 for 320 indexes
   (bare-metal probe), against the line's 409.6 us on the wire.
4. For D202: the CORE1 observer on a PicoCalc kernel with the panel
   renderer running, which `CORE1.md` records as unmeasured.

Each result goes to `sys/arch/rp2040/doc/PICOCALC.md` with its kernel,
commit and method.

## Open questions for the owner

- D198 against D202 again, once measurement 2 exists.
- The RAM and flash budget of the chosen option, from a relink.
