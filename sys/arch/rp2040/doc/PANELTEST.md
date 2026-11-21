# PicoCalc panel register test

The PicoCalc's ST7365P panel controller takes its gamma, power, VCOM,
frame-rate and inversion settings from Command Table 2 (ST7365P_SPEC_V1.0,
section 9.3). The vendor ILI9488P sequence that `init_seq` of
`st7365p.c` carries writes those registers without CSCON F0h C3h/96h,
which page 245 documents as the enable of command 2, and page 246 gives
CSCON's power-on and reset defaults as N/A. Which of those writes take
effect, and whether other settings render small text more crisply, are
questions for the board. The panel test answers them there: the PICOCALC kernel (`options LCD_PANELTEST`) programs
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
  `/dev/lcdgfx` is open, so the console stays idle while the panel is reprogrammed or read, and the last close programs set 0 again.
- The readback sends each read command with D/CX low and clocks the reply
  at 3.125 MHz, inside TSCYCR (150 ns, page 54). The first twelve entries
  are Command Table 1 and ID reads (04h, 09h, 0Ah-0Fh, DAh-DCh, D3h), the
  calibration: a dead SDO line reads all zero or all one there. Entries
  13-20 read Command Table 2 registers through SPI Read Control (FBh,
  page 246, SPI_REN in bit 4, the parameter number in bits 3-0). If SPIRC is ignored those reads clock zero bytes into the register as
  parameters, so `paneltest` programs its set again after every readback.
- `machdep.picocalc.panel_scan` (superuser, read only, same EBUSY rule)
  samples GSCAN (45h, page 201), the line the controller is scanning, and
  returns a `struct picocalc_panel_scan`; the frame-rate measurement
  below states its decode, resolution and error bound.
- Entries 21-28 of the readback are the Command Table 2 read control:
  each unlocks command 2, writes the mark values C5h 2Ah and B1h 50h 1Ch
  (`PANEL_MARK_*`, `machine/picocalcvar.h`) and then reads, changing one
  condition from the marked C5h read through FBh 10h: no FBh, parameter
  number 1, FBh before the unlock, command 2 locked again (CSCON 3Ch,
  69h) before FBh, FBh inside the read's chip select, then B1h through
  parameter numbers 0 and 1. The `how` byte of each entry names its
  variant. Rewriting the marks before every read keeps the variants
  independent of what an earlier read clocked into the registers. Set 27
  programs the same marks, so they also stand in the panel between
  readbacks.
- Every PicoCalc kernel programs the panel at startup with the unlock,
  `init_seq` and the 78 Hz frame table (`init_unlock`, `init_frame`),
  which the board runs at 74.94 Hz. The full kernel also stores one
  post-sequence pointer and length per numbered set, plus selectors for
  its preamble and base. Set 0 sends the startup bytes, so the last close
  of `/dev/lcdgfx` returns the panel to its startup state; set 31 sends a
  NOP on either side of `init_seq`, the locked base that startup sent
  until the frame rate was chosen and that set 0 named then. In the
  results below dated 2026-11-19 or earlier, "set 0" and "shipped" mean
  that locked base. The bare Pico configurations use their own console
  drivers.

## Running it

Boot the PICOCALC kernel, which carries `LCD_PANELTEST`, with `paneltest` in the
root (the PicoCalc SD root installs it), log in, `su`, run `paneltest`.
Keys: right and left arrows (or `n`/`p`, or `d`/`a`) step the set, down and
up arrows (or `s`/`w`, or `1`-`7`) step the pattern, `r` reads back, `f` measures the
frame rate, `q` quits. The top left corner of every pattern (RAM line
160 under pattern 7, the first line on the glass there) shows the set
number, its name, the pattern and the rate last measured under the set.
`/var/log/paneltest.log` keeps every set change, readback and
measurement. A set stays in force until the last close of `/dev/lcdgfx`
programs set 0 again.

Pattern 1 shows, below the text sample, 04h, 09h and D3h raw and shifted
left one bit, the 09h fields by name, 0Ah, 0Bh and 0Ch decoded from
their first byte, the last measurement, and every raw entry in two
columns as `cmd fbNN hH: bytes`. `usr.bin/paneltest/panel_decode.h` holds
the decode; `panel_decode_test` in `check-picocalc-host` checks it.

| Pattern | Draws | Experiment |
| --- | --- | --- |
| 1 | console text sample and the latest readback | text crispness, readback |
| 2 | black/white halves, 1-pixel checker, columns, rows | E0, E3 |
| 3 | 32 gray bars, full black, full white | E0, E2 |
| 4 | 1-pixel red, green, blue, white columns; red beside white | E6 |
| 5 | 50% gray with a 1-dot checker square | E1, E5 |
| 6 | white square on 50% gray | E3 crosstalk |
| 7 | ten color bands, 480-line scroll area started at line 160 | E7 |

## Frame-rate sets and measurement

FRMCTR1 (B1h, page 219) gives the normal-mode frame rate as
10^7 / ((168 + RTNA + 32 (15 - FRS)) (480 + VFP + VBP)). BPC (B5h, page 222) lacks a documented reset value, so every frame-rate set writes
B5h 02h 02h 00h 04h before SLPOUT, which fixes the second factor at 484.
The formula omits DIVA; the 24 Hz set relies on DIVA = 01b (Fosc / 2)
halving a 48 Hz timing, an inference. `st7365p.c` asserts at compile
time that each set's bytes give its nominal rate within 0.25 Hz.

| Set | B1h bytes | FRS | DIVA | RTNA | Line units | Formula |
| --- | --- | --- | --- | --- | --- | --- |
| 21 | 71h 06h | 7 | Fosc/2 | 6 | 430 | 48.05 Hz, halved 24.02 Hz |
| 22 | 50h 1Ch | 5 | Fosc | 28 | 516 | 40.04 Hz |
| 23 | A0h 10h | 10 | Fosc | 16 | 344 | 60.06 Hz |
| 24 | C0h 0Bh | 12 | Fosc | 11 | 275 | 75.13 Hz |
| 25 | E0h 1Eh | 14 | Fosc | 30 | 230 | 89.83 Hz |
| 26 | F0h 04h | 15 | Fosc | 4 | 172 | 120.13 Hz |
| 28 | A0h 04h | 10 | Fosc | 4 | 332 | 62.23 Hz |
| 29 | C0h 01h | 12 | Fosc | 1 | 265 | 77.96 Hz |
| 30 | F0h 00h | 15 | Fosc | 0 | 168 | 122.98 Hz |

Set 0 carries the bytes of set 29.

Sets 28-30 aim the formula above a target by the ratio the board measured
at the same FRS (result 3 of the 2026-11-18 run): 60 / 0.965, 75 / 0.962
and, for 120, the formula's maximum at these porches (168 line units).

Sets 21-26 and 28-30 unlock command 2 first. Set 23 writes the datasheet defaults,
so it is the reference the other rates compare against. `init_seq`
(set 31) writes B1h A0h alone (RTNA left as reset) and no B5h; sets 14
and 15 write B1h 00h alone, 31.1 Hz at RTNA 10h and 484 lines.

The kernel reads GSCAN back to back at 3.125 MHz, about 10 us a read,
and times each read with the 1 us system timer, until nine wraps or
400 ms. A wrap is a fall of more than 64 decoded lines. The reply's line
number is decoded as bits 22-7 of its three bytes, the one-bit dummy
clock the board's 24- and 32-bit reads carry; that decode is unmeasured
for 45h, so paneltest also prints the line count without the shift.

- Rate: the wraps - 1 frames between the first and last wrap over the
  time between them. Each wrap's time is that of the read after it, late
  by at most one read interval plus the read in progress, so the span is
  off by at most 2 g + 2 us, where g is the largest interval between two
  reads in the sample (interrupts stay enabled and lengthen g) and 2 us
  covers the timer's 1 us steps. paneltest prints the bound
  rate x (2 g + 2) / (span - 2 g - 2): with g near 11 us and eight
  frames at 60 Hz it is about 0.02 Hz, and it grows only when an
  interrupt stretches a gap. The 12 MHz crystal's tolerance adds parts
  per million.
- Resolution: hundredths of a hertz in the printout; eight frames at
  24 Hz need 333 ms, inside the 400 ms bound, while fewer than two wraps leave the rate blank.
- Line count: the largest line seen plus one. A read interval longer
  than a line period (17 us at 120 Hz and 484 lines) can miss the last
  lines; paneltest prints that slack, the read interval x rate x lines,
  rounded up.

## Predictions, recorded before the board run

The experiment numbers refer to the controller report
(`st7365p-crispness-report.md` of the local reference set); its section 7
gives each measurement. Each line states what the outcome means.

1. Readback calibration: 04h returns a nonzero ID after the one-bit dummy
   shift, 0Ah reads 9Ch with the display on and sleep out (BSTON, SLPOUT,
   NORON, DISON per page 151), 0Bh reads 48h and 0Ch reads 66h, in the
   first byte if the SPI read skips the dummy cycle (page 94) and in the
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

## Predictions for the frame-rate sets and read control, recorded before their board run

9. GSCAN answers: under set 0 the line count reads 484 or close to it
   with the shift (about 242 without), and the rate reads 60.06 Hz if the
   reset RTNA is 10h and the reset porches are 2 lines. All-zero or
   all-one samples (no wraps, or every read bad) mean GSCAN returns unusable data, and the frame-rate rows below are void.
10. Command 2 gating by rate: if the unlocked sets 22-26 measure their
    formula rates within the printed bound, unlocked Command Table 2
    writes take effect; if every set measures set 0's rate, they do not,
    or B1h needs more than the unlock.
11. DIVA: set 21 measures 24.02 Hz if DIVA divides the frame clock as the
    formula's omission implies, 48.05 Hz if DIVA is inert in this mode.
12. Locked and unlocked 31 Hz, the deciding pair: sets 14 (locked) and
    15 (unlocked) write the same FRMCTR1 value, B1h 00h, and differ only
    in the CSCON unlock, so their measured rates decide the gating that
    the gray levels of result 4 suggest. Set 15 at 31.1 Hz with set 14
    at set 0's rate means the lock holds and the shipped writes are
    discarded; both at 31.1 Hz means command 2 is live at power-on. Both
    sets stay in the list for this comparison.
13. Command Table 2 read control: a variant that returns 2Ah for C5h (in
    the first byte, or as 2Ah shifted right one bit, 15h, followed by a
    set top bit in the next) is a working read method, and the B1h
    variants then read 50h and 1Ch. All eight returning zero is a
    recorded negative for these conditions: FBh SPI_REN with parameter
    numbers 0 and 1, before and after the unlock, locked, and in or out
    of the read's chip select, at 3.125 MHz in four-wire SPI.

## Predictions for the compensated frame-rate sets, recorded before their board run

14. Sets 28, 29 and 30, measured with `f`: 60.05 Hz, 75.0 Hz and 119.8 Hz,
    each within 0.6 Hz, if the oscillator ratio of a set depends on FRS
    alone (0.965 at FRS 10, 0.962 at FRS 12, 0.974 at FRS 15, from sets
    23, 24 and 26). A set that lands at its formula rate times a ratio
    outside 0.960 to 0.976 means the ratio also depends on RTNA, and the
    compensation needs a measurement at the exact RTNA.
15. Set 30 writes RTNA 0, the smallest line period the register holds.
    Hypothesis: the panel still shows a stable picture; the falsifier is
    a torn, dim or rolling picture under pattern 1, in which case the
    highest usable rate is set 26's measured 117.04 Hz.

## Predictions for the startup sequence at 75 Hz, recorded before its board run

The startup sequence takes the bytes of set 29, which measured 74.94 Hz
with a picture (results of the second 2026-11-19 kernel below).

16. After a boot and before any set change, `f` reads 74.9 Hz within
    0.3 Hz under set 0, and set 31 reads 57.95 Hz within 0.2 Hz.
    Falsifier: 57.9 Hz under set 0, which would mean the startup path
    sends other bytes than `st7365p_set(0)` (the emulated trace shows the
    same 75 bytes before SLPOUT).
17. The rate holds across what a session does to the panel: after
    `lcdexttest`, whose dithering probe locks command 2 again (CSCON 3Ch,
    69h) and whose power test sends SLPIN and SLPOUT, `f` under set 0
    still reads 74.9 Hz. Falsifier: 57.9 Hz after it, which would show
    that a lock or a sleep returns FRMCTR1 to the gated value;
    `lcd_gfx_dither()` would then stop locking, or the wake path would
    send the frame table again.
18. The console and the pixel device draw as before; `lcdgfxtest`,
    `lcdexttest` and `tiledemo` pass. The scan-line wait of
    `lcdexttest` keeps its 100 of 100, since its timeout is a count of
    samples and no constant holds the frame period.

## Results, 2026-11-17

Board run on the PICOCALC kernel of branch head b47cda40 (sets 0-20, no
frame-rate measurement or read control; those arrived later).

1. Calibration (prediction 1): 0Ah 9Ch, 0Bh 48h and 0Ch 66h in the first
   byte, as predicted for a read without a dummy cycle, so SDO reaches
   GP12. The multi-byte reads carry one dummy clock: D3h reads
   `00 3b cb 00`, which shifted left one bit is `00 77 96`, the ST7796
   family ID; 09h reads `d2 31 d2 00 00`, shifted `a4 63 a4 00`: booster
   on, MX, BGR (MADCTL 48h), 18-bit pixels, sleep out, normal mode,
   scroll on, inversion on, display on. 04h reads `ff ff ff ff` and DAh,
   DBh, DCh read `ff ff`; the prediction was a nonzero ID, and all-one
   bytes are not distinguishable from an idle line.
2. MADCTL (prediction 7, readback part): set 19 reads 0Bh 40h and 09h
   shifted `a0` (BGR clear); set 20 reads 0Bh 08h and 09h shifted `84`
   (MX clear). Sets 1 to 18 read as set 0 in every Command Table 1
   register.
3. Command Table 2 through FBh (prediction 3): B4h, B1h, C5h, B7h and
   E0h read 00 under every set, locked or unlocked. No positive control
   existed, so the observation is that this read method returned zero,
   not that the registers are unreadable. Entries 21-28 add the control.
4. E1 gating (prediction 2), by eye: set 15 (31 Hz, unlocked) flickers;
   set 16 (VCOM 3Fh, locked) shows striping. Sets 13 and 14 were not
   reported by eye. Hand-held 60 fps camera clips of pattern 5, cropped
   to the screen, measured no flicker in any set (0.07% to 0.39%
   frame-to-frame variation, a weak instrument with white clipped), and
   in two recording runs the unlocked sets 13 and 15 render the 50% gray
   lighter relative to white (gray/white 0.86 and 0.80, then 0.92 and
   0.93) than the locked sets 14 and 16 and set 0 (0.71 and 0.70, then
   0.83, 0.88 and 0.86). Inference, not proof: Command Table 2 writes
   are discarded while locked and act after the CSCON unlock. Clipped
   white compresses the ratios and exposure differed between runs.
5. E7 lines (prediction 8): a photograph of pattern 7 shows bands 5-9
   in the top half and undrawn RAM noise below, as predicted.

## Results, 2026-11-18

Board run on the PICOCALC kernel of main bff303aa (sets 0-27, the first
with the scanline sampler and the read control).

1. Scanline decode: every sample set gives 484 lines with the one-clock
   shift (raw maximum `00 f1 80`), which is 480 + VFP + VBP with the
   porches the frame-rate sets write, and 242 without it. GSCAN answers on
   this link with one dummy clock, as 04h, 09h and D3h do.
2. The wrap rule miscounted. Repeated samples of one set reported rates
   from 58 to 155 Hz, each with an error bound under 0.2 Hz. The span from
   the first to the last counted wrap, divided by one frame period, is an
   integer or an integer plus or minus 0.53 (once 0.26) in every sample;
   256 / 484 = 0.529 and 128 / 484 = 0.264. The sampler counted a wrap on
   any fall of more than 64 lines, and a read that meets the line counter
   during the 255 to 256 or 127 to 128 carry returns one low line. No
   fraction near 0.79 (383 to 384) occurred in 85 samples. The reads are
   sound and the rule was the defect; `st7365p_scan.h` now requires the
   sample after a fall to continue from the low line and counts the
   samples it sets aside (`rejected`). The carry explanation is an
   inference from the span fractions; the kernel that carries the new rule
   has not sampled the board.
3. Frame period, recovered from 8 to 10 samples per set by fitting one
   period to the spans (mean residual 0.001 to 0.004 frame):

   | Set | Formula | Recovered | Ratio |
   | --- | --- | --- | --- |
   | 0 shipped | 60.06 Hz | 57.95 Hz | 0.965 |
   | 1 vendor | 60.06 Hz | 57.97 Hz | 0.965 |
   | 14, B1h 00h without CSCON | 31.0 Hz | 58.00 Hz | not applied |
   | 15, B1h 00h after CSCON | 31.0 Hz | 30.18 Hz | 0.974 |
   | 21, 24 Hz, DIVA 01b | 24.02 Hz | 23.33 Hz | 0.971 |
   | 22, 40 Hz | 40.04 Hz | 38.72 Hz | 0.967 |
   | 23, 60 Hz | 60.06 Hz | 57.94 Hz | 0.965 |
   | 24, 75 Hz | 75.13 Hz | 72.30 Hz | 0.962 |
   | 25, 90 Hz | 89.83 Hz | 86.51 Hz | 0.963 |
   | 26, 120 Hz | 120.13 Hz | 117.04 Hz | 0.974 |

   The FRMCTR1 formula of page 219 holds on this controller with its
   oscillator 2.6% to 3.8% slow, and DIVA 01b halves the rate, which the
   printed formula omits.
4. E1 gating, measured for B1h: the write is discarded without the CSCON
   unlock (set 14 keeps the default period) and acts after it (set 15).
   `st7365p.c` never sends F0h, so its B1h write has never acted; whether
   its other Command Table 2 writes (gamma, power, VCOM, inversion, entry
   mode) share the gate is not measured, and the set 16 striping of
   2026-11-17, seen by eye, would mean C5h does not.
5. Read control (prediction of 2026-11-17, entries 21-28): B1h and C5h
   read 00 in all eight variants in all 28 sets, with the mark values
   written just before each read. Section 9.3 of the datasheet prints read
   rows only for D2h and D3h in Command Table 2; B0h-B7h, C0h-C6h, E0h-E8h
   and F0h have write rows only. The zeros agree with the datasheet. The
   positive control remains open: entries 29-33 read D2h, and D3h through
   FBh 10h, 11h and 12h.
6. Board tests on the same kernel passed: divtest, fptest, forktest,
   sigtest, filetab, inotab, textcrc, streamtest, swapmaptest, tartest,
   romprobe, clistbound, proctab and evactest as root, lcdgfxtest,
   windowtest, and stdiocap with each operation. tiledemo's key session
   was refused from the UART terminal, as its header says; it was not run
   from the console.

## Results, 2026-11-19

Kernel PICOCALC_SD #1 4399 (main f9e9623e2), SPI mode 3, started by the
loader from the card; paneltest on the UART terminal as root.

1. Readback under sets 0, 2 and 3 is the same as on 2026-11-18 in every
   entry it shares: 09h `d2 31 d2 00 00`, 0Ah 9Ch, 0Bh 48h, 0Ch 66h,
   D3h `00 3b cb 00`. The frame format of the link changed and the replies
   did not.
2. Scan: 57.92 Hz +-0.03 (set 0), 57.88 Hz +-0.03 (set 2), 57.97 Hz +-0.02
   (set 3), 484 lines, nine wraps each, with 5, 5 and 3 samples rejected.
   The wrap rule sets the carry misreads aside on the board and the rate
   agrees with the 57.95 Hz recovered by hand on 2026-11-18.
3. NVM status (D2h): `00 00 00 80 00` plain and through FBh 10h.
4. SPI Read Control on a readable register: D3h through FBh 10h returns
   `00 00 00 00`, through 11h `00 00 00 00` and through 12h `77 77 77 ff`.
   FBh selects one parameter by number and the read repeats it, so the
   control acts on this link, and the zeros of entries 13-28 are what the
   Command Table 2 registers answer.
5. Memory read of the white pixel: `20 ff ff ff ff`, then `fe ff ff ff ff`:
   under 55h the reply is the 16-bit value and does not show the widening.
   The entry selects 66h before its read from the next kernel on.
6. Self-diagnostic (0Fh): `00` on the first readback after the open, `c0`
   on every later one.

Not done: the by-eye comparison of sets 0, 2 and 3.

Kernel PICOCALC_SD #1 4401 (main ab9ac8e7a), the same day; its pixel entry
reads under 66h:

7. Memory read of the white pixel: `fe | f8 fc f8` under sets 0, 2 and 3.
   A sixteen-bit pixel is stored with the low bit of red and blue clear
   whether or not the set unlocks Command 2, and whichever gamma it
   carries. EM (B7h) through FBh 10h reads `00` under each set. Every set
   writes EPF 11; the other three EPF values were not tried.
8. Photographs of sets 0 to 3 at 640 by 480 with glare do not separate
   them; the by-eye comparison needs full-size frames.

Kernel PICOCALC_SD #1 4421 (main 23ae068cb), the compensated frame-rate
sets (predictions 14 and 15):

9. Scan: set 28 59.99 Hz +-0.03, set 29 74.94 Hz +-0.06, set 30
   119.66 Hz +-0.08, each 484 lines and nine wraps with 4, 4 and 3
   samples rejected. Prediction 14 gave 60.05, 75.0 and 119.8 Hz; all
   three are inside its 0.6 Hz, so the ratio follows FRS at these RTNA
   values.
10. Set 30 shows a black glass while the scan runs at its predicted
    rate, and the picture returns on leaving the set. Prediction 15's
    hypothesis is false: RTNA 0 is unusable on this panel, and the
    fastest rate with a picture stays set 26's 117.04 Hz.
11. Set 29 shows a stable picture. It became the startup sequence.
