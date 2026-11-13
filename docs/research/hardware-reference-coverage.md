# Hardware reference coverage: Pico, Pico W, RP2040 and the PicoCalc

The claim under test: the build host holds the reference document for
every part of the Raspberry Pi Pico, the Pico W, the RP2040 and the
ClockworkPi PicoCalc, along with an installed Pico SDK. Tested on
2026-11-12, the claim was false: the notes repository held the
RP2040 and Pico datasheets and the PicoCalc's schematic, panel and PMU
documents, the flash and PSRAM datasheets sat uncommitted in one checkout,
no Pico W document beyond the CYW43439 product brief existed, seven of the
PicoCalc's ten distinct integrated circuits had no document, no Arm core,
PrimeCell or bus specification was held, and no Pico SDK existed on the
host. The Arch `pico-sdk` package had been removed on 2026-10-10, and
`tools/pico-sdk/vendor`, where `tools/pico-sdk/fetch-pico-sdk.sh` writes,
was absent from the primary checkout and from every worktree.

The coverage tables record the state after the gaps were filled: the AUR
`pico-sdk` 2.3.1-1 package supplies `/usr/share/pico-sdk` with every
submodule, the fetch script pins the same 2.3.1 release, and the notes
repository holds every document a manufacturer, standards body or
distributor served. What remains absent is listed under "Pending checks"
and in the notes repository's `docs/NOT-FETCHED.tsv`.

## Method

A coverage entry names a part, the place its document lives, and one of
these states:

| State | Meaning |
| --- | --- |
| tracked | committed in the named repository |
| local only | on disk under the notes repository's ignored `docs/local/`, kept off the server for the reason its README row states (a license forbidding copies, or account terms not yet read); the README of the matching directory there records its digest |
| read, not kept | an earlier note fetched and cited it; no local copy exists |
| absent | no source served it, or no copy exists |
| installed | a host package provides it; version given |

The 2026-11-12 search covered `~/Github`, `~/Projects`, `~/Documents`,
`~/Downloads` and `/usr/share/doc` by file name (`find ... -iname` over
`.pdf`, `.md`, `.txt`, `.html`, `.7z`, `.zip`, for every part number below
and the architecture and bus document numbers), the manifests of the
owner's private document stores, `pacman -Q` and `/var/log/pacman.log`,
and every worktree and scratch copy of this tree on the host. A document
held under a name that carries no part number escapes the file-name
search. A search for `pico_sdk_init.cmake` across `~`, `/opt` and `/usr`
with no depth limit backed the SDK row.

The recursion runs from each board to every integrated circuit,
connector and crystal on its schematic, from each integrated circuit to
the licensed or standard blocks inside it, and from each block to the
architecture or bus specification that defines its interface. It stops
at the instruction set and bus specifications; passives other than the
ferrite beads, the RF filter and the diodes have no row. The notes
repository is the sibling checkout `discobsd-2040-notes`; its paths below
start at `docs/`, and each of its document directories carries a
`README.md` with every file's printed revision, source and retrieval date
and a `SHA256SUMS`.

## Software: SDK, tools and models

| Item | Pin or need | State | Evidence |
| --- | --- | --- | --- |
| AUR `pico-sdk` package | 2.3.1-1: tag 2.3.1, `btstack` `eb0bb8b`, `cyw43-driver` `055d642`, `lwip` `77dcd25`, `mbedtls` `0bebf8b`, `tinyusb` `86ad6e5`, the submodule commits upstream 2.3.1 records | installed, `/usr/share/pico-sdk`, with `/etc/profile.d/pico-sdk.sh` exporting `PICO_SDK_PATH` | `pacman -Q pico-sdk`; the 2.3.0-1 package before it was installed 2026-09-11 and removed 2026-10-10 (`pacman.log`) |
| Pico SDK fetched by the tree | `tools/pico-sdk/fetch-pico-sdk.sh`: commit `079c6f39` (tag 2.3.1), `lib/tinyusb` at `86ad6e56` | absent from the primary checkout until the script runs there | `sh tools/pico-sdk/check-flash-id.sh` passes against the fetched copy and the package alike: `flash_id` 37,996 and `flash_semantics` 53,660 of 262,144 usable SRAM bytes, GCC 16.2.0 (build evidence) |
| SDK submodules for the Pico W: `cyw43-driver`, `btstack`, `lwip`, `mbedtls` | needed by any Pico W wireless build | installed in the package; absent from the fetched copy | `fetch-pico-sdk.sh` initializes `lib/tinyusb` alone, by design |
| `pioasm` | built by the SDK | source in the package, `tools/pioasm`; no binary on `PATH` | an SDK CMake build compiles it on demand |
| `picotool` | flash, info, reboot | installed | 2.3.1-1, `/usr/bin/picotool` |
| `arm-none-eabi-gcc`, binutils, newlib, gdb | cross toolchain | installed | gcc 16.2.0-1.1, binutils 2.47-1.1, newlib 4.6.0.20260123-1, gdb 17.2-1.1 |
| `openocd` with `target/rp2040.cfg`, `target/stm32f1x.cfg`, `interface/cmsis-dap.cfg` | SWD for both MCUs | installed | 1:0.12.0-6.1 |
| `cmake`, `bmake`, `renode` | build and emulation | installed | `/usr/bin` |
| Renode RP2040 models with boot ROM `bootroms/rp2040/b2.elf` | `tools/renode/fetch-renode-rp2040.sh`, commit `205a5e4b` | absent from the primary checkout; present in one session worktree at the pinned commit | `tools/renode/vendor` absent from the primary checkout |
| RP2040 boot ROM source, `raspberrypi/pico-bootrom-rp2040` | cited by `MPU.md`, `BOOT-MAP.md`, `doc/research/float-libs.md` | read, not kept | `docs/research/firmware-updates.md` read it through `gh api`; only the compiled `b2.elf` above is local |
| RP2040 SVD register description | debugger and generator input | installed | `src/rp2040/hardware_regs/RP2040.svd` in the SDK package |
| `stm32flash`, `arduino-cli` | keyboard-controller firmware, per the vendor wiki | absent | not on `PATH` |
| `dfu-util` | STM32 USB DFU | installed | `/usr/bin/dfu-util` |

## Raspberry Pi Pico

Designators come from the Rev3 board schematic, Pico Datasheet appendix B,
figure 19 (PDF page 26), rendered at 400 dpi; the crystal's part number
comes from the datasheet's section 2 text, since the schematic prints only
its frequency.

| Ref | Part as printed | Role | Document | State |
| --- | --- | --- | --- | --- |
| board | Raspberry Pi Pico Rev3 | the module | Pico Datasheet, build `d4e0f1799616`, 2026-07-03, `rp2040/pico-datasheet.pdf`; product brief, January 2021, `rp2040/pico_product_brief_rp008308.pdf` | tracked, notes |
| board | design files | schematic, layout, BOM | `rp2040/design-files/rpi_pico_r3_public_20200119_rp008379.zip` | tracked, notes |
| U1 | RP2040, silicon B2 | the MCU | RP2040 Datasheet, build `3184e62-clean`, 2025-02-20, errata in appendix B, `rp2040/rp2040-datasheet.pdf` | tracked, notes |
| U2 | RT6150B-33GQW | buck-boost SMPS, VSYS to 3V3, PS on GPIO23, EN on 3V3_EN | Richtek DS6150A/B-06, `rp2040/richtek_rt6150a_b_ds6150ab_06.pdf` | tracked, notes |
| U3 | W25Q16JVUXIQ | 2 MiB QSPI flash | Winbond W25Q16JV revision G, 2018-03-22, whose table 9.1 gives `W25Q16JV-IQ/JQ` manufacturer `EFh` and JEDEC device `4015h`, the `ef 40 15` the measured board returns (`DEVICE-INVENTORY.md`), `rp2040/W25Q16JV_Datasheet.pdf` | tracked, notes |
| X1 | 12 MHz; ABM8-272-T3 in the datasheet text | crystal | Abracon ABM8-272-T3 source control drawing 456603 Rev B, 2024-09-16, sheets 2, 4 and 6 of 9 as Abracon publishes them, `rp2040/abracon_abm8_272_t3_source_control_drawing_456603_revb.pdf`; ABM8 family sheet | tracked, notes |
| D1 | MBR120VLSFT1G | VBUS to VSYS Schottky | onsemi MBR120VLSFT1/D Rev. 5, `rp2040/onsemi_mbr120vlsft1_d.pdf` | tracked, notes |
| Q1 | DMG1012T | ADC3 isolation FET on GPIO29 | Diodes DS31783, `rp2040/diodes_dmg1012t_ds31783.pdf` | tracked, notes |
| J1 | 690-005-298-486 | micro-USB receptacle | EDAC drawing 690-005-298-486 issue 2, 2015-09-01, `rp2040/edac_690_005_298_486_micro_usb_5pin_b_type_drawing.pdf` | tracked, notes |
| SW1 | TP-1221U-K9K5325 | BOOTSEL switch on QSPI_SS | the Pico BOM names it "Tact switch 4.2x3.4mm"; no manufacturer sheet found | absent |
| D2 | green LED | GPIO25 | no part number printed | not applicable |
| guides | Hardware design with RP2040; Getting started with Pico-series; Pico-series C/C++ SDK | reference circuit, setup, SDK API | `rp2040/hardware_design_with_rp2040_rp008279.pdf`, `rp2040/getting_started_with_pico_series_rp008276.pdf`, `rp2040/pico_series_c_cpp_sdk_rp009085.pdf` | tracked, notes |

### Inside the RP2040

The RP2040 Datasheet documents every block's registers. The rows below
are the documents behind the licensed blocks, which the datasheet cites
for behavior it does not restate.

| Block | Port user | Document | State |
| --- | --- | --- | --- |
| Cortex-M0+ core, NVIC, SysTick, MPU | the whole kernel; `MPU.md` | Cortex-M0+ TRM DDI 0484C (r0p1), `arm/arm_ddi0484c_cortex_m0plus_r0p1_trm.pdf`; Armv6-M ARM DDI 0419E, `arm/arm_ddi0419e_armv6m_architecture_reference_manual.pdf` | tracked, notes |
| Debug: SWD, multidrop | OpenOCD | ADIv5 IHI 0031G, `arm/arm_ihi0031g_adiv5_architecture_specification.pdf` | tracked, notes |
| UART, PL011 | `dev/uart.c` | PL011 TRM DDI 0183G (r1p5), `arm/arm_ddi0183g_primecell_uart_pl011_r1p5_trm.pdf` | tracked, notes |
| SPI, PL022 | `st7365p.c`, `sdspi.c` | PL022 TRM DDI 0194H (r1p4), `arm/arm_ddi0194h_primecell_ssp_pl022_r1p4_trm.pdf` | tracked, notes |
| I2C, Synopsys DW_apb_i2c | `picokbd.c` | Synopsys databook, licensee-only | absent; RP2040 Datasheet section 4.3 is the public authority |
| SSI, Synopsys DW_apb_ssi | `boot2/boot2_w25q080.S` | Synopsys databook, licensee-only | absent; RP2040 Datasheet section 4.10 is the public authority |
| USB 1.1 controller and PHY | `sys/dev/usb`, the CDC-ACM console | USB 2.0, `specs/usb_20.pdf`; CDC 1.2 with errata through 2025-04-03 and PSTN 1.2, `specs/usb_cdc120_with_errata_and_ecn_through_2025-04-03.pdf`, `specs/usb_pstn120.pdf` | tracked, notes |

## Raspberry Pi Pico W

Designators come from the Pico W Rev3 schematic, Pico W Datasheet
appendix B (PDF pages 21-22), rendered at 500 dpi. The RP2040, W25Q16JV,
MBR120VLSFT1G, DMG1012T, micro-USB receptacle and BOOTSEL switch rows of
the Pico apply unchanged.

| Ref | Part as printed | Role | Document | State |
| --- | --- | --- | --- | --- |
| board | Raspberry Pi Pico W Rev3 | the module | Pico W Datasheet, Release 7, build `d4e0f1799616`, `rp2040/pico_w_datasheet_rp008312.pdf`; product brief, April 2024, `rp2040/pico_w_product_brief_rp008313.pdf` | tracked, notes |
| board | design files | layout and schematic source; the ZIP carries no schematic PDF | `rp2040/design-files/rpi_picow_public_20220607_rp008380.zip` | tracked, notes |
| U2 | RT6154AGQW | buck-boost SMPS, PS/SYNC on WL_GPIO1 | Richtek DS6154A/B-05, `rp2040/richtek_rt6154a_b_ds6154ab_05.pdf` | tracked, notes |
| U19 | CYW43439KUBG | Wi-Fi and Bluetooth, gSPI to the RP2040 | Infineon 002-30348 Rev. \*D, 2023-04-17, LCSC copy, `rp2040/infineon_cyw43439_datasheet_002_30348_rev_d_lcsc_c5359496.pdf`; Infineon lists version 07_00 of 2024-05-29 for myInfineon accounts only | Rev. \*D tracked, notes; 07_00 absent |
| U19 sibling | CYW43438 | the CYW43439's single-band sibling, whose Rev. \*O datasheet maps Broadcom to Cypress part numbers | Infineon 002-14796 Rev. \*O, `rp2040/infineon_cyw43438_datasheet_002_14796_rev_o.pdf` | tracked, notes |
| software | Wi-Fi and Bluetooth connectivity on Pico W-series (formerly "Connecting to the Internet") | wireless SDK use | `rp2040/wifi_bluetooth_connectivity_pico_w_series_rp008257.pdf`, Release 9 | tracked, notes |
| X1 | 12 MHz | RP2040 crystal | ABM8-272-T3 drawing as on the Pico | tracked, notes |
| X2 | 37.4 MHz | CYW43439 reference crystal | no part number printed | not applicable |
| FL1 | LFB212G45CG7D227 | 2.45 GHz band-pass filter in the antenna path | Murata product data sheet, 2022-03-13, LCSC copy; murata.com answers HTTP 500 | tracked, notes |
| FB8, FB9 | BLM18AG601SN1, BLM03AG601SN1 | ferrites on the CYW43439 supplies | Murata product data sheet (BLM18AG601SN1) and reference specification JENF243A-0020AD-01 (BLM03AG), distributor copies | tracked, notes |
| L9 | CML0306-4N7-H-NH | RF matching inductor | Cyntec 0201 thin-film inductor sheet CYNP-8Z-031 rev A5, LCSC copy | tracked, notes |
| D2 | green LED | WL_GPIO0 | no part number printed | not applicable |

## ClockworkPi PicoCalc mainboard V2.0

Designators come from a 600 dpi render of
`picocalc/clockwork_Mainboard_V2.0_Schematic.pdf` (title block dated
2024-12-20); every part number below also appears in that PDF's text
layer. The Pico seated in J301 and J302 is covered above. Paths in
`picocalc/parts/` name a distributor where one served the file.

| Ref | Part as printed | Role | Document | State |
| --- | --- | --- | --- | --- |
| board | mainboard V2.0 | schematic, Gerbers, assembly guide | `picocalc/clockwork_Mainboard_V2.0_Schematic.pdf`, `picocalc/pcb/CPI_V2.0_Gerber.7z`, `picocalc/Clockwork_PicoCalc_Assembly_Guidelines.pdf` | tracked, notes |
| U101 | AXP2101 | PMU: VBUS, battery, DCDC1, ALDO1-4, BLDO1-2 | X-Powers datasheet V1.4, `picocalc/AXP2101_Datasheet_V1.4.pdf` | tracked, notes |
| U102 | WAS4603 | current-limited load switch, IPS to PICO_VSYS, enable PICO_EN | Will Semiconductor WS4603 Rev 1.2, `picocalc/parts/willsemi_ws4603_datasheet_rev1.2.pdf`; no document carries the printed WAS prefix (Part identity verdicts, below) | tracked, notes |
| U201 | CKS32F103Rx | keyboard controller: matrix, backlights, PMU I2C, I2C1 target at 0x1F | CKS32F103x8/xB datasheet v1.15 (Chinese, LCSC copy) and an undated English CKS32F103xx datasheet (third-party copy); ST DS5319 Rev 20, RM0008 Rev 21, PM0056 Rev 7, ES096 Rev 15 (x8/xB) and ES0340 (xC/D/E), all in `picocalc/parts/` | tracked, notes; the CKS32F103x8/xB reference manual (458 pages, V1.1 per sekorm.com) local only, `local/picocalc/parts/`, while the sekorm account terms it came under are unread |
| U301 | ESP-PSRAM64H | 8 MB quad PSRAM | Espressif ESP-PSRAM64/64H V1.1, 2020-10, English and Chinese, from Wayback copies of Espressif's URL, which now answers 404 (`picocalc/esp_psram64_esp_psram64h_datasheet_en_v1.1_wayback.pdf`), and the V1.0 of 2018-06; Lyontek LY68L6400 Rev. 1.2, whose sheet the Espressif one derives from; AP Memory APS6404L-3SQR Rev. 2.7, a family member with different limits (Part identity verdicts, below) | tracked, notes |
| U401, U601 | TPS6116x | LCD and keyboard backlight boost converters | TI SLVS791E, covering TPS61160 and TPS61161 | tracked, notes |
| U501 | NC7WZ16 | dual buffer, PWM_L and PWM_R to the audio filters | onsemi NC7WZ16/D Rev 8 | tracked, notes |
| U502, U503 | AW8010 | class-D speaker amplifiers, enable PA_EN | Awinic AW8010A V1.3, AW8010B V1.4, AW8010D V1.1; the schematic prints no suffix | tracked, notes |
| U701, U703 | WAS7227 | USB pair switch (U701) and serial pair switch (U703) under SW701 | Will Semiconductor WAS7227Q Rev 1.4 | tracked, notes |
| U702 | CH340C | USB-serial bridge | WCH CH340DS1 version 3D | tracked, notes |
| X201, X202 | 8 MHz, 32.768 kHz | keyboard-controller clocks | no part number printed | not applicable |
| D401, D601 | B5819W | boost catch diodes | MCC B5817W-B5819W SOD-123 | tracked, notes |
| FB101 | BLM18PG121SH1 | DCDC1 to MCU_3V3 | Murata reference specification JENF243A_9101X-01 | tracked, notes |
| J101 | KS18650X2 | two 18650 cells in parallel: pins 1 (P1) and 2 (P2) join VBAT, pins 4 (N1) and 3 (N2) join ground, the one-cell, two-parallel arrangement the AXP2101 ("Single Cell NVDC PMU") requires | JBT Componentes battery-holder catalog, PDF page 6, printed page P-08: KS18650x2-DIY, -SMT and -THM, each for two 18650 cells, 77 x 40 x 15 mm, with photographs of their terminals, `picocalc/parts/jbt_suportes_de_bateria_catalog_ks18650x2_p08.pdf`; the catalog is a Brazilian distributor's and establishes the listed family, not who made ClockworkPi's holder or which mounting variant it fitted | catalog tracked, notes; variant, terminal positions and footprint correspondence read from the board |
| J303 | TF-03A | microSD socket, card detect on GP22 | Korean Hroparts drawing, LCSC C693842 | tracked, notes |
| J401 | 0.5-40PBSX | 40-pin 0.5 mm panel FPC, SPI1 | no listing at LCSC, szlcsc or GitHub; the same label sits at J302 on ClockworkPi's uConsole mainboard V3.14, so it reads as ClockworkPi's BOM code; the 0.5-40P-plus-suffix naming matches BOOMELE's FPC series (C11080 0.5-40P, C9160 0.5-40PFGPZ), which leaves the maker inferred, not established | absent; the contact side and lock style are read from the board |
| panel | ST7365P controller, 320x320 IPS | display | Sitronix ST7365P V1.0, `picocalc/ST7365P_SPEC_V1.0.pdf`; the panel module's own datasheet (glass, backlight current); the Tianxianwei TXW392001B0 listing and forum statements, `picocalc/panel-sources.md` | controller tracked, notes, byte-identical to ClockworkPi's GitHub copy (blob `40f0c1f1`); module datasheet absent |
| J501 | PJ-327C | 3.5 mm headphone jack, detect contact on HP_DET | HOOYA PJ-327C drawing, LCSC C2939157 | tracked, notes |
| SW501 | JC101GV01 | dual-gang potentiometer: two tracks with wipers, the schematic's PCM_AUDIO_L/R taps between R505/R506 (1.8 kOhm) and the amplifiers; the assembly guide's interfaces page (page 20) calls it the "Volume knob" on the left edge, a thumbwheel | no listing at LCSC, szlcsc or GitHub, and no resistance or taper printed | absent; the marking and the end-to-end resistance are read from the board |
| J701 | TYPEC-31-M-12 | USB-C receptacle | Korean Hroparts drawing, LCSC C165948 | tracked, notes |
| SW102, SW701 | K2-1112SA, EM-02KP | power key on AXP2101 PWRON; USB and BOOT0 routing | Korean Hroparts K2-1112SA-A4DW-01 drawing, LCSC C961745; HRO EM-series DIP switch drawing, LCSC C129078 | tracked, notes |

### Inside U201

| Block | Document | State |
| --- | --- | --- |
| Cortex-M3 core | Cortex-M3 TRM document 100165 (r2p1, the revision CKS states for CKS32F103C8/B), DDI 0337E (r1p1, the STM32F103 core) and DDI 0337H (r2p0); Armv7-M ARM DDI 0403E.e, all in `arm/` | tracked, notes |

Neither CKS32F103 datasheet states pin or register compatibility with
STM32F103: the English copy claims compatibility only among CKS32F103xx
members (section 2.2, page 10), and the Chinese v1.15 names no STM32 part.
CKS's own list of differences from STM32F103C8/B
(`picocalc/parts/cks_cks32f103c8_b_stm32_incompatibility_list.pdf`)
names seven: `GPIO_PinRemapConfig` writing SWJ_CFG disables the debug
port; an access outside the documented peripheral map raises HardFault
where the ST part ignores it; SPI and timer pins lack open-drain output;
the core is Cortex-M3 r2p1, so a programmer that checks the ST core ID
refuses the part; BOOT0 carries an internal pull-down of about 40 kOhm,
so an external pull-up must stay at or below 15 kOhm; plus a dual-ADC
trigger note and an early-package NC pin. On the mainboard, SW701
switches SEL1/BOOT0 to MCU_3V3 through R705 (10 kOhm) while R703
(100 kOhm) pulls the same node down; with the internal pull-down of about
40 kOhm in parallel with R703, BOOT0 sits at about 3.3 V x 28.6 / 38.6, or
2.45 V, above the 2 V input-high minimum the English CKS datasheet gives
for FT pins (I/O characteristics table, page 41), so the selection works
with about 0.45 V of margin while the internal pull-down stays near its
stated value. The reference manual
(sekorm.com document 1471113, `cksic.com` being unreachable) contradicts
itself on the core: section 22.6.3 gives the Cortex-M3 TAP ID
`0x3BA00477` and calls it r1p1, the value ST's RM0008 gives, while table
92 gives IDCODE `0x4BA00477`, "ARM Cortex-M3 r2p1-01rel0", which agrees
with the incompatibility list. OpenOCD's `target/stm32f1x.cfg` expects
ST's ID codes, so hypothesis: SWD to U201 through OpenOCD needs
`CPUTAPID` set to the part's reported value or to 0; the falsifier is an
`openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg` session on
the owner's board that examines the core without that override. SWD
itself is hard to reach on this board: no schematic net is named SWDIO or
SWCLK, the debug header J702 and test points TP1-TP6 carry UART0,
M_UART1, M_USB, MCU_3V3 and GND only, and the firmware drives PA13 and
PA14 as outputs (`picocalc_keyboard.ino` lines 471 and 503), which on the
STM32duino core it builds with releases them from SWD (hypothesis: the
core source is not held). A session needs wires on U201 pins 46 and 49
and NRST, and connect-under-reset.

### Bus and card specifications the PicoCalc drivers rest on

| Specification | Port user | State |
| --- | --- | --- |
| SD Physical Layer Simplified Specification, Part 1 v9.10, 2023-12-01 | `sd.c`, `sdspi.c` | local only, notes; the SD Association's click-through license forbids copies on another server |
| I2C-bus specification, NXP UM10204 Rev. 7.0 | `picokbd.c` | tracked, notes |
| USB Type-C Cable and Connector Specification, Release 2.5 | the CC pull-down rule below | tracked, notes, as the usb.org ZIP |

## Part identity verdicts

Each part number below was tested against the parameters its candidate
documents print: a candidate is refuted by one disagreeing pin, value or
limit, and supported only when every compared row agrees. Pins, nets and
component values come from the 600 dpi schematic render; datasheet pages
are PDF pages in the notes repository. A verdict that documents cannot
reach names the board observation that reaches it.

| Ref | Claim | Verdict | Discriminating evidence | Board observation that decides |
| --- | --- | --- | --- | --- |
| U301 | ESP-PSRAM64H sheet describes the part | supported | the part number the schematic prints | not needed |
| U301 | LY68L6400 shares the specification | limits and opcodes supported; command semantics differ | every timing, current and opcode row equal (tCHD 20 ns, tCPH 50 ns, tACLK 2-6 ns, ICC 40 mA, absolute maximum 4.2 V); LY68L6400's command table (page 8) adds note 2, "Linear burst is prohibited", to one cell, SPI-mode Write 02h at 133 MHz, which the ESP sheets lack; the port writes with 38h in quad mode at 31.25 MHz in 64-byte chunks inside one 1 KB page, so the note does not bear on the driver; page 5 of the ESP V1.1 PDF carries an invisible text layer from Lyontek's LY68S3200, a different part, which shows a shared Lyontek template, not derivation from the LY68L6400 sheet | die origin: none in documents |
| U301 | APS6404L-3SQR is the same specification | refuted | tCHD 3.0 vs 20 ns, tCPH 18 vs 50 ns, tACLK and tHZ 5.5 vs 6 ns, ICC 7 vs 40 mA, absolute maximum 4.0 vs 4.2 V; opcodes, MF 0x0D, KGD 0x5D, 1 KB page and SOP8 pin order agree, so it stays a family member | a 9-byte Read ID (9Fh after the 66h/99h reset) compared with reference APS6404L and LY68L6400 parts; only APS defines EID[47:45] = 010 for 64 Mb |
| U102 | WS4603 (printed WAS4603) | pin map confirmed; prefix undecided | SOT-23-5 pins 1 OUT/PICO_VSYS, 2 GND, 3 ISET/R104, 4 EN/PICO_EN active high, 5 IN/IPS; I_OC = 6.8 kOhm / 3.4 kOhm = 2.0 A (datasheet page 8) | top mark |
| U502, U503 | AW8010 A, B or D | undecided | all three print the same 3x3 ball map, and all nine balls agree with the schematic; gain 315 kOhm / 47 kOhm = 6.7 V/V | top mark: `A1A` A, `LCV` B, `U0J` D |
| U401, U601 | TPS61160 or TPS61161 | pin map confirmed; variant undecided | six pins and pad agree; I_LED = 200 mV / R: LCD 5.1 Ohm gives 39.2 mA, keyboard 20 Ohm gives 10.0 mA; the keyboard string is 6 LEDs in series (54 LEDs, 9 parallel per column), inside both OVP limits; 25 V output capacitors weakly favor the 26 V-OVP TPS61160 | top mark `BZQ` 61160, `BZR` 61161, or LEDA voltage at full brightness |
| U702 | CH340C | confirmed against CH340G/T/R; CH340B not excluded | SOP-16 pin map agrees; pins 7 and 8 carry no crystal, which CH340G/T/R require | top mark |
| U201 | CKS32F103 in LQFP64 | pin map confirmed | 64 of 64 pins agree with the CKS LQFP64 table | not needed |
| U201 | flash size letter 8, B, C or E | undecided | the x8/xB and xC-xE LQFP64 pin columns are identical | top mark, or the flash-size word at 0x1FFFF7E0 read through the system bootloader: SW701 in the keyboard-flash position raises BOOT0 and routes the CH340C to M_UART1, the vendor wiki's `stm32flash` path (`stm32flash` is not installed on the build host) |
| U201 | Cortex-M3 r1p1 or r2p1 | undecided; documents conflict | the CKS manual gives TAP ID 0x3BA00477 (r1p1) in 22.6.3 and IDCODE 0x4BA00477 (r2p1) in table 92; CKS's difference list says r2p1 | CPUID at 0xE000ED00: 0x411FC231 r1p1, 0x412FC231 r2p1, read over SWD wired to pins 46 and 49 under reset, or reported by a firmware build; the bootloader's device ID does not carry the core revision |
| U701, U703 | WAS7227Q | confirmed | all 10 pins agree; OE tied low; SEL low selects D1, matching `docs/picocalc/INDEX.md` | not needed |
| U501 | NC7WZ16 | pin map confirmed; package undecided | six pins agree | top mark `Z16MG` SC-88, `C7KK` SIP6/UDFN6 |
| U19 (Pico W) | BCM43439 is the Broadcom name | refuted as documented | no held document prints it; the CYW43438 sheet maps only BCM43438; the SDK loads firmware for the 43439A0 die | not needed |
| panel | Tianxianwei TXW392001B0 base model, ST7365P, SPI/MCU interface | supported as the base model, one source | both forum posts rest on the same TXW listing and supplier statement, so they count once: one relays the supplier naming the ST7365P for the TXW392001B0 page, the other calls TXW392001B0 the same base model at a higher resolution; ClockworkPi ships the ST7365P specification, and the vendor drivers program 320x320 | the module's FPC or back label |
| panel | the listing's 480x480 glass, of which the PicoCalc uses 320x320 | refuted horizontally, open vertically | the ST7365P has 320 RGB source outputs (specification page 1), so 160 of 480 columns would have no driver; the controller has 480 gate lines and the drivers show 320 (VSCRDEF 0/320/160), so the vertical half of the hypothesis survives documents | pixel pitch: a 3.92 inch square is 70.4 mm a side, 0.220 mm per pixel at 320 and 0.147 mm at 480; or fill frame-memory rows 320-479 and watch whether any row lights |

Schematic findings from the same read: U201 drives PICO_EN from PA13 and
PA_EN from PA14. Each net label on the U201 symbol sits about 2 pt above
its pin name in the PDF's text layer, so pairing labels with the nearest
pin name shifts every net one pin up; the silicon-fixed functions decide
the pairing (M_UART1_TX/RX on PA9/PA10 and M_USB_DM/DP on PA11/PA12, the
USART1 and USB pins), and the vendor firmware drives PA13 as the Pico
enable and PA14 as PA_EN (`picocalc_keyboard.ino`). PA13 and PA14 are
the SWDIO and SWCLK pins, which RM0008 puts in input pull-up and pull-down
after reset; on an ST part PICO_EN therefore reads
high and both amplifiers stay shut down until firmware remaps SWJ_CFG
(hypothesis for the CKS part). M_I2C1 uses the I2C1 remap pins PB8/PB9.
The audio input capacitors are 15 nF against 47 kOhm, a 225 Hz corner.
U701, U702 and U703 show no decoupling capacitor on VCC, which the CH340
datasheet (page 3) asks for.

## Findings beside the coverage

The schematic wires R701 and R702, 5.1 kOhm each, from J701's CC1 and CC2
to ground. `sys/arch/rp2040/doc/PICOCALC.md` ("USB-C and power") carries
the consequence: a missing CC pull-down no longer explains the owner's
board staying off on USB-C without cells unless the board leaves R701 and
R702 unfitted, and the power-key explanation, with SW102 on the AXP2101's
PWRON pin, stands.

U201 is printed `CKS32F103Rx`, a CKS32 part, while the vendor keyboard
`README.md` names `STM32F103R8T6`. The marking on the owner's board
decides which document set governs; `docs/picocalc/INDEX.md` in the notes
repository keeps the vendor's name until then.

## Pending checks

- CYW43439 version 07_00: Infineon's download link loops in a signed-in
  session; Rev. \*D is the newest public copy.
- Read the top marks of U102, U201, U401, U501, U502, U601 and U702 and
  the R701 and R702 sites on the owner's board; read U201's flash-size word
  (0x1FFFF7E0) through the USART1 bootloader, and its CPUID (0xE000ED00)
  over wired SWD or from a firmware build.
- The panel module datasheet, the 0.5-40PBSX connector, the JC101GV01
  volume potentiometer and the TP-1221U-K9K5325 switch have no source
  that served them; the notes repository's
  `docs/NOT-FETCHED.tsv` records each search.
