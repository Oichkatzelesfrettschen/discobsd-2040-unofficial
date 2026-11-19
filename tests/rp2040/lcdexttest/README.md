# lcdexttest -- PicoCalc extended pixel commands on the board

lcdexttest, run as root (the memory read is the superuser's), opens
`/dev/lcdgfx` read-write and exercises the commands a kernel
built with `options LCD_GFX_EXT` adds (`sys/sys/lcdgfx.h`; the PICOCALC_SD
kernel carries the option). On any other kernel it prints
`LCDEXTTEST SKIP` and exits 2.

## Stages

1. **Read framing.** Six known pixels go to the last line of the controller's
   memory, which the glass does not show. The reply of the memory read is
   printed byte for byte, and every lead of 0 to 15 bits under which it
   decodes to those pixels is reported, at 24 reply bits a pixel under
   COLMOD 0x66 and 16 under 0x55, the sizes the board returned. The kernel decodes with
   `LCDG_RD_LEAD_BITS`; a different lead is a failure and the printed bytes
   are the evidence for changing it. The stage runs under COLMOD 0x66 and
   0x55.
2. **Write and read-back.** A pseudo-random pattern fills all 480 lines
   through one window, is read back whole, and every pixel is compared on its
   five, six and five bits. The first five differences are printed with their
   coordinates, then the count and the write and read times.
3. **Sprite.** A 16 by 16 disc with a transparent index is drawn over the
   pattern at line 400 and read back: disc pixels hold the palette color,
   keyed pixels the pattern.
4. **Scan-line wait.** Twenty `LCDG_WAITLINE` calls for each of lines 0,
   100, 200, 300 and 400; every call must return a line inside its span.
5. **Hidden lines.** With a 480-line scroll area the scroll start steps
   through every second line, each step after a wait for the scan line, so
   the glass shows lines 320 to 479 moving through.
6. **Power.** Idle, display off and sleep for a second each; a fill in sleep
   must fail with EBUSY; after the wake, line 200 must still hold the
   pattern.
7. **Dithering probe.** `LCDG_DITHER` without and with MODESEL (B9h), each
   followed by a raw read of its pixels. The two replies are printed and
   compared; the stage reports and does not fail.

`lcdexttest shot file` writes the text console's screen to `file` as a
binary PPM, reading the console's ring from the line `LCDG_XINFO` reports.

## What the operator sees

The panel shows the pattern, scrolls it, shifts to eight colors, goes blank
for about two seconds and returns; the console text reappears at the close.
The terminal that started the program receives one line per stage and
`LCDEXTTEST OK` (exit 0) or `LCDEXTTEST FAILED` (exit 1).

A board run is opt-in. The host gate for the same commands is
`lcdgfx_ext_test` in `check-picocalc-host`.
