# The V7 stdio core as a C17 unit, with a one-byte pushback slot

## What the unit is

One migration unit in the sense of `STYLE-GUIDE.md` section 4: the five
files that make up the stream core this tree ships -- `lib/libc/stdio/filbuf.c`,
`flsbuf.c`, `findiop.c`, `ungetc.c` and `exit.c` -- converted to C17 under
the tree's full warning profile, with the public entry signatures unchanged
and one deliberate layout change: `struct _iobuf` gains a one-byte pushback
slot and grows from 20 to 24 bytes.

It follows the evaluation in `stdio-torek-evaluation.md`, which built the
patch-499 stream core and declined it on cost, and takes from that core the
smallest of its ideas that removes a defect this tree still had.

## The conversion

| File | Before | After |
| --- | --- | --- |
| `filbuf.c` | K&R definition, `goto tryagain`, `st_blksize <= NULL`, `char c` scratch for the unbuffered case | prototype, a `for (;;)` with explicit `break`s, `<= 0`, the same scratch byte with its lifetime stated in the file comment |
| `flsbuf.c` | K&R, `register`, `char c1` copy of the character | prototype, the character written from its own parameter, `write` lengths as `size_t` |
| `findiop.c` | K&R, `extern int errno`, `_fwalk(function) int (*function)();`, positional `_iob` initializers, unchecked `calloc` for `_smallbuf` | `<errno.h>`, `_fwalk(int (*)(FILE *))`, `_cleanup(void)`, `f_prealloc(void)`, designated initializers, `_Static_assert(sizeof(FILE) == 24)` beside `_iob` |
| `ungetc.c` | conditional store from #145 | three ordered cases, below |
| `exit.c` | `exit(code) int code;`, `atexit(fn) void (*fn)();`, `extern void _cleanup();` | `exit(int)`, `atexit(void (*)(void))`, `extern void _cleanup(void)` |

`stdio.h` declares `_fwalk` and `_cleanup` so no caller declares them K&R.
The `_Static_assert` is the point where the layout is pinned: every
program's `getc` and `putc` macros are compiled against `struct _iobuf`, so
a change to it is a change to every binary, and the assertion fails the
build where the change lands rather than in a program that reads a stale
header.

## The pushback slot

`ungetc` now tries three cases in order:

1. The byte equals the one just read and the cursor is past the buffer
   start: step `_ptr` back and `_cnt` forward. Nothing is stored. This is
   every pushback this tree's scanner performs.
2. The stream is `_IOSTRG` and the byte differs: store it in `_ub[0]`,
   park `_cnt` in `_bufsiz`, set `_cnt` to zero, set `_IOUNGET`. The next
   `getc` underflows into `_filbuf`, which returns the slot byte and
   restores `_cnt` before it reaches its `_IOSTRG` refusal. `_bufsiz` is
   free to carry the count because a string stream never refills from it.
3. Otherwise the buffer is the stream's own or one the caller gave
   `setbuf`, both writable, and the byte is stored in place as before.

The slot holds one byte; a second differing pushback before a read returns
`EOF`, which is the one-character guarantee C17 7.21.7.10 makes and no
more. No saved-cursor fields were needed, which is why the layout grows by
one padded word rather than the two words 499's `_up`/`_ur` cost.

This closes the case #145 left documented: a differing-byte pushback on the
string stream `sscanf` builds stored into the caller's string, a literal in
the common case. On this target that store lands silently in RAM; on a host
that maps literals read-only it faults, which is how the gate catches it.

## A latent bug fixed on the way

`_f_morefiles` allocated the glue table and then the per-descriptor
`_smallbuf` without checking the second `calloc`. On failure `_smallbuf`
kept pointing at the eight-byte static `sbuf` while `_filbuf` indexes it by
descriptor, so an unbuffered read on descriptor 8 or above wrote past it.
It now fails whole, freeing the glue table, and `_findiop` reports
`ENOMEM`. `docs/research/audit-handbacks/step5-nstatic-handback.md` had
named the unchecked allocation.

## Gate and calibration

`check-libc-scanf` gains one case: a string stream over a `static const`
literal, `getc` once, `ungetc` a differing byte, `ungetc` a second
differing byte, then read to end and check the literal is unchanged. It
runs at host width, ILP32 and under the address sanitizer. Built against
the pre-change `ungetc.c` and `filbuf.c`, the case faults at the store in
`ungetc` (`SEGV` under the sanitizer); against the new sources every tier
passes. The `filbuf` recipes drop their `-Wno-deprecated-non-prototype`
exemption because the file no longer needs it.

## Cost

Measured against a clean build of `0b35cbcc` in a separate worktree,
`arm-none-eabi-gcc` 16.2.0 at `-Os`, zero warnings both ways.

| Object | Before | After |
| --- | --- | --- |
| `filbuf.o` | text 264 | text 288 |
| `flsbuf.o` | text 448 | text 452 |
| `findiop.o` | text 332, data 164, bss 16 | text 348, data 196, bss 16 |
| `ungetc.o` | text 66 | text 110 |
| stdio members total | text 11811, data 168, bss 32 | text 11895, data 200, bss 32 |

Over the 244 programs built both ways, 234 change:

| Account | Delta |
| --- | --- |
| data | exactly +32 in every changed program (eight slots, four bytes each) |
| text | +20 in 66 programs, +44 in 117, +88 in 37, +164 in `as`; others between |
| bss | 0 in 230, +4 in 3, -4 in 1 |
| sum | +18120 bytes (text +10624, data +7488, bss +8) |

`adminbox` packs to 22 blocks before and after, which is its budget
exactly; `main` already sat there after #147, so the next growth in
`adminbox` trips the gate whatever causes it.

The text number is larger than the "tens of bytes" the campaign row
estimated. The +44 that most programs carry is `ungetc` itself, and nearly
every program links `ungetc` through `scanf` or its own use, so the slot
logic is paid for world-wide while it guards a path no shipped program
exercises today: this tree's scanner pushes back only the byte it read.
The change was made knowing the data cost; the text cost is what the
measurement added. A split that keeps `FILE` at 20 bytes and takes only
the C17 conversion and the `_f_morefiles` fix is one commit if the
trade is judged the other way.

## Not done

The `r+` read/write mode discipline from the evaluation stays queued. No
board run; every number here is a build or host-gate result.
