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

`rp2040/locore0.S` assembles clean for `cortex-m0plus`, and `include/intr.h`
compiles clean under `-Wall -Wextra`, generating PRIMASK and NVIC accesses
with no BASEPRI. That is reset, stack, BSS, data copy, the user-mode switch,
the vector table, and interrupt masking.

Not yet written: `SystemInit` and clock bring-up, a boot2 stage, UART, an
SPI-attached SD card, `fault.c` reduced to the single ARMv6-M fault, the
kernel configuration files, and the build glue. A kernel that assembles is not
a kernel that boots.
