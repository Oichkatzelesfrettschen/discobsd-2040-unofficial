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
emits `include/syscall.h`, `sys/sys/syscall_dispatch.h`,
`sys/kern/init_sysent.c`, and `sys/kern/syscalls.c`. The master preserves this tree's ABI: slot 23 is
`__sysctl`, dispatch slot 66 is `vfork`, `SYS_vfork` remains the public alias
for slot 2, and every handler retains the `void handler(void)` calling
convention. A donor syscall master is unsuitable because equal slot numbers
name different operations across the small BSD descendants.

`sys/kern/errlist.master` similarly emits the one shared errno-message pool
used by RP2040 and STM32. Both pools are stored in less than their text.
The trace names carry no terminator and form one common superstring, built
by maximum-overlap merging, in which a name lies inside another or across
the seam of two more; a 16-bit entry holds a 10-bit offset and a 6-bit
length, and `syscall_name()` returns the span. The errno messages are a
grammar: up to 128 rules, each a pair of symbols naming a character or an
earlier rule, chosen by replacing the most frequent adjacent pair until
none occurs three times. `kernel_errmsg()` expands one message into a
`KERNEL_ERRMSG_MAX`-byte caller buffer with recursion four rules deep at
most. On the PICO kernel the two encodings take 1,068 bytes less flash
than terminated strings. The syscall
dispatch keeps its function pointers in a separate naturally aligned array;
ARMv6-M cannot safely trade alignment for a packed pointer record.

The guarded private dispatch header owns handler prototypes and array types
without device or process prerequisites. `systm.h` and kernel `glob.h` retain
compatible include entry points. The argument-count declaration uses the
underlying unsigned-byte type so Smaller C can accept the interface without
parsing unrelated 64-bit integer typedefs. The metadata gate checks generation,
types, all configuration-selected rows and the preserved numeric assembly ABI.

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
| process | 132: the 100-byte `struct proc` plus the SwapRAM `sr_tab` and `sr_seg` rows |
| inode | 84 |
| file | 16 |
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

`tests/rp2040/capacity-corpus.json` is the finite admission denominator.
Each case names its resource, normal/exhaustion/fault/recovery obligation,
fixture, execution class, receipt artifact, dependencies, expected result and
recovery result. `check-capacity-corpus` binds the six active values to
`sys/arch/rp2040/include/machparam.h`, verifies decision coverage, dependency
closure and fixture existence, and requires both Renode and board cases for
every admission obligation. A `reduce-approved` row additionally requires
retained receipts for every obligation and completed transitive dependencies.
A passing registry gate establishes a complete declared plan; open receipts
remain open.

`check-kernel-ilp32` compiles the production `subr_capacity.c` against the
RP2040 compact table configuration. Its capacity fixture executes process,
inode, file, clist and buffer occupancy, retained peaks, minimum-free values,
failure attribution, stack paint boundaries and latched saturation. Six
mutated production-source copies alter one predicate or monotonicity rule and
must fail the same fixture. The fixture establishes counter semantics at the
target data-model width. Injected `capacity_note()` calls establish counter
attribution, not allocator exhaustion. Separate host, Renode and board cases
own their declared exhaustion obligations. The fixture does not establish
workload peaks or silicon headroom.

The board cases `process-table-exhaustion`, `system-file-table-exhaustion`
and `incore-inode-exhaustion` name
`tests/rp2040/capacity_exhaustion/proctab.c`, `filetab.c` and `inotab.c`.
`fork1()` refuses with EAGAIN and notes one process failure only when
`freeproc` is empty, or for a non-root caller at `MAXUPRC` or the last slot;
`proctab` therefore runs as root. `newproc()` refuses with ENOMEM and notes
nothing when `swap_image_reserve()` finds no backing, so the fixture reads
the errno to attribute a refusal and reports ENOMEM as an unreached table.
`falloc()` notes a file failure only for ENFILE; `ufalloc()` refuses with
EMFILE at `NOFILE` (30) descriptors and notes nothing, and `dup(2)` shares a
file slot, so `filetab` reaches EMFILE through `dup` with the file counter
unchanged, then reaches ENFILE through holder processes that each stay far below
`NOFILE`. Because `NOFILE` exceeds `NFILE` here, one process could fill the
file table alone; the holders keep the refusal attributable to the table and
keep the fixture valid when the limits change.

`iget()` takes a slot for a new identity from the free list, which holds
the slots whose reference count is zero, and with that list empty notes one
inode failure and fails with ENFILE, the errno of a full file table. A
process references an inode without a file slot through its working
directory, so each holder of `inotab` changes into its own directory and
opens one file there: two references for one file slot. With the table
full, an `open` and a `stat` of two further files must both fail with
ENFILE while the inode counter alone moves and file slots remain free;
`stat` takes no file slot at all. Every path is relative to the working
directory, so no lookup needs a slot for an intermediate directory.
`inotab` ends as inconclusive when the free process or file slots cannot
carry enough holders.

Each child of `proctab` copies the parent image to swap. `swap_image_span()`
in `sys/kern/vm_swap.c` charges the data past the clean text, the stack and
the three u-area blocks, rounded up to `SWAP_IMAGE_ALIGN`. The stack is
`SSIZE` (2,048 bytes) plus the argument vectors by `exec_aout.c`, so at least
three blocks, and the data is at least one block: no OMAGIC child costs less
than 7 blocks rounded to 8, and `proctab` measures its own span through
`KERN_PROC_PID`. At `NPROC=16` a boot that leaves 6 slots in use admits 10
children, at most 80 of the 380 flash blocks, less the roughly four images
the 16-KiB SwapRAM pool takes (`SR_WORST` reserves at least 3,460 bytes per
image for the u area). With the process that runs the test and process 0
excluded, at most three system images sit in swap; the 60 blocks that
`docs/research/swap-exhaustion/plan.md` records for a small process put them
at 180 blocks. That leaves 120 blocks of margin before the swapper needs
headroom to exchange the resident image, so the process table is reachable
at `NPROC=16` on paper. The earlier table of 25 slots needed 160 blocks for
its 20 children and left about 40.
`proctab` prints the swap map, the image span and the number of children flash
alone holds before the first fork, and ends as inconclusive when ENOMEM
precedes EAGAIN. The arithmetic is derived from source and the retained
snapshot; it is not a board measurement.

One Pico run on 2026-11-11 reached both tables at `NPROC=25` and `NFILE=24`,
the limits before the reduction. With 6
process slots in use `proctab` forked 19 children, the twentieth and a
repeated fork refused with EAGAIN, the process failure counter rose by 2 and
no other counter moved, and every child was reaped before a recovery fork
succeeded. `filetab` reached EMFILE by `dup` at 30 descriptors with the file
counter unchanged, then three holders and the parent filled all 24 slots, two
opens refused with ENFILE, and the counter rose by 2.
`docs/research/board-capacity-exhaustion-58ec71f1.txt` is the transcript and
the receipt of the two board exhaustion cases.

One Renode run on 2026-11-11 reached both tables at those limits on the
emulated PICO_UART kernel through `check-renode-capacity`. The counts equal
the board's: 19
children from 6 occupied slots before EAGAIN, three holders and the parent
filling 24 file slots before ENFILE, each failure counter up by 2 and no
other counter moved, and occupancy back at 6 and 3 after the release.
`docs/research/renode-capacity-exhaustion-d76edd1a.txt` is the transcript and
the receipt of the two Renode exhaustion cases.

One Pico run on 2026-11-11 at `a9970f3d` holds the receipt of three more
board cases, `docs/research/board-capacity-cases-a9970f3d.txt`. Two SwapRAM
evacuations moved every pooled image to flash and all eight children read
back their data, stack and u-area patterns; `sigtest` passed its nine checks;
and `tar`, `find` and `du` run at once produced the outputs of their serial
runs with no failure on any table. That workload peaked at 10 processes, 19
inodes, 15 files, 17 clists and 3 buffers, and the root passed the next
boot's `fsck -p` with its free space unchanged.

Those receipts completed the deciding cases of the process and file
decisions, and `NPROC` fell from 25 to 16 and `NFILE` from 24 to 18: 6 slots
above the workload peak of 10 processes and 3 above the peak of 15 files.
The PICO kernel's bss fell from 44,760 to 43,416 bytes, 1,344 bytes of
kernel RAM; the process table is 1,664 bytes, the file table 432, and the
SwapRAM entry and segment tables that scale with `NPROC` are 384 and 136.
Giving the fields of `struct proc` and `struct file` their true widths later
shrank those slots to 100 and 16 bytes, so the process table is 1,600 bytes and
the file table 288 at the same limits.
Three runs on 2026-11-11 at `65d7244c` repeat the cases at the reduced
limits. On the Pico, `proctab` forked 10 children from 6 occupied slots
before EAGAIN and `filetab` filled 18 file slots with two holders and the
parent before ENFILE, each failure counter rose by 2, and every slot was
released (`docs/research/board-capacity-exhaustion-65d7244c.txt`).
`check-renode-capacity` reports the same counts on the emulated kernel
(`docs/research/renode-capacity-exhaustion-65d7244c.txt`). The evacuation,
signal-frame and storage-pressure driver passed its 16 checks on the same
boot with no failure beyond the four the exhaustion run left
(`docs/research/board-capacity-cases-65d7244c.txt`).

One Renode run on 2026-11-12 at `083b35c3` reached the in-core inode table
at `NINODE=24` on the emulated PICO_UART kernel. With 10 inodes referenced,
seven holders took the other 14, an `open` and a `stat` refused with ENFILE
with 10 of 18 file slots in use, the inode failure counter rose by 2 and no
other counter moved, and both calls succeeded after the release with
occupancy back at 10. `docs/research/renode-capacity-exhaustion-083b35c3.txt`
is the transcript and the receipt of `incore-inode-renode-exhaustion`. A
kernel whose `iget()` drops the failure note was run once as the known-bad
input and failed the gate on the counter. The board case
`incore-inode-exhaustion` has the fixture and no receipt.

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
silicon headroom. Until those records exist for a resource, its limit
remains unchanged: `NINODE=24`, `NCLIST=32`, `NBUF=4`, and `USIZE=3072` wait
on the open cases of `tests/rp2040/capacity-corpus.json`.

The retained C17/SUS stdio run exercises operator login, wheel `su`, 43
checked stdio and snapshot commands, 12 concurrent streams and a 45-second
post-probe quiet interval on the RP2040. It records peaks of 8 processes, 22
inodes, 15 files, 22 clists and 3 buffers with zero failure counters. The
kernel stack watermark reaches 1,828 of 2,048 bytes and leaves 220 bytes
untouched without saturation. The run supplies one named workload result; it
omits the required exhaustion controls, deliberate fault path and broader
signal/SwapRAM recovery corpus, so no limit moved on that run alone.

The retained decision rows keep `NPROC=16` and `NFILE=18` after their
reduction and `NINODE=24`, `NCLIST=32`, `NBUF=4`, and `USIZE=3072` at their
original values. The corpus preserves the greatest admitted workload
observation across runs rather than replacing it with a newer lower value:
10 processes, 22 inodes, 15 files, 22 clists, 3 buffers and 1,828 touched
kernel-stack bytes. The two-slot inode reserve and the 220-byte
paint remainder are especially narrow observations; neither becomes a safe
decrement without the declared exhaustion, fault, signal, SwapRAM and
recovery receipts.

`NMOUNT=1` and the four-buffer cache are already active. The USB transmit ring
occupies 8 KiB in SRAM4/SRAM5 scratch banks. Reducing that ring changes retained
console-output behavior and releases scratch-bank capacity; it does not by
itself enlarge the fixed 192 KiB process window.
