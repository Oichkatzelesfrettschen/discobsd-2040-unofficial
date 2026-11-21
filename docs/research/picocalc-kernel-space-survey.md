# PicoCalc kernel space survey at 4eb0f4eb3

The owner asked what in the kernel image could move to the SD card and
which packing or code techniques remain (P5 brief, 2026-11-17). This record
measures the candidates on main at 4eb0f4eb3 with arm-none-eabi-gcc
16.2.0; every number is a linked `unix` image's `size` output or a
`bloaty`/`pahole` reading of it. PICOCALC is the subject because its
flash budget is the tight one: text 127,728 bytes against a 128 KB kernel
slot with a 1,024-byte margin (`check-kernel-headroom`).

## Where the text is

`bloaty -d compileunits` over PICOCALC's `unix.elf` (176 KiB of VM size
across text, data and the RAM sections):

| Unit | Size |
| --- | --- |
| `rp2040/swapram.c` (compressed swap pool) | 18.8 KiB |
| `rp2040/machdep.c` | 15.8 KiB |
| `dev/lcdterm.c` | 8.4 KiB |
| `dev/usb.c` | 8.1 KiB |
| `kern/vm_swap.c` | 6.8 KiB |
| `dev/lcdgfx.c` | 6.1 KiB |
| `kern/tty.c` | 5.8 KiB |
| `kern/ufs_syscalls.c` | 5.7 KiB |

The largest functions are `namei_at` (2,992 bytes), `lcdterm_putc`
(1,968), `usb_service` (1,708), `prf` (1,360) and `ttioctl` (1,328).

## Configuration options: the measured lever

Each row removes one `-D` option from PICOCALC's generated Makefile and
relinks (text, data and bss in bytes; the baseline is 127,728, 144 and
53,080):

| Option removed | Text saved | Bss saved |
| --- | --- | --- |
| `LCD_PANELTEST` (panel register test, `PANELTEST.md`) | 2,003 | 320 |
| `SYSTRACE` (system call trace names, `ktrace`-style output) | 1,789 | 8 |
| `DEV_PCM` (`/dev/audio`) | 544 | 0 |
| `DEV_GPIO` | 456 | 8 |
| `DEV_ADC` | 416 | 8 |

`CAPACITY_STATS` leaves a build that fails to link alone, since other
options reference its counters. The two diagnostic options together are
3,792 bytes, more than PICOCALC's current headroom over its margin; both
also ship in PICOCALC_SD, whose linker script gives it about 32 KB of room.

## Measured nulls

- **Compiler options** (PICOCALC and PICO text, against 127,728 and 98,257):
  `-fmerge-all-constants` saves 2 and 0 bytes; `-fno-jump-tables` costs
  352 and 192; `-fno-inline-functions-called-once` saves 24 on PICOCALC
  and costs 224 on PICO; `-fno-tree-switch-conversion` changes 0. GCC's
  IPA identical-code folding is already on at `-Os`, and the BFD linker
  the tree uses lacks a linker-level `--icf`.
- **Struct layout**: `pahole` finds the hot tables already packed: `proc`
  100 bytes, `inode` 92, `file` 16, `buf` 36, `tty` 100, `callout` 16,
  each without holes; `user` carries one 2-byte hole in 1,024 bytes.
- **Card-resident rodata**: the tables a loader-started PicoCalc could read
  from the card are small. The errno message pool and its tables
  (`errno_message_pool` 874, `errno_message_rules` 256,
  `errno_message_offsets` 168) and the system call names
  (`syscall_name_pool` 819, `syscall_name_spans` 326) total 2,443 bytes,
  and the syscall names already leave with `SYSTRACE`. `lcdfont` (855)
  draws the panic screen, so it stays in flash.

## Choices for the owner

1. Which diagnostic options PICOCALC keeps. Moving `LCD_PANELTEST` and
   `SYSTRACE` to the diagnostic configurations (PICOCALC_LEAN_DIAG,
   PICOCALC_SD_DIAG) frees 3,792 bytes of PICOCALC text; PANEL task 46's
   photographs use the panel test, which PICOCALC_SD keeps.
2. Whether the errno message pool moves to the card: 1,298 bytes of flash
   for the same in RAM after a boot-time read, or a read on each lookup.
   The tree lacks a kernel file loader so far; the read path is its cost.
3. Whether to build a loadable-overlay mechanism for rarely used drivers.
   It is a design with its own linker and relocation work, measured only
   by what it would move (the option table above bounds the driver side).

The P5 brief's userland section (the ten largest programs) stays open; the
multicall packing in `PROFILES.md` records what that work has given so far.
