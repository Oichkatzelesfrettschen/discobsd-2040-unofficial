# tiledemo -- tile atlas workloads and raw key events

tiledemo exercises the tile atlas of `/dev/lcdgfx` through the tile library
(`games/gametile.h`) and the console's raw key events (`LCDIOC_KEYEVS`,
`LCDIOC_GETKEY` in `sys/lcdcons.h`).

## What it draws

It uploads twelve generated tiles, then runs three workloads on a 40 by 40
grid of 8x8 tiles, each timed against the bus time predicted for RGB666 at
12.5 MHz (122.9 us per tile, 7.0 us per window):

- repaint: the whole grid in 40 windows;
- pan: a 40 by 20 view of a hashed world slid one column per step for 88
  steps, about eleven of twelve cells changing per step;
- ring: 120 steps that rewrite the row that left the screen and move the scroll
  start by one tile row, so the grid scrolls vertically.

## What the operator sees

A tiled pattern fills the panel, slides sideways, then scrolls vertically. The
run holds the last frame for two seconds and the console text returns when the
device closes. Pressing Esc after the start ends the pan and ring loops early.
Standard output receives one timing line per workload (frames, total and
per-frame microseconds, predicted per-frame microseconds, percent of
prediction), a line of key counts (pressed, hold, released, lost) and then
`TILEDEMO OK` (exit 0) or `TILEDEMO FAILED` (exit 1) after a `FAIL <call>`
line per failed call. The program puts its tty in raw mode without echo and
restores it before it returns.

## Where it runs

Run it from a login on the panel console with the keyboard. The key session
is an ioctl on standard input that the panel console driver
(`sys/arch/rp2040/dev/lcdcons.c`, `lcd_keyevs`) accepts only for an open file
of the console tty that is open for reading, in the foreground process group,
and in raw or cbreak mode. From a serial login, standard input belongs to the
UART or USB driver, whose ioctl returns ENOTTY for `LCDIOC_KEYEVS`, so the
program prints `FAIL LCDIOC_KEYEVS start` and ends with `TILEDEMO FAILED`;
the drawing and timings still run. That outcome follows from the source and
lacks a board run behind it. A board run takes over the panel, so it needs the
owner's opt-in.
