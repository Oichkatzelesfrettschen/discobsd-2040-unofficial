# lcdgfxtest -- PicoCalc pixel device speed and restore check

lcdgfxtest opens `/dev/lcdgfx` (`sys/arch/rp2040/dev/lcdgfx.c`), draws a fixed
set of shapes, times each with `gettimeofday(2)` and prints the measured
microseconds beside the bus-time prediction of
`docs/research/picocalc-calculator-and-gui.md` with their ratio. The ratios are
the measurement; the program fails only when a system call fails.

## What it draws

Each pass runs twice, first under COLMOD 0x66 (RGB666, the console format),
then under COLMOD 0x55 (RGB565), and restores RGB666 at the end. Per pass:
a full 320x320 black fill; 100 one-pixel green lines of 320 pixels; a row of
40 gradient tiles of 8x8 across the top; eight 16x16 one-bit sprites with a
blue 16x16 fill under each; a white diagonal line; a magenta 291x191 frame;
a cyan circle of radius 100; and 60 scroll starts, which move the visible
window without changing pixels.

## What the operator sees

The panel clears to black and the shapes accumulate in the order above, the
second pass redrawing over the first. A finished run holds the final frame for
two seconds, closes the device, and the console text reappears (the last close
repaints it). The terminal that started the program receives the timing
table, then `LCDGFXTEST OK` (exit 0) or `LCDGFXTEST FAILED` (exit 1) with a
`FAIL <call>` line per failed call. `cannot open /dev/lcdgfx` (exit 1) means a
non-PICOCALC kernel or another opener. A second open of the device must fail
with EBUSY, and the program records a success of that second open as a failure.

With the argument `crash` the program fills the panel red and kills itself
with SIGKILL while the device is open. The operator confirms the console text
returns, which exercises the restore on process exit. That outcome is visible
on the panel alone; the shell reports only the kill.

## Where it runs

The program draws through the device and prints through its standard output,
so a serial login (UART or USB CDC) works: the panel console is silent while
the device is open (`lcdgfx.c` header comment), the timing table goes to the
serial terminal, and the panel shows the drawing. The source reading above
does not rest on a board run; no run from a serial login is recorded. A board
run takes over the panel and needs a person watching it, so it needs the
owner's opt-in.
