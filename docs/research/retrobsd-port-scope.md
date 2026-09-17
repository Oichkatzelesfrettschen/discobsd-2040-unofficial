# Scoping a RetroBSD/DiscoBSD Port to the Raspberry Pi Pico (RP2040)

Target hardware: RP2040 -- dual Cortex-M0+ (ARMv6-M), no MMU, no MPU on
either core, 264 KB on-chip SRAM, 2 MB external QSPI flash accessed XIP
(execute-in-place). This document goes file-by-file into the two candidate
codebases; `research/os-options.md` in this same directory already carries a
survey-level verdict on both and on other embedded-Unix candidates (FUZIX,
NuttX) -- read that first for the comparison. This document supersedes that
file's DiscoBSD memory-protection paragraph specifically: see "Correction to
prior finding" under item 3.

Sources: `github.com/RetroBSD/retrobsd` cloned locally at commit
`a95be955bb0241c25452adf7e0b581bf329ac881` (2026-08-02); the canonical
DiscoBSD repository is `github.com/chettrick/discobsd`, not
`github.com/DiscoBSD/DiscoBSD` (that URL 404s -- confirmed by `git clone`
failing with "Repository not found"), cloned locally at commit
`f7635b6517410d4a679271d6f9d9e2d5e1d7d0d9` (2026-08-11). Both clones are
`--depth 1` against `master` (the only branch either repo has). Every file
path below is relative to one of these two clones unless stated otherwise.
Every architecture claim about ARMv6-M vs. ARMv7-M is checked against the
primary source (ARM DDI 0419E, the ARMv6-M Architecture Reference Manual) or
empirically, by assembling the actual source file with `arm-none-eabi-gcc
-mcpu=cortex-m0plus`, not from memory. Retrieved 2026-09-11.

Labels used throughout: **CONFIRMED** (read directly in the source, or
empirically reproduced), **INFERRED** (a conclusion drawn from confirmed
facts, stated as such), **UNVERIFIED** (could not be checked with the tools
available in this session).

## Answering the reframing question first

**CONFIRMED.** DiscoBSD is RetroBSD's own MIPS-to-ARM port, not an
independent implementation. `README.md` in `chettrick/discobsd`: "This
microcontroller-focused operating system is an independent continuation of
RetroBSD, a 2.11BSD-based OS targeting the MIPS-based PIC32MX7. DiscoBSD is
multi-platform, as it also supports Arm Cortex-M4 STM32F4 devices." The
project began as Christopher Hettrick's 2020 University of Victoria directed
study, documented in a 20-page report, *Porting the Unix Kernel* (CSC 490,
supervised by Dr. Bill Bird, 30 December 2020, fetched from
`github.com/chettrick/CSC490`), whose abstract states the goal directly:
"This report describes the process of porting a variant of the Unix kernel
from the MIPS processor architecture to the Arm processor architecture." The
report states DiscoBSD's PIC32 code and RetroBSD's kernel proper (`sys/kern`)
share a direct lineage: "DiscoBSD derives from the most recent commit to the
RetroBSD codebase, which is revision 506 from February 17, 2019."

**Consequence for the task.** The reframing in the prompt is correct. The
2011-vintage MIPS32-to-ARM rewrite -- new startup code, new exception/trap
entry, a new calling convention, a new `setjmp`/`longjmp`, a new syscall
gate -- is already done, targeted at ARM Cortex-M4 (ARMv7E-M). The remaining
question, worked through below, is how much of that ARMv7-M-specific work
carries to ARMv6-M unchanged, how much needs a mechanical edit, and how much
needs new engineering.

## 1. RetroBSD machine-dependent surface

**CONFIRMED.** All MIPS-specific kernel code lives in one directory,
`sys/pic32/` (`sys/mips/` does not exist -- MIPS-specific code and
PIC32-peripheral code are not separated into distinct trees). File count and
sizes (line counts via `wc -l`, commit `a95be955`):

| File | Lines | Role |
|---|---|---|
| `sys/pic32/startup.S` | 436 | Reset vector, exception/interrupt entry point, all `mtc0`/`mfc0` (coprocessor 0) access |
| `sys/pic32/exception.c` | 565 | C-level trap/interrupt dispatch called from `startup.S` |
| `sys/pic32/machdep.c` | 1071 | Kernel init, clock, `kseg0` cache-enable, board bring-up |
| `sys/pic32/signal.c` | 194 | Signal delivery (`sendsig`) and `sigreturn` |
| `sys/pic32/machparam.h` | 228 | Fixed physical memory map, `USIZE`/`SSIZE`, `spl*()` macros |
| `sys/pic32/swap.c` | 250 | Whole-process swap-file proxy driver |
| `sys/pic32/elf_machdep.h` | 88 | ELF machine-type constant for the MIPS target |
| `sys/pic32/cpu.h`, `debug.h`, `float.h`, `limits.h`, `io.h` | 44/84/50/66/209 | Small MD headers |
| `sys/pic32/pic32mx.h` | 1396 | PIC32MX peripheral register map (chip-specific, not MIPS-ISA-specific) |
| device drivers (`uart.c`, `sd.c`, `spi*.c`, `gpio.c`, `adc.c`, `pwm.c`, `gpanel*.c`, `usb_*.c`, `kbd.c`, `mrams.c`, `sramc.c`, `sdramp.c`, `sdram.S`, ...) | ~15,900 combined | PIC32 peripheral drivers -- not MIPS-ISA-specific; each is a rewrite-per-target regardless of CPU architecture |

There is no `locore.S`/`setjmp.S`/`swtch.S` split in the kernel tree the way
older BSDs have it -- `startup.S` is RetroBSD's locore, and context-switch
save/restore (`resume`, i.e. `swtch`) lives with `setjmp`/`longjmp` in the
userland MIPS library, not the kernel: `src/libc/mips/gen/setjmp.S` (117
lines), `_setjmp.S` (85), `sigsetjmp.S` (67) -- 377 lines total across that
directory including two byte-order helpers.

**MIPS-architecture-specific content, confirmed by direct read:**

- **Coprocessor 0 (trap/interrupt entry, context save).** `sys/pic32/startup.S`
  uses `mtc0`/`mfc0` on `$C0_EPC`, `$C0_STATUS`, `$C0_DEBUG`, `$C0_DEPC`
  throughout the exception/debug entry points (lines 89, 91, 93, 139, 195,
  248, 252, 273, 279, 281, 283, 284, 285, 287, 294, 297, 299, 303, 305, 307).
  `sys/pic32/io.h` lines 125 and 136 wrap `mfc0`/`mtc0` as inline-asm macros
  used by drivers for I/O-mapped coprocessor-0-adjacent access. None of this
  exists on ARM: Cortex-M has no coprocessor-0 concept: exception state
  (return address, status) is pushed to the stack by hardware and read back
  through ordinary load/store, not `mfc0`/`mtc0`.
- **Cache and address-map assumptions (kseg0/kseg1).** `sys/pic32/machdep.c`
  line 268: `/* Config register: enable kseg0 caching. */` -- this only has
  meaning given the MIPS convention that `kseg0` (`0x80000000`-`0x9fffffff`)
  is a fixed, unmapped, cacheable window onto physical RAM and `kseg1`
  (`0xa0000000`-`0xbfffffff`) is the same physical range uncached, a
  hardwired address-decode rule with no TLB involved (the PIC32 M4K core has
  none). `sys/pic32/machparam.h` hard-codes `KERNEL_DATA_START 0x80000000`
  (kseg0) and `KERNEL_FLASH_START 0x9d000000` (kseg1) directly from this
  convention. ARM has no equivalent fixed-segment convention; address
  decoding is per-SoC (on RP2040, SRAM is at `0x20000000`, flash XIP is at
  `0x10000000`, both fixed by the RP2040's own memory map, not by the CPU
  architecture).
- **Setjmp/longjmp.** `src/libc/mips/gen/setjmp.S` saves/restores MIPS `$s0`-`$s7`
  (callee-saved) and `$ra`/`$sp` using `sw`/`lw`; MIPS-specific register
  names and calling convention throughout.
- **Signal trampoline / syscall entry.** `sys/pic32/signal.c` builds the
  user-mode signal frame in C (portable logic, 194 lines) but the actual
  entry/exit through coprocessor-0 EPC happens in `startup.S`, which is
  where trap and syscall entry are unified with exception entry (RetroBSD
  has one exception vector, not a separate SVC-style gate).

**Confirmed absence:** `grep -rl "kseg0\|kseg1\|KSEG0\|KSEG1"` across `sys/`
returns only linker scripts (boot-loader `.ld` files) and `machdep.c` line
268 -- the MIPS segment convention is load-bearing for exactly one runtime
decision (cache enable) plus the fixed addresses baked into
`machparam.h`, not scattered through the tree.

## 2. RetroBSD memory model

**CONFIRMED: no MMU, no MPU, no isolation, whole-process swap.**

- `README.md`: "**A real Unix that fits in 128 kbytes of RAM** ... a chip
  with no MMU ... **Kernel** -- under 128 kbytes of flash, fits in on-chip
  RAM. **Userland** -- up to 96 kbytes per process, swapped whole to the SD
  card." The example boot log in the README prints `phys mem = 128 kbytes`,
  `swap dev = (0,2)`, `swap size = 2048 kbytes`.
- `sys/pic32/machparam.h`: `FLASH_SIZE (512*1024)`, `DATA_SIZE (128*1024)`,
  `KERNEL_FLASH_SIZE (192*1024)`, `KERNEL_DATA_SIZE (32*1024)` (kernel takes
  32 KB of the 128 KB RAM), leaving `USER_DATA_START` to `USER_DATA_END` --
  a single fixed 96 KB window shared serially, not partitioned, among user
  processes. `USIZE 3072` (user-area/kernel-stack size), `SSIZE 2048`
  (initial user stack).
- `sys/pic32/swap.c`: whole-process swap proxy driver, forwards to a
  dedicated swap partition (`swapdev`). This repo's own architecture note
  (`CLAUDE.md`, present in the clone, written for a coding agent, not part
  of upstream RetroBSD's canonical docs but an accurate paraphrase of the
  source): "processes are swapped whole (no demand paging)."
- **Process isolation: none, by design.** There is no page table, no
  segment/region-based protection, and no hardware fault path that
  distinguishes "process A touched process B's memory" from any other
  access -- because only one user process is ever resident in RAM at a
  time (the swapped-in process occupies the entire 96 KB user window; the
  next process to run swaps the current one out first). Correctness rests
  entirely on the compiler and the a.out loader placing a process inside its
  96 KB window, not on hardware.
- **Max process size:** 96 KB (`USER_DATA_END - USER_DATA_START`), confirmed
  by the README ("up to 96 kbytes per process") and `machparam.h`'s address
  constants.
- **Does it swap:** yes, always, and swapping is mandatory rather than an
  optional performance feature -- with 128 KB total RAM and a 32 KB kernel
  reservation, running even one process requires evicting whatever was
  resident before it (the exception is `swapper`, PID 0, which is not
  itself a user process and never swaps out).

## 3. DiscoBSD's ARM port

**CONFIRMED: machine-dependent tree.** `sys/arch/stm32/stm32/` (the ARM
analogue of `sys/pic32/`), commit `f7635b65`:

| File | Lines | Role |
|---|---|---|
| `machdep.c` | 1095 | Kernel init, clock, board bring-up -- pure C, no inline asm anywhere in the file (confirmed by `grep -n "__asm\|asm volatile"` returning nothing) |
| `locore0.S` | 547 | Vendor-derived reset handler + full Cortex-M4 vector table (from ST's `startup_stm32f407xx.s`), mode switch into user Thread mode |
| `locore.S` | 190 | `setjmp`/`longjmp`/`resume` (context switch) and the `icode` stub that execs `/sbin/init` |
| `syscall.c` | 246 | `SVC_Handler`/`PendSV_Handler` (two-stage syscall gate: SVC pends a PendSV exception, which does the actual trap-frame save/restore) |
| `fault.c` | 260 | `HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler`, `UsageFault_Handler`, and `arm_fault()`, which decodes `CFSR`/`MMFAR`/`BFAR`/`HFSR` to pick a signal |
| `sig_machdep.c` | 169 | `sendsig()`/`sigreturn()` -- pure C, no inline asm, builds the trap frame the trampoline in `lib/libc/arm/sys/sigaction.S` returns through |
| `systick.c` | 67 | `SysTick_Handler`, drives `hardclock()` |
| `mpu.c` | 54 | See below -- **not** region configuration |
| `sysctl.c`, `conf.c` | 337/317 | Sysctl tree, device-switch tables -- portable C |
| `clock.c` | 17 | Trivial |

Plus `sys/arch/stm32/include/` (headers, 1102 lines: `frame.h`, `intr.h`,
`mpuvar.h`, `cpu.h`, ...), `sys/arch/stm32/dev/` (device drivers, 3403
lines), and `sys/arch/stm32/hal/` (**vendored** STMicroelectronics HAL +
CMSIS, 151,597 lines -- not written by the DiscoBSD project, and not
reusable for RP2040, but also not something a porter writes by hand; it is
the equivalent role the Raspberry Pi Pico SDK would play). The genuinely
DiscoBSD-authored ARM machine-dependent surface is `stm32/` + `dev/` +
`include/` = **7,804 lines**, comparable in scale to RetroBSD's ~22,500-line
`sys/pic32/`.

**MPU usage -- read the actual code, not the label.** `sys/arch/stm32/stm32/mpu.c`
in full is 54 lines. Its entire content is:

```c
int
mpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	if (namelen != 1)
		return ENOTDIR;

	switch (name[0]) {
	case CPU_MPU_ENABLE:
		return sysctl_rdint(oldp, oldlenp, newp, LL_MPU_IsEnabled());
	case CPU_MPU_CTRL:
		return sysctl_rdint(oldp, oldlenp, newp, LL_MPU_GetCtrl());
	case CPU_MPU_NREGIONS:
		return sysctl_rdint(oldp, oldlenp, newp, LL_MPU_GetNumRegions());
	case CPU_MPU_SEPARATE:
		return sysctl_rdint(oldp, oldlenp, newp, LL_MPU_GetSeparate());
	default:
		return EOPNOTSUPP;
	}
}
```

This is a **read-only sysctl** (`sysctl_rdint`, every branch) exposing MPU
*capability* (is it present, how many regions, is it enabled) to userland --
it never calls an MPU-region-configuring function. `grep -rn
"MPU_Region\|MPU_Enable\|ConfigRegion\|LL_MPU\|HAL_MPU" sys/` across the
entire tree returns exactly these four read calls and nothing else: no
`LL_MPU_ConfigRegion`, no `HAL_MPU_Enable`, no region base/size/attribute
setup anywhere in the kernel, in any of the ten STM32 board configs, or at
boot in `locore0.S`/`machdep.c`. The MPU is never turned on.

`sys/arch/stm32/stm32/fault.c` defines `MemManage_Handler()`, the ARMv7-M
exception a configured MPU would trigger on a violation, but it exists to
**report** a fault if one somehow occurs (translating the ARM fault-status
registers into a signal, e.g. `SIGSEGV`/`SIGBUS`/`SIGILL`), not to enforce
one -- consistent with no region ever being configured.

**Correction to prior finding.** `research/os-options.md` in this directory
(section 2, "Memory-protection mechanism on its ARM boards") states DiscoBSD
"uses the ARMv7-M optional Memory Protection Unit (MPU)" and that its
verdict of IMPOSSIBLE rests on the port "hard-depend[ing] on ARMv7-M MPU
hardware for its memory-protection model." That is not what the code does.
`mpu.c` exposes MPU *state* to userland; it does not use the MPU for
protection. This is corroborated directly by the project's own history: the
2020 directed-study report that produced this code states, in its hardware
section, "A secondary feature of the target hardware is that their
processors have the ability to protect kernel code from user code with a
memory protection unit. **This feature was not explored in this project**,
but is a viable focus of additional study" (p. 2), and repeats it as an open
item in "Future Work": "**The implementation of kernel memory protection
from user processes is a potential long-term goal.** The STM32F4xx family of
microcontrollers is endowed with a memory protection unit that is dedicated
to this function. A system that offers reliable service must guarantee some
sort of memory protection" (p. 14, *Porting the Unix Kernel*, C. Hettrick,
CSC 490, Univ. of Victoria, 30 December 2020). Six years and several
releases later (`mpu.c` carries a 2025 copyright year), the MPU still is not
configured anywhere in the tree -- only a read-only sysctl was added. **The
"how deeply is MPU use woven in" answer is: not woven in at all.** DiscoBSD's
process model is the same one item 2 describes for RetroBSD --whole-process
swap, one process resident at a time, no hardware isolation -- carried
forward unchanged onto hardware that could in principle support MPU-based
kernel self-protection but does not yet. This means an RP2040 port does not
need to design a software replacement for an MPU-based scheme that does not
exist to replace: it inherits the identical no-isolation trust model
RetroBSD and DiscoBSD already ship, which is a strictly easier starting
point than the prior document's framing implied, though it does mean any
future memory-protection work (on STM32 or on RP2040-class hardware) starts
from zero either way.

## 4. ARMv7-M vs. ARMv6-M gap, checked against this codebase

**CONFIRMED via the primary source.** ARM DDI 0419E (ARMv6-M Architecture
Reference Manual), section A5.2.5, note (a): "The If-Then (IT) instruction
is not supported in ARMv6-M. The encoding space is UNDEFINED." This is not
"restricted to one instruction" (a common but inaccurate summary) -- IT is
absent outright on Cortex-M0/M0+.

**CONFIRMED empirically**, by assembling extracted fragments of the actual
DiscoBSD source with `arm-none-eabi-gcc` (GNU Arm Embedded toolchain,
locally installed) under `-mcpu=cortex-m4` (DiscoBSD's real target) and
`-mcpu=cortex-m0plus` (RP2040):

- The `ite`/`mrseq`/`mrsne` sequence from `fault.c`'s `HardFault_Handler`
  (and identically in `MemManage_Handler`, `BusFault_Handler`,
  `UsageFault_Handler` -- four occurrences, plus a fifth guarded one in
  `systick.c`) assembles clean under `-mcpu=cortex-m4` and fails under
  `-mcpu=cortex-m0plus` with four errors: `selected processor does not
  support 'ite eq' in Thumb mode`, `thumb conditional instruction should be
  in IT block` (x2), and `cannot honor width suffix -- 'tst lr,#0x4'` (the
  16-bit `tst` immediate form used here needs Thumb-2 too).
- `locore.S` (`setjmp`/`longjmp`/`resume`/`icode`) **assembles clean under
  `-mcpu=cortex-m0plus` as-is**, with no source changes. This is because the
  file is already conditionally compiled: `#ifdef __thumb2__` / `#else /*
  __thumb__ */` pairs exist at three points (lines 37, 102, 121), and GCC
  predefines `__thumb2__` only when the target actually has Thumb-2
  (confirmed: `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -dM -E -
  </dev/null` defines `__thumb2__`; the same command with
  `-mcpu=cortex-m0plus` does not). The `__thumb__`-only branches use plain
  register `mov`s to shuffle high registers instead of the Thumb-2
  `stmea`/`ldmia` forms that can address them directly.
- `syscall.c`'s `PendSV_Handler` and `intr.h`'s `splraise()`/`splx()`/`spl0()`
  have the same `#ifdef __thumb2__` / `#else` structure, with a
  hand-written ARMv6-M-correct fallback already present: the comment at
  `syscall.c` line 92 literally reads "ARMv6-M hardware already pushed
  r0-r3, ip, lr, pc, psr on PSP..." and the code that follows avoids `ite`,
  using a `mov r0,r8` / `push` sequence to shuffle high registers instead.
  `intr.h`'s `__thumb__` branch collapses `splhigh()`/`splclock()`/`spltty()`/
  `splnet()`/`splbio()` to a single global-interrupt-disable primitive
  (`arm_intr_disable()`), because ARMv6-M has no `BASEPRI` register to do
  graduated priority masking with -- a real architectural narrowing (fewer
  usable priority levels), not just a syntax change, and the code already
  reflects it.
- `locore0.S` -- the vector table and reset handler -- is **not** part of
  this pre-existing Thumb-1 fallback work, and initially appears to
  assemble clean under `-mcpu=cortex-m0plus`, but that is misleading: the
  file itself carries `.cpu cortex-m4` and `.fpu softvfp` directives (lines
  49-51) that **override the command-line `-mcpu` flag for the rest of the
  file** -- so it silently assembles as Cortex-M4/Thumb-2 regardless of the
  build's target flag, producing 32-bit-wide encodings (confirmed:
  `objdump` on the resulting object shows `ldr.w sp, [pc, #108]`, the
  32-bit Thumb-2 form, at `Reset_Handler`) that Cortex-M0+ hardware would
  fault on at runtime with no compile-time warning. Removing the `.cpu`/
  `.fpu` override lines and reassembling under `-mcpu=cortex-m0plus`
  produces six real errors: `ldr sp,=_estack`, three more Thumb-2-only
  literal-load/shift-immediate forms, and the unconditional `msr BASEPRI,
  r0` at the user-mode-switch sequence (line 122) -- `BASEPRI` does not
  exist on ARMv6-M. GNU `as` does **not** reject `msr BASEPRI, r0` at
  assemble time under `-mcpu=cortex-m0plus` (it is just a special-register
  immediate encoding the assembler does not validate against the selected
  core), so this specific line is a silent-wrong-at-runtime bug, not a
  build failure -- more dangerous than the errors above precisely because
  nothing catches it automatically.
- `lib/libc/arm/` (userland, not kernel): `grep -rl
  'ite\b|itt\b|cbz|cbnz|\.cpu|__thumb2__|sdiv|udiv'` across all 21 files (659
  lines total) matches `gen/_setjmp.S`, `gen/setjmp.S` (same
  `__thumb2__`/`__thumb__` split as the kernel's `locore.S`, same
  conclusion), `string/strcmp.S` (a comment states outright, "This is also
  suitable for ARMv6-M" -- upstream Newlib provenance, already portable),
  and `sys/pipe.S` (uses `.cpu`; not yet inspected in this pass, flagged
  **UNVERIFIED** below).
- **LDREX/STREX, CBZ/CBNZ, hardware divide, bitfield instructions
  (UBFX/SBFX/BFI): confirmed absent from DiscoBSD's own source.**
  `__LDREXW`/`__STREXW`/etc. exist only inside the vendored CMSIS header
  `sys/arch/arm/include/cmsis_gcc.h`, and `grep` for callers (`__LDREX`,
  `__STREX`, `__CLREX`) across the kernel, drivers, and headers returns
  nothing outside that one file -- dead code as far as DiscoBSD is
  concerned. No `sdiv`/`udiv`/`cbz`/`cbnz` mnemonics appear anywhere in
  DiscoBSD's hand-written `.S` files. Where the *compiler* (not
  hand-written source) would normally emit these for M4 code (e.g. an `int`
  divide, or a short forward branch), that is transparent to porting:
  recompiling the same C with `-mcpu=cortex-m0plus` makes the compiler emit
  a library divide call and a 16-bit branch instead, automatically. These
  are non-issues for this codebase, contrary to what a generic ARMv7-M/v6-M
  gap checklist would suggest -- they only matter here because they are
  absent, which is itself worth confirming rather than assuming.
- **VTOR:** confirmed present on RP2040 (`M0PLUS_VTOR` register at PPB
  offset `0xed08`, RP2040 datasheet p. 86) -- not all Cortex-M0+
  implementations carry it (VTOR is optional in ARMv6-M), but RP2040 does,
  so DiscoBSD's `SCB->VTOR` vector-table relocation (used in the vendored
  ST HAL, `system_stm32f4xx.c`, not in DiscoBSD's own code) has a target to
  relocate to; DiscoBSD's own code does not currently reference `VTOR` at
  all, so this is moot for the kernel itself but relevant if a boot-stage
  design needs it.
- **NVIC priority bits:** STM32F4 implements 4 priority bits (16 levels,
  `__NVIC_PRIO_BITS` = 4, consumed by `intr.h`'s `IPL_BITS` macro); RP2040's
  Cortex-M0+ implements 2 (4 levels, `__NVIC_PRIO_BITS` = 2, confirmed via
  the RP2040 datasheet and Pico SDK headers). This changes `IPL_BITS`'s
  computed value and halves the number of distinct interrupt priority
  levels DiscoBSD's `spl()` scheme can express -- a mechanical constant
  change plus a policy decision about which of DiscoBSD's five priority
  levels (`IPL_BIO`, `IPL_TTY`, `IPL_NET`, `IPL_SOFTCLOCK`, `IPL_CLOCK`/
  `IPL_HIGH`) to collapse together, not a rewrite.
- **-mcpu=cortex-m4, confirmed explicit and pervasive:** every one of the
  ten `sys/arch/stm32/compile/*/Makefile` board directories sets
  `CMACHCPU= -mcpu=cortex-m4`, as does `sys/arch/stm32/conf/Makefile.stm32`
  (the shared template) and the `.cpu cortex-m4` directive in `locore0.S`
  noted above. `-mfloat-abi=soft` is set everywhere too (confirmed in
  `Makefile.stm32`) -- DiscoBSD does not use the STM32F4's hardware FPU
  despite it being present, which removes one whole category of RP2040 gap
  (Cortex-M0+ has no FPU) before it starts.

**Would `-mcpu=cortex-m0plus` compile the tree as-is? No, but not
uniformly.** Of the files directly checked: `locore.S`, `syscall.c`,
`systick.c`, and `intr.h` already have working ARMv6-M code paths that
compile clean today (never exercised by any build -- no board config passes
`-mcpu=cortex-m0plus` anywhere in the tree, and `master` is the only branch,
with zero commits mentioning RP2040/ARMv6-M/Cortex-M0 in `git log`). `fault.c`
and `locore0.S` do not, and need real (if largely mechanical, given the
precedent already set by the other four files) rewriting. `sig_machdep.c`
and `conf.c` are pure portable C with no architecture dependency either way.
This is a materially different picture from "nobody has thought about
ARMv6-M here" -- someone (the maintainer, git blame not run this pass but
worth doing) already carried four of the six hottest kernel MD files
partway there, apparently as groundwork, without ever wiring up a board that
uses it.

## 5. Userland and toolchain

**CONFIRMED: both projects build BSD a.out for userland, via the same
mechanism.** RetroBSD's own architecture doc (`CLAUDE.md` in the clone, an
accurate description of the build, not upstream canon but internally
consistent with the Makefiles): target link recipe links with `ld.lld`
against `src/elf32-mips.ld` + `crt0.o` to produce a temporary ELF, then
`tools/elf2aout` converts it to an a.out binary and the `.elf` is discarded.
DiscoBSD does the identical thing on ARM: every `usr.bin/*/Makefile` (e.g.
`usr.bin/du/Makefile`) ends its link rule with `${ELF2AOUT} $@.elf $@ && rm
$@.elf`. Confirmed both `tools/elf2aout` (RetroBSD) and its DiscoBSD
counterpart (`tools/elf2aout/elf2aout.c`) exist and are actively invoked;
DiscoBSD additionally keeps `sys/kern/exec_elf.c` alongside `exec_aout.c` in
the kernel (both loaders are present in the tree; the standard userland
build path produces and therefore requires a.out, per the Makefile evidence
above -- whether `exec_elf.c` is reachable from any default build
configuration is **UNVERIFIED** in this pass).

**Kernel image format** is unrelated to the userland-binary-format question
above: both projects' *kernel* is delivered as ELF + Intel HEX + raw binary
(`unix.elf`/`unix.hex`/`unix.bin` -- confirmed in both `README.md` files and
the `Makefile.stm32` `SYSTEM_LD_TAIL` rule, which runs `objcopy -O ihex` and
`objcopy -O binary` against the linked ELF). This has no interaction with
process isolation or with the RP2040 gap; it is a flashing-format
convenience, and an RP2040 port would produce a UF2 (RP2040's native
flashing container) via the same kind of `objcopy`-adjacent step.

**How much of the userland is architecture-independent C vs. assembly
needing rewrite, confirmed by line count:**

- RetroBSD: `src/libc/mips/` totals 1,251 lines of MIPS assembly (`gen/`
  setjmp family + byteorder helpers, `string/` -- `bcmp`, `bcopy`, `bzero`,
  `ffs`, `index`, `memcpy`, `memmove`, `memset`, `rindex`, `strcmp`,
  `strlen` -- and `sys/` -- `_brk`, `_exit`, `pipe`, `ptrace`, `sigaction`).
  Everything else under `src/` (`src/cmd/`, `src/libc/gen`, `stdio`,
  `stdlib`, and the ~130 userland programs) is portable C, unaffected by
  CPU architecture beyond needing to recompile.
- DiscoBSD: `lib/libc/arm/` totals 659 lines across 21 files -- smaller
  than RetroBSD's MIPS equivalent because `string/` here has only
  `memmove.S` and `strcmp.S` (most of DiscoBSD's string routines are the
  portable C fallback, not hand-written asm, per `lib/libc/string`'s
  broader directory listing, not separately re-verified this pass). Item 4
  above already establishes which of these 21 files need real ARMv6-M work
  (`_setjmp.S`/`setjmp.S` -- already forked and working, same pattern as
  the kernel) versus which are untouched (`sys/sigaction.S`, `sys/pipe.S`,
  `sys/ptrace.S`, `sys/_brk.S`, `sys/_exit.S` -- **UNVERIFIED** this pass,
  flagged as the next files to check before starting real port work).
- Kernel-independent C library and command count: DiscoBSD ships 122
  `usr.bin/*` program directories (`ls usr.bin | wc -l`) plus `usr.sbin` and
  `games`; RetroBSD's `src/cmd` + `src/games` is comparable in scope. None
  of this is CPU-specific beyond the a.out link step and the 659/1,251
  lines of hand-written asm libc routines above.
- **No custom linker/loader beyond the standard a.out layout the swap model
  requires.** Both projects use a real linker (`ld.lld` for RetroBSD,
  GNU `ld` via `arm-none-eabi-gcc` for DiscoBSD) against a small,
  project-specific linker script (`src/elf32-mips.ld`, `lib/elf32-arm.ld`)
  that places `.text`/`.data`/`.bss` for the ELF-then-a.out pipeline; the
  "custom" part is `elf2aout` itself (a small host tool, not a target-side
  loader) and the kernel's `exec_aout.c`, which is portable C.

## 6. Concrete scope estimate

Effort bands below are rough order-of-magnitude, not estimates carrying
schedule authority -- this is a scoping document, not a plan. "Mechanical"
means the change pattern is already demonstrated working in this exact
codebase (item 4's `locore.S`/`syscall.c`/`systick.c`/`intr.h`
`__thumb2__`/`__thumb__` split); "new engineering" means no such pattern
exists in-tree yet; "research problem" means no known solution is implied
by anything in either codebase.

| # | Work item | Files | Category | Basis |
|---|---|---|---|---|
| 1 | Add RP2040 CMSIS core header (`core_cm0plus.h`) | new `sys/arch/arm/include/` file | New engineering, licensing-gated | `core_cm4.h` exists for M4; no `core_cm0plus.h` anywhere in the tree. Maintainer's own comment on DiscoBSD issue #17 (2026-03-25): current ARM CMSIS-Core is Apache-2.0-licensed and incompatible with the project's self-contained-source goal, but notes "`core_cm0plus.h` was historically 3-clause BSD licensed before the switch to Apache 2.0" -- i.e., an old, compatibly-licensed version may exist to source instead of writing one from scratch. This decides whether an upstreamable (BSD-licensed, self-contained) port is even possible, not just how hard it is. |
| 2 | Rewrite `locore0.S`: strip the `.cpu cortex-m4`/`.fpu softvfp` override, replace the Thumb-2-only reset-handler sequences (6 confirmed assembler errors once the override is removed), replace `msr BASEPRI, r0` with a `PRIMASK`-based equivalent (silent-wrong-at-runtime bug, not caught by the assembler), and rebuild the vector table for ARMv6-M's shorter exception set (no separate MemManage/BusFault/UsageFault/DebugMon vector slots) | `locore0.S` (547 lines, rewrite maybe 150-200 of them) | Mechanical, once designed | Empirically confirmed via `arm-none-eabi-gcc -mcpu=cortex-m0plus` (item 4); the fix pattern (branch-based instead of literal-load-immediate, PRIMASK instead of BASEPRI) is the same one already applied in `intr.h`'s `__thumb__` branch |
| 3 | Rewrite `fault.c`'s four handlers into one `HardFault_Handler`, and rewrite `arm_fault()` to drop the `CFSR`/`MMFAR`/`BFAR`/`HFSR` decode entirely | `fault.c` (260 lines, net shrinks) | Mechanical rewrite, but a real capability loss | ARMv6-M has one fault type and none of those status registers (confirmed via `frame.h`'s own comment: "ARMv6-M fault types: HardFault" vs. "ARMv7-M fault types: HardFault, MemManage, BusFault, UsageFault"); this is not "port the diagnostic," it is "accept losing fault-cause diagnosis" |
| 4 | Port `sig_machdep.c` (trivial -- already portable C) and `lib/libc/arm/sys/sigaction.S` (the actual trampoline, not yet checked) | `sig_machdep.c` (0 lines to change), `sigaction.S` (unverified, likely <50 lines) | Mechanical to research-needed, split by file | `sig_machdep.c` read in full, zero inline asm; `sigaction.S` flagged UNVERIFIED in item 5 |
| 5 | Fix `intr.h`'s `IPL_BITS`/priority-level constants for 2-bit NVIC priority | `intr.h` (a handful of lines) | Mechanical | RP2040 `__NVIC_PRIO_BITS` = 2 vs. STM32F4's 4, confirmed via datasheet/Pico SDK |
| 6 | Write RP2040 peripheral drivers: UART, SPI+SD card, GPIO, clock/PLL init, SysTick source selection | new files under a new `sys/arch/rp2040/` (or similar) tree | New engineering, but same shape as ten existing STM32 board ports | DiscoBSD's thesis (Future Work, p. 14) names exactly this set ("A UART driver and an SPI-based SD card driver would be enough for the system to stand on its own... A GPIO driver would enable more functionality") as the minimum for *any* new board; RP2040 has no special difficulty here relative to STM32 |
| 7 | Second-stage boot / flash XIP bring-up | new, no DiscoBSD/RetroBSD equivalent | New engineering | RP2040 has no internal flash and no "write hex to a fixed flash address" boot model either project's linker scripts assume; code executes from external QSPI flash behind a boot ROM plus a 256-byte second-stage bootloader that configures the flash controller for XIP. The Pico SDK ships working `boot2` stubs for common flash parts to adapt (not from-scratch); after boot2 runs, XIP flash behaves like a normal memory-mapped ROM, similar in kind (if not layout) to STM32's fixed flash-at-`0x08000000` model, so the kernel-side flash-execution assumptions the linker scripts encode do not need a deeper rewrite once boot2 exists |
| 8 | Adjust linker scripts / memory map (`kern.ldscript` + per-board `.ld`) for RP2040's flash-at-`0x10000000`/SRAM-at-`0x20000000` map and 264 KB (vs. STM32F4's 192 KB or PIC32's 128 KB) RAM | `sys/arch/stm32/conf/kern.ldscript` (as a template) | Mechanical | `kern.ldscript` already slices one flat `MEMORY` region into `FLASH`/`RAM`/`USERRAM`/`U0AREA`/`UAREA` via `ORIGIN`/`LENGTH`, a pattern that carries directly; RP2040's SRAM is contiguous at the standard `0x20000000` alias (confirmed via RP2040 datasheet: the "striped" 4-bank interleave that improves bus parallelism is transparent at that alias -- every address in `0x20000000`-`0x20042000` is backed by real, individually addressable SRAM, so the existing flat-region linker model needs new constants, not a new model) |
| 9 | Widen `USIZE`/user-window constants to use more of the 264 KB RAM (264 KB vs. STM32F4's 192 KB/128 KB baseline and PIC32's 128 KB) | `machparam.h`-equivalent for the new port | Mechanical, and a net improvement | Confirmed favorable: RP2040 gives more headroom per process than either existing target, not less |
| 10 | Second-core (RP2040 is dual-Cortex-M0+) handling: leave core 1 unused, or design SMP/AMP support | none currently | Out of scope for MVP / research problem if pursued | Neither RetroBSD nor DiscoBSD's kernel has any multi-core code; the simplest correct answer is to never start core 1, which costs nothing, but is worth stating explicitly so it is a decision, not an oversight |
| 11 | Process isolation beyond "trust the compiler" | none currently, on either project | Not required for parity, a research problem only if attempted | Item 3's correction: neither RetroBSD nor DiscoBSD isolates processes today. RP2040 has no MPU on either core, so this door is permanently closed on this hardware, not just currently unbuilt -- unlike on STM32F4, where the MPU exists and unused. An RP2040 port cannot ever add MPU-based protection later; a from-scratch software scheme (e.g., a bounds-checked interpreter layer, or accepting the existing trust model permanently) is the only option, and no design for one exists in either codebase. **This is the one item on this list that is a genuine research problem, not an engineering task** -- everything else above has a known solution shape. |
| 12 | Kernel autoconfiguration (`kconfig`) entries + board `Config` file for the new target | `sys/arch/stm32/conf/*` analogues | Mechanical | Both projects already parameterize board bring-up this way for ten (DiscoBSD) or two (RetroBSD) existing boards |
| 13 | Bring up the whole toolchain: confirm `arm-none-eabi-gcc`/`binutils` handle `-mcpu=cortex-m0plus -mthumb` for every file in the tree, not just the ones sampled in item 4 | build system | Verification work, cheap | This session confirmed the toolchain exists and works for the specific files checked; a full-tree build attempt is the fast way to enumerate every remaining `__thumb2__`-only file at once |

**Boots-to-shell minimum viable milestone** (narrower than a complete port):
items 2, 3, 5, 6 (UART + SD/SPI only, per the thesis's own stated minimum),
8, 9, 12, 13, plus whichever of item 4's unverified `lib/libc/arm/sys/*.S`
files the toolchain flags in a real build attempt. This explicitly excludes
GPIO/ADC/PWM/USB drivers, full peripheral coverage across board variants,
and item 11 (isolation) entirely -- consistent with how DiscoBSD's own
thesis frames its STM32 minimum ("A UART driver and an SPI-based SD card
driver would be enough for the system to stand on its own"). Item 1 (CMSIS
licensing) gates whether the result can be upstreamed cleanly, not whether
it boots -- a temporary Apache-2.0 header or a hand-written minimal
replacement (RP2040's M0+ CMSIS surface is small: NVIC, SCB, SysTick, a
handful of intrinsics) unblocks bring-up either way.

**A complete port** additionally needs the remaining ~90 peripheral-driver
lines' worth of GPIO/ADC/PWM/graphics/USB support DiscoBSD carries for
STM32 (`sys/arch/stm32/dev/`, 3,403 lines, none of it RP2040-specific yet),
full `lib/libc/arm/` coverage, and a decision on item 11 and item 10.

## 7. Has anyone tried?

**CONFIRMED: no RP2040/Cortex-M0/ARMv6-M attempt exists on either project,
past or in progress.**

- RetroBSD: all 107 issues (`gh api repos/RetroBSD/retrobsd/issues
  ?state=all&per_page=100`) read and titled above; none mentions ARM,
  Cortex-M, Pico, or RP2040. Two issues request *other* non-MIPS ports:
  #97/#88 (RISC-V, for CH32V307) and #87 (ESP32/Xtensa) -- both closed,
  neither ARM. A GitHub repository search for `"retrobsd" rp2040` and
  `"retrobsd" pico` returns zero results; a search for `"retrobsd"` alone
  returns nine repositories, all MIPS/PIC32-only forks or utilities, none
  targeting ARM.
- DiscoBSD: all 36 issues/PRs read (`gh api
  repos/chettrick/discobsd/issues?state=all&per_page=100`); the single
  branch is `master`; 25 forks checked, of which the six most recently
  pushed were diffed against upstream `master` via `gh api
  repos/chettrick/discobsd/compare/master...<fork>:master` -- none carries
  RP2040-related commits (the only fork ahead of upstream at all,
  `Sch-LikA/discobsd`, adds a DevEBox STM32F407 board, already merged
  upstream as PR #36). No commit in `chettrick/discobsd`'s history mentions
  RP2040/Pico/Cortex-M0 (`git log` searched; also confirmed no matching
  commit via GitHub's commit search API).
- **DiscoBSD issue #17, "RP2350 support" (opened 2024-08-08 by the
  maintainer, still open) exists and is directly on point, but is about the
  RP2350, the RP2040's successor -- not the same chip.** The RP2350 carries
  dual Cortex-M33 cores (ARMv8-M Mainline, which **does** have an MPU and
  TrustZone) or, alternatively, dual RISC-V Hazard3 cores selectable at
  boot. Maintainer's own framing, quoted directly from the issue thread:
  - 2024-08-08 (opening the issue after linking a third-party RP2350
    teardown): "Too bad there wasn't a Pi Pico with a Cortex M4 and tons of
    RAM. Would be excellent. I could do what a lot of other projects do and
    just git submodule the CMSIS and HAL stuff, but I would like the OS to
    be as self-contained as possible."
  - 2026-03-25 (18 months later, the most recent activity): "As far as
    licensing is concerned, adding support for a RISC-V Hazard3 core
    instead of a Cortex-M33 core, as the RP2350 has both and can be
    individually selected during boot, is a way to move forward with
    support for RP2350-based devices. From what I have determined so far,
    the supplied pico-sdk provides 3-clause BSD licensed code for the
    Hazard3 core and peripheral drivers and supporting files. The
    Cortex-M33 CMSIS-Core is still currently Apache 2.0 licensed (so it is
    not compatible)... **(Note that the RP2040 is also a potential porting
    candidate, as it has a Cortex-M0+ core and the core_cm0plus.h CMSIS
    file was historically 3-clause BSD licensed before the switch to
    Apache 2.0.)**"
  - This is the only place, in either project's entire issue/PR/commit
    history, where the RP2040 specifically is discussed as a target. It is
    a maintainer aside inside a thread about a different chip, dated six
    months before this report, with no follow-up commit, branch, or PR.
- General web search (`RetroBSD OR DiscoBSD "RP2040" OR "Raspberry Pi
  Pico" port`) returns only generic Pico hardware/retailer pages, no hits
  connecting either project to the RP2040.

**Bottom line for item 7:** the RP2040 has been named once, in passing, by
DiscoBSD's own maintainer, as a plausible future target contingent on
resolving a CMSIS licensing question -- and nothing beyond that sentence
exists anywhere in either project.

## Summary

The reframing holds: DiscoBSD already did the MIPS-to-ARM rewrite RetroBSD
would otherwise require from scratch, and did it more thoroughly toward
ARMv6-M than expected going in -- four of its six hottest machine-dependent
files (`locore.S`, `syscall.c`, `systick.c`, `intr.h`) already carry a
working, empirically-confirmed ARMv6-M code path, apparently written as
groundwork and never wired to a board. The two real blockers are narrower
than "port the OS": (1) `locore0.S` and `fault.c`, the two files that do not
yet have that treatment, need real (if precedented) rewriting, and (2) the
MPU is not actually used by DiscoBSD today for anything but read-only
introspection, so there is no MPU-dependent protection scheme to replace --
only the standing decision, never resolved on STM32 either, of whether to
build one from scratch. No one has attempted this port; the RP2040 has been
named once, by DiscoBSD's maintainer, as a licensing-gated possibility.
