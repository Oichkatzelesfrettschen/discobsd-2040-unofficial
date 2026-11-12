Source reading, datasheet text and linked-kernel measurement at `7a270276`; no board, Renode or qemu-user run backs any figure here.

# RP2040 hardware reviews reconciled: what survives, what translates, what comes next

Twenty-one review documents written outside the tree on 2026-11-10 described
the RP2040 and this port from the chip down to single bytes, proposed
optimizations, and asked whether mathematics from the owner's research
corpora could buy an order of magnitude. They were checked claim by claim
against the RP2040 datasheet text, the port source and three freshly linked
kernels. This document is what remains after that check: the corrected
hardware facts the reviews were reaching for, the disposition of every
proposal, the ideas that survive in a different form than the one proposed,
and one ordered roadmap. The reviews themselves are not kept; a claim they
made that is absent below did not reproduce.

The reviews asked the right question -- where does every byte, cycle and
erase go, and who owns it -- and several of their instincts match work the
tree had already done or planned. Their weakness was arithmetic and
addresses written without a datasheet or a linked kernel at hand. Most of
their proposals fail as written and most of their intent survives.

## Evidence base

- Source and build: this repository at `7a270276`, `arm-none-eabi-gcc`
  16.2.0 at `-Os`, `bmake MACHINE=rp2040 logged-kernel`. Sizes are
  `arm-none-eabi-size`, `arm-none-eabi-nm -S`, `pahole` over the kernel's
  DWARF, and `tools/check_kernel_headroom.py`.
- Chip: the RP2040 datasheet text in the sibling `discobsd-2040-notes`
  repository (`docs/rp2040/rp2040-datasheet.txt`); section numbers resolve
  to pages through `sys/arch/rp2040/doc/DATASHEET-INDEX.md`.
- Review method: each specific claim (address, size, register, saving) got
  one verdict, confirmed, refuted with the correct value, or unverifiable
  with the measurement that would settle it. Two read-only review passes
  covered twelve documents; the roadmap, task and mathematics documents were
  read directly.
- Not checked: Pico SDK equivalence (the SDK is absent from the host), boot
  ROM internals (no ROM image or source on the host), ARMv6-M Architecture
  Reference Manual wording (the manual is absent; the datasheet's Cortex-M0+
  chapter and the linked kernel stood in), and every statement about silicon
  behavior, which needs a board run under the hardware opt-in.

## The hardware, corrected

These are the facts the reviews circled, stated from the datasheet and the
linked kernel. Each row names the claim it replaces.

| Subject | Fact | Authority | Review claim it replaces |
| --- | --- | --- | --- |
| Striped SRAM | SRAM0-3 are four 64 KB banks striped by word across 0x20000000-0x2003ffff; `RP2040.ld` tiles all 256 KB (192 KB user window, 58 KB kernel RAM, two 3 KB u areas) and `kern.ldscript` asserts the tiling | datasheet 2.6.2; `RP2040.ld`; `kern.ldscript` | "260 KB unused" behind the aliases |
| Unstriped aliases | 0x21000000 + 0x10000 b + 4 w names the same word as 0x20000000 + 16 w + 4 b for bank b and word w; the four 64 KB alias windows are the striped memory seen bank by bank, so they hold no free byte | datasheet 2.6.2 | bank-specific placement through the alias |
| SRAM4, SRAM5 | two unstriped 4 KB banks at 0x20040000 and 0x20041000, intended for per-core stacks and hot code; PICO fills both with the 8,192-byte USB transmit ring; PICO_UART uses 1,536 bytes and leaves 0x20040500-0x20041eff (6,656 bytes) without an owner | datasheet 2.6.2; `kern.ldscript`; `nm` | "8 KB free scratch" |
| XIP cache | 16 kB, two-way, 8-byte lines, one-cycle hit; `XIP_CTRL` at 0x14000000 with `CTRL.EN` reset to 1; hit and access counters `CTR_HIT` and `CTR_ACC`; neither boot2 nor the kernel writes `XIP_CTRL` | datasheet 2.6.3.1 | both "no XIP cache" and "configure it through SSI registers" |
| XIP aliases | 0x10 cacheable and allocating, 0x11 cacheable and non-allocating, 0x12 non-cacheable and allocating, 0x13 bypass; all valid | datasheet 2.6.3.1 | "0x12000000-0x13ffffff reserved" |
| Cache as SRAM | 0x15000000 is 16 kB of RAM only while `CTRL.EN` is 0, and then every flash fetch bypasses the cache; the kernel executes from flash | datasheet 2.6.3.1, erratum on booting into cache-as-SRAM | "16 KB extra RAM" |
| SSI | the flash serial interface is at 0x18000000; 0x40060000 is the ring oscillator | datasheet address map | SSI at 0x40060000 |
| USB DPSRAM | 4 KB at 0x50100000. PICO: endpoint area 0x50100000-0x5010023f (576), Dhara page 0x50100240 (1,024), codec workspace 0x50100740 (1,040), flash program-page buffer 0x50100f00 (256); two gaps of 256 and 944 bytes remain | `kern.ldscript`; `nm` | 8 KB; "unused" |
| MPU | eight regions, 256-byte minimum, power-of-two size with base aligned to size, eight subregions on a region of 256 bytes or more; the port programs four and never reprograms them | `MPU.md`; `mpu.c` | 32-byte region at 0xd0000078; 4-byte guard; "256 subregions" |
| Alias and MPU | no region covers 0x21000000-0x2103ffff, so an unprivileged access there faults by non-coverage; a region granting user read-write over it would open kernel RAM | `MPU.md`; `mpu.c` | "aliases are unprotected; add AP_RW regions" |
| boot2 | 252 bytes of code and a CRC32 at offset 0xfc; the boot ROM checks it before running boot2 from 0x20041f00 | `BOOT-MAP.md`; `tools/boot2sum` | CRC at 0xf8; metadata in the trailer |
| Vector table | 48 words at 0x10000100; nine system slots are reserved on ARMv6-M; six hold zero and three hold the image identity words; PICO installs real handlers for USBCTRL, UART0 and UART1 and `Default_Handler` elsewhere | `locore0.S` | 1 KB table |
| DMA | 12 channels; the kernel uses none | datasheet 2.5; source search | 8 channels; a `dma.c` to tune |
| Flash geometry | 2 MB is 512 erase sectors of 4,096 bytes and 8,192 program pages of 256 bytes; the Dhara root's 1,536 KB yields 989 KB of logical blocks | `STORAGE.md`; `dhara-geometry.txt` | 4,096 sectors; "12 KB Dhara overhead" |
| Flash clock | boot2 sets the serial clock divider to 2, 62.5 MHz at the 125 MHz system clock | `boot2_w25q080.S` | not stated |
| Idle and GPIO | `idle()` executes `wfi`; GPIO goes through SIO; exception priorities are set at boot | `machdep.c` | proposed as new work |

### Linked kernels at `7a270276`

| Kernel | Text | Flash image | Flash headroom of 130,816 | Ordinary data and bss | SwapRAM pool | Kernel RAM free of 59,392 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| PICO | 95,248 | 95,248 | 35,568 | 17,112 | 16,384 | 25,896 |
| PICO_UART | 86,275 | 86,220 | 44,596 | 16,688 | none | 42,704 |
| PICOCALC | 111,088 | 111,208 | 19,608 | 29,200 | 16,384 | 13,824 |

Flash image and headroom are the `check_kernel_headroom.py` span. Of PICO's
25,896 free bytes the linker reserves 4,096 for the swap-exchange sector
(`kern.ldscript`), which leaves 21,800 without an owner; PICOCALC keeps 9,728 by the same
subtraction and is the configuration where kernel RAM is scarce. The PICO fixed
tables at that revision are `proc` 2,600 (25 slots of 104), `inode` 2,016 (24
of 84), `file` 576 (24 of 24), `bufdata` 4,096, `mount` 1,036, `cfree` 1,024,
`sr_tab` 600 and `sr_seg` 208. The capacity trim of roadmap item 9 brings
`proc` to 1,664 (16 slots), `file` to 432 (18), `sr_tab` to 384 and `sr_seg`
to 136.

## Proposals, one by one

"Lands here" means this change carries it. "Owned by" names the document
that already tracks the surviving form.

| Proposal in the reviews | Finding | Disposition |
| --- | --- | --- |
| Reorder `struct proc` and `struct file` by alignment to save 900 and 96 bytes | `pahole`: reordering alone saves nothing, since `proc` held one 2-byte hole in 104 bytes and `file`, `inode` and `buf` held none. Narrowing does: `p_stat` takes values 1 to 6 and becomes `uint8_t`, and with the head ordered `p_uid`, `p_sig`, `p_flag`, `p_pid`, `p_ppid`, `p_stat` the struct is 100 bytes with one pad byte before the union. `file` carries `f_count` (bounded by `NPROC` times `NOFILE`) in `uint16_t`, `f_type` and `f_msgcount` in `uint8_t`, and is 16 bytes. The 100 is pinned by a static assertion in `machdep.c`, the offset of `p_addr` (60) by `P_ADDR_OFFSET` in `proc_asm.h`, and the whole struct is copied to `ps`, `w` and `fstat` through `kern.proc`, which compile against the same header | Lands as the fixed-width narrowing of `struct proc`, `struct file` and `struct tty`; the field placement by Thumb-1 addressing reach in "What translates" is the remaining form |
| C bit-fields and nibble packing of process state | Thumb-1 has no bit-field instruction, so each store becomes a read-modify-write | Declined, as the reviews' own later pass concluded |
| Trim `NPROC` 25 to 16 and `NFILE` 24 to 18 | The saving is 1,368 bytes (136 per process slot, 24 per file slot), larger than the 732 the reviews computed; observed peaks are 8 and 15 | Owned by `sys/arch/rp2040/doc/CAPACITY.md`: landed at 16 and 18 once the exhaustion and recovery cases of `tests/rp2040/capacity-corpus.json` held receipts; the PICO bss fell by 1,344 bytes |
| Trim `NINODE`, `NBUF`, `USIZE` | Peaks are 22 of 24, 3 of 4, and 1,828 of a 2,048-byte stack | Retained, as the reviews' later pass concluded and `CAPACITY.md` records |
| Hot data, stacks or interrupt code in one bank through the 0x21000000 alias | The alias overlays assigned memory, and one Cortex-M0+ issues one data access per cycle, so striping buys no parallelism within a core | No placement available. Survives as a bus-contention measurement; see roadmap item 2 |
| Use SRAM4 and SRAM5 for per-core stacks | The datasheet intends exactly this; PICO has no free byte there, PICO_UART has 6,656 | Open only on PICO_UART; needs a contention measurement first |
| Configure or enable the XIP cache | Enabled at reset and untouched by the port | Nothing to enable. Survives as the cache counters and the non-allocating alias; roadmap items 1 and 3 |
| Use the XIP cache as 16 KB of RAM | Requires the cache off while the kernel executes from flash | Deferred, as `rp2040-memory-wear-engineering-program.md` already records |
| Align hot loops and stubs to the 8-byte cache line | The line size is real; the gain is unmeasured and each aligned symbol costs up to 6 bytes | Roadmap item 4, after the counters |
| Split the user window into text, data and bss regions | OMAGIC images are one writable extent and text size is neither fixed nor a power of two | Conflicts with the programmed-once map of `MPU.md`; the guard-subregion items below are the form that fits |
| A 4-byte stack guard | Below the 256-byte region minimum | Survives as the 32-byte subregion guard, owned by `xip-shared-libc/README.md` roadmap items 10 and 11 |
| Shrink the SIO divider region to 32 bytes, or widen it | The shipped 256-byte region with subregion mask 0xf7 is the smallest legal form; widening exposes CPUID, which `mputest` requires to fault | Already minimal |
| MPU regions over the alias windows | Access already faults by non-coverage | Survives as the `mputest` alias probes; roadmap item 6 |
| DMA transfer alignment | The kernel uses no DMA | Survives as a throughput question for the PicoCalc panel and SD paths; roadmap item 8 |
| Convert 32-bit Thumb instructions to 16-bit; reorder functions to shorten calls | All 32-bit instructions in the kernel are `bl`, `mrs`, `msr`, `isb` and `dsb`, which have no 16-bit form | No bytes available; the dead-function and hot-field queue of `kernel-footprint-handoff.md` is where kernel text comes from |
| Metadata in boot2 padding or the CRC trailer | The slot is fixed at 256 bytes and checked by the ROM | No byte is freed |
| Identity words in the reserved vector slots | Nine words (36 bytes) are never fetched, and the boot2 CRC does not cover them | Landed in three of the nine: the image length, its CRC32 and the board descriptor address (`BOOT-MAP.md`, "Image identity"). The kernel recomputes the sum at boot and `board_image_identity.py` is the first reader, the kernel part of the running-board identity task (R01) of the memory and wear program |
| Truncate the interrupt table after the last used vector | Saves at most 40 bytes and makes any later interrupt enable fetch past the table | Declined on risk against yield |
| Gate unused peripheral clocks; power down unused SRAM banks | `WAKE_EN`/`SLEEP_EN` and `SYSCFG.MEMPOWERDOWN` exist (datasheet 2.11); no shipped kernel leaves a whole bank unowned, and no current has been measured | Roadmap item 7, scoped to the battery-powered PicoCalc |
| `wfi` in idle, SIO GPIO, exception priorities, multicall bss overlay, UART scratch layout, `NBUF` 10 to 4 | Present in the tree | Already done |
| XIP shared libc with r9-relative data | The reviews restate the tree's own design, including its 186-block net gain; they omit that flash is closed to user mode, so libc text needs its own MPU region | Owned by `xip-shared-libc/README.md`; updated by this change |
| Second-core message passing through the SIO FIFOs | Surveyed; core 1 runs as the kernel observer | Owned by `rp2040-second-core.md` and `sys/arch/rp2040/doc/CORE1.md` |
| Boot time 9 s to 7 s | No stage timing exists and no mechanism was named | Needs stage timestamps before any target |

## The mathematics documents

Four documents proposed partial differential equations, Cayley-Dickson
algebras and quantum circuits as kernel mechanisms, with tables of expected
gains. One document formally refuted an order-of-magnitude claim, and one
restated the idea as a testable conjecture.

The proposed mechanisms have no site in this kernel. They name files and
subsystems the tree does not have (a virtual-memory layer with page faults,
a deadlock detector), they compute on floating-point grids on a core without
a floating-point unit, and their baselines ("deadlocks 5/hr", "RAM usage
250 KB") were never measured. The tables the kernel searches hold 25, 24 and
4 entries, so nothing asymptotic is scarce.
`corpus-mathematics-kernel-survey.md` applied the working test to the same
corpora -- name a kernel site and move flash bytes, SRAM bytes, erases or
masked-interrupt time there -- and landed three changes worth 1,692 bytes of
kernel text. That test is the standard for any further candidate.

The refutation document's conclusion holds and its table arithmetic is
replaced by the measured values above (it used 64, 48 and 544 bytes per
inode, file and buffer slot; the linked sizes are 84, 24 and 36 plus a
1,024-byte block). Its bound stands in corrected form: every fixed table in
PICO sums to about 12 KB, so no table change moves total SRAM by more than
a few percent.

The conjecture document survives whole, because it claims only what can be
tested: model one workload as a state-transition system with stated
invariants, pick one metric such as sector erases per completed workload,
predict, and compare matched builds. Its experiment protocol matches the
evidence classes and promotion gates of
`rp2040-memory-wear-engineering-program.md`, which owns that work.

## What translates

The direct implementations mostly fail. The ideas under them mostly do not,
and several are already at work in the tree under other names.

| Idea in the reviews | Form that survives | Where it lives or lands |
| --- | --- | --- |
| A map of every byte | A link-time ownership ledger: every range has one named owner and the linker refuses a gap or an overlap. The byte map was unreliable because it was written by hand; the linker script and `nm` give the same thing exactly | `kern.ldscript` assertions; the DPSRAM ledger of the memory and wear program; `BOOT-MAP.md` section 2, extended by this change |
| Struct packing for size | Layout for instruction reach. A Thumb-1 load reaches 124 bytes by word, 62 by halfword and 31 by byte from a base register; a field beyond reach costs a literal or an add at every use. Moving hot fields inside reach saves text where reordering saves no data | `kernel-footprint-handoff.md` (hot `struct user` fields) |
| Bank-aware allocation | Contention as a measured quantity. Two bus masters contend only on the same bank in the same cycle, and the bus fabric counts it: `BUSCTRL` performance counters select contested and total accesses per SRAM bank (datasheet 2.1.1.2). Core 1 wakes every 10 ms, so the expected count is near zero; a nonzero count would make SRAM4 or SRAM5 the home of core 1's stack and words | roadmap item 2; `CORE1.md` |
| XIP cache tuning | The cache as an instrument. `CTR_ACC` less `CTR_HIT` is a miss count for any workload, and it showed the cache already serves 99.7 percent of accesses, so alias policy has nothing left to win | roadmap items 1, 3 and 4; `xip-shared-libc/README.md` item 12 |
| Faster interrupt paths by bank placement | `.ramfunc` already places code in RAM at a known byte cost; the cache counters decide whether any handler belongs there | roadmap item 4 |
| DMA alignment | DMA as a throughput tool where a peripheral is the bottleneck: the PicoCalc panel fill and SD block transfer over SPI | roadmap item 8; `sys/arch/rp2040/doc/PICOCALC.md` |
| Power-down of banks and clocks | A power budget for the battery-powered PicoCalc, measured at the supply before any gating | roadmap item 7 |
| Formal proof of table bounds | The capacity corpus is the empirical form of the same obligation: a table shrinks only when exhaustion and recovery are exercised and attributed. A machine-checked model of one admission path would strengthen a receipt and cannot replace the board case | `CAPACITY.md`; `tests/rp2040/capacity-corpus.json` |
| Call-graph partitioning | Multicall composition is already chosen by a measured three-number screen; an integer program over the same measurements is a possible refinement with a bounded prize, the root blocks still free | `memory-ownership-plan.md`; `sys/arch/rp2040/doc/PROFILES.md` |
| Diffusion and "temperature" for memory and wear | Recency and wear counters. Swap victim choice already uses sleep time; raw swap already rotates its start sector; per-sector erase counters are the open telemetry task (H02) | `rp2040-memory-wear-engineering-program.md` |
| Phase separation for storage layout | Fragmentation of contiguous swap extents under the rotating raw-swap allocator | `swap-exhaustion/plan.md`; `memory-ownership-plan.md` |
| The whole system as one mathematical object | One constrained transition model per workload with one stated objective | the conjecture, carried by the memory and wear program's experiment matrices |

The port is constrained and the concept is sound. With 25,896 bytes of
unowned kernel RAM, 35,568 bytes of kernel flash and about 310 free root
blocks, the remaining gains are measured in kilobytes and blocks, and the
tree's own program already reaches for the largest of them: the shared libc
(186 blocks), the SwapRAM pool comparison, and write telemetry. What the
reviews add to that program is three instruments it had listed last and
should build first.

## Roadmap

Each item is one reviewable change with its gate. Items 1 and 2 are
instruments; items 3 to 8 each wait on the measurement named.

1. XIP cache counters: landed as `machdep.xip_acc` and `machdep.xip_hit`
   (`XIP_CTRL` `CTR_ACC` and `CTR_HIT`); a write clears a counter. Gate:
   `tests/rp2040/hwcount/board_hwcount.py` under its opt-in.
2. Bus contention counters: landed as `machdep.bus_perfctr0` to `3` with
   `machdep.bus_perfsel0` to `3` choosing each counter's event. Gate: the
   same board test.
3. Flash data through the non-allocating alias: measured and declined. It
   removes 7 percent of cache misses and no measurable time ("The cache
   control" below).
4. Cache-line alignment or RAM placement of hot kernel paths: declined on
   the same measurement. Every miss of a command together costs about
   0.17 s of 2.8 s, at a hit rate of 99.7 percent.
5. Shared libc, in the order of `xip-shared-libc/README.md`. Its board
   evidence item uses the counters of item 1. Of its static-state items,
   const `_ctype_` and getgrent's first-use record have landed and the
   other buffers stay static on measurement (that README's section "Static
   libc state under static links").
6. `mputest` probes for the unstriped alias and for 0x15000000: landed as
   four `alias bank` lines at 0x21000000, 0x21030000, 0x2100c000 and
   0x2103c000, expecting SIGSEGV with the MPU on and a successful read with
   it off, and one `xip cache sram` line at 0x15000000, expecting SIGSEGV in
   both states because the bus error stops it whatever the MPU does. Gate:
   `check-renode` for the MPU-on lines through `MPUTEST OK (mpu on)`;
   `MPU.md` records the board run and the MPU-off calibration run.
7. PicoCalc power budget: supply current at idle, then with unused
   peripheral clocks gated in `SLEEP_EN`, then with an unowned SRAM bank
   powered down. On PICO_UART, moving the 256-byte flash program-page
   buffer from the top of SRAM5 into SRAM4's free span leaves SRAM5 without
   an owner.
8. DMA for the PicoCalc panel and SD transfers, if a timing of the current
   programmed-I/O paths shows the peripheral rather than the bus as the
   limit.
9. Capacity trims of `NPROC` and `NFILE` under the `CAPACITY.md` admission
   rule. Landed: `NPROC` is 16 and `NFILE` is 18, and the PICO kernel's bss
   is 1,344 bytes smaller. The deciding cases hold receipts at the earlier
   limits (board and Renode table exhaustion, SwapRAM evacuation, signal
   frame recovery, storage pressure, and the all-buffers-busy fixture of
   `tests/kernel`), and the board and Renode exhaustion runs and the board
   cases repeat at the reduced limits
   (`board-capacity-exhaustion-65d7244c.txt`,
   `renode-capacity-exhaustion-65d7244c.txt`,
   `board-capacity-cases-65d7244c.txt`). `NINODE`, `NCLIST`, `NBUF` and
   `USIZE` keep their values; their exhaustion and fault cases are open.
10. Image identity in the reserved vector words. Landed for the kernel:
    `tools/kernsum` stamps the length and CRC32 after the link, the kernel
    recomputes the sum over its flash at boot and exports it as
    `machdep.image_crc`, and `board_image_identity.py` compares a board with
    a host `unix.bin`. The rest of the running-board identity task of the
    memory and wear program is open: the root file system's hash, the flash
    chip identity and the ROM revision in one record.

## First readings from the instruments

One run of `board_hwcount.py` on a Raspberry Pi Pico, PICO kernel at `ecf54e13`
with `options CORE1` and the matching root image, 2026-11-11; all twelve
checks passed. The workload is `cat /bin/sh /bin/sh /bin/sh /bin/sh >
/dev/null` typed at the USB console, so each count includes the shell, the
`sysctl` commands around it and the idle kernel.

| Reading | Value | What it settles |
| --- | ---: | --- |
| SRAM0 accesses, all | 8,665,460 | the counter is below its 16,777,215 ceiling over this workload |
| SRAM0 accesses, contested | 250 | 0.003 percent: core 1 as the observer does not measurably contend for a striped bank, so moving its stack and words to SRAM4 or SRAM5 buys nothing. The falsifier of that proposal held |

The same run read 186,183,398 XIP accesses against 93,638,182 hits, a
fraction of 0.503. That fraction measured the test, not the cache: the
counters were cleared and read by separate console commands, each of which
adds tens of millions of flash accesses to one counter and not the other.
The control below replaces it.

## The cache control, and the two proposals it decides

`machdep.xip_cache` turns the XIP cache off and on; off survives flash
writes, whose ROM flush routine would otherwise re-enable it.
`board_hwcount.py` clears both counters, runs `cat /bin/sh > /dev/null` and
reads both in one shell command, and times that command from its first
output to its sentinel. One run on the same Pico, kernel and root built
from this change on `58ec71f1`, 2026-11-11, all 19 checks passed:

| Cache | Seconds | XIP accesses | Hits | Misses |
| --- | ---: | ---: | ---: | ---: |
| on | 4.06 | 66,932,156 | 66,715,934 | 216,222 |
| off | 93.23 | 0 | 0 | 0 |
| on, just flushed | 4.48 | 105,215,058 | 105,025,464 | 189,594 |
| on, warm | 4.48 | 105,337,662 | 105,148,176 | 189,486 |

The seconds of this run include the echo of the typed line, which the test
now leaves out; the later runs below time the command alone.

- The hit rate is 99.7 to 99.8 percent. The 16 kB cache serves a 95 KB
  kernel almost entirely.
- The cache is worth a factor of 23 on this command. Uncached execution is a
  measurement control and nothing else.
- With the cache off `CTR_ACC` stays at zero. The datasheet (2.6.3.1) says
  it counts every XIP access, cached or not; on this board it counts only
  while `CTRL.EN` is 1. An access count for an uncached run has to come
  from elsewhere.
- The flushed and warm runs miss equally often, so the misses are steady
  conflict and data-read misses, not start-up cost.

Roadmap item 3, flash data through the non-allocating alias, was measured
on a kernel built for the experiment whose data-read base could be switched
between 0x10000000 and 0x11000000 at run time; the kernel was not kept.
Six timed runs per alias and workload, alternating:

| Workload | Alias | Misses, range | Seconds, range |
| --- | --- | ---: | ---: |
| `cat /bin/sh > /dev/null` | 0x10 | 189,142 to 190,743 | 2.81 to 3.02 |
| `cat /bin/sh > /dev/null` | 0x11 | 176,134 to 177,360 | 2.81 to 2.83 |
| `ls -lR /bin /etc > /dev/null` | 0x10 | 179,851 to 208,642 | 2.61 to 2.83 |
| `ls -lR /bin /etc > /dev/null` | 0x11 | 167,227 to 193,106 | 2.61 to 3.02 |

The non-allocating alias removes about 13,500 misses per command, 7
percent, and elapsed time does not move within the 0.2 s resolution of the
host timing. At the uncached cost of about 0.9 microseconds per access
(93 s over roughly 100 million accesses) those misses are about 12 ms of a
2.8 s command. The item's own rule was to adopt on an elapsed-time gain, so
the alias is declined. The second `ls` round ran with different access
totals than the first, which is why its ranges are wide; the comparison
within each round shows the same 7 percent.

The same arithmetic bounds roadmap item 4. All of a command's misses
together, about 190,000, cost about 0.17 s; alignment or RAM placement can
remove only a part of them, at up to 6 bytes of flash per aligned symbol or
a function's size in kernel RAM. It is declined until a workload shows a
miss rate far above 0.3 percent.

## Falsifiers for this document

- A kernel at `7a270276` with the same compiler whose sizes differ from the
  table refutes the measurement section.
- A `pahole` run showing a smaller legal order of `struct proc` refutes the
  packing finding.
- A board read through 0x21000000 from user mode that returns data refutes
  the alias row and is an MPU defect.
- A contested-access count well above zero with core 1 idle refutes the
  expectation in "What translates" and promotes roadmap item 2's
  consequence.
