# RP2040 board descriptor prediction

The `PICOCALC_PSRAM` kernel carries every board driver and one descriptor at
the start of a 256-byte-aligned kernel-flash payload. Initialized data can
share the remainder of that payload. `distrib/rp2040/mkboarduf2.py` changes
only the descriptor's 16 bytes. The default descriptor names a bare Pico and
USB console; the PicoCalc descriptors name the panel console. The host gate
establishes descriptor parsing and image byte identity. The physical-board
run remains the deciding evidence for pin behavior and boot output.

The direct source observation is that `config()` visits the panel, PSRAM, SD,
UART0 and USB probes in that order. Each of the first three probes returns
before its first hardware operation unless `board_id()` reports PicoCalc.
Kernel `printf` and `cngetc` dispatch through `board_console()`. The governing
invariant is that a bare-Pico descriptor causes no PicoCalc pin access before
or during device probing. A PicoCalc boot line on a bare Pico falsifies the
implementation hypothesis; a logic capture showing activity on GP2-GP7 or
GP10-GP22 before the UART0 line is the stronger falsifier.

## Bare Pico default

The unpatched union kernel should show these device lines in order:

```text
uart0: pins tx=GP0/rx=GP1
uartusb: CDC-ACM on the USB device controller, interrupt 5, console
```

The boot must contain none of the `lcd0:`, `kbd0:`, `psram0:` or `sd0:`
lines. An invalid descriptor adds exactly this diagnostic before the version
banner and then follows the same USB-console path:

```text
board: invalid descriptor, using bare Pico with USB console
```

## PicoCalc descriptor

The measured keyboard model and retained PicoCalc evidence predict these
device lines in order when the PSRAM identifies and passes its pattern test:

```text
lcd0: ST7365P 320x320 on SPI1, 53x32 cells, console
kbd0: keyboard controller at I2C1 0x1f, firmware 1.6
psram0: read id 0d/5d eid <twelve hex digits>
psram0: ESP-PSRAM64H 8192 kbytes on PIO0, swap
sd0: SPI0 on GP16-GP19, card detect GP22
uart0: pins tx=GP0/rx=GP1
uartusb: CDC-ACM on the USB device controller, interrupt 5
```

A PSRAM identification or transfer failure replaces the success line with
one of the source-defined `psram0: ... swap stays on fl1` lines; the
`read id` line stays whenever the Read ID frame completed. The later
summary must then report `swap dev  = (4,1)` for flash; a successful PSRAM
probe must report `swap dev  = (3,0)`. A keyboard communication failure
replaces its firmware line with:

```text
kbd0: keyboard controller at I2C1 0x1f not answering
```

The deciding capture is the complete boot transcript paired with the exact
patched kernel UF2 SHA-256. The bare-Pico capture should move the predicted
absence checks to pass; the PicoCalc capture should move the panel, keyboard,
SD and selected swap-path checks to pass. Any deviation stops promotion and
selects a pin trace around the first unexpected line.

## Handoff

- Build and gate commands remain host-only and require caller-selected `${PYTHON}`.
- Flashing and serial access remain closed to this session.
- The orchestrator should run the bare-Pico default image first.
- Record the kernel UF2 SHA-256 before flashing.
- Capture the complete boot transcript from reset through the swap summary.
- Repeat with the PicoCalc-patched image on PicoCalc hardware.
- Compare device lines with the two ordered predictions above.
- Treat any unexpected PicoCalc line on the bare Pico as a safety failure.
- Retain PSRAM success and `fl1` fallback as separate observed outcomes.
