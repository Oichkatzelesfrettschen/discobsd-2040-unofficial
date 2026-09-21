# The patch-499 stdio core on this target: measured and declined

## Verdict

The Torek stream core as 2.11BSD patch 499 shrinks it is not adopted. On a
full cross build it adds a median 534 bytes to every program's loaded image,
256 bytes of initialized data to every program without exception, and pushes
`adminbox` from 21 to 23 packed root blocks against a budget of 22. What it
buys -- unlimited `ungetc` pushback, function-backed streams, correct
read/write mode switching on `r+` files -- is used by no shipped program.

Two things it contains are worth having and cost almost nothing, and they
are queued as their own units at the end: the one-byte in-`FILE` pushback
slot that removes `ungetc`'s residual write into a caller's string, and the
read/write mode discipline for `r+` streams.

## Identity

| Surface | Value |
| --- | --- |
| Baseline | `39b1bf77`, main after #145 and #146 |
| Prototype | branch `eval/stdio-torek`, one commit on that baseline, kept as evidence and not proposed for merge |
| Cross compiler | `arm-none-eabi-gcc` 16.2.0, `-Os`, the tree's exact flags |
| Host compiler | `gcc` 16.2.1 |
| Build | `bmake MACHINE=rp2040 clean` then `distribution`; exit 0, zero warnings, with `MAX_PACKED_ROOT_BLOCKS=24` passed on the command line so the whole world could be measured past the `adminbox` gate. Nothing in the tree changed that budget. |
| Board | not run; every number here is a build or host-gate result |

## What was built

The prototype takes patch 499's stream core -- `findfp.c`, `refill.c`,
`wbuf.c`, `wsetup.c`, `makebuf.c`, `flags.c`, `fvwrite.c`, `stdio.c`,
`rget.c`, `fclose.c`, `fflush.c`, `fopen.c`, `fdopen.c`, `freopen.c`,
`fseek.c`, `ftell.c`, `rewind.c`, `fpurge.c`, `setvbuf.c`, `ungetc.c`,
`getc.c`, `putc.c` and the small get/put members -- and its `stdio.h` with
`struct __sFILE` and the on-demand `struct __sfops`. It keeps this tree's own
formatter (`doprnt.c`) and scanner (`doscan.c`) on the new `getc`/`putc`
macros, keeps `exit.c`, which owns `atexit` and `errno` for all of libc and
which 499's copy would have replaced, keeps `compat/tmpnam.c`, which
implements `tmpfile`, `tmpnam` and `tempnam` together with a gate, and keeps
this tree's `gets.c` over 499's, which writes a 54-byte warning to stderr on
first call.

Adaptation to C17 under the tree's `-Wall -Wextra -Werror`: prototypes for the
K&R definitions (`stdio.c`, `fclose.c`, `fgets.c`, `putw.c`), designated
initializers for `__sF` and `__sdefops`, parenthesized assignments in
`tempnam.c`, a `size_t`/`int` narrowing in `fread.c` and `fvwrite.c`, and the
`(__SLBF|__SWR)` parenthesization in `refill.c`, which patch 499 carries
verbatim from 4.4BSD-Lite2 and NetBSD corrected long ago.

Two mechanisms were added for this tree's formatter, which writes through
`putc` rather than 4.4BSD's `__sfvwrite`:

- `__swbuf` drops a character and returns it when the stream is `__SSTR`, so
  `sprintf` and its bounded relatives point `_bf` at the caller's array with
  `_fops` NULL and never reach a write function. The count still advances,
  which is the C99 return contract.
- `vfprintf` on an unbuffered stream runs the conversion into a 1 KB stack
  copy of the stream and writes it once, the `__sbprintf` arrangement, so
  `fprintf(stderr, ...)` is one `write(2)` and not one per character. The old
  code did the same with `alloca(BUFSIZ)`; the frame is 1088 bytes either
  way.

`sscanf` builds its string stream over a static `__sfops` whose read function
returns end of input. 499's `ungetc` satisfies a pushback of the byte just
read by stepping `_p` back, and a single differing byte through the stream's
own `_ubuf`, so that shared block is never written.

Nine consumers touched `FILE` fields directly and moved to the public API:
`lib/libcurses/printw.c` and `scanw.c`, `games/cribbage/io.c` and
`games/battlestar/curses.c` to `vsnprintf` and `vsscanf`; `usr.bin/more`,
`games/rogue` and `games/hunt` to the renamed fields; `lib/libc/gen/syslog.c`
carries its `%m` text in `_up` under a private `__SSYSLOG` flag. The board
libc member list, the a.out libc list and the three contract gates moved to
the new object names. Everything else compiled untouched.

## Measurements

### Library

`arm-none-eabi-size` over `lib/libc/stdio/*.o`, stale objects removed:

| | text | data | bss |
| --- | ---: | ---: | ---: |
| baseline | 11811 | 168 | 32 |
| prototype | 12143 | 510 | 32 |
| delta | +332 | +342 | 0 |

The data growth is the static stream array: eight `struct __sFILE` at 48
bytes against eight `struct _iobuf` at 20, plus the 36-byte shared
`__sdefops`, plus the 36-byte read-only-in-spirit `eofops` that `sscanf`
needs. `sizeof` was checked with `_Static_assert` at ARM32, not estimated.

Members that enter a typical link, prototype against the members they
replace:

| | text | data |
| --- | ---: | ---: |
| baseline `filbuf flsbuf findiop rew strout` | 1432 | 164 |
| prototype `findfp stdio wbuf wsetup makebuf refill rget fflush fclose fvwrite flags vfprintf ungetc getc putc rewind fpurge` | 2500 | 420 |

`fvwrite.c` alone is 540 bytes of text and is reached by `fputs`, `fwrite`
and `puts`; `ungetc.c` is 300 against the old 66.

### Programs

The tree's own `tools/bin/size` over the 244 programs built both ways:

| | count |
| ---: | --- |
| changed | 234 of 244 |
| larger | 226 |
| smaller | 8 |
| median image delta | +534 bytes |
| range | -184 to +1256 |
| sum of image deltas | +126800 (text +67520, data +61090, bss -1810) |

Every changed program gains 250 to 292 bytes of data. The text delta depends
on which core members the program's stdio use drags in: a program that only
prints gains about 260, one that reads and writes and seeks gains 700 to 970.
The eight that shrink lost more from the old `_doprnt` string path than the
new core added, and still gained the 256 bytes of data.

The 28 programs the rp2040 manifest ships:

| Delta | text | data | Program |
| ---: | ---: | ---: | --- |
| +1072 | +788 | +292 | `bin/tar` |
| +1020 | +736 | +292 | `sbin/adminbox` |
| +960 | +712 | +252 | `usr.bin/passwd` |
| +860 | +612 | +256 | `usr.bin/su` |
| +852 | +608 | +250 | `usr.bin/login` |
| +788 | +540 | +256 | `bin/ps` |
| +752 | +504 | +256 | `usr.bin/find` |
| +716 | +432 | +288 | `usr.bin/awk` |
| +716 | +468 | +256 | `sbin/fsck` |
| +692 | +444 | +256 | `sbin/grepbox` |
| +576 | +328 | +256 | `usr.bin/as` |
| +564 | +316 | +256 | `sbin/textbox` |
| +560 | +312 | +256 | `usr.bin/du`, `usr.bin/menu` |
| +556 | +308 | +256 | `usr.bin/stevie` |
| +552 | +304 | +256 | `sbin/box` |
| +540 | +292 | +256 | `usr.bin/compress` |
| +536 | +288 | +256 | `sbin/sysbox` |
| +520 | +236 | +292 | `sbin/utilbox` |
| +472 | +224 | +256 | `usr.bin/smlrc` |
| +460 | +212 | +256 | `sbin/init` |
| +352 | +104 | +256 | `usr.bin/sed` |
| +336 | +88 | +256 | `usr.bin/cpio` |
| +316 | +68 | +256 | `bin/sh` |
| +132 | -116 | +256 | `games/gamebox` |
| +68 | -180 | +256 | `usr.bin/ld` |
| +36 | -212 | +256 | `libexec/getty` |
| +32 | -216 | +256 | `legacy/pdp11-v6/usr.bin/pdp11` |

Sum over the shipped set: **+15596 bytes** of loaded image, text +8512, data
+7298. `distrib/rp2040/sdcard.img` is the same 1012736 bytes, which is the
fixed image geometry and not a measurement of slack.

### The budget gate

`sbin/adminbox/Makefile` holds the packed root image to
`MAX_PACKED_ROOT_BLOCKS`, 22, and records 21 as the value the current
compiler produces. The prototype's `adminbox` packs to 23:

```
file                          raw   packed   rblk   pblk
sbin/adminbox/adminbox      26712    21764     28     23
```

so the unmodified build stops there with `adminbox: packed root image exceeds
22 blocks`. This is a budget gate in the sense the style guide's V18 draws:
it measured the headroom and found it gone. The world was measured past it by
overriding the value on the command line; the tree's budget is untouched.

### Stack

`-fstack-usage` at `-Os`: `vfprintf` 1088 bytes on the unbuffered path only
(the stack copy of the stream plus its 1 KB buffer; the old `alloca` path
was the same size), `__sfvwrite` 48, `fseek` 32, `ungetc` 24, `__sflush` 24,
`__swsetup` 16, `__swbuf` 16. The core adds no frame worth naming; the cost
is text and data, not stack.

### Behavior

The three stdio contract gates run against the prototype's objects:
`check-libc-printf` at host width and ILP32, `check-libc-syslog`, and
`check-libc-scanf` at host width, ILP32 and under the address sanitizer,
including the 512-digit overflow cases. All pass. The full `bmake check` run
is recorded in the prototype branch's commit message.

## Why the numbers come out this way

The 20-byte `_iobuf` carries a count, two pointers, a size and two shorts.
The 48-byte `__sFILE` carries a position, separate read and write counts, the
buffer as a base-and-size pair, the line-buffer trick value, the fileops
pointer, the two saved ungetc fields, two one-byte guarantee buffers, and a
seek offset. Each field pays for a capability: `_r`/`_w` for the mode
switch, `_fops` for function-backed streams, `_up`/`_ur`/`_ubuf` for
pushback beyond one byte, `_offset` for the seek optimization 499 then
removes but whose field stays. Ragge's `__sfops` split keeps the four
function pointers and the two auxiliary buffers out of the stream, which is
what holds `__sFILE` at 48 rather than 4.4BSD's 88; it does not touch the
fields above.

The text growth is the flush and refill discipline. The old core's
`_filbuf`/`_flsbuf` pair is 712 bytes and does one thing each. The new core
separates flush from write-setup from buffer allocation from refill, each
guarding the mode invariants, and adds `__sfvwrite` so that `fputs` and
`fwrite` copy through the buffer in one pass. Those are good engineering
decisions for a system with many kinds of stream. This system has files and
strings.

## What is worth keeping

Two ideas cost bytes rather than hundreds of bytes and each removes a
defect this tree had. Both have since landed, and a third unit took the
parts of 499 that are C17 interface rather than stream core.

| Unit | Where it landed | Cost measured |
| --- | --- | --- |
| One-byte pushback slot | `stdio-core-c17-unit.md`, `check-libc-scanf` gained the differing-byte case | `FILE` 20 to 24 bytes, +32 data and +20 to +164 text per program |
| `r+` mode discipline | `lib/libc/stdio/filbuf.c`, `flsbuf.c`, `fseek.c` and `ungetc.c`, gated by `check-libc-rwmode` | folded into the same measurement |
| The C17 interface surface | `stdio-ansi-surface-unit.md`, gated by `check-libc-ansi` | +20 per program, +100 where the program opens files, 3 root blocks |

The estimates the queue carried, for comparison with what the two units
then cost:

| Unit | Mechanism | Cost estimate | Gate |
| --- | --- | --- | --- |
| One-byte pushback slot | Add a `_ubuf[1]` byte and a `__SUNC`-style flag to `struct _iobuf`, so `ungetc` of a byte differing from the one read stores into the stream rather than the caller's array. This removes the residual case `lib/libc/stdio/ungetc.c` documents. | `FILE` 20 to 24 bytes, +32 data over eight slots, tens of bytes of text | `check-libc-scanf` gains a differing-byte pushback case over a `const` string that the sanitizer would catch |
| `r+` mode discipline | On a read-and-write stream, flush before switching to read and discard the read buffer before switching to write, as `__srefill` and `__swsetup` do. The old core relies on the caller seeking between directions. | tens of bytes of text in `_filbuf`/`_flsbuf` | a host contract that writes, reads back and writes again on one `r+` stream |

## What the prototype branch is for

`eval/stdio-torek` holds the complete adapted core, compiling warning-free
under the tree's full profile and passing the stdio gates, as the artifact
these numbers were measured on. Anyone reopening the question can rebuild it
and remeasure rather than trusting this table. It is not proposed for merge
and the campaign row in `211bsd-patch-scope.md` records the outcome.

## Not run

No board execution. The `r+` mode behavior of the old core was not
characterized here beyond reading; the queued unit owes that. Root
filesystem free-space before and after was not captured, because the image
is regenerated in place; the packed-block table above is the per-program
proxy.
