# Memory ownership over instruction tuning: the plan and what landed

The kernel's largest gains on the RP2040 come from changing who owns SRAM
and from reusing the compression machinery the SwapRAM tier already pays
for, not from instruction-level tuning. This note records the plan in its
implementation order, the baseline it started from, and the measured
result of each step as it merged into the port. Every number below is
read from the linked ELF (`readelf -S`, `arm-none-eabi-nm -S`) or from
a board session over the USB console.

## Baseline (port main at 63447526, 2026-09-13)

| quantity | value |
|---|---|
| kernel .text | 95,754 bytes (0x1760a) |
| kernel .bss | 108,048 bytes (0x1a610), 65,536 of it the SwapRAM pool |
| `longjmp`/`resume` | 3,152 bytes, 3,072 of them the expanded u-area exchange |
| `usbd` | 8,392 bytes, 8,192 of them the TX ring |
| user window | 96 KB |
| SwapRAM admission | worst case: input plus an eighth plus four bytes per segment |

## Order

1. Recompile every object when a Config option changes and gate the
   linked SwapRAM tier against the configuration.
2. Replace the 3,072-byte expanded u-area exchange with a Thumb-1 loop.
3. Move the USB TX ring into SRAM4 and SRAM5.
4. Reserve SwapRAM at the counted compressed size instead of the worst
   case.
5. Shrink the SwapRAM pool and hand the recovered SRAM to the user
   window.
6. Replace the fixed user/SwapRAM division with a lifetime-managed arena.
7. Compressed a.out container decoded by the kernel's heatshrink decoder
   during exec and clean-text restoration.
8. Flash-buffer lifetime graph and overlay of mutually exclusive buffers.
9. Convert tool-private COMMON globals to static, verify with
   `-fno-common`, then extend multicall consolidation.

## Results

### 1. PARAM stamp and SwapRAM link gate (port PR #27)

`.deps` recorded headers alone, so an option edited in Config left every
object stale. Makefile.rp2040 writes PARAM to `.params` at parse time
when the text differs and every object depends on it.
`tools/verify_rp2040_swapram_link.sh` reads the linked kernel and
`vm_swap.o`: with `-DSWAPRAM` the five entry points must be linked and
referenced and `sr_pool` must measure `SWAPRAM_KB`; without it none may
appear. `bmake MACHINE=rp2040 check-swapram` runs it over PICO and
PICO_UART. Falsified both ways: a 16 KB edit to the generated Makefile
recompiled swapram.c and linked a 0x4000 pool, and a 64 KB claim against
that pool was refused.

### 2. u-area exchange loop (port PR #28)

`longjmp` went from 3,152 to 92 bytes; .text from 95,754 to 92,698. The
cost is 512 instructions of loop control per process switch.
`tests/rp2040/uarea_exchange` cuts the loop from locore.S between markers
into a qemu-arm harness that checks all 3,072 bytes each way and both end
pointers; a wrong register list or step-back fails it. On the board: boot
to login, a 40-fork loop, nested pipelines, and cat | sort | uniq | wc.

### 3. USB TX ring in the scratch banks (port PR #29)

RP2040.ld names SRAM4 and SRAM5 as SCRATCH, kern.ldscript places a
NOLOAD `.scratch` section there and asserts `usb_tx_ring` lies inside it
(guarded by DEFINED for PICO_UART). .bss fell from 0x1a610 to 0x18610 and
`usbd` from 8,392 to 200 bytes. The ring is never cleared: head and tail
stay in .bss and every byte is written before it is read. On the board:
boot messages arrive through the ring and 6,070 and 1,525 bytes of
output match the board's own `wc -c`.

### 4. Counted SwapRAM admission (port PR #30)

`swapram_out` takes the data and stack addresses, encodes each into a
discarding sink, and reserves the counted length; the u area keeps the
worst case because the clock interrupt writes accounting into the
current process's u between the count and the capture. `swapram_put`
panics with `count mismatch` when a data or stack segment does not
compress to the counted length. The cost is a second encode of data and
stack per swapout. The host test checks count equals write, an
exact-size buffer succeeds, and one byte less fails. On the board, with
the trace on: 321 swaps of a 30-fork loop, every image admitted at its
counted size, no flash fallback, no panic.

### 5. 16 KB pool, 144 KB user window (port PR #31)

SWAPRAM_KB 64 to 16, USERRAM 96K to 144K, RAM moved to 0x20024000 with
106K. kern.ldscript asserts the five SRAM regions tile with no gap or
overlap and machdep.c panics at boot when USERRAM and machparam.h
disagree; MAXMEM now comes from machparam.h, and the libc ROM float
resolver bounds its descriptor check with USER_DATA_END instead of a
literal 0x20018000. Kernel .bss fell from 100,160 to 51,008 bytes with
about 57 KB of headroom below the u areas. On the board, with the new
kernel and filesystem: a natively compiled program with a 120,000-byte
bss runs and exits 0, one with a 150,000-byte bss is refused at exec, a
20-fork loop and nested pipelines complete. Two lessons from the run:
the console's input line limit silently truncates a long `echo` and
leaves the shell at a continuation prompt, and `distrib/rp2040/flash.uf2`
carries the filesystem alone, so a kernel change needs `unix.uf2` loaded
in the same BOOTSEL visit.

### 8. Flash staging buffers overlaid (port PR #32)

flash.c's raw swap path stages a partial sector in a 4 KB buffer and the
Dhara path stages a partial trailing unit and a page move in two 1 KB
buffers; both paths run inside flstrategy under splbio from entry to
exit and neither sleeps, so the sector buffer now lies over the two unit
buffers in one union. An owner word makes the argument checkable: each
flstrategy path refuses to enter while the union is held, and
dhara_nand_copy, which Dhara also reaches from dhara_map_sync at close
and dhara_map_resume at setup, panics if the swap path holds it. Kernel
.bss fell from 51,008 to 48,960 bytes. Dhara's own page buffer and the
boot2 copy stay separate: the first lives as long as the map, the second
is read on every XIP re-entry. On the board with six background processes
forcing swap traffic, copies of /bin/sh, /etc/rc, /etc/passwd and ten
copies of /etc/group compare equal by cmp and md5 after sync.

### 9. COMMON symbol inventory

A userland build with `-fno-common` (`bmake MACHINE=rp2040
COPTS="-Os -fno-common" build`) fails to link 27 programs on multiple
definitions: atc, awk, backgammon, basic, battlestar, caesar, canfield,
cribbage, diff, forth, fsck, hangman, med, mille, pom, primes, re,
robots, sail, sed, sh, sl, snake, tail, tip, trek, worm. The games
dominate; sail alone carries a dozen shared structures declared without
extern in its header. Each is a header that defines rather than declares
a variable, so the conversion is mechanical per program: one definition
in a .c file, extern in the header, static where the name is private.
Multicall consolidation of these tools waits on that conversion.

## Open

Step 6 needs the pool and window to share one arena with resident
expansion taking precedence: the window is now fixed at 144 KB and the
pool at 16 KB, and an arena would let a process that fits in 160 KB run
while the pool is empty. Step 7 needs a container format, bounded
decoder output, and corrupt-stream rejection before process commitment;
the decoder is already linked and costs no further text. Step 9's
conversion is inventoried above and unstarted.
