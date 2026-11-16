# The program window under large swap

The owner asked how a swap device as large as the PicoCalc's 8 MB PSRAM
affects the program window (decision D116). The short answer: large swap
raises how many processes can exist at once and makes each context switch
two orders of magnitude cheaper than flash swap; it leaves the largest
runnable program exactly where it is. The 192 KB window at 0x20000000 is
the bound, because the RP2040 has no address translation and the kernel
keeps one process image resident at a time. This note derives both halves
from source, then costs the four designs that could raise the bound.

Evidence classes follow `docs/research/memory-devices.md` (branch
`docs/memory-devices`, pull request #649, cited as `d2ffbd83:path`):
source (SRC), data sheet (DS), model (M), build (B) and inference or
arithmetic (I). Source was read at `origin/main` 268483f7. Kernel sizes come
from a build of that commit with arm-none-eabi-gcc 14.2.1 (Debian
`15:14.2.rel1-1`); program sizes from `bmake MACHINE=rp2040 build` of the
same commit and compiler. No board, Renode or qemu run belongs to this note.

## What bounds a program today

| Bound | Value | Source |
| --- | --- | --- |
| User window | 192 KB at 0x20000000, ending at 0x20030000 | `sys/arch/rp2040/include/machparam.h:135-138` |
| Admission at exec | text + data + bss + heap + stack must not exceed `MAXMEM` (the window) | `sys/kern/exec_subr.c:420-425` |
| Residency | one process image resident; every other image sits in swap | `sys/arch/rp2040/doc/MPU.md:5-7`, `sys/kern/vm_sched.c:44-100` |
| Address translation | none: the MPU grants permissions over eight regions and reports no fault address on ARMv6-M | `sys/arch/rp2040/doc/MPU.md:27-30`, `:397-403` |
| Largest swap image | 192 KB plus the 3 KB u area, 196 blocks (`SWAP_IMAGE_GUARD`) | `machparam.h:117-123` |

An OMAGIC a.out loads its text into the window too, so text counts against
the 192 KB. The largest program the build ships is `games/warp/warp`:
73,940 B text, 788 B data and 20,616 B bss, 95,344 B before heap and stack
(B, `od` of the a.out header). `usr.bin/awk/awk` totals 61,204 B and
`bin/sh/sh` 44,612 B. Every shipped program fits with room to spare; the
window binds the programs the port does not yet ship (a larger compiler, an
editor with a large buffer, a language runtime).

No swap size changes any row of that table. A swap image is a copy of
window contents, and swapin writes it back to the same addresses
(`sys/kern/vm_swap.c:36-49` sizes it as data past the clean text, the stack
and the u area). A larger device stores more copies; it adds no address.

## What large swap changes

### Capacity: how many images fit

A swap image occupies one contiguous run rounded to `SWAP_IMAGE_ALIGN`, four
1 KB blocks (`vm_swap.c:36-49`, `machparam.h:109-116`), and the map keeps
one free run of `SWAP_IMAGE_GUARD` blocks while placement allows, so a fork
of the largest image always finds room (`machparam.h:117-123`,
`sys/kern/init_main.c:155-161`). The usable size is the device less the four
reserved blocks (`init_main.c:170-174`).

| Device | Blocks usable | sh-sized image, 20 blocks | Largest image, 196 blocks |
| --- | ---: | --- | --- |
| `fl1` raw flash, 384 KB (`dev/flash.h:105`) | 380 | 19; 9 with the guard kept | 1; none with the guard kept |
| PSRAM swap minor of #624, 2 MB | 2,044 | 102; 92 with the guard | 10; 9 with the guard |
| Whole PSRAM, 8 MB (`dev/psram_xfer.h:26`, `dev/psram.c:57-59`) | 8,188 | 409; 399 with the guard | 41; 40 with the guard |

The sh-sized image is the shell's 9,608 B of data and bss, a 4 KB stack and
the 3 KB u area: 10 + 4 + 3 = 17 blocks, rounded to 20 (I, the model inputs of
`docs/research/picocalc-device-first-memory.md` on pull request #644). The
compact map's 16-bit descriptors hold any of these devices: `machdep.c`
asserts `PSRAM_BYTES / DEV_BSIZE` fits a `u_short` (`sys/arch/rp2040/rp2040/machdep.c:159-168`).

### The binding limit moves from swap to the process table

`NPROC` is 16 (`machparam.h:70`). Slot 0 is the swapper and one image is
resident, so at most 15 images are ever outside the window together (I).
The process table binds instead of the device once the device holds 15
images and the guard:

    15 x span + 196 <= usable blocks

For the largest image that is 3,136 blocks, about 3.1 MB. The 8 MB part
therefore holds every process the table admits at every image size; the
2 MB minor of #624 does so for spans up to 123 blocks (about 120 KB images)
and binds before `NPROC` above that; `fl1` binds first for any image larger
than 12 blocks (I, the inequality above).

Each process slot costs kernel RAM whether or not swap could hold its image.
The GCC 14.2.1 build gives `proc` 1,600 B (100 B a slot), `sr_tab` 384 B
(24 B a slot) and `sr_seg` 136 B (8 B a slot, `NPROC + 1` entries), the same
on PICO and PICOCALC (B, `arm-none-eabi-nm -S`;
`machdep.c:140`, `sys/arch/rp2040/rp2040/swapram.c:69,90`), plus one byte a
slot of `sched()`'s `refused[]` on the swapper's stack (`vm_sched.c:51`).
Raising `NPROC` by 8 to use the PSRAM's capacity costs about 1,064 B of
kernel bss (I: 8 x 133 B). `sys/arch/rp2040/doc/CAPACITY.md:258-264` records
why `NPROC` fell from 25 to 16: six slots above a measured peak of 10
processes.

The swap map comment is stale against that reduction. `SMAPSIZ` is 29 and
its comment counts 23 swapped processes and 24 user slots
(`machparam.h:90-97`, written by 15ce5c1d when `NPROC` was 25). At `NPROC` 16 the
same reasoning gives 14 swapped images, three temporary devices and one
exec spool, or 15 slots during evacuation plus the three devices: 18 live
extents, 19 free runs and a terminator, 20 descriptors (I). The other 9
descriptors are 36 B of kernel RAM. The arithmetic is the comment's own;
whether the PSRAM allocator fragments more than the flash ring is
unmeasured.

### Latency and wear

Every switch between two user processes swaps one image out and another in,
because only one is resident. The cost per sh-sized image, from
`d2ffbd83:docs/research/memory-devices.md`:

| Tier | One sh-sized image | Basis |
| --- | --- | --- |
| PSRAM at 31.25 MHz quad | about 1.4 ms each way | M, `check-psram-pio` frame model, 12 MB/s |
| SwapRAM pool admission | 256 ms | M, cycle model of #644 |
| `fl1` erase and program | 257 ms | DS arithmetic, W25Q16JV typical times |

A largest image of 196 KB moves through the PSRAM in about 16 ms each way at
the same 12 MB/s (I). `fl1` erases five 4 KB sectors per sh-sized swapout;
its 96 sectors at 100,000 cycles each allow about 1.9 million such swapouts
if the ring spreads them evenly (I, DS endurance). The PSRAM has no wear
mechanism (DS: DRAM cells).

What large swap changes, plainly:
- multiprogramming depth: from about 9 shells on `fl1` (with the guard) to
  the process table's 15 images on the 8 MB part;
- swap wear: none on the PSRAM;
- switch latency: from about half a second for an sh-to-sh switch on
  `fl1` or the pool to about 3 ms on the PSRAM (M);
- a program's address space: unchanged.

## Designs that would raise the program bound

Without translation, every address a program touches must be backed by
memory at that physical address when it is touched. Three ways out remain:
move part of the program to other physical addresses (text to flash), reuse
the same addresses for different contents at different times under the
program's control (overlays), or keep data outside the address space and
reach it by explicit copy (files or a device). Demand paging belongs to
none of them, and the table records why.

| Design | Mechanism | Bound it raises | Kernel text, RAM (GCC 14.2.1 context) | Evidence it needs |
| --- | --- | --- | --- | --- |
| D1. Data in PSRAM files | the program keeps a large buffer in a file on the PSRAM /tmp minor of #624 and reads or writes blocks of it | data reachable by the program: up to the 6 MB minor; the window is unchanged | 0 beyond #624 | a program benchmark: 1 KB block copy about 83 us (M, `d2ffbd83:docs/research/memory-devices.md`); the #624 board run |
| D2. Raw PSRAM extent per process | a character minor whose open allocates a private PSRAM extent from a resource map, read and written with `pread` and `pwrite`, freed on close and exit | as D1, without file-system overhead | +300 to +600 B text, +4 to +8 B a process slot (I) | host model of the allocator with a leak and a double-free mutant; exit cleanup under Renode |
| D3. Text executed in place from flash | a second exec format whose text is linked at a flash address and stored contiguously in a reserved XIP region (the Dhara root relocates pages, so text cannot execute from it); one of MPU regions 4-7 grants user r-x over that region | data, bss, heap and stack get the whole 192 KB; text grows to the region size | +800 to +1,600 B text: format, region allocator and installer, MPU programming; +8 B a process slot (I) | the `doc/MPU.md` burden (written and read-back registers, privilege test, deliberate fault); a board measurement of XIP cache misses, since user text would share the 16 KB cache with kernel text (`dev/flash.c:318` toggles the cache for such a measurement); the flash budget, which is fully allocated today (128 KB kernel, 1,536 KB root, 384 KB `fl1`) |
| D4. Overlays, as 2.11BSD used | 2.11BSD's `A_MAGIC5` and `A_MAGIC6` executables carry up to `NOVL` = 15 text overlays (`211bsd/sys/h/exec.h:25,43-44`), loaded by `execve` (`211bsd/sys/sys/kern_exec.c:439-596`) and switched by reloading PDP-11 segmentation registers. Here a switch copies the overlay's text into a fixed overlay area of the window, from the PSRAM or from the executable, through a thunk on each cross-overlay call | text beyond the window, one overlay resident at a time | +400 to +900 B text: header parsing, an overlay-switch system call, per-process overlay state of about 8 B (I) | a GNU ld `OVERLAY` link and an `elf2aout` overlay header proven on qemu-user; switch time on the board (an 8 KB overlay from the PSRAM is about 0.7 ms at 12 MB/s, I); a review of function pointers and returns that cross overlays |
| D5. Page-wise demand copy-in | the MPU marks subregions absent; a HardFault handler decodes the faulting Thumb load or store from the stacked registers to recover the address, copies the subregion in from the PSRAM and retries | none: a page still has to live at its own physical address inside the window. It could only shorten swapin of an image whose untouched subregions stay out, which the PSRAM's 16 ms worst case does not need | high (I): an instruction decoder, subregion bookkeeping, fault-return paths | not pursued |

Segment-wise copy-in exists already in the useful sense: swap moves the data
span and the stack span, never the unused middle of the window
(`vm_swap.c:36-49`). The inverse lever, two smaller windows with relocation
at exec so that two processes stay resident, lowers the per-program bound
rather than raising it; `docs/research/xip-shared-libc/README.md` studies
the relocating loader it would need, and D3 shares that note's
execute-in-place mechanism (pull request #372).

Recommendation: D1 first. It costs no kernel text, it reuses the #624 minor,
and it gives a program a data space about thirty times the window today, at the
price of explicit I/O in the program. D3 is the only design that raises the
bound for unmodified code; it should wait for the #372 decision, since both
spend flash and an MPU region on the same mechanism. D4 is the historical
answer and carries the largest toolchain cost. D2 earns its text only if a
D1 measurement shows the file-system path is the bottleneck.

## Residual owner decisions

1. **The process table on PSRAM kernels.**
   - Keep `NPROC` at 16 everywhere (recommended): the measured peak is 10
     processes and kernel RAM is the tightest budget.
   - Raise `NPROC` to 24 on PicoCalc kernels with the PSRAM: eight more
     processes become possible for about 1,064 B of bss.
   - Make `NPROC` a per-kernel option without changing it: costs nothing now
     and lets a later measurement decide.
2. **The design to pursue for programs larger than the window.**
   - D1, data in PSRAM files (recommended): no kernel text, uses #624.
   - D3, text in place from flash: the only design that serves unmodified
     programs, but it needs flash space, an MPU region and the #372 decision.
   - D4, overlays: proven history on 2.11BSD, the largest toolchain cost.
   - None: the shipped programs fit; revisit when one does not.
3. **The stale `SMAPSIZ` sizing.**
   - Resize it to 20 with a comment derived from `NPROC` (recommended once a
     host model confirms the count): 36 B of RAM, and the comment states the
     current table.
   - Keep 29 and correct only the comment: no behavior change, 36 B unclaimed.

## Unverified and not run

- The PSRAM latencies are model figures; the PSRAM driver has not run on a
  board (`sys/arch/rp2040/doc/PICOCALC.md`).
- The 20-descriptor `SMAPSIZ` is arithmetic from the comment's own
  derivation, without a host run of the map under the PSRAM's larger ring.
- D2-D4 text costs are estimates from comparable code, not builds.
- The flash endurance figure assumes the `fl1` ring spreads erases evenly;
  no wear count has been measured on the board.
