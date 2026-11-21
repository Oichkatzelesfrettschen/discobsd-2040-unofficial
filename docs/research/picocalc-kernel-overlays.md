# Kernel overlays for the RP2040 kernels: a design

A kernel overlay keeps rarely run kernel code out of the kernel's flash
region and copies it into an SRAM buffer when a caller needs it. This note
designs that mechanism for the RP2040 port, prices it in both budgets the
port treats as binding (flash text and kernel RAM), and states when to
build it. The owner asked for the design in the kernel space survey's
choice 5 (`picocalc-kernel-space-survey.md`, 2026-11-21).

## What the measured work found

He, Debray and Andrews ("The Revenge of the Overlay: Automatic Compaction
of OS Kernel Code via On-Demand Code Loading", EMSOFT 2007; notes
repository `docs/papers/`) profile the Linux 2.4 kernel under MiBench and
find about 32 percent of its code executed. A binary rewriter clusters the
basic blocks by edge profile, keeps a core set and the hot code in memory,
moves the cold code to secondary storage, and rewrites every hot-to-cold
edge to call a loader. Main-memory code shrinks by about 53 percent with
little slowdown. Their kernel runs from RAM, so the saving is RAM.

The RP2040 kernels differ in where code runs. The kernel executes in place
from QSPI flash (`sys/arch/rp2040/conf/RP2040.ld`: `FLASH` at 0x10000100,
128 KiB less 256 bytes; `RP2040-sd.ld`: 160 KiB less 256 for the SD-root
kernels), and SRAM holds only kernel data (`RAM`, 58 KiB), the u-areas
(3 KiB each), `SCRATCH` (8 KiB in SRAM4 and SRAM5) and the 192 KiB user
window. Cold code already stays out of RAM. What it costs is room in the fixed
flash window, so an overlay here trades flash text for RAM, the reverse of
the paper's trade.

## Mechanism

1. **Unit.** An overlay is one rarely used subsystem compiled into its own
   input sections (`.ovl.<name>.text` and `.ovl.<name>.rodata`), chosen
   by configuration option, not by profile: the port lacks a binary
   rewriter, and GCC with GNU ld places code by section. Data and bss stay
   resident, so every overlay's state survives an eviction.
2. **Placement.** GNU ld's `OVERLAY` statement gives every overlay the
   same run address, a buffer in SRAM, and its own load address in a store
   outside the kernel window. A store in XIP flash (a slice of the root or
   swap region the layout gives up) loads with a word copy; a store on the
   SD card needs the SD driver resident and a sleep on the transfer.
3. **Entry.** Resident code calls an overlay only through a veneer per
   exported function: the veneer loads the overlay if the buffer holds
   another, raises the overlay's pin count, calls the function with `BLX`,
   and drops the pin count on return. Overlay code calls resident code
   directly, since resident addresses never move. A call from one overlay
   into another is a link error, enforced by a gate that reads the
   relocations of every `.ovl.*` section and fails on a target in a
   different overlay.
4. **Eviction.** A process can sleep inside an overlay (an SD read, a
   `tsleep` in a driver). Loading a different overlay waits until the
   resident one's pin count reaches zero, so no return lands in replaced
   code. Interrupt handlers, the fault and panic paths, the crash-dump
   writer and anything `lcdabandon()` reaches stay resident, because they
   run where a load cannot sleep.
5. **Verification.** A Unicorn check in the style of
   `tools/unicorn/check_panel_program.py` calls each veneer twice with
   another overlay loaded in between and compares the result with the
   resident build's; a mutant veneer that skips the load must fail it.

## Cost

The SRAM buffer is as large as the largest overlay. The rarely used options
in the survey's table measure 2,003 bytes (`LCD_PANELTEST`) down to a few
hundred, so a useful buffer is 2 to 4 KiB, plus one veneer per exported
function and a few words of state (an estimate of a dozen bytes per veneer;
none is written). That RAM
comes from the 58 KiB kernel data region, which the port treats as tight as
the flash text (owner ruling 2026-11-16), or from `SCRATCH`, whose 8 KiB
already has tenants that this design would have to displace. The flash
saved is the overlays' text less the veneers and a loader of a few hundred
bytes (also an estimate). The kernel already runs the static form of this
mechanism: the `.ramfunc` sections, the flash-programming routines of
`dev/flash.c` and the PSRAM transfer of `dev/psram_xfer.c`, load from
flash and run from RAM (1,000 bytes in the PICOCALC map the survey read).

## When to build it

Each cheaper route comes first:

1. Move a rarely used option to the SD-root kernel, as the 2026-11-21 move
   of `SYSTRACE` and `LCD_PANELTEST` did (PICOCALC headroom 3,192 to 6,776
   bytes).
2. Grow the SD-root kernels' flash reservation, a layout constant: their
   root lives on the card, so the flash window can widen without touching
   the root, though it shrinks the flash filesystem region beside it;
   PICOCALC_SD holds 33,400 bytes of headroom.
3. Move data out (the errno message file of the survey's choice 2).

The overlay mechanism earns its RAM when a flash-root kernel (PICO,
PICOCALC, PICOCALC_LEAN) exceeds its 128 KiB window after those routes are
spent, with a subsystem that runs only from process context. The falsifier
for building it now is the headroom table: every flash-root kernel keeps at
least 6,776 bytes free, 5,752 above the 1,024-byte margin
(`check-kernel-headroom`, 2026-11-21), so every flash-root kernel still fits
without it.
