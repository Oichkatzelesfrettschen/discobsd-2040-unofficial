# PicoCalc battery and charging: what the RP2040 can read, and the driver plan

The owner asked to read the battery level and charging state and to build
the drivers behind them from the hardware sheets (decision D123, which also
keeps the vendor keyboard-controller firmware). The finding leads: the
RP2040 has no electrical path to the AXP2101 power-management unit. The PMU
sits on the keyboard controller's second I2C bus, and the vendor firmware
passes exactly two facts across to the Pico, a battery percentage and a
charging bit, both of which the kernel already exposes. Every other
quantity (cell voltage, charger input, charger phase, faults, rail
voltages) needs a firmware change, a board rework or a voltmeter. This note
traces the path, maps the registers, records two semantic defects of the
two facts that do cross, and decomposes the remaining work.

Evidence classes: schematic (S, the 300 dpi render of
`clockwork_Mainboard_V2.0_Schematic.pdf`, title block 2024-12-20), data sheet
(DS, AXP2101 V1.4, sections by number with the printed page), firmware
source (SRC-FW, `Code/picocalc_keyboard` at ClockworkPi commit e36ee963b4,
byte-identical to the notes copy), library source (SRC-LIB, XPowersLib),
tree source (SRC, `origin/main` 268483f7) and inference (I). No board run
belongs to this note.

## The path from the RP2040 to the PMU

| Link | Nets as wired | Evidence |
| --- | --- | --- |
| RP2040 to keyboard controller | GP6 SDA, GP7 SCL (I2C1) to U201 PB9, PB8 (`M_I2C1`), pulled up through 4.7 kOhm; the firmware's I2C target at 0x1F, about 96 kHz | S; `sys/arch/rp2040/dev/picocalc.h:38-45`; SRC-FW `conf_app.h:6` |
| Keyboard controller to PMU | U201 PB10 SCL, PB11 SDA (`M_I2C2`) to U101 SCK pin 40 and SDA pin 39 (`AXP_SCK`, `AXP_SDA`), pulled up through R105 and R106, 2.2 kOhm, to `MCU_3V3`; PMU IRQ pin 38 (`AXP_IRQ`) to U201 PC9 (`PMU_IRQ`) through R107, 4.7 kOhm | S; SRC-FW `conf_app.h:13-23` |
| PMU address | 7-bit 0x34 (the data sheet's 0x68/0x69 are the 8-bit write and read forms) | DS 6.3 p. 18; SRC-LIB `AXP2101Constants.h:3` |
| A second Pico channel | Pico UART1 TX GP8 to U201 PC11 (`M_UART3_RX`), UART1 RX GP9 to PC10 (`M_UART3_TX`) | S; the vendor firmware opens only `Serial1` on PA9/PA10 (SRC-FW `picocalc_keyboard.ino:36`), so USART3 is wired and idle |
| PMU-to-Pico power switch | U102 WAS4603 load switch: input `IPS`, output `PICO_VSYS`, enable `PICO_EN` from U201 PA13 | S; SRC-FW `picocalc_keyboard.ino:471-472`, `:546` |

`M_I2C2` appears at two places on the sheet, the PMU and U201, and on no
header (S, text-layer search of the net name). The PMU is therefore
reachable only through the firmware's register file. The UART1-to-USART3
pair is the one spare wired path between the two processors; it carries
nothing until a firmware change listens on it.

## What crosses today, and how fresh it is

The firmware keeps one byte for the Pico, register 0x0B (`REG_ID_BAT`): the
battery percentage in bits 0-6, bit 7 set while charging, 0 when the PMU
reports no cell (SRC-FW `picocalc_keyboard.ino:285-297`). The byte changes
at these events:

| Event | What the firmware stores | Source |
| --- | --- | --- |
| Boot | `sync_bat()`: percent, bit 7 if `isCharging()` | SRC-FW `:285-297`, `:597` |
| Every 20 s | percent, bit 7 if `isCharging()`; runs `low_bat()` | SRC-FW `:310-323` |
| Cell inserted | percent | SRC-FW `:371-379` |
| Cell removed | 0 | SRC-FW `:381-383` |
| Charge started | percent with bit 7 | SRC-FW `:439-447` |
| Charge done | percent with bit 7 | SRC-FW `:428-436` |
| Charger input inserted or removed | nothing (the IRQ only prints) | SRC-FW `:364-369` |

XPowersLib defines `getBatteryPercent()` as register 0xA4 when STATUS1 bit 3
reports a cell and -1 otherwise, and `isCharging()` as STATUS2 bits 7:5
equal to 001, the "charge" current direction (SRC-LIB
`XPowersAXP2101.hpp`; DS 6.13.2.1-2 p. 31, 6.13.2.88 p. 56). The firmware's
own copy of the library is not in the repository, so the revision it was
built with is unknown; the definitions above come from XPowersLib at
17132a5 (2026-11-09).

The kernel reads the byte through the shared keyboard poller in about
50 ms and serves it as `machdep.picocalc.battery` and
`machdep.picocalc.charging` (SRC `sys/arch/rp2040/include/picocalcvar.h:30-31`,
`sys/arch/rp2040/dev/picokbd.c:612-627`, `dev/picokbd_req.c:119-128`).
`picofetch` and `picotop` display them (`usr.bin/picofetch/hwinfo_sysctl.c:292-293`,
`usr.bin/picotop/picotop.c:423-429`). The driver work for what crosses is
complete; its board check is open (`sys/arch/rp2040/doc/PICOCALC.md:1015-1024`).

Two semantic defects follow from the firmware source:
- **Charging after the charge ends.** The charge-done interrupt stores the
  percentage with bit 7 set (SRC-FW `:428-436`), so `charging` reads 1 after
  the charge completes until the next 20 s refresh clears it through
  `isCharging()`. A reader sees "charging" for up to 20 s past the end.
- **No cell against an empty cell.** `PICOCALC.md:561-564` records that both
  read 0. The firmware narrows it: `low_bat()` runs only while a cell is
  present, counts consecutive refreshes at or below 20 %, and shuts the PMU
  down at a reading of 0 once that count reaches four (SRC-FW
  `battery.ino:46-64`, `picocalc_keyboard.ino:310-323`). A system that keeps
  running while `battery` reads 0 for more than about 80 s therefore most
  likely has no cell (I: four 20 s refreshes, the boot call counting as one).

## What does not cross

The firmware reads more than it passes on. On a short press of the power key
it prints charge state, cell voltage, charger-input voltage and system
voltage on its debug UART (`printPMU()`, SRC-FW `:236-283`, `:387-402`),
which leaves the board through the CH340 on the USB-C port, not through the
Pico. That print is the oracle a board session can hold the kernel's
reading against without changing anything.

The register map a richer driver would read, all through U201:

| Register | Content | DS section, page |
| --- | --- | --- |
| 0x00 STATUS1 | bit 5 charger input good, bit 4 BATFET, bit 3 cell present, bit 1 thermal regulation, bit 0 input current limit | 6.13.2.1, p. 31 |
| 0x01 STATUS2 | bits 6:5 current direction (standby, charge, discharge), bit 4 system on, bit 3 input voltage regulation, bits 2:0 charger phase (trickle, pre, CC, CV, done, not charging) | 6.13.2.2, p. 31 |
| 0x10 | bit 0 power off | 6.13.2.7, p. 32 |
| 0x18 | gauge, cell charger and watchdog enables | 6.13.2.14, p. 34 |
| 0x1A | low-battery warning levels; the firmware sets 5 % and 1 % (SRC-FW `:585`, `:593`) | 6.13.2.16, p. 35 |
| 0x30 | ADC channel enables; the firmware enables cell, input and system voltage and disables the TS pin (SRC-FW `:553-558`) | 6.13.2.29, p. 39 |
| 0x34-0x35 | cell voltage, 14 bits, 1 mV a count; read the high byte first | 6.10 p. 28, 6.13.2.30-31 p. 40 |
| 0x38-0x39, 0x3A-0x3B | charger-input and system voltage, same format | 6.13.2.34-37, p. 41 |
| 0x3C-0x3D | die temperature | 6.13.2.38-39, p. 41 |
| 0x48-0x4A | interrupt status, write 1 to clear | 6.13.2.43-45, pp. 43-44 |
| 0x90, 0x92-0x94 | LDO enables and ALDO1-ALDO3 voltage codes | 6.13.2.75-79, pp. 53-54 |
| 0xA4 | gauge percentage | 6.11 p. 28, 6.13.2.88 p. 56 |

Update rates: the data sheet calls the ADC "low speed" and gives no sample
period while the system is on; with the system off and only the cell
present it samples every 64 s when register 0x36 bit 7 is set, its default
(DS 6.10, 6.13.2.32 p. 40). The gauge's update period is not stated; it
learns the cell's curve over a charge and discharge cycle (DS 6.11). The
firmware polls every 20 s, which bounds the staleness of register 0x0B.

The PMU is also where faults live: over-temperature, cell over-voltage,
charger timeout and the gauge watchdog raise interrupts that the firmware
prints and discards (SRC-FW `:325-462`). None reaches the Pico.

## Rails, for the open ALDO question

| Rail | Load as wired | Decoupling capacitor | Voltage |
| --- | --- | --- | --- |
| DCDC1 | `MCU_3V3`: U201 and its VBAT pin, the I2C pull-ups | C107 | not stated |
| ALDO1 | microSD socket VDD | C112 | EFUSE default, unknown |
| ALDO2 | PSRAM U301 VCC | C113 | EFUSE default, unknown |
| ALDO3 | panel interface-mode pins IM0-IM2 | C114 | EFUSE default, unknown |
| ALDO4 | audio filter and headphone-detect pull-up | C115 | EFUSE default, unknown |
| BLDO1 | U401 TPS6116x boost for the LCD backlight, CTRL from U201 PA8 (`LCD_BL`) | C117 | EFUSE default, unknown |
| BLDO2 | U601 TPS6116x boost for the keyboard backlight, CTRL from U201 PC8 (`KEY_BL`) | C118 | EFUSE default, unknown |

(S; the ALDO1-ALDO3 loads agree with `d2ffbd83:docs/research/memory-devices.md`.)
The firmware sets no LDO voltage (SRC-FW: no LDO call in any file), so every
rail runs at the value X-Powers fused for this design. Measurement item: a
voltmeter on C112, C113, C114 and C115 with the board on battery and on
USB, which also settles whether ALDO2 meets the PSRAM's 3.1 V floor for
3.3 V inputs (`d2ffbd83:docs/research/memory-devices.md`, "Supply and chip select").

Power-off and the backlights are complete as drivers: `machdep.picocalc.poweroff`
writes register 0x0E after a `sync()`, and `lcd_backlight` and
`kbd_backlight` write 0x05 and 0x0A (`picokbd.c:628-648`). The firmware's
power-off waits inside its main loop for the whole countdown
(SRC-FW `:304-307`), which is why keys typed during it may be lost
(`PICOCALC.md:1015-1024`).

## Driver plan

Each slice is one pull request. Kernel costs are estimates (I) at GCC 14.2.1
against comparable code: `picokbd_reg` is 144 B and `kbdreq_battery_percent`
6 B in the PICOCALC build of 268483f7 (B, `arm-none-eabi-nm -S`).

| Slice | Work | Files | Kernel text, RAM | Host-testable model | Board measurement |
| --- | --- | --- | --- | --- | --- |
| P1 | A register model of 0x0B: the firmware's six update events, the 20 s refresh, the charge-done bit and the 80 s shutdown rule, driven against `kbdreq_battery_percent` and `kbdreq_battery_charging`; known-bad mutants that decode bit 6 or keep bit 7 | the `check-picocalc-host` test sources, `dev/picokbd_req.c` unchanged | 0 | the model itself | none |
| P2 | A `battery` command: percent, charging, and "no cell" when 0 persists past the shutdown window; a low-battery line to the console at 5 % | a new program under `usr.bin` | 0 | the P1 model behind a stubbed `sysctl` | the command against `printPMU()` on the CH340 port over one charge and one discharge |
| P3 | Board record of the existing nodes and of the ALDO rails | `sys/arch/rp2040/doc/PICOCALC.md` evidence rows | 0 | none | the `PICOCALC.md:1015-1024` checks; voltmeter on C112-C115 |
| P4 (needs a D123 revision) | Firmware registers 0x10-0x1A: STATUS1, STATUS2, cell, input and system millivolts, latched interrupt status, ALDO1-ALDO3 codes; kernel nodes `vbat_mv`, `vbus_mv`, `vsys_mv`, `charger`, `faults` | a fork of `picocalc_keyboard`; `dev/picokbd.c`, `include/picocalcvar.h` | +150 to +250 B text, 0 RAM | extend the P1 model and the Renode keyboard model (`docs/research/picocalc-renode-keyboard-registers.md`) | the new nodes against `printPMU()` and the voltmeter |
| P5 (needs a D123 revision) | PMU telemetry over the idle UART1-to-USART3 pair instead of new I2C registers | the firmware fork; a UART1 instance of `dev/uart.c` | +300 to +600 B text and a receive ring (I) | a framing model with a corrupt-frame mutant | frame loss over a day |

Recommendation: P1 and P3 now, P2 after P1. They finish what the vendor
firmware allows. P4 is the cheapest route to voltage, charger phase and
faults, and it answers the ALDO question from software, but it reverses the
part of D123 that keeps the vendor firmware.

## Residual owner decisions

1. **Quantities beyond percent and charging.**
   - Accept percent and charging as the whole battery interface under the
     vendor firmware (recommended while D123 stands): no firmware risk.
   - Fork the firmware for registers 0x10-0x1A (P4): voltage, charger phase
     and faults for about 200 B of kernel text, at the cost of maintaining a
     controller image and reflashing U201 through its USART1 bootloader.
   - Rework the board to put `M_I2C2` on Pico pins: the kernel would read the
     PMU directly, but two masters on one bus need arbitration the firmware
     does not do.
2. **Low-battery warning.**
   - In user space, in the P2 command run from `cron` or `init` (recommended):
     no kernel text.
   - In the kernel, a callout that reads 0x0B every 30 s and prints at 5 %:
     about +150 B text and 4 B of RAM, and it runs without a user process.
3. **The charge-done bit.**
   - Document the 20 s window and leave the decode as it is (recommended
     under D123).
   - Clear `charging` in the kernel when the percentage reads 100: masks the
     window but misreads a cell that charges at 100 %.

## Unverified sources and gaps

- The XPowersLib revision the vendor binary was built with is unknown; the
  register definitions come from 17132a5.
- `PicoCalc_BIOS_v1.7.bin` was not disassembled; the behavior above is the
  source's, which reports `BIOSVERSION` 0x16.
- Rail loads come from net labels on the rendered schematic; no continuity
  test on a board.
- The gauge's update period and the ADC sample period while the system is
  on are not in the data sheet.

## Sources

- Schematic, AXP2101 data sheet V1.4 and DS5319: the notes repository files
  and SHA-256 sums listed in `d2ffbd83:docs/research/memory-devices.md`,
  "Sources".
- XPowersLib, https://github.com/lewisxhe/XPowersLib at
  17132a58605034281a47c4015f0d4fe08de5ac57 (2026-11-09):
  `src/XPowersAXP2101.hpp` (the register behind each `PMU.*` call) and
  `src/REG/AXP2101Constants.h` (address 0x34, register names), fetched
  2026-11-16.
