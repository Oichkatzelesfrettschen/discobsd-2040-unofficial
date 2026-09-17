# Storage techniques from other small systems, and the SWAPRAM enable path

Scope: DiscoBSD on RP2040, a.out OMAGIC, one 96 KB process window, no MMU,
static libc per binary, on-device native toolchain. Part A surveys
NetBSD, Minix, xv6, ELKS, Contiki/Contiki-NG, Fuchsia's libc, and
picolibc/newlib-nano for techniques this tree can use, and skips what
`STORAGE.md` already covers except to extend it. Part B verifies the
SWAPRAM enable path against `swapram.c`, `swapram_pool.c`,
`include/swapram.h`, and `doc/research/zswap.md`.

## Part A: what borrows, and what does not

### Ruled out, with the reason

**xv6** targets a paging MMU and teaches VAX/x86 virtual memory; nothing
in it addresses flash or RAM footprint on an unpaged part, so it has no
technique this tree can use. **Minix 3**'s footprint story is a
microkernel's: drivers and the VFS run as separate user processes
exchanging IPC messages, which trades RAM and cycles for isolation this
board cannot afford and does not need with no MMU to enforce it anyway;
Minix's own resource-frugal ancestor (Minix 1/2 on the 8086) matches
2.11BSD's own techniques rather than adding new ones. **Fuchsia**'s libc
targets phones and larger; it carries no small-footprint technique this
tree lacks already. **Plan 9/9front** links single static binaries with
no dynamic loader, which is the same shape DiscoBSD already has, and its
libc is small by having few features rather than by a technique to graft
on. None of these four contributes an action item below.

**Contiki/Contiki-NG**'s two headline techniques do not transfer.
Protothreads replace per-task stacks with a shared stack and a
switch-statement resumption point, which saves RAM only when many tasks
run concurrently without their own stack; DiscoBSD already runs one
process resident at a time in the 96 KB window and serializes the rest
through swap, so there is no per-task stack duplication to remove.
Keeping constant tables resident in flash and read in place (Contiki's
model on the MSP430, and the general AVR `PROGMEM` pattern) is the same
idea `STORAGE.md` already rejects for user text -- OMAGIC binaries need
position-independent code and a different loader to execute from flash --
and it fails a second, independent way here: the root filesystem sits
behind Dhara, which relocates logical blocks for wear leveling, so no
binary's rodata has a fixed physical flash address to read in place
against. Flash-resident rodata is blocked twice over, not once.

**NetBSD's crunchgen** is the multicall generator `STORAGE.md` already
credits box/sysbox/textbox/utilbox/adminbox as an equivalent of: one
`ld -r` combine per box, `main` renamed and every other symbol localized,
one shared libc link. What crunchgen does not do that is worth adding is
below.

### 1. Section-level dead code and identical-code folding on the box link

Each box links roughly twenty tools' objects with `ld -r`, then one pass
through the shared libc. Tools that share a helper shape -- `usage()`
printed to stderr and exit, `getopt`-style option parsing, a small
`err()`/`warnx()` wrapper -- compile to near-identical machine code
across tools but are not byte-identical, so `ld -r` keeps every copy;
nothing in the current build asks for function-level garbage collection,
let alone identical-code folding. Two independent levers apply: compile
with `-ffunction-sections -fdata-sections` so any function or table a box
never calls through any tool's path can be dropped, and link with
`--gc-sections` to drop it. Confirm first whether the host cross
toolchain's `arm-none-eabi-ld` is BFD (no ICF) or a build with gold or
lld (`--icf=all`, function folding for byte-identical bodies); BFD ld
alone already gets `--gc-sections`, ICF is the stretch goal if the
toolchain has it. The tree's own on-device `usr.bin/ld` is a separate
question: it need not implement either, since installed a.outs are
already final-linked on the host and section GC is a link-time-only
concern for the box build.

Estimated saving: dead-code GC on a box of twenty tools recovers unused
static tables and any tool-specific code paths not reached from any
`<tool>_main`, likely low hundreds of bytes per box; ICF on shared
boilerplate (`usage`, `getopt`, `err`) across many tools could recover
low kilobytes on the larger boxes (utilbox at 51 KB, sysbox). Effort is
low for `-ffunction-sections`/`--gc-sections` (build flags plus a
rebuild-and-measure pass); effort is medium for ICF, gated on toolchain
availability and needing the round-trip verification the box build
already does (uuencode/cksum on the board) to rule out a folded function
that is not actually byte-identical in behavior across two call sites
(differing only in, say, an inlined constant the folder missed). Risk is
low to medium: linker section GC has occasionally mis-handled ELF-to-a.out
relocation edge cases in the past, so the existing host round-trip test
plus a board boot is the gate, not a leap of faith.

### 2. Extend the float-printf weak-reference split to printf's width/precision path

`STORAGE.md` already documents the `PRINTF_FLOAT=yes` weak-reference
split that keeps `cvt`/`cvtround` out of every program that never calls a
floating conversion. picolibc formalizes this same idea one level
further: it ships selectable printf/scanf levels (`float`, `integer`,
`minimal`, `nano`), where the minimal levels drop field width, precision,
the `#`/`0`/`-`/`+` flags, and `%n`, on the grounds that most call sites
in a small-utility corpus use only `%s`, `%d`, `%c`, and `%x`. ELKS makes
the same call at the libc-selection level rather than per format
directive. The applicable version here is a second weak-referenced
`_doprnt` variant -- `doprnt_mini.o` -- covering only the directives
without width/precision/flags, linked by default, with the existing
full `_doprnt` staying available (as `PRINTF_FULL=yes` or similar) for
the tools that call `printf("%-10s %5d\n", ...)`-shaped formats. An
audit of format strings across `/bin` and `/usr/bin` (the same kind of
survey that produced the float table) decides which tools qualify;
`printf`(1) itself, `awk`, and anything driving column output almost
certainly need the full path, while `id`, `touch`, `basename`, `env`,
and `tty` plausibly do not.

Estimated saving: `_doprnt` itself is 5.8 KB; the minimal path removes
the width/precision counting and padding logic, not the whole function,
so a per-binary saving in the 200-600 byte range is the reasonable
estimate, pending the same kind of before/after table `STORAGE.md`
built for float. Effort is medium: the mechanism is proven (the float
split already exists as a template), but it needs the format-string
audit done carefully, since a tool that occasionally hits a width
directive on an error path is easy to miss by grep alone. Risk is low,
by the same reasoning as item 1 -- the failure mode is a silently wrong
format, not a crash, so it needs the same output-diffing check the float
split's before/after table implies (run each affected tool's test cases,
compare output byte for byte, not just check that it links).

### 3. What already matches, confirming no more libc-structural fat to cut

DiscoBSD's `struct _iobuf` (`include/stdio.h`) is the 20-byte V7/2.11BSD
FILE struct with no locale, no wide-char state, and a static `_iob[]`
array rather than a heap-allocated stream table -- exactly the shape
picolibc's tinystdio and ELKS's libc converge on independently.
`strerror()` (`lib/libc/string/strerror.c`) does not carry a
`sys_errlist[]` string table at all: it asks the kernel for the message
by `sysctl(CTL_MACHDEP, CPU_ERRMSG, errnum)` into a 64-byte stack buffer,
which is a better answer than either ELKS or picolibc give (they still
duplicate the table per binary or accept a shared libc they do not have
here) -- one string table in the kernel image instead of one copy per
installed a.out. This is worth recording because it closes off a branch
of the survey: there is no ELKS/picolibc/Contiki libc-shrinking idea left
unapplied at the FILE-struct or error-table level; the remaining
headroom is in items 1 and 2, at the link and format-string level, not
the data-structure level.

## Part B: the SWAPRAM enable path

### The exact edit

In `sys/arch/rp2040/compile/PICO/Config`, uncomment the two lines already
sitting under the "Compressed RAM tier" comment:

```
options         SWAPRAM                     # RAM tier ahead of fl1
options         "SWAPRAM_KB=64"             # kbytes of bss for the pool
```

then, from `sys/arch/rp2040/compile/PICO`:

```
../../../../../tools/bin/config Config
bmake clean && bmake all
```

`config` gates `swapram.c`, `swapram_pool.c`, and the two heatshrink
codec files on the valueless `options SWAPRAM` token (`files.rp2040`
marks them `optional swapram`); `mkmakefile.c:240` matches `optional X`
only against an option with no value, which is why `SWAPRAM_KB` is a
second, separate `options "..."` line rather than `SWAPRAM=64` on the
first -- the latter would silently drop all four sources from `OBJS`.

### Documented cost

`zswap.md`'s measured `arm-none-eabi-size` table, `compile/PICO/unix`
after a clean rebuild both ways:

| Build | text | bss |
|---|---|---|
| shipped (option off) | 90,901 | 39,608 |
| option on, `SWAPRAM_KB=64` | 95,049 | 108,008 |

4,148 bytes of kernel text and 68,400 bytes of bss (65,536 for the pool
itself, 1,040 encoder, 590 decoder, 1,234 for the segment table, the
per-process table, and the pool descriptor). With the option off, both
figures are byte-identical to the tree without this change -- the four
sources are entirely absent from `OBJS`, not merely `#ifdef`'d out.

### Root flash cost: zero

The 4,148 bytes of text land in the 128 KB boot2-and-kernel flash region
(`0x10000000`), which the shipped kernel already occupies to 90,901 of
131,072 bytes; at 95,049 bytes the region still has 36,023 bytes free.
This region is disjoint from the root filesystem's 1536 KB Dhara region
at `0x10020000` that `STORAGE.md` tracks as "70 KB free of ~971 KB."
Enabling SWAPRAM changes zero bytes of that root budget: it is kernel
text and kernel bss, not a root filesystem file. The 68,400 bytes of bss
come out of RAM, not flash, at all.

### SWAPRAM_KB: keep the shipped default of 64

`BOOT-MAP.md` lists the layout this task's numbers describe: the 96 KB
user process window at `0x20000000`, and 154 KB of kernel data, bss, and
RAM-resident flash writers at `0x20018000` (matching `zswap.md`'s
157,696-byte figure for that region, 154 * 1024). At `SWAPRAM_KB=64` the
region holds 108,008 of 157,696 bytes, leaving 49,688 bytes -- just under
a third of the 154 KB kernel RAM region -- for everything else the kernel
needs at runtime: the 2,100-byte kernel stack per active context, the
proc table (`struct proc[NPROC]`, NPROC 25), buffer headers, the Dhara
and flash-swap state machines, and growth. That headroom is comparable in
scale to the option's own bss cost, which is the right ratio for a
feature that is explicitly allowed to refuse (falling through to flash)
rather than one that must never fail; a pool sized to consume most of the
remaining RAM would leave too little margin for the rest of the kernel to
run correctly, and the observed 6.4x worst-case compression ratio makes
64 KB already generous against NPROC = 25 -- it holds roughly 33
sh-sized images by final compressed size, more than every process slot
could occupy at once. Do not raise `SWAPRAM_KB` past 64 without first
re-measuring free bss after the change; do not lower it without a
stated reason, since the pool's only failure mode (falling through to
flash on a full pool) is graceful, so oversizing buys nothing a
correctness case requires and undersizing only costs performance, not
correctness.

### Integration with the flash swap tier (fl1)

`kern/vm_swap.c` offers every swapout to SWAPRAM first, in the same call
path that would otherwise go straight to `fl1`. `swapram_out` reserves
the encoder's worst case for the whole image (input plus an eighth plus
four bytes per stream, `SR_WORST()` in `swapram.c`) before any byte is
compressed, so a reservation that succeeds cannot run out partway and
`swapout` never has to unwind a half-written image. When the pool cannot
find a single free run of that worst-case size, `swapram_out` returns 0
and `swapout` falls through to the existing flash path with nothing
about it changed -- the same code that ran with the option off. A
process SWAPRAM takes leaves `p_daddr`, `p_saddr`, and `p_addr` zero and
never calls `malloc3`, so it holds no `swapmap` blocks and `pstat`
reading `swapmap` through sysctl sees no entry for it, which is correct
rather than an omission. `swapin` asks `swapram_present()` first for
every process and only consults the flash path when that returns false.
The two tiers are mutually exclusive per process, checked at both
swapout and swapin, with the flash tier as the unconditional fallback --
this is a strict front-end cache in front of `fl1`, not a replacement
for it, and every board and host test that exercised only the flash path
before this option existed continues to exercise exactly that path
whenever the pool is full or the option is off.

### Board test sequence (from zswap.md)

`swapramdebug` is 1 by default, so every swapout/swapin prints a line
naming the tier. With a kernel built with the option on:

1. `minicom -D /dev/ttyACM0 -C zswap.log`, log in.
2. Force one swap and confirm the pool: `sleep 30 &` then `ls -l
   /usr/bin`. Expect `swapram: pid N ram R -> C bytes, pool F free` on
   the way out and `swapram: pid N in C -> R bytes` on the way back, with
   `R` equal to `p_dsize - p_tsize` plus `p_ssize` plus 3072.
3. Force several swaps with a memory-hungry job:
   `for i in 1 2 3 4; do sleep 60 & done`, then
   `awk 'BEGIN{for(i=0;i<20000;i++)a[i]=i; print "done"}'`, then
   `cc -c /usr/src/bin/echo/echo.c`. Every image should print `ram`
   while the pool has room; `pool F free` should fall as images
   accumulate and rise as they return.
4. Force the flash fallthrough deliberately: rebuild with
   `options "SWAPRAM_KB=8"`, repeat step 3. The first image or two print
   `ram`, the rest print `swapram: pid N flash R bytes, pool F free`, and
   the board keeps working -- this is the proof the tier is optional
   rather than load-bearing, not just a claim.
5. Compare wall time on the step-3 `awk` run with the option on and off;
   the on run should be faster, and the flash activity LED
   (`led_control(LED_SWAP)` in `dev/flash.c`) should stay dark during
   `ram` swaps.

Rebuild with the production `SWAPRAM_KB=64` after step 4's deliberate
undersizing before shipping the board.

### Correctness and stability risks

The design has real invariants backing it, not just aspiration.
`swapram_pool.c` is a first-fit allocator over an address-ordered free
list held outside the pool buffer, so a corrupt image in the pool cannot
corrupt the allocator's own bookkeeping, and every entry point reports an
inconsistency by return value rather than assuming success -- `swapram.c`
turns a -1 into a panic rather than continuing on bad state. Nothing on
the path sleeps or allocates: `swapout` runs from `sched` in process 0 or
from `newproc` in a forking process, and the flash path is the one that
sleeps inside `swap()` on `B_DONE`, so the two tiers cannot interleave
inside SWAPRAM's file-scope encoder/decoder statics -- this is what makes
sharing those statics across calls safe without locking. Deepest
measured stack usage on the path is about 250 bytes against 2,100
available (`swapout` 56, `swapram_put` 64,
`heatshrink_encoder_poll` 88 plus leaves), leaving headroom against
`USIZE`.

Against that design, `zswap.md`'s own "Not verified" section is the
honest risk list, and it is the reason the option ships off: nothing has
run on hardware. Every board figure -- the compression ratios for the
stack and u-area segments, the M0+ throughput estimate (extrapolated at
100-200x from a host benchmark, not measured), and the comparison against
the flash path's 0.5-second figure -- is a host measurement or a reasoned
estimate, not a board measurement. `swapramdebug` has no runtime toggle;
turning the trace off needs a rebuild, which matters for a first bring-up
where the trace is exactly what step 2 through step 5 depend on. The
panic list in `zswap.md` (`short expand`, `reservation overflow`, `image
already resident`, free-list corruption) names failure modes the host
test's misuse cases already cover for the allocator and codec in
isolation, but board-only conditions -- an ISR touching the file-scope
statics unexpectedly, a HardFault mid-encode from a kernel stack
overflow the host cannot reproduce -- are exactly what the board test
sequence exists to rule out before this ships on by default. "Merged,
off" is the correct state until that sequence runs once on real
hardware; nothing in the code argues for skipping it.
