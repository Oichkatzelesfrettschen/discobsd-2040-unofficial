# ST7365P register reference for the PicoCalc panel

The PicoCalc's 320x320 panel answers RDID4 (D3h) with `00 77 96`, the value
the ST7365P datasheet itself prints for that command (p. 239, ll. 13751-13753), on
four-line SPI from the RP2040's SPI1: GP10 SCK, GP11 TX (panel SDA), GP12 RX
(panel SDO), GP13 chip select, GP14 D/CX, GP15 reset. `st7365p.c` writes at
12.5 MHz; the panel test branch reads at 3.125 MHz. This file gives one entry
per command of Command Table 1 and Command Table 2 with fields, defaults,
rules, formulas and read format, says for each command whether the driver
sends it, the panel test reads it, the board run measured it, or nothing in
the tree uses it, and answers the hardware questions a driver author meets
on this board (tearing-effect signal, vertical sync, frame rate, visible
area, power and image modes).

## Sources

Citations of the datasheet give the printed page and the line of the text
extraction, as "p. 145, l. 7380". The printed page equals the PDF page. The
page of a line is the page whose footer ("V1.0 Page N of 287") follows it in
the extraction. The extraction drops figures, flow charts and most diagrams;
where a statement here rests on a figure, the entry says it was read from
the PDF page image, and where a figure was not read, the entry says so.

| Source | Where | sha256 |
| --- | --- | --- |
| Sitronix ST7365P datasheet V1.0, 2023/02, 287 pages (PDF) | notes repository, `docs/picocalc/ST7365P_SPEC_V1.0.pdf` | `8971993df5a55c72180f700f9cd04dcb2a4305b86a87c11ba3de401d3eadfb79` |
| Its `pdftotext -layout` extraction, 16,645 lines | notes repository, `docs/picocalc/ST7365P_SPEC_V1.0.txt` | `ec6e56b6da66825dcee8a220233d8a063bbe01e793ed1337c567fa7ca99d6fca` |
| Section index of the datasheet | notes repository, `docs/picocalc/ST7365P-INDEX.md` | `6544d03893cd2564407e42a8f90dd6860dca6c65c8160f891b6567d535e1bb47` |
| ClockworkPi mainboard V2.0 schematic (PDF, one page) | notes repository, `docs/picocalc/clockwork_Mainboard_V2.0_Schematic.pdf` | `b6fd79c6cbe9b0825525211d0f2e3e0fd269a2f9a6fc22580452de3012156ede` |
| Its text extraction | notes repository, `docs/picocalc/clockwork_Mainboard_V2.0_Schematic.txt` | `03086d8bda88a3bf51ed8a0dcce17159ccb033de4a5c7dc21d1309941a8cc5f6` |
| Earlier controller study (crispness report) | local reference set, `picocalc-font-options/st7365p-crispness-report.md` | `1806a7597842b2a8d1543df60d0321e3f708395d9098980b6d70eb258250f41b` |
| `sys/arch/rp2040/dev/st7365p.c` at 793d34329 | this tree | `b96c3254e1a3dc8ca300afaddf39fb48d4a55d7a38bf366a52e155a1cdf42eb3` |
| `sys/arch/rp2040/dev/lcdgfx.c` at 793d34329 | this tree | `c18820bafb69c8da7dfb74ab93cd341e842ca66cc0259b029aba8d3c88c4efd1` |
| `sys/arch/rp2040/dev/lcdterm.c` at 793d34329 | this tree | `d31540c70f247f5b718a8016185ae6b745422bf48d3a827396a681c1efc9aee2` |
| `sys/arch/rp2040/doc/PICOCALC.md` at 793d34329 | this tree | `7324c5cc8fb44e29def0dc0777918e50a76a323ffaa0d3152848b55353d4744b` |
| `sys/arch/rp2040/doc/PANELTEST.md` | branch `rp2040-picocalc-panel-register-test` at b47cda40 | `c7c00ada0cba26e2036276fc737d475d498142905a0e0c09b0004e543c44ee1f` |
| `sys/arch/rp2040/dev/paneltest_sets.h` | same branch and commit | `c9f57362816429c6818824bf6e228a049436d2d92da4ededdd9bb8cb5d11c5f8` |
| `usr.bin/paneltest/paneltest.c` | same branch and commit | `4a7e3de719d2844223cde72633f68020d36c2980ae94d762137956c4a8fdf74e` |
| `sys/arch/rp2040/dev/st7365p.c` with the panel test | same branch and commit | `e1945a0e4cc2cce761b4452414a4cea6954a3978cf86b55482aee793f12eeacf` |
| Vendor PicoMite `picomite/SPI-LCD.c` (ST7796SP and ILI9488P sequences) | local reference set, ClockworkPi `pico_multi_booter` staging tree | `17e1cbe48ae64d606722bb982960254f9b802c43fb09ff1342bcfaf198c95eb2` |
| Vendor `sd_boot/lcdspi/lcdspi.c` | same staging tree | `d5c9b8735e41c081636fef39a82c09773074c6588d9836119a7bdb916bb0e3ab` |
| PicoCalc board run 2026-11-17, record README | orchestration run `picocalc-2026-11-17`, outside version control | `d9e89214d945ffec7a68673db9d6d7980be5d84a0760f417f8ab871267a75b31` |
| Same run, `paneltest.log` (21 register sets, readback bytes) | same run | `a3e54863207cbd530c9ca6ab958f0dbe685152444987450ab7894fba8bd1ed9c` |
| Same run, photograph of pattern 7 (`photos/p7-set-current-b.jpg`) | same run; not committed | `3abe1c78a62e6a4d129df6beb7b4b56c4f05547a2165b76c7c23ad4a0c00643d` |

The board run used the PICOCALC kernel of branch
`rp2040-picocalc-panel-register-test` at b47cda40 (main f48e19c9 plus the
panel test). Driver line numbers below are those of `st7365p.c` at
793d34329 on main, written `st7365p.c:N`; panel-test line numbers are those of the
branch, written `st7365p.c@b47cda40:N`.

Evidence classes: **DOC** is stated by the datasheet or the schematic;
**DRV** is what the driver source sends; **BOARD** is observed on the
PicoCalc in the 2026-11-17 run; **INF** is an inference from those, with its
premises named. Status words per command: **SENT** (the driver sends it),
**READ** (the panel test reads it), **MEASURED** (a board observation bears on
it), **NOT IMPLEMENTED** (nothing in the tree uses it). The Command Table 2
readback of the panel test returned `00` for every register under every set
(BOARD), so READ for a Command Table 2 register records that the read was
attempted, not a value.

Datasheet misprints are quoted as printed with their line, not corrected in
place; section 7 lists them.

## 1. The link on this board

**Interface selection (DOC, schematic image).** The mainboard's 40-pin panel
connector J401 (0.5 mm, "0.5-40PBSX") ties IM0, IM1 and IM2 (pins 8-10) to
the ALDO3 rail and DB0-DB15 (pins 13-28) to ground, and routes SPI1_RX to SDO
(29), SPI1_TX to SDA (30), SPI1_SCK to WR/SCL (32), LCD_DC to DCX (33),
SPI1_CS to CS (34) and LCD_RST to RESET (11). IM[2:0] = 111 selects 4-line
SPI (p. 42, ll. 2054-2064), and the unused DB pins are tied as page 93 requires
(p. 93, ll. 4644-4647). The words at the connector pins are net labels on short
stubs; a net joins every place its label occurs, and the ALDO3 and ground
nets are also drawn as wires. The text extraction lists the labels on lines
16-48 of the schematic text but cannot show wires; the connector was read
from a 600 dpi render of the PDF around J401, and label occurrences were
counted with `pdftotext -bbox` over the whole sheet. The labels RD (pin 31)
and TE (pin 35) occur once each and their stubs end without a wire or
junction, so each net has a single node; LEDA, LEDK and LCD_BL each occur
twice (J401 or the keyboard controller, and the backlight LED driver). Pins
2-6 and 12 carry no-connect marks. The datasheet asks that an unused RDX be
fixed at VDDI or DGND (p. 42, ll. 2077-2079); whether the panel module ties it on
its own flex is not documented (INF).

**Write format (DOC).** A byte is sent MSB first, sampled on the rising edge
of SCL; D/CX low marks a command byte, high a parameter or pixel byte
(pp. 93-94, ll. 4663-4693). Chip select may rise between bytes; SCL and SDA are
ignored while CSX is high (p. 94, ll. 4714-4716). Releasing CSX after a whole
byte pauses the transfer, which resumes at the next byte when CSX falls
(p. 253, ll. 14565-14578); a CSX break inside a byte discards that byte
(p. 252, ll. 14510-14513); a new command after part of a parameter list keeps the
parameters already sent and drops the rest (p. 252, ll. 14514-14517).

**Write timing (DOC; DRV).** TSCYCW is 66 ns minimum, so the write clock is
at most 15.15 MHz; TSCYCR is 150 ns minimum, so the read clock is at most
6.67 MHz; chip select setup is 15 ns for a write and 60 ns for a read; D/CX
setup and hold are 10 ns (p. 54, ll. 2621-2645). `st7365p.c` writes at
125 MHz / (2 x 5) = 12.5 MHz, an 80 ns cycle (`st7365p.c:69-70`, `st7365p.c:354-357`);
the panel test reads at 125 MHz / (2 x 20) = 3.125 MHz, a 320 ns cycle
(`st7365p.c@b47cda40:455`).

**Read format (DOC; BOARD).** The SDO pin shifts on the falling edge of SCL
and the host samples on the rising edge (p. 94, ll. 4722-4727). The 4-line read
figures give three shapes: 8-bit reads (RDID1-3, 0Ah-0Fh) return the data
byte directly after the command with no dummy clock (p. 95, l. 4736); the 24-bit
RDDID read and the 32-bit RDDST read insert one dummy clock cycle before the
data (p. 95, l. 4761, p. 95, l. 4787; p. 95, l. 4792, p. 95, l. 4816). The command pages print
a whole dummy parameter for every read; that row describes the parallel bus
(for example p. 153, l. 7900: "There is one dummy parameter when using Parallel
interface"). The board run confirms both shapes: 0Ah, 0Bh and 0Ch return their
value in the first byte, and the multi-byte 09h and D3h replies are the
register shifted right by one bit. The command pages' flow charts
split serial from parallel mode for fifteen reads: the serial chart shows a
"Dummy Clock" for 04h and 09h (p. 148, ll. 7541-7549; p. 152, ll. 7808-7816) and sends
the data parameter directly for 0Ah-0Fh, 52h, 54h, 56h, 5Fh and DAh-DCh (for
example p. 154, ll. 7936-7946, p. 204, ll. 11284-11294). The charts of 2Eh and 3Eh show
a generic "Dummy" before the image data (p. 180, l. 9574, p. 199, l. 10932), the GSCAN
chart a generic "Dummy read" (p. 202, l. 11117), and the charts of AAh, AFh, D2h
and D3h are empty in the extraction; none of these seven has a serial-mode
figure, so its dummy-clock count on this wiring is not documented. D3h
behaved on the board as one dummy clock followed by 24 data bits.

**Reset (DOC; DRV).** A RESX low pulse of at least 10 us resets the chip;
shorter than 5 us is rejected (p. 59, ll. 2908-2929). After RESX rises, commands
wait 5 ms and SLPOUT waits 120 ms (note 7, p. 60, ll. 2955-2957); the 5 ms
covers loading ID, VCOM and other settings from the NVM (note 1,
p. 59, ll. 2918-2920). `st7365p_init()` holds reset low 1 ms, then waits 120 ms
(`st7365p.c:366-370`).

**Power and modes (DOC).** Six power levels run from Normal Mode On, Idle
Mode Off, Sleep Out down to power off; transitions among the first five are
commands (p. 277, ll. 15811-15838). VDDI and VDD may rise in any order
(p. 278, l. 15849). In Sleep Out the supplies must stay up at least 120 ms after
RESX is released before they fall (p. 278, ll. 15851-15852). Typical controller
current at 60 Hz with default registers is 12 mA (IDD) in normal mode and
0.025 mA in standby (p. 50, ll. 2385-2393); the backlight is separate (section 4).

## 2. Command Table 1

Every Command Table 1 command lists all five power states (normal or
partial, idle on or off, sleep out; sleep in) as available, except where an
entry says otherwise. The summary list is on pages 135-140
(pp. 135-140, ll. 6612-7047).

### 00h NOP (p. 145, l. 7337)

No parameter; no operation (p. 145, ll. 7343-7347). Defaults N/A.
Status: NOT IMPLEMENTED in the driver. The panel test sends it as the empty
PRE and POST table (`st7365p.c@b47cda40:457`). It buys nothing on this hardware beyond a
harmless byte.

### 01h SWRESET -- Software Reset (p. 145, l. 7380)

No parameter. Resets commands and parameters to their software-reset
defaults; frame memory is kept (p. 145, ll. 7392-7396).
Rules: wait 5 ms before the next command, during which factory defaults load;
if sent in Sleep In, wait 120 ms before SLPOUT; never send it during the
sleep-out sequence (pp. 145-146, ll. 7398-7410).
Status: NOT IMPLEMENTED. `st7365p_init()` pulses RESX instead
(`st7365p.c:366-370`), which also restores the hardware-reset defaults. SWRESET
would allow a recovery from a wedged panel state without the reset GPIO; the
reset pin is wired (GP15), so nothing needs it.

### 04h RDDID -- Read Display ID (p. 147, l. 7474)

Read, 24 bits after one dummy clock on 4-line SPI (p. 95, l. 4761). Parameter 2
ID1 (manufacturer), 3 ID2 (module/driver version), 4 ID3 (module/driver);
the same bytes as DAh, DBh, DCh (p. 147, ll. 7482-7500). Default FFh FFh FFh at
power-on, software and hardware reset (p. 147, ll. 7524-7532).
Status: READ by the panel test, 4 bytes (`st7365p.c@b47cda40:603-610`); MEASURED
`ff ff ff ff` under every set (BOARD). That is the documented default of an
unprogrammed module ID; it cannot be told apart from an SDO line held high,
but 0Ah-0Ch read correct non-FF values over the same line.

### 05h RDNUMED -- Read Number of the Errors on DSI (p. 148, l. 7576)

Read; P[6:0] counts DSI errors, P[7] flags overflow; cleared after the read
(p. 148, ll. 7586-7598). Only the MIPI interface uses it; on other interfaces
P[7:0] reads 0 (p. 148, l. 7602). Default 00h.
Status: NOT IMPLEMENTED; it carries no information on SPI.

### 09h RDDST -- Read Display Status (p. 150, l. 7671)

Read, 32 bits after one dummy clock (p. 95, l. 4792). Bits (pp. 150-151, ll. 7681-7768):
D31 BSTON, D30 MY, D29 MX, D28 MV, D27 ML, D26 RGB, D25-D24 future;
D23 future, D22-D20 IFPF (101 = 16, 110 = 18, 111 = 24 bit/pixel), D19 IDMON,
D18 PTLON, D17 SLPOUT, D16 NORON; D15 vertical scrolling on, D14 unused,
D13 INVON, D12-D11 unused, D10 DISON, D9 TEON, D8-D6 gamma curve select,
D5 TEM, D4-D0 future. Defaults: power-on and hardware reset
`0000-0000 0110-0001 0000-0000 0000-0000`, software reset
`0xxx-xx00 0110-0001 ...` (p. 151, ll. 7791-7799).
Status: READ by the panel test, 5 bytes; MEASURED raw `d2 31 d2 00 00` under
set 0, which shifted left one bit is `a4 63 a4 00` (BOARD; decode below is
this file's). D31-D24 = a4: BSTON 1, MX 1, RGB 1, MY/MV/ML 0. D23-D16 = 63:
IFPF 110 (18 bit), SLPOUT 1, NORON 1, IDMON 0, PTLON 0. D15-D8 = a4:
vertical scrolling 1, INVON 1, DISON 1, TEON 0. D7-D0 = 00: gamma curve 0,
TEM 0. The run record gives set 19 (MADCTL 40h) as `a0` and set 20 (MADCTL
08h) as `84` in the first status byte.

### 0Ah RDDPM -- Read Display Power Mode (p. 153, l. 7854)

Read, 8 bits, no dummy on 4-line SPI (p. 95, l. 4736). D7 BSTON, D6 IDMON,
D5 PTLON, D4 SLPOUT, D3 NORON, D2 DISON, D1-D0 0 (p. 153, ll. 7864-7896).
Default 08h (p. 154, ll. 7924-7930).
Status: READ; MEASURED `9c` (BSTON, SLPOUT, NORON, DISON) under every set
(BOARD).

### 0Bh RDDMADCTL -- Read Display MADCTL (p. 155, l. 7969)

Read, 8 bits. D7 MY, D6 MX, D5 MV, D4 ML, D3 RGB, D2 MH, D1-D0 0
(p. 155, ll. 7979-8019). Default 00h at power-on and hardware reset; software
reset leaves it unchanged (p. 156, ll. 8047-8053).
Status: READ; MEASURED `48` under the shipped set, `40` under set 19 and
`08` under set 20 (BOARD): the register follows what is written.

### 0Ch RDDCOLMOD -- Read Display Pixel Format (p. 157, l. 8095)

Read, 8 bits. D6-D4 RGB-interface format, D2-D0 control-interface format,
101 = 16, 110 = 18, 111 = 24 bit/pixel (p. 157, ll. 8105-8127). Default
`0000-0110` at power-on and hardware reset; software reset unchanged
(p. 157, ll. 8150-8156).
Status: READ; MEASURED `66` (BOARD).

### 0Dh RDDIM -- Read Display Image Mode (p. 159, l. 8200)

Read, 8 bits. D7 VSSON (vertical scrolling), D5 INVON, D2-D0 gamma curve
(p. 159, ll. 8210-8236); the gamma table names a "Gamma set (26h)" command that
Command Table 1 does not contain. Default 00h.
Status: READ; MEASURED `a0` under every set (BOARD): VSSON 1, INVON 1,
gamma curve 0 (this file's decode).

### 0Eh RDDSM -- Read Display Signal Mode (p. 160, l. 8301)

Read, 8 bits. D7 TEON, D6 TEM, D5 HS, D4 VS, D3 pixel clock, D2 data enable
(the last four for the RGB interface), D0 DSI error (p. 160, ll. 8311-8331).
Default 00h.
Status: READ; MEASURED `00` (BOARD): tearing-effect output off.

### 0Fh RDDSDR -- Read Display Self-Diagnostic Result (p. 162, l. 8406)

Read, 8 bits. D7 register loading detection, D6 functionality detection,
D0 checksum comparison (0 = same) (p. 162, ll. 8416-8428); the text gives no
meaning for D7 = 1 or D6 = 1 beyond the names. Default 00h.
Status: READ; MEASURED `c0` (BOARD): D7 and D6 set, D0 clear. What the set
bits assert is not documented in the extraction (the result follows the
self-diagnostic run after SLPOUT, p. 165, l. 8622).

### 10h SLPIN -- Sleep In (p. 163, l. 8495)

No parameter. Stops the DC/DC converter, the oscillator and panel scanning;
the MCU interface and memory keep working and RAM keeps its contents
(p. 163, ll. 8505-8509). Rules: no effect if already asleep; wait 5 ms before the
next command; wait 120 ms after SLPOUT before SLPIN (p. 163, ll. 8515-8525). The
TE output is held low in Sleep In (p. 187, l. 10049). Default: Sleep In.
Status: NOT IMPLEMENTED. It would cut the controller from about 12 mA to
0.025 mA typical (p. 50, ll. 2392-2393) while the console is idle or the system
suspends; nothing in the kernel has a display-idle or suspend path to call
it from, and the backlight, the larger load, is the keyboard controller's.

### 11h SLPOUT -- Sleep Out (p. 164, l. 8591)

No parameter. Starts the DC/DC converter, the oscillator and scanning; the
display shows blank for two frames (flow chart text, p. 166, ll. 8657-8659); runs
the self-diagnostics (p. 165, l. 8622). Rules: wait 5 ms before the next command
(p. 165, l. 8614); 120 ms after reset release before SLPOUT (p. 60, l. 2955); B3h,
B5h and B6h must be set before Sleep Out (p. 221, l. 12470, p. 223, l. 12638,
p. 227, l. 12916). Default: Sleep In.
Status: SENT, after the whole `init_seq` and before VSCRDEF, followed by
120 ms (`st7365p.c:378-380`). MEASURED: 0Ah and 09h report SLPOUT = 1 (BOARD).

### 12h PTLON -- Partial Display Mode On (p. 167, l. 8692)

No parameter. Enters partial mode with the window of PTLAR (30h); NORON
(13h) leaves it (p. 167, ll. 8702-8704). Default: normal mode.
Status: NOT IMPLEMENTED; see partial mode in section 4.

### 13h NORON -- Normal Display Mode On (p. 168, l. 8744)

No parameter. Normal mode means partial off (p. 168, ll. 8754-8758); the heading
row prints "12H" (p. 168, l. 8746). Default: normal mode on.
Status: NOT IMPLEMENTED (not needed: the panel never leaves normal mode;
09h and 0Ah report NORON = 1, BOARD).

### 20h INVOFF -- Display Inversion Off (p. 169, l. 8798)

No parameter. Leaves display inversion (p. 169, l. 8808). Default: inversion off.
Status: NOT IMPLEMENTED. The driver keeps inversion on (21h).

### 21h INVON -- Display Inversion On (p. 170, l. 8883)

No parameter. The description row repeats INVOFF's text "used to recover
from display inversion mode" (p. 170, l. 8893); the command's effect is shown
only in a figure, which was not read. Default: inversion off
(p. 171, ll. 8927-8933).
Status: SENT in `init_seq` (`st7365p.c:121`); MEASURED: 09h D13 and 0Dh D5
read INVON = 1 (BOARD). Both vendor sequences send it on this glass
(`SPI-LCD.c`: INVON when `Option.BGR`). Why the glass needs inversion is
not documented (INF: a normally-black glass; unverified).

### 28h DISPOFF -- Display Off (p. 171, l. 8968)

No parameter. Output from frame memory is disabled and a blank page inserted;
memory and other status unchanged (p. 171, ll. 8978-8988). Default: display off.
Status: NOT IMPLEMENTED. It would blank the panel without sleeping, for a
screen-blank feature; the kernel has none.

### 29h DISPON -- Display On (p. 173, l. 9065)

No parameter. Enables output from frame memory (p. 173, ll. 9075-9081). Default:
display off.
Status: SENT by `st7365p_display_on()` (`st7365p.c:396-401`), which lcdterm
calls after its first full clear; MEASURED: DISON = 1 in 0Ah (BOARD).

### 2Ah CASET -- Column Address Set (p. 174, l. 9156)

Four parameters: XS[15:8], XS[7:0], XE[15:8], XE[7:0] (p. 174, ll. 9164-9170).
Rules: XS <= XE; addresses past the maximum are ignored; maximum 013Fh (319)
with MV = 0, 01DFh (479) with MV = 1 (p. 174, ll. 9183-9191). Defaults XS = 0,
XE = 013Fh; after software reset XE follows MV (p. 175, ll. 9213-9221).
Status: SENT before every pixel transfer by `lcd_window()`
(`st7365p.c:179-188`); MEASURED indirectly: the console and pattern 7 draw where
addressed (BOARD).

### 2Bh RASET -- Row Address Set (p. 176, l. 9274)

Four parameters: YS[15:8], YS[7:0], YE[15:8], YE[7:0] (p. 176, ll. 9282-9288).
Rules: YS <= YE; maximum 01DFh (479) with MV = 0, 013Fh with MV = 1; the
restriction row prints a strict "< (01DFh)" here and "<=" for CASET
(p. 176, ll. 9308-9316). Defaults YS = 0, YE = 01DFh (pp. 176-177, ll. 9334-9348).
Status: SENT by `lcd_window()` (`st7365p.c:184-186`); rows 0-479 are all
addressable, and pattern 7 drew rows 0-319 and showed rows 320-479 as undrawn
RAM (BOARD).

### 2Ch RAMWR -- Memory Write (p. 177, l. 9407)

Any number of pixel parameters. Resets the column and page counters to the
window start; any other command ends the write (p. 177, ll. 9421-9429). No length
restriction (p. 178, l. 9437). Counter rules: column increments; past XE it
returns to XS and the page increments; past YE both return to the start
(p. 250, ll. 14413-14421). Power-on RAM content is random; resets do not clear it
(p. 178, ll. 9455-9461).
Status: SENT by `lcd_window()` (`st7365p.c:187`); pixels follow as three
bytes (`st7365p.c:190-196`) or, under lcdgfx COLMOD 55h, two.

### 2Eh RAMRD -- Memory Read (p. 178, l. 9496)

Read: one dummy parameter, then pixels from the window start; any other
command cancels (pp. 178-179, ll. 9504-9527). Default RAM random. The flow chart shows "Dummy" then the image data, ended by any command
(p. 180, ll. 9574-9589); no 4-line SPI figure covers RAMRD, so its dummy-clock
count on this wiring is not documented.
Status: NOT IMPLEMENTED. It would let the 160 RAM rows that are off the
glass at a given scroll start serve as readable scratch, and would allow a
read-modify-write cursor; blockers are the undocumented SPI dummy timing, the
read clock (at most 6.67 MHz, p. 54, l. 2633), and the lack of any caller.

### 30h PTLAR -- Partial Area (p. 181, l. 9602)

Four parameters: PSL[15:0] start row, PEL[15:0] end row, frame-memory row
addresses; with PEL < PSL the area wraps; PSL = PEL is one row
(p. 181, ll. 9610-9669). Defaults PSL = 0000h, PEL = 01DFh (p. 182, ll. 9693-9699).
Ordering (flow chart, p. 182, ll. 9703-9733): optionally DISPOFF to prevent
tearing, PTLAR, PTLON, RAMWR with the image, DISPON; NORON leaves partial
mode.
Status: NOT IMPLEMENTED (section 4, partial mode).

### 33h VSCRDEF -- Vertical Scrolling Definition (p. 183, l. 9743)

Six parameters: TFA[15:0] top fixed area, VSA[15:0] scroll area height,
BFA[15:0] bottom fixed area, all in frame-memory lines (p. 183, ll. 9751-9779);
with MV = 1 the text puts TFA at the bottom (pp. 183-184, ll. 9802-9821).
Invariant: TFA + VSA + BFA = 480, the device's line count, otherwise
scrolling is undefined (p. 184, ll. 9843-9845; p. 257, ll. 14825-14829). MADCTL MV must
be 0 in scroll mode (p. 184, l. 9847). Ordering (flow chart, p. 185, ll. 9880-9930):
from normal mode, VSCRDEF, then CASET and RASET (only required for
non-rolling scrolling), MADCTL if the write direction must change, RAMWR
with the scroll image, then VSCSAD. Defaults TFA 0, VSA 01E0h (480), BFA 0
(p. 184, ll. 9865-9871).
Status: SENT with 0 / 320 / 160 after SLPOUT (`st7365p.c:382-386`) and by
`lcd_gfx_scrdef()` with TFA = top, BFA = bottom and VSA = 480 - top -
bottom (`st7365p.c:292-301`); lcdgfx refuses top + bottom >= 480
(`lcdgfx.c:497-504`). MEASURED: pattern 7 set 0 / 480 / 0 and the glass
showed the expected rows (BOARD; section 4).

### 34h TEOFF -- Tearing Effect Line Off (p. 185, l. 9935)

No parameter. Turns the TE output off (active low) (p. 185, l. 9945). Default
off.
Status: NOT IMPLEMENTED; TE is off by default (0Eh reads TEON = 0, BOARD),
and the TE pin is not wired on this mainboard (section 4).

### 35h TEON -- Tearing Effect Line On (p. 186, l. 10012)

One parameter: D0 TEM; TEM = 0 gives V-blanking only, TEM = 1 V- and
H-blanking (pp. 186-187, ll. 10020-10042); unaffected by MADCTL ML (p. 186, l. 10024).
Default off.
Status: NOT IMPLEMENTED. Blocked by the hardware: J401 pin 35 (TE) is not
routed to the RP2040 (section 4).

### 36h MADCTL -- Memory Data Access Control (p. 188, l. 10113)

One parameter: D7 MY row order, D6 MX column order, D5 MV row/column
exchange (these three set the MCU-to-memory direction), D4 ML vertical
refresh order (0 = top to bottom), D3 RGB (0 = RGB color filter panel, 1 =
BGR color filter panel), D2 MH horizontal refresh order (p. 188, ll. 10121-10151).
The RGB-bit figure on page 189 was read from the PDF image by the earlier
study, not here. MX mirrors whole pixels; the subpixel order within a pixel
is fixed by the figure on page 249 (p. 249, ll. 14350-14382). Default 00h at
power-on and hardware reset, unchanged by software reset
(p. 190, ll. 10256-10262).
Status: SENT as 48h (MX, RGB) in `init_seq` (`st7365p.c:117`); READ and
MEASURED through 0Bh: 48 / 40 / 08 under sets 0 / 19 / 20 (BOARD). Which
physical stripe a red pixel lights was not judged in the run.

### 37h VSCSAD -- Vertical Scroll Start Address of RAM (p. 190, l. 10296)

Two parameters: VSP[15:0], the frame-memory line shown first after the top
fixed area (p. 190, ll. 10304-10314). A new start address takes effect at the next
panel scan, which avoids tearing (p. 191, ll. 10365-10367). Invariant: VSP must
lie in the scroll area, not in a fixed area (p. 191, ll. 10371-10375). Default
0000h. The command table list names it VSCRSADD (p. 138, l. 6863).
Status: SENT by `lcd_gfx_scroll()` (`st7365p.c:251-258`); lcdterm calls it
through `lcd_set_top()` (`lcdterm.c:211`, `lcdterm.c:778`,
`lcdterm.c:792`) and lcdgfx after a bounds check that keeps VSP inside the
scroll area (`lcdgfx.c:492-496`). MEASURED: pattern 7 (BOARD; section 4).

### 38h IDMOFF -- Idle Mode Off (p. 192, l. 10410)

No parameter. Full color, normal frame rate (p. 192, ll. 10420-10426). Default
idle off.
Status: NOT IMPLEMENTED (the panel never enters idle mode).

### 39h IDMON -- Idle Mode On (p. 193, l. 10490)

No parameter. Eight colors from the MSB of each of R, G, B; the B2h frame
rate applies (pp. 193-194, ll. 10500-10542). The restriction row repeats IDMOFF's
"already in idle off mode" (p. 194, l. 10544). Default idle off.
Status: NOT IMPLEMENTED (section 4, idle mode).

### 3Ah COLMOD -- Interface Pixel Format (p. 195, l. 10607)

One parameter: D6-D4 RGB-interface format, D2-D0 control-interface format,
101 = 16, 110 = 18, 111 = 24 bit/pixel (p. 195, ll. 10616-10640). On 4-line SPI
the datasheet shows 16-bit (two bytes, R5 G6 B5; p. 96, ll. 4829-4850) and 18-bit
(three bytes, six significant high bits each; p. 97, ll. 4868-4881); it shows no
24-bit SPI transfer. Default 18 bit/pixel at power-on and hardware reset,
unchanged by software reset (p. 196, ll. 10664-10670).
Status: SENT as 66h in `init_seq` (`st7365p.c:118`) and by
`lcd_gfx_colmod()` with 66h or 55h (`st7365p.c:303-310`, `lcdgfx.c:505-510`);
MEASURED: 0Ch reads 66, 09h IFPF reads 110 (BOARD). COLMOD 55h has not been
run on the board.

### 3Ch WRMEMC -- Write Memory Continue (p. 196, l. 10705)

Pixel parameters continuing after the last RAMWR or WRMEMC position; counter
rules as RAMWR; pixels past (XE-XS+1)(YE-YS+1) are ignored
(pp. 196-197, ll. 10720-10768). Must follow a CASET or RASET plus RAMWR, else the
address is undefined (p. 197, ll. 10770-10772).
Status: NOT IMPLEMENTED. Every driver transfer opens a fresh window with
RAMWR, so a transfer paused by CSX resumes on its own (section 1); WRMEMC
would save the 11-byte window header only when a transfer is broken by
another command.

### 3Eh RDMEMC -- Read Memory Continue (p. 198, l. 10835)

Read, one dummy parameter, then pixels continuing after the last read
(pp. 198-199, ll. 10843-10883). Always 18-bit regardless of COLMOD
(p. 199, ll. 10895-10897).
Status: NOT IMPLEMENTED (as RAMRD).

### 44h STE -- Set Tear Scanline (p. 199, l. 10954)

Two parameters: N[15:0], the line at which TE fires; V-blanking only; N = 0
equals TEON with TEM = 0 (p. 200, ll. 10966-10983); TE is low in sleep
(p. 200, l. 10985). Takes effect on the frame after the current one
(p. 200, ll. 10987-10991). Default 0000h. The command table list calls it TESCAN
(p. 138, l. 6906).
Status: NOT IMPLEMENTED; blocked by the unwired TE pin (section 4).

### 45h GSCAN -- Get Scanline (p. 201, l. 11054)

Read: one dummy, then N[15:8], N[7:0], the line currently being scanned
(p. 201, ll. 11062-11066). The line count is VSYNC + VBP + VACT + VFP and line 0 is
the first VSYNC line (p. 201, ll. 11068-11072); undefined in Sleep In
(p. 201, l. 11074). The flow chart shows "Dummy read", "Line N (MSB)",
"Line N (LSB)" with no serial-mode variant (p. 202, ll. 11117-11126). Default
0000h. The command table list calls it RDTESCAN
(p. 139, l. 6925).
Status: NOT IMPLEMENTED; the only software source of panel timing on this
board (section 4, vertical sync).

### 51h WRDISBV -- Write Display Brightness (p. 202, l. 11142)

One parameter DBV[7:0]; 00h lowest, FFh highest; the mapping to light is
the module's (p. 202, ll. 11150-11158). Default 0000h.
Status: NOT IMPLEMENTED; it acts through CABC_PWM, which the mainboard does
not use (section 4, brightness).

### 52h RDDISBV -- Read Display Brightness Value (p. 203, l. 11225)

Read, 8 bits; the serial flow chart has no dummy (p. 204, ll. 11284-11294). DBV[7:0]; reset in Sleep In; 0 when BCTRL = 0
(pp. 203-204, ll. 11233-11254). Default 0000h.
Status: NOT IMPLEMENTED.

### 53h WRCTRLD -- Write CTRL Display (p. 204, l. 11313)

One parameter: D5 BCTRL brightness block on, D3 DD dimming on (manual
brightness only), D2 BL backlight control on (pp. 204-205, ll. 11321-11350). Dimming
applies when BCTRL changes with DD = 1; BL on-to-off is immediate
(p. 205, ll. 11354-11358). Dimming transition 400-600 ms, linear
(p. 268, ll. 15431-15436). Default 0000h.
Status: NOT IMPLEMENTED; no backlight path through the controller on this
board (section 4).

### 54h RDCTRLD -- Read CTRL Value Display (p. 206, l. 11423)

Read, 8 bits, no dummy in the serial flow chart (p. 207, l. 11493); the 53h bits (p. 206, ll. 11430-11456). Default 0000h.
Status: NOT IMPLEMENTED.

### 55h WRCABC -- Write Content Adaptive Brightness Control (p. 207, l. 11522)

One parameter: D7 CECTRL color enhancement on, D5-D4 CE level (00 low, 01
medium, 11 high), D1-D0 CABC mode (00 off, 01 user interface, 10 still
picture, 11 moving image) (pp. 207-208, ll. 11529-11570). Output brightness = manual
setting x CABC ratio (p. 274, l. 15693). Default 0000h.
Status: NOT IMPLEMENTED. CABC acts on the backlight through CABC_PWM, which is
not connected (section 4). CECTRL acts on the image; the extraction gives no
transfer function for it, so its effect on this glass is unknown.

### 56h RDCABC -- Read Content Adaptive Brightness Control (p. 209, l. 11640)

Read, 8 bits, no dummy in the serial flow chart (p. 210, l. 11703); C1-C0 (p. 209, ll. 11648-11666). Default 0000h.
Status: NOT IMPLEMENTED.

### 5Eh WRCABCMB -- Write CABC Minimum Brightness (p. 210, l. 11732)

One parameter CMB[7:0], the floor CABC may not go below
(p. 210, ll. 11740-11746; p. 275, ll. 15710-15715); ignored while BCTRL = 0
(p. 275, ll. 15723-15725). Default 0000h.
Status: NOT IMPLEMENTED (no CABC path).

### 5Fh RDCABCMB -- Read CABC Minimum Brightness (p. 212, l. 11818)

Read, 8 bits, no dummy in the serial flow chart (p. 213, l. 11872); CMB[7:0] (p. 212, ll. 11826-11828). Default 0000h.
Status: NOT IMPLEMENTED.

### AAh RDFCS -- Read First Checksum (p. 213, l. 11901)

Read; the table prints a dummy parameter and no serial flow chart survives the extraction; FCS[7:0], a checksum of user registers and frame memory
after writes (p. 213, ll. 11908-11914). Wait 150 ms after the last register write
(p. 213, ll. 11918-11920). The instruction row is labeled "RDID1" (p. 213, l. 11906).
Default 00h.
Status: NOT IMPLEMENTED. With AFh it would give a cheap host check that a
register sequence landed, independent of the SPIRC path that read zero.

### AFh RDCFCS -- Read Continue Checksum (p. 214, l. 11957)

Read, laid out as AAh; CCS[7:0], a running checksum after the first
(p. 214, ll. 11964-11972). Wait 300 ms after the last write for the first read
(p. 214, ll. 11976-11978). The instruction row is labeled "RDID1" (p. 214, l. 11962).
Default 00h.
Status: NOT IMPLEMENTED (as AAh).

### DAh RDID1 -- Read ID1 (p. 215, l. 12017)

Read, 8 bits on 4-line SPI (p. 95, l. 4736): manufacturer ID (p. 215, l. 12029).
Default FFh (p. 215, ll. 12051-12057).
Status: READ; MEASURED `ff` (BOARD), the documented default.

### DBh RDID2 -- Read ID2 (p. 216, l. 12096)

Read, 8 bits: module/driver version (p. 216, l. 12108). Default FFh.
Status: READ; MEASURED `ff` (BOARD), the documented default.

### DCh RDID3 -- Read ID3 (p. 217, l. 12175)

Read, 8 bits: module/driver ID (p. 217, l. 12187). Default FFh.
Status: READ; MEASURED `ff` (BOARD), the documented default.

## 3. Command Table 2

The summary list is on pages 140-144 (pp. 140-144, ll. 7049-7326). Every Command Table 2
page gives its power-on, software-reset and hardware-reset default as "N/A";
the HEX column of the parameter rows prints one example byte per parameter,
and the values below come from that column as printed examples. They are not
reset defaults: the default tables mark them N/A (for B1h, p. 220,
ll. 12383-12389), so an unsent parameter has no documented value. CSCON (F0h)
names the enables "command 2 part I" (C3h) and "part II" (96h)
(p. 245, ll. 14194-14206); the datasheet does not say which registers belong to
which part, whether command 2 is enabled after reset, or whether reads through
SPIRC need it. `st7365p.c` never sends F0h. The board run's one relevant
observation: the owner saw striping under set 16, which writes C5h 3Fh
without F0h, and flicker under set 15, which writes B1h 00h after F0h C3h
96h (BOARD, by eye, no photograph; sets 13 and 14, the matching pair, were
not reported). If the striping is the C5h write acting, C5h is live without
CSCON on this panel (INF, one unphotographed observation).

### B0h IFMODE -- Interface Mode Control (p. 218, l. 12256)

One parameter: D7 SPI_EN (0 = DIN and DOUT both used; 1 = DIN/SDA only, DOUT
unused), D3 VSCP, D2 HSCP, D1 PKP, D0 DEP (RGB-interface polarities)
(p. 218, ll. 12264-12282). Effective as soon as received (p. 218, l. 12266). HEX 00h.
Status: SENT as 00h (`st7365p.c:119`): SDO stays enabled, which the panel-test
reads rely on.

### B1h FRMCTR1 -- Frame Rate Control, normal mode (p. 219, l. 12324)

Two parameters: D7-D4 FRS[3:0], D1-D0 DIVA[1:0]; then D4-D0 RTNA[4:0]
(p. 219, ll. 12332-12334). FRS sets the full-color normal-mode frame frequency;
DIVA divides the internal clock by 1, 2, 4, 8 (the table column is headed
"Inversion mode", p. 219, l. 12340); RTNA sets the line period
(p. 219, ll. 12336-12352).
Formula (p. 219, ll. 12354-12358; the PDF page shows the same formula):

    frame rate = 10^7 / ((168 + RTNA + 32 x (15 - FRS)) x (480 + VFP + VBP))

FRS 0-15, RTNA 0-31; VFP and VBP come from B5h. DIVA does not appear in the
formula. HEX column A0h, 10h (p. 219, ll. 12332-12334): FRS = 10, DIVA = 0,
RTNA = 16. If RTNA, VFP and VBP hold the HEX-column values 10h, 02h, 02h, the
formula gives 10^7 / (344 x 484) = 60.06 Hz. The datasheet marks the reset
values of B1h as N/A (p. 220, ll. 12383-12389), so that assumption is
unsupported by the datasheet.
Status: SENT as one byte A0h (`st7365p.c:120`); RTNA is not sent and B5h is
never sent, so RTNA, VFP and VBP hold whatever the panel powers up with, which
is undocumented. The 60.06 Hz figure is computed under the HEX-column
assumption and unmeasured on the board; the GSCAN line-period reading of 4.2
decides. MEASURED by eye: set 15 (F0h unlock, then B1h 00h, a
formula rate of 10^7 / (664 x 484) = 31.1 Hz) flickers (BOARD). Range and
table in section 4.

### B2h FRMCTR2 -- Frame Rate Control, idle mode (p. 220, l. 12397)

Two parameters: 00h, then RTNB[4:0], the idle-mode line period
(p. 220, ll. 12404-12410). HEX 00h, 10h. No formula is printed (text and PDF
page 220 checked).
Status: NOT IMPLEMENTED (idle mode unused).

### B3h FRMCTR3 -- Frame Rate Control, partial mode (p. 221, l. 12453)

Two parameters: 00h, then RTNC[4:0], the partial-mode line period
(p. 221, ll. 12460-12466). Must be set before Sleep Out (p. 221, l. 12470). HEX 00h,
10h. No formula printed.
Status: NOT IMPLEMENTED.

### B4h DIC -- Display Inversion Control (p. 221, l. 12508)

One parameter: D1-D0 DINV, 00 column, 01 1-dot, 10 2-dot inversion, 11
reserved (p. 221, l. 12515, p. 222, ll. 12522-12533). This is drive-polarity inversion,
distinct from data inversion (21h). HEX 01h. The command table list prints
its opcode bits as `0 0 0 0 1 0 0 1` (p. 141, l. 7093).
Status: SENT as 02h, 2-dot (`st7365p.c:122`). READ through SPIRC (`fb10`,
`fb11`), MEASURED `00 00` under every set (BOARD), which matches neither
02h nor 01h.

### B5h BPC -- Blanking Porch Control (p. 222, l. 12571)

Four parameters: VFP[7:0], VBP[7:0], 00h, HBP[7:0] (p. 222, ll. 12578-12584).
Front and back porch in lines, 02h-FFh (00h and 01h reserved); HBP in dot
clocks, same range (pp. 222-223, ll. 12586-12634). Must be set before Sleep Out
(p. 223, l. 12638). HEX column 02h, 02h, 00h, 04h; the reset values are
undocumented, as for B1h.
Status: NOT IMPLEMENTED, so VFP and VBP keep their undocumented power-up values. Larger porches are the documented route to frame
rates below 30.4 Hz (section 4).

### B6h DFC -- Display Function Control (p. 224, l. 12680)

Three parameters (p. 224, ll. 12687-12691; the heading row repeats "Blanking Porch
Control", p. 224, l. 12682):
byte 1 D7 BYPASS (RGB interface: memory or direct), D6 RCM (DE or SYNC
mode), D5 RM (system or RGB interface writes GRAM), D3-D2 PTG (gate scan in
the non-display area: 00 normal, 10 interval), D1-D0 PT (source level in the
partial-mode non-display area: V63, V0, AGND, Hi-Z) (pp. 224-225, ll. 12693-12757);
byte 2 D6 GS (gate scan G1 to G480 or reverse), D5 SS (source S1 to S960 or
reverse; with the RGB bit it sets the R, G, B assignment to source pins),
D4 SM (gate pin arrangement), D3-D0 ISC (interval-scan cycle, odd n from 3 to
31 frames) (p. 225, ll. 12761-12801); byte 3 D5-D0 NL, lines driven = 8 x (NL +
1), 00h-3Bh; GRAM mapping is unaffected; NL must cover the glass
(p. 226, ll. 12897-12905). Must be set before Sleep Out (p. 227, l. 12916). HEX 80h,
02h, 3Bh.
Status: SENT as 02h 02h 3Bh (`st7365p.c:123`): BYPASS 0, PT = 10 (AGND), ISC 2,
NL 3Bh (480 lines). The vendor ST7796SP sequence sends 80h 02h 3Bh.
NOT IMPLEMENTED: NL = 27h (320 lines), which the glass would allow; whether
the frame formula's constant 480 tracks NL is not documented, so the gain in
frame time is unverified.

### B7h EM -- Entry Mode Set (p. 228, l. 12957)

One parameter: D7-D6 EPF, D3 DSTB, D2 GON, D1 DTE (p. 228, l. 12964). DSTB = 1
enters deep standby, losing RAM and registers; exit by six CSX low pulses or
a RESX pulse (p. 228, ll. 12966-12974). GON/DTE: 11 normal display, other values
hold the gates at VGH or VGL (p. 228, ll. 12978-12988). EPF sets how a 16-bit
pixel fills the 18-bit RAM: 00 low bits 0, 01 low bits 1, 10 MSB copied to
LSB, 11 r0 = b0 = G0 (p. 228, ll. 12992-13002). HEX 06h.
Status: SENT as C6h (`st7365p.c:124`): EPF 11, GON 1, DTE 1. READ through SPIRC,
MEASURED `00 00` (BOARD).

### B9h MODESEL -- Mode Selection (p. 229, l. 13045)

One parameter, printed as fixed bits `0 0 0 0 0 0 1 0`, HEX 02h; "dithering
on", effective only for 16.7M-color input (p. 229, ll. 13052-13054).
Status: NOT IMPLEMENTED; 24-bit input has no 4-line SPI format in the
datasheet (section 4, dithering).

### C0h PWR1 -- Power Control 1 (p. 230, l. 13095)

Two parameters: D7-D6 AVDDS, D5-D4 AVCLS, D3-D0 0; then D6-D4 VGHS, D2-D0
VGLS (p. 230, ll. 13102-13104). AVDD 6.20 / 6.40 / 6.60 / 6.80 V; AVCL -4.4 /
-4.6 / -4.8 / -5.0 V (p. 230, ll. 13108-13110). VGH 12.541 + steps to 15.467 V,
VGL -7.158 to -12.500 V in eight steps (p. 230, ll. 13116-13124). HEX 80h, 25h.
Status: SENT as 17h 15h (`st7365p.c:114`): AVDD 6.20 V, AVCL -4.6 V, low nibble
0111 where the table prints 0000; VGH 12.889 V, VGL -10.429 V.

### C1h PWR2 -- Power Control 2 (p. 231, l. 13163)

One parameter: VRH[6:0] (p. 231, l. 13170). VAP(GVDD) = 3.55 V + 0.05 V x VRH +
(VCOM + VCOM offset) for VRH 00h-27h (this file's closed form for the printed
table), 28h-3Fh reserved; VAN(GVCL) = -(3.55 V + 0.05 V x VRH) +
(VCOM + VCOM offset): the printed VAN rows negate the VRH term only and add the
VCOM terms with a plus sign (pp. 231-232, ll. 13224-13272). The two rails are
therefore not mirror images unless VCOM + offset is 0. Note: VCOM default 1Ch (1.0 V), offset
00h (p. 232, ll. 13276-13280). HEX 13h (4.5 V + VCOM terms).
Status: SENT as 41h (`st7365p.c:115`), which lies above the printed table (40h-7Fh
are not listed); the resulting GVDD is undocumented.

### C2h PWR3 -- Power Control 3 (p. 233, l. 13322)

One parameter: D7-D4 fixed 1010, D3-D2 SOP source driving current, D1-D0 GOP
gamma driving current; 00 none, 01 low, 10 medium, 11 high
(p. 233, ll. 13329-13343). HEX A5h.
Status: NOT IMPLEMENTED in the driver; the panel test writes AAh and AFh
(sets 8, 9), not judged in the run.

### C5h VCMPCTL -- VCOM Control (p. 234, l. 13383)

One parameter: VCMP[5:0] (p. 234, l. 13390). VCOM = 0.300 V + 0.025 V x VCMP,
0.300-1.875 V (a closed form for the printed table, p. 234, ll. 13394-13411); VCOM + VMF offset < 1.875 V
(p. 234, l. 13413). HEX 1Ch (1.000 V).
Status: SENT as three bytes 00h 12h 80h (`st7365p.c:116`); the register has one
parameter, so the first byte gives VCMP 0 (0.300 V); what the controller does
with the two extra bytes is not documented. READ through SPIRC, MEASURED
`00 00` (BOARD). MEASURED by eye: set 16 (C5h 3Fh, no F0h) shows striping
(BOARD).

### C6h VCM Offset -- VCOM Offset Register (p. 235, l. 13457)

One parameter: D7 VMFSEL (0 = offset from NVM, 1 = from VMF_REG), D5-D0
VMF_REG, two's complement -32 to +31 steps (p. 235, ll. 13464-13494). HEX 00h.
Status: NOT IMPLEMENTED; the NVM offset applies.

### D0h NVMADW -- NVM Address/Data Write (p. 236, l. 13538)

Two parameters: PROG_ADDR[4:0] (00000 ID1, 00001 ID2, 00010 ID3, 00011
VMF_REG; others not allowed), PROG_DATA[7:0] (p. 236, ll. 13545-13566). NVM
writing needs 7.5 V on VPP at more than 10 mA (p. 41, ll. 2037-2040) and EXTC
high (p. 43, ll. 2135-2137).
Status: NOT IMPLEMENTED and not to be implemented: VPP and EXTC are on the
module, not the mainboard connector J401, and the panel test excludes NVM
commands (`paneltest_sets.h` header).

### D1h NVMBPROG -- NVM Byte Program (p. 237, l. 13608)

Three parameters PROGCODE[23:0]; C3AA3Ch starts an auto byte program
(p. 237, ll. 13615-13621).
Status: NOT IMPLEMENTED (as D0h).

### D2h NVMSTRD -- NVM Status Read (p. 238, l. 13660)

Read: dummy; ID2CNT/ID1CNT; VMFCNT/ID3CNT (program counts 0-4); BUSY; VMF
(p. 238, ll. 13667-13697). The opcode row prints D1h's bits (p. 238, l. 13665).
Status: NOT IMPLEMENTED. A read would show whether this module's IDs and VCOM
offset were ever programmed (the FFh IDs suggest the IDs were not; INF).

### D3h RDID4 -- Read ID4 (p. 239, l. 13740)

Read: dummy, 00h, ID4[15:8] = 77h, ID4[7:0] = 96h, "the IC model name"
(p. 239, ll. 13747-13759). The opcode row prints `1 0 1 1 0 1 1 1` (B7h)
(p. 239, l. 13745).
Status: READ, 4 bytes; MEASURED raw `00 3b cb 00`, shifted one bit
`00 77 96` (BOARD): the value this datasheet documents.

### E0h PGC -- Positive Gamma Control (p. 240, l. 13801)

Fourteen parameters (p. 240, ll. 13809-13835): V63P[3:0]/V0P[3:0]; V1P[5:0];
V2P[5:0]; V4P[4:0]; V6P[4:0]; J0P[1:0]/V13P[3:0]; V20P[6:0];
V36P[2:0]/V27P[2:0]; V43P[6:0]; J1P[1:0]/V50P[3:0]; V57P[4:0]; V59P[4:0];
V61P[5:0]; V62P[5:0]. HEX F0 03 05 09 0C 0F 3E 77 4F 0F 17 17 21 23. The
description area holds only "Adjust the gamma characteristics of the TFT
panel" (PDF page 240 image checked; no figure). The
voltages follow section 4, gamma.
Status: SENT as 15 bytes 00 03 09 08 16 0A 3F 78 4C 09 0A 08 16 1A 0F
(`st7365p.c:110-111`), one more than the register holds. READ through SPIRC
(`fb10`, `fb11`, `fb1d`), MEASURED `00 00` (BOARD).

### E1h NGC -- Negative Gamma Control (p. 241, l. 13876)

Fourteen parameters with the same layout for the negative side
(p. 241, ll. 13884-13910); HEX as E0h. Default rows read "Refer to description"
(p. 242, ll. 13942-13948).
Status: SENT as 15 bytes 00 16 19 03 0F 05 32 45 46 04 0E 0D 35 37 0F
(`st7365p.c:112-113`).

### E2h DGC1 -- Digital Gamma Control 1 (red) (p. 242, l. 13953)

Sixty-four parameters DGAM_R00-R63, the red look-up table (p. 242, ll. 13960-13972).
Status: NOT IMPLEMENTED (section 4, digital gamma).

### E3h DGC2 -- Digital Gamma Control 2 (blue) (p. 243, l. 14012)

Sixty-four parameters DGAM_B00-B63 (p. 243, ll. 14019-14031). The opcode row prints
E2h's bits (p. 243, l. 14017).
Status: NOT IMPLEMENTED.

### E8h DOCA -- Display Output Ctrl Adjust (p. 244, l. 14073)

Eight parameters: 40h, 8Ah, 00h, 00h, then 0010 S_END[3:0], G_START[5:0],
G_EQ/G_END[5:0], 33h (p. 244, ll. 14080-14094). Source equalizing time = 9 us +
1.5 us x S_END (00h 9 us to 0Fh 31.5 us; closed form for the printed
table); gate start and end = (n + 1) Tclk, Tclk = 4/osc at 60 Hz (pp. 244-245, ll. 14096-14151); G_EQ gate EQ on or off
(p. 245, l. 14145). HEX 40 8A 00 00 25 0A 38 33.
Status: NOT IMPLEMENTED.

### F0h CSCON -- Command Set Control (p. 245, l. 14185)

One parameter: C3h enables command 2 part I, 96h part II; 3Ch and 69h disable
them (p. 245, ll. 14192-14206). HEX 00h; status defaults N/A (p. 246, ll. 14234-14240).
Status: NOT IMPLEMENTED in the driver. Sent by the panel test as C3h then 96h
(`st7365p.c@b47cda40:458`) in sets 2-13, 15 and 18; the vendor ST7796SP sequence sends the
same pair after its Command Table 2 writes, with the comment "?? lock
manufacturer commands" (`SPI-LCD.c:317-318`).

### FBh SPIRC -- SPI Read Control (p. 246, l. 14248)

One parameter: D4 SPI_REN read enable, D3-D0 SPI_CNT read parameter number
(p. 246, ll. 14255-14259; bit positions read from the PDF page 246 image). HEX
00h. The extraction describes no read sequence that uses it.
Status: READ path of the panel test: FBh with 1xh, chip select raised, then
the register's command clocked as a read, then FBh 00h (`st7365p.c@b47cda40:568-591`).
MEASURED: every such read returned `00 00` under every set, locked or
unlocked (BOARD); with no positive control, the observation is that this
sequence returns zero.

### Commands outside both tables

- **F7h** is sent by the driver as A9h 51h 2Ch 82h (`st7365p.c:125`). Neither
  Command Table 1 nor Command Table 2 lists it (pp. 135-144, ll. 6612-7326); its effect
  on the ST7365P is undocumented. The vendor ILI9488P branch sends it
  (`SPI-LCD.c`, ILI9488P case), the ST7796SP branch has it commented out.
- **E9h** appears in the vendor `sd_boot` sequence (`lcdspi.c:662`), not in
  either table, and not in the driver.
- **26h** (gamma set) is named by the 0Dh description (p. 159, l. 8218) but has
  no command page.

## 4. Hardware questions

### 4.1 Is a tearing-effect signal wired on the PicoCalc mainboard?

No (DOC, schematic image and label count). The controller's TE output
(p. 43, ll. 2132-2134) is J401 pin 35, which carries the net label TE on a short
stub. In a 600 dpi render of the schematic PDF around J401 that stub ends
without a wire or junction, unlike the IM pins (wired to ALDO3) and the DB
pins (wired to ground). `pdftotext -bbox` over the whole sheet finds the
word TE once, at J401, so the TE net has a single node; LEDA, LEDK and
LCD_BL, by contrast, each occur twice and reach the backlight LED driver. The
text extraction alone cannot decide this: it prints "TE 35 34" (schematic
text line 44) without showing wires, and the routed IM pins print the same
way (lines 16-18). The Pico headers J301/J302 carry no TE net. So TEON (35h) and STE (44h) have no reader on this board; using
them needs a wire from the flex connector's pin 35 to a free GPIO, a hardware
change.

### 4.2 Vertical sync from GSCAN (45h), and what it costs

GSCAN returns the line the panel is scanning, counting VSYNC + VBP + VACT +
VFP lines from the first VSYNC line (p. 201, ll. 11068-11072). At the shipped
the rate the B1h formula gives if RTNA, VFP and VBP hold the HEX-column
values 10h, 02h, 02h is 60.06 Hz with 484 lines (480 + VFP 2 + VBP 2), a line of
1 / (60.06 x 484) = 34.40 us and a frame of 16.65 ms (INF). The datasheet gives
those three fields no reset value (N/A, p. 220, ll. 12383-12389) and the
driver never sends B5h, so this rate, line period and frame time are computed
from an assumption and unmeasured on the board; the scanline read below
decides. Every figure in this section that uses 34.4 us, 484 lines or 16.65 ms
carries the same condition.

Cost on the wire (INF): a GSCAN read is 8 command clocks, one dummy clock if
it behaves like the documented multi-byte serial reads (p. 95, l. 4787; the
GSCAN flow chart shows a dummy read without a serial-mode variant,
p. 202, l. 11117), and 16 data clocks, 25 clocks in all: 8.0 us at the panel test's 3.125 MHz, 3.75 us at
the 6.67 MHz the 150 ns TSCYCR allows (p. 54, l. 2633). Each read also costs two
PL022 clock changes and a chip-select cycle, and it must sit between pixel
transfers because the panel and the reads share SPI1 and one chip select. A
read once per frame occupies the bus 0.05 % of the time; polling the line
every 34 us would occupy about a quarter of it at 3.125 MHz. The useful
pattern is one read, then a timed wait computed from the line period.

Falsifier for the dummy-clock assumption: read GSCAN twice a known interval
apart; the two values must differ by interval / line period modulo the line
count after the one-bit shift, and by twice that without it. The 34.40 us and
484 of the assumed case are predictions; the measured differences give the
actual line period and line count. GSCAN has not been read on the board.

What vertical sync can buy here (INF):

- A full-screen write cannot be tear-free at this clock. 320 x 320 RGB666 is
  307,200 bytes, 196.6 ms at 12.5 MHz, 11.8 frames; RGB565 is 204,800 bytes,
  131.1 ms, 7.9 frames. Even at the 15.15 MHz TSCYCW limit RGB565 takes
  108 ms.
- A band can be, if the assumed line period holds (INF, conditional on the
  HEX-column assumption above). Following the slow-writer example of page 262
  (p. 262, ll. 15082-15085), a write that starts just behind the scan line stays
  ahead of the next scan pass while k x (write time per line) <= frame time
  + k x (scan time per line). A full-width line takes 614.4 us in RGB666 and
  409.6 us in RGB565 at 12.5 MHz against 34.4 us of scan, so a band of up to
  28 full-width lines (RGB666) or 44 (RGB565) updates without a tear when
  started from a GSCAN reading. A 6x10 console cell (191 bytes with its
  window, 122 us) is far inside that limit.
- Scrolling is already tear-free without any sync: a new VSCSAD value takes
  effect at the next panel scan (p. 191, ll. 10365-10367).

### 4.3 Reachable frame rates

The B1h formula (p. 219, ll. 12354-12358) with FRS 0-15, RTNA 0-31 and the
HEX-column porches VFP = VBP = 2 (assumed for the shipped panel; the reset
values are N/A) reaches 10^7 / (679 x 484) = 30.43 Hz at FRS 0, RTNA
1Fh up to 10^7 / (168 x 484) = 122.98 Hz at FRS Fh, RTNA 0 (DOC formula,
INF arithmetic). With VFP = VBP = FFh the floor drops to
10^7 / (679 x 990) = 14.88 Hz. B5h, which sets the porches, must be written
before Sleep Out (p. 223, l. 12638). DIVA[1:0] divides the internal clock by 1, 2,
4 or 8 (p. 219, ll. 12338-12348) but does not appear in the formula, so a DIVA
row is an inference that the rate divides with it.

| Target (Hz) | FRS | RTNA | DIVA | VFP / VBP | Rate (Hz) | Class |
| --- | --- | --- | --- | --- | --- | --- |
| 24 | 0h | 1Fh | 0 | 67 / 67 | 23.99 | DOC formula; porches through B5h before SLPOUT |
| 24 | 7h | 06h | 1 | 2 / 2 | 24.02 | INF: assumes DIVA = 1 halves the rate; the formula has no DIVA term |
| 30 | 0h | 1Fh | 0 | 6 / 5 | 29.99 | DOC formula; porches through B5h |
| 30 | Ah | 10h | 1 | 2 / 2 | 30.03 | INF, as above |
| 40 | 5h | 1Dh | 0 | 2 / 2 | 39.96 | DOC formula |
| 50 | 8h | 15h | 0 | 2 / 2 | 50.03 | DOC formula |
| 60 | Ah | 10h | 0 | 2 / 2 | 60.06 | DOC formula; the HEX-column example values and the driver's A0h, with RTNA and porches unsent and assumed |
| 75 | Ch | 0Bh | 0 | 2 / 2 | 75.13 | DOC formula |
| 90 | Eh | 1Eh | 0 | 2 / 2 | 89.83 | DOC formula |
| 120 | Fh | 04h | 0 | 2 / 2 | 120.12 | DOC formula |

The driver sends B1h A0h for the 60.06 Hz row (`st7365p.c:120`) and never sends
RTNA's companion B5h, so the shipped rate is the 60.06 Hz row only under the
HEX-column assumption and is unmeasured. The only
board observation is set 15 (FRS 0, RTNA 10h, a formula rate of 31.1 Hz),
which the owner saw flicker. The idle-mode and partial-mode line periods
(B2h RTNB, B3h RTNC) have no printed formula. Lower rates trade flicker
(seen at 31 Hz) for a longer frame, which widens the tear-free band of 4.2;
higher rates shorten the GSCAN window arithmetic of 4.2 but do not
change the SPI-bound write time.

### 4.4 Can more than 320 rows or columns ever show?

Columns: no (DOC). The controller has 320 RGB source channels, 960 source
lines (p. 8, ll. 230-234), and X addresses 0-319 (p. 250, l. 14398); CASET above
013Fh is ignored with MV = 0 (p. 174, ll. 9185-9189).

Rows: no on this glass (BOARD). The controller RAM is 320 x 480 x 18 bit
(p. 248, ll. 14311-14312) and it drives 480 gate lines (B6h NL 3Bh). In the board
run, pattern 7 of the panel test drew ten 32-row bands over RAM rows 0-319,
set VSCRDEF 0 / 480 / 0 and VSCSAD 160 (`paneltest.c` pattern 7 on the
branch); the photograph shows five bands over the top half of the glass and
undrawn RAM noise over the bottom half. The glass therefore shows display
lines 0-319, and at scroll start 160 those lines hold RAM rows 160-479. With
GS = 0 and SM = 0 the first 320 display lines are gate outputs G1-G320
(INF from pp. 225-226, ll. 12789-12823).

The 160 rows the glass does not show at a given scroll start are still
reachable: a scroll-start change brings them onto the glass at the next panel
scan. That makes them a 160-row staging area for pre-drawn content (INF;
RAM holds 480 rows, so a full 320-row second page does not fit).

### 4.5 Partial mode

PTLON (12h) shows only rows PSL-PEL of PTLAR (30h); B6h PT sets what the
sources drive in the non-display area (V63, V0, AGND, Hi-Z) and PTG the gate
scan there (normal or interval every ISC frames); B3h sets the partial-mode
line period before Sleep Out (pp. 224-225, ll. 12727-12757, p. 221, l. 12470). On the
PicoCalc the lines that partial mode would blank are off the glass already
(4.4); the gain would be lower drive work on lines 320-479, of unmeasured
size. The datasheet does not say how partial mode interacts with vertical
scrolling, which the console uses on every scroll; the partial-mode and
scroll flow charts (p. 182, ll. 9703-9733; p. 185, ll. 9880-9930) each start from
normal mode and are silent on combining them. NOT IMPLEMENTED; nothing
blocks a trial except that interaction.

### 4.6 Idle mode

IDMON (39h) shows eight colors from the MSB of each channel and switches to
the B2h line period (p. 193, ll. 10506-10510). The console palette has sixteen
colors (`st7365p.c:95-102`); in idle mode its eight bold and normal pairs would
collapse by MSB. Power saving is unmeasured. NOT IMPLEMENTED.

### 4.7 Sleep

SLPIN (10h) stops the converter, oscillator and scan while RAM keeps its
contents (p. 163, ll. 8505-8509); typical controller current falls from 12 mA to
0.025 mA (p. 50, ll. 2392-2393). The rules: 5 ms after SLPIN or SLPOUT before
the next command, 120 ms between SLPOUT and SLPIN (p. 163, ll. 8519-8525), 120 ms
after RESX before SLPOUT (p. 60, l. 2955). The backlight is separate and belongs
to the keyboard controller, so a console blank needs both. NOT IMPLEMENTED
beyond the SLPOUT at init.

### 4.8 Inversion

Two separate mechanisms. INVON/INVOFF (21h/20h) invert the pixel data; the
driver sends INVON and the board reports it on (09h, 0Dh). DIC (B4h) chooses
the drive-polarity pattern: column, 1-dot or 2-dot (p. 222, ll. 12522-12533); the
driver sends 2-dot, the B4h HEX value is 1-dot. The panel test's sets 6 and 7
(column, 1-dot, both after CSCON) were not judged in the run.

### 4.9 Brightness, dimming and CABC

The controller's brightness path ends at two pads, CABC_PWM (PWM to an LED
driver) and CABC_ON (LED enable) (p. 44, ll. 2185-2190). J401 carries neither: its
backlight pins are LEDA (39) and LEDK (40), whose labels occur a second
time at the backlight LED driver, and that driver is switched by LCD_BL,
whose label occurs at the keyboard controller's PA7 and at the driver
(schematic text line 19 and lines 79-83; label positions from
`pdftotext -bbox`). So WRDISBV (51h),
WRCTRLD (53h), the CABC modes of WRCABC (55h) and WRCABCMB (5Eh) cannot
change the light on this board (DOC schematic, INF that the module does not
loop CABC_PWM to an LED driver of its own, since LEDA and LEDK are fed from
the mainboard). WRCABC's CECTRL color enhancement is an image-path feature
whose transfer function the extraction does not give.

### 4.10 Analog gamma, VCOM and GVDD

GVDD (VAP) = 3.55 V + 0.05 V x VRH + (VCOM + VCOM offset), VRH 00h-27h
(C1h, p. 231, ll. 13177-13217; the closed forms in this paragraph fit the printed
tables); VAN = -(3.55 V + 0.05 V x VRH) + (VCOM + VCOM offset)
(C1h, p. 231, l. 13224 to p. 232, l. 13272), so VAP and VAN share the VCOM
term and differ in the sign of the VRH term only; at VCOM = 1.0 V, offset 0, the
VAN rail sits 2 V above the negative of VAP. VCOM = 0.300 V + 0.025 V x
VCMP (C5h, p. 234, ll. 13394-13411), plus the C6h offset in steps, with VCOM +
offset < 1.875 V (p. 234, l. 13413). VBP = VCOM + VCOM offset (p. 263, l. 15162).
The positive levels are a resistor ladder between VAP and VBP; R is one
ladder unit (pp. 265-266, ll. 15224-15294):

- Register knots from the full span: VP0 = (VAP - VBP)(129 - V0P)/129 + VBP;
  VP1, VP2, VP20, VP43 = (VAP - VBP)(128 - VxP)/129 + VBP; VP61, VP62 =
  (VAP - VBP)(64 - VxP)/129 + VBP; VP63 = (VAP - VBP)(23 - V63P)/129 + VBP.
- Knots from a sub-span: VP4 = (VP2 - VP20)(57 - V4P)/60 + VP20, VP6 with
  47, VP13 with 21; VP27 = (VP20 - VP43)(20 - V27P)/25 + VP43, VP36 with 11;
  VP50 = (VP43 - VP61)(54 - V50P)/60 + VP61, VP57 with 44, VP59 with 34.
- Levels between knots are linear: VPk = (VPa - VPb)/(b - a) x (b - k) + VPb.
- J0P and J1P choose the percentages for VP3, VP5, VP7-VP12 and VP51-VP56,
  VP58, VP60 from a four-column table (p. 264, ll. 15191-15212).
- The negative side has the same form about VBN: VN0 = VBN - (VBN - VAN)(129 -
  V0N)/129 and so on (pp. 266-267, ll. 15298-15369). The extraction prints no
  value for VBN (only VBP, p. 263, l. 15162), so the negative levels are
  expressed through VAN and an undocumented VBN, not as a mirror of the
  positive ones.

The voltage-generation and source-voltage diagrams of pages 281-283
(pp. 281-283, ll. 16006-16102) did not survive the extraction and were not read from
the PDF.

For strictly black and white text only levels 0 and 63 matter, so V0P/V0N,
V63P/V63N and the VAP-VBP span (C1h, C5h) set the two text levels. The 16-color
console is not black and white only: `lcd_palette` (`st7365p.c:95-101`) holds
channel values 0, 60, 90, 100, 120, 127, 200, 205, 238 and 255, and a glyph's
foreground and background pick palette entries. A channel at one of the
intermediate values selects a gray level between 0 and 63 after the 8-bit to
6-bit reduction, so the gamma knots between V0 and V63 (E0h, E1h) shape the
rendered text for every palette entry other than the pure black, white and
saturated-channel ones. The endpoint-only conclusion holds for black and white
text alone.

### 4.11 Digital gamma

E2h (red) and E3h (blue) each hold a 64-entry table; the LUT output passes a
dithering stage to the 6-bit source drivers (p. 267, ll. 15376-15395). The tables
act "when bit DGMEN be set to 1" (p. 267, l. 15399), but no command in either table
carries a DGMEN field in the extraction, and there is no green table. NOT
IMPLEMENTED; blocked by the missing enable.

### 4.12 Dithering

B9h turns dithering on for 16.7M-color input only (p. 229, l. 13054). The 4-line
SPI chapter documents 16-bit and 18-bit input only (p. 96, ll. 4823-4826), so B9h
has no documented effect on this wiring (INF).

### 4.13 Pixel formats

| COLMOD | Bytes per pixel on SPI | Bytes per 6x10 cell | Full 320x320 frame at 12.5 MHz | RAM fill |
| --- | --- | --- | --- | --- |
| 55h (16 bit) | 2, R5 G6 B5 (p. 96, l. 4843) | 120 | 131.1 ms | low red and blue bit from B7h EPF (p. 228, ll. 12992-13002) |
| 66h (18 bit) | 3, six high bits each (p. 97, l. 4881) | 180 | 196.6 ms | direct |
| 77h (24 bit) | no SPI figure | -- | -- | -- |

With EPF = 11 (the driver's B7h C6h) a 565 pixel fills r0 = b0 = G0, so pure
black and white are identical in both formats (INF from p. 228, l. 13002).
RAMRD and RDMEMC return 18-bit data whatever COLMOD says (p. 199, ll. 10895-10897).

### 4.14 Tearing-effect timing, for a board that wires TE

At 60 Hz the TE low time (display updating) is at least 13 ms and the high
time (no update) at least 1000 us; in mode 2 the per-line low time is at least
16 us and the high time at most 500 us (p. 260, ll. 14995-15000). Mode 2 gives one
V-sync and 480 H-sync pulses per field (p. 258, ll. 14931-14932).

## 5. Claims that did not survive

Each item names the claim, where it is made, and the datasheet line or board
observation that contradicts or narrows it.

**Against the datasheet or the board run**

1. "The driver is write-only, so every judgment is optical" (crispness
   report, section 7). The board run read 0Ah, 0Bh, 0Ch, 0Dh, 0Eh, 0Fh, 09h
   and D3h over SDO to GP12 and got the expected values (BOARD).
2. "The 160 unshown rows stay available as off-screen RAM only if the host
   reads them back" (crispness report, section 8). Pattern 7 showed RAM rows
   320-479 on the glass through VSCSAD 160 (BOARD); a scroll-start change, not
   a readback, brings them into view (p. 190, ll. 10312-10314).
3. "Every gamma, power, VCOM, frame-rate and inversion write may be
   discarded" without F0h (crispness report, section 1, item 6, a
   hypothesis). Weakened, not refuted: set 16, which writes C5h 3Fh without
   F0h, showed striping by eye (BOARD, one unphotographed observation; sets
   13 and 14 not reported).
4. "No board run exists for the DiscoBSD kernel" (crispness report, section
   10) and "no PicoCalc has booted this kernel" (cited there). Superseded by
   the 2026-11-17 run.
5. "All-zero or all-one bytes mean SDO is not reaching GP12, and every
   later readback is void" (`PANELTEST.md` prediction 1 on the branch). The
   datasheet documents FFh for all three ID bytes after power-on and both
   resets (p. 147, ll. 7524-7532; p. 215, l. 12053, p. 216, l. 12132, p. 217, l. 12211), so the
   all-one replies of 04h and DAh-DCh are the documented values, and 0Ah-0Fh
   read non-trivial values over the same line (BOARD).
6. "The controller answers the ST7796 family ID" (run record addendum).
   Narrowed: the ST7365P datasheet prints RDID4 = 00h 77h 96h for the ST7365P
   itself (p. 239, ll. 13751-13753), so the reply does not distinguish an ST7365P
   from an ST7796-family part.
7. "B4h through FBh 10h reads 02h under set 0 if the shipped write acts, the
   datasheet default 01h if it is gated; E0h parameter 0 reads F0h under set
   3" (`PANELTEST.md` prediction 3). Every SPIRC read returned 00h under
   every set (BOARD); the read sequence, not the gate, is what the run tested.
8. "TSCYCR (150 ns, page 77)" (`PANELTEST.md` line 31 and the read comment
   at `st7365p.c@b47cda40:565-566`). TSCYCR is on page 54 (p. 54, l. 2633); page 77 is the
   16-bit 8080 read format.
9. "the SPI figure on page 94" for the 8-bit read without a dummy cycle
   (`st7365p.c@b47cda40:598`, `PANELTEST.md` line 69). The 8-bit read figure follows the
   page 94 footer and sits on page 95 (p. 95, l. 4736); page 94 carries the read
   sequence text (p. 94, ll. 4721-4727).
10. Page numbers in the `paneltest_sets.h` header comment on the branch:
    VCMPCTL C5h "page 235" is page 234 (p. 234, l. 13383); DIC B4h "page 223" is
    page 221 (p. 221, l. 12508); FRMCTR1 B1h "page 220" is page 219
    (p. 219, l. 12324).
11. "Formula read from the page image" for B1h (crispness report, section 2).
    The text extraction carries the formula (p. 219, ll. 12356-12358); the page
    image agrees.

**Against the vendor sources the driver comments cite** (outside the
datasheet; listed separately)

12. "RGB666 in three bytes (COLMOD 66h), the format every vendor driver for
    this board uses" (`st7365p.c:30-31`; `PICOCALC.md` "as every vendor driver
    for this panel sends them"). The vendor PicoMite ST7796SP sequence, the one
    `OPTION PICOCALC` selects, sends COLMOD 55h (`SPI-LCD.c:307`).
13. The values come from "the vendor's PicoMite ILI9488P (320x320)
    initialization ... with three deliberate differences ...: the command E9h,
    absent from the ST7365P command table, is not sent" (`st7365p.c:36-40`). The
    PicoMite ILI9488P branch (`SPI-LCD.c`, lines 337-436) sends no E9h; E9h is
    in the `sd_boot` sequence (`lcdspi.c:662`). The same reasoning would
    drop F7h, which is equally absent from both command tables
    (pp. 135-144, ll. 6612-7326) and which the driver sends (`st7365p.c:125`).

## 6. Open items and the measurement that decides each

- Command 2 gating: judge sets 13 and 14 against 15 and 16 on pattern 5 with
  photographs at fixed exposure.
- The SPIRC read sequence: find a positive control (a Command Table 2
  register whose written value is visible, read in the same chip-select cycle
  as FBh and in a separate one).
- GSCAN alignment and line period: the two-reading test of 4.2.
- DIVA's effect on the rate: set DIVA = 1 with FRS Ah, RTNA 10h and time
  GSCAN wraps; the formula predicts no change, the INF rows a halving.
- B6h NL = 27h: does the frame time shrink, and do lines 0-319 still map to
  the glass.
- Subpixel stripe order under MADCTL 48h: pattern 4 under a loupe.
- RAMRD over 4-line SPI: dummy-clock count, by reading back a known pixel.
- 0Fh D7 and D6 read set; their meaning is undocumented in the extraction.

## 7. Datasheet misprints, as printed

| Line | Printed | Context |
| --- | --- | --- |
| p. 141, l. 7093 | B4h opcode bits `0 0 0 0 1 0 0 1` | command list; the B4h page prints `1 0 1 1 0 1 0 0` (p. 221, l. 12513) |
| p. 168, l. 8746 | "12H NORON" | NORON is 13h (p. 168, l. 8750) |
| p. 170, l. 8893 | INVON "is used to recover from display inversion mode" | INVOFF's description |
| p. 176, l. 9314 | RASET range "< (01DFh)" | CASET prints "<=" (p. 174, l. 9189) |
| p. 183, l. 9777 | "The 4th & 5th parameter BFA [6:0]" | BFA is parameters 5-6, 16 bits (p. 183, ll. 9759-9761) |
| p. 194, l. 10544 | IDMON restriction "already in idle off mode" | IDMOFF's text |
| p. 213, l. 11906, p. 214, l. 11962 | AAh and AFh rows labeled "RDID1" | the commands are RDFCS and RDCFCS |
| p. 219, l. 12340 | DIVA table column "Inversion mode" | the column lists Fosc divisions |
| p. 224, l. 12682 | B6h heading "Blanking Porch Control" | B6h is Display Function Control |
| p. 226, l. 12905 | NL "8 * (NL5:0)+1) lines" | read as 8 x (NL + 1) |
| p. 238, l. 13665 | D2h opcode bits of D1h | |
| p. 239, l. 13745 | D3h opcode bits `1 0 1 1 0 1 1 1` (B7h) | |
| p. 243, l. 14017 | E3h opcode bits of E2h | |
| p. 143, l. 7240, p. 143, l. 7250 | NGC list rows "V4P[4:0]", "V43P[5:0]" | the E1h page prints V4N[4:0], V43N[6:0] (p. 241, l. 13890, p. 241, l. 13900) |
| p. 265, l. 15277, p. 267, l. 15357 | "(V5P0-VP57)", "(V5N0-VN57)" | the neighboring rows use VP50, VN50 |
| p. 250, l. 14406 | "horizontal addressing mode (V=0)" | MV = 0 |
