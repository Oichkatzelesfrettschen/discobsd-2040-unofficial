# word: a proposed graphical mode on the PicoCalc panel

`usr.bin/word` draws through the console terminal: 53 by 32 cells of 6 by
10 pixels on the 320 by 320 panel (`sys/arch/rp2040/doc/PICOCALC.md`,
Console). The pixel device `/dev/lcdgfx` (`sys/sys/lcdgfx.h`) offers a
second route: fills, lines, frames, pixel windows in RGB565, 4-bit palette
index or 1 bit per pixel, a 16-entry palette, a 128-tile atlas of 8 by 8
tiles on a 40 by 40 grid, sprites, and on PICOCALC_SD kernels memory
read-back and scan-line waits. This note proposes what a graphical mode of
`word` would add and what it would cost. Nothing here is built; the byte
figures are estimates from the current build (text 38 KB, bss 48 KB) and
the sizes of the neighbouring pixel programs, and a prototype measures
them.

## What it adds

- Character formatting that the text console cannot show: bold, italic,
  underline, double underline, strikethrough, small capitals, superscript
  and subscript drawn into the glyph pixels. This is the part of Word 5.1's
  Format Character command that SPEC.md leaves out for want of a display.
  The document would need a formatting run list beside the piece table.
- A page view (the original's Print preView): the page at reduced scale
  with margins and line breaks, from the same layout code that wraps rows.
- Denser text: an 8 by 8 cell font in the tile atlas gives 40 rows by 40
  columns, or a 5 by 8 font in 1-bit windows gives 64 columns by 40 rows.
- A ruler line with tab stops and indents, and menus drawn as framed panels
  with the highlighted command filled, instead of reverse video.

## What it costs

- Text: a cell renderer that writes 1-bit windows per changed run, reusing
  `scr_flush()`'s change detection, about 2 to 3 KB of Thumb code. A page
  view about 2 KB. Formatting runs and the Format Character form about 3 KB.
- Data: one 96-glyph 6 by 10 font is 960 bytes; bold and italic derive from
  it by smearing and shearing, so they need code rather than more glyphs.
- RAM: the panel holds the image, so no frame buffer. The cell arrays of
  `word_term.c` gain a style byte per cell, 3,392 bytes at 53 by 32 for
  both arrays; formatting runs at 8 bytes each in a fixed table.
- Behavior: `/dev/lcdgfx` is exclusive and the console stays silent while
  it is open (`lcdterm_gfx()`), so the graphical mode owns the panel
  completely; Library Run would close the device before the shell runs.
  Keys still come from `/dev/tty`.

Total estimate: 7 to 9 KB of text, 1 KB of data and 4 KB of bss, which
keeps the program under 60 KB of text in the 192 KiB process window.

## Falsifiers

A prototype that draws a full 53 by 32 screen of 1-bit windows slower than
the console redraw (measured on the board with the scan-line clock) refutes
the denser-text route, and a formatting run table that cannot hold a
typical letter in 1 KB refutes the fixed-table design.
