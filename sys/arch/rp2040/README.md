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
inserts long-branch veneers, `__flash_erase_sector_veneer` and
`__flash_program_page_veneer`, and places them in flash, because a call from
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
one. Every region is larger than the STM32F407XE reference target: flash by
four times, kernel RAM by two, user RAM by a third.

## State

The kernel links. `tools/config` knows the architecture, `make` in
`compile/PICO` runs to completion, and the image places as intended:

| Region | Used | Available |
|---|---|---|
| Kernel flash | 84,867 | 524,032 |
| Kernel RAM | 23,792 | 122,880 |

`.text` lands at 0x10000100, immediately above the 256 bytes the boot ROM
reserves for the second stage, with the vector table at the image base.
`.data` loads from flash and lives at 0x20000000, and the three flash-writing
functions sit inside it in SRAM. The filesystem region and the 128K of user
RAM are untouched by the kernel.

Still missing before it can boot: a boot2 stage, so the image is loadable at
all; an SD driver, if anyone wants a second disk; and a filesystem written
into the flash region for root to be found on. The clock bring-up, the PL011
console, and the flash block device are written but have never executed. A
kernel that links is not a kernel that boots.
