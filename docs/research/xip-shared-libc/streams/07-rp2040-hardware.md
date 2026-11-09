# Stream: RP2040 CPU, memory and bus features against DiscoBSD's use

Read-only. The Pico SDK is not on disk: `tools/pico-sdk/` holds only
`fetch-pico-sdk.sh` (pins tag 2.3.0, commit 98a542c, lines 22-23),
`sdk-path.sh` and `check-flash-id.sh`; the `vendor/` checkout is absent. SDK
references below name SDK 2.x files from the agent's knowledge and are
unverified. Hardware facts cite the datasheet text in the companion notes
repository (`docs/rp2040/rp2040-datasheet.txt`, cited DS:line).

The LARGE-epoch defect analysis below is retained for the measured layout.
`../../swap-exhaustion/review-04.md` supersedes the live design with a fixed
192 KiB window, a kernel-resident SwapRAM pool and removal of the epoch.

## Settled hardware facts
- ARMv6-M defines no TrustZone, no PSPLIM, no bit-band and no fault status or
  address registers (ARMv6-M Architecture Reference Manual); every MPU fault is
  a HardFault (`fault.c:1-20`; MPU.md).
- Unmapped addresses raise a bus error (DS:1583); ROM writes are ignored
  (DS:8879).
- Four bus masters: core0, core1, DMA read, DMA write (DS:846-862). DMA ignores
  the core MPU. Userland cannot reach DMA registers (MPU grants only ROM, the
  window and the SIO divider); a kernel DMA path taking user pointers would rely
  on software range checks (`baduaddr_span`, `machdep.c:920`) alone.
- AIRCR.SYSRESETREQ resets the core only (DS:4893-4903); `boot()` uses it
  (`machdep.c:766`), so DMA and PIO state survive a kernel reboot.

## Defect: LARGE epoch stack outside the MPU map
The PICO kernel builds with `-DSWAPRAM -DSWAPRAM_KB=16`
(`sys/arch/rp2040/compile/PICO/Makefile:6-7`). `user_top()` returns
`__user_data_end + SWAPRAM_BONUS` for a P_LARGE process
(`sys/arch/rp2040/rp2040/swapram.c:730-733`), and exec places the stack at
`USER_TOP` (`sys/kern/exec_subr.c:451`). `mpu_map`
(`sys/arch/rp2040/rp2040/mpu.c:130-136`) grants SRAM only through 0x20024000,
is programmed once at startup, and leaves regions 4-7 disabled
(`mpu.c:187-195`).

A P_LARGE process's stack and argument area therefore intersect 0x20024000 to
0x20028000, which no enabled region grants. PRIVDEFENA covers privileged
accesses only (DS:6483-6492), and the board transcript at `MPU.md:210-227`
shows mputest's unprivileged probe at 0x20024000 faulting with the MPU on. The
first denied access depends on layout: with maximal empty arguments (ARG_MAX
5120, `sys/sys/syslimits.h:36`) stack.len is 27,664 and the stack base is
0x200213f0, so early pushes land inside the window, and the reviewer's
reproduction places crt0's `push {r4,lr}` inside and the first argument string
at 0x20026bf8, which faults later. The stream's agent confirmed the base
arithmetic and ARG_MAX; the crt0 case is the reviewer's reproduction.

The retained LARGE board successes (`docs/research/memory-ownership-plan.md`
lines 293-299, 2026-09-13, PR #42) predate MPU initialization (2026-09-20,
commit 1cb75f1e), and `tests/rp2040/swapram_large/bigtest.c` asserts nothing
about the MPU, so they are no counterexample.

Falsifier: a run that records kernel and executable hashes, the computed
resident demand (`exec_subr.c:434-451`), P_LARGE, the entry stack pointer, a
live MPU register readback (`machdep.mpu.enable` is cached, `mpu.c:198,233-237`),
and successful unprivileged reads and writes across the bonus interval.

Repair: enable a read-write region for the bonus interval only while the pool
is evacuated and the LARGE epoch owns those bytes, and disable it with readback
before the pool reopens. Add a combined MPU-plus-LARGE board gate.

Also: the Renode transcript in MPU.md shows `programmed=3` while the board
transcript and `mpu.c` say 4.

## Inventory

| Feature | Status | Evidence | Payoff | Falsifier |
| --- | --- | --- | --- | --- |
| Core1 | unused | no multicore, core1 or FIFO references; SIO_IRQ_PROC1 vector is a default loop (`locore0.S:253,303`); ROM leaves core1 asleep (DS:9625) | about zero: flash erase and program stall XIP for both cores; `spl` is per-core PRIMASK (`intr.h:18`) | a trace shows swap or compress work that could overlap a runnable process |
| SIO spinlocks, FIFO, INTERP0/1 | unused | MPU region 3 closes them (`mpu.c:51`) | none without core1; interpolators might help `lcdrender.S` blending (unmeasured) | profile shows RGB565 blend dominating PicoCalc rendering |
| Hardware divider | used, user only | `lib/libc/arm/gen/aeabi_div.S:72`; region 3 grants offsets 0x60-0x7f; kernel use banned by `tools/verify_rp2040_divider_ownership.py` | divider state is not saved across processes (small leak) | kernel division in a hot path |
| SysTick, VTOR, nPRIV and PSP/MSP, SVC and PendSV | used | `clock.c:51-53`; `machdep.c:346`; `locore.S:179`, `locore0.S:185-194`; `syscall.c` | | |
| WFI/WFE/SEV | partial | WFI in `idle()` (`machdep.c:710`) | WFE is an unmeasured single-core alternative for event-driven waits; wake events include interrupts under SEVONPEND and preempting asynchronous exceptions (DS:4803-4817) | |
| MPU regions 4-7 | unused | `mpu.c:187-190` writes RASR 0 | candidates: XIP libc, LARGE bonus, stack guards | |
| SDK stack guard | unused | `PICO_USE_STACK_GUARDS` absent | SDK uses region 0, which collides with the ROM region here | |
| Kernel stack guard | unused | 2048-byte stack; 220 untouched sentinel bytes in the retained workload (CAPACITY.md), and the watermark misses unwritten frames and the fault, signal and SwapRAM paths (CAPACITY.md:138-144), so it is no safe margin; the only check is at syscall entry (`syscall.c:170`) | high safety: overflow silently corrupts `struct user` | board test that a stacking fault into the guard does not lock up, and the u-area exchange (`locore.S:~110-125`) works with the guard toggled |
| User stack guard | unused | `syscall.c:188-198` checks sp at syscalls only | a higher-priority 32-byte no-access subregion of a 256-byte region, the mechanism `mpu.c:117-127` already uses (DS:6588-6594): 32-63 window bytes, one region write per switch and per break move; a frame larger than 32 bytes can skip it | stack-into-heap corruption without an intervening syscall |
| XN on user data | unsupported by the current static map and OMAGIC loader | overlapping higher-priority regions and XN exist (DS:5485-5488, 6580-6594) and four regions are free | aligned overlays can protect stack and data portions; full separation needs text and data bounds, padding and a region budget | |
| Atomic SET/CLR/XOR aliases | partial | SET/CLR in `dev/usb.h:31-32`, `machdep.c:182`, `picocalc_reg.h:35-36`; XOR unused | | |
| SRAM4/5 | used | USB TX ring (`RP2040.ld:78`, `usb.c:340`) | moving the kernel stack there gains about zero | contention measured with DMA or core1 active |
| XIP cache as 16 KB SRAM | unused | | +16 KB RAM with kernel text uncached; about 40 cycles per word is a per-miss transfer estimate (`boot2_w25q080.S:99-124`) against a 1-cycle hit (DS:9027), not a workload slowdown | elapsed time plus XIP access and hit counters on a kernel workload |
| XIP noalloc alias 0x11000000 | unused | all flash data reads use 0x10000000 (`flash.c:357,422,441,662`), so file and swap reads may evict kernel text | hypothesis: speed; a hit-count delta alone does not establish a speed gain | elapsed time and XIP counters before and after a swap-in or file read |
| XIP and BUSCTRL perf counters | unused | | free profiling; decides the cache questions | |
| USB DPRAM tail | partial | 3520-byte tail (BOOT-MAP.md); 2320 used by flpage, codec and flscratch (`kern.ldscript:154-160`) | about 1200 spare in 256 and 944 byte gaps; all 4 KB free in UART builds | a staging buffer above 1.2 KB is needed |
| ROM float, clz, flash routines | used | `rom_float_resolver.S`, `rom_float_clz.S:4`, `flash.c:264-270`, `machdep.c:742` | | |
| ROM popcount, ctz, reverse | unused | codes P3, T3, R3 (DS:9778-9790) | tiny | |
| ROM memcpy and memset | unused | kernel `bcopy`/`bzero` are C loops running from XIP (`machdep.c:~1030,1063`) | ROM code needs no XIP cache; undefined for overlap | benchmark `bcopy` against ROM C4 on 1-4 KB |
| Boot2, SSI | used / partial | `boot2_w25q080.S`, quad continuous read, CLKDIV 2 | | |
| XIP streaming FIFO, DMA, sniffer CRC32 | unused | no DMA register use; CRC32 in software (`sys/kern/subr_crc32.c:19`, used by `hsx_stream.c:60`) | unmeasured: zswap.md lines 123-132 bound write latency, not read-side benefit; XIP streaming plus DMA overlaps reads with execution (DS:9142-9186) | exec, swap-in, file-read, copy and CRC workloads measured separately |
| PIO | used, PicoCalc | PSRAM swap on PIO0 (`psram_pio.h:~95`) | | |
| Watchdog counter | disabled | `machdep.c:503-504` | arm only above a 400 ms erase | |
| Watchdog scratch | partial | scratch0 swap cursor (`machdep.c:219,255`); scratch1-2 masked-section site (`machdep.c:507-511`); scratch3 free | panic PC in scratch3, about 40 bytes of code | a hang or panic the console cannot explain |
| RTC, SYSCFG MEMPOWERDOWN | unused | | power only; DS:23290 urges caution | |

## Ranked by payoff over cost
1. Repair the LARGE epoch MPU coverage and add the combined board gate.
2. Kernel stack guard in a spare MPU region, gated on a lockup test.
3. XIP and BUSCTRL counters as profiling instruments; they decide the cache
   questions and the XIP libc speed question.
4. XIP noalloc alias for flash data reads, after counter and elapsed-time
   measurement.
5. Panic PC in watchdog scratch3.
6. ROM memcpy for kernel `bcopy`, after a benchmark.
7. User stack guard as a 32-byte subregion at the break.
8. USB DPRAM spare, only when a buffer needs it.
9. ROM popcount and ctz: negligible.
10. Unmeasured, not pursued yet: XIP cache as RAM, core1, DMA, interpolators.

## Resolution of the adversarial review

Verified by the stream's originating agent against source, datasheet and git,
and applied by the orchestrator because the agent's role had no write access.

1. ACCEPT. The defect stands; the repair is a dynamic bonus region plus a
   combined gate.
2. ACCEPT. The falsifier records resident demand and a live MPU readback.
3. ACCEPT. "First push faults" removed; arithmetic and ARG_MAX confirmed; the
   crt0 case is the reviewer's reproduction.
4. ACCEPT. git places LARGE (2026-09-13) before MPU initialization (2026-09-20).
5. ACCEPT. XN reclassified as unsupported by the current map.
6. ACCEPT. Guard cost corrected to 32-63 bytes.
7. ACCEPT. DMA payoff marked unmeasured.
8. ACCEPT. The 20-40x figure reduced to a per-miss transfer estimate.
9. ACCEPT. WFE claim corrected.
10. ACCEPT. Stack margin wording corrected.
11. ACCEPT. SDK pin lines and ISA-absence citations corrected.
