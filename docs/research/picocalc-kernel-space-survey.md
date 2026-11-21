# PicoCalc kernel space survey at 4eb0f4eb3

The owner asked what in the kernel image could move to the SD card and
which packing or code techniques remain (P5 brief, 2026-11-17). This record
measures the candidates on main at 4eb0f4eb3 with arm-none-eabi-gcc
16.2.0. The numbers come from the linked `unix` image of that build
(`size`, `bloaty`, `pahole`, `tools/check_kernel_headroom.py`), except
the link-map and `readelf` figures, read from PICOCALC at f780f6796 with
the same compiler, whose text is the same 127,728 bytes, and the figures
marked as quoted from `memory-devices.md`, which that record measured at
4b3eaf6c with GCC 14.2.1. PICOCALC is the subject because its
flash budget is the tight one: `tools/check_kernel_headroom.py` measures its
flash image at 127,624 of 130,816 bytes, 3,192 bytes of headroom and 2,168
above the 1,024-byte margin.

## Where the text is

Each object's linked flash contribution: the `.text` and `.rodata` input
sections that `sys/arch/rp2040/compile/PICOCALC/unix.map` places in flash,
summed per object, so the sections `--gc-sections` discarded stay out
(126,069 bytes in all, of 127,728 bytes of text; the rest is the vector
table, the boot stage and the exception index). `bloaty`'s compile-unit
view adds RAM, so it puts `swapram.c` first on the strength of its 16 KiB
RAM pool, whose code is 2,635 bytes.

| Object | Linked flash bytes |
| --- | --- |
| `tty.o` | 5,869 |
| `ufs_syscalls.o` | 5,842 |
| `exec_conf.o` | 5,238 |
| `ufs_namei.o` | 4,182 |
| `lcdterm.o` | 4,140 |
| `usb.o` | 3,836 |
| `kern_sysctl.o` | 3,322 |
| `st7365p.o` | 3,184 |
| `sys_inode.o` | 3,000 |
| `sd.o` | 2,900 |

The largest functions (the FUNC symbols of
`arm-none-eabi-readelf -sW sys/arch/rp2040/compile/PICOCALC/unix.elf`,
sorted by size) are `namei_at` (2,992 bytes), `lcdterm_putc`
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

Removing `-DCAPACITY_STATS` alone fails to compile: the Makefile still
builds `subr_capacity.o`, whose function definitions `capacity.h` then
turns into stub macros. Measuring it takes `subr_capacity.o` out of
`OBJS` as well (`docs/research/kernel-footprint-handoff.md`); this
survey leaves that row unmeasured. Removing `LCD_PANELTEST` and `SYSTRACE` together in one build measures 123,944 bytes of
text, 3,784 fewer than the baseline (the isolated rows overlap by 8 bytes),
and `tools/check_kernel_headroom.py` reports a flash image of 124,040 bytes,
3,584 fewer, which leaves 6,776 bytes of headroom against today's 3,192.

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

1. Which diagnostic options PICOCALC keeps. Dropping `LCD_PANELTEST` and
   `SYSTRACE` frees 3,584 bytes of PICOCALC's flash image (measured
   together). The panel test needs
   `DEV_LCDGFX`, which PICOCALC_LEAN_DIAG omits, so it stays in PICOCALC_SD
   and PICOCALC_SD_DIAG alone; `SYSTRACE` can also go to
   PICOCALC_LEAN_DIAG. The panel test's move has a gate cost:
   `check-panel-programs`, in the PR job, calls `st7365p_set` in the
   PICOCALC ELF, so it would read the PICOCALC_SD ELF instead, and the PR
   job would link that kernel first. The generated-configuration gate
   moves with it: `check_variant_delta()` in
   `tools/config/check_generated_sync.py` holds PICOCALC_SD to PICOCALC plus
   its listed additions and refuses a PICOCALC_LEAN removal its base lacks,
   so the move takes four rule edits: PICOCALC_SD's additions gain
   `LCD_PANELTEST`, `SD_DIAG_OPTIONS` (PICOCALC_SD_DIAG's additions) gains
   `SYSTRACE`, PICOCALC_LEAN's removals drop both options (its base no
   longer carries them), and PICOCALC_LEAN_DIAG keeps adding
   `DIAGNOSTIC_OPTIONS`, which brings `SYSTRACE` back there; the affected
   Makefiles are then regenerated.
2. Whether the errno message table (1,390 bytes with its rules and
   offsets) leaves the kernel for a message file on the root, the design
   `memory-devices.md` records: libc `strerror()` reads the file from the
   running kernel's root, `fl0` on PICOCALC and PICOCALC_LEAN, where it
   is present with the card out, and `sd0a` on PICOCALC_SD and
   PICOCALC_SD_DIAG, which reach user space only with the card in, so the
   file goes into each root image; it falls back to "Error N"; the kernel keeps zero bytes of RAM for it.
   Its cost is the file format, the build step that writes it and the
   libc reader. A copy read into RAM at boot would spend the same 1,298
   bytes of RAM the flash saves, which this survey sets aside.
3. Whether the kernel symbol table behind `knlist()` (`nlist` 288 bytes
   and `sysctl_nlist` 146 by `readelf` at f780f6796, plus 259 bytes of
   names quoted from `memory-devices.md`) moves to a symbol file
   the build writes and checks against the kernel's version string, as
   `memory-devices.md` records it.
4. Whether the PicoCalc kernels drop ELF execution (`exec_elf_check` and
   its strings: 500 bytes by `readelf` at f780f6796, plus about 30 bytes of
   strings quoted from `memory-devices.md`), a removal
   from the PicoCalc `execsw` once a check confirms that every shipped
   program is a.out.
5. Whether to build a loadable-overlay mechanism for rarely used drivers.
   It is a design with its own linker and relocation work, measured only
   by what it would move (the option table above bounds the driver side).

The P5 brief's userland section (the ten largest programs) stays open; the
multicall packing in `PROFILES.md` records what that work has given so far.
