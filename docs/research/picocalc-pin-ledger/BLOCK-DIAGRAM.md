# PicoCalc mainboard V2.0: block diagram and panel connector placement

The mainboard joins a Raspberry Pi Pico to five subsystems: power, the
keyboard controller, the panel, storage and audio. Three of them answer
to the keyboard controller U201 rather than to the Pico: the AXP2101 PMU,
both backlights and the amplifier enable. The Pico reaches the controller
over one I2C bus and polls it, since no interrupt line crosses. The diagram
below is drawn from `board-nets.tsv` and `pico-pins.tsv` beside this file,
which read the schematic (`clockwork_Mainboard_V2.0_Schematic.pdf` in the
notes repository's `docs/picocalc/`, the vendor repository at
`fdcfe2c8`); each box names its reference designator and each arrow the
net or GPIO the ledger records.

```
 USB-C J701 ─┬─ VBUS_IN ─────────────► U101 AXP2101 PMU ◄── J101 2x18650 (VBAT)
             │                         │  I2C PB10/PB11, IRQ PC9 (to U201 only)
             │                         ├─ DCDC1 ─► MCU_3V3: U201, CH340C, both switches
             │                         ├─ ALDO1 ─► J303 microSD VDD
             │                         ├─ ALDO2 ─► U301 PSRAM VCC
             │                         ├─ ALDO3 ─► J401 panel VDD and IM0-IM2 straps
             │                         ├─ ALDO4 ─► U501 audio buffer, HP_DET pull-up
             │                         ├─ BLDO1 ─► U401 LCD backlight boost
             │                         ├─ BLDO2 ─► U601 key backlight boost
             │                         └─ VSYS (IPS) ─► U102 WAS4603 ─► PICO_VSYS
             │                                          EN = PICO_EN (U201 PA13)
             └─ D+/D- ─► U701 WAS7227 ─┬─► U702 CH340C ─► U703 WAS7227 ─┬─► Pico UART0 GP0/GP1
                         SEL2 (SW701)  └─► U201 USB       SEL1 = BOOT0  └─► U201 UART1

 U201 keyboard controller (CKS32F103, STM32F103 class), I2C1 slave 0x1F
   ├─ key matrix ROW1-7 x COL1-8, side keys KEY1-12
   ├─ PA8 LCD_BL ─► U401 TPS6116x ─► J401 LEDA / LEDK (pins 39, 40)
   ├─ PC8 KEY_BL ─► U601 TPS6116x ─► 54 key LEDs
   ├─ PA14 PA_EN ─► U502, U503 AW8010 amplifiers
   └─ PC12 ◄─ HP_DET from J501

 Raspberry Pi Pico in J301 / J302
   ├─ GP6 / GP7 I2C1 ──────────────► U201 (polled; no interrupt line)
   ├─ GP10-GP15 SPI1, DC, RST ─────► J401 panel FPC, ST7365P in 4-line SPI
   ├─ GP16-GP19 SPI0, GP22 SD_DET ─► J303 microSD
   ├─ GP2-GP5, GP20, GP21 QSPI ────► U301 ESP-PSRAM64H (GP2-GP5, GP21 also on J703)
   ├─ GP26 / GP27 PWM_L / PWM_R ───► U501 buffer ─► RC filters ─► SW501 volume
   │                                  ─► J501 headphones and U502 / U503 ─► J502 / J503 speakers
   └─ GP8 / GP9 UART1 ─────────────► U201 PC11 / PC10 (the firmware never opens it)
```

## Panel connector J401 on the board

J401 sits on the bottom side between the pin-20 ends of the Pico's two
header sockets, with the silkscreen word SCREEN below it. The vendor's Gerber set
(`PCB/CPI_V2.0_Gerber.7z`, copied to the notes repository's
`docs/picocalc/pcb/`) carries copper, mask, silkscreen, drill drawing and
outline layers and no pick-and-place file, so the position below is read
from the copper: `bottom.art` and `solder_bot.art` each hold one row of
40 equal pads at 0.5 mm pitch and no other row of that length.

| Quantity | Value | Source |
|---|---|---|
| Board outline | 91.70 x 165.61 mm (3.6103 x 6.5199 in), origin at the lower left corner, top view | `panel.art` |
| J401 pad row | y = 103.71 mm (4.0830 in) from the lower edge | `bottom.art` flashes |
| J401 pad span | x = 38.32 to 57.83 mm (1.5088 to 2.2766 in), center 48.07 mm, 2.2 mm right of the board center in top view | `bottom.art` flashes |
| Pitch | 0.50 mm (0.0197 in) | 39 gaps: 34 of 0.0197 in, 5 of 0.0196 in |
| Pad | 0.25 x 1.32 mm copper, 0.36 x 1.42 mm mask opening | apertures of `bottom.art` and `solder_bot.art` |
| Pin 1 | the pad at x = 38.32 mm in top view, the right end when the bottom side faces the viewer | silkscreen "1" and "40" beside the row |

The 40 nets on those pads are the J401 rows of `board-nets.tsv`: ground on
pins 1, 7, 13-28 and 38; IM0-IM2 (pins 8-10) and VDD (pins 36, 37) on ALDO3;
RESET on GP15; SDO, SDA, WR/SCL, DCX and CS on GP12, GP11, GP10, GP14 and
GP13 (pins 29, 30, 32, 33, 34); RD (31) and TE (35) without a net; LEDA and
LEDK on pins 39 and 40; the rest unconnected.

The renders that locate it are `docs/picocalc/pcb/renders/` in the notes
repository: `top.png` and `bottom.png` at 100 dpi and `j401-bottom.png`, a
300 dpi crop mirrored so its silkscreen reads, drawn by
`tools/render-gerber.py` there.

## Claims about the panel module, checked

A pasted summary (2026-11-21) made five claims about the panel; this table
records what the held sources say about each.

| Claim | Finding |
|---|---|
| The driver IC is the ST7365P | Agrees: the vendor repository carries `ST7365P_SPEC_V1.0.pdf` and its drivers program the ST7365P command set (`sys/arch/rp2040/doc/PICOCALC.md`). |
| J401's netlist is defined pin by pin in the schematic | Agrees, with one correction: the panel's logic supply is the AXP2101's ALDO3, not a 3V3 net, and the backlight runs from U401's boost output (LEDA) and its feedback node (LEDK). |
| J401 is a "0.5-40PBSX, 40-pin 0.5 mm bottom-contact FPC" | The label and the 0.5 mm, 40-pad row agree; bottom contact is unestablished, since no datasheet for 0.5-40PBSX was found (notes `docs/NOT-FETCHED.tsv`). |
| TXW392001B0 is a lead, not the panel's part number | Agrees with `panel-sources.md` in the notes repository: the number comes from one forum post, and no ClockworkPi document names the module. |
| The PCB files give J401's coordinates | The Gerber set lacks a placement file; the coordinates above come from the copper pads. |
