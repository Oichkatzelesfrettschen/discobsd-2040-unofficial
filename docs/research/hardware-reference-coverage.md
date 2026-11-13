# Hardware reference coverage: Pico, Pico W, RP2040 and the PicoCalc

This ledger tests a claim: that the tree's owner holds, on the build host,
the reference document for every part of the Raspberry Pi Pico, the Pico W,
the RP2040 and the ClockworkPi PicoCalc, along with an installed Pico SDK. The claim
is false. The RP2040 and Pico datasheets and the PicoCalc's own
schematic, panel and PMU documents are committed; the flash and PSRAM
datasheets sit uncommitted in one checkout, so a fresh clone lacks them.
No Pico SDK exists on the host: the Arch `pico-sdk` package was
removed on 2026-10-10, and `tools/pico-sdk/vendor`, where
`tools/pico-sdk/fetch-pico-sdk.sh` writes, is absent from the primary
checkout and from every worktree. No Pico W
document beyond the CYW43439 product brief is held. Of the PicoCalc's
ten distinct integrated circuits, the PMU, the PSRAM and the panel
controller have documents and the other seven have none, and no Arm core,
PrimeCell or bus specification behind the RP2040's blocks is held.

## Method

Each row names a part, the place its document would live, and one of
these states:

| State | Meaning |
| --- | --- |
| tracked | committed in the named repository |
| untracked | on disk in the named checkout, not committed |
| read, not kept | an earlier note fetched and cited it; no local copy exists |
| absent | no copy found by the search below |
| installed | a host package provides it; version given |
| removed | a host package provided it and was uninstalled |

The search covered `~/Github`, `~/Projects`, `~/Documents`, `~/Downloads`
and `/usr/share/doc` by file name (`find ... -iname` over `.pdf`, `.md`,
`.txt`, `.html`, `.7z`, `.zip`, for every part number below and the
architecture and bus document numbers), the manifests of the owner's private
document stores, `pacman -Q` and `/var/log/pacman.log`,
and every worktree and scratch copy of this tree on the host. A document held under a name that carries no part number
escapes the file-name search. A search for `pico_sdk_init.cmake` across
`~`, `/opt` and `/usr` with no depth limit backs the SDK row.

The recursion runs from each board to every integrated circuit,
connector and crystal on its schematic, from each integrated circuit to
the licensed or standard blocks inside it, and from each block to the
architecture or bus specification that defines its interface. It stops
at the instruction set and bus specifications; passives other than the
ferrite bead and the diodes have no row. The notes repository is the
sibling checkout `discobsd-2040-notes`; its paths below start at `docs/`.

## Software: SDK, tools and models

| Item | Pin or need | State | Evidence |
| --- | --- | --- | --- |
| Pico SDK | commit `98a542c1` (tag 2.3.0), `lib/tinyusb` at `86ad6e56`, from `tools/pico-sdk/fetch-pico-sdk.sh` | absent | `sh tools/pico-sdk/sdk-path.sh` exits 1 and lists all five candidates absent; no `pico_sdk_init.cmake` under `~`, `/opt` or `/usr/share` |
| Arch `pico-sdk` package | 2.3.0-1 | removed | `pacman.log`: installed 2026-09-11, removed 2026-10-10; `discobsd-2040-notes/tools/flash-id/build/CMakeCache.txt` still names `PICO_SDK_PATH=/usr/share/pico-sdk` |
| SDK submodules for the Pico W: `cyw43-driver`, `btstack`, `lwip`, `mbedtls` | needed by any Pico W wireless build | absent | `fetch-pico-sdk.sh` initializes `lib/tinyusb` alone, by design, so a fetched SDK still builds no Pico W wireless code |
| `pioasm` | built by the SDK | absent | not on `PATH`; it arrives with the SDK |
| `picotool` | flash, info, reboot | installed | 2.3.1-1, `/usr/bin/picotool` |
| `arm-none-eabi-gcc`, binutils, newlib, gdb | cross toolchain | installed | gcc 16.2.0-1.1, binutils 2.47-1.1, newlib 4.6.0.20260123-1, gdb 17.2-1.1 |
| `openocd` with `target/rp2040.cfg`, `target/stm32f1x.cfg`, `interface/cmsis-dap.cfg` | SWD for both MCUs | installed | 1:0.12.0-6.1 |
| `cmake`, `bmake`, `renode` | build and emulation | installed | `/usr/bin` |
| Renode RP2040 models with boot ROM `bootroms/rp2040/b2.elf` | `tools/renode/fetch-renode-rp2040.sh`, commit `205a5e4b` | absent from the primary checkout; present in one session worktree at the pinned commit | `tools/renode/vendor` absent from the primary checkout |
| RP2040 boot ROM source, `raspberrypi/pico-bootrom-rp2040` | cited by `MPU.md`, `BOOT-MAP.md`, `doc/research/float-libs.md` | read, not kept | `docs/research/firmware-updates.md` read it through `gh api`; only the compiled `b2.elf` above is local |
| RP2040 SVD register description | debugger and generator input | absent | no `rp2040*.svd` on the host; it ships in the SDK |
| `stm32flash`, `arduino-cli` | keyboard-controller firmware, per the vendor wiki | absent | not on `PATH` |
| `dfu-util` | STM32 USB DFU | installed | `/usr/bin/dfu-util` |

## Raspberry Pi Pico

Designators come from the Rev3 board schematic, Pico Datasheet appendix B,
figure 19 (PDF page 26), rendered at 400 dpi; the crystal's part number
comes from the datasheet's section 2 text, since the schematic prints only
its frequency.

| Ref | Part as printed | Role | Document | State |
| --- | --- | --- | --- | --- |
| board | Raspberry Pi Pico Rev3 | the module | Pico Datasheet, build `d4e0f1799616`, 2026-07-03, 31 pages, `docs/rp2040/pico-datasheet.pdf` and `.txt` | tracked, notes |
| board | design files | schematic source and layout | ZIP named in the Pico Datasheet section 2 | absent |
| U1 | RP2040, silicon B2 | the MCU | RP2040 Datasheet, build `3184e62-clean`, 2025-02-20, 642 pages, errata in appendix B, `docs/rp2040/rp2040-datasheet.pdf` and `.txt`; RP2040 product brief in a private document store | tracked, notes |
| U2 | RT6150B-33GQW | buck-boost SMPS, VSYS to 3V3, PS on GPIO23, EN on 3V3_EN | Richtek RT6150 datasheet | absent |
| U3 | W25Q16JVUXIQ | 2 MiB QSPI flash | Winbond W25Q16JV revision G, 2018-03-22, 75 pages, whose table 9.1 gives `W25Q16JV-IQ/JQ` manufacturer `EFh` and JEDEC device `4015h`, the `ef 40 15` the measured board returns (`DEVICE-INVENTORY.md`), `docs/rp2040/W25Q16JV_Datasheet.pdf` and `.txt` | untracked, notes |
| X1 | 12 MHz; ABM8-272-T3 in the datasheet text | crystal | Abracon ABM8 datasheet | absent |
| D1 | MBR120VLSFT1G | VBUS to VSYS Schottky | onsemi MBR120VLSFT1 datasheet | absent |
| Q1 | DMG1012T | ADC3 isolation FET on GPIO29 | Diodes DMG1012T datasheet | absent |
| J1 | 690-005-298-486 | micro-USB receptacle | EDAC drawing | absent |
| SW1 | TP-1221U-K9K5325 | BOOTSEL switch on QSPI_SS | switch drawing | absent |
| D2 | green LED | GPIO25 | no part number printed | not applicable |
| guides | Hardware design with RP2040; Getting started with Raspberry Pi Pico; Raspberry Pi Pico-series C/C++ SDK | reference circuit, setup, SDK API | Raspberry Pi documents | absent |

### Inside the RP2040

The RP2040 Datasheet documents every block's registers. The rows below
are the documents behind the licensed blocks, which the datasheet cites
for behavior it does not restate.

| Block | Port user | Document | State |
| --- | --- | --- | --- |
| Cortex-M0+ core, NVIC, SysTick, MPU | the whole kernel; `MPU.md` | Arm Cortex-M0+ Technical Reference Manual (DDI 0484); Armv6-M Architecture Reference Manual (DDI 0419) | absent |
| Debug: SWD, multidrop | OpenOCD | Arm Debug Interface v5 (IHI 0031) | absent |
| UART, PL011 | `dev/uart.c` | Arm PrimeCell UART PL011 TRM (DDI 0183) | absent |
| SPI, PL022 | `st7365p.c`, `sdspi.c` | Arm PrimeCell SSP PL022 TRM (DDI 0194) | absent |
| I2C, Synopsys DW_apb_i2c | `picokbd.c` | Synopsys databook, licensee-only | absent; RP2040 Datasheet section 4.3 is the public authority |
| SSI, Synopsys DW_apb_ssi | `boot2/boot2_w25q080.S` | Synopsys databook, licensee-only | absent; RP2040 Datasheet section 4.10 is the public authority |
| USB 1.1 controller and PHY | `sys/dev/usb`, the CDC-ACM console | USB 2.0 Specification; USB CDC 1.2 and PSTN 1.2 | absent |

## Raspberry Pi Pico W

| Part | Role | Document | State |
| --- | --- | --- | --- |
| Pico W board | the module | Pico W Datasheet | read, not kept: `docs/research/firmware-updates.md` cites `datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf` |
| Pico W design files | schematic and layout | ZIP named in the Pico W Datasheet | absent |
| Connecting to the internet with Raspberry Pi Pico W | wireless software | Raspberry Pi document | absent |
| CYW43439 | Wi-Fi and Bluetooth, gSPI to the RP2040 | Infineon AIROC CYW43439 product brief, in a private document store; the full datasheet | product brief only; the full datasheet is absent |
| RP2040, W25Q16JV | as on the Pico | as on the Pico | as on the Pico |
| SMPS, crystal, antenna | power and clock | the Pico W Datasheet names them | absent; a row per part follows a read of that datasheet |

## ClockworkPi PicoCalc mainboard V2.0

Designators come from a 600 dpi render of
`docs/picocalc/clockwork_Mainboard_V2.0_Schematic.pdf` (title block dated
2024-12-20); every part number below also appears in that PDF's text
layer. The Pico seated in J301 and J302 is covered above. The vendor
copies in `docs/picocalc/` match `SHA256SUMS` (`sha256sum -c` passes);
the PSRAM datasheet postdates that file and is absent from it.

| Ref | Part as printed | Role | Document | State |
| --- | --- | --- | --- | --- |
| board | mainboard V2.0 | schematic, Gerbers, assembly guide | `docs/picocalc/clockwork_Mainboard_V2.0_Schematic.pdf`, `pcb/CPI_V2.0_Gerber.7z`, `Clockwork_PicoCalc_Assembly_Guidelines.pdf` | tracked, notes |
| U101 | AXP2101 | PMU: VBUS, battery, DCDC1, ALDO1-4, BLDO1-2 | X-Powers datasheet V1.4, `docs/picocalc/AXP2101_Datasheet_V1.4.pdf` | tracked, notes |
| U102 | WAS4603 | current-limited load switch, IPS to PICO_VSYS, enable PICO_EN | WillSemi WAS4603 datasheet | absent |
| U201 | CKS32F103Rx | keyboard controller: matrix, backlights, PMU I2C, I2C1 target at 0x1F | CKS32F103 datasheet; ST STM32F103x8/xB datasheet, RM0008 reference manual, PM0056 programming manual, errata ES0340 | absent; the controller firmware source is tracked, `docs/picocalc/code/picocalc_keyboard` |
| U301 | ESP-PSRAM64H | 8 MB quad PSRAM | Espressif ESP-PSRAM64/64H datasheet, `docs/picocalc/esp-psram64_esp-psram64h_datasheet_en.pdf` | untracked, notes |
| U401, U601 | TPS6116x | LCD and keyboard backlight boost converters | TI TPS61160/TPS61161 datasheet; the schematic leaves the variant open | absent |
| U501 | NC7WZ16 | dual buffer, PWM_L and PWM_R to the audio filters | onsemi NC7WZ16 datasheet | absent |
| U502, U503 | AW8010 | class-D speaker amplifiers, enable PA_EN | Awinic AW8010 datasheet | absent |
| U701, U703 | WAS7227 | USB pair switch (U701) and serial pair switch (U703) under SW701 | WillSemi WAS7227 datasheet | absent |
| U702 | CH340C | USB-serial bridge | WCH CH340 datasheet | absent |
| X201, X202 | 8 MHz, 32.768 kHz | keyboard-controller clocks | no part number printed | not applicable |
| D401, D601 | B5819W | boost catch diodes | B5819W datasheet | absent |
| FB101 | BLM18PG121SH1 | DCDC1 to MCU_3V3 | Murata BLM18PG datasheet | absent |
| J101 | KS18650X2 | two 18650 cells | holder drawing | absent |
| J303 | TF-03A | microSD socket, card detect on GP22 | socket drawing | absent |
| J401 | 0.5-40PBSX | 40-pin panel FPC, SPI1 | connector drawing | absent |
| panel | ST7365P controller, 320x320 IPS | display | Sitronix ST7365P V1.0, `docs/picocalc/ST7365P_SPEC_V1.0.pdf`; the panel module's own datasheet (glass, backlight current) | controller tracked, notes; module absent |
| J501, SW501 | PJ-327C, JC101GV01 | headphone jack and its contact | drawings | absent |
| J701 | TYPEC-31-M-12 | USB-C receptacle | receptacle drawing | absent |
| SW102, SW701 | K2-1112SA, EM-02KP | power key on AXP2101 PWRON; USB and BOOT0 routing | drawings | absent |

### Inside U201

| Block | Document | State |
| --- | --- | --- |
| Cortex-M3 core | Arm Cortex-M3 Technical Reference Manual (DDI 0337); Armv7-M Architecture Reference Manual (DDI 0403) | absent |

### Bus and card specifications the PicoCalc drivers rest on

| Specification | Port user | State |
| --- | --- | --- |
| SD Physical Layer Simplified Specification | `sd.c`, `sdspi.c` | absent |
| I2C-bus specification, NXP UM10204 | `picokbd.c` | absent |
| USB Type-C Cable and Connector Specification | the CC pull-down rule below | absent |

## Findings beside the coverage

The schematic wires R701 and R702, 5.1 kOhm each, from J701's CC1 and CC2
to ground. `sys/arch/rp2040/doc/PICOCALC.md` ("USB-C and power") offers
missing CC pull-downs as the first explanation for the owner's board
staying off on USB-C without cells; the schematic refutes it at the design
level, and a board with R701 and R702 unfitted would restore it. The same
schematic puts SW102 on the AXP2101's PWRON pin, which leaves the second
explanation, a power-key press or a battery needed to start, standing.
Reading the two resistor sites on the owner's board decides it.

U201 is printed `CKS32F103Rx`, a CKS32 part, while the vendor keyboard
`README.md` names `STM32F103R8T6`. The marking on the owner's board
decides which document set governs; `docs/picocalc/INDEX.md` keeps the
vendor's name until then.

`DEVICE-INVENTORY.md` cites `~/Github/rpi/DEVICE.md` and
`~/Github/rpi/docs/rp2040/rp2040-datasheet.txt`; neither path exists. The
files live at `DEVICE.md` and `docs/rp2040/rp2040-datasheet.txt` in the
notes repository.

`docs/rp2040/` in the notes repository carries no `SHA256SUMS`, so its
three PDFs have no recorded digest.

## Pending checks

- Fetch the Pico SDK at the pinned commit, or reinstall the package, then
  rerun `sh tools/pico-sdk/sdk-path.sh`, which then exits 0, and
  `bmake MACHINE=rp2040 check-flash-id`.
- Read the Pico W Datasheet and add a row for each of its power, clock
  and antenna parts.
- Commit the W25Q16JV and ESP-PSRAM64H copies with their digests.
- Read the U201 marking and the R701 and R702 sites on the owner's board.
