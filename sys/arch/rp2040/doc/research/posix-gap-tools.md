# Five POSIX-gap tools for rp2040: md, man, make/yacc/lex/ar, which, ANSI vs Unicode

Measured against the tree at commit a7fc2bad (2026-09-12), same terminal
model as `usr.bin/kilo`: 80x24, xterm-like, ANSI escapes over serial, no
MMU, 96 KB process window, a.out linked at 0x20000000. STORAGE.md gives
70 KB free on the built root (901 of 971 KB used); every byte below is
weighed against that number, not against the 1536 KB Dhara region.

## 1. `md`, a Markdown viewer

### Feature subset

Render, do not edit. One pass, line-oriented, no buffering of the whole
file (`kilo` already owns the multi-KB-buffer editor niche and the 96 KB
window punishes large heap use -- see `utilbox.md`'s note on the 18 KB
overrun that wedged the kernel). Input is a file argument or stdin;
output goes to stdout so `md file.md | more` composes with the pager
already shipped (`usr.bin/more`, listed as shipping in STORAGE.md).

Recognized, in order of how cheap each is to detect at line start or
inline scan:

- ATX headings, `#` through `######`: color by level, bold on `#`/`##`.
- Rule: a line of three or more `-`, `*`, or `_` alone -- render as a
  full-width line of `-` in dim SGR.
- Fenced code blocks, a line of three backticks to open and a matching
  line of three backticks to close: passed through byte-for-byte, no
  inline parsing inside, set off by one SGR (dim or a fixed color) so it
  reads as a block.
- Unordered list items: `-`, `*`, or `+` at line start followed by a
  space -- render as `  * ` in a list color, one indent level (nesting is
  not tracked; a run of leading spaces before the marker is passed
  through verbatim, which reproduces one extra level visually without
  the parser carrying a stack).
- Ordered list items: `<digits>.` at line start -- passed through as
  typed, only the marker gets color.
- Inline bold `**text**` or `__text__`, inline italic `*text*` or
  `_text_`, and inline code `` `text` ``: single-pass scan for matching
  delimiter pairs on the same line; no nesting, no escaping beyond a
  literal backslash dropping the next character's special meaning.
- Everything else: paragraph text, passed through with inline spans
  applied, wrapped at column 80 by breaking on the last space before the
  limit (the same width `kilo` assumes; no reflow beyond that).

Out of scope, matching what a one-pass renderer without a document tree
cannot do cheaply: reference-style links (`[text][ref]`), tables,
footnotes, nested blockquotes, HTML blocks, and multi-line inline spans
(a `**bold` that does not close on the same line prints its `**`
literally rather than scanning forward). Link syntax `[text](url)`
renders as `text` in underline SGR with `(url)` following in dim, one
line only -- no scanning across a wrap point.

### ANSI SGR codes

Matches `kilo`'s existing vocabulary (`\x1b[%dm` for a color, `\x1b[0m`
to reset, `\x1b[39m` to drop to default foreground) so the escape
sequences already proven over this port's serial path and in xterm.js
are the only ones `md` emits -- no 256-color or truecolor codes, which
`kilo` does not use either and which cost more bytes per escape:

| Element | Code | Meaning |
|---|---|---|
| H1 | `\x1b[1;33m` | bold yellow |
| H2 | `\x1b[1;36m` | bold cyan |
| H3-H6 | `\x1b[36m` | cyan |
| Bold | `\x1b[1m` | bold |
| Italic | `\x1b[3m` | italic (xterm and xterm.js both render it; a terminal that ignores SGR 3 just shows plain text, no corruption) |
| Inline code | `\x1b[2m` | dim |
| Code block | `\x1b[2m` per line | dim |
| List marker | `\x1b[32m` | green |
| Rule | `\x1b[2m` + 80 `-` | dim line |
| Link underline | `\x1b[4m` | underline |
| Reset | `\x1b[0m` | end of span |

Every span closes with `\x1b[0m` rather than tracking a color stack --
one pass, no nesting, so there is never a second open span to restore.

### Size estimate

The floor in this tree for a syscall-light tool that skips stdio is
`usr.bin/tty` at 2,680 bytes (isatty plus write, no printf, per
`distrib/obj/destdir.rp2040/usr/bin/tty`). `cat` at 10,220 bytes
(STORAGE.md, `PRINTF_FLOAT=no` already applied) is the floor for a tool
that pulls in buffered stdio; `md` does not need `printf`'s float path
and should avoid `stdio` entirely, the way `sbin/box/box.c.in` uses
`write(2)` directly instead of `printf`. Built that way -- `read(2)` into
a fixed line buffer (a 256-byte line is enough at 80 columns; longer
lines truncate, matching `kilo`'s tab-rendering approach of a bounded
buffer, not an unbounded one), a hand-rolled line classifier, and
`write(2)` for both literal text and escape sequences -- `md`'s own code
should land in the 1.5-2.5 KB range, close to `tr`'s in-box cost of 723
bytes (`utilbox.md`) plus the extra state a line classifier needs over a
byte transliterator. Standalone, add libc's non-stdio floor (`tty`'s
2,680 bytes covers argument handling, `isatty`, and `write`; `md` adds
`read` and string comparison, still in that neighborhood) for a
standalone a.out in the 4-6 KB range. That overshoots the 3-5 KB target
by a small margin if built standalone; it lands inside the target once
boxed.

### Box or standalone

Box it. `md`'s own code is small and line-oriented like `tr`, `wc`, and
`head`, the tools STORAGE.md's box already collects into `utilbox`, and
the multicall pattern removes libc's per-program floor, which is where
the entire 4-6 KB standalone estimate above goes. A boxed `md` should
cost under 2 KB against `utilbox`'s already-linked libc, the same order
as `tr`'s 723 bytes or `head`'s 414 -- not the 3-5 KB of a standalone
a.out. `utilbox`'s `_end` sits at 0x2000de50, 39 KB below the 0x20018000
window ceiling (`utilbox.md`), so there is headroom for the addition
without repeating the 18 KB overrun that pulled `tail`, `tee`, and `du`
back out. Recommendation: add `md` as a `utilbox` member from day one,
not as a fourth standalone binary.

## 2. Man-page viewer

### What already exists

`usr.bin/man/man.c` (2.11BSD, unmodified) already assumes pre-formatted
`.0` cat pages: `manual()` builds `${dir}/${section}/${name}.0` and either
pages it (`add()`, which prepends the pager -- `usr.bin/more`, already
shipped) or dumps it raw (`cat()`, a `read`/`write` loop, no formatting
logic on device at all). `usr.bin/man/apropos.c` shares the binary and
greps a flat `whatis` index. Neither tool links `nroff`, `troff`, or any
macro package -- there is nothing to port for the on-device half; it is
already PATH search (`_PATH_MAN`, `MANPATH`) plus `more`.

The host side is also already built: `share/mk/sys.mk` sets `MANROFF` to
`mandoc -Tascii -Ios="DiscoBSD ${OSREV}"` when `mandoc` is on the build
host, falling back to `groff -mandoc -Tascii` (this host has both, plus
`col`, confirmed by `which`). `share/man/man1/Makefile`'s `.1.0` rule
runs `${MANROFF} $*.1 > $*.0`, and `share/man/Makefile`'s `whatis.db`
target builds the flat index `apropos` reads. A full host build already
produces `distrib/obj/destdir.rp2040/usr/share/man/cat{1..9}/*.0`: 1,005
files, 3.9 MB, confirmed with `du -sh` and `find | wc -l` on the built
tree. Sections 2-7 and 9 are library- and kernel-API references that do
not apply to a trimmed board libc with no man3 audience on device; they
inflate the 3.9 MB figure without inflating what a user of this port
would ever `man`.

### The gap: what ships, not what to build

Nothing needs writing on-device. The question is which of the 1,005
already-built cat pages travel from `distrib/obj` onto the 70 KB-free
root, and the answer is: a small curated set, not the tree. Measured
page sizes vary by an order of magnitude with command complexity --
`cat1/cat.0` is 2,103 bytes, `cat1/awk.0` is 6,488, `cat1/sh.0` is
23,603 (the Bourne shell reference, the largest man1 page checked).
Average across all 1,005 pages is 3.9 KB; STORAGE.md's own 70 KB budget
is the whole remaining root, not a docs allowance, so even a dozen
average pages (about 47 KB) would consume most of what is free.

### Recommendation

1. Ship `man` and `apropos` unconditionally: 12,488 bytes each as built
   (`distrib/obj/destdir.rp2040/usr/bin/{man,apropos}`), and per the box
   pattern above these are further candidates for `sysbox` or `utilbox`
   rather than two standalone a.outs -- 2.11BSD `man.c`'s own code is a
   few KB of `getopt`, string building, and `add()`/`cat()`; the 12,488
   byte figure is dominated by the same libc floor `box`ing removes
   elsewhere in this tree.
2. Curate a page list keyed to what STORAGE.md says ships in root: `sh`,
   `ed`, `awk`, `sed`, `grep`, `find`, `cc` -- the tools with a real
   option surface a user cannot guess from `--help` (this port carries
   no `--help` convention; these are 2.11BSD tools with terse or absent
   usage strings). Cut `sh.0` (23,603 bytes) from the first pass; every
   other candidate on this list is under 7 KB and the set totals under
   30 KB, still a third of the free budget after `man`+`apropos`
   (25 KB) and `md` are accounted for.
3. Build `whatis.db` from only the shipped subset (`whatis.db`'s target
   already walks whatever `cat*` directories exist under
   `${DESTDIR}/usr/share/man`, so restricting the install list restricts
   the index for free) so `apropos` never reports a page that is not
   there.
4. Treat the remaining ~975 pages as available-in-the-tree, the same
   status STORAGE.md gives `make`, `diff`, `tar`, and the rest: built by
   the host step whenever wanted, installed onto an SD card or a future
   larger root, not carried in the 70 KB now.

Flash cost: `man`+`apropos` 25 KB (or less once boxed), curated pages
under 30 KB for seven commands, whatis index a few hundred bytes (one
line per page, `NAME - one-line description`). Total under 55 KB against
70 KB free if this ships as the last addition; ship it after `md` (which
this document sizes at under 2 KB boxed) and before any of section 3's
candidates, none of which fit in what is left.

## 3. make, yacc(byacc), lex, ar, bison

### What exists, measured

All four named tools are in the tree and build for rp2040; `bison` is
not in the tree (`find . -maxdepth 2 -iname bison` returns nothing --
this port has byacc-style `yacc`, not GNU bison, matching STORAGE.md's
own wording). Built target a.outs, measured directly:

| Tool | Path | a.out size |
|---|---|---:|
| make | `distrib/obj/destdir.rp2040/usr/bin/make` | 25,380 |
| ar | `distrib/obj/destdir.rp2040/usr/bin/ar` | 26,589 |
| yacc | `distrib/obj/destdir.rp2040/usr/bin/yacc` | 34,416 |
| lex | `distrib/obj/destdir.rp2040/usr/bin/lex` | 50,008 |

`file` confirms `make`'s binary is `a.out little-endian 32-bit
executable` -- a real target build, not a host artifact left in the
object tree. `make`'s figure matches STORAGE.md's own "make (~25 KB)"
line. Sum of all four: 136,393 bytes, 1.9x the entire 70 KB free budget
before any other tool, man page, or growth margin is counted.

### Verdict

None of the four fit together; only `make` alone fits inside 70 KB, and
only if nothing else ships beside it. `yacc` and `lex` exist as a pair
for a reason -- a lexer generator with no parser generator to feed, or
the reverse, serves a narrower slice of "real projects" than the pair
together, and the pair alone is 84,424 bytes, still more than the whole
budget. `ar` matters only once a project links multiple `.o` files into
a library, which this 96 KB single-process, no-MMU target does not
reward the way a multi-translation-unit host project does -- `ld`
already ships (STORAGE.md: "/usr/bin/ld (23 KB)") and can link `.o`
files directly without an archive step for anything a device-resident
build is likely to produce.

Recommendation: ship none of the four in the 70 KB now available. If a
future flash-layout change (STORAGE.md notes 38 KB of growth room in the
kernel region and floats a third text-tools box as "the next step")
frees enough root space, `make` alone is the one to add first -- it is
the smallest of the four, and every "real project" argument for the set
starts with wanting a build driver, not a parser generator, once `cc`,
`as`, and `ld` are already on the board (STORAGE.md confirms all three
ship). `yacc`+`lex` stay a matched pair for whenever 84 KB is free
alongside everything else already on root, which STORAGE.md's own 38 KB
kernel-region margin does not reach on its own. `ar` stays out
indefinitely: its function is covered by direct `ld` linking at this
scale, and nothing in the shipped toolchain currently produces or
consumes `.a` archives on device.

## 4. which vs type

### What `type` does today

`bin/sh/msg.c:178` registers `type` as a shell builtin, `SYSTYPE`
(`bin/sh/defs.h:70`). `bin/sh/xec.c`'s `SYSTYPE` case calls
`what_is_path()` (`bin/sh/hashserv.c:312`) once per argument. That
function looks the name up in the shell's command hash table and prints,
per 2.11BSD convention:

- `<name> is a shell builtin` if `hfind()` marks it `BUILTIN`.
- `<name> is a function` plus the function body, if it is a shell
  function (`FUNCTION`).
- A path result for `REL_COMMAND` entries, resolving through
  `pathlook()` and reporting "not found" when that lookup fails.

This covers the three things a POSIX `type` is asked to distinguish --
builtin, function, external command -- using the shell's own hash table
rather than a fresh `$PATH` walk, so `type`'s answer reflects what the
running shell would actually execute, hash cache included.

### Whether a standalone `which` adds value

No. `which`'s entire job -- resolve a name against `$PATH` and print the
first match -- is the `REL_COMMAND` branch of `what_is_path()` already
running inside `sh`, and a standalone `which` run from a *different*
process cannot see the invoking shell's hash table or its builtins and
functions, so it would answer a narrower question (`$PATH` only) less
accurately than `type` answers the fuller one, while costing a full
libc-floor a.out (10-13 KB by this tree's own numbers, see section 1) to
do it. `usr.bin/whereis` already exists in the tree for the adjacent,
genuinely different question ("where is the binary, source, and man page
for this name, searching fixed directory lists") that `type` does not
answer -- so the "resolve a name" niche is not empty, it is filled by
`type` for a running shell's view and `whereis` for a filesystem-wide
search, and a `which` sits exactly between them adding no case neither
already covers.

### Recommendation

Add nothing. `type` is already correct and already free (it is shell
code that exists regardless of what ships in `/bin`). `whereis` already
covers the multi-directory search `which` would otherwise justify. This
is the one item in this document with a zero-byte recommendation.

## 5. ANSI SGR vs Unicode over serial

### The terminal contract this port already assumes

Every escape-sequence user in the tree is 7-bit-clean today. `kilo`
(`usr.bin/kilo/kilo.c`) issues cursor position reports (`\x1b[6n`),
absolute cursor moves (`\x1b[%d;%dH`), the standard SGR set
(`\x1b[1m`/`\x1b[7m`/`\x1b[0m`/`\x1b[39m`, plus 30-37 foreground colors
for syntax highlight), and line-clear (`\x1b[0K`) -- all single-byte
ASCII escape and parameter characters, no UTF-8 anywhere in the emitted
byte stream. A tree-wide search for `utf`/`UTF`/`wchar`/`multibyte`
across `sys/kern/tty*.c` and the rest of `sys/` (excluding driver files
for unrelated hardware, e.g. `pic32/dev/gpanel.c`, `stm32*.h`) returns
nothing: the tty layer has no multibyte awareness at any layer.

### Where UTF-8 would break

`kilo` is the concrete case. Every column computation in
`usr.bin/kilo/kilo.c` is `strlen`/byte-indexed: `row->rsize` is
documented in-source as "one byte per rendered character," `E.cx`/`E.cy`
are "cursor x and y position in characters," and the render path
(`row->render[idx++] = row->chars[j]`) copies bytes one for one with no
decode step. A UTF-8 continuation byte would count as a full column in
every one of these computations, desynchronizing the cursor from the
terminal's actual rendered column on the very first multibyte character
-- not a cosmetic glitch, a wrong-position bug in the editor's own
cursor math, and the same byte-counted pattern would recur in any new
tool (`md` included) written against this tree's existing conventions
without a UTF-8-aware column function, which does not exist anywhere in
this codebase to reuse.

### The recommendation

ANSI SGR color plus 7-bit ASCII (and ASCII box-drawing -- `-`, `|`, `+`
-- rather than the Unicode box-drawing block) is what `kilo` already
proves works over this exact serial path to both xterm.js and real
xterm, and it costs nothing new: `md`'s design in section 1 reuses
`kilo`'s own SGR vocabulary rather than inventing one. Unicode box art
or wide glyphs would require a decode-and-column-width layer (at minimum
a UTF-8 sequence length table and a wcwidth-equivalent) added to every
tool that positions a cursor or wraps text, starting with fixing
`kilo`'s byte-counted `rsize`/`cx` math, before a single such glyph could
render at the right column reliably. Nothing in this session's
investigation found existing wcwidth, UTF-8 decode, or locale-aware
column logic anywhere in `sys/` or the userland tree to build on --
this is not a small gap to close.

Recommend ANSI SGR plus 7-bit ASCII now, unconditionally, for `md` and
every other tool in this document. Gate Unicode behind a capability
check later (a terminal that announces UTF-8 support, or a build-time
option) only once a column-width layer exists to make the gate
meaningful -- introducing raw UTF-8 bytes into a byte-column codebase
today would silently corrupt cursor positioning the same way it would
in `kilo`.
