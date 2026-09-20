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
| 3 | 0xd0000000 | 256 B | AP 011, XN 1, SIZE 7, SRD 0xf7 | rw- | SIO offsets 0x060 to 0x07f, the hardware divider |
| 4 to 7 | | | RASR 0 | | disabled |

MPU_CTRL is written 0x5, PRIVDEFENA and ENABLE, and HFNMIENA stays clear so
the HardFault handler runs with the MPU off.

## Why the divider is in the map

The boot ROM's float division runs in the calling process's context and
drives the SIO hardware divider: `mufp_fdiv`, and every transcendental that
branches into `fdiv_n`, writes DIV_UDIVIDEND and DIV_UDIVISOR and reads
DIV_QUOTIENT (doc/research/float-libs.md section 4.1, from
pico-bootrom-rp2040's `mufplib.S`; datasheet 2.3.1.5 places those registers
at SIO offsets 0x060 to 0x078). The first map closed SIO to unprivileged
code, and the board answered: `fptest` died of SIGSEGV with pc 0x00002cec,
r0 0x40f00000 and r1 0x40200000, and the boot ROM image disassembles there
as

    00002cda <fdiv_n>:
        2ce8:  movs r5, #208    @ 0xd0
        2cea:  lsls r5, r5, #24        ; r5 = 0xd0000000
        2cec:  str  r6, [r5, #96]      @ 0x60, DIV_UDIVIDEND
        2cee:  str  r3, [r5, #100]     @ 0x64, DIV_UDIVISOR

so the faulting instruction is the dividend write, with 7.5f and 2.5f in the
argument registers. `fadd`, `fsub`, `fmul` and the conversions are
divider-free and kept working, which is why only the division cases failed.

Region 3 grants one 32-byte subregion of a 256-byte region: PMSAv6 divides a
region into eight (datasheet table 117, SRD), and only subregion 3, offsets
0x060 to 0x07f, is enabled, so CPUID, the GPIO control registers, the
inter-core FIFO, the spinlocks and the interpolators beside it stay closed.
XN is set because nothing fetches instructions from a peripheral. The grant
restores what user mode held before the MPU was programmed rather than
adding a capability, and the divider already has exactly one owner: the
kernel, its interrupts and its callouts contain no divider consumer, which
`tools/verify_rp2040_divider_ownership.py` enforces on every linked kernel
through `check-divider`.

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
written, and the enable is written only when every region does. A map
missing a user region would fault the first user instruction, so a core or
emulator that drops the writes runs with the MPU off and says so.

What was read back, not what was written, is what the kernel reports. The
console line after the `cpu:` banner reads

    mpu: 8 regions, 4 programmed, MPU_CTRL 0x5: rom 16K r-x, user 144K rwx, sio div rw

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
| SIO DIV_CSR | 0xd0000078 | reads | reads |
| ROM float and double divide | 7.5 / 2.5 | 3.0, bit-exact | 3.0, bit-exact |

CPUID and DIV_CSR lie 0x78 bytes apart inside the same 256-byte region, so
the pair decides the subregion disables rather than the region's presence,
and the division that follows is what the boot ROM actually does with the
register it was granted. The test also requires `nregions` to read 8 and
`enable` to be set only with `programmed` equal to 4, and ends with `MPUTEST OK (mpu on)` or
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

Board, a Raspberry Pi Pico with Boot ROM V3, reflashed from this tree and
driven over its CDC-ACM console:

    $ sysctl machdep.mpu
    machdep.mpu.enable=1
    machdep.mpu.ctrl=0x5
    machdep.mpu.nregions=8
    machdep.mpu.separate=0
    machdep.mpu.programmed=4
    $ mputest
    machdep.mpu: enable 1, nregions 8, programmed 4, ctrl 0x5
    nregions                 0x00000000 ok (want 8, got 8)
    enable implies map       0x00000000 ok (want 1, got 1)
    kernel ram               0x20024000 ok (want 11, got 11)
    kernel text              0x10000100 ok (want 11, got 11)
    sio cpuid                0xd0000000 ok (want 11, got 11)
    boot rom                 0x00000010 ok (want 0, got 0)
    window top               0x20023ffc ok (want 0, got 0)
    sio divider              0xd0000078 ok (want 0, got 0)
    rom float divide         0x00000000 ok (want 0, got 0)
    MPUTEST OK (mpu on)
    $ fptest
    FPTEST OK

Silicon reports the datasheet's eight regions, takes all four writes, and
faults every closed probe. CPUID and DIV_CSR sit in one region and answer
differently, so the subregion disables are decided on hardware rather than
inferred. The same board ran `fptest` to SIGSEGV at ROM pc 0x00002cec
before region 3 existed.

Renode's Cortex-M0+ model implements the region registers through its NVIC
peripheral, so the emulator decides the register-level claim and the fault
path ahead of the board; the map, the readback and the test carry no
emulator-specific assumption.

Cost: 656 bytes of text and 16 of bss in the PICO kernel, 688 and 16 in
PICO_UART, and 7,988 bytes of root for mputest.

## Address translation

PMSAv6 grants permissions and exports memory attributes; it performs no
address translation (ARMv6-M Architecture Reference Manual B3.5, RP2040
datasheet 2.4.6.1, whose only fault response is HardFault). Every process
therefore runs at the addresses it was linked for, and the window is the
same 144 KB at 0x20000000 for all of them. What follows is not a gap in
the map but the architecture:

- a process is capped at MAXMEM, because there is nowhere else to put it;
- `fork` copies an image rather than sharing pages copy-on-write;
- two images never share a text mapping, so identical programs pay for
  their text twice in swap, which `p_tip` softens by reading text back
  from the executable instead of writing it to swap;
- a.out images are OMAGIC and linked at USER_DATA_START, which
  `exec_estab` requires.

Programming more regions changes none of this. A port that wants
translation wants a different core.

## Isolation between processes

One process is resident at a time. Every other image lives in the SwapRAM
pool or in the raw flash swap, both of which sit outside the four regions,
so a running process cannot read another process's saved image at all.

What remains inside the window is the span between the top of the resident
image and the bottom of its stack. A swap carries an image's data and
stack alone, so those bytes belong to whichever program ran last, and
`exec_clear` (sys/kern/exec_subr.c) zeroes them along with bss, the heap
and the stack region. `mputest residue` is the oracle: it fills the span
with a marker, execs, and counts what the new image still reads. The
board reports

    $ mputest residue
    residue: planted 26112 words in 0x20002800..0x2001c000, exec
    residue: 0 of 26112 words survived exec
    RESIDUE OK

and reported 25,856 of 26,112 surviving before the clear. Two identical
board workloads time 2.04 s against 2.04 s and 3.63 s against 3.68 s
across that change, so one bzero of the span costs at or below what a
console-timed measurement resolves.

The MPU cannot narrow the window to the resident image. The stack grows
downward between syscalls with no kernel bookkeeping -- `syscall.c`
derives `p_ssize` from the stack pointer on entry -- and ARMv6-M supplies
no fault address register, so a kernel that fenced the unused span could
not tell a growing stack from a stray pointer, and could not resume the
first. Narrowing waits on a core that reports the faulting address.
