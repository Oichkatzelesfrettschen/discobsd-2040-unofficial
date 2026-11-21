# PicoCalc backlight firmware lane: plan for owner approval

D194 (owner, 2026-11-21): research a controller-firmware lane that gives the
OS the screen backlight's full PWM range, as a read-only plan (patch, build,
flash route, recovery) approved before any flash. This document is that
plan. It leaves the vendor firmware's files and the board untouched; every
command below that writes the keyboard controller waits for the owner's
approval of the exact list in "Approval list".

Sources: the clockworkpi/PicoCalc repository at `fdcfe2c`
(`Code/picocalc_keyboard`, `Bin/`, and the wiki page "Setting Up Arduino
Development for PicoCalc keyboard"), cited by file and line;
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
below 32 or above 240 off: 8 keyboard levels. `lcd_backlight_update_reg()`
writes the stored byte to the PWM unrounded, and it has zero callers. The
PWM itself takes 256 levels, so the rounding is the whole limit.

## 2. The patch

Three changes to `Code/picocalc_keyboard`, kept as a patch file beside this
document when the lane proceeds:

1. `REG_ID_BKL` write: clamp the byte to [16, 240] and write it to PA8
   exactly (`lcd_backlight_update_reg()` after the clamp), so every value
   the OS sends in range lands as sent. The floor keeps the panel lit, as
   the rounding does today.
2. `REG_ID_BK2` write: clamp to [0, 240] and write PC8 exactly, with 0 as off.
3. `BIOSVERSION` (`conf_app.h`) raised past every vendor release, so the
   OS reads register 0x01 (`REG_ID_VER`) and knows the exact-level firmware
   is present.

The Fn key steps (`keyboard.ino`, `lcd_backlight_update(+-LCD_BACKLIGHT_STEP)`)
keep their 16-step rounding: they move from the current level in steps, so
a key press after a fine OS setting snaps back to the grid, which is the
vendor's behavior for keys and leaves the OS path exact.

OS side, after the firmware: `backlight(1)` and the MATEzinho adjuster
offer fine steps only when `REG_ID_VER` reports the new version, and
PICOCALC.md's firmware-rounding paragraph gains the version condition.
The kernel passes levels through and clamps to 0-240 already, so it stays
as it is. The Renode keyboard model, which conditions levels as the vendor
firmware does (`picocalc-renode-keyboard-registers.md`), gains a version
switch.

## 3. Build

The wiki's toolchain, pinned: STM32duino core 2.10.0 ("STM32 MCU based
boards"), board "Generic STM32F1 series", part number Generic F103R8Tx, and
XPowersLib 0.2.3 from the `stm32f103r8t6` branch of cuu/XPowersLib. With
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
2. Backup: `stm32flash -r kbd-backup.bin -S 0x08000000:65536 PORT`, the
   wiki's extraction command, then its SHA-256 beside the vendor
   binaries' MD5 sums, which names the installed release.

## 5. Flash route

The wiki's route: DIP switch 1 on (SW701, which raises BOOT0 and routes the
CH340C to the controller's USART1), USB-C to the host, a long press of the
power key, then

    stm32flash -w build/picocalc_keyboard.ino.bin -v -S 0x08000000 PORT

and DIP switch 1 off. `-v` reads every written block back. PORT is the
CH340C's serial node, named through `/dev/serial/by-id/` so that the Pico's
own CDC-ACM console stays out of the command.

## 6. Recovery

The system bootloader lives in the controller's ROM, which a flash write
leaves intact, so a bad image recovers by the same route with the backup:
`stm32flash -w kbd-backup.bin -v -S 0x08000000 PORT`. The plan writes only
the main flash. Option bytes, readout protection and write protection
stay untouched (`stm32flash` options `-j`, `-k`, `-e` and the option-byte
commands are excluded), because readout protection would lock the backup
path itself. A controller running a bad image may leave the Pico unpowered
(PA13 drives PICO_EN), which the recovery route survives: the bootloader
runs without the Pico.

## 7. Verification after the flash

- `REG_ID_VER` returns the new version (the DiscoBSD keyboard driver
  reads it; `sysctl` exposes it).
- Writing 0x05 with 100 reads back 100; the vendor firmware returns 96.
  Writing 17 reads back 17; writing 8 reads back 16.
- The panel's brightness changes between 96 and 100 (eye, or a light
  sensor against the panel for a number).
- The Fn keys still step the screen by 16 and the keyboard light cycles.
- `check-backlight-host` and `check-renode-picocalc` pass with the model's
  version switch in both positions.

## 8. Approval list

Each item runs only after the owner approves this list as written, on the
board the owner names:

1. Install arduino-cli, STM32duino 2.10.0 and XPowersLib 0.2.3 (cuu fork,
   `stm32f103r8t6`).
2. Build the patched firmware (section 3) and record its SHA-256 and size.
3. Read the identity (section 4, item 1).
4. Read the backup (section 4, item 2) and record its SHA-256.
5. Write the patched image (section 5) with `-v`.
6. Verify (section 7); on any failure, write the backup back (section 6).
