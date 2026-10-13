# DiscoBSD/rp2040 - 2.11BSD-based OS for the Raspberry Pi Pico

## Supported hardware

 * [Raspberry Pi Pico][1], the original RP2040 board with a 2 MB Winbond
   W25Q16JV flash and no wireless module.

[1]: https://www.raspberrypi.com/products/raspberry-pi-pico/

## Images

Two UF2 files make a bootable board, both produced by the build:

| File | Contents | Flash address |
|---|---|---|
| `sys/arch/rp2040/compile/PICO/unix.uf2` | second stage and kernel | 0x10000000 |
| `distrib/rp2040/flash.uf2` | 1536 KiB Dhara root region | 0x10020000 |

The root lives on the flash above the kernel behind the Dhara translation
layer, so `flash.uf2` is not the filesystem image itself: `sdcard.img` is
the filesystem, and `tools/flashimg` writes it into the on-flash form the
kernel reads. `Makefile.inc` builds a 988 KiB filesystem inside the
989 KiB logical capacity. The 384 KiB raw swap region starts at 0x101a0000
and stays outside both UF2 files. Loading only `flash.uf2` replaces the
root filesystem; loading `unix.uf2` replaces the kernel.

## Building

    : "${PYTHON:?set PYTHON to the intended interpreter}"
    export PYTHON
    bmake MACHINE=rp2040 distribution
    bmake MACHINE=rp2040 flash

The build needs `bmake`, `arm-none-eabi-gcc`, `picotool` for the UF2
conversion, and `groff` or `mandoc` for the manual pages. The tree's
termcap and awk build scripts run under `ex`, or under `vim -es` where no
`ex` is installed.

## Flashing

Hold BOOTSEL while plugging the board in; it enumerates as `RPI-RP2`. Then:

    picotool load distrib/rp2040/flash.uf2
    picotool load sys/arch/rp2040/compile/PICO/unix.uf2
    picotool reboot
    sh distrib/rp2040/host/discobsd-connect

Each `load` programs only the sectors its file covers, so the two do not
disturb each other; the ROM's mass-storage loader accepts the same files by
copy. `picotool info -a` afterwards reports the kernel's binary information
as `none`, which is expected: the kernel carries no SDK metadata block.

## Console

The console is the same USB cable. After `picotool reboot` the board
re-enumerates as a CDC-ACM device, vendor 2e8a product 000a, and Linux
provides a `/dev/serial/by-id/*DiscoBSD*` identity. Resolve that identity
instead of assuming a ttyACM number:

    sh distrib/rp2040/host/discobsd-connect

The kernel keeps the last 8 KiB of output until a terminal opens the port,
so the boot messages are there to read. Holding BOOTSEL through a reset
with a working kernel selects single-user mode. The board carries the Pico
SDK's reset interface, so `picotool reboot -u` returns it to BOOTSEL from a
running kernel; the button held through a power cycle does the same. UART0
on GP0 and GP1 at 115200 baud is `/dev/tty0` for a serial adapter. The LED
on GP25 lights for kernel, disk, and swap activity.
