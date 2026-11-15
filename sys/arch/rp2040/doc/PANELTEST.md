# PicoCalc panel register test

The PicoCalc's ST7365P panel controller takes its gamma, power, VCOM,
frame-rate and inversion settings from Command Table 2 (ST7365P_SPEC_V1.0,
section 9.3). `st7365p.c` writes those registers from the vendor ILI9488P
sequence but never sends CSCON F0h C3h/96h, which page 245 documents as the
enable of command 2, and page 246 gives CSCON's power-on and reset defaults
as N/A. Whether the shipped writes take effect is therefore open, and so is
whether other settings render small text more crisply. The panel test
answers both on the board: a kernel built with `-DLCD_PANELTEST` programs
one of a fixed list of register sets on request and reads the controller's
read commands back over SPI1 RX (GP12, the panel's SDO), and `paneltest`
draws the calibration patterns.

## Mechanism

- `sys/arch/rp2040/dev/paneltest_sets.h` lists the register sets as
  `X(name, pre, base, post)`. A set is a hardware reset, the byte tables
  PRE, BASE and POST, SLPOUT, the 320-line scroll definition and DISPON,
  then COLMOD 66h so `/dev/lcdgfx` keeps its pixel format. Every byte comes
  from a table in `st7365p.c`; user space chooses only the set number. No
  set sends an NVM command (D0h, D1h) or a value outside the datasheet's
  documented field range.
- `machdep.picocalc.panel_variant` (superuser) reprograms set N when the
  written value is `0x5054 << 16 | N`; any other value returns EINVAL.
  `machdep.picocalc.panel_read` (superuser, read only) returns a `struct
  picocalc_panel_read` (`machine/picocalcvar.h`). Both return EBUSY unless
  `/dev/lcdgfx` is open, so the console draws nothing while the panel is
  reprogrammed or read, and the last close programs set 0 again.
- The readback sends each read command with D/CX low and clocks the reply
  at 3.125 MHz, inside TSCYCR (150 ns, page 77). The first twelve entries
  are Command Table 1 and ID reads (04h, 09h, 0Ah-0Fh, DAh-DCh, D3h), the
  calibration: a dead SDO line reads all zero or all one there. The last
  eight read Command Table 2 registers through SPI Read Control (FBh,
  page 246, SPI_REN in bit 4, the parameter number in bits 3-0). If SPIRC
  has no effect those reads clock zero bytes into the register as
  parameters, so `paneltest` programs its set again after every readback.
- Kernels without the define compile none of this; their panel sequence is
  unchanged (`panel_program()` runs the same `init_seq` bytes).

## Running it

Load `discobsd-paneltest.uf2` from the loader menu (`/pico1-apps`), log in,
`su`, run `paneltest`. Keys: `n`/`p` set, `1`-`7` pattern, `r` readback,
`q` quit. `/var/log/paneltest.log` keeps every set change and readback.
Return to the normal kernel by choosing `discobsd.uf2` in the loader menu.

| Pattern | Draws | Experiment |
| --- | --- | --- |
| 1 | console text sample and the latest readback | text crispness, readback |
| 2 | black/white halves, 1-pixel checker, columns, rows | E0, E3 |
| 3 | 32 gray bars, full black, full white | E0, E2 |
| 4 | 1-pixel red, green, blue, white columns; red beside white | E6 |
| 5 | 50% gray with a 1-dot checker square | E1, E5 |
| 6 | white square on 50% gray | E3 crosstalk |
| 7 | ten color bands, 480-line scroll area started at line 160 | E7 |

## Predictions, recorded before the board run

The experiment numbers refer to the controller report
(`st7365p-crispness-report.md` of the local reference set); its section 7
gives each measurement. Each line states what the outcome means.

1. Readback calibration: 04h returns a nonzero ID after the one-bit dummy
   shift, 0Ah reads 9Ch with the display on and sleep out (BSTON, SLPOUT,
   NORON, DISON per page 151), 0Bh reads 48h and 0Ch reads 66h, in the
   first byte if the SPI read has no dummy cycle (page 94) and in the
   second if it follows the command table's dummy parameter. All-zero or
   all-one bytes mean SDO is not reaching GP12, and every later readback
   is void.
2. E1 gating: if sets 14 (31 Hz locked) and 16 (VCOM 3Fh locked) flicker
   or shift gray like sets 15 and 13 (unlocked), command 2 is live at
   power-on and the shipped writes act. If only the unlocked sets change
   the image, the shipped writes are discarded today, and set 2 (unlock)
   differs visibly from set 0.
3. SPIRC readback: B4h through FBh 10h reads 02h under set 0 if the
   shipped write acts, the datasheet default 01h if it is gated. E0h
   parameter 0 reads 00h under set 0 and F0h under set 3.
4. E2 gamma: set 3 (datasheet default) gives a black frame at or below
   set 2's; set 4 reproduces set 2's black level if V63P alone lifts it.
5. E3 inversion: no firm prediction; sets 6, 7 and 2 (column, 1-dot,
   2-dot) differ in shimmer on pattern 2.
6. E5 VCOM: least flicker on pattern 5 near set 11 (1Ch, the datasheet
   default) rather than set 2 (00h, the shipped value).
7. E6 subpixel: under set 0 (MADCTL 48h) a red column lights the leftmost
   stripe of its pixel; set 19 (MADCTL 40h) swaps red and blue; set 20
   (MADCTL 08h) mirrors the columns without changing the stripe order.
8. E7 lines: pattern 7 shows bands 5-9 in the top half and undrawn RAM
   below; no more than 320 lines are visible at any scroll start.

A board run records the set, the pattern, a photograph at fixed exposure
for each judged item, and `/var/log/paneltest.log`.
