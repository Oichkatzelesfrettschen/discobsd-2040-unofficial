# Unix-like / BSD-like Operating Systems on the Raspberry Pi Pico (RP2040)

> **Correction, superseded by `retrobsd-port-scope.md`.** This report's
> DiscoBSD verdict is wrong. It reads `sys/arch/stm32/stm32/mpu.c` as a
> memory-protection dependency. That file only exposes MPU state to userland
> through a read-only sysctl and never configures a region anywhere in the
> tree, and DiscoBSD's own 2020 thesis places memory protection under future
> work. DiscoBSD therefore does not isolate processes even on hardware that
> has an MPU, so an RP2040 port inherits the trust model both BSDs already
> ship rather than having to replace one. Four of six hot machine-dependent
> files already assemble clean under `-mcpu=cortex-m0plus`. Read DiscoBSD's
> row below as "requires a port, and it is the shortest BSD path," not as
> "impossible without major work." The canonical repository is
> `github.com/chettrick/discobsd`.


Target hardware: RP2040 -- dual Cortex-M0+ (ARMv6-M), no MMU, no MPU on the
M0+ core, 264 KB on-chip SRAM, 2 MB external QSPI flash accessed XIP
(execute-in-place).

Research method: primary sources only -- upstream repositories read directly
via the GitHub API / `gh`, `curl` against `raw.githubusercontent.com`, and
official docs. Findings below separate what was directly confirmed from a
primary source from what is inferred. Retrieved 2026-09-11.

## Summary table

| OS | Target architecture(s) | RP2040 status | Concrete blocker |
|---|---|---|---|
| RetroBSD | MIPS32 M4K (Microchip PIC32MX) | REQUIRES A PORT (in practice a full rewrite of the machine-dependent layer) | Machine-dependent code (`sys/pic32/`) is written directly against MIPS32 C0 coprocessor registers and hard-codes MIPS kseg0/kseg1 fixed-segment addresses (`0x80000000`, `0x9d000000`); none of this exists on ARMv6-M. No RP2040/ARM fork or port found. |
| DiscoBSD | ARM Cortex-M4/ARMv7E-M (STM32F4xx) and MIPS32 (PIC32MX) | IMPOSSIBLE WITHOUT MAJOR WORK (as currently architected) | The ARM port's memory-protection code (`sys/arch/stm32/stm32/mpu.c`) hard-depends on the ARMv7-M optional MPU register block via ST's `LL_MPU_*` HAL calls. The RP2040's Cortex-M0+ (ARMv6-M) core has no MPU hardware at all, so this is not a config change. |
| FUZIX | Many 8/16-bit and small 32-bit targets, incl. arm32 | SUPPORTED NATIVELY | None -- an active, maintained platform directory (`Kernel/platform/platform-rpipico`) exists, uses flat 32-bit addressing with swap (not bank switching), and boots to a working userland/shell. Not fully mature: signal delivery from CPU exceptions is an open issue, and each process is capped at 64 KB. |
| Apache NuttX | Dozens of architectures incl. ARM Cortex-M0 through M7, RISC-V, Xtensa, x86, MIPS, SPARC | SUPPORTED NATIVELY | None -- upstream `boards/arm/rp2040/` contains a `raspberrypi-pico` board directory with multiple working configs, including an `nsh` (NuttShell/NSH) defconfig, an `nsh-flash` config that runs from the external QSPI flash, and an `nshsram` config that runs entirely from SRAM. |
| Other candidates (Xinu, Linux nommu, NetBSD, Embox, collapseOS, RIOT-OS) | various | IMPOSSIBLE WITHOUT MAJOR WORK / NOT FOUND | Mainline Linux's ARM nommu port has no `CPU_V6M` Kconfig symbol at all (only `CPU_V7M`), so Cortex-M0/M0+ support does not exist even as an unimplemented stub. Xinu, NetBSD, and Embox have no RP2040 board/platform anywhere in their trees. See section 5. |

## 1. RetroBSD (github.com/RetroBSD/retrobsd)

**CPU architecture / chips.** RetroBSD targets the MIPS32 M4K core used in
Microchip PIC32 (PIC32MX family) microcontrollers. The repository README
describes the project as "RetroBSD is 2.11BSD ported to PIC32
microcontrollers ... running on a chip with no MMU and no external memory,"
and the machine-dependent tree contains a single architecture directory,
`sys/pic32/`, holding MIPS32-specific source (`machparam.h`, `machdep.c`,
`startup.S`, `elf_machdep.h`, `pic32mx.h`).
Source: https://github.com/RetroBSD/retrobsd/blob/master/README.md ;
https://github.com/RetroBSD/retrobsd/blob/master/sys/pic32/machparam.h

**Minimum RAM.** Stated as 128 KB. The README's tagline reads "A real Unix
that fits in 128 kbytes of RAM," and its example boot log prints `RAM size:
128 kbytes` / `phys mem = 128 kbytes`. `sys/pic32/machparam.h` hard-codes a
32 KB kernel reservation (`KERNEL_DATA_SIZE (32*1024)`) against a 128 KB
total (`DATA_SIZE (128*1024)`), leaving roughly 96 KB for user processes.
Source: README.md; `sys/pic32/machparam.h`.

**MMU / memory protection.** None. The README states explicitly the target
chip has "no MMU and no external memory." `sys/pic32/machparam.h` defines
fixed physical addresses for kernel and user memory
(`KERNEL_DATA_START 0x80000000`, `KERNEL_FLASH_START 0x9d000000`,
`USER_DATA_START ...`) rather than any page-table or TLB abstraction, and
`sys/include/vmparam.h` contains no paging/protection-bit definitions.
`sys/pic32/machdep.c` programs the MIPS Config0 coprocessor register
directly (comment: "Config register: enable kseg0 caching"), which only has
meaning given MIPS's hard-wired kseg0/kseg1 unmapped-segment convention (the
PIC32 M4K core has no TLB).
Source: README.md; `sys/pic32/machparam.h`; `sys/pic32/machdep.c` (~line
268).

**RP2040 / ARM Cortex-M0+ port.** None found. A GitHub issue/PR search
(`gh search issues/prs --repo RetroBSD/retrobsd` for "RP2040", "Pico",
"Cortex-M0", "ARM") returned zero results. Listing all 61 forks
(`gh api repos/RetroBSD/retrobsd/forks`) shows only one
architecture-divergent fork, `brentharts/retrobsd` ("RISC-V RetroBSD
Operating System") -- no ARM or RP2040 fork exists. A general web search for
"RetroBSD RP2040" returned no matches.
Source: `gh api repos/RetroBSD/retrobsd/forks`; GitHub code/issue search;
WebSearch (no primary source found, which is itself the finding -- absence
confirmed by exhaustive search of the canonical repo's forks/issues/PRs).

**Why a port is not a config change (inference).** The machine-dependent
layer is written directly against MIPS32 primitives with no HAL boundary:
fixed absolute addresses map onto MIPS kseg0 (cached, unmapped) and
kseg1/kuseg conventions, and `machdep.c` accesses C0 coprocessor registers
(`C0_CONFIG`, `C0_STATUS`) by name throughout `sys/pic32/*.S` and `*.c`.
ARMv6-M has no C0 coprocessor, no kseg0/kseg1 concept, and a different
exception/vector model. This is inference drawn from reading those files,
not a quoted claim from the project: it means an RP2040 port would require
rewriting the entire `sys/pic32/` machine-dependent layer (startup code,
exception handling, address layout, register access), not adjusting a
config file.

**VERDICT: REQUIRES A PORT.** Technical blocker: MIPS32 M4K-specific
machine-dependent code (C0 coprocessor register access, hard-coded
kseg0/kseg1-style fixed addressing) with no equivalent on ARMv6-M, and no
existing ARM/RP2040 work anywhere in the project's forks, issues, or PRs.

## 2. DiscoBSD

**Canonical repository.** The prompt's example URLs
(`github.com/DiscoBSD/DiscoBSD`, `github.com/sergev/DiscoBSD`) do not exist.
The real, actively maintained upstream is
**https://github.com/chettrick/discobsd** (created 2021-02-18, last push
2026-08-12, 244 stars, maintained by Christopher Hettrick) -- confirmed as
canonical by checking that the other candidate URLs 404 and that GitHub
repo search surfaces only this repository plus stale, unmaintained clones.
Source: https://github.com/chettrick/discobsd ; `gh api
repos/chettrick/discobsd`.

**Boards/MCUs supported (exact list from the project's own docs).**
- ARM (STM32), from `distrib/stm32/README.md`: WeAct STM32F405RGT6,
  NUCLEO-F411RE, STM32F412G-DISCO, WeAct STM32F412RET6, STM32F413H-DISCO,
  NUCLEO-F446RE, WeAct STM32F446RET6, STM32F469I-DISCO, STM32F4DISCOVERY,
  DevEBox STM32F407VET6.
- MIPS (PIC32), from `distrib/pic32/README.md`: Fubarino SD, Olimex
  Duinomite / Duinomite-Mini / Duinomite-Mega / Duinomite-eMega, Olimex
  Pinguino-Micro (PIC32MX795F512H), Maximite / Colour Maximite, Majenko
  SDXL, 4D Systems Picadillo-35T, MikroElektronika MultiMedia Board
  (PIC32MX7), chipKIT Max32/WF32, Sparkfun UBW32, Microchip Explorer 16,
  PIC32 USB/Ethernet Starter Kit, Pontech Quick240.
Source: `distrib/stm32/README.md`; `distrib/pic32/README.md` in
chettrick/discobsd.

**Cortex-M0+/ARMv6-M support.** Not present. Every ARM board listed is an
STM32F4xx part, i.e. Cortex-M4 / ARMv7E-M; the non-ARM boards are PIC32MX
(MIPS). The ARM CMSIS core-access header in the tree,
`sys/arch/arm/include/core_cm4.h`, is titled "CMSIS Cortex-M4 Core
Peripheral Access Layer" -- there is no Cortex-M0 CMSIS file anywhere in the
repository.
Source: `distrib/stm32/README.md`; `sys/arch/arm/include/core_cm4.h`.

**RP2040/Pico port or discussion.** None. No open or closed issue/PR
mentions RP2040 or Pico (`gh api "search/issues?q=repo:chettrick/discobsd+RP2040"`
returned no matches). Issue #17, "RP2350 support" (open since 2024-08-08),
explicitly calls RP2350 the "RP2040 successor" and describes its cores as
"2x Cortex-M33F" -- i.e., it is a request for the *different*, newer RP2350
chip (ARMv8-M, has an MPU/TrustZone), not the RP2040, and remains
unimplemented. Issue #1 requests unrelated ATSAMD51 (Cortex-M4) support. A
WebSearch for "DiscoBSD RP2040" and "DiscoBSD Pico" returned no results
connecting the two projects.
Source: https://github.com/chettrick/discobsd/issues/17 ;
https://github.com/chettrick/discobsd/issues/1 ; GitHub issue search.

**Relationship to RetroBSD.** The project's own README states DiscoBSD "is
an independent continuation of RetroBSD, a 2.11BSD-based OS targeting the
MIPS-based PIC32MX7," begun as a 2020 University of Victoria directed-study
project porting RetroBSD to Cortex-M4. Different author from RetroBSD
(Serge Vakulenko wrote RetroBSD; Christopher Hettrick maintains DiscoBSD).
The PIC32/MIPS code descends from RetroBSD directly; the STM32/ARM code is
new work built for DiscoBSD.
Source: `README.md`, "History" section, chettrick/discobsd.

**Memory-protection mechanism on its ARM boards.** DiscoBSD's STM32 port
uses the ARMv7-M optional Memory Protection Unit (MPU), not an MMU.
`sys/arch/stm32/stm32/mpu.c` implements a `mpu_sysctl()` handler calling
ST HAL functions `LL_MPU_IsEnabled()`, `LL_MPU_GetCtrl()`,
`LL_MPU_GetNumRegions()`, `LL_MPU_GetSeparate()` from
`sys/arch/stm32/hal/stm32f4xx_ll_cortex.h`, and
`sys/arch/stm32/include/mpuvar.h` defines corresponding sysctl names
(`enable`, `ctrl`, `nregions`, `separate`). The ARMv7-M MPU (8 or 16
configurable regions) is architecturally absent from ARMv6-M cores: the
RP2040's Cortex-M0+ has no MPU register block at all.
Source: `sys/arch/stm32/stm32/mpu.c`; `sys/arch/stm32/include/mpuvar.h`.

**VERDICT: IMPOSSIBLE WITHOUT MAJOR WORK.** DiscoBSD's only ARM port hard-
depends on ARMv7-M MPU hardware for its memory-protection model, and the
RP2040's Cortex-M0+ cores (ARMv6-M) have no MPU and, per the shared RetroBSD
lineage, no MMU either. Getting DiscoBSD running would mean designing a new
software-only protection scheme or shipping an unprotected single-address-
space kernel -- a from-scratch port, not a configuration change. Note: the
project's only stated interest in an RP-series chip (issue #17) is for the
RP2350 (Cortex-M33F, which does have an MPU/TrustZone), not the RP2040.

## 3. FUZIX (github.com/EtchedPixels/FUZIX)

**Platform directory.** The live `Kernel/platform/` directory (139 entries)
contains a directory named exactly `platform-rpipico`. (A separate,
unrelated `platform-pico68k` directory also exists, for a 68000-based
single-board computer -- confirmed via its own README, which describes "a
small 68000 board" with 64 or 128 KB RAM -- and is not related to the
RP2040.)
Source: `gh api repos/EtchedPixels/FUZIX/contents/Kernel/platform` ;
https://raw.githubusercontent.com/EtchedPixels/FUZIX/master/Kernel/platform/platform-pico68k/README

**Memory model and constraints.** The platform README states the target
board has "two Cortex-M0+ cores, 2MB of onboard NAND flash ... and 264kB of
RAM," with "enough memory to run four or five processes at once,"
extendable to roughly 15 processes via SD-backed swap. `config.h` shows no
bank switching is used: `#define CONFIG_BANKS 1` (commented "Pure swap"),
plus `CONFIG_32BIT` and `CONFIG_USERMEM_DIRECT` -- a flat 32-bit user
address space. Each process is capped at `PROGSIZE = 65536 - UDATA_SIZE`
(64 KB max). `TOTALMEM` defaults to 160 (KB) of the chip's 264 KB SRAM.
Source: `Kernel/platform/platform-rpipico/README.md`;
`Kernel/platform/platform-rpipico/config.h`.

**Completeness / maturity.** Commit history against this platform directory
(`gh api repos/EtchedPixels/FUZIX/commits?path=Kernel/platform/platform-rpipico`)
shows active development through at least 2025-04-23, including RP2350/Pico
2 support and "rudimentary net support." The README describes a working
userland (Cortex-M0 ELF PIE binaries, an `fforth` interpreter, games under
`/usr/games`, filesystem installers) and an explicit "Issues" section
noting, for example, that CPU exceptions are not yet fully mapped to Unix
signals. This indicates the port boots to a functioning shell but is not
feature-complete.
Source: `gh api repos/EtchedPixels/FUZIX/commits?path=Kernel/platform/platform-rpipico`;
`Kernel/platform/platform-rpipico/README.md`, "Issues" section.

**Storage.** Two block devices: `/dev/hda` is the onboard QSPI/NAND flash
accessed through the Dhara flash-translation-layer library (wear-leveled,
trim-aware) and holds the root filesystem; `/dev/hdb` is an optional SD
card over SPI (explicit pin table given in the README), DOS-partition-table
aware, supporting filesystems up to 32 MB. `config.h` confirms
`CONFIG_PICO_FLASH` and `CONFIG_SD`.
Source: `Kernel/platform/platform-rpipico/README.md`;
`Kernel/platform/platform-rpipico/config.h`.

**General FUZIX design.** The top-level README contrasts FUZIX against UZI
(a flat, single-64K-process ancestor), explaining that FUZIX supports
larger programs via "banked ROM or similar," and lists supported
architectures spanning 8-bit chips through arm32, esp8266, MSP430, and
pdp11 -- confirming FUZIX is designed for MMU-less 8/16-bit and small 32-bit
targets generally, not built around any MMU requirement.
Source: https://raw.githubusercontent.com/EtchedPixels/FUZIX/master/README.md,
"What does UZI have over FUZIX" section.

**VERDICT: SUPPORTED NATIVELY.** An active, maintained
`Kernel/platform/platform-rpipico` exists using flat 32-bit addressing and
swap (not bank switching), boots to a working shell/userland, and stores
its root filesystem on the RP2040's onboard flash with optional SD
expansion. It is not fully mature -- signal delivery from CPU exceptions is
an open issue, and each process is capped at 64 KB.

## 4. Apache NuttX (github.com/apache/nuttx)

**Board directory.** Confirmed directly via `gh api
repos/apache/nuttx/contents/boards/arm/rp2040` (live tree listing, `master`
branch, checked 2026-09-11): the directory exists and contains, among
others, an exact `raspberrypi-pico` subdirectory, alongside
`raspberrypi-pico-w`, `adafruit-feather-rp2040`, `adafruit-kb2040`,
`adafruit-qt-py-rp2040`, `pimoroni-tiny2040`, `seeed-xiao-rp2040`,
`w5500-evb-pico`, `waveshare-rp2040-lcd-1.28`, `waveshare-rp2040-zero`, and
a shared `common` directory.
Path: `boards/arm/rp2040/raspberrypi-pico`
Source: `gh api repos/apache/nuttx/contents/boards/arm/rp2040` (directly
queried; each entry's `html_url` resolves to
`https://github.com/apache/nuttx/tree/master/boards/arm/rp2040/<name>`).

**Chip/architecture directory.** Confirmed: `arch/arm/src/rp2040/` exists
and contains `Kconfig`, `CMakeLists.txt`, and RP2040-specific source (chip
startup, clocking, peripheral drivers).
Source: `gh api repos/apache/nuttx/contents/arch/arm/src/rp2040`.

**NSH (NuttShell, POSIX-like shell).** Confirmed: the board's `configs/`
directory (`gh api
repos/apache/nuttx/contents/boards/arm/rp2040/raspberrypi-pico/configs`)
lists, among 19 defconfigs, `nsh`, `nsh-flash`, `nshsram`, and `usbnsh` --
i.e. multiple ready-made NSH build configurations for this exact board,
including one that runs from external flash (`nsh-flash`) and one that runs
entirely from on-chip SRAM (`nshsram`).
Source: `gh api
repos/apache/nuttx/contents/boards/arm/rp2040/raspberrypi-pico/configs`.

**License.** Confirmed: the repository's root `LICENSE` file begins with
the standard "Apache License" (Version 2.0) header text.
Source: `https://raw.githubusercontent.com/apache/nuttx/master/LICENSE`
(fetched directly, first lines read "Apache License", consistent with
Apache NuttX being an Apache Software Foundation top-level project).

**Running from 2 MB flash with 264 KB RAM.** Directly inferred from the
defconfig names present for this exact board: an `nsh-flash` config
(execute from external QSPI flash, XIP) and a separate `nshsram` config
(execute entirely from the 264 KB on-chip SRAM) both exist for
`raspberrypi-pico`, meaning NuttX's own build system offers both modes as
supported, working configurations for this hardware. The individual
defconfig files were not read line-by-line for exact memory-map figures in
this pass; that a dedicated flash-XIP defconfig and a dedicated all-SRAM
defconfig both ship for this board is itself the primary-source evidence
that both fit the chip's real 2 MB flash / 264 KB RAM budget.
Source: `gh api
repos/apache/nuttx/contents/boards/arm/rp2040/raspberrypi-pico/configs`
(defconfig names `nsh-flash`, `nshsram`).

**Self-description.** Not independently re-verified in this pass beyond
what is well established in NuttX's own documentation and widely mirrored
on its official site (nuttx.apache.org): NuttX describes itself as a
real-time embedded operating system with POSIX- and ANSI-standard APIs.
Marked **unverified in this exact session** insofar as the project
homepage text was not re-fetched here; the NSH-defconfig evidence above
(a POSIX-style interactive shell shipping for this exact board) is the
directly confirmed primary-source fact this report relies on for
"Unix-like" characterization.

**VERDICT: SUPPORTED NATIVELY.** Confirmed via the live upstream tree: a
dedicated `boards/arm/rp2040/raspberrypi-pico` board directory and
`arch/arm/src/rp2040` chip support exist, multiple NSH-shell defconfigs
ship for this exact board (including flash-XIP and all-SRAM variants), and
the project is licensed Apache License 2.0.

## 5. Other genuinely Unix-like / BSD-derived candidates

Investigated and checked against primary sources; none besides FUZIX and
NuttX (covered above) has a genuine native RP2040 port.

- **Xinu** (Purdue teaching OS, `xinu-os/xinu`). The live
  `compile/platforms` / `system/platforms` directories list exactly:
  `arm-qemu`, `arm-rpi`, `e2100l`, `mipsel-qemu`, `wl330ge`, `wrt160nl`,
  `wrt54gl`, `x86`. `arm-rpi`/`arm-qemu` target ARM1176/BCM2835-class
  (MMU-equipped) hardware, not ARMv6-M. No `rp2040`/`pico`/Cortex-M0
  directory exists anywhere in the tree.
  Source: `gh api repos/xinu-os/xinu/contents/compile/platforms`.
  Not found -- confirmed by direct tree listing.

- **Linux nommu (ARM)**. Mainline `arch/arm/mm/Kconfig` defines `CPU_V7M`
  but contains no `CPU_V6M` symbol anywhere in the file (grepped in full);
  `arch/arm/Kconfig` and `arch/arm/kernel/head-nommu.S` likewise have zero
  matches for `CPU_V6M` or Cortex-M0/RP2040. The in-tree ARMv7-M nommu
  platforms (`ARCH_LPC18XX`, `ARCH_MPS2`) are Cortex-M3/M4 only. This means
  Cortex-M0/M0+ (ARMv6-M) support does not exist even as an unimplemented
  Kconfig stub in mainline Linux -- the structural hook for it is absent,
  which is a stronger statement than "unimplemented."
  Source: `https://raw.githubusercontent.com/torvalds/linux/master/arch/arm/mm/Kconfig`,
  `arch/arm/Kconfig`, `arch/arm/kernel/head-nommu.S` (grepped directly on
  the `master` branch).
  IMPOSSIBLE WITHOUT MAJOR WORK -- the ARMv6-M nommu CPU-support code would
  need to be written from scratch upstream.

- **NetBSD**. The official ports lists (`netbsd.org/ports/`,
  `wiki.netbsd.org/ports/`) enumerate 58-61 ports (evbarm, evbmips,
  acorn32, alpha, vax, etc.), all MMU-class CPUs or full SBCs; there is no
  nommu/microcontroller tier and no RP2040/Pico entry.
  Source: https://www.netbsd.org/ports/ ; https://wiki.netbsd.org/ports/.
  Not found. (That NetBSD's pmap/uvm subsystem assumes an MMU is inferred
  from the absence of any non-MMU port category, not from a direct
  statement in these pages.)

- **collapseOS and other hobby/Forth OSes**. collapseOS (canonical source
  at collapseos.org / sourcehut `~vdupras/collapseos-again`) targets Z80,
  8086, 6809, 6502, and AVR programming only -- no ARM support exists even
  in principle. `lurk101/pshell` (a Pico-targeted shell with LittleFS and a
  small C compiler) was checked directly and is a single-tasking monitor
  with no process model and no POSIX claim, so it does not qualify as
  Unix-like. Embox (genuinely POSIX-compliant, BSD-2-Clause licensed) was
  checked board-by-board (`templates/arm/*`, `src/arch/*`): STM32F4/F7,
  OMAP, TI816x, vexpress-a9 are present; no `rp2040`/`pico` entry exists
  anywhere in its tree.
  Source: https://collapseos.org/ ; https://github.com/lurk101/pshell ;
  `gh api` contents listings on `templates/arm` and `src/arch` in
  `embox/embox`.
  Not found.

- **RIOT-OS**. Has an official `rpi_pico` board and is frequently described
  as offering partial POSIX-like APIs, but its concurrency model is
  cooperative/preemptive threads, not Unix processes with fork/exec, and it
  does not present a Unix-style shell/filesystem by default. Excluded per
  this report's definition of "genuinely Unix-like" (Unix-style
  process/file/shell semantics or a direct BSD/Unix derivative); noted here
  for completeness rather than included as a finding.

- **Notable non-qualifying curiosity: Pico_1140**
  (`github.com/Isysxp/Pico_1140`). This is a PDP-11/40 emulator (derived
  from Dave Cheney's CPP11) that runs natively on RP2040 hardware and, as
  guest software, boots genuine Unix V5/V6 and mini-unix from an SD card.
  Verified via its own README. It is explicitly an RP2040-native *emulator*
  running 1970s Unix as a guest, not a Unix-like OS ported to run directly
  on the RP2040's own instruction set -- flagged here as a footnote, not a
  finding, since it does not meet the "native port" bar the other entries
  are held to.
  Source: https://github.com/Isysxp/Pico_1140 (README).

**Conclusion for this section:** no additional genuinely Unix-like or
BSD-derived OS with a real, verifiable native RP2040 port was found beyond
FUZIX and Apache NuttX.

## What was not independently re-verified in this pass

- NuttX's own self-description as a POSIX/ANSI-standard-API RTOS was not
  re-fetched from nuttx.apache.org in this session; it rests on the
  NSH-defconfig evidence above plus well-established public knowledge of
  the project, not a URL fetched and quoted in this report. Marked
  unverified for this specific claim.
- Exact per-defconfig memory-map figures (stack/heap/flash-partition sizes
  in bytes) for the `raspberrypi-pico` NuttX `nsh-flash` and `nshsram`
  configs were not read line-by-line; the finding rests on the existence of
  both defconfig names in the live upstream tree, which is primary-source
  evidence that both configurations are shipped and presumably build/fit,
  but is one step short of reading the literal linker-script numbers.
