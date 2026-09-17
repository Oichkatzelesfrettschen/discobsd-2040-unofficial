# Firmware on an original Raspberry Pi Pico (RP2040, silicon B2): what exists, what an owner can update

Scope: the bare Raspberry Pi Pico (non-W), RP2040 silicon revision B2. Pico W is
covered separately in section 4 for contrast. All claims below are tagged
**confirmed** (read directly from the cited primary source), **inferred**
(a conclusion drawn from confirmed facts, stated as such), or **unverified**
(could not be confirmed from a primary source in this research).

Primary sources fetched and searched directly (PDF converted with `pdftotext`,
schematic pages rendered with `pdftoppm` and read as images):

- RP2040 Datasheet, document `RP-008371-DS-1`, revision dated 20 February 2025 --
  `https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf`
- Raspberry Pi Pico Datasheet, document `RP-008307-DS-2` -- `https://pip-assets.raspberrypi.com/categories/610-raspberry-pi-pico/documents/RP-008307-DS-2-pico-datasheet.pdf`,
  including Appendix B, Figure 19, the Rev3 board schematic (rendered and read as an image)
- Raspberry Pi Pico W Datasheet -- `https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf`
- `raspberrypi/pico-bootrom-rp2040` on GitHub, README and releases, read via `gh api`
- `raspberrypi/pico-sdk` releases (`gh api repos/raspberrypi/pico-sdk/releases`) and source file
  `src/rp2_common/pico_unique_id/unique_id.c` and `src/rp2_common/hardware_flash/flash.c`, fetched raw
- `raspberrypi/picotool` releases, via `gh api`
- `georgerobotics/cyw43-driver` README and `firmware/README.md`, fetched raw
- Winbond W25Q16JV datasheet (via `wiki.amperka.ru`'s hosted copy), converted with `pdftotext` and read in full
- `www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html`, the Pico-series variant comparison page

## 1. The RP2040 bootrom: fixed at fabrication, not flashable

RP2040 Datasheet section 2.8, "Bootrom": "The bootrom is software that is
built into the chip, performing the 'processor controlled' part of the boot
sequence" (section 2.7, introducing section 2.8), and further into section
2.8: "Since the Bootrom is immutable, it aims for compatibility" (page 130 of
the PDF). The datasheet never uses the literal phrase "mask ROM" -- "mask ROM"
is the conventional industry term for a ROM whose bit pattern is fixed by the
photomask at fabrication, and it is **inferred**, not a direct quote, when
applied to the RP2040 bootrom. The datasheet's own words -- "built into the
chip" and "immutable" -- are the confirmed facts; there is no register, flash
region, or documented procedure anywhere in the datasheet for writing to the
bootrom. The bootrom occupies a fixed 16 kB (section 2.8: "The Bootrom size is
limited to 16kB").

There is no table mapping silicon revision to bootrom version. The mapping
appears only as prose, in a "Bootrom Source Code" call-out box inside section
2.8: "The full source for the RP2040 bootrom can be found at
https://github.com/raspberrypi/pico-bootrom. This includes versions 1, 2 and
3 of the bootrom, which correspond to the B0, B1 and B2 silicon revisions,
respectively." (That URL now redirects to
`https://github.com/raspberrypi/pico-bootrom-rp2040`, the repo the task
names.) Table 163 in section 2.8.3, "Bootrom Contents," documents only the
*location* of a version indicator -- a single byte at fixed address
`0x00000013` labeled "Bootrom version" -- not a revision-to-version mapping;
the mapping itself lives in the prose quoted above.

The `raspberrypi/pico-bootrom-rp2040` repository confirms this independently.
Its three tagged releases, read via `gh api repos/raspberrypi/pico-bootrom-rp2040/releases`:

| Tag | Commit | Release body |
|---|---|---|
| `b0` | `c09c7f0` | "The version of the bootrom in RP2040-B0" |
| `b1` | `ad55537` | "The version of the bootrom in RP2040-B1" |
| `b2` | `ef22cd8` | "The version of the bootrom in RP2040-B2" |

The `master` branch README (fetched raw): "This is the B2 version of the
RP2040 bootrom. The version on the chip was built in *Debug* mode using GCC
9.3.1 (GNU Arm Embedded Toolchain 9-2020-q2-update). Note the GIT revision
info (included in the bootrom) on chip does not match the GIT revision of
this branch." This is source code Raspberry Pi publishes for reference and
auditing; nothing in the repository or the datasheet describes a procedure to
write a rebuilt bootrom onto a fabricated chip. The bootrom differs between
steppings only because each stepping's silicon was fabricated with a
different mask.

Content actually changed between bootrom versions, per the datasheet:

- V1 to V2 (B0 to B1): double-precision floating point support is added.
  Section 2.8.3.2: "Whilst the overall goal ... Additionally V2 onwards also
  contain an optimized double-precision floating point implementation," and
  later, "Note that double-precision floating point support is not present in
  version 1 (V1) of the bootrom." Table 169's caption: "The functions (and
  table entries) from offset 0x54 onwards are only present in the V2 ROM."
- V2 to V3 (B1 to B2): errata RP2040-E6, "GPIO digital inputs not disabled
  for ADC pins by default" -- "Workaround ... This is done in the RP2040B2
  bootrom and early on in SDK platform setup code on RP2040B0 and RP2040B1."
  "Fixed by RP2040B2 bootrom. Fixed on RP2040B0 and RP2040B1 in SDK." This
  shows the bootrom's early-init code itself differs between B1 and B2
  silicon, not only USB electrical behavior.

## 2. Silicon revisions: B0, B1, B2 confirmed; B2 newest of the three documented; "final" is inferred

Appendix B, "Errata," lists an `Affects` field on every entry, and across
every errata entry read in this research, the only revision labels used are
`RP2040B0`, `RP2040B1`, and `RP2040B2` -- no `A`-stepping and no revision past
B2 appears anywhere. This is **confirmed**: three steppings exist in the
documentation. Whether pre-B0 engineering-sample steppings (an A0/A1) ever
existed is **unverified** -- the datasheet simply never mentions any.

B2 is the newest of the three documented steppings (**confirmed**: it is the
only one ever named as a `Fixed by` target). Across every `Fixed by` line in
Appendix B, `RP2040B1` never appears alone as a fix target -- fixes are
"RP2040B2", "Software", "Documentation", "Not fixed", or combinations of
those, but B1 fixed nothing in hardware by itself. That matches the
"B1 adds bootrom features but fixes no silicon errata" pattern found in
V1-to-V2 bootrom differences above.

Whether B2 is the *final* stepping is **inferred**, not stated outright
anywhere in the datasheet. Supporting evidence: (a) no B3 or later appears in
any datasheet revision through 20 February 2025, the latest revision fetched;
(b) the Documentation Release History's last stepping-related entry is dated
30 September 2021: "Added information about B2 release. Updated errata for B2
release."; (c) Appendix C, "Availability": "We expect RP2040 to remain in
production until at least January 2041," with no forward-looking note about a
future stepping. The physical Pico measured in this repository's
`DEVICE.md` independently reports silicon revision B2 via `picotool info -d`,
consistent with B2 being the currently shipping (and, by the above, likely
final) revision.

What changed, hardware-wise, between B0/B1 and B2: two USB device-controller
issues, fixed only in B2 silicon.

- **RP2040-E2**, "USB device endpoint abort is not cleared." "Affects
  RP2040B0, RP2040B1" / "Fixed by RP2040B2." A logic error causes the USB
  device controller to NAK forever on all endpoints once `EP_ABORT` is set on
  any endpoint.
- **RP2040-E5**, "USB device fails to exit RESET state on busy USB bus."
  "Affects RP2040B0, RP2040B1" (B2 not listed as affected). "There is a
  hardware fix in RP2040B2 which avoids the need for 800us of IDLE time after
  RESET state." A device behind a busy USB hub transaction translator could
  fail to enumerate on B0/B1; B2 removes the underlying timing requirement in
  hardware.

Section 4.1.2.8.5, "Errata," in the USB device controller chapter summarizes
both: "There are two hardware issues with the device controller, both of
which have software workarounds on RP2040B0, RP2040B1, and are fixed in
hardware on RP2040B2. See RP2040-E2 and RP2040-E5 for more information."

## 3. No user-updatable firmware, microcode, or persistent configuration beyond the application flash

**Bootrom.** Immutable per section 1 above. No update mechanism exists or is
documented.

**OTP / eFuse on RP2040.** A full-text search of the extracted RP2040
datasheet for "OTP," "eFuse," "e-fuse," "one-time," and "fuse" returns zero
matches describing any such block on RP2040 itself (the only "fuse" hits are
unrelated CPUID register text). RP2040 has no OTP array. This is corroborated
by `pico-sdk` source: `src/rp2_common/pico_unique_id/unique_id.c` branches on
`#if PICO_RP2040` to read the *flash chip's* unique ID (`flash_get_unique_id`)
for the board ID, and only in the `#else` (RP2350) branch does it call
`rom_get_sys_info_fn(... SYS_INFO_CHIP_INFO)`, a ROM function that on RP2350
reads chip info backed by that part's OTP. RP2040 has no equivalent path.
Public reporting on the RP2350 datasheet describes an 8 kB anti-fuse OTP array
used for cryptographic keys and boot-signing configuration -- this RP2350 detail
is **reported from search-engine snippets of the RP2350 datasheet, not an
independently fetched and read primary source in this research**, and is
included only for contrast, not as a fully verified claim.

**The flash chip's own state.** The Winbond W25Q16JV on the Pico does hold one
piece of persistent, non-application state: a factory-programmed 64-bit
Unique ID, read with SPI opcode `0x4B` ("Read Unique ID"). This is read-only
to the user -- it is not writable, let alone updatable, firmware. Confirmed
from `pico-sdk` source `src/rp2_common/hardware_flash/flash.c`:
`#define FLASH_RUID_CMD 0x4b`, used by `flash_get_unique_id()`. This
repository's own `DEVICE.md` independently measured this ID from the physical
board (`e66488c15f098435`, via `tools/flash-id`, opcode `0x4b`) and
distinguishes it from the separate boot-ROM-issued USB serial number
(`E0C9125B0D9B`).

The W25Q16JV datasheet itself (Winbond, fetched and converted directly) lists
a second piece of persistent state beyond the unique ID: "the device supports
JEDEC standard manufacturer and device ID and SFDP Register, a 64-bit Unique
Serial Number and three 256-bytes Security Registers" (section 1, Description),
repeated in the features list as "3X256-Bytes Security Registers with OTP
locks." Programming, reading, and erasing them use dedicated opcodes listed
in the instruction table: Program Security Registers (`42h`), Read Security
Registers (`48h`), Erase Security Registers (`44h`). Section 8.6, "Security
Register Lock Bits (LB3, LB2, LB1) -- Non-Volatile OTP Writable": "The
Security Register Lock Bits (LB3, LB2, LB1) are non-volatile One Time Program
(OTP) bits in Status Register (S13, S12, S11) ... The default state of LB3-1
is 0, Security Registers are unlocked." So the flash chip exposes 3 x 256
bytes of user-programmable one-time storage plus three OTP lock bits -- this
is OTP data storage a user could in principle write once via SWD-driven flash
commands, not firmware, and neither the RP2040 datasheet nor pico-sdk exposes
any built-in tooling to use them on a stock Pico. The flash chip's own
controller behavior (how it interprets SPI/QSPI commands) is fixed silicon
logic from Winbond, with no field-update path documented anywhere in the
sources checked. The exact part, `W25Q16JVUXIQ` (USON-8, 16 Mbit), appears in
the same datasheet's ordering-information table (section 12.1) among the
valid Winbond part numbers, matching the label on the Pico schematic in
section 6 below.

**USB descriptors.** BOOTSEL-mode USB descriptors (VID/PID, strings) are
compiled into the immutable bootrom and are not persistent configuration --
they are as fixed as the rest of the bootrom. A running application can
present its own descriptors, but those live in the same 2 MB QSPI flash as
the rest of the user's program, not in any separate persistent-configuration
store.

**Conclusion for a bare Pico:** apart from the user's own compiled
application occupying the 2 MB QSPI flash, there is no user-updatable
firmware or microcode on the board. The only non-application persistent data
is on the flash chip itself: a read-only factory 64-bit unique ID, and 3 x
256 bytes of one-time-programmable Security Registers plus their OTP lock
bits that Raspberry Pi's own tooling never writes on a stock board -- data
storage, not firmware.

## 4. Pico W contrast: CYW43439 wireless "firmware" is redelivered from RP2040 flash at every boot, not stored on the wireless chip

Pico W Datasheet section 3.8, "Wireless interface": "Pico W contains an
on-board 2.4 GHz wireless interface using the Infineon CYW43439 ... The
wireless interface is connected via SPI to the RP2040." Flash: identical
statement to the plain Pico -- "flash memory (Winbond W25Q16JV)" (section 1)
and "The on-board 2 MB QSPI flash can be (re)programmed..." (section 3.1).

The CYW43439 itself holds no persistent firmware of its own that a user
updates in place. `georgerobotics/cyw43-driver` (the driver pico-sdk's
`pico_cyw43_driver` wraps), README: layer 2 of the driver, "Low-level CYW43xx
interface, managing the bus, control messages, Ethernet frames and
asynchronous events. Includes download of SoC firmware." The
`firmware/README.md` in that same repository: "This directory contains
firmware patch blobs that need to be downloaded on to the CYW43xx SoC in
order for it to function correctly," describing WiFi firmware, an
NVRAM/CLM blob, and Bluetooth firmware, each converted with `xxd -i` into C
header files (for example `cyw43_btfw_43439.h`) and compiled directly into
the application binary. In other words: the "firmware" lives as byte arrays
in the RP2040's own 2 MB QSPI flash (part of the user's compiled program) and
is pushed to the CYW43439's volatile RAM over SPI every time the wireless
interface initializes. "Updating the CYW43439 firmware" means rebuilding the
application against a newer `cyw43-driver`/`pico-sdk` and reflashing the Pico
W the same way any other application update happens -- it is not a separate,
persistent update on the wireless chip itself. This mechanism, and the
firmware blobs themselves, do not exist on a non-W Pico: there is no CYW43439,
no SPI wireless link, and nothing in the plain Pico's boot or flash path
downloads anything to a companion chip.

## 5. pico-sdk version and what "updating" means for a Pico owner

Latest `pico-sdk` release, via `gh api repos/raspberrypi/pico-sdk/releases`:
**2.3.1**, published 2026-09-04T22:38:41Z. Release history (tag, publish
date): 2.3.1 (2026-09-04), 2.3.0 (2026-07-03), 2.2.0 (2025-07-29), 2.1.1
(2025-02-19), 2.1.0 (2024-11-25), 2.0.0 (2024-08-08). Latest `picotool`
release, via `gh api repos/raspberrypi/picotool/releases`: **2.3.1**,
published 2026-09-05T15:17:45Z -- paired with the SDK release a day later.
This repository's `DEVICE.md` records the locally installed toolchain as
`picotool` 2.3.0-2 (a locally patched package) and `pico-sdk` 2.3.0, one minor
version behind the current upstream release as of this writing.

For a Pico owner, "updating" never touches anything resident on the chip
except the user's own application:

- **pico-sdk** is a host-side C/C++ SDK and CMake toolchain (used together
  with `arm-none-eabi-gcc` etc.). Updating it changes what a developer can
  compile; it installs nothing onto the Pico by itself.
- **picotool** is a host-side USB utility for querying a BOOTSEL-mode Pico
  and loading/verifying application images. Updating it changes host tooling
  only.
- **The UF2 bootloader** -- the thing that makes a Pico in BOOTSEL mode
  present as a USB mass-storage drive that accepts `.uf2` files -- is not a
  separate flash-resident bootloader as on many other microcontroller boards.
  RP2040 Datasheet section 2.8: the bootrom itself contains "USB MSC
  class-compliant bootloader with UF2 support for downloading code/data to
  FLASH or RAM" and "USB PICOBOOT bootloader interface for advanced
  management." This bootloader lives inside the immutable bootrom described
  in section 1, not in the 2 MB QSPI flash. The Pico Datasheet states this
  outright in section 4.1, "Programming the flash," immediately after
  describing the BOOTSEL drag-and-drop flow: "The USB boot code is stored in
  ROM on RP2040, so can not be accidentally overwritten." (The Pico W
  Datasheet repeats the identical sentence in its own section 3.1.) There is
  no `.uf2` file, no `picotool` command, and no SDK release that updates it --
  it is fixed for the life of the chip. What an owner actually updates via
  UF2 drag-and-drop or `picotool load` is only ever their own application
  image in the 2 MB QSPI flash.

## 6. Flash chip: Winbond W25Q16JVUXIQ, 2 MB, confirmed for all four Pico 1 variants

The Pico Datasheet body text (section on external circuitry): "Pico provides
minimal (yet flexible) external circuitry to support the RP2040 chip: flash
(Winbond W25Q16JV), crystal (Abracon ABM8-272-T3), power supplies and
decoupling, and USB connector." Section 4.1, "Programming the flash": "The
on-board 2 MB QSPI Flash can be (re)programmed either using the Serial Wire
Debug port or by the special USB Mass Storage Device mode."

The exact package-level part number, `W25Q16JVUXIQ`, is visible directly on
the schematic: Appendix B, Figure 19, "The Raspberry Pi Pico Rev3 board
schematic" (drawn by James Adams, dated Friday 15 January 2021, title block
`RPI-PICO` Rev 3), component `U3`, labeled `W25Q16JVUXIQ` with pins CS,
DI_IO0, DO_IO1, WP_IO2, HOLD_IO3, CLK, VCC, GND. This schematic page was
rendered from the official PDF and read directly as an image for this
research. The Winbond W25Q16JV datasheet's own ordering-information table
(section 12.1) confirms `W25Q16JVUXIQ` as a valid part number in a USON-8
package, 16 Mbit -- matching the schematic label exactly.

The Pico W Datasheet uses identical wording for its own flash: "Raspberry Pi
Pico W provides a minimal yet flexible external circuitry to support the
RP2040 chip: flash memory (Winbond W25Q16JV) ..." and "The on-board 2 MB QSPI
flash can be (re)programmed either using the serial wire debug port or by
the special USB mass storage device mode" (section 3.1).

All four first-generation variants -- Pico, Pico H, Pico W, Pico WH -- are
confirmed to carry 2 MB of flash. The strongest evidence is the pair of
datasheets read directly above: the Pico Datasheet covers both headerless
Pico and header-fitted Pico H under one schematic and one "flash (Winbond
W25Q16JV)" statement (its ordering table, Appendix A, lists "Raspberry Pi
Pico" order codes SC0915/SC0916 and "Raspberry Pi Pico H" order code SC0917
against that same schematic); the Pico W Datasheet likewise covers both
Pico W and Pico WH under one schematic and one "flash memory (Winbond
W25Q16JV)" statement (its ordering table lists "Raspberry Pi Pico W" order
code SCO918 and "Raspberry Pi Pico WH" order code SCO919 -- the Pico order
codes are printed with a digit `0` and the Pico W order codes with a letter
`O`, exactly as they appear in the respective PDFs; this is not a
transcription error here). The official Pico-series documentation
comparison page
(`raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html`)
corroborates this, listing "2 MB of on-board flash memory" for all four
boards and noting the variants differ only in presoldered headers (H/WH)
and the wireless module plus antenna (W/WH), never in flash capacity.

Independent confirmation from real hardware, `DEVICE.md` in this repository:
a physical Pico's flash chip answered JEDEC opcode `0x9f` with `ef 40 15` --
`ef` = Winbond manufacturer ID, `40` = W25Q SPI family, `15` = capacity
exponent for 2^21 bytes = 2097152 bytes = 2 MiB -- read directly off the chip
by a custom no-flash `tools/flash-id` program, not inferred from any
datasheet claim.

## Summary table

| # | Question | Answer | Status |
|---|---|---|---|
| 1 | Is the RP2040 bootrom mask ROM or flashable? | Fixed at fabrication ("built into the chip," "immutable" per datasheet section 2.8); no write mechanism exists. "Mask ROM" is the conventional term, applied here by inference. | Confirmed (immutability); inferred (term "mask ROM") |
| 1 | Bootrom version per silicon revision | No table; stated in prose (section 2.8 sidebar): versions 1/2/3 correspond to B0/B1/B2. Table 163 documents only the version byte's address, `0x00000013`. | Confirmed |
| 2 | Silicon revisions that exist | B0, B1, B2 -- the only labels used anywhere in Appendix B errata. | Confirmed |
| 2 | Newest / final stepping | B2 is newest of the three documented; "final" is not stated outright, inferred from no B3 appearing through the 20 Feb 2025 doc revision plus 2041 production-availability language. | B2-newest: confirmed. Final: inferred |
| 2 | B0/B1 -> B2 changes | Two USB device-controller hardware fixes (E2, E5) fixed only in B2; bootrom gains double-precision float in B1 and an earlier ADC-pin GPIO-disable fix in B2. | Confirmed |
| 3 | User-updatable firmware/microcode/config beyond app flash | None. Bootrom immutable; RP2040 has no OTP/eFuse (zero datasheet mentions, and `pico-sdk` reads RP2350-only OTP-backed chip info via a code path that does not exist for RP2040); the flash chip itself holds a read-only factory 64-bit unique ID (opcode `0x4B`) and 3x256-byte OTP Security Registers (opcodes `42h`/`44h`/`48h`) -- data storage, not firmware, and unused by stock tooling. | Confirmed |
| 4 | Pico W CYW43439 firmware updatable? | Not persistently on the chip -- it is a blob compiled into the RP2040's own 2 MB flash and pushed to the wireless chip's volatile RAM at every init. "Updating" it means rebuilding and reflashing the RP2040 application. Does not apply to a non-W Pico (no CYW43439 present). | Confirmed |
| 5 | Current pico-sdk / picotool version | pico-sdk 2.3.1 (2026-09-04); picotool 2.3.1 (2026-09-05). | Confirmed via GitHub API |
| 5 | What "updating" means for an owner | SDK and picotool are host-side tools; the UF2 bootloader lives inside the immutable RP2040 bootrom, not in flash -- unlike boards where a flash-resident bootloader can itself be reflashed; only the user's own application in QSPI flash is ever field-updated. | Confirmed |
| 6 | Official flash part | Winbond W25Q16JVUXIQ, 2 MB, labeled directly on the Pico Rev3 schematic (component U3) and confirmed independently by JEDEC ID read off a physical board (`ef 40 15`). | Confirmed |
| 6 | All variants (Pico/H/W/WH) 2 MB? | Yes, per the official Pico-series documentation comparison page; variants differ only in headers and wireless, never flash size. | Confirmed |
