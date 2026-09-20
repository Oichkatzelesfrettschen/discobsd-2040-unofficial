# 4.4BSD-Lite2 candidates for the rp2040 port

Measured against the tree at commit 870fc71c (2026-09-12, branch
`audit-correctness`) and against `the 4.4BSD-Lite2 checkout`, the
4.4BSD-Lite2 CSRG distribution. Target: RP2040, Cortex-M0+, ARMv6-M,
Thumb-1, no FPU, no MMU, an eight-region MPU programmed to fence the user window, 264 KB SRAM, one 96 KB user process
window, a.out OMAGIC binaries, a 2.11BSD-lineage kernel (no demand
paging, no vnode layer, no mbuf network stack, single core), a root
filesystem with roughly 130 free blocks, and two compilers: cross-gcc
for shipped binaries and the on-board Smaller C (`usr.bin/smlrc`,
Thumb-1 backend `cgthumb.c`) for native builds.

The libc already in this tree is not a clean-room 2.11BSD libc. It
carries 4.4BSD-Lite2-derived files verbatim, copyright headers included:
`lib/libc/gen/err.c`, `lib/libc/string/strlcpy.c`,
`lib/libc/string/strlcat.c`, `lib/libc/string/strsep.c`, and
`lib/libc/stdlib/getopt.c` (with `optreset`, the 4.4BSD extension) are
already in place, and `sys/sys/signal.h` already declares
`sigaction`/`sigprocmask`/`sigsuspend`. The gap this survey found is
narrower than the four-item brief suggests, because a prior pass already
took the low-hanging libc fruit. What follows was found by diffing
`lib/libc`'s file set against 4.4BSD-Lite2's, file by file, then reading
the remainder for fit against the constraints above.

Ranked by (benefit / footprint), highest first.

## 1. `<err.h>` -- the header for a function this tree already ships

**Source:** `include/err.h` (60 lines, 4.4BSD-Lite2). No `.c` change: the
implementation (`err`, `verr`, `errx`, `verrx`, `warn`, `vwarn`, `warnx`,
`vwarnx`) is already `lib/libc/gen/err.c` in this tree, 186 lines,
byte-identical in spirit to the 4.4BSD-Lite2 original down to the
`putprog`/`putcolsp` helper functions.

**What it does.** Declares the eight functions `err.c` defines. Nothing
else.

**Why it matters.** `include/` has no `err.h`. Fifteen call sites
already invoke `err()`/`warn()`/`errx()`/`warnx()` with no prototype in
scope: `usr.bin/uname/uname.c`, `usr.bin/printf/printf.c`,
`usr.bin/virus/virus.c`, `usr.bin/calendar/calendar.c`,
`usr.bin/hostid/hostid.c`, `usr.bin/id/id.c`, `sbin/shutdown/shutdown.c`,
`bin/test/test.c`, `usr.bin/env/env.c`, `sbin/umount/umount.c`,
`sbin/mount/mount.c` and `getmntopts.c`, `bin/hostname/hostname.c`,
`bin/df/df.c`, `bin/stty/stty.c`.

Checked all 15 files for a local workaround -- an `extern` line or a
hand-rolled prototype declaring `err`/`warn` before first use, the usual
way a 4.4BSD tool ported without `<err.h>` papers over the gap. None
exists: grepping each file for `extern` and for a `void`-returning
declaration of either name turns up nothing but the call sites
themselves. Every one of these calls a variadic, `void`-returning
function the compiler has not seen declared -- an implicit
`int`-returning declaration under K&R rules, silently wrong against the
real (`void`, varargs) signature. It has not broken yet because none of
these call sites use the return value and because both compilers in
this tree tolerate the mismatch on the calling convention they use for
`...`-argument calls, but it is exactly the class of latent bug a
stricter build (`-Wimplicit-function-declaration` as an error, or a
compiler that does register-based varargs) turns into a crash. Adding
the header fixes this outright, matches what these files already
assume, and costs nothing: the object code is unchanged, since `err.c`
is already linked into everything that calls these functions.

**Footprint.** Zero object-code bytes. One new header, adapted from 60
lines to this tree's plain style.

**Porting gotchas.** The upstream header pulls `<machine/ansi.h>` for
`_BSD_VA_LIST_` and `<sys/cdefs.h>` for `__BEGIN_DECLS`/`__P`/`__dead`;
this tree has neither header (confirmed: no `cdefs.h` anywhere under
`include/`) and its own recently-added headers (`include/sysexits.h`, a
2.11BSD file, not 4.4BSD-Lite2) use plain ANSI prototypes with no such
wrapper. Write the port without `__P`, `__BEGIN_DECLS`, or `_BSD_VA_LIST_`
-- `#include <stdarg.h>` and prototype with `va_list` directly, matching
`err.c`'s own signatures (`err(int, const char *, ...)`, `verr(int,
const char *, va_list)`, etc.). Smaller C supports variadic functions
(`usr.bin/smlrc/smlrc.c` has explicit `va_list`-shape detection,
`DetermineVaListType`), so the header is safe for on-device native
builds as well as cross-gcc.

## 2. `<sys/queue.h>` -- linked-list and tail-queue macros

**Source:** `sys/sys/queue.h` (259 lines, 4.4BSD-Lite2, `@(#)queue.h
8.5`). Not present anywhere in this tree (`find . -iname queue.h` finds
nothing under `sys/` or `include/`).

**What it does.** `LIST_*`, `TAILQ_*`, and `CIRCLEQ_*` macro families:
singly-linked lists, doubly-linked tail queues, and circular queues,
built entirely from struct-embedding and pointer-to-pointer tricks (a
`LIST_ENTRY`/`TAILQ_ENTRY` embedded in the element struct, no
allocation, no hidden state). `LIST_REMOVE` is O(1) without a
head-pointer walk because the macro stores the address of the previous
element's `next` field, not just a back-pointer.

**Why it matters.** Every one of the 24 macros expands to plain ANSI C
-- struct literals and brace-wrapped, backslash-continued compound
statements, no `##` token-pasting, no `typeof`, no GNU statement
expressions. It compiles under both toolchains in this tree without
modification. It replaces nothing today -- `sys/kern` uses
array-based or ad hoc pointer-chasing lists (`callout.h`'s fixed-size
callout table, hand-rolled chains in `kern_proc.c`) -- so adopting it is
purely additive: new kernel or userland code that needs a list gets a
correct, debugged one instead of a fresh hand-rolled one, at zero
runtime cost, since every macro compiles to the same inline pointer
arithmetic a hand-written list would use.

**Footprint.** Zero object-code bytes (header-only, no functions, no
data). ~8.4 KB of source text that is never linked into a program that
doesn't use the macros.

**Porting gotchas.** The macros are brace-wrapped
(`#define LIST_INIT(head) { ... }`), not the safer `do { ... } while
(0)` idiom later BSDs adopted for the same file. `if (c) LIST_REMOVE(a,
b); else foo();` expands to `if (c) { ... }; else foo();` -- a stray
semicolon after the closing brace breaks the `if`/`else` pairing and is
a compile error, not a silent bug, but it is a real trap for a first-time
caller writing the macro invocation the way a normal function call
looks. Wrap each multi-statement macro in `do { ... } while (0)` when
porting the header in, matching how later BSD `queue.h` revisions fixed
the same defect; that is the one change to make to the upstream text,
everything else copies verbatim. Otherwise this is the least risky item
on this list: it changes no existing behavior, because nothing in the
tree includes it yet. The only judgment call is scope -- add the header
and use it in new code going forward; do not use it as a pretext to
refactor `kern_proc.c` or `callout.h`'s existing working list logic,
which the
brief's "very conservative" instruction for kernel changes rules out.

## 3. `<vis.h>`, `vis()`/`strvis()`, `unvis()`/`strunvis()` -- safe encoding of non-printable bytes

**Source:** `include/vis.h` (84 lines), `lib/libc/gen/vis.c` (186
lines), `lib/libc/gen/unvis.c` (248 lines). Total ~518 lines, ~13.7 KB
of source, all 4.4BSD-Lite2. Confirmed absent: no `vis`, `strvis`, or
`unvis` symbol anywhere in this tree's `include/` or `lib/`.

**What it does.** `vis()` encodes one byte, `strvis()` a whole string,
into a printable representation -- octal (`\ddd`), C-style escapes
(`\n`, `\t`, ...), or both, controlled by flag bits (`VIS_OCTAL`,
`VIS_CSTYLE`, `VIS_WHITE`, `VIS_SAFE`, `VIS_NOSLASH`). `unvis()`/
`strunvis()` invert it. No heap allocation; the caller supplies the
output buffer (`strvis` requires up to 4x the input length in the worst
case, documented in the header's callers, not in `vis.h` itself -- a
gotcha to carry into any port's own doc comment).

**Why it matters.** This tree has no equivalent facility. Any future
tool that must print an untrusted or binary-capable string safely to a
terminal -- a filename with control characters, a corrupted-record
dump, a `cat -v`-style filter, safer `ls` output for filenames that
contain escape sequences (a real terminal-injection concern on a serial
console with no window manager to sandbox it) -- currently has nothing
to call and would hand-roll an encoder, which is exactly the kind of
small, easy-to-get-subtly-wrong code this library exists to standardize.

**Footprint.** ~518 lines total; `vis.c`/`unvis.c` are switch-driven
byte pushers with no loops over anything but the input length -- object
code in the same size class as `strlcpy.c`/`strsep.c`, which this tree
already carries. Only linked into programs that call these functions.

**Porting gotchas.** Strip `<sys/cdefs.h>`/`__BEGIN_DECLS`/`__P` as with
`err.h` above; the declarations themselves (`vis`, `strvis`, `strvisx`,
`strunvis`, `unvis`) translate to plain ANSI prototypes with no other
change. No floating point, no locale, no wide characters. Self-contained
against `<stdio.h>`/`<ctype.h>` already present.

## 4. `fmt(1)` -- paragraph-fill text formatter

**Source:** `usr.bin/fmt/fmt.c` (467 lines, 10,845 bytes of source,
4.4BSD-Lite2).

**What it does.** Reflows paragraphs: joins short lines and rewraps at a
target width, preserving blank-line paragraph breaks and leading
indentation, tracking sentence-end spacing (two spaces after `.`/`!`/`?`
by default). This is distinct from `fold(1)`, already shipped, which
breaks a line at a fixed column with no regard for word boundaries or
paragraph structure and does not rejoin short lines.

**Why it matters.** The existing toolset (`sh`, `ed`, `vi`/stevie, `awk`,
`sed`, the sbase and utilbox sets) has nothing that reflows text. That
is a real, missing capability for anyone composing `mail`, a README, or
man-page source directly on the device, where `fmt` is the traditional
one-pass answer and `fold` is not a substitute for it.

**Footprint.** This is the one candidate on the list where installed
size, not source size, is the binding number, and installed size is not
measured here -- it needs a real build with this tree's cross-gcc, not
an estimate from source lines. What follows is inferred, not measured:
`STORAGE.md` shows `cat` at 1.4 KB of its own code landing at 17 KB
installed and `id` at 1.5 KB landing at 20 KB, both dominated by the
static libc pull-in (stdio, ctype-table, getopt) rather than the
program's own text. `fmt.c`'s 467 lines is roughly 2-3x `cat`'s own
code and pulls the same stdio/ctype/getopt baseline plus, once item 1
lands, `err.c` (already linked everywhere else, so no new baseline
cost). A working estimate is 12-18 KB installed, comparable to `id` --
but that is an inference from a different program's numbers, not a
measurement of `fmt` itself, and against a ~130-free-block root it
should be confirmed with an actual cross-gcc build and `size` before
committing filesystem space to it.

**Porting gotchas.** None structural. Straight ANSI C, no locale, no
wide characters, no floating point. Verify the upstream getopt usage
matches this tree's `getopt.c` (it does -- both are the same 4.4BSD
`getopt`).

## 5. `heapsort()` -- an optional, worst-case-bounded sort to sit next to `qsort()`

**Source:** `lib/libc/stdlib/heapsort.c` (184 lines, 4.4BSD-Lite2).

**What it does.** In-place heapsort: build a max-heap over the array
(`CREATE`), then repeatedly swap the max to the end and re-sift
(`SELECT`). One `malloc(size)` call for a single element's worth of swap
scratch space, freed before `heapsort()` returns -- O(1) extra memory,
not O(n). No recursion.

**Why it matters, precisely, and why it sits last of the five.** This
tree's `qsort()` (`lib/libc/gen/qsort.c`, a 2.11BSD file, 202 lines) is
already a hardened median-of-3 quicksort that recurses on the smaller
partition and loops on the larger (`qst()`'s tail: `if (lo <= hi) {
qst(base, smaller); base = larger; } else { qst(larger); max = smaller;
}`), which bounds recursion depth to O(log n) even on adversarial input
-- so this is not the stack-overflow-prone naive quicksort found in some
historic libc's, and heapsort does not fix a stack-safety bug that
exists here. What it does fix: median-of-3 quicksort still has known
adversarial input sequences that force O(n^2) *comparison* count
regardless of recursion-depth bounding, and this tree calls `qsort()` on
externally-influenced data -- `bin/ls/ls.c` sorts directory entries by
name, and directory entry order is exactly the kind of thing a crafted
set of filenames can shape. Heapsort's O(n log n) is worst-case, not
just average-case. Value here is real but modest -- a defense against a
low-probability, low-severity (CPU time, not memory corruption) input
pattern on a single-user embedded console, not a security-critical fix
-- and unlike items 1-4 it has no forcing caller today: nothing in this
tree currently needs `heapsort()`'s guarantee badly enough to have hit
the `qsort()` pathology in practice. It ranks below `vis` and `fmt`,
which both fill a capability this tree has zero of today, and stays on
the list as an optional hardening item to add opportunistically, not one
to schedule ahead of a real gap.

**Footprint.** ~184 lines, roughly 1-1.5 KB of Thumb-1 object code
(comparable density to the existing `qsort.c`). Only linked into
programs that call `heapsort()` explicitly -- it does not replace or
shadow `qsort()`, so it costs nothing for programs that keep calling
`qsort()`.

**Porting gotchas.** Uses `malloc`/`free` from `<stdlib.h>`, already
present. Uses `memmove`-shaped byte-copy loops via its own `SWAP`/`COPY`
macros (no dependency on this tree's absent `memmove.c`). No floating
point, no 64-bit division, no locale calls. One real behavioral gotcha
for Thumb-1/no-MMU: `heapsort()` returns `int`, -1 with `errno` set to
`ENOMEM` when its one `malloc(size)` call fails, unlike `qsort()`, which
is `void` and cannot report an allocation failure at all. On a 96 KB
process window with a heap that fragments under the box's usual
mixed-size allocation pattern, that failure path is reachable, not
theoretical -- any call site converted from `qsort()` to `heapsort()`
must start checking a return value it never had to check before, or it
silently leaves the array unsorted on the failure path.

## Secondary candidates (worth a line, not top-5)

- **`getcwd()`** (`lib/libc/gen/getcwd.c`, 384 lines, 4.4BSD-Lite2)
  would replace this tree's `getwd()` (`lib/libc/gen/getwd.c`), which
  takes a fixed-size buffer with no length check, the classic `getwd()`
  overflow footgun. Lower priority than the top 5 because no
  user-visible program in this tree currently calls `getwd()` in a way
  that has shown a problem, and because 4.4BSD-Lite2's `getcwd()` walks
  `.`/`..` pairs through `opendir()`/`fstat()` rather than calling a
  kernel `__getcwd`-style syscall -- portable as plain userland code
  against `sys/kern/ufs_namei.c`'s existing directory semantics, but
  384 lines of directory-walking and two `malloc(1024 - 4)` scratch
  buffers is a heavier dependency than the one-line problem it solves
  justifies today.
- **`column(1)`** (`usr.bin/column/column.c`, 305 lines, 7,171 bytes) --
  columnates lists into a terminal-width table. Complements the sbase
  `cut`/`paste`/`nl` set already shipped but overlaps enough with `pr(1)`
  (already present) that it is a nice-to-have, not a gap. Same
  source-vs-installed caveat as `fmt` above applies to any footprint
  claim for it.
- **`bsearch()`** -- 4.4BSD-Lite2's is a ~50-line binary search
  complementing `qsort()`. No call site anywhere in this tree wants it
  today (the only `bsearch` hits found are inside `usr.bin/picoc`'s own
  embedded libc and `usr.bin/lccom`'s test fixtures, both unrelated to
  the system libc). Trivial to add whenever a real caller shows up;
  not worth doing speculatively.
- **`fnmatch()`** (`lib/libc/gen/fnmatch.c`, 171 lines, 4.4BSD-Lite2) --
  POSIX-required shell-pattern matching, self-contained, no locale
  dependency worth mentioning for a C-locale-only tree. Held at
  secondary for the same reason as `bsearch`: no call site in this tree
  currently wants a standalone `fnmatch()` (`csh`'s globbing,
  `bin/csh/sh.glob.c`, and `find`'s `-name` matching both carry their
  own inline pattern matcher rather than calling a libc function). It is
  the same tier of speculative-consumer argument used for `vis` above,
  and sized similarly small; the two differ in that `vis` covers a
  capability (safe non-printable-byte display) nothing in the tree does
  at all, while `fnmatch` would duplicate logic two tools already
  implement inline. Worth adding the same day as `bsearch`, not before.

## Reject list

- **POSIX regex** (`lib/libc/regex/`: `regcomp.c` 1,698 lines,
  `engine.c` 1,091 lines, `regexec.c`/`regerror.c`/`regfree.c` plus
  `regex2.h`/`cclass.h`/`cname.h`/`utils.h`, 3,671 lines total, ~110 KB
  source). This tree already ships a compact BSD `regexp(3)`-style
  engine at `lib/libc/gen/regex.c`, 400 lines, that `grep`/`sed`/`ed`
  already build against. Swapping in the 4.4BSD-Lite2 POSIX engine would
  be a ~9x code-size increase for a capability (POSIX `regcomp`/`regexec`
  API, submatch arrays) none of the shipped tools need.
- **Berkeley DB** (`lib/libc/db/`: btree, hash, recno, mpool --
  10,509 lines total across the four subsystems). A memory-mapped,
  paged-cache storage engine built on `mmap()`. This kernel has no
  vnode layer and no page cache to hang `mmap()` semantics on; the
  tree's own `ndbm.c` already covers the one on-device consumer
  (`include/ndbm.h`). Non-starter against the no-MMU constraint alone.
- **Locale and wide-character code** (`setlocale.c`, `localeconv.c`,
  `strcoll.c`, `strxfrm.c`, `mbrune.c`/`rune.c`/`euc.c`/`utf2.c`). The
  entire tree is fixed-C-locale, single-byte ASCII. These add code
  (`strcoll`/`strxfrm` alone pull the whole locale machinery) with no
  behavioral payoff: `strcoll("a","b")` and `strcmp("a","b")` return the
  same answer in the C locale, and this port has no other locale.
- **`gprof`/`gmon.c`/`mcount.c` and `kdump`/`ktrace`** (936 lines for
  the profiling pair alone, plus the `ktrace(2)` kernel event-record
  facility). Both need kernel hooks -- a profiling clock sampler for
  `gprof`, a per-process trace-record queue for `ktrace` -- this
  2.11BSD-lineage, single-core kernel has neither, and building either
  hook is a kernel feature project, not a userland backport.
- **Every networking `usr.bin`/`usr.sbin` tool**: `ftp`, `telnet`,
  `rlogin`, `rsh`, `talk`, `netstat`, `nfsstat`, `rwho`/`ruptime`/`rwhod`,
  `biff`, `finger`, `from`, `showmount`, `tftp`, `tn3270`, `arp`,
  `traceroute`, `inetd`, `syslogd`, `portmap`, `amd`, `sendmail`,
  `sliplogin`. The constraint is explicit: no mbuf networking stack
  ships. None of these link without one.
- **`quota`/`edquota`/`repquota`/`quotaon`**. 4.4BSD disk quotas are a
  VFS/vnode-layer feature; `sys/kern/ufs_*.c` in this tree has no vnode
  layer to attach quota checks to.
- **`window(1)`** (13,891 lines across its sources). A pty-multiplexing
  terminal manager, one of the largest single tools surveyed in either
  tree. This kernel's pty support (`sys/kern/tty_pty.c`) is sized for
  one pty pair's worth of complexity, and the single 96 KB process
  window has no room for a multi-pane terminal manager on top of
  whatever it is multiplexing.
- **`xstr(1)`** (471 lines, 9,074 bytes). A PDP-11-era build step that
  pulls string literals out of source into a shared `strings` file so
  multiple programs sharing a text segment don't duplicate them. Every
  a.out on this port is linked and loaded independently -- there is no
  shared text segment across processes -- so `xstr`'s entire value
  proposition does not transfer, and it would add a runtime data-file
  dependency per tool that the ~130-free-block root filesystem cannot
  afford for a technique that saves nothing here.
- **`patch(1)`** (`patch.c` 800 lines, `pch.c` 1,108 lines, `inp.c` 313
  lines, `util.c` 339 lines -- 2,800+ lines, ~62 KB source, the largest
  userland candidate surveyed). `diff(1)` ships without a `patch`
  counterpart, which is a real gap, but the size and format-sniffing
  complexity (context/unified/ed-script diffs, `.orig` backups, reject
  files, fuzzy offset matching) is a poor trade against tools already
  on the box; revisit only if on-device patch application becomes a
  workflow this port actually needs.
- **`fgetln()`**. 4.4BSD-Lite2's implementation is written against the
  "new" BSD stdio internals (`_r`/`_p`/`_lb`, the unget/line-buffer
  fields from that libc's buffered-I/O rewrite). This tree's stdio is
  the older layout -- `include/stdio.h`'s `getc`/`putc` macros expand
  through `_cnt`/`_ptr`/`_iob`, not `_r`/`_p` -- so `fgetln()` cannot be
  dropped in verbatim; it would need a full reimplementation against
  this tree's actual `FILE` struct, at which point it is new code, not
  a backport.

## Top pick, in one line each

1. `<err.h>` -- write the header, ship nothing new, fix a real
   implicit-declaration hazard at 15 call sites, for free.
2. `<sys/queue.h>` -- zero-cost, zero-risk, ANSI-clean macro header
   (wrap each macro in `do { ... } while (0)` while porting); pure
   future-proofing for kernel and userland list code.
3. `vis.h`/`vis()`/`unvis()` -- ~518 lines, fills a real gap (safe
   encoding of non-printable bytes) with no equivalent in the tree today.
4. `fmt(1)` -- ~467 lines, the one missing text-processing verb
   (paragraph reflow) that `fold(1)` does not cover; confirm installed
   size with a real build before committing root-filesystem space.
5. `heapsort()` -- 184 lines, O(1) extra memory, worst-case-bounded
   alternative to `qsort()` for the one caller (`ls`) that sorts
   externally-influenced data; optional hardening, add opportunistically.

## Landed

Branch `backport-bsd44`, eleven candidates surveyed above, nine landed as
nine commits plus three follow-ups that fix defects the ported text carried
in. Sizes are cross-gcc output for MACHINE=rp2040: `text` bytes
from `arm-none-eabi-size` for a libc object, on-disk a.out bytes after
`elf2aout` for a program. The build gate for every commit is
`bmake MACHINE=rp2040 build`, exit 0, with the warning set a subset of the
pre-change baseline; the final clean build emits 214 warning lines, the same
count as the pre-change clean baseline, and none of them names a file this
branch added.

| Item | Commit subject | Size | In mi.rp2040 |
| --- | --- | --- | --- |
| `<err.h>` | `include: give the err(3) family its own <err.h>` | 0 (header) | header, not a manifest entry |
| `<sys/queue.h>` | `sys: add 4.4BSD-Lite2 <sys/queue.h>` | 0 (header) | header, not a manifest entry |
| vis(3) | `libc: add the 4.4BSD-Lite2 vis(3) and unvis(3) encoders` | vis.o 420, unvis.o 428 | in libc.a, linked on demand |
| fmt(1) | `fmt: port the 4.4BSD-Lite2 paragraph formatter` | 11700 | no |
| heapsort(3) | `libc: add heapsort(3) beside qsort(3)` | 388 | in libc.a, linked on demand |
| bsearch(3) | `libc: add bsearch(3) to complete the sort and search pair` | 48 | in libc.a, linked on demand |
| fnmatch(3) | `libc: add fnmatch(3) for POSIX shell-pattern matching` | 384 | in libc.a, linked on demand |
| column(1) | `column: port the 4.4BSD-Lite2 list columnator` | 11140 | no |
| getcwd(3), realpath(3) | `libc: add getcwd(3) and realpath(3) beside getwd(3)` | 1101 | in libc.a, linked on demand |

`fmt` and `column` build from `usr.bin`'s SUBDIR and install into
`${DESTDIR}`, and neither appears in `distrib/rp2040/mi.rp2040`, so neither
spends a block of the root filesystem image. A libc object costs nothing
until a program references one of its symbols.

### Corrections to the survey

Item 1's premise is wrong. `include/unistd.h` already declared all eight
err(3) functions with the same `va_list` guard the port needed, so the
fifteen call sites had a prototype in scope and the baseline build shows no
implicit-declaration warning for `err`, `warn`, `errx` or `warnx`. The
commit moves those eight declarations into `include/err.h` and has
`<unistd.h>` include it, which gives `#include <err.h>` the header it names
without changing what any existing caller sees.

fmt(1) needs `ishead()`, which 4.4BSD-Lite2 builds from `../mail/head.c`.
This tree's `usr.bin/mail` is a single-file mailer with no `head.c`, so the
port carries `usr.bin/fmt/head.c` from mail 8.2, self-contained: everything
but `ishead` is static, and the three declarations it drew from `rcv.h` and
`def.h` are spelled out in the file.

column(1) carries an upstream precedence bug, `realloc(cols, (u_int)maxcols
+ DEFCOLS * sizeof(char *))`, which grows the column and length arrays by
`maxcols` bytes plus `DEFCOLS` elements instead of `maxcols + DEFCOLS`
elements and overruns them past the 25th field. The port parenthesizes both
reallocs.

`LINE_MAX` is absent from this tree's `<limits.h>`, so column's
`MAXLINELEN` derives from `BUFSIZ` rather than 4.4BSD's 2048.

### Defects fixed in the ported text

Three, each with a check that fails on the upstream text and passes on the
ported one.

`getcwd_physical()` reset `bup` to the start of the scratch buffer when it
grew it, discarding every `../` built so far. With `MAXNAMLEN` 63 and
`MAXPATHLEN` 256 the grow fires around 63 levels down, inside the depth a
256-byte path reaches, so the reset is reachable here rather than
theoretical. Over a host shim carrying those two values, a 70-level
directory returns the correct path from the ported text and ENOENT from the
upstream text.

`fmt`'s `ispref()` advanced only `s1`, comparing every character of the
headname against `s2[0]`, so any headname whose first character matched
answered yes and a line like `Tx: hello` was held on its own unwrapped line
as a mail header. Walking both strings wraps it as ordinary text and still
holds a real `To:` header.

`column`'s `maketbl()` reallocs, described above.

`<err.h>` shipped first with `<unistd.h>`'s `_VA_LIST_` guard around a
`va_list` definition it then undefined. Nothing in this tree defines
`_VA_LIST_`, so the undef fired unconditionally and a file including only
`<err.h>` could not declare the `va_list` it must pass to `verr()`. The
header includes `<stdarg.h>` instead, which is what the survey's own porting
note prescribed.

### Skipped

Everything on the reject list above stays rejected for the reason given
there: POSIX regex (9x the code of the engine already shipped), Berkeley DB
(mmap), locale and wide characters (fixed C locale), gprof and ktrace
(absent kernel hooks), every networking tool (no mbuf stack), disk quotas
(no vnode layer), window(1) (no room), xstr(1) (no shared text segment),
patch(1) (size against `diff`-only workflow), and fgetln() (written against
a stdio layout this tree does not have).

A man page for vis(3), heapsort(3), bsearch(3), fnmatch(3), getcwd(3),
fmt(1) or column(1) is not added. `lib/libc` carries no man directory; the
tree's libc pages live in `share/man/man3`, and adding one there is a
separate decision about root filesystem space.
