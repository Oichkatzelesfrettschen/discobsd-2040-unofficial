# PicoCalc backlight firmware lane: plan for owner approval

D194 (owner, 2026-11-21): research a controller-firmware lane that gives the
OS every screen backlight level from 16 to 240 exactly (225 levels in place
of 15), as a read-only plan (patch, build,
flash route, recovery) approved before any flash. This document is that
plan. It leaves the vendor firmware's files and the board untouched; every
command below that writes the keyboard controller waits for the owner's
approval of the exact list in "Approval list".

Sources: the clockworkpi/PicoCalc repository at `fdcfe2c`
(`Code/picocalc_keyboard`, `Bin/`, and the wiki page "Setting Up Arduino
Development for PicoCalc keyboard"), cited by file;
`docs/research/picocalc-vendor-repository-harvest.md` for the register map;
`sys/arch/rp2040/doc/PICOCALC.md` for the OS side.

## 1. What limits the steps today

The keyboard controller (U201, an STM32F103R8-class part; the board prints
`CKS32F103Rx`) drives the screen backlight with a 10 kHz, 8-bit PWM on PA8
and the keyboard backlight on PC8 (`backlight.ino`, `analogWriteFrequency(10000)`,
`analogWrite(PA8, val)`). An I2C write of register 0x05 (`REG_ID_BKL`) stores
the byte and calls `lcd_backlight_update(0)` (`picocalc_keyboard.ino`, the
`REG_ID_BKL` case), which rounds the stored value down to a multiple of
`LCD_BACKLIGHT_STEP` (16, `conf_app.h`), raises it to 16 and caps it at 240:
15 distinct screen levels. Register 0x0A (`REG_ID_BK2`) goes through
`kbd_backlight_update(0)`, which rounds to multiples of 32 and turns values
below 32 off: 8 keyboard levels (a raw 241-255 rounds down to 224 before
the test for values above 240, so that test never fires). `lcd_backlight_update_reg()`
writes the stored byte to the PWM unrounded, and it has zero callers. The
PWM itself takes 256 levels, so the rounding is the whole limit.

## 2. The patch

Three changes to `Code/picocalc_keyboard`. The vendor directory lacks a
license (`picocalc-vendor-repository-harvest.md`), so the patch lives in
the owner's private copy of the vendor repository, outside this tree, and
this document describes it:

1. `REG_ID_BKL` write: clamp the byte to [16, 240] and write it to PA8
   exactly (`lcd_backlight_update_reg()` after the clamp), so every value
   the OS sends from 16 to 240 lands as sent. The floor keeps the panel
   lit, as the rounding does today; a request from 0 to 15, which the
   kernel and `backlight(1)` accept, still lands at 16, the kept range `picocalcvar.h` and PICOCALC.md
   already document.
2. `REG_ID_BK2` write: clamp to [0, 240] and write PC8 exactly, with 0 as off.
   A direct I2C write of 241-255, which lands at 224 today, lands at 240.
3. `BIOSVERSION` (`conf_app.h`) set to the private value 0xB7, which no
   vendor release has used: `conf_app.h` defines 0x16, the `REG_ID_VER`
   handler of both the v1.6 and v1.7 binaries stores 0x16 (`movs r3, #22`
   then `strb r3, [r4, #1]`, at 0x262c and 0x2634 in
   `arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb` of
   `Bin/PicoCalc_BIOS_v1.6.bin` and `v1.7.bin`), and earlier releases
   predate the register. The OS treats the
   exact-level firmware as present only when `REG_ID_VER` equals 0xB7
   exactly, a predicate the firmware, the kernel's version node and the
   Renode model's tests all pin; a later vendor release with any other
   value keeps the rounded behavior.

The Fn key steps (`keyboard.ino`, `lcd_backlight_update(+-LCD_BACKLIGHT_STEP)`)
keep their 16-step rounding: they move from the current level in steps, so
a key press after a fine OS setting snaps back to the grid, which is the
vendor's behavior for keys and leaves the OS path exact.

OS side, after the firmware: the kernel reads the version at attach
(`picokbd_version()`, `dev/picokbd.c`) but keeps it for the boot message
alone, so a read-only sysctl node for it comes first, with its host test
and the text cost measured on PICOCALC. `backlight(1)` already passes any level
from 0 to 240 as given; the MATEzinho adjuster, which steps by
`MZ_SCREEN_STEP`, offers fine steps only when that node reports the new
version, and `backlight.1` and PICOCALC.md's firmware-rounding paragraph
gain the version condition. The level path itself (clamp to 0-240, pass through) stays as
it is. The Renode keyboard model, which conditions levels as the vendor
firmware does (`picocalc-renode-keyboard-registers.md`), gains a version
switch. That OS-side change is gated on `check-backlight-host` and
`check-renode-picocalc` passing with the model's switch in both
positions.

## 3. Build

The wiki's toolchain, pinned: STM32duino core 2.10.0 ("STM32 MCU based
boards"), board "Generic STM32F1 series", part number Generic F103R8Tx, and
XPowersLib 0.2.3 from the `stm32f103r8t6` branch of cuu/XPowersLib, pinned
to the exact commit the build uses, with that commit and the archive's
SHA-256 recorded before the build, since a branch name can move. With
arduino-cli the build is one command and its output a binary:

    arduino-cli compile --fqbn STMicroelectronics:stm32:GenF1:pnum=GENERIC_F103R8TX \
        --output-dir build Code/picocalc_keyboard

Installing the core and the library is a package install, so the owner
runs it or grants it. The vendor's v1.6 and v1.7 binaries are 40,596 and
40,604 bytes; a patched build lands within a few dozen bytes of that, well
inside 64 KiB, the smallest flash the part's letter allows.

## 4. Before any write: identity and backup

Both steps only read.

1. Identity: `tools/picocalc/kbd_bootloader_id.py` (read commands only,
   `check-picocalc-kbd-bootloader`) reports the product ID (0x410 for
   the medium-density family the wiki's log shows), the flash-size word and
   the unique ID, which settles the x8/xB question for this board.
2. Backup: `"$PYTHON" tools/picocalc/kbd_bootloader_id.py --dump kbd-backup-1.bin PORT`,
   then again with `kbd-backup-2.bin` (the tool refuses an existing path), which reads
   the whole application flash, sized by the flash-size word the identity
   step reports (64 KiB on an x8 part, 128 KiB on an xB part), and records
   the dump's SHA-256. To name the installed release, the MD5 of the
   backup's first 40,596 bytes is compared with v1.6's
   (018705f91e20ad5f282b9b6eee8eede5, the vendor's own `.md5sum`), of its
   first 40,604 bytes with v1.7's (4113b190831e306057d3d0b8eb617c61,
   computed here from `Bin/PicoCalc_BIOS_v1.7.bin` at `fdcfe2c`, which
   ships without a checksum file), and of its first 65,536 bytes with the
   vendor's sums for v1.2 and v1.4.

## 5. Flash route

The `stm32flash` behavior below comes from its manual page and `main.c` at
stm32duino/stm32flash `8bc5ceb3d`; this plan has not executed it. `-S
address:length` erases the pages covering that range (`main.c` computes
`first_page` and `num_pages` from it, and selects a mass erase when the
range runs from the first page to the end of flash); `-v` reads every
written block back; `-g address` starts execution there once the write
ends, through the bootloader's Go command, which jumps without a reset.

Each step's state, from the wiki's route (SW701 switch 1 raises BOOT0 and
routes the CH340C to the controller's USART1; switch 2 stays off
throughout, since switch 2 on routes the USB-C data pair to the
controller's USB pins through U701 and disconnects the CH340C,
`picocalc-first-power-up.md`):

| Step | Switch 1 | Switch 2 | Controller runs | USART1 | Command |
|---|---|---|---|---|---|
| a. Enter | on | off | ROM bootloader, after a power-on reset with BOOT0 high | CH340C | power the board off (a long press of the power key while the vendor firmware runs), then on |
| b. Identity, backup | on | off | ROM bootloader | CH340C | section 4 |
| c. Write and start | on | off | ROM bootloader, then the new image | CH340C | `stm32flash -w build/picocalc_keyboard.ino.bin -v -S 0x08000000:LEN -g 0x08000000 PORT` |
| d. Leave | off | off | the new image; every later reset boots it, BOOT0 being low | Pico side | none |

LEN is the image's size rounded up to the 1 KiB flash page, so the erase
covers the image's pages alone; without a length `stm32flash` extends the
range to the end of flash and erases all of it. The Go command rides on the
write invocation because switch 1 still routes the CH340C at that point;
once switch 1 goes off, the host loses its serial path to the bootloader. PORT is
the CH340C's serial node, named through `/dev/serial/by-id/` so that the
Pico's own CDC-ACM console stays out of the command.

## 6. Recovery

The system bootloader lives in the controller's ROM, which a flash write
leaves intact, so a bad image recovers through the same table with the
backup. Step a then needs a power-on reset that the bad image cannot
refuse: with switch 1 on, remove all power (USB-C and cells out, as in
`picocalc-first-power-up.md`) and restore it, since the power key's long
press belongs to the application firmware. Step c becomes
`stm32flash -w kbd-backup-1.bin -v -S 0x08000000 -g 0x08000000 PORT`,
whose range runs to the end of flash and so erases all of it, which a full
backup covers by design; step d follows unchanged. The plan writes only the
main flash. Option bytes, readout protection and write protection stay
untouched (`stm32flash` options `-j`, `-k`, `-u` and the option-byte
commands are excluded; `-u` disables write protection), because readout
protection would lock the backup path itself. A controller running a bad
image may leave the Pico unpowered (PA13 drives PICO_EN), which the
recovery route survives: the bootloader runs without the Pico.

## 7. Verification after the flash

- The kernel's boot line `kbd0: keyboard controller at I2C1 0x1f,
  firmware 11.7` shows 0xB7 (the running kernel prints the version it reads
  at attach, so this check runs on the current kernel).
- Writing 0x05 with 100 reads back 100; the vendor firmware returns 96.
  Writing 17 reads back 17; writing 8 reads back 16.
- The panel's brightness changes between 96 and 100 (eye, or a light
  sensor against the panel for a number).
- The Fn keys still step the screen by 16 and the keyboard light cycles.

## 8. Approval list

Each item runs only after the owner approves this list as written, on the
board the owner names, with SW701 switch 2 off throughout:

1. Install arduino-cli, STM32duino 2.10.0 and XPowersLib 0.2.3 (cuu fork,
   `stm32f103r8t6`, at the recorded commit).
2. Build the patched firmware (section 3) and record its SHA-256 and size.
3. Read the identity (section 4, item 1).
4. Read the backup (section 4, item 2) twice and record its SHA-256. Item 5
   runs only when both reads have the full length the flash-size word
   gives and the same SHA-256; a short, failed or differing read stops the
   lane before any write.
5. Write and start the patched image (section 5, step c), then set
   switch 1 off (step d).
6. Verify (section 7); on any failure, write the backup back (section 6).
