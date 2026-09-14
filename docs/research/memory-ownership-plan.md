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
| --- | --- |
| kernel .text | 95,754 bytes (0x1760a) |
| kernel .bss | 108,048 bytes (0x1a610), 65,536 of it the SwapRAM pool |
| `longjmp`/`resume` | 3,152 bytes, 3,072 of them the expanded u-area exchange |
| `usbd` | 8,392 bytes, 8,192 of them the TX ring |
| user window | 96 KB |
| SwapRAM admission | input + input/8 + four bytes per segment |

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
10. Replace the flash erase-sector staging buffer with one physical-page
    buffer and erase-aligned process images.

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

### Board tests found 4-byte initial user-stack alignment (port PR #36)

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

### The 160 KB LARGE window (port PR #43, verifier #44)

Step 3. The pool lives in a NOLOAD section kern.ldscript places first
in RAM, so its 16 KB sit at 0x20024000 where the 144 KB window ends
(link assert, and swapram_init checks the running image at boot);
SWAPRAM_BONUS is the pool's size, and USER_TOP(p) in sys/systm.h is
`__user_data_end` plus the bonus for P_LARGE. Every site that laid out or
bounded the window against `__user_data_end` asks USER_TOP: swapin's
stack, the a.out and ELF exec layouts, the stack-growth checks in
syscall.c and sig_machdep.c, the fault handler's frame bounds, the core
dump, and baduaddr (which otherwise would let a small process name the
pool's bytes to copyin). exec_estab decides the epoch before laying the
stack. The bonus is a fresh exec's alone: a running process's stack
already sits under 144 KB and the pool is above it, so brk never asks
(the brk hook from #42 came out). A leak found on the board: the exit
release had landed in endvfork, which a normal exit never passes, so
P_LARGE leaked and the epoch stuck LARGE; it lives in exit() now, and
machdep.swapram_large counts holders. Board, as root: bigtest (bss
150,000) runs under LARGE with the pool empty and its forked child's
copy intact from flash, three runs; hugetest (170,000) is refused as
"too big"; epochtest and evactest pass afterward with the pool
admitting again. Kernel text +240; `_sdata` moves up 16 KB. PR #44 fixed
the link verifier, which still measured sr_pool; the #43 merge went in
on a red check-swapram because the chain echoed the status instead of
gating on it.

Step 4 (three-extent SwapRAM) is deferred by decision until
fragmentation measurements justify it. Step 5, compressed executables,
is next.

### Packed a.out executables (port PR #45)

A packed executable is `struct exec` with the `EX_HSPACK` flag, then
`struct hsx` (signature, version, header length, codec, heatshrink window
and lookahead, the two packed stream lengths, the two expanded-stream
CRC-32s, a zero reserved field, and a header CRC-32), then the text and
the initialized data as two heatshrink streams. `a_text`, `a_data`,
`a_bss` and `a_entry` keep the raw image's values, so the loader lays the
process out from `struct exec` alone. Files:
`sys/sys/exec_hsaout.h` (container, `hsx_check`), `sys/kern/subr_crc32.c`
(tableless CRC-32), `sys/arch/rp2040/rp2040/hsx_stream.c` (the stream
loop), `sys/arch/rp2040/rp2040/hsx_decoder.c` and
`sys/arch/rp2040/include/hsx_decoder.h` (heatshrink built a second time
with lookahead 3), `sys/arch/rp2040/rp2040/exec_hsaout.c` (loader).

The checker recognizes the format by the flag and returns `EFTYPE` for
every recognized-but-malformed container; the dispatcher passes `EFTYPE`
through, so corruption never reaches the next format. The loader
preflights both streams into a discard buffer while the old image stands,
reproducing each stream's length and CRC-32; it calls `exec_estab` only
after preflight, expands again into the user window, and `SIGKILL`s the
process before entry if the second pass disagrees.

Executable-write exclusion lives in `sys/kern/exec_aout.c`:
`exec_text_hold` refuses a file open for writing and marks it `ITEXT`;
`access()` in `sys/kern/ufs_fio.c` turns `ITEXT` into `ETXTBSY` for every
later writer; `exec_text_release`, called from exec, `exit()` and a
replacing exec, clears the mark once the last `p_tip` referencing the
inode is gone. Clean-text restoration is one format-aware function,
`exec_text_restore`, that `swapin` calls for raw and packed alike: raw
text is read from behind the header, packed text is expanded through a
**swapper-private static decoder**, because the swapper (proc 0) must not
wait on a buffer, while exec decodes into a caller-owned buffer-cache
block so two concurrent execs never share decoder state. Every packed
restoration recomputes and checks the text CRC.

The lookahead is the one parameter that differs from the swap tier.
Sweeping the shipped a.outs, lookahead 3 packs them into 148 fewer root
blocks than the swap tier's 8: swap images are long zero runs where a
long match wins, Thumb text has short matches where a short lookahead
packs tighter. `tools/hsaout` packs and unpacks and fsutil's `pack`
manifest command installs a raw a.out as a packed one; both run the
kernel's own `hsx_stream.c` and `hsx_decoder.c` as a self-check.

Evidence. Host `bmake -C tests/hsaout check`: every header and stream bit
of a small image (16934 rejected, 10 equivalent LZSS encodings, none
accepted with different bytes), every truncation and extension, forged
near-`UINT_MAX` lengths, wrong parameters, a fill failing at each chunk,
and a round-trip of all 244 shipped a.outs. Board
(`tests/rp2040/exec_hsaout`, textcrc raw and packed, bigtest packed):
foreground and background packed exec; text CRC `a1ae2b8e` unchanged
across 21 swap-ins; a write to a running executable refused and allowed
after it exits; appended and truncated containers refused; a 150000-byte
packed image run under LARGE. Packing the 33 root programs frees 148 of
756 root blocks, 20 percent. Kernel text +2626 bytes, plus ~1 KB bss for
the swapper's decoder.

Concern recorded, not a defect: `getnewbuf` in the exec path can wait for
a free buffer; in every run it completed and was never the cause of a
stall. The one observed slowness was a 300000-iteration awk thrashing the
single-process window, unrelated to packing; the swap-owned-text idea
(setting `p_tsize=0`) is rejected because it would enlarge swapped images
and worsen exactly that thrash. Restoration performance belongs to a
separate measurement of swap count, compressed bytes, and elapsed time
under a bounded workload.

### Multicall consolidation: the three-number screen (port PR #46)

PR #46 overlays each box's applet-private BSS into one shared extent (only
one applet runs per process, so their zero-initialized storage has disjoint
lifetimes) and packs all 33 OMAGIC root executables. It also folds sort
into utilbox and rewrites sort to drop four 256-byte classification tables,
bound the merge array at the seven-way fan-in, and size the temp-name
buffer from the chosen directory.

Every consolidation candidate is judged on three numbers: raw root blocks
saved, packed root blocks saved, and resident data+bss added when the tool
runs through the box instead of standalone. The third number is the guard:
a box's overlaid extent is sized to its largest applet, so folding a
large-BSS tool raises the resident cost of every applet in that box, and
an apparent flash win then eats the RAM the exclusive epoch and tail
recovered.

sort into utilbox, the completed candidate: raw -10 blocks, packed -7
blocks, resident +0. The overlay drops utilbox's own mutable image from
9,484 to 5,336 bytes, and sort's standalone mutable falls from 3,432 to
1,080; sort's applet BSS overlays the other applets, so the box gains no
resident cost. A clear pass on all three numbers.

Screening the remaining standalone executables (mutable = a_data+a_bss,
packed blocks from `hsaout -s`):

| Tool | packed blk | mutable B | verdict |
| --- | ---: | ---: | --- |
| smlrc | 42 | 27,700 | reject: mutable dwarfs any box |
| awk | 35 | 13,572 | reject |
| as | 24 | 31,364 | reject |
| ld | 17 | 35,928 | reject |
| fsck | 25 | 28,924 | reject |
| sed | 13 | 32,068 | reject: the respace/genbuf workspace |
| compress | 11 | 30,576 | reject |
| find | 16 | 12,248 | reject: mutable exceeds the box |
| grep | 9 | 2,240 | fold candidate |
| fgrep | 8 | ~1,900 | fold candidate |
| ed | 9 | 3,316 | fold candidate |
| cpio | 8 | 1,072 | fold candidate |
| su, passwd, login | 13/12/19 | 4,800/2,216/6,280 | keep: set-id surface |
| init, update | 14/2 | 3,452/120 | keep: never invoked by name |

The tools with the largest flash footprint all carry 13-36 KB mutable
images, so folding any of them raises a box's resident extent by that much:
they fail the third number and are rejected. The RAM-free candidates are
those whose mutable is at or below utilbox's 5,336 bytes: grep, fgrep, ed,
cpio add no resident cost when folded there. The grep family (grep, egrep,
fgrep) is better consolidated as its own multicall binary because the three
share one regex engine, collapsing about 30 packed blocks toward 13 at a
resident cost of the shared engine alone. Set-id programs stay separate to
keep the privilege surface narrow, and init and update are never run by
name. The screen therefore endorses PR #46, names one further worthwhile
step (a grep-family box), and rejects folding the compilers and filesystem
tools.

Board gate (packed root, kernel and filesystem UF2s, run as root): every
hard-link name across all six boxes dispatched and ran correctly, with
halt, reboot and shutdown-action held out and the same adminbox dispatcher
proven through sysctl and shutdown usage; games dispatched on EOF without
hanging. Sort of 24,000 lines, more than seven times the ~3,225-line
in-core run capacity, produced 24,000 correctly ordered lines through the
multi-pass seven-way merge. Each box was swapped out and back in: five
against a concurrent 24,000-line sort, gamebox held resident on a
sleep-fed pipe while foreground applets displaced it. Host gates:
shellcheck, the sort differential test, the elf2aout layout test including
the BSS overlay, check-swapram, check-divider, check-hsaout, and
swapram-evac all pass.

### The grep-family box, and a corrected screen (port PR #47)

The earlier screen guessed the grep family shared a regex engine and was
RAM-neutral to fold. Measurement corrected both: grep (basic REs), egrep
(a yacc DFA) and fgrep (Aho-Corasick) are three distinct engines, and
egrep and fgrep carry large fixed tables in bss, 43,388 and 64,156 bytes,
against grep's 1,808. Folding them naively would set the overlay extent to
64 KB and make grep, the most-used member, 26 times heavier. The value is
not the box; it is bounding fgrep's table.

fgrep sized its Aho-Corasick trie at a fixed w[MAXSIZ] of 4000 states,
64,064 bytes on every invocation. The trie holds at most one state per
pattern byte plus a terminal per line under -x, so w becomes a pointer
allocated from the pattern source (the -f file size, or the argument
length), guarded by the existing overflo(). fgrep's mutable image falls
from 64,832 to 588 bytes. This also moves fgrep's COMMON tables into .bss
under -fno-common, which the overlay requires: ld -r leaves a COMMON
unallocated, so it escapes the .app_bss rename and sums into the final
.bss rather than overlaying. grep and fgrep were added to
tools/verify_userland_no_common.sh.

grepbox then folds grep and fgrep on the utilbox pattern. The engines
differ, so the code does not deduplicate; the win is one libc copy and,
because fgrep's trie now leaves bss, an overlay extent of grep's 1,796
bytes. egrep stays standalone: it is absent from the root manifest, and
its `gotofn[NSTATES][NCHARS]` is algorithmic, not oversizing, so folding it
would add text and reset the extent.

Three numbers: raw root blocks -8, packed root blocks -6, resident
data+bss +4 bytes for grep and +1,656 for fgrep, both far under the ~64 KB
the bounding returns. Board: grepbox alone lists its commands; grep and
fgrep as links match correctly; fgrep -f with 200 patterns allocates and
matches; a 600-pattern set overruns the window and exits 2 with "wordlist
too large" rather than corrupting memory. Host: the no-common verifier and
the elf2aout layout test pass.

The lesson for the screen: measure a_bss per member before folding, not
just packed blocks. A tool's fixed worst-case table, not its file count,
is what a box would spend, and bounding that table is the larger and often
the only real win.

### Physical-page flash staging and erase-aligned swap images (port PR #63)

The prior flash overlay still held one 4,096-byte erase sector because a
process image could share that sector with another live image. The resource
map now allocates data, stack and u area as one contiguous run, rounds the run
to four 1,024-byte blocks, and starts the free map at block four. Every live
process image therefore owns every sector that its writes erase. SwapRAM
evacuation reserves the same whole-image runs, and `/dev/tempN` allocations
also preserve the map alignment.

Process writes carry `B_SWAPIMAGE`; `flash_swap_append` erases a sector when
the ascending stream reaches its boundary and programs it 256 bytes at a
time. Arbitrary `/dev/tempN` rewrites preserve their block semantics by
copying the destination sector through the first swap sector, which the
resource map already leaves unavailable, and then restoring it through the
same 256-byte SRAM page. The flash copy path also moves each 1,024-byte Dhara
page as four physical program pages. The mapped root device has only a block
entry, whose buffer-cache requests are 1,024-byte blocks; the driver rejects
a partial Dhara page instead of retaining a second staging page.

Against clean port main `028bd2d9d5ae4627ad672acf7441a87e7ff29151`, both
linked kernels reduce the size tool's bss column by 3,840 bytes:

| kernel | bss before | bss after | text cost |
| --- | ---: | ---: | ---: |
| PICO | 58,144 | 54,304 | +732 bytes |
| PICO_UART | 28,712 | 24,872 | +724 bytes |

`flscratch` is exactly 256 bytes in each linked ELF. The host NOR model
enforces 4 KiB erase alignment, 256-byte program alignment, one-to-zero
programming, adjacent-image preservation, run reuse, cross-sector temporary
rewrites, and operation-error propagation. The allocator and evacuation test
checks empty and overflowing sizes, exact addresses, all-or-none shortage,
zero-length segments, two complete round trips, and the append-only write
flag. The linked verifier checks `flscratch`, both flash algorithms, and the
allocator relocations in `flash.o`, `vm_swap.o`, `swap.o`, and `swapram.o`.
The direct PICO and PICO_UART builds and the flash-swap, SwapRAM, divider,
cache-footprint, packed-root, config, and ELF-to-a.out gates pass.

The storage price is explicit: the first 4 KiB swap sector is the rewrite
scratch sector, and each live process or temporary-device allocation holds
zero to three padding blocks. The eight-image evacuation fixture writes 52
blocks and holds 64, so that specimen spends 12 KiB on erase isolation. The
scratch sector adds erase and program cycles to arbitrary block rewrites but
adds no further reserved flash. Hardware flash behavior remains an attended
validation gate; the implementation pass did not flash a device.

## Open

Step 6 needs the pool and window to share one arena with resident
expansion taking precedence: the window is now fixed at 144 KB and the
pool at 16 KB, and an arena would let a process that fits in 160 KB run
while the pool is empty. Step 7 landed as PR #45 above. Step 9's
conversion landed in PR #35; extending the multicall boxes with sed,
sort and find is the remaining part.
