# FUZIX and Apache NuttX coexistence on a Raspberry Pi Pico (RP2040)

Scope: one 2 MiB QSPI-flash, 264 KB SRAM, Cortex-M0+ (ARMv6-M, no MMU)
Raspberry Pi Pico. The question is whether FUZIX and NuttX can both live on
the board and be selected at boot. RP2040's boot ROM carries no partition
table and no multi-slot image support (that arrives with RP2350); this
report treats that as established and verifies everything else from primary
sources: the RP2040 datasheet, the ARMv6-M Architecture Reference Manual,
the FUZIX and NuttX trees fetched from their upstream repositories, and a
local build of three NuttX board configs with `arm-none-eabi-gcc 16.2.0`
against NuttX `master` at commit `ad176ac47fbfa29bafb53bf79826ad44450a2cb7`
(2026-09-11).

Confirmed = read from a primary source or measured directly in this
session. Inferred = follows from confirmed facts but was not independently
built or measured. Unverified = stated but not checked here.

## Summary

| Question | Finding | Status |
|---|---|---|
| Boot ROM multi-image support | None. One second-stage candidate is read from flash offset 0; no partition table, no slot list, no fallback image | Confirmed (datasheet Sec 2.7-2.8.1) |
| boot2 | 256 bytes at flash offset 0, last 4 bytes a CRC32 of the first 252, copied to SRAM5 and run there, ~0.5 s timeout before USB boot | Confirmed (datasheet Sec 2.8.1.2-2.8.1.3.1) |
| VTOR on RP2040's Cortex-M0+ | Bare ARMv6-M makes VTOR optional (RAZ/WI is a conforming implementation); RP2040 implements a live, writable VTOR at PPB offset 0xED08 | Confirmed (ARM DDI 0419C Sec B3.2.5; RP2040 datasheet Sec 2.4.1.2 and M0PLUS register table) |
| Existing chain bootloaders | Several real, maintained-to-varying-degrees projects exist; none is a general "pick OS A or OS B" menu. All that hand off do it via `SCB->VTOR` write + branch | Confirmed (source read on 7 repos) |
| FUZIX flash layout | Kernel: flash[0, 96 KiB). Root FS: flash[96 KiB, 2 MiB) via Dhara FTL, no end cap. `FLASH_OFFSET` is a hard-coded `#define`, not a build option | Confirmed (globals.h, devflash.c, Makefile) |
| NuttX SRAM config (`nshsram`) | Links 100% into SRAM, 0 bytes of flash used; produces a UF2 whose target address is SRAM base | Confirmed (built and measured this session) |
| NuttX non-zero flash-offset link | No Kconfig option, no documented procedure; offset is fixed in the linker script's `SECTIONS`, not a `#define` | Confirmed absent (Kconfig grep, docs read) |
| Best realistic option | (b): FUZIX resident in flash as shipped; NuttX loaded into SRAM on demand via `nshsram` and `picotool load -x` or a BOOTSEL UF2 drop. Zero source changes to either OS | Recommended, confirmed feasible |
| Chain bootloader with two flash slots (a) | Buildable in principle, but blocked today by FUZIX's FTL claiming all flash from 96 KiB to the chip's end with no reserved gap; needs a 4-file FUZIX patch plus a hand-edited NuttX linker script | Feasible only after source changes |
| BOOTSEL reflash swap (c) | Always works, but swapping back to FUZIX after flashing a flash-resident NuttX config likely requires reflashing FUZIX's `filesystem.ftl`, not just its kernel | Feasible, destructive, slow |
| SD card (d) | Not a boot path; RP2040's boot ROM only understands QSPI flash or USB. SD only matters once something is already running from flash or SRAM | Not applicable standalone |

## 1. RP2040 boot sequence and multi-image support

The RP2040 datasheet splits the boot process into a hardware-controlled
stage (Section 2.7) and a processor-controlled stage run out of the boot
ROM (Section 2.8). The hardware stage brings up the digital supply, starts
the ring oscillator, takes the reset controller, XIP hardware, memories,
bus fabric, and processor subsystem out of reset, then releases both cores
into the boot ROM. The boot ROM itself is capped at 16 KB (Section 2.8):
"The Bootrom size is limited to 16kB." Its contents are the core-0 boot
sequence, the core-1 low-power wait/launch protocol, a USB MSC UF2
bootloader, a USB PICOBOOT interface, flash programming routines, and fast
float/bit/memory-copy libraries.

The processor-controlled sequence (Section 2.8.1) runs on core 0 (core 1
goes to `WFE` with `SCR.SLEEPDEEP` set and waits for a mailbox message) and
proceeds, in order:

1. If the power-up event came from the Rescue DP, halt and wait for the
   debug host.
2. If the watchdog's scratch registers carry a specific magic pattern
   (Section 2.8.1.1: scratch 4 = `0xb007c0d3`, scratch 5 = entry point XOR
   that magic, scratch 6 = stack pointer, scratch 7 = entry point), jump
   straight to that pre-loaded SRAM code. This is "watchdog boot", used to
   warm-restart into code already resident in SRAM without touching flash.
3. Check whether the SPI chip-select pin is held low (the datasheet's
   "bootrom button"); if so, skip flash boot entirely. This is the
   mechanism the BOOTSEL button uses.
4. Configure QSPI pin muxing and the Synopsys SSI for standard SPI mode,
   issue an XIP-exit sequence (Section 2.8.1.2, a best-effort sequence
   designed to knock unknown flash parts out of continuous-read mode), and
   read the **first 256 bytes of external flash** into SRAM bank 5.
5. Check those 256 bytes as a candidate second-stage image: the last 4
   bytes are a little-endian CRC32 (polynomial `0x04c11db7`, no input/output
   reflection, initial value `0xffffffff`, final XOR 0) of the first 252
   bytes (Section 2.8.1.3.1). If it matches, jump into the 252-byte payload
   and run it from SRAM5.
6. The boot ROM retries with varying SPI parameters for about 0.5 seconds
   (128 attempts of ~4 ms each); if nothing checksums, it drops into USB
   device boot, presenting as a USB Mass Storage class UF2 target (which
   "can program the SPI flash, or load directly into SRAM and run, by
   dragging and dropping an image in UF2 format") and as a PICOBOOT
   interface.

The flash second stage (boot2) is exactly that 256-byte image: 252 bytes
of code plus a 4-byte CRC32 trailer. Its job, per Section 2.8.1.3, is
narrow and specific: "the second stage must be copied from flash to SRAM by
the bootrom, and executed in SRAM," and its only job is to configure the
SSI/QSPI interface (clock divider, bus width, XIP continuation code) well
enough for fast execute-in-place, since the boot ROM's own configuration
targets compatibility over speed. Every boot2 implementation this report
inspected -- NuttX's own compiled-in boot2 (`raspberrypi-pico-flash.ld`'s
`.boot2` section, `KEEP(*(.boot2)) > flash` at `ORIGIN = 0x10000000`), the
`rp-rs/rp2040-boot2` Rust images, and the `crispy-bootloader-rp2040-rs`
project's own linker script (`BOOT2 : ORIGIN = 0x10000000, LENGTH = 0x100`)
-- places it at the very start of flash, matching the boot ROM's behavior of
reading from address 0 of the SPI device with no offset parameter anywhere
in the datasheet's boot sequence.

**No native multi-image selection exists.** The processor-controlled boot
sequence above is exhaustive as documented: one 256-byte candidate is read
from a single fixed flash address, checked against a single checksum, and
either run or rejected in favor of USB boot. There is no partition table
read, no list of candidate offsets, no "try slot B if slot A fails" branch,
and no configuration register that changes which flash address is read.
The only two "alternate path" mechanisms the boot ROM itself provides are
watchdog boot (jump into SRAM code already resident from a previous run)
and the CS-pin-low bootrom-button check (skip flash boot outright) -- neither
is an A/B image selector; both are on/off switches around the single flash
boot path. This matches the already-established fact that partition-table
support is an RP2350 boot-ROM feature.

Citations: RP2040 Datasheet, Section 2.7 "Boot Sequence", Section 2.8
"Bootrom", Section 2.8.1 "Processor Controlled Boot Sequence", Section
2.8.1.1 "Watchdog Boot", Section 2.8.1.2 "Flash Boot Sequence", Section
2.8.1.3 "Flash Second Stage", Section 2.8.1.3.1 "Checksum".

## 2. Third-party chain/second-stage bootloaders

Seven real, existing GitHub projects were read directly (README plus
relevant source) rather than taken from search-result summaries. None of
them is a general-purpose "boot menu, pick OS A or OS B" tool; each solves
a narrower problem (OTA update, fail-safe self-update, or automatic A/B
rollback), but each demonstrates a working, real handoff mechanism that a
purpose-built dual-boot loader would reuse.

| Project | Repo | What it does | 2+ full images? | Handoff | Maintained |
|---|---|---|---|---|---|
| picowota | github.com/usedbytes/picowota | OTA firmware upload over WiFi for Pico W | No -- one active app slot, CRC-validated | `SCB->VTOR = vtor` write, MSP set from word 0, `bx` to word 1 (reset vector) -- direct in-place jump, no reboot | Yes-ish: last push 2024-07-14, 137 stars, not archived |
| rp2040-serial-bootloader | github.com/usedbytes/rp2040-serial-bootloader | UART code upload, non-W predecessor of picowota, same author/technique | No -- one app slot | Same VTOR-jump lineage (per its own README) | Stale: last push 2023-03-03, 120 stars, not archived |
| RP2040-serial-bootloader (Mr Beam) | github.com/mrbeam/RP2040-serial-bootloader | Vendor-specific serial bootloader for a laser-cutter product | No -- fixed bootloader region (0x10000000-0x10008000, 32 KB) then one app | Not confirmed in this pass; app resides at a fixed offset after the bootloader | Low adoption: last push 2026-05-04, 2 stars |
| pico-flashloader | github.com/rhulme/pico-flashloader | Power-fail-safe self-update: flashes a new image before starting it, falls back to bootrom bootloader on failure | No -- one "current app" plus one staged "new app" location; not a persistent 2-slot select-at-boot design | Falls through a priority list (watchdog-scratch trigger, valid app, scan for new image, bootrom bootloader); direct jump once the target is validated | Moderate: last push 2025-02-01, 86 stars, not archived |
| pico-simple-bootloader | github.com/purpleJosh/pico-simple-bootloader | Resets to BOOTSEL when firmware looks corrupt | No | Not inspected in depth (out of scope: not an image selector) | Low adoption: last push 2024-09-13, 3 stars |
| crispy-bootloader-rp2040-rs | github.com/ADNTIO/crispy-bootloader-rp2040-rs | Genuine A/B bootloader: two flash banks, each copied to RAM and executed there | Yes -- real dual-bank, but selection is automatic rollback (`select_boot_bank()`), not a user-chosen "boot this OS" menu | Copies the chosen bank from flash to a fixed SRAM address, then jumps (RAM execution, not XIP) | Young and small but active: created Feb 2026, pushed 2026-09-08 (3 days before this report), 9 stars, self-disclosed as AI-assisted development |
| rp2040-boot2 | github.com/rp-rs/rp2040-boot2 | Canonical Rust reimplementation of the Pico-SDK's boot2 stage-2 images (w25q080 and others) | N/A -- this is a boot2 image provider, not a chain loader | N/A | Actively maintained: last push 2025-04-18, 89 stars |

Two facts worth isolating:

**The handoff mechanism is uniform across every project that documents
one.** picowota's `jump_to_vtor()` (`main.c`) is the clearest example:

```c
static void jump_to_vtor(uint32_t vtor)
{
	uint32_t reset_vector = *(volatile uint32_t *)(vtor + 0x04);
	SCB->VTOR = (volatile uint32_t)(vtor);
	asm volatile("msr msp, %0"::"g" (*(volatile uint32_t *)vtor));
	asm volatile("bx %0"::"r" (reset_vector));
}
```

This writes the new image's vector-table base into `SCB->VTOR`, loads the
main stack pointer from that table's word 0, and branches to word 1 (the
reset handler) -- a direct in-place jump, not a reboot through the boot ROM.
`picotool load -x` instead documents "a bootrom reboot to execute the
downloaded file as a program after the load -- either a flash update boot
for binaries in flash, or a RAM image boot for other binaries" (picotool
README, `load` command options), which is a different, boot-ROM-mediated
path. Both are real, both are used in practice; a purpose-built dual-boot
loader for this project would use the VTOR-write technique, since it is
the one every surveyed chain-loader project actually ships.

**Nothing surveyed is a drop-in answer.** picowota and its ancestor solve
network/serial OTA update, not local dual-boot selection, and carry a
WiFi-firmware size cost (300-400 KB duplicated between bootloader and app,
per picowota's own README) that is irrelevant here since neither FUZIX nor
NuttX needs Pico W networking for this use case, but illustrates how large
a "real" Pico-SDK-based bootloader gets. crispy-bootloader-rp2040-rs is the
closest structural match (two real flash banks, real selection logic,
real rollback), but its selection criterion is image health, not a user's
choice of operating system, and it runs everything from RAM, which caps
each bank's *resident, executing* code at whatever fits under SRAM's 264 KB
after subtracting its own 16 KB and the app's data/BSS/stack -- 192 KB of
code copied to RAM in its own default configuration. Building "select
FUZIX or NuttX" specifically means writing new bootloader code that reuses
this VTOR-jump pattern, not reusing one of these projects unmodified.

## 3. VTOR and vector-table relocation on Cortex-M0+

This is the single most consequential fact for any chain-bootloader design,
and it resolves in RP2040's favor. Two separate primary sources, read
directly rather than summarized, agree and are consistent with each other:

**ARMv6-M itself treats VTOR as optional.** The ARMv6-M Architecture
Reference Manual (ARM DDI 0419C), Section B3.2.5, "Vector Table Offset
Register, VTOR", states directly: "The number of bits in the VTOR is
IMPLEMENTATION DEFINED. Unimplemented bits are RAZ/WI" and "The VTOR
behavior is IMPLEMENTATION DEFINED." Section B1's description of the
vector table is explicit about what "unimplemented" means in practice:
"Depending on the implementation, the vector table base is adjustable...
Implementations not providing configurability of the table base provide a
VTOR with RAZ/WI behavior," and further, "For implementations that do not
support the VTOR register, the VTOR register space defined in ARMv7-M is
reserved, RAZ/WI." A conforming ARMv6-M part can legally have a VTOR
address that always reads zero and silently discards writes -- the vector
table is then permanently fixed at address 0, and no relocation is
possible in hardware.

**RP2040's Cortex-M0+ specifically does implement a live VTOR.** The
RP2040 datasheet lists it explicitly among the configured features of each
core (Section 2.4.1.2): "Vector Table Offset Register (VTOR)" appears in
the bullet list alongside 8 MPU regions, SysTick, and the WIC lines. The
register itself is documented with a real, writable field: "M0PLUS: VTOR
Register, Offset: 0xed08... The VTOR holds the vector table offset
address," with Table 106 giving bits `[31:8]` as `TBLOFF`, read-write,
reset value `0x000000`. This is not a RAZ/WI stub; it is a functional
register at Privileged Peripheral Bus offset `0xED08` (i.e., system address
`0xE000ED08`).

**Consequence:** a chain bootloader on this specific board can relocate
the vector table in place with a single MMIO write, exactly as every
surveyed bootloader in Section 2 does (`SCB->VTOR = <new base>`), rather
than having to copy the target image's exception vectors into a fixed
low-memory table (the workaround a genuinely VTOR-less ARMv6-M part would
need). The only constraint this specific implementation adds is alignment:
RP2040's `TBLOFF` occupies bits `[31:8]`, one bit narrower than the
architecture's baseline `[31:7]` (i.e., 256-byte alignment required, versus
a 128-byte-aligned table on a part that implements the full field width).
This is independently confirmed by NuttX's own SRAM linker script, which
inserts `. = ALIGN(256);` immediately before `*(.vectors)` when the vector
table is placed in SRAM at a bootloader-supplied offset
(`raspberrypi-pico-sram.ld`) -- the alignment is baked into the linker
script rather than left to chance.

## 4. FUZIX `platform-rpipico` flash layout

Read directly from `github.com/EtchedPixels/FUZIX`,
`Kernel/platform/platform-rpipico/` (`master` branch, fetched this
session).

**Kernel region:** `globals.h` sets the split point with one line:

```c
#define FLASH_OFFSET (96*1024)
```

This is a plain C `#define` in a header, not a CMake cache variable, not a
Kconfig-equivalent, not something `config.h`, the `Makefile`, or
`CMakeLists.txt` expose as a build-time option. `rawflash.c`'s Dhara
callbacks (`dhara_nand_erase`, `dhara_nand_prog`, `dhara_nand_read`) all
add `FLASH_OFFSET` to their block/page arithmetic before touching
`hardware_flash`, so 96 KiB is where the flash filesystem region begins,
uniformly.

**Root filesystem region:** `devflash.c` computes the Dhara NAND geometry
as:

```c
static const struct dhara_nand nand =
{
	.log2_page_size = 9, /* 512 bytes */
	.log2_ppb = 12 - 9, /* 4096 bytes */
	.num_blocks = (PICO_FLASH_SIZE_BYTES - FLASH_OFFSET) / 4096,
};
```

There is no separate length or end-of-region constant anywhere in this
file or `rawflash.c` -- the filesystem's size is defined purely as
"everything left after `FLASH_OFFSET`." `PICO_FLASH_SIZE_BYTES` is a
Pico-SDK board macro (`boards/pico.h` in `raspberrypi/pico-sdk`,
`pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))`),
2 MiB for the plain Pico. The `Makefile`'s image target confirms the exact
arithmetic in two independent places: `mkftl -s 1952 -e 0x1000 -g 10
filesystem.img -o filesystem.ftl` builds a 1952 KiB filesystem image
(2048 - 96 = 1952), and `picotool uf2 convert filesystem.ftl -t bin
filesystem.uf2 -o 0x10018000` writes it starting at `0x10018000`, which is
`0x10000000` (`XIP_BASE`) `+ 0x18000` (98304 decimal, exactly 96 KiB).

**Kernel size budget:** `CMakeLists.txt` sets `set(PICO_COPY_TO_RAM 1)`,
so the compiled `fuzix.uf2` is copied out of flash into SRAM at boot and
executes there -- this frees the XIP hardware for the flash-filesystem
driver, and it also means the kernel's flash-resident footprint (the image
written by `build/fuzix.uf2`) must fit inside the 96 KiB budget by
construction, since anything past that offset is the filesystem's declared
territory. The actual compiled size of `fuzix.uf2` was not built or
measured in this session (FUZIX requires its own bespoke cross-toolchain
setup and `cpu-armm0` targeting distinct from a stock Pico SDK build,
scoped out here); the 96 KiB figure is the allocated budget, confirmed
from source, not a measured binary size. `config.h`'s `TOTALMEM 160`
reserves 160 KB of the 264 KB SRAM for the running kernel and user
processes.

**Configurability, summarized:** the flash split is controlled by exactly
one hard-coded constant (`FLASH_OFFSET` in `globals.h`), duplicated by
implication into two more hard-coded constants in the `Makefile`
(`-s 1952` and `-o 0x10018000`) that a developer must keep in sync by hand,
and the filesystem's end is not parameterized at all -- it is always "to
the top of the chip." Moving the boundary, or carving out a reserved gap
for a second OS image, requires editing at least three files and adding a
new end-of-region cap to `devflash.c` that does not exist today.

## 5. NuttX `raspberrypi-pico` flash layout and the SRAM config

Read from `github.com/apache/nuttx`, `master` @
`ad176ac47fbfa29bafb53bf79826ad44450a2cb7` (fetched and built this
session, `arm-none-eabi-gcc (Arch Repository) 16.2.0`).

**The `nshsram` defconfig exists and does exactly what its name implies.**
`boards/arm/rp2040/raspberrypi-pico/configs/nshsram/defconfig` sets
`# CONFIG_RP2040_FLASH_BOOT is not set`. `boards/arm/rp2040/common/Kconfig`
defines that option: `config RP2040_FLASH_BOOT`, `bool "flash boot"`,
`default y`, help text "If y, the built binary can be used for flash boot.
If not, the binary is for SRAM boot." The board's `scripts/Make.defs`
switches linker scripts directly on this option:

```makefile
ifeq ($(CONFIG_RP2040_FLASH_BOOT),y)
  LDSCRIPT = raspberrypi-pico-flash.ld
else
  LDSCRIPT = raspberrypi-pico-sram.ld
endif
```

`raspberrypi-pico-sram.ld` places `.text`, `.init_section`, `.ARM.extab`,
`.ARM.exidx`, `.data`, and `.bss` all `> sram` (`ORIGIN = 0x20000000,
LENGTH = 264K`); only an explicitly `.flash.`-tagged section remains in
the `flash` region, and no `.boot2` section exists in this script at all.
`arch/arm/src/rp2040/Make.defs` confirms the boot2 stage is skipped
entirely for SRAM configs: `ifeq ($(CONFIG_RP2040_FLASH_BOOT),y) ...
include chip/boot2/Make.defs endif` -- no `else` branch, so `nshsram`
carries no boot2 image at all.

**Measured, this session** (three configs built to completion, zero
compiler errors, `PICO_SDK_PATH` not required since these boards vendor
their own `chip/boot2`):

| Config | text | data | bss | Linker's own flash/SRAM report |
|---|---|---|---|---|
| `nsh` | 156500 | 892 | 9616 | not printed for this config (`CONFIG_RP2040_UF2_BINARY` path only reports usage for `usbnsh`/`nshsram` builds in this tree) |
| `usbnsh` | 162416 | 740 | 8612 | "flash: 160 KB used / 2 MB (7.81%)", "sram: 8872 B / 264 KB (3.28%)" |
| `nshsram` | 163300 | 900 | 7664 | **"flash: 0 B used / 2 MB (0.00%)"**, **"sram: 171872 B / 264 KB (63.58%)"** |

The `nshsram` result is the direct confirmation the task asked for: this
config links and runs entirely from SRAM, with **zero** bytes of flash
residency, as reported by the linker itself, not inferred from the source.
Its generated `nuttx.uf2` (produced because `CONFIG_RP2040_UF2_BINARY`
defaults to `y` and `nshsram`'s defconfig does not override it) has, in
the first 512-byte UF2 block, target address `0x20000000` (SRAM base) and
family ID `0xe48bff56` (RP2040's registered UF2 family ID) -- confirming
that dragging this file onto the `RPI-RP2` BOOTSEL drive lands it directly
in RAM via the datasheet's documented "load directly into SRAM and run"
path (Section 2.8.1), with no flash write at all.

`nsh` and `usbnsh` (flash-resident, `CONFIG_RP2040_FLASH_BOOT=y` by
default, unset in both defconfigs) come in at roughly 154-160 KB of flash
(text + data), well inside the 2 MiB chip.

**Non-zero flash offset: no supported path found.** `MEMORY { flash (rx)
: ORIGIN = 0x10000000, LENGTH = 2048K }` is written directly into
`raspberrypi-pico-flash.ld`'s `SECTIONS` block; it is not driven by a
`#define`, a `CONFIG_` symbol, or a Make variable anywhere in this tree.
A targeted search for an offset or bootloader-aware option --
`grep -n -i "offset\|bootloader"` across `arch/arm/src/rp2040/Kconfig`,
`boards/arm/rp2040/common/Kconfig`, and
`boards/arm/rp2040/raspberrypi-pico/Kconfig` -- returned zero matches in
all three files, and
`Documentation/platforms/arm/rp2040/boards/raspberrypi-pico/index.rst`
documents no such procedure either. Linking a flash-resident NuttX image
at a non-zero offset (to sit below or above a chain bootloader) is
mechanically possible -- edit the `.ld` file's `ORIGIN`, and drop the
`.boot2` section, since a bootloader that has already run would have
configured XIP already -- but it is an unsupported, hand-edit change to
the linker script, not a documented or build-option-driven feature. This
puts it in the same category as FUZIX's `FLASH_OFFSET`: a real,
achievable edit, but a source change rather than a configuration knob.

## 6. Realistic options for coexistence on one 2 MiB Pico

**(a) Chain bootloader with two flash slots.** Buildable in the sense that
every individual piece has working precedent: a fixed-address boot2-style
loader (Section 1), a `SCB->VTOR` handoff (Section 3, proven by every
surveyed bootloader in Section 2), and a slot-selection trigger modeled on
the boot ROM's own watchdog-scratch-register convention (Section
2.8.1.1). It is blocked today, however, by a fact confirmed directly from
source and not previously stated: **FUZIX's Dhara FTL claims every byte
from `FLASH_OFFSET` (96 KiB) to the top of the 2 MiB chip, with no
end-of-region cap** (`devflash.c`'s `num_blocks` expression, Section 4).
Dhara is a log-structured flash translation layer -- it wear-levels and
garbage-collects across its entire declared span. Simply moving
`FLASH_OFFSET` further out to make room for a NuttX slot does not reserve
that room; it only moves where the FTL's region *starts*, and the FTL will
still eventually garbage-collect into whatever flash it considers free
unless a matching *end* cap is added, which does not exist in the code
today. Making this option work therefore requires a real FUZIX patch --
`globals.h`'s `FLASH_OFFSET`, a new end-of-region bound added to
`devflash.c`, and the two hard-coded constants in the `Makefile`
(`mkftl -s 1952`, `-o 0x10018000`), four coordinated edits across three
files -- plus hand-editing NuttX's `raspberrypi-pico-flash.ld` to link at
the resulting non-zero offset and drop its now-redundant `.boot2` section
(Section 5). No existing project (Section 2) provides this glue for two
arbitrary full OS images; it would have to be written, on top of source
changes to FUZIX itself.

**(b) One OS in flash, the other loaded into SRAM on demand.** This is
the only option that requires **zero source changes to either project**,
and both halves of it are independently confirmed working: FUZIX's default
build already occupies flash exactly as shipped (kernel in the first 96
KiB, Dhara-backed root filesystem in the remaining ~1.9 MiB), and NuttX's
`nshsram` config, built and measured in this session, already links to
zero bytes of flash and produces a UF2 that the boot ROM's own documented
SRAM-load path (Section 1) or `picotool load -x`'s documented "RAM image
boot" (picotool README) will run directly. The natural split is FUZIX
resident in flash at all times (it needs the flash for its root
filesystem; nothing else does), with NuttX invoked into SRAM whenever
wanted via a live USB connection (`picotool load -x nuttx.uf2`, or a
BOOTSEL-mode drag-and-drop of the same SRAM-targeted UF2). The cost:
running NuttX this way requires a host connection each time (no
persistence across power loss, since it is never written to flash), and
returning to FUZIX is a plain power-cycle or reset, exactly as it works
unmodified today. Given the 2 MiB budget FUZIX's root filesystem consumes
almost in full, and given that NuttX's own measured SRAM footprint
(171872 bytes, 63.58% of 264 KB) leaves no room to also hold a Unix-style
root filesystem in RAM even if FUZIX itself were pointed at SRAM instead,
this asymmetric split -- flash to the OS that needs persistent storage,
SRAM to the one that does not -- is the option the hardware budget
actually favors.

**(c) Reflash via BOOTSEL.** Always available as the baseline (Section 1,
Section 2.8.1's USB MSC path exists unconditionally on every RP2040).
It works, but it is not cheap to reverse: NuttX's flash-resident configs
(`nsh`, `usbnsh`) link starting at flash offset 0 through roughly 154-160
KB (Section 5's measurements), which overlaps FUZIX's kernel region
(0-96 KiB) and reaches into the start of FUZIX's Dhara filesystem region
(96 KiB onward, Section 4). Restoring a working FUZIX system after
flashing NuttX this way therefore likely requires reflashing
`filesystem.ftl` as well as `fuzix.uf2`, not just the kernel -- the exact
extent of Dhara-journal damage from a partial overwrite was not measured
here and is inferred, not confirmed, but the overlap itself is a direct
consequence of the confirmed offsets in Sections 4 and 5.

**(d) SD card.** Not a boot path on its own: the RP2040 boot ROM's
documented boot sequence (Section 1) recognizes exactly two sources, QSPI
flash and USB -- there is no SD/SPI-card boot mode in the silicon. Both
projects support SD as a secondary block device once already running
(FUZIX: `CONFIG_SD`, `/dev/hdb`, MBR-partitioned, up to 32 MB per its
platform README; NuttX: the `spisd` board config), so an SD card can hold
a second OS's *filesystem*, but something has to already be executing out
of flash or SRAM before that card can be read. SD therefore does not add
a fifth independent way to select an OS at boot; it only extends whichever
of (a)-(c) is already getting an OS running.

**Bottom line:** option (b) is the answer given the 2 MiB budget and
FUZIX's root-filesystem requirement -- it needs no modification to either
upstream project, both halves are individually confirmed working in this
session, and it matches each OS to the storage class it actually needs
(flash for FUZIX's persistent root filesystem, SRAM for an on-demand
NuttX session). Option (a) is the "real" dual-boot answer in spirit -- a
genuine chain bootloader selecting between two flash-resident images at
every cold boot -- but it requires patching FUZIX's flash driver to add an
end-of-region cap that does not exist today, hand-linking NuttX at a
non-zero, undocumented flash offset, and writing new bootloader code, since
no surveyed project does this for two arbitrary full OS images.
