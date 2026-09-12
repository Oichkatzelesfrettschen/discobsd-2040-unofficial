# A third multicall box: the sbase text tools

`sbin/textbox` links cut, paste, seq, dirname, nl, cksum, expand,
unexpand, uuencode and uudecode into one a.out of 30,124 bytes
(text 29,364 + data 728 + a.out header), built the same way as
`sbin/box` and `sbin/sysbox`: each tool builds in its own directory
under `usr.bin`, `ld -r` combines its objects, `objcopy
--redefine-sym main=<tool>_main --keep-global-symbol=<tool>_main`
localizes everything else, and `sbin/textbox/textbox.c.in` generates
the dispatch table. mkfifo, comm, rev and fold compile clean for
rp2040 and are not in the box; see "What did not fit" below.

Nine of the eleven tools have no upstream 2.11BSD or tree source
under `bin`/`usr.bin`; they come from sbase
(https://git.suckless.org/sbase, commit `c546c3a5724c81cee9a11d816a38ccdf17472129`,
MIT/X license). comm, rev and fold already exist in the tree
(`usr.bin/comm`, `usr.bin/rev`, `usr.bin/fold`) and are reused
unmodified. seq is a fresh integer-only implementation, not an
sbase import (see "seq" below).

## Tool list, source, and size

Sizes are `text+data` of each tool's own object plus the compat
helpers it pulls in, as combined into the box (`sbin/textbox/*.tool.o`
after `ld -r`, before the box's one shared libc is linked in). The
standalone column is the tool's own a.out, built and installed the
same way every other `usr.bin` program is, for comparison and for
`usr.bin/Makefile`'s `SUBDIR` build.

| Tool | Source | In box (text+data) | Standalone a.out |
|---|---|---:|---:|
| cut | sbase cut.c | 2,809 | 12,224 |
| paste | sbase paste.c | 1,929 | 10,804 |
| seq | new (integer-only) | 1,130 | 10,121 |
| dirname | sbase dirname.c | 374 | 8,780 |
| nl | sbase nl.c, regex option dropped | 2,944 | 12,704 |
| cksum | sbase cksum.c | 1,840 | 10,208 |
| expand | sbase expand.c | 2,035 | 11,297 |
| unexpand | sbase unexpand.c | 2,127 | 11,389 |
| uuencode | sbase uuencode.c | 1,598 | 10,896 |
| uudecode | sbase uudecode.c | 3,396 | 12,985 |
| **box total** | | **29,364 + 728 data ≈ 30,124 on disk** | |

Not in the box (compiled, zero warnings, not shipped):

| Tool | Source | Standalone a.out | Why left out |
|---|---|---:|---|
| mkfifo | sbase mkfifo.c, mknod(2)-based | 9,477 | lowest priority tool that would not fit after uudecode/uuencode through unexpand were kept |
| comm | tree's usr.bin/comm (2.11BSD-derived) | 9,108 | already ships standalone in the current manifest; adding it to the box saved nothing worth taking mkfifo's slot |
| rev | tree's usr.bin/rev | 8,128 | lowest priority in the tool list |
| fold | tree's usr.bin/fold | 9,676 | lowest priority in the tool list |

The budget in `~/Github/rpi/STORAGE.md` is 30 KB on disk for the
whole box, out of 91 KB free on the root with the Thumb-1 assembler
and a.out linker (roughly 40 KB) still to land. Dropping tools in
priority order from the tail (fold, then rev, then comm, then
mkfifo) brought the box from 33,252 bytes (all fourteen candidates)
to 30,124 -- 596 bytes under budget. Each remaining tool's own code
is 300 bytes (dirname) to 3.4 KB (uudecode, which carries the base64
and traditional decoders plus `parsemode()`); the box's fixed cost
(the dispatcher plus one shared copy of the stdio/printf libc every
a.out on this port pays, per STORAGE.md's "Static libc" section) is
about 9.6 KB, the same order as `box` and `sysbox`'s baseline.

## What sbase needed that this libc does not have, and what filled the gap

`usr.bin/textbox/` holds the shared pieces sbase's `util.h` and
`utf.h` normally supply from `libutil`/`libutf`, trimmed to what
these eleven tools call, plus `arg.h` and `LICENSE` copied verbatim
from sbase. Each is a small file, most compiled into only the tool
directories that need it (not linked into every tool), so the size
above already reflects real, not worst-case, sharing:

- **eprintf.c** -- `argv0`, `eprintf`, `weprintf`, `xvprintf`, taken
  from sbase's `libutil/eprintf.c` with `enprintf` dropped (unused
  once `ealloc.c` collapses the status-indirection layer, see
  below). `eprintf` is declared `__attribute__((noreturn))` in
  `compat.h` so a variable set only on sbase's `default: eprintf(...)`
  arm of a `switch` (`mode.c`'s `op`) does not trip GCC's "may be
  used uninitialized."
- **fshut.c** -- `fshut` alone, from `libutil/fshut.c` (the
  `enfshut`/`efshut` wrappers are unused here).
- **ealloc.c** -- `emalloc`, `estrdup`, from `libutil/ealloc.c` with
  the `enmalloc`/`enstrdup`-by-status indirection collapsed, since
  every caller here exits 1 on failure.
- **reallocarray.c** -- OpenBSD's `reallocarray.c` (as sbase carries
  it) verbatim, same collapse for `ereallocarray`.
- **strtonum.c** -- OpenBSD's `strtonum.c` (as sbase carries it),
  adapted from `long long`/`strtoll` to `long`/`strtol`: this libc
  has neither `long long` support in `strtoll(3)` nor the type in
  practice, and every caller's range fits a 32-bit `long` (byte
  offsets, line numbers, tab stops).
- **unescape.c**, **memmem.c** -- sbase's `libutil/unescape.c` and
  OpenBSD's `memmem.c` (as sbase carries it), verbatim but for the
  `#include`.
- **mode.c** -- sbase's `libutil/mode.c`'s `parsemode()` alone
  (`getumask()` is unused; both callers, mkfifo and uudecode, pass
  their own mask).
- **rune.c**, **compat.h**'s `Rune` -- not from sbase. sbase's
  `utf.h`/`libutf` decode real UTF-8 through a 317-line `libutf` plus
  a runetype table; this console is ASCII, so `compat.h` types
  `Rune` as `unsigned char`, and `rune.c` gives `fullrune()` (always
  true), `utflen()` (`strlen()`), and `fgetrune`/`fputrune`/
  `efgetrune`/`efputrune` as one-byte reads and writes. `paste.c`'s
  two calls to sbase's `utfmemlen()`/`utfntorunestr()` (absent from
  this shim) are replaced with a one-line byte-copy loop, commented
  in place. This is the deliberate multibyte-UTF-8 scope cut: on a
  single-byte console it changes nothing observable, and it drops
  libutf's tables entirely.
- **getline.c** -- not from sbase; this libc has no POSIX.1-2008
  `getline(3)` at all (`grep getline lib/libc` finds nothing), so
  cut, nl and uudecode needed one. A plain doubling-buffer
  `fgetc()`-based implementation.
- `strlcpy`/`strlcat` are declared in this libc's `string.h` and
  implemented in `lib/libc/string/`; `compat.h` `#define`s sbase's
  `estrlcpy`/`estrlcat` straight onto them (dropping sbase's
  fatal-on-truncate wrapper -- every call site here uses a
  fixed-size local buffer sized for its own content).
- `strsep`, `strdup`, `dirname(3)`, `chmod(2)`, `mknod(2)`,
  `fstat(2)`, `lstat(2)`, `umask(2)` are already in this libc and
  used directly.

## Source changes beyond a straight import

- **nl**: sbase's `-f`/`-b`/`-h p<BRE>` line-type option calls
  `regcomp`/`regexec`; this libc has no POSIX regex (`grep -r
  regcomp lib/libc include` finds nothing, and the tree's own
  `regexp.h` in `libtcl` is Tcl's non-POSIX API, not a drop-in). The
  `p<BRE>` form is rejected by `getlinetype()` like any other bad
  type letter, with the reason commented at the call site; `-f`/`-b`/
  `-h` still take `a`, `t` and `n`, nl's overwhelmingly common cases.
- **mkfifo**: this libc has no `mkfifo(3)`, only the `mknod(2)`
  syscall it is built on (see `chmod(1)`'s own file-creation code and
  `sys/kern/ufs_syscalls.c`'s `mknod()`). The tool calls
  `mknod(path, mode | S_IFIFO, 0)` directly; `sys/sys/stat.h`'s
  `S_IFIFO` comment ("Not used by 2.11BSD") is about the vnode layer
  honoring FIFO semantics on open/read/write, not about `mknod`
  storing the type bit, so this stays a userland-only fix and the
  kernel is untouched. Host-tested: `mkfifo(1)` creates a
  `S_ISFIFO`-typed node (`test -p`); whether opening it on the board
  blocks like a real pipe is a kernel question out of this task's
  scope.
- **seq**: sbase's `seq.c` parses `start`/`step`/`end` with
  `strtod()` and prints every term through a `printf("%f"...)`-family
  format string it builds at runtime. STORAGE.md's PRINTF_FLOAT
  section prices a float in `printf` at about 10 KB of `_doprnt`'s
  `cvt` path on every program that links it, float user or not, and
  the task rules a float out entirely. `usr.bin/seq/seq.c` is a
  fresh implementation instead of an import: `long` arithmetic, an
  integer increment loop, and `-w` zero-padding computed from decimal
  digit counts rather than sbase's `-f`/`-w` `printf`-format
  machinery. It does not support GNU seq's fractional `step` (a
  string parsed as a float); everything else in the man page's
  common usage (`seq last`, `seq first last`, `seq first step last`,
  `-s sep`, `-w`) matches.
- **uudecode**: this libc's `limits.h` has no `PATH_MAX` (checked
  across `include/limits.h` and `include/rp2040/limits.h`); the
  header buffer sbase sizes with it is instead sized with a local
  `#define PATH_MAX 256`, well past any name this flash root can
  hold, with the reason commented in place.

## A real bug the host tests caught

`expand.c`, `unexpand.c` and `nl.c`'s tab-stop and line-number bounds
were first written as `estrtonum(p, 1, MIN(LONG_MAX, (long)SIZE_T_MAX))`,
following the shape of sbase's `MIN(LLONG_MAX, SIZE_MAX)`. Casting
`SIZE_T_MAX` (`0xffffffff`, unsigned) to `long` (32-bit signed, same
width on both the host's `int`-adjacent test build and the ARM
target) produces `-1` in two's complement, so `MIN(LONG_MAX, -1)` is
always `-1`, `estrtonum`'s `minval(1) > maxval(-1)` and every call
fails with `strtonum 8: invalid` -- `expand`'s own default tab stop
rejected before it read a byte. `expand` and `unexpand` would have
failed on the board on their very first line, silently to anyone who
had not tried the default (no `-t`) invocation. The host build
(`usr.bin/textbox/tests/run.sh`) caught this because it runs the
tool, not just compiles it; the fix drops the flawed cast and uses
`LONG_MAX` directly, which `SIZE_T_MAX` (or `SIZE_MAX` on the host,
see `compat.h`'s alias) always exceeds on this platform.

## Host tests

`usr.bin/textbox/tests/run.sh` (POSIX `sh`, `set -eu`,
`PYTHON=${PYTHON:-python3}` though this suite is pure `sh`/`cc`)
builds each tool with the host `cc` from the identical `.c` files the
rp2040 cross build uses, substituting only the three compat pieces a
glibc host already carries (`reallocarray`, `memmem`, `getline` --
`usr.bin/textbox/tests/ereallocarray_host.c` gives `ereallocarray()`
alone so `reallocarray()` itself is not redefined against glibc's
own), and diffs each tool's output against GNU coreutils or GNU
sharutils for the same input:

```
sh usr.bin/textbox/tests/run.sh
```

Results: cut (`-d:-f2`, `-c1-3`), paste (`-d,`), seq (`1 10`,
`5 2 20`), dirname, cksum, expand, unexpand (`-a` after `expand`),
mkfifo (creates a FIFO-typed node), and uuencode/uudecode (both the
traditional and `-m` base64 encodings, round-tripped through our own
uudecode and cross-checked against the system's `uuencode`/`uudecode`
where installed) all match. nl differs from GNU coreutils on one
point: GNU pads a blank line with the number field's width in spaces
even though it prints no number there; sbase's (and so this port's)
`nl` prints the blank line as-is. This is upstream sbase-vs-GNU
divergence in the unmodified algorithm, not something the regex
removal changed -- checked by reading sbase's `nl.c` before any edit,
where the same `fwrite(line.data, ...)` with no leading pad already
stands for the `donumber == 0` case.

## Cross build and manifest

```
bmake MACHINE=rp2040 symlinks          # include/machine -> rp2040
bmake MACHINE=rp2040 -C lib
bmake MACHINE=rp2040 -C tools install  # elf2aout, etc.
bmake MACHINE=rp2040 -C usr.bin cut paste seq dirname nl cksum \
    expand unexpand mkfifo uuencode uudecode
bmake MACHINE=rp2040 -C sbin/textbox
bmake MACHINE=rp2040 fs
```

`Makefile`'s top-level `SUBDIR` now builds `usr.bin` before `sbin`
(`share lib bin usr.bin sbin libexec usr.sbin games`, was `... bin
sbin libexec usr.bin ...`): `sbin/textbox`'s `TOOLS` loop, like
`sbin/box`'s and `sbin/sysbox`'s, links each tool's objects out of
the `usr.bin/<tool>` directory that already built them, and
textbox's tools live under `usr.bin` where box's and sysbox's live
under `bin` (already before `sbin`). `usr.bin/Makefile`'s `SUBDIR`
gained the eleven new directories (mkfifo included, for the
build-and-warn check, though it does not ship). `distrib/rp2040/mi.rp2040`
gained `file /usr/bin/textbox` and ten `link`/`target` pairs for cut,
paste, seq, dirname, nl, cksum, expand, unexpand, uuencode and
uudecode, following the existing box/sysbox comment and link-block
style. `bmake MACHINE=rp2040 fs` builds `distrib/rp2040/sdcard.img`
successfully with this manifest; extracting the image
(`tools/bin/fsutil --extract`) shows all eleven names present at
30,124 bytes, matching the standalone `sbin/textbox/textbox` build.

## What was not run

The board is not available to this task and was not touched: no
`/dev/ttyACM0`, no `picotool`, no flash write, no boot test. The
a.out cross build's correctness rests on (a) matching source and
build mechanism to `box`/`sysbox`, which are verified booting per
`~/Github/rpi/STORAGE.md`, and (b) the host test suite proving the
shared C logic against GNU coreutils/sharutils output, not on running
the ARM binary anywhere. `bmake MACHINE=rp2040 distribution` (the
full release step, `etc`'s `/etc`, `/root`, `/var` population) was
not run either; only `build` and `fs` were, which is sufficient to
prove the manifest and the box link but leaves `sdcard.img` without
`/etc/rc` and friends in this test run -- unrelated to this change,
and the same gap exists on `main` without it.

## Report

- Box: `sbin/textbox/textbox`, 30,124 bytes on disk (text 29,364 +
  data 728 + a.out header), under the 30 KB budget.
- Shipped (10): cut, paste, seq, dirname, nl, cksum, expand,
  unexpand, uuencode, uudecode.
- Did not fit (4, priority order low to high, all compile clean and
  are in `usr.bin/Makefile`'s `SUBDIR`): fold, rev, comm, mkfifo.
- One correctness bug found and fixed before it reached the target:
  `expand`/`unexpand`/`nl`'s tab-stop and line-number upper bound
  cast `SIZE_T_MAX` to a negative `long`, rejecting every call
  including the default tab stop.

## Follow-up after merge

fold, rev and comm joined the box once the root had 102 KB free: the
three cost 3 KB together because the tools share one libc image, and the
box measures 32,436 bytes with thirteen tools. mkfifo stays out for a
reason the host tests could not see: sys/kern knows S_IFIFO only as a
bit in stat.h, and on the board a writer and a reader on the node it
makes exchange nothing. The box rule now runs each tool's own make
before the relocatable link, so a clean tree builds in any order.
