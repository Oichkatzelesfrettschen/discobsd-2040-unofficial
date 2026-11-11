# The MPU map: what the kernel programs and what proves it

The RP2040's Cortex-M0+ carries an eight-region Protected Memory System
Architecture MPU (RP2040 datasheet 2.4.1, 2.4.2.5, 2.4.6; ARMv6-M
Architecture Reference Manual B3.5). sys/arch/rp2040/rp2040/mpu.c programs
it once at startup and never again: one process is resident at a time and
every process image occupies the same 192 KB window at USER_DATA_START, so
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
| 2 | 0x20020000 | 64 KB | AP 011, XN 0, SIZE 15 | rwx | the user window, high part |
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
adding a capability, and the divider has user code as its only consumer:
the kernel, its interrupts and its callouts contain none, which
`tools/verify_rp2040_divider_ownership.py` enforces on every linked kernel
through `check-divider`. The consumers are the boot ROM's float division and
the libc integer helpers `__aeabi_idiv`, `__aeabi_idivmod`, `__aeabi_uidiv`
and `__aeabi_uidivmod` (lib/libc/arm/gen/aeabi_div.S), which write
DIV_UDIVIDEND and DIV_UDIVISOR, wait eight cycles and read DIV_REMAINDER and
then DIV_QUOTIENT.

The kernel saves no divider state because no kernel path runs between a
divisor write and the quotient read of an unprivileged sequence. A process
switch happens in `swtch`, whose callers are `userret` (reached from the
system-call path in PendSV_Handler and from the fault path), the exit and
stop paths, and `tsleep` in kernel context; a signal is delivered in
`userret`'s `postsig` loop. SysTick and device interrupts return to the
interrupted instruction after `hardclock` and the drivers, none of which
touch the divider. The helpers' sequence holds no `svc`, so the divider
registers an interrupted process left are the registers it finds on return.
A future kernel divider consumer, an asynchronous preemption point in an
exception return, or a debugger breakpoint planted inside the sequence
(ptrace stops a process at `userret`) breaks that argument; the first is
rejected by `check-divider`, and the sequence's shape is held by its
`--user-helper` check (doc/TESTING.md).

192 KB is not a power of two, so the window takes two regions; static
assertions in mpu.c pin their sum to USER_DATA_SIZE and both bases to their
region alignments. Text runs from the window because a.out images load into
SRAM, so XN stays clear. S, C and B stay zero: the RP2040 bus fabric
consumes none of the exported attributes (the XIP cache is an
address-range cache) and the Cortex-M0+ reorders nothing.

The kernel runs privileged and sees the default map as its background, so
kernel text in XIP flash, kernel data above the window, the peripherals at
0x40000000, SIO at 0xd0000000 and the PPB stay reachable to it and closed
to a process. A process may read, write and execute its own window, and
read and execute the boot ROM. Nothing else. That includes the unstriped
SRAM windows at 0x21000000-0x2103ffff (datasheet 2.6.2), which name the
same cells as the user window and kernel RAM one bank at a time: no region
covers them, so an unprivileged access faults, and a region that granted
them to user mode would hand a process the kernel's data. `mputest` carries
no probe at those addresses.

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

    mpu: 8 regions, 4 programmed, MPU_CTRL 0x5: rom 16K r-x, user 192K rwx, sio div rw

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
| kernel RAM | USER_DATA_END, 0x20030000 | SIGSEGV | reads |
| kernel text | 0x10000100, the kernel's vector table | SIGSEGV | reads |
| SIO CPUID | 0xd0000000 | SIGSEGV | reads |
| boot ROM magic | 0x00000010 | reads | reads |
| window top | USER_DATA_END - 4 | reads | reads |
| SIO DIV_CSR | 0xd0000078 | reads | reads |
| ROM float and double divide | 7.5 / 2.5 | 3.0, bit-exact | 3.0, bit-exact |
| caught fault | USER_DATA_END, read three times with a SIGSEGV handler installed | handler runs with the faulting pc and fault-time r4-r11 in its sigcontext, leaves by siglongjmp | not run |

CPUID and DIV_CSR lie 0x78 bytes apart inside the same 256-byte region, so
the pair decides the subregion disables rather than the region's presence,
and the division that follows is what the boot ROM actually does with the
register it was granted. The test also requires `nregions` to read 8 and
`enable` to be set only with `programmed` equal to 4, and ends with `MPUTEST OK (mpu on)` or
`MPUTEST OK (mpu off)` when every line agrees, so the same program on the
same image distinguishes a map the registers accepted from one they
dropped. Each closed probe under an enabled MPU is a real HardFault, and
fault.c prints its frame on the console before delivering SIGSEGV.

arm_fault (fault.c) delivers that signal through the same struct
trapframe a system call uses: it builds one from the stacked hardware
frame and the r4-r11 HardFault_Handler banked into fault_regs, points
u.u_frame at it before psignal and userret, then writes it back as the
frame the exception return pops and reloads r4-r11 from it, so a handler
entered by sendsig, or a register a debugger changed while the process
was stopped, takes effect. The `caught fault` probe decides it: against a
kernel that leaves u.u_frame on the last system call's frame the boot
never gets past mputest, because the faulting load replays at HardFault
priority, and against one whose trapframe carries a wrong r4 the probe
fails all three reads.

## Evidence

Renode 1.17.0 with the matgla/Renode_RP2040 models, `check-renode`
(tools/renode/check-boot.py): the probe stage asserts the `mpu:` line
above; the shell stage runs mputest and asserts `MPUTEST OK (mpu on)`. The
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

## Read-only admission of system-call arguments

The kernel checks a user pointer against what the process may do with it
rather than against the window alone. `baduaddr_span` (machdep.c) admits
`[__user_data_start, USER_TOP(p))`, the bytes a process may read and write.
`baduaddr_rspan` adds every region of the map above that grants
unprivileged read and denies unprivileged write (AP 110), across its enabled
subregions only: `mpu_user_rospan` (mpu.c) walks the static map and
`rp2040_user_region_span_contains` (machine/usercopy.h) decides one region,
so the boot ROM is admitted and the SIO divider, an AP 011 device grant, is
not. Transfers that only read user memory take the wider check: `copyin`,
through which namei, execve's argument and environment strings, sysctl
names and every other argument structure arrive, and the source vectors of
`write` and `writev` in `rwuio` (sys_generic.c). Transfers that store into
user memory keep the window: `copyout`, the target vectors of `read` and
`readv`, ioctl buffers (whose direction a driver may not honor), and
`physio` (vm_swp.c). This is the shape of a read-only mapping under
`mvalid` (docs/research/xip-shared-libc/streams/09-historical-unix-archive.md):
a process may hand the kernel the address of bytes it can read but not
write, and the kernel never writes them on its behalf. The answer comes from
the static map rather than the registers, so a core that dropped the region
writes admits no more than one that took them.

A future read-only libc partition in flash enters the map as one more AP 110
region and is admitted through the same walk; nothing else changes.
`mputest` holds the board behavior: a write and a writev from the ROM deliver
its bytes through a pipe, access on a NUL-terminated ROM string reaches
namei and answers ENOENT, and a read into the ROM, a write source crossing
the ROM's end at 0x3ff8 and a write source in kernel text each fail with
EFAULT. `check-kernel-ilp32` and `check-kernel` hold the arithmetic and the
direction split on the host (doc/TESTING.md).

## Address translation

PMSAv6 grants permissions and exports memory attributes; it performs no
address translation (ARMv6-M Architecture Reference Manual B3.5, RP2040
datasheet 2.4.6.1, whose only fault response is HardFault). Every process
therefore runs at the addresses it was linked for, and the window is the
same 192 KB at 0x20000000 for all of them. What follows is not a gap in
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

## Permanent window and SwapRAM pool

The process ceiling and stack top are fixed at `0x20030000`. The optional
16 KB SwapRAM pool is a kernel-only `NOLOAD` section that the linker places
after ordinary kernel data and BSS. The kernel can evacuate compressed images
from the pool to raw flash, but evacuation does not change the process window.

The former LARGE epoch lent the adjacent pool to one process. Relocating the
pool made that contract unsafe: extending `USER_TOP` above `0x20030000` would
grant ordinary kernel RAM to the process. The port therefore removed
`SWAPRAM_BONUS`, `P_LARGE`, the variable `user_top()` path and the epoch
sysctls before moving the pool. `docs/research/swap-exhaustion/review-04.md`
owns the adversarial decision, consumer inventory and host evidence.

The board transcripts in the Evidence section retain `0x20024000` because
they measured the former 144 KB map. They establish the old image's MPU
behavior only; `review-04.md` supersedes their live-layout conclusion. A new
board run must probe `0x20030000` and record the new region readback before it
establishes physical-hardware protection for the 192 KB map.

The Renode transcript in the Evidence section reads `programmed=3` from an
earlier three-region map; the board transcript and `mpu.c` program four. The
transcript stays as captured until the next `check-renode` run replaces it.
