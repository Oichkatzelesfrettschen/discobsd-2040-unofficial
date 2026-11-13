# PicoCalc first power-up with cells

The owner's PicoCalc stays off on USB-C with no 18650 cells fitted
(`sys/arch/rp2040/doc/PICOCALC.md`, "USB-C and power"). This page orders
the board observations that the coverage ledger
(`hardware-reference-coverage.md`, "Pending checks") leaves open, so that
each one costs the least and none disturbs an earlier one. Each step
states its prediction and the observation that refutes it; a result goes
into the ledger row named in the step. Every step reads except "PicoCalc
kernel boot", which writes the Pico's flash; a board run of a DiscoBSD
image stays the owner's explicit opt-in.

## The Pico's flash image, first

The Pico on the bench runs the PICO kernel from its UFS root (`fl0a`).
Save its flash before it goes near the PicoCalc, through its own
micro-USB port:

    set -o noclobber
    LOCAL="$NOTES/docs/local/picocalc"
    STAMP=$(date -u +%Y%m%dT%H%M%SZ)
    PICO_IMAGE="$LOCAL/pico-flash-before-$STAMP.bin"
    mkdir -p "$LOCAL"
    discobsd-flash --bootsel
    picotool save -r 0x10000000 0x10200000 "$PICO_IMAGE" &&
        [ "$(wc -c < "$PICO_IMAGE")" -eq 2097152 ] &&
        sha256sum "$PICO_IMAGE" > "$PICO_IMAGE.sha256" &&
        echo saved
    discobsd-flash --eject

where `$NOTES` names the discobsd-2040-notes checkout. The second-resolution
name keeps an earlier image, `noclobber` makes the shell refuse to replace
an existing file, and the image counts as saved only when the command
prints `saved`: picotool succeeded, the file holds the whole 2 Mbytes
(0x10000000 to 0x10200000), and its digest sits beside it. Keep the Pico out
of the PicoCalc until "PicoCalc kernel boot": every power-off below cuts
its supply without a `sync`, and with U201 held in its bootloader PA13
(PICO_EN) floats to its reset pull-up, which enables the Pico's supply
through U102 (ledger, schematic findings; a hypothesis for the CKS part).
A Pico that is seated anyway is halted at its console (`halt`) before each
power-off.

## Cell-less power-key test

The vendor assembly guide (page "The Interfaces") starts the board by
holding the power key for one second. Nothing in the record says whether
the cell-less board was tried that way, or with which supply.

1. Note the supply and cable. A USB Type-C to Standard-A cable ties the
   plug's CC pin to VBUS through 56 kOhm (USB Type-C 2.5, 3.5.1) and its
   A-end host supplies VBUS without reading CC, so R701 and R702 play no
   part on it; a C-to-C supply switches VBUS on only after it sees them.
2. With no cells, connect USB-C and hold the power key (SW102, on the
   AXP2101's PWRON pin) for one second.

Prediction under the power-key hypothesis (AXP2101 V1.4, 6.5.4.2: VBUS
rising starts the PMU only when "configured by customization"): the board
stays off until the key is held, then starts. A board that starts on
USB-C alone refutes the hypothesis; a board that stays off after the key
on an A-to-C cable refutes it as well and points at the power path
(U102, the WS4603 load switch, and the AXP2101's VBUS input). Only a C-to-C
supply that leaves the board off makes R701 and R702 the next look. The
result goes to `PICOCALC.md` ("USB-C and power").

## One cell, then two

J101 joins both cells in parallel (ledger row J101: P1 and P2 to VBAT, N1
and N2 to ground), and the assembly guide (step I) supports one or two
cells and asks, for two, for the same brand, capacity and charge level.
Parallel cells exchange a current of their voltage difference divided by
their summed internal resistance; with tens of milliohms per cell, 0.1 V
of difference already drives amperes, and the board has no element
between the two cells to limit it.

1. Measure each cell's open-circuit voltage with a meter.
2. Fit one cell, observing the "+" and "-" marks in the holder.
3. Hold the power key for one second and watch the power LED; Alt+B
   flashes it three, two or one times for high, medium or low charge
   (assembly guide, "Check the battery level").
4. Power the board off (hold the key) and disconnect USB-C. Let a freshly
   charged cell rest before measuring, then fit the second cell only when
   both are undamaged, within the cell maker's limits for parallel use,
   and read within about 0.05 V of each other; charge the first through
   USB-C until they do. The 0.05 V figure is an engineering judgment: the
   assembly guide gives no threshold, and at 20 to 60 mOhm summed
   internal resistance it still allows a transient of roughly 0.8 to
   2.5 A.

Prediction: the board starts on one cell and the keyboard backlight
answers Alt+Space. A board that stays dark with a cell measured above
3.5 V refutes the reading that the AXP2101 starts from the battery on the
power key, and the board goes no further until that is traced.

## Charging on USB-C

With a cell fitted and the board on, connect USB-C. Prediction: the power
LED turns yellow while charging (assembly guide), and later the PICOCALC
kernel's `machdep.picocalc.charging` reads 1. A missing yellow LED with a
cell below full refutes the charge path; the AXP2101 then needs reading
through the keyboard controller's battery register.

## Keyboard controller identity through its bootloader

`tools/picocalc/kbd_bootloader_id.py` reads U201's identity through the
system bootloader and sends only Get, Get Version, Get ID and Read
Memory; `check-picocalc-kbd-bootloader` calibrates it.

1. Power the board off and disconnect USB-C. Set SW701 switch 1 ON (the
   vendor wiki's "Put DIP 1 ON"): it raises BOOT0 to about 2.45 V (ledger,
   SW701 paragraph) and routes the CH340C to M_UART1. Leave switch 2 OFF:
   it moves the USB-C pair through U701 to the keyboard controller's own
   USB pins (`PICOCALC.md`, "USB-C and power"), away from the CH340C.
2. Connect USB-C to the host. A CH340 enumerates as `1a86:7523` and gives
   `/dev/ttyUSB0`; this host has no other `ttyUSB` device.
3. Hold the power key one second, so the controller resets with BOOT0
   high.
4. Run, from the repository root, in the shell that set `LOCAL` and
   `noclobber` above:

       STAMP=$(date -u +%Y%m%dT%H%M%SZ)
       $PYTHON tools/picocalc/kbd_bootloader_id.py /dev/ttyUSB0 \
           --dump "$LOCAL/kbd-flash-$STAMP.bin" \
           > "$LOCAL/kbd-bootloader-id-$STAMP.json"
       sha256sum "$LOCAL/kbd-bootloader-id-$STAMP.json"

   The tool stops sending at the first reply outside the protocol, refuses
   a dump file that exists, and keeps the dump inside the flash size the
   part reports; the JSON records the dump's sha256. A sync that fails
   with "run again" (an ACK that may belong to the first 0x7F) is rerun
   with a new `STAMP`, whose first 0x7F returns the parser to its command
   state.
5. Power off, disconnect USB-C, set SW701 switch 1 OFF, power on.

Predictions: exit 0; bootloader version 0x22 and product ID 0x410, as in
the vendor wiki's log of its own board; a flash-size word of 64 or 128
Kbytes (the x8 or xB letter), since 0x410 is the ST medium-density ID; a
dump whose length equals that size. A product ID of 0x414 points at the
xC-xE letters; exit 3 means Get listed Read Memory and its command byte
was refused, and the dump stays unattempted (hypothesis: readout
protection, AN3155, which the notes repository does not hold; whether the
CKS bootloader refuses that byte for another reason is unknown); exit 1
with the memory reads "not run" means either that Get did not list Read
Memory or that a protocol failure stopped the session, as each read's
`detail` field says; exit 1
with a refusal at the address stage means the bootloader rejected that
address. The CPUID and DBGMCU_IDCODE reads are attempts: an ST
bootloader may refuse the private peripheral bus, and on ST silicon
DBGMCU_IDCODE reads zero outside debug mode (ES096 2.2.2), so a nonzero
value there distinguishes the CKS part from ST behavior.

The flash size, product ID and CPUID go into the ledger's U201 rows. The
unique ID and the firmware dump stay in the private notes repository
under its ignored `docs/local/`: the ID names one physical part, and the
dump is the backup of the installed keyboard firmware, whose bytes also
carry its version for comparison with the vendor's `PicoCalc_BIOS_*`
images.

## PicoCalc kernel boot

This step writes the Pico's flash and waits for the owner's opt-in.

1. Confirm the saved image from "The Pico's flash image, first":
   `sha256sum -c` on its `.sha256` file under `$LOCAL` prints `OK`, and
   the image holds 2097152 bytes. The present kernel is PICO (`uname`:
   `DiscoBSD pico 2.7 PICO#1 rp2040`).
2. Seat the Pico in the PicoCalc with the board off. Its micro-USB port
   stays the console and BOOTSEL path once cells are fitted
   (`PICOCALC.md`).
3. Flash `sys/arch/rp2040/compile/PICOCALC_PSRAM/unix.uf2` with
   `distrib/rp2040/flash-PICOCALC_PSRAM.uf2`
   (`distrib/rp2040/host/DEVELOPMENT.md`).

Predicted boot lines and what each settles:

| Boot line | Settles |
| --- | --- |
| `lcd0: ST7365P 320x320 on SPI1, ...` | panel attach; ledger panel row |
| `kbd0: keyboard controller at I2C1 0x1f, firmware M.m` | the installed keyboard firmware version; the vendor source in the notes repository sets `BIOSVERSION` 0x16, which prints 1.6 |
| `psram0: ESP-PSRAM64H 8192 kbytes on PIO0, swap` | Read ID 0x0D with a passing die; `PICOCALC.md` row "The part answers Read ID 0x0D" |
| `psram0: id M/K is not an ESP-PSRAM64H, swap stays on fl1` | a different manufacturer or a failing die, with the bytes read |

Then, at the shell: `sysctl machdep.picocalc` reads the battery, charging
and backlight registers (`PICOCALC.md` row "The controller answers the
battery, backlight and power-off registers").

The driver reads the manufacturer and known-good-die bytes only, so this
boot does not separate the ESP-PSRAM64H and LY68L6400 from the APS6404L
(ledger row U301); that needs the 48-bit EID that `psram_read_id()` does
not collect.

## Panel rows

The ST7365P drives 480 gate lines and the vendor drivers use 320 (ledger
panel rows). `st7365p.c` programs display function control (0xB6) for 480
lines (third parameter 0x3B) and clears frame-memory rows 0 to 319 only,
so on a 480-row glass rows 320 to 479 show whatever the memory held at
power-up. Measure the height of the console's 320 rows, the area the
kernel draws: 320 rows at 0.220 mm are about 70.4 mm, the full 3.92 inch
square, and 320 rows at 0.147 mm are about 47 mm. A console height near
70 mm refutes a 480-row glass; near 47 mm, with noise or dark below it,
supports it.

## Marks to photograph

| Part | Mark that decides |
| --- | --- |
| U102 | WAS4603 against WS4603 |
| U201 | the CKS32F103 flash letter (8, B, C, E) and lot code |
| U401, U601 | `BZQ` TPS61160 against `BZR` TPS61161 |
| U501 | `Z16MG` SC-88 against `C7KK` SIP6/UDFN6 |
| U502, U503 | `A1A` AW8010A, `LCV` B, `U0J` D |
| U702 | CH340C against CH340B |
| R701, R702 | fitted or empty |
| J101 | the holder's mounting variant |
| panel FPC | the module part number |

## Probe points

The Core GPIO header carries GP2, GP3, GP4, GP5, GP21 and GP28 (assembly
guide, "The Interfaces"), and GP2-GP5 and GP21 are the PSRAM's SIO0-SIO3
and SCK (`PICOCALC.md` pin map); chip select, GP20, is absent from it. A
logic analyzer there sees the PSRAM bus at 31.25 MHz, which needs at least
125 MS/s to resolve, and would replace the assumed pad delays of
`check-psram-pio` with measured ones. Nothing else may drive those pins
while the PSRAM driver runs. The schematic's test points TP1-TP6, on header J702, carry
UART0_RX, UART0_TX, M_UART1_RX, M_UART1_TX, M_USB_DP and M_USB_DM, the
same six nets in the same order as the guide's "Mainboard GPIOs" legend,
so the keyboard controller's USART1 and USB are reachable there as well.
