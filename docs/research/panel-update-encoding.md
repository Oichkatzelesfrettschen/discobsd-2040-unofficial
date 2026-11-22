# Fewer bytes to the PicoCalc panel: panel-space damage, controller reuse and packed internal streams

Status: research note, 2026-11-21. It answers whether a full i86 graphics
frame must cost 128,000 bytes on the wire. It derives the least a frame
update can cost from what the ST7365P accepts, and measures that minimum on
two recorded i86 sessions. It also surveys how every program that draws to
the panel decides what to redraw. Figures are labeled measured (host model),
computed or unmeasured; nothing here ran on a board.

Provenance: tree 9642c1fda to b6255e8dc (main). The corpora come from a
scratch build of `usr.bin/i86` around its host panel model
(`tests/panel.c`), with instrumentation that logs each refresh and dumps the
guest's display memory and the model's panel frame. Two sessions were
recorded with `-T` and scripted keys. One is SvarDOS at 80 columns
(`dir`, `mem`, `type`). The other is Windows 3.00a in CGA mode 6, booted
to Program Manager from owner-supplied media; no byte of that media enters
the tree. The worker reports behind the tables are in the orchestration
records (panel-bytes S1, S2, H1, H2); their scripts and corpora stayed in
scratch space.

## The claim and its falsification

A full frame is 320 by 200 pixels, two bytes each at COLMOD 55h: 128,000
bytes. Each factor in that product is an assumption, and each one fails.

- The information is far smaller. A CGA graphics frame is 16,000 bytes of
  guest memory (2 bits a pixel in modes 4 and 5, 1 bit in mode 6). Two bytes
  a pixel is the panel's input format, not the picture's content.
- A full frame is rarely the update. In the Windows session, 98.2 percent of
  the pixels sent in steady state left the panel unchanged (S1).
- The controller can reuse what it holds. A vertical scroll moves every line
  of its scroll area for a few command bytes, with no pixel data.

What does not fail is the per-pixel cost of a pixel that does change. The
ST7365P documents only 16-, 18- and 24-bit pixel formats (datasheet
page 195). Its digital gamma tables cover red and blue but not green, and
no enable bit was found for them (`st7365p-register-reference.md` 4.11). It
lacks a copy, fill or decompression command. Every changed pixel therefore
costs two bytes on the wire. The savings come from sending fewer pixels, and
from moving fewer bytes on the paths before the wire.

## First principles: the display map and its kernel

Write the display as a map D from guest state to panel pixels: expansion of
the packed guest bytes, the palette, then the panel. A refresh must make the
panel equal D(new). The controller is the only decoder. Inside the
normal-mode `/dev/lcdgfx` interface, which keeps the console's COLMOD,
MADCTL, inversion and full display mode, it offers three operations:

| Operation | Wire cost | Effect |
| --- | --- | --- |
| Window write (CASET, RASET, RAMWR) | 11 bytes plus 2 a pixel | sets a rectangle |
| Vertical scroll start (VSCSAD, inside a VSCRDEF area) | a few bytes | moves every line of the area by k |
| Nothing | 0 | the panel keeps its memory |

The controller has other transforms of the whole display, which this bound
leaves out on purpose:
- INVON and INVOFF invert every displayed pixel;
- idle mode shows eight colors;
- partial mode blanks lines;
- MADCTL remaps the scan order.

A guest update matches one of these only in rare whole-screen cases (an
inversion of every pixel); they change the console's state; and i86 does
not send them. Bytes saved through them would come on top of what this
note counts.

The cheapest update is therefore a short program over these operations that
turns D(old) into D(new):
- a scroll first, if one helps;
- then rectangles covering the support of D(new) XOR D(old), merged under
  libwin's cost rule `cost(r) = 2wh + 11 * ceil(h / floor(5120 / w))`.

Two consequences follow.

- **Damage belongs in panel space, not guest space.** Any change in the
  kernel of D is free. The kernel holds:
  - same-value writes;
  - mode-6 pixel pairs 01 and 10, which render to the same half level;
  - character codes 0, 32 and 255, all drawn blank;
  - any change while every palette entry is black.

  This is the idea the Cayley-Dickson zero-divisor work studies: a
  multiplication map L_a whose kernel is large, so that a vector modulo
  ker L_a carries all of the image. The transfer is the quotient, not the
  algebra. The controller multiplies nothing, so no octonion or sedenion
  structure can reach the wire, and graph sparsification (the thin-tree
  preprints) offers nothing here to act on.
- **The scroll operation turns a row shift into a change of a few bytes.** It
  pays when the new frame equals the old one shifted. That is the common case
  for text, the main use of i86's display.

Measured worth of the kernel alone (S2):
- text: 0 of 33,088 changed cells;
- graphics pixel pairs: 6 of 89,967;
- changed graphics bytes: 12 percent;
- the 22 Windows refreshes drawn while the palette was all black: 481,280
  bytes today, 14.3 percent of that session.

So the kernel matters at palette and mode transitions, not in steady state.
Most of the gain is the granularity of panel-space damage itself.

## Wire bytes on the two sessions (measured, host model)

S2 computed each method from the recorded frames. A controller model replays
method 4 and reproduces every recorded frame.

| Method | DOS, 149 refreshes | Windows, 81 refreshes |
| --- | --- | --- |
| 1 today: page-dirty lines and cell runs, full-panel clears | 2,430,336 | 3,361,088 |
| 2 guest-space span rectangles | 2,494,198 | 1,405,344 |
| 3 panel-space rectangles under the cost rule | 1,414,148 | 447,553 |
| 4 method 3 with the best vertical scroll of the 200-line area | 442,045 | 447,553 |
| 5 method 4 without clears of a panel already black | 237,234 | 242,742 |

- **Gains:** method 5 is a 10x gain on the DOS session and 14x on Windows.
- **Scroll:** chosen on 68 DOS refreshes, always by multiples of 8 lines,
  saving 972,103 bytes. Never chosen on Windows.
- **Already-black panel:** at a non-superuser open, the PICOCALC_SD kernel
  fills all 480 memory lines black (`lcdgfx.c`), so the first refresh's
  full clear is redundant there.
- **Time:** at 12.5 MHz (0.64 us a byte), the DOS session's wire time falls
  from 1.56 s to 0.15 s and Windows from 2.15 s to 0.16 s.

## Streams that do not cross the wire (measured, host model)

The program-to-kernel `write(2)` stream and a core 0 to core 1 queue (stage 6
of `i86-core1-scanout.md`) carry bytes the panel never sees, so any lossless
coding with a cheap decoder serves them.

| Encoding, bytes a session | DOS | Windows |
| --- | --- | --- |
| today's `write(2)`: 1 byte a pixel, 1 bit a pixel for cells | 236,204 | 1,306,116 |
| panel-space rectangles in packed native indexes (1, 2 or 4 bits a pixel) | 501,165 | 31,059 |
| guest-state delta: skip and literal tokens (equal to XOR with zero runs) | 79,318 | 32,110 |
| plus row-copy operations | 11,747 | 28,676 |
| plus per-line predictors (up, two up, left, previous frame) | 11,747 | 6,796 |
| `xz -9e` of the last stream, a reference rather than a bound | 3,324 | 4,172 |

Every encoder has a decoder whose output, rendered through D, reproduces the
recorded frames. Cortex-M0+ decode costs (computed, `-Os`, from S2's static
counts):
- 8.75 to 15 cycles a pixel for the packed index formats;
- 10 to 13 cycles a byte for literal, XOR and predictor bytes;
- 2.75 cycles a byte for row moves.

A pixel on the wire takes 160 core cycles at 12.5 MHz, so decoding costs
under a tenth of what a skipped pixel saves.

## Resolution: what fills the glass without artifacts

- 320 by 200 content maps 1:1, centered with 60 black lines above and below.
  No other integer factor fits the 320 lines, since 2 x 200 = 400.
- 640-wide content (mode 6) halves horizontally. Each pair becomes one pixel
  of three levels, and a mixed pair loses which of its two pixels was set. On
  the Windows session that is 4.9 percent of pairs in the frames that show
  anything (103,436 of them, at most 8,391 in one frame). This is the only
  lossy step in i86's display, and the panel's 320 columns rule out a lossless
  alternative.
- A 320x240 target, half of VGA's 640x480, gains nothing for these modes. It
  repeats one line in five (uneven line heights), grows panel-space damage by
  10 to 19 percent, and breaks the 8-line alignment that makes text scroll
  cheap (DOS method 4 rises from 442,045 to 932,568 bytes). It suits a guest
  mode that is 240 lines tall, which i86 does not emulate.
- Partial mode (PTLON, PTLAR) only blanks lines; its rows are memory rows
  (datasheet page 181). The spec never describes it combined with scrolling,
  and the 160 hidden memory lines cannot hold a second 200-line page. Idle
  mode changes what is displayed, not the bytes sent.

## How each program decides what to redraw (S1, read at 9642c1fda)

| Program | Unit | Main overstatement | Cheapest change |
| --- | --- | --- | --- |
| i86 graphics | 1 KB page of a field, about 13 lines | no comparison: a 4-pixel change resends whole lines | a 16 KB copy of the frame last sent, compared byte for byte (lossless, span-exact, 16 of i86's page frames); a per-line hash is cheaper but probabilistic (below) |
| i86 stale, palette, mode | whole panel | full 320x320 clear before a full redraw; same-value palette, port and mode writes redraw everything | compare before `palette()`; clear only the margins, and only when not already black; keep `stale` off for the same kind |
| i86 text | cell run | cells redrawn whole; a scroll resends every shifted cell | VSCRDEF 60/200/220 and VSCSAD for row shifts; trim runs to changed rows |
| kernel console (`lcdterm.c`) | 6x10 cell | every glyph drawn without comparing to its cell store; 1,696 cells repainted at every retake | skip identical cells; dirty flags for the retake |
| libwin | up to 8 rectangles | move, raise, lower, show and hide damage the whole window; same-title sets redraw | damage only the uncovered intersection; compare titles |
| matezinho | band and span | static views marked every second; wide marks for small changes | drop the static case; narrow the clock mark |
| ridge | 8x8 tile | a still sprite reloads 9 tiles every 8 ticks | animation counted only when a shard is in the box |
| ulisp | one fill a primitive | 13 bytes a pixel; opaque glyphs draw background then foreground | one 1-bit window per glyph |

None of the sibling emulators on this board compares frames. picocalc_x86
streams the whole panel every frame, and pico-286 pushes a 320x240 window on
a 16,667 us timer (H2). i86's page watch is already ahead of them.

## The resolved method

Each part is lossless, stands alone, and carries its own falsifier.

1. **Panel-space damage in i86.**
   - Lossless form: a 16 KB copy of the guest frame last sent, compared byte
     for byte when a watched page is written. It gives span-exact damage,
     cut by the cost rule. Windows graphics fall from 1,229,760 pixels to
     the span-exact figure, and line-exact alone already gives 382,080 (S1).
     The cost is 16 of i86's page frames (120 by default under `-e 2048`).
     The copy can live in pager frames that page out like guest memory.
   - A per-line hash (16 or 32 bits, 400 or 800 bytes) is cheaper but not
     lossless. Two different lines with the same hash leave the old line on
     the glass. With a 32-bit hash the chance is 2^-32 for each changed
     line, about 3 x 10^-7 over the Windows session's changed lines. A
     periodic resend of every line bounds how long such a line stays wrong.
     Choosing it over the copy trades exactness for memory, and that choice
     belongs to the owner.
   - Falsifier: a host-model session whose final frame differs from the
     reference frame rendered from guest memory.
2. **Hardware scroll for i86 text and graphics row shifts.**
   - VSCRDEF 60/200/220 makes the guest's 200 lines the scroll area.
     VSCSAD then takes an absolute memory line inside it: 60 + (s mod 200),
     where s is the accumulated shift in lines. The first 24-line shift
     sets 84. `LCDG_SCROLL` refuses an address in the fixed areas
     (`sys/sys/lcdgfx.h`). Every window is then addressed through the same
     rotation and cut at the wrap seam.
   - Falsifier: a board read-back of the scroll area after VSCSAD 84 that
     differs from the image rolled by 24 lines. That refutes 972 KB of the DOS
     session's saving, and it waits on the owner's board opt-in.
3. **Known-black clears skipped.** Same-value palette, port and mode writes
   leave the panel alone.
4. **The console and libwin fixes** of the table above, each with the
   counter its existing host test already keeps (`lcdterm_test.c`,
   `win_test.c`, `core_test.c`, `render_test.c`).
5. **Packed native indexes and guest-state deltas** for the internal streams.
   - This is a `/dev/lcdgfx` window format, so a kernel ABI change, under
     D109.
   - It pays mainly with the core-1 queue of stage 6.

Parts 1 and 3 change i86 alone; 2 needs i86 and the existing `LCDG_SCRDEF`
and `LCDG_SCROLL` commands. The board measurements of
`i86-core1-scanout.md` still decide the offload stages. What this note
shows is that the bytes those stages carry shrink by an order of magnitude
first.
