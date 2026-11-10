# Core 1: the observer, its supervisor and what proves them

The RP2040 carries two Cortex-M0+ cores. The kernel runs on core 0 alone,
and `sys/arch/rp2040/rp2040/core1.c`, selected by `options "CORE1"`, gives
core 1 one job: watch core 0's clock, find out where core 0 is when the
clock stops, and reset the chip with that record when it never restarts.
This document is the authority for that mechanism, the registers and words
it rests on, its cost, and the evidence class behind each claim.
`docs/research/rp2040-second-core.md` holds the reasons a second kernel CPU
is excluded and the ranking that puts the observer first.

The PICO and PICO_UART kernels carry the option. The PICO kernel passed
the board procedure below on a Raspberry Pi Pico. The PICOCALC and
PICOCALC_PSRAM kernels do not carry it: no PicoCalc has run it, and the
panel renderer's effect on the tick record there is unmeasured.

## What it is not

It is not symmetric multiprocessing. One process is resident in one 192 KB
window, `u` is one fixed structure exchanged at a switch, and ARMv6-M has no
load-exclusive, compare-and-swap or atomic exchange to build a kernel lock
from. Core 0 depends on core 1 for nothing: a core 1 that is absent, parked
or dead costs the observer and no kernel function.

## Where each mechanism comes from

| Mechanism here | Source | What was kept and what was dropped |
| --- | --- | --- |
| One owning core per mutable word | DragonFly LWKT's per-CPU ownership | Kept as the only concurrency rule. Tokens, serializers and critical-section counts are dropped: core 1 runs no blocking thread |
| Request and reply rings, publish then advance | DragonFly `lwkt_ipiq.c`, entry store then `ip_windex` | Kept in `corering.c`. Entries carry a slot number and an argument, never a pointer, so a restarted core follows no stale address |
| Doorbell skipped when one is pending | DragonFly's `atomic_swap_int` on `gd_npoll` | The SIO FIFO's `RDY` bit replaces the atomic exchange ARMv6-M lacks |
| Credits that keep a reply slot free | DragonFly's IPI queue drain rule | One counter on the requesting core |
| Interrupt the processor that stopped answering and record its PC | DragonFly's sampled PC of a non-draining CPU; NetBSD's "hogging kernel lock" IPI | The NMI probe below |
| A processor that survives the main one and holds its record | the VAX 8600 console processor | Core 1 keeps running from SRAM while core 0 is masked |
| Ping answered from the service's own loop, restart with backoff, give up after a bound | MINIX 3 Reincarnation Server | Kept. Inverted in one respect: a dead service degrades the system instead of stopping it |
| State held outside the service, operations safe to repeat | MINIX 3 | A ping is a pure function of its entry; core 1 holds no state a restart must preserve |
| Launch protocol, forced-off reset, vector table and stack in SRAM | RP2040 datasheet 2.8.2 and 2.13; Pico SDK `multicore.c` read for technique | Written against the registers; no SDK code |

## Core 1 executes and reads SRAM only

A flash erase or program takes execute-in-place down for both cores
(`dev/flash.c`), and the datasheet defines no result for a fetch or load
from flash in that window. Every function core 1 runs is therefore in
`.ramfunc`: `core1_main`, `core1_op`, `core1_fault`, `core1_reset` and this
unit's copy of `cring_put` and `cring_service`, 544 bytes in all. Its
vector table and stack are in `.bss`. It masks interrupts, sleeps in `WFI`
and wakes when its own SysTick becomes pending every 10 ms, so no handler
runs and no vector is fetched in normal operation; every vector names the
SRAM function `core1_fault`. It never divides and uses no jump table, since
both would call into flash.

`tools/verify_rp2040_core1_image.py` holds each linked kernel to this rule,
and the emulation gate runs the linked code with no flash mapped.

## Word ownership

| Word | Stored by | Read by |
| --- | --- | --- |
| `core1.c1_obs`, `core1.c1_faults` | core 1 | core 0 (sysctl) |
| request ring: `cr_windex` and entries | core 0 | core 1 |
| request ring: `cr_rindex`, `cr_bellerr` | core 1 | core 0 |
| reply ring: the mirror image | | |
| `core1_probe[0]`, probes raised | core 1 | core 0's NMI |
| `core1_probe[1..3]`, probe answered, PC, LR | core 0's NMI | core 1 |
| tick record `clkcatch.stamp_us` | core 0 | core 1 |
| `core1.c1_vec`, `core1_nmi_mask`, supervisor state | core 0 | |
| `SYSCFG.PROC0_NMI_MASK` | core 0 | |
| `TIMER.INTF` bit 3 | core 1 sets, core 0 clears, through the atomic aliases | |
| watchdog `SCRATCH1..3` | core 0 normally; core 1 once, immediately before it resets the chip | |

Core 0 re-initializes the rings and the vector table only while core 1 is
in reset or parked in the boot ROM.

## Launch and park

`Reset_Handler` sets `PSM.FRCE_OFF` bit 16 before it touches SRAM. A reset
through `AIRCR.SYSRESETREQ` reaches core 0 alone (datasheet 2.4.2.9), so
without that store a core 1 left running would execute the previous
kernel's SRAM while the reset code overwrites it.

`core1_park()` resets core 1 into the boot ROM: set `FRCE_OFF`, read it
back, clear it, withdraw the NMI route and the forced interrupt. It is safe
from any context. `boot()`, the kernel-fault path in `fault.c` and both USB
reset requests call it before they stop taking clock ticks or reset core 0.

`core1_launch()` parks core 1, initializes the rings and vectors, consumes
the zero the boot ROM pushes when it reaches its wait loop, then sends 0, 0,
1, vector table, stack pointer, entry and requires each word echoed
(datasheet 2.8.2). The whole exchange is bounded by 10 ms of the microsecond
timer. The first launch happens one second after `clkstart()`, from the
supervisor callout, so the tick record is live before core 1 samples it.

## The observer

Every 10 ms core 1 reads the declared masked section (`SCRATCH1`,
`machine/watchdog.h`), core 0's tick record, and `TIMERAWL`, and
`core1_observe()` in `core1.h` decides:

- The record moved since the last sample: nothing.
- The record stood still across two samples with no section declared at
  either, and is 100 ms old: a stall. Count it, keep the longest, and raise
  the probe once.
- The record stood still for 4 s, declared section or not: a hang. Raise
  the probe again, and on the next sample reset the chip.

Two samples are required because core 1 reads the section and the record
at different instants: one sample can pair a record from inside an erase
with a section read after the erase cleared it. `flash_erase()` and
`flash_program()` advance the record before they clear the section, so the
pair cannot repeat.

The 4 s bound exceeds every section the kernel masks on purpose; the
longest is a sector erase with a 400 ms maximum. A debugger that halts core
0 for longer than 4 s while core 1 runs will see the chip reset: park core
1 first with `sysctl -w machdep.core1=0`.

## The probe

Core 1 forces timer alarm 3's interrupt through `TIMER.INTF`. No driver
arms or enables IRQ 3. `SYSCFG.PROC0_NMI_MASK` bit 3 routes it to core 0's
NMI, which PRIMASK and a HardFault in progress cannot hold off.
`NMI_Handler` stores the stacked PC and LR, then the count of the probe it
answers, and clears the forced interrupt. Core 1 treats the PC as valid
only when the answered count equals the raised count, so an unanswered
probe is recorded as zeros and never as an earlier stall's address.

The NMI vector and handler are in flash. `watchdog_site()` therefore
withdraws the route when a section is declared and restores it when the
section clears; both calls run with execute-in-place up, and core 0 is the
register's only writer. `check-core1-image` requires every source line
that takes execute-in-place down to sit inside a declared section.
`bootsel_pressed()` floats the flash chip select without a declaration; its
only caller is `startup()`, before core 1 exists.

## The record a reset leaves

Before it triggers the watchdog, core 1 stores `WD_SITE_STALL` in
`SCRATCH1`, the answered PC in `SCRATCH2` and the LR in `SCRATCH3`, unless
a section was declared, in which case the section's own record stands. The
scratch registers survive the watchdog reset (datasheet 4.7.6). The next
boot prints

    watchdog: reason 2, last masked site 3 arg 1000f44c
    watchdog: core 0 stalled at pc 1000f44c lr 1000f4a1

and keeps the values readable as `machdep.watchdog_reason`,
`machdep.watchdog_site`, `machdep.watchdog_arg` and
`machdep.watchdog_arg2`. Resolve the PC with `arm-none-eabi-addr2line -e
unix.elf`.

## The supervisor

`core1_poll()` runs once a second as a callout and makes every ring access
core 0 makes. It empties the reply ring, then `core1_sup_tick()` decides:
send ping number n stamped with the launch count; or, after two periods
without the matching answer, park core 1 and launch it again after 1, 2 and
then 4 seconds; or, after three restarts, leave it parked. A launch that
fails counts as a restart.

## Operating it

| Node | Meaning |
| --- | --- |
| `machdep.core1` | 0 parked by request, 1 running, 2 waiting to launch, 3 parked after three restarts. Write 0 to park, nonzero to launch with a fresh restart budget |
| `machdep.core1_restarts` | restarts since the last start |
| `machdep.core1_stalls` | stalls of core 0's clock with no section declared |
| `machdep.core1_gap_us` | the longest of them |
| `machdep.core1_pc`, `machdep.core1_lr` | where the last answered probe found core 0 |
| `machdep.core1_faults` | exceptions core 1 took |
| `machdep.core1_stall_ms` | write n, at most 10000: core 0 masks interrupts for n ms with no section declared, to test the report |

## Settings

| Setting | Default | Where | Bound |
| --- | --- | --- | --- |
| `C1_PERIOD_US` | 10,000 | `core1.h`, or `options "C1_PERIOD_US=..."` | fits core 1's 24-bit SysTick at `CPU_KHZ`; at most half of `C1_STALL_US` |
| `C1_STALL_US` | 100,000 | the same | below `C1_HANG_US` |
| `C1_HANG_US` | 4,000,000 | the same | above the 400 ms sector erase; raise it for a kernel run under a debugger |
| `C1_MISS_MAX`, `C1_RESTART_MAX` | 2, 3 | `core1.h` | |
| `C1_LAUNCH_US` | 10,000 | `core1.c` | a failed launch holds off this many microseconds of clock ticks, because callouts run inside the clock interrupt |
| `CRING_SLOTS` | 8 | `core1.c` | a power of two; one ping is in flight at a time |
| `C1_STACK_WORDS` | 40 | `core1.c` | the emulation measures 88 bytes used and requires 64 left |

## Left out, and what would bring each in

- Core 1's MPU stays off. The decided design reserves it as the privilege
  list of a service that writes kernel memory; the observer stores only its
  own words in the shared block, the reply ring, one timer bit and, once,
  the watchdog. The first core 1 service that takes a buffer brings it in.
- `BUSCTRL.BUS_PRIORITY` stays at reset, equal priority. Core 1 runs a few
  hundred instructions a hundred times a second; a service that streams
  SRAM would warrant giving core 0 priority, decided by the bus
  performance counters.
- Core 1 prints nothing. A UART0 line at the moment of a stall would reach
  only a host with an adapter on GP0, and the scratch-register record
  reaches every host at the next boot.
- A user-mode fault report runs at HardFault priority with core 1 still
  running; one that took 100 ms would be counted as a stall and probed
  inside the handler. None was seen in the board run.
- The PICOCALC kernels: see the head of this document.

## Cost

Measured on the PICO kernel with `arm-none-eabi-size` and `nm`,
`arm-none-eabi-gcc` 16.2.0 at `-Os`: 1,828 bytes of kernel flash, of which
544 are the load image of core 1's SRAM code, and 576 bytes of `.bss` (the
256-byte shared block and its alignment, a 160-byte stack, and supervisor
state). Core 1 wakes 100 times a second for a few hundred instructions and
sleeps otherwise.

## Evidence

| Claim | Gate | Class |
| --- | --- | --- |
| The observer's and supervisor's decisions, including the two-sample rule, the timer wrap, backoff and stale replies | `check-core1`, 24 mutants | host |
| The rings lose and duplicate nothing | `check-corering`, 16 mutants | host |
| Core 1's code and literals are in SRAM in every built kernel; every execute-in-place window is declared | `check-core1-image`, calibrated on four defective fixtures | linked image |
| The linked machine code launches through the launch protocol, answers pings, probes once per stall through MSP and PSP frames, stays silent across declared 400 ms sections, resets with the right record in three hang cases, restarts a silent core 1 with backoff, and bounds a launch the boot ROM never completes | `check-core1-emulation`, 17 mutant kernels | emulated |
| The real boot ROM launches core 1, the rings carry pings through a whole boot and login with no stall recorded across its flash writes, and an injected 300 ms stall is counted once | `check-renode`, calibrated with a kernel whose core 1 never answers | Renode |
| Launch, ping, probe with PC and LR, silence across flash writes, hang reset with record, park and relaunch, on one Pico | the board run below | board |

## Board run

One Raspberry Pi Pico (boot ROM V3), PICO kernel and root image flashed
together on 2026-11-10, console over USB CDC-ACM, commands typed as root,
and the same steps once more through `tests/rp2040/core1/board_core1.py`,
which passed all seventeen of its checks.
The kernel was built from this tree with `arm-none-eabi-gcc` 16.2.0;
`core1_stall` occupied 0x1000f4b8 to 0x1000f4e3 and `core1_sysctl`
0x1000f4e4 to 0x1000f59b in it.

| Step | Observed |
| --- | --- |
| Ten seconds after boot | `machdep.core1=1`, restarts 0, stalls 0, faults 0 |
| Write 300 to `machdep.core1_stall_ms` | stalls 1, `core1_gap_us=296063`, `core1_pc` 0x1000f4cc (inside `core1_stall`), `core1_lr` 0x1000f57f (inside `core1_sysctl`), restarts 0 |
| Write 232 KB to the root until it filled, `sync`, remove, then piped commands that swap | stalls still 1, restarts 0, faults 0 |
| `reboot` | next boot `watchdog_site=0`, `core1=1`, stalls 0 |
| `sync`, write 6000 to `machdep.core1_stall_ms` | the board reset itself; next boot `watchdog_reason=2`, `watchdog_site=3`, `watchdog_arg` 0x1000f4cc, `watchdog_arg2` 0x1000f57f, `core1=1` |
| Write 0 to `machdep.core1`, inject 300 ms | `core1=0`, stalls stays 0 |
| Write 1, wait five seconds, inject 300 ms | `core1=1`, restarts 0, stalls 1, same PC |
| Park and launch five more times | `core1=1` |

On this board, then: the boot ROM's launch completes after a forced-off
reset, `WFI` wakes core 1 on its pending SysTick with interrupts masked,
the forced timer interrupt reaches core 0 as NMI through `PROC0_NMI_MASK`
while core 0 holds PRIMASK, flash erases and programs inside declared
sections raise no stall, a watchdog trigger written by core 1 resets the
chip and preserves the scratch registers, and a parked core 1 survives
neither `reboot` nor the reset. One board and one run of each step; no
step was repeated for a rate.

Not established by any of them, nor by the board run:

- A restart of a core 1 that stopped answering on silicon. The emulation
  covers the supervisor's path; the board run parked and launched core 1
  by request, which exercises the same registers, and no fault was induced
  in core 1 itself.
- A hang inside a declared section, an unanswered probe, and a stall with
  core 0 in a HardFault, on silicon.
- Write ordering between SRAM banks under sustained ring traffic: one ping
  a second is the only load the rings have carried on a board.
- That no section of the shipped kernel masks interrupts for 100 ms
  without declaring itself, beyond the workload above.
  `machdep.core1_stalls` answers that for any workload run on a board.
- Anything on a PicoCalc.

`tests/rp2040/core1/board_core1.py` is that procedure as a script; it
opens no port unless `DISCOBSD_BOARD_CORE1` is exactly `run`. Its steps,
for a kernel or board that has not run it: boot a kernel with
the option and confirm `machdep.core1` reads 1 and `machdep.core1_restarts`
0 after ten seconds; run a swap-heavy workload and confirm
`machdep.core1_stalls` stays 0; write 300 to `machdep.core1_stall_ms` and
confirm one stall whose PC lies in `core1_stall`; `sync`, write 6000, and
confirm the board resets by itself and the next boot reports site 3 with a
PC in `core1_stall`; reboot and confirm a parked core 1 does not report
the reboot as a stall.
