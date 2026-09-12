# Storage on the Pico: what costs what, and the layout chosen

Measured on the built DiscoBSD tree for the RP2040 on 2026-09-11. Sizes
are bytes of the a.out as installed unless marked.

## The three costs, in order

**Static libc, one copy per program.** No MMU means no shared library,
so every a.out carries what it uses of libc. `cat` has 1.4 KB of its own
code in a 17 KB file; `id` has 1.5 KB in 20 KB. Across the 29 programs
in `/bin` the tree's own code is a fraction of the 528 KB installed.

**Floating point in printf.** `_doprnt` is 5.8 KB and its `cvt` path
pulls `__aeabi_dadd`, `dmul`, `ddiv`, `fsub` and the comparisons, about
10 KB in every program whether it prints a float or not. The fix is a
weak reference: `cvt` and `cvtround` now live in `doprnt_float.o`, and
on rp2040 only a Makefile that says `PRINTF_FLOAT=yes` links it. Twenty
one programs do (awk, bc, dc, basic, forth, picoc, scm, tclsh, vmstat,
iostat and friends). Every other machine links it unconditionally, as
before.

| Program | Before | After |
|---|---|---|
| cat | 16,984 | 10,220 |
| ls | 26,455 | 19,839 |
| grep | 17,976 | 11,212 |
| sed | 23,400 | 16,636 |
| ps | 22,290 | 15,694 |
| init | 23,968 | 17,372 |

**The kernel region.** 512 KB was the STM32F407XE figure; the kernel is
90 KB. The region is 128 KB, leaving 38 KB for growth, and the Dhara
region is 1536 KB, 989 KB of logical blocks; the 64 KB moved from the
kernel region bought 39 KB of root because Dhara reserves its share of
every erase block.

## The multicall binary

`bin/box` and `bin/sysbox` link the small utilities into two a.outs of
36 KB each, dispatched on the name invoked by; `[` is an alias of `test`.
One box of 58 KB linked and ran, and then could not be swapped: the swap
map hands out contiguous runs, and after a few forks 256 KB of swap held
115 KB free in three pieces, none of 70 KB. Two images of 36 KB fit
where one of 70 did not, and swap is 384 KB. Each tool's own objects are combined with `ld -r`, `main` is
renamed to `<tool>_main`, and `objcopy --keep-global-symbol` makes every
other global local, so the sources are untouched and the tools still
build on their own. The manifest hard-links the twenty-six names to it.
Separately those programs cost about 390 KB.

The limit on a box is the swap map before the process window: the
largest image must find a contiguous run beside the shell and init after
fragmentation, so images stay near 40 KB. A third box for the text tools
(sed, grep, sort, uniq, head, tail, tr, wc, cmp) is the next step; awk at
62 KB and the editors stay separate.

## What compression would and would not do

gzip -9 takes 30 to 35 percent off an a.out and xz 36 to 44 percent.
A decompressor at exec time (heatshrink or LZ4, 1 to 2 KB of kernel)
would recover that across the whole root, at a cost of tens of
milliseconds per exec on the M0+. It is a smaller win than the two above
and it comes after them; it is not done.

Executing userland in place from flash would free RAM rather than
flash, and is not possible with a.out linked at 0x20000000; it would need
position-independent binaries and a different loader.

The kernel itself compresses 35 percent, but it executes in place from
flash and a compressed kernel would have to be copied to RAM, which
costs 90 KB of the 264.

## The layout

| Region | Address | Size | Content |
|---|---|---|---|
| boot2 and kernel | 0x10000000 | 128 KB | 90 KB used |
| root, Dhara journal | 0x10020000 | 1536 KB | 989 KB of logical blocks |
| swap, raw | 0x101a0000 | 384 KB | erase and program in place |

Dhara's overhead on the root is fixed by its metadata: one checkpoint
page per eight, a reserve of one fifth for garbage collection, and a
64 KB safety margin; 1536 KB of flash yields 989 KB of blocks. The
`flashimg -c` tool prints the figure for any geometry.

## What ships

51 files and 66 links, 175 KB free (df: 796 of 971 KB used), verified
booting to a root shell on the board: box, sysbox, textbox, sh, ed, ps,
md5, expr, init, getty, login, passwd, reboot, shutdown, fsck, sysctl,
update, and from usr/bin awk, sed, grep, fgrep, find, sort, uniq, head,
tail, tr, wc, cmp, more, basename, env, id, printf, tee, touch, xargs,
du, nohup, tty, uname, true, false, the `re` screen editor, and the
native build chain: /usr/bin/cc driving /usr/libexec/smlrc (53 KB),
/usr/bin/as (32 KB), /usr/bin/ld (23 KB) against /usr/lib/libc.a (36 KB,
89 members) and crt0.o. textbox (32 KB) carries cut, paste, seq,
dirname, nl, cksum, expand, unexpand, uuencode, uudecode, fold, rev and
comm; utilbox also carries md, a Markdown-to-ANSI viewer. On the board: `cc -o h h.c` compiles, assembles and links a
program with integer division and printf, and `./h` runs; a
uuencode/uudecode round trip of /bin/box matches by cksum. picoc is no
longer on the root; the native chain replaces it.

Left out and available in the tree: make (25 KB), diff (24 KB), tar
(24 KB), emg (MicroEMACS, public domain, 24 KB), med (curses editor,
23 KB), virus (busybox-derived tiny vi, GPL, 25 KB), scm (Scheme, 34 KB),
tclsh (72 KB), basic (43 KB), forth (43 KB), bc and dc (35 and 22 KB),
m4, lex, yacc, ar, and the games. A core dump costs a process image of
flash in /tmp; the first one on the board took 64 KB.

## Not shipped: the MIPS toolchain

`cc`, `ccom`, `cpp`, `as`, `ld`, `smallc`, `lcc`, `adb`: the tree's
native toolchain emits MIPS code and assembles MIPS, inherited from
RetroBSD, and is left out of the rp2040 build. Smaller C gained a Thumb-1
back end (usr.bin/smlrc/cgthumb.c, 16 host tests plus 200 generated
programs matching a host oracle under qemu-arm) and ships; a Thumb-1
assembler and the a.out linker extensions for ARM relocations are the
remaining pieces of a native build chain.
