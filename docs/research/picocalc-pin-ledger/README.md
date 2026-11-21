# PicoCalc mainboard pin ledger

The two TSV files here record every net the PicoCalc V2.0 mainboard schematic
draws between the Pico socket, the keyboard controller, the PMU, the panel,
the SD socket, the PSRAM, the audio path and the USB-C switches. No continuity
or voltage measurement has been made on a board; every row is a reading of the
drawing, and the rows marked "inferred" say which part of the reading is not
drawn.

## Source

The schematic is `docs/picocalc/clockwork_Mainboard_V2.0_Schematic.pdf` in the
notes repository at commit 71b56d3ffe56 (one page). Keyboard-controller
behavior comes from the vendor firmware source in `docs/picocalc/code/
picocalc_keyboard/` of the same repository (`BIOSVERSION 0x16`); the v1.7
binary is not read.

## Method

Each label and pin number comes from the PDF text layer with its page
coordinates (points). The coordinates were checked against 300 dpi crops of the
page, which the evidence column names by file name (for example
`j301_top.png`); the crops are not stored here. A net is joined across the
sheet by label identity: two labels of the same name are read as one net where
the sheet draws no wire between them. That reading is an inference from the
drawing's labels, not a traced conductor, and it carries the joins of `3V3_OUT`
(J301 pin 5 and the power symbols), `GP28` (J301 pin 7 and J703 pin 7), `LEDA`
and `LEDK` (panel connector and backlight driver), and J703's `GP2`-`GP5` and
`GP21` (whose names at the Pico socket are also the Pico's pin names). The
ledger marks those rows inferred, and the Unresolved table lists the
expansion-header joins: nothing here establishes that a J703 pin reaches a Pico
pin until the copper or a continuity test shows it. The Gerber layers locate
the panel connector J401 (`BLOCK-DIAGRAM.md`) and were not traced for
connectivity. The CKS32F103
assignments are confirmed against the firmware source, not against the 64-pin
table of its datasheet, except pins 46 and 49.

## Files

`BLOCK-DIAGRAM.md` draws the board's subsystems from these rows, places J401
on the board from the vendor Gerber set and checks five claims about the panel
module. Both TSV files are tab-separated with no header row.

| File | Rows | Columns |
| --- | --- | --- |
| `pico-pins.tsv` | 40 (J301 pins 1-20, J302 pins 1-20) | socket, socket pin, Pico pin name, net labels, destinations, series or attached parts, RP2040 function implied, evidence, confidence |
| `board-nets.tsv` | 160 | block, pin or part, net or signal, destinations, attached parts or detail, software relevance, evidence, confidence |

`board-nets.tsv` blocks: U201 keyboard controller (53 rows), the J401 panel
connector (40, one per pin), the AXP2101 PMU U101 (18), the SD socket J303
(10), the USB-C switches and DIP switch (10), the PSRAM U301 (8), the audio
path (7), the backlight, key and power parts (7), U201 supplies (6) and
U201 programming (1). About 98 distinct named nets appear, 34 of them on the
Pico sockets.

Confidence reads "read directly" for a label or wire the drawing shows, and
"inferred" where a polarity, topology or value comes from outside the sheet.

## Not reachable from the Pico

These functions have a path on the sheet that does not reach a Pico pin.

| Function | Path on the sheet | Port access |
| --- | --- | --- |
| Battery percent and charging | AXP2101 on the keyboard controller's private I2C (PB10, PB11); IRQ to PC9 | keyboard register 0x0B; GP29 reads VSYS/3 on the Pico module, which is PICO_VSYS after U102, not the battery |
| USB-C VBUS presence | AXP2101 only; Pico VBUS has no net | none; GP24 sees only the Pico's own micro-USB |
| Headphone detect | keyboard controller PC12 only | no firmware register |
| Amplifier enable (PA_EN) | keyboard controller PA14 only; high turns the amplifiers on | the firmware sets it from the headphone detect; the Pico cannot mute |
| LCD backlight | PA8 PWM to U401 | keyboard register 0x05 |
| Keyboard backlight | PC8 PWM to U601 | keyboard register 0x0A |
| Power key and power-off | AXP2101 PWRON; keyboard controller | keyboard register 0x0E; the Pico cannot read the key |
| Pico reset and power | PICO_EN (PA13) is the only control; RUN and 3V3_EN are unwired | none (register 0x08 resets the controller; see `sys/arch/rp2040/doc/PICOCALC.md`) |
| Charge LED D101 and indicator D201 | AXP CHGLED; PC13 | none |

## Unresolved

| Item | Measurement that settles it |
| --- | --- |
| AXP2101 rail voltages (ALDO1-4, BLDO1-2); the firmware sets none, U301 is the 3.3 V part, and R302 pulls up to 3V3_OUT while its VCC is ALDO2 | multimeter on C112, C113, C114, C115, C117 and C118 |
| SD card-detect polarity; `sd.c` does not interpret it | multimeter from J303 pin 10 to ground with and without a card |
| U102 reverse conduction when micro-USB powers the Pico and PICO_EN is low; the WAS4603 datasheet is not in the notes | measure IPS with micro-USB plugged and PICO_EN low |
| WAS7227 SEL polarity, taken from the vendor flashing recipe | continuity or logic probe at U703 pin 10 against SW701 pole 1 |
| SW501 topology and the role of R509 (0R) | continuity at SW501 and the jack |
| J703 to the Pico: pins 2-7 are labeled `GP2`, `GP3`, `GP4`, `GP5`, `GP21`, `GP28` and pin 1 `3V3_OUT`, joined to the socket by label only | continuity from each J703 pin to the Pico socket pin of that name (J302 pins 4-7, J301 pins 14, 7 and 5), or the Gerber layers |

## Unused wiring

Nine connections are drawn and have no consumer in the port.

| Wiring | Conclusion |
| --- | --- |
| UART1: GP8/GP9 to controller PC11/PC10 (USART3 remap) | The vendor firmware never opens USART3: `Serial1(PA10, PA9)` is the debug UART on J702, and no Serial3, USART3, PC10 or PC11 appears in the firmware source. A UART1 driver on GP8/GP9 has no peer until that firmware changes, so none exists. |
| SD in native 1-bit mode: DAT0=GP16, DAT3=GP17, CLK=GP18, CMD=GP19 | A PIO SD driver could replace SPI mode. DAT1, DAT2 and WP are no-connect, so 4-bit mode and write-protect sensing cannot exist. |
| Panel TE (J401 pin 35) and RD (pin 31) | Both unwired: the panel has no tearing line. The read-back line SDO is wired and used. |
| GP28, labeled on J703 pin 7 | A free GPIO or ADC2 at the Pico; `gpio.c` offers it to programs. That it reaches J703 rests on the label (see Unresolved). |
| J702 UART0 taps TP1, TP2 | External serial access; the CH340C stays connected. |
| Keyboard controller USB (M_USB_DP, M_USB_DM, SW701 pole 2) | No use in the Pico software; the firmware has no USB code. |
| AXP IRQ to PC9 only | Not routed to the Pico, so the Pico can only poll the controller. |
| Controller PA7/ROW8 | A bare wire with no key; the firmware scans `NUM_OF_ROWS 7`. |
| Pico VBUS, RUN, 3V3_EN, ADC_VREF | No net on the sheet. |
