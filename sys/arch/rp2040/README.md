# DiscoBSD on the RP2040

A Raspberry Pi Pico target: dual Cortex-M0+ implementing ARMv6-M, 264 KB SRAM,
2 MB external QSPI flash, no MMU and no MPU.

## Licensing

This tree carries no Apache-2.0 code and needs none.

ARM relicensed CMSIS from 3-clause BSD to Apache-2.0, which is why
`sys/arch/arm/include/core_cm4.h` here is the 2009-2015 BSD-licensed edition
and no `core_cm0plus.h` accompanies it. Apache-2.0 and BSD-3-Clause are
compatible in one direction: Apache files may sit inside a BSD-licensed
project, but their NOTICE must be kept, changes must be stated, and they
cannot be relicensed as BSD. The result is then "BSD except these files,"
which is a real cost for a tree whose whole point is self-contained BSD
sources.

That cost is avoidable. `include/intr.h` needs exactly four CMSIS intrinsics,
and each is one instruction, so they are written here as inline assembly.
Nothing else in this port reaches for CMSIS.

For the hardware layer, the Raspberry Pi Pico SDK is the source to draw on,
because it is BSD-3-Clause, the same license as this tree. Of its files under
`src/`, 666 are BSD-3-Clause and 27 are Apache-2.0, and every one of the 27
sits under `cmsis/stub/`. Every RP2040 driver, the register and struct
headers, and the boot2 stages are BSD-3-Clause.

Two sources are deliberately not used:

- **FUZIX** is GPL-2.0. Importing any of its driver code would relicense this
  kernel as GPL-2.0, and GPL-2.0 is separately incompatible with Apache-2.0.
  Nothing is lost by declining it, because its RP2040 drivers are thin
  wrappers over the Pico SDK, so the hardware knowledge is available directly
  under BSD.
- **Apache NuttX** is Apache-2.0. Legally combinable, but it brings the notice
  and change-statement obligations described above for no gain the Pico SDK
  does not already provide under BSD.

## Root filesystem, and what was taken from where

DiscoBSD's other targets boot from an SD card, and its device tree is mostly
SD and SDIO. This board has no socket, so `dev/flash.c` makes the onboard
2 MB QSPI flash a block device and the board needs no extra hardware.

The idea of putting a Unix root on a Pico's flash behind a wear-leveling
translation layer is FUZIX's, and the credit is theirs. None of their code is
here, and their implementation was deliberately not read.

That is not a courtesy, it is the license. The FUZIX kernel is GPL-2.0.
Copying any of it into this tree would relicense the whole kernel as GPL-2.0,
and no SPDX header or credit line changes that, because attribution is not a
license grant. GPL-2.0 is separately incompatible with Apache-2.0, so taking
FUZIX code would also foreclose ever using NuttX.

Neither rewriting nor crediting was necessary, because the part worth having
is not FUZIX's. Both systems need the same published library, Dhara, which is
ISC licensed, the same license as this tree's own headers. It is vendored
verbatim under `dhara/`, pinned at upstream commit 1b166e41b74b, with its
copyright intact and its sources byte-identical to upstream so it can be
re-fetched without reapplying edits. Dhara names three standard headers that a
`-nostdinc` kernel does not have, so `dhara/compat/` supplies only the types
and two functions it uses, leaving the vendored files untouched.

The layering, and the license at each level:

    bdevsw              block requests           this tree, ISC
      dhara_map_*       wear leveling, mapping  Dhara, ISC
        dhara_nand_*    NOR geometry             dev/flash.c, ISC
          bootrom       erase and program        silicon

Nothing links against the Pico SDK. Its flash routines are thin wrappers over
boot ROM entry points found through a published table, so `dev/flash.c` looks
them up itself and the kernel stays self-contained.

Dhara is written for NAND and this part is NOR, which costs nothing. Its
contract wants pages programmed sequentially within an eraseblock and never
reprogrammed, which NOR satisfies. NOR has no factory bad blocks and no ECC,
so the bad-block callbacks are honest constants rather than unfinished stubs.

The constraint that shapes the driver: erasing or programming flash takes the
QSPI interface out of execute-in-place, so code driving one cannot be fetched
from flash while it runs. Those functions carry `__ramfunc`, which
`conf/kern.ldscript` places inside `.data` so the existing copy loop in
`locore0.S` carries them to RAM with no second mechanism. Verified in the
object file: the only direct call out of `.ramfunc` is to another `.ramfunc`
function, every other call is indirect into ROM, and interrupt masking is
inlined inside the RAM functions rather than called into flash.

`conf/RP2040.ld` splits flash 512K for the kernel and 1536K for the
filesystem, so the kernel cannot grow into the root.

One detail survives linking and looks alarming until traced. The linker
inserts long-branch veneers, `__flash_erase_block_veneer` and
`__flash_program_unit_veneer`, and places them in flash, because a call from
flash to RAM exceeds a short branch. They are safe: a veneer runs on the way
in, while XIP is still up, and merely jumps to the RAM function, which takes
XIP down only after it is already executing from RAM. The return path is a
plain `bx lr` back into flash after XIP has been restored. What would be
unsafe is a veneer on a call made from inside the XIP-down window, and there
is none, because those functions call only the boot ROM and each other.

## What ARMv6-M changes

ARMv6-M is not a subset of Cortex-M4 that merely runs slower. Six encodings in
`locore0.S` do not exist, and one instruction is worse than missing.

`msr BASEPRI, r0` assembles cleanly for `cortex-m0plus` and encodes SYSm 17, a
system register ARMv6-M does not define. The assembler never complains, so the
Cortex-M4 original builds for this target and then writes an unimplemented
register at run time. It is deleted here rather than translated, because
`cpsie i` already clears PRIMASK, which is the only mask ARMv6-M has.

The upstream `.cpu cortex-m4` directive is what hides this. A `.cpu` directive
overrides `-mcpu` on the command line, so a build that looks correctly
targeted is not. This tree names `cortex-m0plus` explicitly so the assembler
cannot misreport the target.

Because PRIMASK masks everything or nothing, every blocking `spl` collapses to
a global disable. The IPL constants remain, since the machine-independent
kernel names them and interrupt priorities still order preemption among
enabled interrupts.

`machparam.h` keeps the macro named `BASEPRI`, which `sys/kern/kern_clock.c`
calls, and tests PRIMASK underneath. Its user-space window moves to
0x20020000 with 128 KB, matching `conf/RP2040.ld`, because the STM32 constants
name an address that is flash on this chip.

Seven IPL levels do not fit two priority bits, and the inherited `IPLTOREG`
does not survive the reduction. It forms a byte as `(IPL_TOP - ipl) <<
IPL_BITS`, which holds at four priority bits. At two, `IPL_BITS` is 6, the
shift overflows for every level below IPL_TTY, and masking wraps the result to
0, 128, 64, 0, 192, 128, 64: IPL_SOFTCLOCK collides with IPL_CLOCK and the
ordering inverts. The replacement distributes the seven levels across the four
priorities the hardware has, non-increasing as urgency rises. Nothing is lost,
because priorities never implement spl here; they only order preemption among
interrupts already enabled.

`fault.h` shrinks from 131 lines to 48. ARMv7-M describes four fault
exceptions through HFSR and CFSR with MMFAR and BFAR for the address. ARMv6-M
raises only HardFault and defines no fault status or fault address register at
all, so the decode has nothing to read. That is a capability loss rather than
a translation, and the exception stack frame is what remains.

`mpuvar.h` is dropped. Neither core has an MPU. DiscoBSD does not isolate
processes on any target today, so this port inherits the trust model rather
than replacing one, but on this chip the door stays shut: no MPU-based scheme
can be added later.

## Memory

`conf/RP2040.ld` claims the 256 KB striped SRAM region and leaves the two
4 KB banks above it alone, because the boot ROM stages boot2 in the topmost
one. User space sits at the bottom, at 0x20000000, because
`lib/elf32-arm.ld` links every userland executable there and
`USER_DATA_START` names the same address on every Arm target; it is 96 KB,
the STM32 figure, so a process image swaps in the same size. The kernel's
data, the two u areas, and the stack occupy the 160 KB above it.

## Boot

The boot ROM reads 256 bytes from flash offset 0, checks a CRC32 trailer,
and runs them from SRAM to bring the QSPI interface up for execute-in-place;
datasheet section 2.8.1.3. `boot2/boot2_w25q080.S` is the Pico SDK's stage
for the W25Q family with its helper includes folded in and the register
constants written out, so it assembles with the kernel's include path and
under the kernel's BSD-3-Clause. It is linked on its own at the SRAM address
the ROM copies it to, reduced to raw bytes, padded and checksummed by
`tools/boot2sum`, and assembled into the `.boot2` section that
`conf/kern.ldscript` places at 0x10000000. The stage assembles
byte-identical to the SDK's `bs2_default.bin` for `PICO_BOARD=pico`, and
`boot2sum` produces the same trailer as the SDK's `pad_checksum` on the same
input; both were checked rather than assumed.

The kernel image is therefore complete from flash offset 0: boot2, then the
vector table at 0x10000100 where boot2's exit routine loads the stack
pointer and reset vector from. The build converts it to `unix.uf2` when
`picotool` is installed.

Before the kernel runs C, `startup()` starts the crystal, runs the reference
clock from it, and enables the watchdog's microsecond tick, because the
timer that `mdelay` reads does not count until that tick runs, datasheet
section 4.6.4. The timer and SYSINFO blocks are released from reset with the
GPIO and pad blocks. `clock.c` arms SysTick from the processor clock for
`HZ` interrupts a second; nothing in the STM32 tree's HAL is left to do it.

## Console over the USB cable

The Pico has one cable, so the console is a CDC-ACM device on the RP2040's
own USB controller, `dev/usb.c`, written against the device controller
model in datasheet section 4.1.2: control and buffer words in DPSRAM, one
buffer per endpoint, an interrupt per completed buffer, the setup packet at
DPSRAM offset 0, the device address written only after the status stage of
SET_ADDRESS has gone out, and AVAILABLE set after the rest of a buffer
control word with a few cycles between (4.1.2.7.1). A Linux host binds it
as `/dev/ttyACM0` with no driver of its own. UART0 remains `/dev/tty0`.

The device also carries the Pico SDK's vendor reset interface, class ff,
subclass 00, protocol 01, on interface 2, and answers its two requests:
BOOTSEL through the boot ROM's USB-boot entry, and a flash reboot through
AIRCR. `picotool reboot -u` therefore returns the board to the ROM loader
with no hand on the button, which matters on a board whose only other route
back is a power cycle with BOOTSEL held.

Output goes through a 4 KB ring rather than straight to the endpoint, since
the host drains the IN endpoint only while a terminal holds the port open,
and the kernel prints long before one does. A terminal opened after boot
sees the tail of the boot messages. The ring only stalls the kernel when a
host is present, has asserted DTR, and is slow, and then for a bounded spin.

The compiled descriptors were walked byte by byte: the device descriptor is
18 bytes with the Raspberry Pi vendor and Pico CDC product identifiers, and
the configuration is 84 bytes whose interface association, three
interfaces, four functional descriptors, and three endpoints sum to its
wTotalLength. The driver has not enumerated against a host. RP2040-E5, the
enumeration erratum needing 800 us of forced idle after bus reset, applies
to silicon B0 and B1 and is fixed in the B2 this board carries, so the
driver does not implement the workaround.

The BOOTSEL button is the flash chip select, so `bootsel_pressed()` in
`machdep.c` floats that pad for a moment and samples the level through SIO,
from RAM with interrupts masked, because flash is unreachable meanwhile.
Holding it at boot enters single-user mode. The disassembly of the routine
contains no branch-and-link, which is the property the RAM placement needs.

## Root filesystem layout

`dev/flash.c` presents the flash region as `fl0` with the SD driver's minor
numbering: partition in the low three bits, unit above, and a PC partition
table in the first 512 bytes of logical block 0 as `tools/fsutil
--repartition` writes it. `fl0a` is root and `fl0b` swap.

Dhara's geometry is chosen above the chip's. A 256-byte page carries one
132-byte metadata entry per checkpoint page, so half of the flash would go
to checkpoints. The unit is 1024 bytes, DEV_BSIZE, and the erase block two
chip sectors, which gives seven data pages in eight and keeps Dhara's safety
margin at 64 KB; `flash.h` records the arithmetic and `tools/flashimg -c`
prints the outcome, 989 KB of logical blocks from the 1536 KB region.

Because the bytes in flash are Dhara's journal rather than the filesystem,
a disk image from `fsutil` cannot be programmed as it is. `tools/flashimg`
runs the same vendored Dhara sources against a memory model of the region,
writes the disk image through `dhara_map_write`, and emits the region for
programming at 0x10080000. A separate Dhara instance resumed the emitted
image and read all 796 sectors back identical to the disk image.

## Userland

`share/mk/sys.mk` selects `-mcpu=cortex-m0plus` for this machine, because a
Cortex-M4 userland uses Thumb-2 encodings the M0+ faults on. Every
assembler source under `lib/libc/arm` and `lib/startup-arm` assembles for
ARMv6-M unchanged; `strcmp.S` already carries a Thumb-1 path. The C
standard is pinned to gnu17 on the compiler line because several Makefiles
replace CFLAGS outright, and GCC 15 and later default to C23, which rejects
the tree's empty parameter lists and old-style definitions. `distrib/rp2040`
holds a manifest for a 795 KB root: init, getty, login, sh, what `rc` runs,
and a working set of `/bin`, with an `rc` that skips the motd rewrite and
cron.

## State

Kernel, root filesystem, and both flash images build from clean:

| Image | Bytes | Flash address |
|---|---|---|
| `compile/PICO/unix.uf2`, boot2 and kernel | 90,901 | 0x10000000 |
| `distrib/rp2040/flash.uf2`, root filesystem | 1,572,864 | 0x10020000 |

Swap is not in either image: `dev/flash.h` FLASH_SWAP_BYTES reserves 384 KB
at the top of the chip as a second flash unit, `fl1`, that the kernel
erases and programs in place at run time.

| Region | Used | Available |
|---|---|---|
| Kernel flash | 90,901 | 39,915 |
| Kernel RAM | 39,608 | 118,088 |
| Root filesystem | 838 KB | 133 KB |
| Swap | 0 | 384 KB |

The board boots to a login prompt in about 9 seconds over its USB console,
a CDC-ACM device (VID 2e8a, PID 000a) that Linux binds as `/dev/ttyACM0`;
`picotool reboot -u -f` returns the board to BOOTSEL from the running
kernel. `uname` reports `DiscoBSD pico 2.7 PICO#1 rp2040`. Verified on the
board: `sh` running scripts, pipes, and background jobs; `awk` with
floating point; `sed`, `sort`, `find`, `ed`, `ps`, `df`, `mount`, and
`stty`; `picoc` with its 8 KB arena; and `smlrc` at `/usr/libexec/smlrc`
emitting Thumb-1 assembly identical to the host build. Not yet on the
board: the Thumb-1 assembler and linker (`usr.bin/as`, `usr.bin/ld`, 32 KB
and 23 KB, in the tree but not in the manifest), the board link library,
and the POSIX tools box; UART0 as `/dev/tty0` has not been exercised.

## Documentation

`sys/arch/rp2040/doc/BOOT-MAP.md` is the boot procedure, memory map, build and flash steps, triage ladder and debugging notes; `STORAGE.md` the flash layout and root contents; `research/` the design reports (USB console, editors, compilers, compression, POSIX tools, static analysis).
