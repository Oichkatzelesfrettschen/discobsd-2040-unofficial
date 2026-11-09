# PicoCalc emulation: a Renode carrier model for the PICOCALC kernel

Status: investigation and implementation note, 2026-11-05, source revision
`d31d6228` plus this branch. Every figure carries one of three marks:
measured (a named command produced it for a named artifact), documented (a
file or URL states it), or not run (with the reason). Nothing here is a board
observation: no PicoCalc and no Pico was touched, and no serial port or flash
tool was opened.

## Result

- Extend Renode with the third-party `matgla/Renode_RP2040` models this tree
  already fetches. It is the only emulator that boots this kernel: DiscoBSD
  erases and programs its own flash through the boot ROM's function table and
  the XIP SSI, which Renode_RP2040 models with a W25QXX flash behind it. The
  alternatives either lack a Pico machine (QEMU) or lack flash writes
  (rp2040js). Renode's peripheral API takes a C# class as an SPI child or an
  I2C target, so the PicoCalc's panel and keyboard controller become two
  small models beside the existing platform.
- The PICOCALC kernel boots under that platform from the real boot ROM to the
  `login:` prompt on the modeled panel, 10.5 virtual seconds after reset
  (measured, below). A keyboard model at I2C address 0x1f answers the
  driver's register protocol, so the console banner reads `kbd0: keyboard
  controller at I2C1 0x1f, firmware 1.6`, and keys queued through the
  monitor log in as `operator` and run `uname -sr` with a Backspace typed in
  the command (measured, below).
- The panel model is an SPI child of SPI1 that decodes the ST7365P command
  and data stream `st7365p.c` sends, keeps the 320x480 graphics RAM with the
  vertical-scroll registers applied, and writes the visible 320x320 image as
  PPM on request. `tools/renode/picocalc_fb.py` decodes that image back into
  the console's 53x32 characters by matching each 6x10 cell against the
  kernel's own font table, so a gate asserts on text, not on pixels, and a
  PNG for a person comes from the same frame.
- The pinned models could not drive SPI1 at all. Four defects stood between
  `st7365p.c` and a panel: a GPIO function table shifted one pin from GP9 up,
  an inverted receive-FIFO-not-empty bit that hangs the driver's first
  drain loop, a ghost frame and stray clock edge around every burst, and a
  bit-per-step clocking path that costs about 30 host microseconds per
  clock edge. Patches 0005 through 0008 under `tools/renode/patches` correct
  them; the fetch script applies them in order.
- What this establishes is a Renode result: the driver sequences, the cell
  renderer, the I2C register exchange, the keymap and the tty agree with
  models written from the datasheets and the vendor firmware. It does not
  establish panel orientation, colors, polarity, timing against the real
  STM32 firmware, power, backlight or any silicon behavior; PICOCALC.md keeps
  every board row `not run`.
- What did not run here: the existing `check-renode` (see "What did not run").

## Survey

Search of 2026-11-05 over GitHub, the ClockworkPi forum and the web, then the
sources read where a claim matters.

| Candidate | What it is | License | RP2040 coverage | SPI display sink | I2C target | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| Renode 1.17 with matgla/Renode_RP2040 (commit `205a5e4`, 2026-03-22) | Instruction-level Cortex-M0+ with C# peripheral models and a boot ROM image | Renode MIT (`/opt/renode/licenses/renode-license`); Renode_RP2040 MIT, Copyright 2025 Mateusz Stadnik (its `LICENSE`) | UART, SPI master (PL022), I2C master, XIP SSI with W25QXX, DMA, ADC, PIO through an external `piosim`, USB present but its README lists it unsupported | `ISPIPeripheral` child of PL022, after patch 0008 | `II2CPeripheral` child of the DW_apb_i2c model | Chosen: boots this kernel today |
| rp2040js (wokwi/rp2040js, Wokwi's engine) | TypeScript RP2040 core | MIT, Copyright 2021-2023 Uri Shaked (repository `LICENSE`) | UART, SPI and I2C master with JavaScript hooks (`RPSPI.onTransmit`, `RPI2C.onConnect`, `onWriteByte`, `onReadByte`), PIO, DMA, USB CDC | A JavaScript `onTransmit` callback | An `onWriteByte` and `onReadByte` callback pair | Rejected: `src/peripherals/ssi.ts` is 121 lines serving status reads, and its README says flash writes are not supported, so the kernel's flash driver and Dhara root cannot run |
| QEMU upstream | System emulator | GPL-2.0 | No RP2040 machine; the installed `qemu-system-arm` 11.1.3 lists `raspi0`, `raspi1ap` and `raspi2b` and no Pico (measured) | not applicable | not applicable | Rejected: nothing to extend |
| QEMU RP2040 RFC v3 (36 patches, 2026-09) and its fork `2xs/qemu-rp2040-pico` | A `raspi-pico` machine: both cores, SRAM, XIP flash, UARTs, SIO, XIP/SSI, DMA, clocks, GPIO, a synthetic or a real boot ROM | GPL-2.0 (the fork's repository metadata reports NOASSERTION; QEMU is GPL-2.0) | SPI, I2C, PIO and USB coverage not established from the pages read | Would be a QEMU device model in C | Same | Not evaluated beyond the announcement: it needs an out-of-tree QEMU build and has no boot record for this kernel. Revisit when it merges |
| Wokwi simulator with a PicoCalc | Browser simulator on rp2040js | Service | A forum user compiled the vendor helloworld and reached `i2c write error`; another was told by Wokwi staff it was too complex | none | none | No PicoCalc part exists |
| Desktop PicoCalc simulators | Searched GitHub and the forum | | The hits run on a PicoCalc (ZX Spectrum, 6502, x86, CHIP-8, Game Boy, C64, Mac, PicoMite) or interpret its BASIC on Linux (MMB4L). One forum user reports a private SDL2 layer over the vendor LCD API, unpublished | | | None models the board for an operating system |

Sources: the ClockworkPi forum threads
`forum.clockworkpi.com/t/pico-calc-emulator/20856` and
`forum.clockworkpi.com/t/how-to-emulate-code-write-for-picocalc-on-computer/16172`;
`github.com/wokwi/rp2040js` (README, `src/peripherals/i2c.ts`, `spi.ts`,
`ssi.ts`); `github.com/matgla/Renode_RP2040` (README and `LICENSE`);
`github.com/2xs/qemu-rp2040-pico`; the qemu-devel RFC thread for
`[RFC PATCH v3 00/36] arm: add Raspberry Pi Pico/RP2040 machine`.

Recommendation: Renode, with the carrier written as C# models loaded from
source. The reasons, in order of weight:

1. It already boots this kernel through `st7365p_init()`, so the PicoCalc
   work adds two peripherals and corrects the SPI controller instead of
   building a machine.
2. A Renode C# class implements `ISPIPeripheral` or `II2CPeripheral`
   directly, and `include @file.cs` compiles it at load time with the Roslyn
   the installed Renode ships. No extra package, SDK or build step is needed
   offline, and the host side stays one `renode` invocation (measured: the two
   files compile in about four seconds).
3. Every piece is MIT-licensed and the models stay in this tree under the
   repository's ISC header; the third-party tree stays a fetched, patched
   clone.
4. Renode's Python peripherals are register blocks on the system bus; this
   investigation did not look for an SPI-child or I2C-target form of them
   and chose C# because the two interfaces are native there.

## Evidence: what the pinned models got wrong for SPI1

Each defect was found by running the PICOCALC kernel and reading the
emulator, with the falsifier stated first.

1. The GPIO function table is shifted from GP9 up. Falsifier: a kernel that
   routes SPI1 to GP10-GP13 sees `SPI1_SCK` on GP10. Observed with the
   unpatched table, `clocks: Update of GPIO mapping for pin: 10 function:
   SPI1_TX`, 11 `SPI1_RX`, 12 `SPI1_CSN`, 13 `SIO`: the clock signal had no
   pin. RP2040 datasheet section 1.4.3 (GPIO Functions, page 12 per `DATASHEET-INDEX.md`)
   assigns RX, CSn, SCK, TX in a repeating group, so GP10 is SCK and GP11
   TX. `emulation/peripherals/gpio/rp2040_gpio.cs` has correct rows through
   GP8 and rows for GP9-GP29 one pin early, in both the SPI and the UART
   columns. Patch 0005 rewrites them by the datasheet rule; after it the
   same log reads `pin: 10 function: SPI1_SCK`, 11 `SPI1_TX`, 12 `SPI1_RX`.
2. The PL022 reports the receive FIFO's state inverted. Falsifier: the
   kernel's `spi_wait_idle()` (`st7365p.c`) drains with `while (SSPSR & RNE)
   read SSPDR` and returns on an idle controller. Observed: the program
   counter sat at `0x10014c46`, inside that loop, in three samples a virtual
   second apart, with no byte on the panel. The model returned RNE for an
   empty FIFO and RFF for one with room, and read SSPDR only while the FIFO
   was not full (datasheet 4.4.4, SSPSR). Patch 0006 corrects the two bits
   and the dequeue.
3. Each burst starts with a ghost frame and ends with a stray clock edge.
   Falsifier: the byte stream at the panel equals the bytes the driver wrote.
   Observed trace with the pin-level model after patch 0006: `ce0 d00 d00 d03
   d09 ...`, with an extra `d00` after every command and an extra `c00`
   between commands, against the driver's `e0` followed by `00 03 09`. A
   write to SSPDR on an idle controller started the clocking thread with
   `transmitData` still zero, and the stop branch of `Step()` fell through
   into one more clock toggle. Patch 0007 loads the first frame at the start
   and returns after the stop. The trace then equals the driver's `init_seq`.
4. Clocking a frame bit by bit through a managed thread is too slow to boot
   a console. Falsifier: one virtual second of PICOCALC boot completes in
   under a minute. Observed with the pin-level model after patches
   0005-0007: 67,173 bytes at the panel after one virtual second, 125 s of
   wall clock, which is the banner not yet drawn. With frame delivery
   (patch 0008), the same one virtual second delivers 779,602 bytes, draws
   the whole banner and the first screen clear, and takes 48 s of wall
   clock including a 6 s start, about 33 times the byte rate. PL022 already
   derives from `NullRegistrationPointPeripheralContainer<ISPIPeripheral>`
   and never calls the child. Patch 0008 does when a device is registered:
   the frame goes to `Transmit()`, the reply goes into the receive FIFO, and
   BSY stays set for `dataSize / bitRate` of virtual time so a driver that
   waits on BSY sees silicon's durations. The frames on the wire stand in for
   the eight-entry transmit FIFO plus the shift register: SSPSR TNF and TFE
   derive from that level, and a frame written to a full FIFO is discarded
   as on silicon (`check-picocalc.py` writes twelve frames at one virtual
   instant and expects the panel to count nine). A clock-source timer ends
   each frame at its exact virtual time, since the clock a register
   callback reads moves in quanta far longer than a 640 ns frame. A
   controller with no registered device keeps the pin-level path that PIO
   interworking needs.

Patches 0005 through 0008 follow the existing series' form (`git format-patch`
headers, applied in order by `tools/renode/fetch-renode-rp2040.sh`, each
checked before it is applied). They are candidates for an upstream report in
the style of `docs/research/renode-rp2040-upstream-reports.md`; no report has
been drafted or sent. The existing four patches are unchanged.

## Design

| File | Role |
| --- | --- |
| `tools/renode/picocalc/st7365p.cs` | The panel: `SPI.ST7365P`, an `ISPIPeripheral` registered under `spi1` plus three GPIO inputs (chip select GP13, D/C GP14, reset GP15). Decodes CASET, RASET, RAMWR, RAMWRC, VSCRDEF, VSCSAD, MADCTL, COLMOD (RGB666, RGB565 and 24-bit), SLPIN, SLPOUT, INVON, INVOFF, DISPON, DISPOFF and SWRESET; counts the gamma, power and timing commands of the bring-up sequence without interpreting them; reports any other command as unmodeled |
| `tools/renode/picocalc/picocalc_keyboard.cs` | The keyboard controller: `I2C.PicoCalcKeyboard`, an `II2CPeripheral` at 0x1f following the vendor firmware's `receiveEvent()` and `requestEvent()` |
| `tools/renode/picocalc.repl` | Adds the panel under `spi1`, the keyboard under `i2c1` at 0x1f and the three GPIO connections to the UART0 platform |
| `tools/renode/picocalc-machine.resc` | Builds the PICOCALC machine without starting it: `machine.resc` with the PICOCALC kernel, the two C# files and `picocalc.repl` |
| `tools/renode/picocalc.resc` | Interactive boot with a UART0 socket and a GDB server, as `boot.resc` does for the UART0 kernel |
| `tools/renode/picocalc_fb.py` | Reads the PPM, decodes 53x32 cells against `dev/lcdfont.c`, writes PNG; a Robot Framework library (`Screen Text`, `Screen Should Contain`); `--self-test` calibrates it |
| `tools/renode/check-picocalc.py`, `check-picocalc.sh` | The gate: missing-dependency reporting in the style of `check-boot.sh`, decoder self-test, then the staged boot |
| `tools/renode/picocalc-warning-classes.txt` | Warning classes the PICOCALC boot adds to `warning-classes.txt`; `check-warnings.py` now reads several class files |
| `tools/renode/patches/0005` to `0008` | The model corrections above |
| Root `Makefile`, `sys/arch/rp2040/doc/TESTING.md` | `check-renode-picocalc`, outside the aggregate like `check-renode`, and its tier section and table row |

Panel model behavior, by the specification page that decides it
(ST7365P_SPEC_V1.0, in the notes repository's `docs/picocalc`):

- D/C is latched with the eighth bit of a byte (page 54). The driver changes
  D/C only after the PL022 reports idle, so the level at the SSPDR write is
  the level the controller would latch.
- A byte arriving with chip select high is counted and dropped.
- RAMWR restarts the write pointer at the CASET/RASET origin; RAMWRC
  continues it. After each pixel the column advances, wraps to the window
  start and advances the row, and wraps to the window origin at the end.
- VSCRDEF must sum to 480 lines (page 184) or it is ignored with a
  warning; VSCSAD then picks the RAM line at the top of the visible window
  inside the scroll area. The visible image applies both, which is what
  makes the hardware scroll in `lcd_set_top()` show.
- MADCTL MX and BGR, and INVON, are recorded in the PPM header and not
  applied: they compensate the board's panel wiring, and the model has no
  panel. MADCTL MV is not modeled and warns.

Keyboard model behavior, from `picocalc_keyboard.ino` and `conf_app.h`:

- A write of a register number (bit 7 set to write, with a value byte)
  prepares a two-byte reply and the next read returns it. Register 0x01
  answers 0, 0x16; 0x09 dequeues one `(state, key)` event from a 31-entry
  FIFO that drops new events when full; 0x04 reports the FIFO depth; 0x05 and
  0x0A hold the two backlights; 0x0B the battery percentage, bit 7 set when
  charging; 0x0E the power-off countdown clamped to at least 6; 0x08, which
  resets the real controller, only counts.
- Monitor commands on `sysbus.i2c1.kbd`: `PressKey`, `ReleaseKey`,
  `TypeKey` (press then release), `TypeText`, `TypeLine`, `TypeCtrl` (Ctrl
  down, letter down and up, Ctrl up, as the host-applied Ctrl of
  `picokbd_map.c` needs), `SetBattery`; properties `Pending`, `Queued`,
  `Delivered`, `Dropped`, `Transactions`, `ResetRequests`.
- Monitor commands on `sysbus.spi1.lcd`: `DumpPpm`, `DumpGramPpm`, `Status`,
  `CommandCount`, `TraceBytes` and `Trace` (the first bytes after the call,
  `cNN` for a command and `dNN` for data); properties `PixelsWritten`,
  `BytesReceived`, `UnknownCommands`, `OutOfWindowPixels`,
  `DroppedWithoutSelect`, `Generation`, `DisplayOn`, `ScrollStart`.
- Not modeled: the controller's 16 ms key scan, hold-repeat events, the FIFO
  overflow interrupt, battery and PMU state beyond the register values, and
  any timing between the write and the read.

Why PL022 delivers frames and the panel does not sample pins: sampling SCK
and MOSI on the GPIO block was the first design and it worked once patches
0005 to 0007 were in (the init trace matched), but at about 560 bytes per
wall second (67,173 bytes in 119 s) the console needs minutes per screen. The frame-level path keeps
the panel model free of clocking, and the pin-level path survives for PIO.

## Measurements

Host: Renode 1.17.0+20260907gitf1dd1b4af (.NET 9.0.22), models at
`205a5e4b25440582008a4292074bb07f80a72328` with patches 0001-0008, kernel
`sys/arch/rp2040/compile/PICOCALC/unix.bin` (119,620 bytes, arm-none-eabi-gcc
16.2.0), root image `distrib/rp2040/flash.bin` from a local
`logged-distribution`, Python 3.14.9. All runs use Renode's console monitor
on stdin; none opens a network listener.

| Observation | Result |
| --- | --- |
| Init bytes at the panel against `init_seq` in `st7365p.c` | 63 of 63 equal, in order (measured, asserted by the gate) |
| Panel registers after boot | display on, sleep out, invert on, MADCTL 0x48, COLMOD 0x66, VSCRDEF 0/320/160, 0 unknown commands, 0 pixels outside the window, 0 bytes without chip select |
| Banner on the decoded panel at 0.5 virtual seconds | `lcd0: ST7365P 320x320 on SPI1, 53x32 cells, console` and `kbd0: keyboard controller at I2C1 0x1f, firmware 1.6` |
| Login prompt | `login:` at 10.5 virtual seconds |
| Warning log | 8,808 warnings in the full gate run, 0 errors, no class outside `warning-classes.txt` and `picocalc-warning-classes.txt` |

The gate's acceptance tail is recorded under "Acceptance" below, from the
last full run of `bmake MACHINE=rp2040 check-renode-picocalc`.

## Pipeline

Stages run in order of cost and of reach; a later stage never substitutes for
an earlier one, and each states what its result is a measurement of.

| Stage | Entry target | Proves | Gating |
| --- | --- | --- | --- |
| Host | `bmake MACHINE=rp2040 check-host` and `check-picocalc-host` | The terminal and key-translation sources compile as strict C17 under sanitizers and agree with a model panel cell for cell | None; in `logged-check` |
| Cross | `check-cross`, `check-cross-kernel`, `check-board-build` | The kernel, the libc and the programs compile and link for Cortex-M0+ with `-Wall -Wextra -Werror`, fit the layout and stay inside the C17 profiles | arm-none-eabi toolchain; in `logged-check` |
| qemu-user Thumb-1 | `check-qemu` | Instruction sequences (the u-area exchange loop, the Smaller C suite, libcalc and libbn) execute as Thumb-1 under qemu-arm | `qemu-arm`; in `logged-check` |
| Renode PICO_UART | `check-renode` | The kernel boots from the real boot ROM through the device probe to a shell, runs `mputest`, and the model log stays inside its warning classes | Renode, fetched models; outside `check` |
| Renode PicoCalc | `check-renode-picocalc` | The PICOCALC console path, the panel byte stream, the I2C keyboard register exchange, the keymap and the tty agree with the models, to a login and a typed command | Renode, fetched models with patches through 0008, PICOCALC kernel and root image; outside `check` |
| Bare Pico board | `discobsd-flash` or `picotool`, then the drivers under `tests/rp2040` (`board_stack_align.py`, `board_aout_admission.py`, `board_exec_hsaout.py`) | Behavior on silicon for the exercised configuration: flash, USB CDC console, MPU, SWAPRAM | Board execution requires the owner's explicit opt-in; an exact opt-in value, unset, empty and zero closed; `check-board-build` builds the programs and runs nothing |
| PicoCalc hardware | The bring-up sequence in `PICOCALC.md` under "Board bring-up of the console" | Panel init and orientation, scroll without tearing, keyboard firmware reply and typing, power from USB-C, character rate | A PicoCalc on the bench and the owner's opt-in |

The Renode PicoCalc stage replaces three of the PicoCalc bring-up checks with
emulated ones (command stream accepted, scroll registers programmed,
keyboard exchange answered) that can fail in CI before a board is on the
bench. It cannot replace the physical ones: a blank panel with a correct
command trace still implicates SPI levels, reset timing or the E9h command the
vendor sends and this driver omits, and only a board capture decides it.

## What the gate does not prove

- Orientation, color order and polarity. The model draws the RAM as written;
  MX, BGR and INVON are metadata. Whether the real panel shows an upright,
  correctly colored image with MADCTL 0x48 and inversion on is a board row.
- Whether the vendor's E9h command, omitted by `st7365p.c`, matters.
- The 66 ns minimum write cycle, the 12.5 MHz clock and the reset and
  sleep-out delays. The PL022 model's busy time and FIFO level follow the
  programmed bit rate; the panel model accepts bytes at any time.
- The firmware's behavior between a register write and the read, its
  hold-repeat events, FIFO overflow, Ctrl release loss and the 2 ms
  write-to-read gap the driver uses while keys arrive. The model answers at
  once.
- Interrupt nesting on the real part: the renderer runs in the SPARE_IRQ_0
  handler on its own stack and the model delivers frames from the CPU thread.
  The render-stack and u-area stack figures `PICOCALC.md` owes still need the
  board.
- USB: the `uartusb` probe writes registers the model decodes without
  behavior; no host attaches.
- Power (AXP2101), the backlights' effect, the SD socket on SPI0, PSRAM and
  audio.

## What did not run

- `check-renode` (the UART0 gate) under `renode-test`: not run. Its Robot
  server listens on `0.0.0.0`, and the host that produced this note ends any
  process holding a wildcard listening socket with SIGKILL (a Python script
  bound to `0.0.0.0` was killed, one bound to `127.0.0.1` ran; Renode with
  its default monitor port died four seconds after binding it). The gate
  moved to the console-monitor driver `tools/renode/check-boot.py` instead,
  recorded under "Acceptance".
- A Robot Framework suite for the PicoCalc: not written. The same constraint
  prevents running one, and an unrun suite is not evidence. The stdin driver
  gives the same waits in virtual time; the model's monitor commands are
  callable from `Execute Command` should a suite be added.
- Any board, flash, serial or USB access: not run, by instruction.
- The QEMU RFC fork: not built or run.
- A hold-repeat, FIFO-overflow or Ctrl-latch test of the keyboard path, and a
  scroll test past line 32 of the panel: the boot screen scrolls (the
  hardware scroll registers are programmed and the decoded frame follows
  VSCSAD), but no gate asserts the scroll start value at a chosen line.

## Acceptance

`bmake MACHINE=rp2040 check-renode-picocalc`, exit 0, 90 s of wall clock
(measured twice with the same virtual-time marks), tail:

    check-picocalc: banner after 0.5 virtual seconds
    check-picocalc: panel display on, sleep out, invert on, madctl 0x48, colmod 0x66, scroll top 0 area 320 bottom 160 start 0, 779602 bytes, 250660 pixels, 0 outside the window, 0 unknown commands, 0 without chip select
    check-picocalc: login prompt after 10.5 virtual seconds
    check-picocalc: shell prompt after 12.0 virtual seconds
    check-picocalc: uname answer typed through the keyboard model after 13.0 virtual seconds
    check-picocalc: pass
    check-warnings: 8808 warnings, 24 classes, 0 errors

The decoded last screen of that run, from `shell.txt` beside the PNG the
gate writes under `tools/renode/vendor/results/picocalc`, ends:

    login: operator
    DiscoBSD 2.7 -- Raspberry Pi Pico (RP2040)
    ...
    operator@pico:~$ uname -sr
    DiscoBSD 2.7
    operator@pico:~$

Calibration and controls:

- `picocalc_fb.py --self-test` decodes a rendered frame exactly, decodes a
  reverse-video row (a cursor cell swaps foreground and background), and
  refuses a frame with one corrupted pixel.
- The PICO_UART kernel, which draws nothing on the panel, run through the same
  driver, exits 1 at the first stage after 20 virtual seconds with `FAIL
  banner: [...] not on the panel`, as it must (measured).
- `bmake MACHINE=rp2040 check-renode`, with patches 0005-0008 applied, exit
  0 in 76 s of wall clock: the device probe after 0.5 virtual seconds, the
  file system check and daemons after 3.5, `login:` after 6.5, then `uname`,
  the capacity record, `MPUTEST OK (mpu on)` and `RESIDUE OK` by 9.5;
  check-warnings counts 35,190 warnings in 19 classes, 0 errors. The
  patches touch only `PL022` and the GPIO rows above GP8, which the UART0
  kernel does not reach. Calibration: an expected probe line changed to
  `swap size = 381 kbytes`, and the capacity record's u-area line changed
  to `stack 2049`, each fail the driver with status 1.

## Next steps

1. Give `check-renode` and `check-renode-picocalc` a CI owner that caches
   the fetched and built models keyed on the pinned commit and the patches.
2. Send patches 0005 to 0008 upstream as reports after the deliberate-request
   rule in `AGENTS.md` (drafts under `docs/research`).
3. Extend the keyboard model with the hold-repeat and overflow paths and add
   a Ctrl-C gate (`TypeCtrl 'c'` against a running `sleep`).
4. When the SD socket driver lands, add an SPI0 SD-card model on the same
   `ISPIPeripheral` path; patch 0008 serves any SPI device.
5. After the first board run, compare a board photograph or capture with a
   frame from `DumpPpm` to settle the orientation and color rows above.
