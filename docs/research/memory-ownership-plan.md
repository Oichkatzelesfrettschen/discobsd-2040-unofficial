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

### 9. COMMON symbols converted, -fno-common on by default (port PR #35)

The arm userland compiled with `-Os -fcommon` because eleven programs
define their shared state in a header every translation unit includes
(fsck.h, sed.h, diff.h, tip.h, r.defs.h, snake.h, back.h, the battlestar
and sail externs.h, player.h, trek.h, sh's ctype.h: 408 multiply defined
names) and because sixteen more link against libraries that each carried
a tentative definition of a name another library owns: `errno` in ten
libm files and libc's exit.c, the termcap tuple `BC UP PC ospeed` in both
libtermcap and libcurses, `_win` in two curses files, and `nswaps` and
`nmapsegs` defined in vmf.h itself. The linker's `multiple definition`
messages under `COPTS="-Os -fno-common"` are the denominator; nm type C
counts alone overstate it, since a single tentative definition is legal.

Every header now declares `extern` and one .c file per program defines
(fsck main.c, sed0.c, diff.c, tip.c, r.main.c, snake.c, backgammon
subs.c, the battlestar and sail globals.c, pl_main.c, trek externs.c with
tags for its six anonymous structs, sh ctype.c); libc owns `errno`,
libtermcap the termcap tuple, refresh.c `_win`, vmf.c its counters.
`-fcommon` had merged backgammon's `char ospeed` with libtermcap's
`short ospeed` and snake's and backgammon's private `PC`/`UP`/`BC` with
the library's. share/mk/sys.mk now passes `-Os -fno-common` for arm; the
kernel's CMACHFLAGS keep `-fcommon`.

`tools/verify_userland_no_common.sh` rebuilds the seven libraries and the
27 programs by directory (lib/Makefile's FRC rule skips a subdirectory
whose mtime moved during the build, which is why a clean `bmake -C lib`
produced between one and twelve archives) and fails on any type C
symbol or link error: 34 rows ok. The full rp2040 build has zero COMMON
symbols in any userland object; each program's exported name set is
unchanged except `med` (nswaps, nmapsegs), `snake` (PC, UP) and `tail`
(errno) now importing from their owner; all 27 also compile with
`-Werror` after a dead counter left robots' rnd_pos.c. The 27 a.outs
total about 9 KB less text, GCC using section anchors once a global is
no longer common. On the board sh, sed, tail, diff, fsck at boot and awk
field arithmetic run from the flashed root (96 files, 814 used, 157
free blocks).

The multicall boxes localize each tool's globals with `objcopy
--keep-global-symbol`, which a COMMON symbol escaped; sbin/utilbox's
Makefile excludes sed, sort and find for exactly that aliasing, so they
are now candidates.

### Found by the board tests: the initial user stack was 4-byte aligned (port PR #36)

`awk 'BEGIN{print 1+1}'` prints `1.6e-154` or `2` depending on the byte
length of the environment, for the pre-PR-#35 binary and the new one
alike, with a 4-byte period. `exec_setupstack` (sys/kern/exec_subr.c)
places argv wherever the string byte count leaves it and sets the entry
SP a multiple of 8 below it, while the ARM EABI hands `_start` an 8-byte
aligned stack and `va_arg(ap, double)` rounds the address up to 8; the
decoded garbage is the double's high word followed by the next stack
word. PR #36 pads the argument block down one word when argv lands on
a 4-byte boundary; tests/rp2040/exec_stack_align/board_stack_align.py runs
awk and printf under twelve environment lengths on the board, all correct.

### a.out admission hardened (port PR #37)

N_GETFLAG shifted the masked flag field left by 26, so it read zero for
every header and exec_aout_check's flag test never rejected anything;
it now shifts right, N_SETMAGIC is its inverse, and tests/aout_header
round-trips every flag and machine id through six magics and checks a
million words decode into fields that reassemble them (`bmake
check-aout`). aout_layout_check in exec_aout.h refuses, before
exec_estab, a header whose size sums wrap, whose text+data+bss exceed
the window, whose file ends inside text or data, or whose entry lies
outside the text or lacks the Thumb bit; the process keeps its old
image. exec_check moved to the next format on any error, so an a.out
verdict such as ENOMEM was overwritten by the script checker's
ENOEXEC; it now continues only on ENOEXEC. On the board
(tests/rp2040/aout_admission) a header with no body and one with an
even entry get ENOEXEC, one with a 1 MB bss gets sh's "too big", and
the shell survives. Kernel text +72 bytes.

### tail scans backward by block (port PR #39)

tail kept the whole tail of its input in a 32,769-byte buffer chosen by
`#ifdef pdp11` rather than by the memory model, 32,960 bytes of bss in
every pipeline ending in it, and cut a run of lines longer than that.
It now finds the end of a seekable file by reading 1 KB blocks backward
and counting newlines, spools a pipe into a 4 KB buffer and past that
into an unlinked /tmp file, and keeps the newest 4 KB with an errno
report when the file cannot be made. Output is byte-identical to the
old tail on 1,248 differential cases (every option form, file and
pipe); usr.bin/tail/tests/tailcheck.py models the semantics (-n is the
bytes after the (n+1)-th newline from the end; -r supplies a newline to
a final partial line and one newline for empty input; -0 nothing) and
passes 2,016 cases with 70 KB lines, ringcheck.sh takes /tmp away under
bwrap. RP2040: bss 32,960 -> 5,244, a.out 4,040 -> 3,968 (a formatted
print would have linked the 3.5 KB _doprnt; messages go through write).
Board: a 31 KB piped tail takes 12.9 s against 6.2 s for the pipe alone
and 7.8 s for a copy to /tmp; the board's pipe throughput is about
5 KB/s. utilbox excluded tail for its bss; it is a candidate again.

### sed's compile/execute union: disproved

The proposed union of respace[10000] (compile) with genbuf[4000]
(execute) assumed respace is dead once the script is compiled. It is
not: sed0.c stores the compiled program in it (`rep->A.ad1 = respace`,
and each command's ad2, re1 and rhs are pointers into the same space),
and sed1.c reads it for every input line, through `match(ipc->A.re1)`
at line 360 and `dosub(ipc->A.rhs)` at 363, while genbuf holds the
substitution being built (lines 382-421). A union would overwrite the
program with the first substitution. genbuf is execution-only (sed0.c
touches it once, to set lcomend), so the only phase-exclusive pair is
genbuf against compile-time state that is small. No change.

### Pool evacuation to flash, transactional (port PR #40)

Step 1 of the exclusive SMALL/LARGE epoch: `swapram_evacuate` reserves
the flash extents for every pool image first (malloc3 at the expanded
sizes swapin reads) and unwinds them all on one shortage with the pool,
the map and every process untouched; then expands each image a block
at a time through a 1 KB stage into its extents, switches the process
to them after its last block (p_daddr, p_saddr, p_addr, as the flash
path of swapout sets them) and frees the pool entry last, so a process
is on exactly one tier at every instant. `swapram_admit` closes the
pool to new images for the duration; the swapper services the request
at the top of its loop; `machdep.swapram_evacuate` posts it (root) and
reads DONE or NOFLASH back, `machdep.swapram_images` counts the pool.
The host test (`bmake check-swapram-evac`) compiles the kernel's
swapram.c and subr_rmap.c against stand-in headers: byte-exact round
trip of eight images twice, five short maps refused whole with images
still restorable, exact-fit map taken whole, admission gating, and the
request path; valgrind clean. Board: tests/rp2040/swapram_evacuate
evactest, four patterned children, three in the pool at the request,
DONE and zero images after, all patterns intact from flash. Kernel
text +1,144, bss +1,328 (the stage block and the extent words). The
layout is unchanged: 144 KB window, 16 KB pool. Next: the SMALL/LARGE
state machine and per-process ceiling, then the 160 KB layout.

### SMALL/LARGE epoch and per-process ceiling (port PR #42)

Step 2, with SWAPRAM_BONUS at zero so no size decision changes yet.
`swapram_enter_large` asks the swapper for LARGE and sleeps; the
swapper evacuates, keeps the pool closed and switches the epoch, or
reopens and the request returns ENOMEM. P_LARGE (p_flag 0x0040) marks
a process admitted under LARGE; newproc inherits it, exit and an exec
that fits the window again drop it, and the last large process out
asks for SMALL. exec_estab and brk compare against
`swapram_ceiling(p)` = MAXMEM + bonus for P_LARGE and ask for LARGE
when a size exceeds the window but not the bonus. `machdep.swapram_epoch`
reads and, as root, requests; SMALL is refused while a large process
lives. Host test: entering evacuates and closes, a second process
joins free, a child inherits, leaving one of three keeps LARGE, the
last leaver reopens, a short map returns ENOMEM with SMALL and the
images intact, operator requests honored; valgrind clean. Board
(tests/rp2040/swapram_epoch, as root): 3-5 images, LARGE empties and
closes, children forked under LARGE never enter, SMALL reopens (3-4
images again), all patterns intact. Kernel text +484, bss +24.

Step 3 is the layout: place sr_pool at the start of the RAM region so
it abuts the window's end, set SWAPRAM_BONUS to its size, and make the
window top a value the epoch selects at the sites that use
__user_data_end (exec_aout, exec_subr, exec_elf, vm_swap, syscall.c and
sig_machdep.c stack growth, fault.c, kern_sig core dump, machdep).

## Open

Step 6 needs the pool and window to share one arena with resident
expansion taking precedence: the window is now fixed at 144 KB and the
pool at 16 KB, and an arena would let a process that fits in 160 KB run
while the pool is empty. Step 7 needs a container format, bounded
decoder output, and corrupt-stream rejection before process commitment;
the decoder is already linked and costs no further text. Step 9's
conversion landed in PR #35; extending the multicall boxes with sed,
sort and find is the remaining part.
