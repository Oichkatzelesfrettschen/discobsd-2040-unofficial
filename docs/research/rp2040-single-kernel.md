# One RP2040 kernel for every board: what the four configurations reduce to

The RP2040 port builds four kernels from `sys/arch/rp2040/compile/*/Config`:
PICO (bare Pico, USB console), PICO_UART (bare Pico, UART0 console),
PICOCALC (Pico on the ClockworkPi PicoCalc carrier, panel and keyboard console)
and PICOCALC_PSRAM (PICOCALC plus the carrier's ESP-PSRAM64H as swap). This
note tests the claim that the four reduce to one kernel binary whose board
granularity is data written by the build and flash infrastructure, and records
which compile-time differences survive the test.

## Measured inputs

Section sizes come from `arm-none-eabi-size -A` on the four `unix.elf` built
from branch `kern/user-window-relocation` at `458cc01a` (SwapRAM pool placed
after kernel bss) with the 192 KiB user window of `swap-exhaustion/review-04.md`.
The kernel flash budget is `FLASH` in `conf/RP2040.ld`, 128 KiB less the
256-byte boot2, 130,816 bytes. Kernel RAM under the 192 KiB window is
`0x20030000..0x2003e800`, 59,392 bytes; the 4,096-byte exchange bounce of
`swap-exhaustion/plan.md` is reserved in it.

| Kernel | `.text` | `.data` | `.bss` | pool | kernel RAM used with bounce | RAM margin |
|---|---|---|---|---|---|---|
| PICO | 94,212 | 256 | 15,256 | 16,384 | 35,992 | 23,400 |
| PICO_UART | 85,343 | 200 | 14,832 | none | 19,128 | 40,264 |
| PICOCALC | 111,784 | 376 | 28,496 | 16,384 | 49,352 | 10,040 |
| PICOCALC_PSRAM | 113,498 | 424 | 28,640 | 16,384 | 49,544 | 9,848 |

The PicoCalc-only kernel RAM is the panel's text and glyph state
(`atlas` 4,096, `lcd_render_stack` 2,048, `cells` 1,696, `attrs` 1,696,
`glyph_bank` 1,152: 10,688 bytes) and the second `mount` entry that
`NMOUNT=2` adds for the SD card (the two-entry table is 2,440 bytes). The USB
transmit ring (`usb_tx_ring`, 8,192 bytes) lives in the SCRATCH banks, outside
kernel RAM, in every configuration that carries USB.

## The differences, one at a time

**PSRAM presence is already a runtime decision.** `psramprobe()` in
`dev/psram.c` reads the chip identity, keeps swap on `fl1` when the identity
or initialization fails, and redirects `swapdev` to PSRAM only on success. A
PicoCalc without the PSRAM chip therefore boots the PICOCALC_PSRAM kernel and
swaps to flash, which is exactly what the PICOCALC kernel does. The PSRAM
driver costs 1,714 bytes of text and 144 bytes of bss. The carrier fixes
GP2-GP5, GP20 and GP21 to the PSRAM footprint, so the probe drives no pin that
the carrier gives another function. Verdict: PICOCALC is subsumed by
PICOCALC_PSRAM; the split carries no information.

**The PicoCalc devices need a presence decision before any pin is driven.**
`lcdprobe()` in `dev/lcdcons.c` initializes the ST7365P on SPI1 (GP10-GP15)
and returns 1 unconditionally; `sdprobe()` takes SPI0 (GP16-GP19, GP22). A
bare Pico wired to other hardware on those pins would see them driven, so
the union kernel must know the board before these probes run. Two
discriminants exist. The keyboard controller answering at I2C1 address `0x1F`
(GP6/GP7, open drain, `picokbd_version()`) is a read that drives no push-pull
pin; its falsifier is the carrier's keyboard microcontroller still booting at
kernel probe time, a case `lcdprobe()` already prints ("not answering"). A
board descriptor written into flash by the image builder names the board
without timing risk and lets the probe confirm it. The descriptor puts the
board choice where the build infrastructure already selects partitions and
images, which is the home the claim proposes.

**The UART console kernel exists because Renode models no USB controller.**
`docs/research/emulation.md` (USB "none") and `tools/renode/check-boot.sh`
boot PICO_UART for that reason. `CONS_MAJOR` is a compile-time constant read
in `sysctl.c`, `uart.c`, `usb.c` and `lcdcons.c`; a console major chosen at
boot from the descriptor replaces it. The falsifier is the USB driver's
initialization stalling on the unmodeled controller in Renode; one
`check-renode` boot of the union kernel with a UART console descriptor
decides it. PICO_UART also omits SwapRAM, so today's emulator gate exercises
no SwapRAM path; the union kernel gives Renode the pool, the heatshrink codec
and the reservation paths of `kern/fork-swap-reservation`.

**`SINGLE_UFS_ROOT` cannot survive in a kernel that mounts the SD card.**
`sys/sys/inode.h` maps `INODE_DEVICE()` and `INODE_FILESYSTEM()` to
`mount[0]` under that option and drops the per-inode device identity. The
union kernel takes `NMOUNT=2` and the per-inode fields, a cost of one mount
entry (1,220 bytes) and the inode device field on a bare Pico.

**`SYSTRACE` is eight bytes of bss** (`systrace`, `systrace_pid`) and joins
the union unconditionally.

## The union kernel's budget

The union is PICOCALC_PSRAM plus `SYSTRACE`, the PICO USB console default
and a runtime console major. Its text is about 113.5 KB of 130,816 bytes,
leaving about 17 KB of kernel flash. Its kernel RAM need equals
PICOCALC_PSRAM's, 49,544 bytes with the bounce, leaving 9,848 bytes under the
192 KiB window. The window choice in `swap-exhaustion/review-04.md` already
budgets PICOCALC_PSRAM as the worst case, so the union adds no RAM requirement
the window does not already carry.

## What survives as build-time granularity

1. The memory layout: the user window, `RAM`, `U0AREA`, `UAREA` and `SCRATCH`
   in `conf/RP2040.ld`, the MPU region map in `rp2040/mpu.c` and
   `USER_DATA_SIZE`. One kernel has one window, and the window is fixed by the
   worst-case board's kernel RAM. A bare Pico pays for the PicoCalc's 11,908
   bytes of panel and mount state in kernel-RAM margin, which matters only
   when a future window larger than 192 KiB would fit PICO's 35,992 bytes and
   not the union's 49,544. Moving the panel state from bss to a boot-time
   claim on the gap after `_ebss`, taken only when the board descriptor names
   a PicoCalc, returns those bytes to a bare Pico's margin.
2. Capacity constants the layout implies: `SWAPRAM_KB`, `COMPACT_SWAPMAP`
   (384 flash swap blocks and the PSRAM block count both fit 16 bits,
   `machdep.c` static assertions), `NFL`, `CPU_KHZ`, `BUS_KHZ`. These are the
   same in all four configurations and stay as one set of options.
3. The board descriptor itself, written by the flash-image tooling under
   `distrib/rp2040`, one image per board from one kernel binary.

## Gates the union needs before it replaces the four kernels

1. A descriptor parser host test calibrated on a valid descriptor, a
   truncated one, an unknown board and a missing one (missing selects bare
   Pico with the USB console and probes nothing on the PicoCalc pins).
2. `check-renode` booting the union kernel with a UART console descriptor
   (the USB-stall falsifier above).
3. A board run on the bare Pico showing no PicoCalc probe output and the USB
   console, and on the PicoCalc showing the panel console, the keyboard
   version and either PSRAM swap or the `fl1` fallback line.
4. `check-kernel-headroom` against the union's numbers, replacing the four
   per-configuration rows.

## Uncertainty

The flash and RAM numbers are measured on the revision named above and move
with every kernel change; the 17 KB flash margin is the figure most likely to
shrink, and the exchange code of `swap-exhaustion/plan.md` lands in it. The
keyboard-presence discriminant is untested against a cold PicoCalc boot; a
board capture of `picokbd_version()` at probe time across power cycles raises
or lowers it. The Renode USB behavior is unknown until the gate in item 2
runs.
