# The MPU map: what the kernel programs and what proves it

The RP2040's Cortex-M0+ carries an eight-region Protected Memory System
Architecture MPU (RP2040 datasheet 2.4.1, 2.4.2.5, 2.4.6; ARMv6-M
Architecture Reference Manual B3.5). sys/arch/rp2040/rp2040/mpu.c programs
it once at startup and never again: one process is resident at a time and
every process image occupies the same 144 KB window at USER_DATA_START, so
the map is static and no context switch touches it. This document is the
authority for that map, the register values behind it, the privilege
transition it depends on, and the fault test that proves it.

## Registers

Datasheet 2.4.6 places the five registers after the System Control Block;
sys/arch/rp2040/include/mpu.h names them and their fields.

| register | offset | role |
| --- | --- | --- |
| MPU_TYPE | 0xed90 | DREGION[15:8] reads 8; SEPARATE reads 0 (unified) |
| MPU_CTRL | 0xed94 | ENABLE[0], HFNMIENA[1], PRIVDEFENA[2] |
| MPU_RNR | 0xed98 | REGION[3:0] selects the region RBAR and RASR address |
| MPU_RBAR | 0xed9c | ADDR[31:8]; VALID[4] with REGION[3:0] selects and sets RNR in one write |
| MPU_RASR | 0xeda0 | XN[28], AP[26:24], S/C/B[18:16], SRD[15:8], SIZE[5:1], ENABLE[0] |

A region is 2^(SIZE+1) bytes, SIZE at least 7 (256 bytes), at a base
aligned to its size. Where two enabled regions overlap the higher number
wins. With PRIVDEFENA set, a privileged access that no region covers uses
the default memory map; an unprivileged one faults. Every MPU fault on
ARMv6-M is a HardFault (datasheet 2.4.6.1); the architecture defines no
fault status or fault address register.

## The map

| region | base | size | RASR | unprivileged | holds |
| --- | --- | --- | --- | --- | --- |
| 0 | 0x00000000 | 16 KB | AP 110, XN 0, SIZE 13 | r-x | the boot ROM (datasheet 2.8), whose float routines libc calls through lib/libc/arm/gen/rom_float_resolver.S |
| 1 | 0x20000000 | 128 KB | AP 011, XN 0, SIZE 16 | rwx | the user window, low part |
| 2 | 0x20020000 | 16 KB | AP 011, XN 0, SIZE 13 | rwx | the user window, high part |
| 3 to 7 | | | RASR 0 | | disabled |

MPU_CTRL is written 0x5, PRIVDEFENA and ENABLE, and HFNMIENA stays clear so
the HardFault handler runs with the MPU off.

144 KB is not a power of two, so the window takes two regions; a static
assertion in mpu.c pins their sum to USER_DATA_SIZE and the low base to
its alignment. Text runs from the window because a.out images load into
SRAM, so XN stays clear. S, C and B stay zero: the RP2040 bus fabric
consumes none of the exported attributes (the XIP cache is an
address-range cache) and the Cortex-M0+ reorders nothing.

The kernel runs privileged and sees the default map as its background, so
kernel text in XIP flash, kernel data above the window, the peripherals at
0x40000000, SIO at 0xd0000000 and the PPB stay reachable to it and closed
to a process. A process may read, write and execute its own window, and
read and execute the boot ROM. Nothing else.

## Programming order and readback

mpu.c writes MPU_CTRL 0, then each region through RBAR with VALID set and
its RASR, then MPU_CTRL 0x5, with DSB after the region writes and DSB then
ISB after the enable (ARMv6-M ARM B3.5.4). Every write is read back: a
region counts as programmed only when RNR, RBAR and RASR return what was
written, and the enable is written only when all three regions do. A map
missing a user region would fault the first user instruction, so a core or
emulator that drops the writes runs with the MPU off and says so.

What was read back, not what was written, is what the kernel reports. The
console line after the `cpu:` banner reads

    mpu: 8 regions, 3 programmed, MPU_CTRL 0x5: rom 16K r-x, user 144K rwx

and `sysctl machdep.mpu` answers `enable`, `ctrl`, `nregions`, `separate`
and `programmed` from the same readback. The first four names are the
STM32 port's (sys/arch/stm32/include/mpuvar.h), so sbin/sysctl serves both
ports from one table; `programmed` is what a protection claim for the
window rests on.

## The privilege transition

locore0.S switches to the process stack after main() returns and branches
to icode at USER_DATA_START still privileged. icode (locore.S), which
init_main.c has copied into the window, sets CONTROL.nPRIV as its first
instructions, so the instruction after the CONTROL write is already
fetched from the window. The first form of the transition set nPRIV while
executing kernel flash, and under the map the ISB that followed it faulted
as the first unprivileged fetch; the Renode transcript of that fault
(process 1, pc 0x1000023c in setup_user_mode, EXC_RETURN 0xfffffffd) is
what moved the write into icode. Every process after exec inherits
unprivileged Thread mode from the exception return, as before.

## The fault test

usr.bin/mputest is the deliberate fault test. It reads `machdep.mpu`,
takes the kernel's claim as the state under test, and forks one child per
probe:

| probe | address | MPU on | MPU off |
| --- | --- | --- | --- |
| kernel RAM | USER_DATA_END, 0x20024000 | SIGSEGV | reads |
| kernel text | 0x10000100, the kernel's vector table | SIGSEGV | reads |
| SIO CPUID | 0xd0000000 | SIGSEGV | reads |
| boot ROM magic | 0x00000010 | reads | reads |
| window top | USER_DATA_END - 4 | reads | reads |

It also requires `nregions` to read 8 and `enable` to be set only with
`programmed` equal to 3, and ends with `MPUTEST OK (mpu on)` or
`MPUTEST OK (mpu off)` when every line agrees, so the same program on the
same image distinguishes a map the registers accepted from one they
dropped. Each closed probe under an enabled MPU is a real HardFault, and
fault.c prints its frame on the console before delivering SIGSEGV.

## Evidence

Renode 1.17.0 with the matgla/Renode_RP2040 models, `check-renode`
(tools/renode/boot.robot): the probe test asserts the `mpu:` line above;
the login test runs mputest and asserts `MPUTEST OK (mpu on)`. The
captured transcript of the login shell:

    $ sysctl machdep.mpu
    machdep.mpu.enable=1
    machdep.mpu.ctrl=0x5
    machdep.mpu.nregions=8
    machdep.mpu.separate=0
    machdep.mpu.programmed=3
    $ mputest
    machdep.mpu: enable 1, nregions 8, programmed 3, ctrl 0x5
    nregions                 0x00000000 ok (want 8, got 8)
    enable implies map       0x00000000 ok (want 1, got 1)
    fault: HardFault, exception 3
    ... r5: 0x20024000 ... pc: 0x200001bc ... lr: 0xfffffffd
    process 16 mputest
    kernel ram               0x20024000 ok (want 11, got 11)
    ... process 17 mputest, r5 0x10000100
    kernel text              0x10000100 ok (want 11, got 11)
    ... process 18 mputest, r5 0xd0000000
    sio cpuid                0xd0000000 ok (want 11, got 11)
    boot rom                 0x00000010 ok (want 0, got 0)
    window top               0x20023ffc ok (want 0, got 0)
    MPUTEST OK (mpu on)
    $ echo status $?
    status 0

Calibration against a known-bad kernel, one that wrote MPU_CTRL 0 after
programming the regions: the banner read `MPU_CTRL 0x0: protection off`,
`machdep.mpu.enable` read 0, every kernel address read from user mode, and
mputest ended `MPUTEST OK (mpu off)`, so neither line the gate asserts
appeared and the gate fails on that kernel.

Renode's Cortex-M0+ model implements the region registers through its NVIC
peripheral, so the emulator decides the register-level claim and the fault
path. A board run of mputest is the silicon evidence and is recorded here
when it is taken; the map, the readback and the test carry no
emulator-specific assumption.

Cost: 656 bytes of text and 16 of bss in the PICO kernel, 688 and 16 in
PICO_UART, and 7,988 bytes of root for mputest.
