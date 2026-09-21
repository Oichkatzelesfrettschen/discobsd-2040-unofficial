<!--
Copyright (c) 2026 DiscoBSD

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
-->

# Compact kernel metadata and capacity evidence

## Generated metadata representations

`sys/kern/syscalls.master` is the single authority for syscall numbers,
argument counts, handlers, trace names, and public definitions. The generator
emits `include/syscall.h`, `sys/kern/init_sysent.c`, and
`sys/kern/syscalls.c`. The master preserves this tree's ABI: slot 23 is
`__sysctl`, dispatch slot 66 is `vfork`, `SYS_vfork` remains the public alias
for slot 2, and every handler retains the `void handler(void)` calling
convention. A donor syscall master is unsuitable because equal slot numbers
name different operations across the small BSD descendants.

`sys/kern/errlist.master` similarly emits the one shared errno-message pool
used by RP2040, STM32, and PIC32. Both pools use 16-bit offsets. The syscall
dispatch keeps its function pointers in a separate naturally aligned array;
ARMv6-M cannot safely trade alignment for a packed pointer record.

The compact representations remove this gross storage from an RP2040 kernel:

| representation | gross bytes |
| --- | ---: |
| one-byte argument counts beside aligned syscall pointers | 468 |
| pooled syscall names with 16-bit offsets | 549 |
| RP2040 device-switch rows above the last active major | 420 |
| 16-bit high and 8-bit low tty watermarks | 145 |
| pooled errno messages with 16-bit offsets | 164 |
| total | 1,746 |

Generation contributes determinism rather than a direct byte reduction. The
1,746-byte total compares the replaced representations only; it is not a
whole-kernel delta. Capacity instrumentation adds code and 100 bytes of BSS,
so a release claim records linked `size` output from identical configurations
before and after the complete change.

## Fixed-table counters

The RP2040 kernels expose `kern.capacity` when `CAPACITY_STATS` is enabled.
The read-only, versioned structure records fixed-table occupancy, allocation
failures, and overwritten kernel-stack bytes. The PICO and PICO_UART
configurations enable the option. The counters collect evidence for a later
capacity decision; they do not change `NPROC`, `NINODE`, `NFILE`, `NCLIST`,
`NBUF`, `USIZE`, or the 8 KiB USB transmit ring.

`sys/sys/capacity.h` defines ABI version 1. A consumer accepts the structure
only when the returned length, `version`, and `size` match its compiled
definition. `sbin/sysctl` prints the structure with:

    sysctl kern.capacity

Each fixed-table row reports:

| field | meaning |
| --- | --- |
| `limit` | compiled slot count |
| `current` | slots occupied when the snapshot runs |
| `peak` | greatest occupancy observed at an allocation event or snapshot |
| `minimum_free` | smallest `limit - current` value observed |
| `failures` | allocation attempts refused or forced to wait for this resource |
| `slot_bytes` | bytes released by one eventual limit decrement in this configuration |

The allocation seams and occupancy rules are:

| resource | success or pressure seam | occupied slot |
| --- | --- | --- |
| process | `newproc()` / `fork1()` | `p_stat` is nonzero, including `SIDL`, live states, and `SZOMB` |
| inode | `iget()` and `igrab()` | `i_count` is nonzero |
| file | `falloc()` | `f_count` is nonzero |
| clist | `putc()` and `b_to_q()` | one `CBSIZE` payload removed from `cfreecount` |
| buffer | `notavail()` / `getnewbuf()` | `B_BUSY` is set; waiting for every buffer increments failures |

`pqinit()` links slots 1 through `NPROC - 1` onto `freeproc` with a zero
state. `newproc()` removes a slot and assigns `SIDL`; exit retains `SZOMB`
until `wait4()` clears the state and returns the slot. The `p_stat` predicate
therefore counts every process-table slot unavailable to `fork1()`, including
zombies.

`cfreecount` measures free character payload bytes rather than objects. The
RP2040 and STM32 pool definitions align `cfree` to `sizeof(struct cblock)`
because `cinit()` rounds the first address with `CROUND`. The explicit
alignment lets `cinit()` add exactly `CBSIZE` for each of the `NCLIST` blocks;
every allocation or release subtracts or adds the same unit. The live count is
therefore `NCLIST - cfreecount / CBSIZE`.

The retained configuration reports these per-slot costs:

| resource | bytes per slot |
| --- | ---: |
| process | 136: `struct proc` plus the SwapRAM `sr_tab` and `sr_seg` rows |
| inode | 84 |
| file | 24 |
| clist | 32 |
| buffer | approximately 1,060: `struct buf` plus one 1 KiB data block |

The runtime structure derives every value with `sizeof` and the active
configuration. A changed ABI or configuration can therefore report a changed
cost instead of inheriting these measurements.

## U-area stack watermark

The linker reserves 3,072 bytes each for `u` and `u0`. The first 1,024 bytes
hold `struct user` and explicit padding; a compile-time assertion rejects a
structure that crosses that boundary. The remaining 2,048 bytes carry the
kernel stack. The reset assembly fills that range in both exchange partners
with `UAREA_STACK_SENTINEL` before `SystemInit()`. Both regions must start
painted because `longjmp()` exchanges all 3,072 bytes.

The scheduler samples the active `u` area immediately before each context
switch, and a `kern.capacity` snapshot samples it again. The scanner advances
from the low address until the first non-sentinel word. `stack_peak` reports
the largest overwritten suffix, `stack_minimum_free` reports the smallest
untouched prefix, and `stack_saturated` latches when the first word was
overwritten.

The watermark measures overwritten words, not the minimum stack pointer. A
reserved but unwritten part of a frame remains invisible. A written word equal
to the sentinel can make the apparent peak smaller by one word; consecutive
matching words can make the error larger. Interrupt, signal, fault, swap, and
context-switch paths must therefore be driven deliberately, and disassembly
must confirm the reset paint and exchange boundaries. A watermark alone cannot
justify changing `USIZE`.

## Admission rule for smaller limits

A capacity reduction requires one named workload corpus and separate Renode
and board runs. Each run records the exact kernel identity, configuration,
workload commands, starting and ending `kern.capacity` snapshots, allocation
failures, and console transcript. The workload must cover boot and login,
parallel process creation through the supported shell, file and inode pressure,
terminal input and output, buffer-cache writeback, signals and deliberate MPU
fault handling, SwapRAM eviction and restoration, and repeated context
switches.

The proposed limit must retain an explicit reserve above the greatest measured
peak. Every failure counter must remain zero except a deliberately calibrated
exhaustion case, and the calibrated case must increment the intended counter.
Renode evidence establishes the emulated path; only the board run establishes
silicon headroom. Until those records exist, `NPROC=25`, `NINODE=24`,
`NFILE=24`, `NCLIST=32`, `NBUF=4`, and `USIZE=3072` remain unchanged.

`NMOUNT=1` and the four-buffer cache are already active. The USB transmit ring
occupies 8 KiB in SRAM4/SRAM5 scratch banks. Reducing that ring changes retained
console-output behavior and releases scratch-bank capacity; it does not by
itself enlarge the 144 KiB process window.
