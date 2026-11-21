# Graphics pipeline design: wire format, damage and band ranges

This note turns the ranked proposals of `README.md` (this directory) into
decisions for the PicoCalc display path. It fixes the panel wire format
for every program, the cost rule libwin uses to merge damage, the band
ranges of matezinho, and the wire-budget constants games plan with, and it
states which proposals wait for a consumer. Line citations name
`72b954476`.

## The wire is the bottleneck

Every pixel a program shows crosses the panel's SPI link. At 0x66 (18 bits a
pixel, 3 wire bytes) a full 320x320 frame takes 196.6 ms; at 0x55 (RGB565,
2 wire bytes) it takes 131.1 ms; the board measured 197,000 and 131,000 us
(`sys/arch/rp2040/doc/PICOCALC.md`). A pixel costs 1.920 us at 0x66 and
1.280 us at 0x55. The glass scans from the panel's memory at 57.9 Hz, 7.6
times faster than a full-width write at 0x55, so the scan laps a writer
after about 48 full-width rows (`README.md`, section 4).

## Decision 1: every program sends RGB565

Each open of `/dev/lcdgfx` sets COLMOD 0x66 (`sys/arch/rp2040/dev/lcdgfx.c:279`,
`:290`), and the last close restores the console's 0x55 (`lcdgfx_close`,
`:323`). Only i86 asks for 0x55 (`usr.bin/i86/gfx.c:251`). Every program
that opens the device issues `LCDG_COLMOD` 0x55 once after each open:

| Program | Pixels it hands the kernel | Change |
| --- | --- | --- |
| `usr.bin/matezinho` | RGB565 bands through `LCDG_WINDOW` (`main.c:85-93`) | COLMOD after both opens (`main.c:372`, `:430`) |
| `lib/libwin` | 1-byte palette indices the kernel expands (`lcdgfx.c:391`) | a backend call that sets COLMOD, used by `tests/rp2040/windemo/windemo.c` |
| `games/ridge` through `games/gametile.h` | 4-bit tiles through `LCDG_TILEWIN` | COLMOD after the open (`ridge.c:1858`) |
| `usr.bin/ulisp` (`dev_picocalc.c`) | RGB565 fills | COLMOD after the lazy open (`:62`) |

`usr.bin/paneltest` keeps 0x66: it measures that mode (`PICOCALC.md:280`).

Gain: 1.5 times on every redraw, with 0 bytes of RAM and one system call a
program. Every color already reaches the panel driver as an RGB565 value:
`window_data` (`lcdgfx.c:376-400`) hands `put_color` either the window's
RGB565 pixels or an entry of the kernel palette `gfx_pal`, so at 0x66 the
driver only widens 16 bits to 18, and 0x55 sends the same color in two
bytes instead of three. What the glass shows at 0x55 is board evidence only: Renode replicates the high bits into the low ones
(`tools/renode/picocalc/st7365p.cs:513-527`), and the board stores them clear
(`PICOCALC.md:268-270`).

Each host model of the device (`tests/libwin/win_test.c`,
`games/ridge/tests/render_test.c`, `tests/picocalc/gametile_test.c`) rejects an ioctl it does not know; each
learns `LCDG_COLMOD`, records the mode set, and fails a program that sends
pixels before setting 0x55.

## Decision 2: libwin merges damage by wire cost

`damage_add` (`lib/libwin/win.c:238-266`) joins rectangles that share an
edge, trims one that another covers, and otherwise keeps both, so a window
moved diagonally by a few pixels repaints its old and new rectangle in
full: 20,000 pixels for a 100x100 window moved by (5,5). The bounding box
of the pair is 11,025 pixels.

The rule: when a new rectangle overlaps or touches a recorded one,
`damage_add` first forms what it keeps today (the pair, with a contained
rectangle dropped, an edge-sharing pair joined, or the overlap trimmed off
one of them), and the pair becomes its bounding box only when

    cost(box) <= cost(kept),  cost(r) = bpp * w * h + 11 * strips(r)

with cost(kept) the sum over the rectangles that form keeps. Comparing
against the trimmed form keeps a merge from sending more than the present
code: rectangles [10,110)x[0,100) and [0,120)x[50,150) at two bytes a
pixel cost 44,055 bytes untrimmed and 34,044 after the first is trimmed to
50 rows, so their 36,044-byte bounding box is refused.

where `bpp` is the scene's wire bytes a pixel (2 at 0x55, 3 at 0x66),
`strips(r)` = ceil(h / floor(5120 / w)) is the number of `LCDG_WINDOW`
strips `repaint` sends for r (its strip height is the integer quotient
`WIN_STRIP_BYTES / w`, so a 34x301 rectangle takes three strips of 150 rows), and 11 bytes is the window command a strip costs.
The scene carries `bpp`, set by the backend call of decision 1.

This widens damage: a repaint then rewrites pixels that did not change,
with the values they already hold. libwin's documented invariant ("never
widened", `win.h:55`, `:75`) becomes "widened only where the bounding box
costs no more wire bytes", and the test oracle that marks every written
pixel as invalidated (`tests/libwin/win_test.c:247-259`) becomes a check that every
invalidated pixel is written and that the bytes sent never exceed the
unmerged cost.

## Decision 3: matezinho sends the dirty columns of a band

`mz_render` (`usr.bin/matezinho/core.c:353-363`) redraws a flagged 320x8
band and `write_band` sends all 5,120 bytes of it. Each band gains a dirty
column range, kept in 4-pixel units in two bytes a band (80 bytes for the
40 bands): `mark` widens it, `mz_render` clears it, and the band callback
receives it. `write_band` opens a window of the range's width and sends
those columns row by row from the band buffer. `scene()` still draws the
whole band; the saving is wire time, up to 5 times for a 64-pixel edit.
The host test copies only the reported range into its reference, so a
`mark` that under-reports fails it.

## Decision 4: games plan against named wire constants

`sys/sys/lcdgfx.h` gains the per-pixel wire times and the scan rate as
named constants:

| Name | Value | Meaning |
| --- | --- | --- |
| `LCDG_NS_PER_PX_565` | 1280 | nanoseconds a pixel at COLMOD 0x55 |
| `LCDG_NS_PER_PX_666` | 1920 | nanoseconds a pixel at COLMOD 0x66 |
| `LCDG_SCAN_HZ_X10` | 579 | glass scan rate, tenths of a hertz |

A game sizes its viewport and strips from them: at 30 frames a second
and 0x55, 26,041 pixels a frame, so at most 406 whole 8x8 tiles of the 1,600 before the window commands of their runs are counted. The figures
are board measurements (`PICOCALC.md:285`, `:322`); a change of panel
clock changes them, and the header says so.

## Proposals that wait for a consumer

- Hardware scroll as a library path. `LCDG_SCRDEF` and `LCDG_SCROLL` work
  (`lcdgfx.c:811-823`, board run of `tests/rp2040/lcdexttest/lcdexttest.c`), and no
  shipped game scrolls vertically: `ridge` scrolls sideways by re-sending
  tiles. A tear-free ring also needs RAMWIN beyond row 320, which
  `LCDG_TILEWIN` cannot reach (`lcdgfx.c:843-845`). The path is written
  when a vertical scroller exists, against that program's test.
- A tear-free frame fence. `LCDG_WAITLINE` sets a start phase only; a
  fence must wait again before each band of at most about 48 full-width
  rows. No shipped program needs it.
- Packed RGB565 blending (`README.md`, proposal 7) and the palette and
  dither policy (proposal 8). No shipped program blends or quantizes at run
  time; both stay as recorded computations.

## Evidence each decision needs

| Decision | Host gate | Board evidence |
| --- | --- | --- |
| 1 | the host models refuse pixels before 0x55: `check-libwin-host`, `check-ridge-host`, `check-picocalc-host` (gametile), `check-matezinho-host`, and a host model of `usr.bin/ulisp/dev_picocalc.c`'s lazy open (`check-ulisp-host` builds `dev_host.c`, not this file) | a redraw timing at 0x55 against 0x66, and the colors on the glass |
| 2 | `check-libwin-host` with the cost oracle and a mutant per rule clause | windemo drag timing |
| 3 | `check-matezinho-host`, `check-matezinho-qemu` with the range oracle; Renode: `tools/renode/check-matezinho.py` | a redraw capture of a small edit |
| 4 | compilation of every includer | none |
