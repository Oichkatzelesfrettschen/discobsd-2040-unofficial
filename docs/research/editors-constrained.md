# Screen editors for DiscoBSD on RP2040: a constrained survey

## Target constraints, restated

DiscoBSD (2.11BSD-derived) on Raspberry Pi Pico RP2040, Cortex-M0+ (ARMv6-M, Thumb-1),
no MMU. A process may use at most 96 KB total for text+data+stack. Binaries are a.out,
built with `arm-none-eabi-gcc -Os` and converted from ELF32. libc is 2.11BSD's, with
partial POSIX coverage; `libcurses`/`libtermlib` (2.11BSD curses, termcap-based,
`/etc/termcap`) are present; there is no dynamic linking. Root filesystem: 840 KB total,
about 340 KB free. Console: USB CDC-ACM, 80x24, xterm-compatible. The tree ships `ed`
and 2.11BSD's `more`; no `vi`. 2.11BSD's own vi/ex needed PDP-11 overlays and is
reported at roughly 200 KB of text -- too big for a single 96 KB process, and this
report could not independently re-confirm the exact figure from a primary 2.11BSD
source in the time available (flagged below as not verified, but consistent with the
well-documented PDP-11 overlay requirement for ex/vi 3.7, whose text exceeded a single
64 KB instruction space on that machine).

Two facts change the ranking math from a generic "smallest editor wins" survey:

1. **96 KB is text+data+stack for the whole process, and the edit buffer is usually the
   larger term**, not the binary. A candidate's *buffer architecture* (whole file in
   core vs. paged/temp-file) matters as much as its compiled size.
2. **The console is fixed and xterm-compatible.** An editor that hardcodes VT100/ANSI
   escapes, normally a portability defect, is a *feature* here: it can skip
   `libcurses`/`libtermlib` entirely and pay no curses-linkage tax in text size.

## Method and what was and wasn't measured

Sizes and licenses below come from WebSearch/WebFetch against upstream repos, project
pages, and Debian/embedded packaging notes (each cited by URL). One editor, kilo, was
compiled directly for this report:

```
arm-none-eabi-gcc -Os -mthumb -mcpu=cortex-m0plus -std=gnu11 -c kilo.c
```

against a header shim standing in for 2.11BSD's `<sys/termios.h>`/`<sys/ioctl.h>`
(the actual target libc is not available in this sandbox, so the shim declares the
POSIX-shaped signatures kilo calls -- `tcgetattr`, `tcsetattr`, `ioctl`, `getline` --
without providing bodies). This measures the *application code's own* `.text`, not the
libc it calls into; `arm-none-eabi-size` reported:

```
   text    data    bss    dec    hex  filename
   6298     376    200   6874   1ada  kilo.o
```

That is real, this-session-measured, Thumb-1 `-Os` output for kilo.c's own logic:
6.3 KB text. No other candidate in this list was locally re-built for Thumb; their
sizes below are the best cited numbers found, mostly from x86/Linux or DOS/CP/M builds,
and are marked accordingly. Where no size claim exists anywhere, the row says
"not found" rather than guessing.

The RAM side needed its own arithmetic, since no source gives a Thumb heap figure for
a 20 KB file. Worked below for kilo, the only candidate whose buffer struct was
available to inspect directly.

## Per-editor findings

### kilo (antirez) and descendants -- BSD-2

- Repo: https://github.com/antirez/kilo -- License: BSD-2-Clause, `LICENSE` file:
  https://github.com/antirez/kilo/blob/master/LICENSE
- Own `.text`, Thumb `-Os`, measured this session: 6298 bytes (see above). This
  excludes libc (`stdio`, `malloc`, `getline`, `tcgetattr`/`tcsetattr`), which any
  candidate pays for regardless.
- Source: 1308 physical lines in one file, `kilo.c` (author's stated design goal is
  "less than 1 KLOC" per `cloc`): https://github.com/antirez/kilo/blob/master/kilo.c
- Terminal handling: raw VT100 escape sequences it emits itself -- "Kilo does not
  depend on any library (not even curses)," per the project README (same repo). This
  matches the fixed xterm-compatible console exactly; no curses/termcap link needed.
- libc calls beyond a minimal C89 set: `tcgetattr`/`tcsetattr` (POSIX termios,
  **not** 2.11BSD's native `sgtty`/`TIOCSETP` interface -- this is the single largest
  port item), `getline` (a GNU/POSIX.1-2008 extension 2.11BSD's libc almost certainly
  lacks), `atexit`, variadic `snprintf`-family formatting for the status line.
- Maintenance: last push 2025-01-04, per GitHub API -- alive but essentially frozen
  (it is intentionally "finished" software).
- No published RetroBSD/2.11BSD/PDP-11 port found.
- **RAM arithmetic for a 20 KB / ~500-line file** (kilo keeps the whole buffer resident
  as an array of `erow` structs, each with three separate heap allocations --
  `chars`, a rendered/tab-expanded copy `render`, and a syntax-highlight byte array
  `hl`, one byte per rendered character):
  - `chars` total: ~20 KB (the file itself)
  - `render` total: ~22.5 KB (tab expansion, rough)
  - `hl` total: ~20 KB (1 byte per rendered char)
  - `erow` struct overhead: ~500 rows x ~40 bytes (7 fields, word-padded) = ~20 KB
  - malloc header overhead: 3 allocations/row x 500 rows x ~8 bytes = ~12 KB
  - **Total: ~93-95 KB of heap alone**, before the ~6.3 KB text, the process's data
    segment, or any stack. **This does not fit in 96 KB.** Dropping the `hl` array
    (no syntax highlighting -- irrelevant on this console anyway) removes ~20 KB;
    reducing to two allocations per row instead of three removes another ~4 KB of
    malloc overhead; shrinking or eliminating the separate `render` cache (render tabs
    on the fly instead of caching) removes another ~20 KB. A stripped kilo -- no
    highlighting, no render cache, minimal `erow` -- lands in the 45-55 KB heap range
    for a 20 KB file, which fits with room for stack and the binary's own footprint.
    **This is a real, non-trivial rewrite of the buffer layer, not a drop-in port.**
- Self-hosting: single-file, C89-shaped (no `long long`, no floating point, straight
  structs and pointers) other than the POSIX calls noted above -- plausible for a
  later on-device PCC-class compiler once termios/getline shims exist. DiscoBSD is not
  confirmed here to carry an on-device C compiler today; this is a statement about the
  source's portability, not a claim about the current tree.
- **Port effort, as steps:**
  1. Add or shim `tcgetattr`/`tcsetattr` over 2.11BSD's native `sgtty` ioctl set
     (`TIOCGETP`/`TIOCSETP`), or confirm 2.11BSD's libc already carries a termios
     compatibility shim -- check before writing one.
  2. Provide `getline` (trivial to implement over `fgets`/realloc if missing).
  3. Strip the `hl` (syntax-highlight) array and its `editorUpdateSyntax` machinery
     entirely -- dead weight on a plain terminal and the single largest RAM item.
  4. Fold `render` into on-demand tab expansion at draw time instead of caching a
     second copy per row, or cap it only for very long lines.
  5. Re-measure heap for a 20 KB file against the trimmed `erow`; re-measure `.text`
     with the target's actual libc linked in (this session's 6.3 KB excludes libc).
  6. Verify `SIGWINCH`/`ioctl(TIOCGWINSZ)` behavior against the CDC-ACM console
     (fixed 80x24, so this can likely be hardcoded and the ioctl dropped).

### mg (public-domain MicroEMACS descendant)

- Repo (actively maintained fork used for packaging): https://github.com/ibara/mg --
  License: public-domain core with ISC/BSD-licensed portability shims per file, per the
  project README: https://github.com/ibara/mg#readme (GitHub's license-detector
  returns no single SPDX tag, consistent with a per-file mixed PD/ISC/BSD codebase --
  the report's original assumption of uniform "public domain" is not accurate and
  should be checked file-by-file, not asserted).
- Ships in NetBSD base and pkgsrc: https://ftp.netbsd.org/pub/pkgsrc/current/pkgsrc/editors/mg2a/index.html
- Terminal handling: **links ncurses by default**; a `--with-builtin-curses` build
  option substitutes a simplified NetBSD-derived curses for systems without ncurses --
  it is not a termcap-only, curses-free design. Source: `ibara/mg` README (above).
  On this target that means either linking against 2.11BSD's own `libcurses` (present,
  but adds text weight for a full curses implementation) or building mg's bundled
  minimal curses substitute, itself nontrivial.
- No binary-size or Thumb figure found anywhere; not measured. No LOC count obtained.
- Maintenance: last push 2024-06-04 -- actively maintained.
- No RetroBSD/2.11BSD/PDP-11 port found.
- **Verdict: unranked candidate.** Its curses dependency and unmeasured size make it a
  weaker bet than kilo or the vi-family candidates below without a build attempt;
  included for completeness, not shortlisted.

### stevie

- Repo used for research (a CP/M-80 retro-port, not the 1987 original):
  https://github.com/udo-munk/stevie -- License: Unlicense (public domain equivalent),
  per GitHub API license field.
- Terminal handling: raw/custom escape output; the port target implements its own
  `windgoto()` screen-positioning routine rather than linking curses/termcap --
  architecturally close to kilo's approach. Source: repo README.
- No binary size or LOC figure found for this or the original source; not measured.
- Historical significance: Stevie's Amiga-era source became the direct ancestor of
  Vim: https://en.wikipedia.org/wiki/Stevie_(text_editor) -- the lineage confirms the
  design was once small enough for 1980s micros, but no current, buildable,
  well-maintained trunk was located. Treat as archaeologically interesting, not a
  practical candidate without a size measurement and an active upstream.
- **Verdict: not ranked** -- insufficient current data (no size, unclear canonical
  source tree, essentially unmaintained).

### xvi

- Home: https://martinwguy.github.io/xvi/ ; repo: https://github.com/martinwguy/xvi.
  License: project page cites a custom "free, but not public domain" COPYING file;
  GitHub's automatic license detector returns no SPDX match. **Not BSD/MIT-equivalent
  by default -- read `COPYING` directly before relying on it**; not confirmed in this
  pass.
- Size: **the only candidate here other than kilo with a directly stated `-Os` figure**
  -- the project page states 104,544 bytes of text with `gcc -Os` and 100,406 bytes
  with `clang -Os`, on x86, "smallest full-function vi clone." Source:
  https://martinwguy.github.io/xvi/
- At ~100-104 KB of text alone on x86 (Thumb would likely differ, untested), **this
  already exceeds the entire 96 KB per-process budget before any data, stack, or
  buffer is counted.** Ruled out on this number alone, consistent with the report's
  instruction to rule out anything that plainly doesn't fit.
- Terminal handling: repo contains a termcap screen driver (`tcap_scr.c`):
  https://github.com/martinwguy/xvi/blob/master/src/tcap_scr.c -- so it can use
  2.11BSD's termcap, but that doesn't rescue the text-size problem.
- Last release 2017-06-07 (v2.50.3); repo last push 2021-03-31 -- lightly maintained.
- **Verdict: ruled out.** 100+ KB text > 96 KB total process budget.

### elvis / elvis-tiny

- Canonical repo: https://github.com/kirkendall/elvis. License: Clarified Artistic
  License (ClArtistic): https://spdx.org/licenses/ClArtistic.html -- **not** one of
  the preferred BSD/MIT/ISC/0BSD family; it is a permissive-but-distinct license with
  its own attribution and derivative-naming clauses. Note this explicitly rather than
  grouping it with BSD.
- Last push 2013-07-29 -- dead upstream; an active fork exists at
  https://github.com/mbert/elvis.
- Terminal handling: multiple GUI/terminal backends including a termcap driver
  (`guitcap.c`), alongside X11/Win32/OS2/MS-DOS backends -- the full elvis is a
  multi-frontend program, most of which is irrelevant weight on this target.
- **elvis-tiny**, a stripped variant, is referenced only secondhand via T2 Linux
  packaging notes (https://t2linux.com/packages/elvis-tiny, "a shrinked elvis (vi
  clone)") with no upstream source tree, size, LOC, or license independently located
  in this pass. Its buffer architecture (the property that would make it interesting
  here -- elvis's design historically uses a temp-file-backed buffer, not
  whole-file-in-core, which is the right shape for a 96 KB ceiling) could not be
  confirmed against a primary source. **Flagged, not ranked: promising on paper
  (temp-file buffering matches this target's constraint pattern the way the original
  PDP-11 ex/vi's overlay design did), but unverifiable without finding elvis-tiny's
  actual upstream.**
- **Verdict: full elvis not ranked (unclear size, non-preferred license, dead
  upstream); elvis-tiny needs a dedicated follow-up to even evaluate.**

### levee

- Repo: https://github.com/Orc/levee (maintainer David L. Parsons); COPYRIGHT file
  present in-repo but its exact terms were not independently confirmed in this pass --
  GitHub's detector returns "Other," not a recognized SPDX license. **Do not assume
  public domain or BSD without reading COPYRIGHT directly.**
- Size: stated directly on the project page as "tiny (~54k on osx/x86_64; smaller on
  32-bit machines)." Source: https://www.pell.portland.or.us/~orc/Code/levee/. This is
  a full binary/package size on a 64-bit host, not a Thumb `.text` figure, and not
  independently re-measured here.
- Terminal handling: supports both curses and a `--use-termcap` build flag that links
  termcap instead, plus a `--tputs` pacing option and a `--stdio` buffered-I/O mode --
  the most terminal-backend-flexible candidate found, and termcap-only builds avoid
  the curses text tax.
- Build flags exist for DOS, Atari TOS, iRMX, and FlexOS targets, showing the codebase
  already assumes small/embedded/non-Unix hosts as first-class targets -- a good sign
  for portability, though none of those targets is a BSD/Unix a.out system and none is
  PDP-11/RetroBSD/2.11BSD specifically.
- Last push 2022-11-15 -- sporadically maintained.
- **Verdict: promising but license-unverified.** Its termcap-flexible build and
  "smaller on 32-bit" claim make it worth a real build attempt, but the license must
  be read from `COPYRIGHT` before committing effort, and no 32-bit or Thumb-comparable
  size number exists yet -- treat the ~54 KB x86-64 figure as an upper bound, not
  a target number.

### toybox vi

- Source: `toys/pending/vi.c` in https://github.com/landley/toybox -- note it lives in
  `pending/`, toybox's own designation for code not yet promoted to the built,
  production command set. Treat as immature/unstable, not "the toybox vi."
- License: 0BSD, confirmed at https://landley.net/toybox/license.html (toybox's
  project-wide license).
- No compiled size, LOC count, or specific terminal-handling behavior for `vi.c` was
  independently confirmed in this pass (general toybox convention favors raw
  termios/ANSI escapes over curses, but this was not verified against `vi.c` itself).
- No embedded/retro port found.
- **Verdict: not ranked.** 0BSD is the most permissive license on this whole list, but
  "unfinished, no size data, unverified terminal handling" rules it out pending a
  direct build attempt against `pending/vi.c` specifically.

### busybox vi -- GPLv2, size-reference only, as instructed

- Source: `editors/vi.c`, https://git.busybox.net/busybox (mirror:
  https://github.com/mirror/busybox/blob/master/editors/vi.c).
- License: GPLv2 (or later), per file headers -- **not** in the BSD-compatible
  preference group; flagged separately per the task's instruction, not scored against
  the BSD-preferred candidates.
- Size: busybox's own `Config.in` help text estimates an incremental binary-size cost
  of "vi (26 kb)" for a fuller-featured build and "vi (23 kb)" in an earlier revision
  (cited via https://embeddedrelated.com/showarticle/1639.php) -- these are busybox's
  internal delta-size estimates from a multi-applet Linux/x86 or ARM32 build, not a
  standalone Thumb a.out measurement, and were not independently re-measured here.
- Terminal handling: raw termios/direct escape sequences, no curses -- consistent with
  busybox's general no-library-dependency design.
- `FEATURE_VI_*` Kconfig options allow trimming regex search, colon-command support,
  and `:set` options individually, which is the right shape for further shrinking.
- Actively maintained (busybox.net).
- **Note for the record, not a ranking entry:** if this project ever tolerates a GPL
  component, busybox vi's ~23-26 KB estimate (unverified on this target) and
  termios-only design make it worth a real build; it is excluded from the ranked
  shortlist solely because the task calls for BSD-compatible licensing as the
  preference and busybox vi is GPLv2.

### vile -- GPL, oversized by design

- Home: https://invisible-island.net/vile/, https://github.com/ThomasDickey/vile-snapshots.
- License: GPL-2.0-only per a secondary source (Wikipedia's Vile entry); a primary
  `COPYING` file was not loaded in this pass -- flagged as likely-GPL2, not fully
  confirmed.
- Actively maintained by Thomas Dickey; current stable 9.8zb.
- Architecturally an emacs/vi hybrid with multi-buffer, multi-window support, built on
  curses -- far more feature surface than this target needs or can afford. No size
  figure was obtained, but the feature set alone (multi-window curses UI, emacs-style
  extensibility) makes it implausible to fit 96 KB; no counter-evidence found.
- **Verdict: ruled out** on architecture and license, without needing a precise size
  number -- the feature scope itself is incompatible with a single 96 KB process.

### nvi / nvi2 -- confirms the original vi/ex is too big, everywhere

- Repo (active fork): https://github.com/lichray/nvi2.
- License: modified BSD, per NetBSD pkgsrc: https://ftp.jaist.ac.jp/pub/pkgsrc/current/pkgsrc/editors/nvi2/index.html
  -- license itself is fine (BSD-preferred group), but size rules it out.
- Curses dependency: confirmed -- the repo's `cl/` directory is documented as "Vi
  interface to the curses(3) library":
  https://github.com/lichray/nvi2 (top-level directory listing).
- Size: FreeBSD/pkgsrc binary package size listed at 422 KiB:
  https://freebsdsoftware.org/editors/nvi2.html -- this is installed package size, not
  a stripped `.text` figure, so it overstates the true binary by some margin, but even
  a generous 3-4x reduction leaves it well over 96 KB, and its historical PDP-11
  ancestor (2.11BSD's own overlaid vi/ex) is the exact program the task already rules
  out.
- Actively maintained: v2.2.2, 2025-10-08.
- **Verdict: ruled out with a number.** A ~400 KB package is nowhere close to a
  96 KB process ceiling; nvi/nvi2 does not rescue the original vi/ex sizing problem,
  it inherits it.

### 2.11BSD's own ex/vi

- The ~200 KB figure supplied in the task is consistent with vi/ex 3.7's documented
  need for PDP-11 overlays (multiple overlaid segments because the program exceeded a
  single 64 KB instruction/data space on that machine) but could not be
  independently re-confirmed from a primary 2.11BSD source (`usr.bin/ex` Makefile or a
  direct `size` output) in the time available via web search alone --
  **flagged as plausible but not independently verified here.** Treat the figure as
  the task's own stated fact rather than this report's confirmed measurement; a
  from-source `size` run against the actual 2.11BSD tree would settle it directly and
  should be preferred over any web citation.
- **Verdict: ruled out per the task's own premise** (this is the editor the task
  already excludes; carried here only so the ranking below has an explicit baseline
  to beat).

### sam / structural editors

- Sam (Rob Pike, Plan 9/Unix): https://sam.cat-v.org/, design paper
  https://research.swtch.com/sam.pdf. It is a GUI, mouse-driven, multi-file
  structural-regex editor built against a display server (originally Blit/Plan 9
  windowing, later X11). There is no headless-terminal, no-mouse, memory-constrained
  variant documented anywhere found. **Confirmed inapplicable** to a USB-CDC serial
  console with no pointing device or window system -- excluded on architecture, not
  size.

### RetroBSD

- No RetroBSD-specific screen-editor port (vi, nano, or otherwise) was found across
  https://github.com/RetroBSD/retrobsd or its wiki. RetroBSD (PIC32, 128 KB RAM) most
  plausibly relies on `ed` for the identical reason this task's DiscoBSD port does --
  no evidence a screen editor was ever fitted to that target either. This is a
  negative result, stated as such rather than inferred as a port precedent.

## What could be self-hosted later, once an on-device compiler exists

This is a property of each source tree (C89-shaped, single-file, no exotic types),
not a claim that DiscoBSD currently carries an on-device compiler -- nothing in the
locally available repository tree confirms that today.

| Editor | Self-host plausibility | Why |
|---|---|---|
| kilo | Good | Single C89-shaped file, no `long long`/float, straightforward structs |
| busybox vi | Poor as shipped | Depends on busybox's shared `libbb` infrastructure and multi-applet build system, not a standalone translation unit |
| toybox vi | Poor as shipped | Depends on toybox's own lib layer and build machinery, same problem as busybox |
| levee | Unclear | Multiple backend-specific files; not evaluated for K&R/C89 compatibility here |
| elvis-tiny | Unclear | Upstream source not located to inspect |
| vile, nvi2 | Poor | Both are large, modern, multi-file C with curses coupling -- self-hosting is moot since neither fits the 96 KB ceiling regardless |

## Ranked shortlist (at most three)

1. **kilo (BSD-2)** -- the only candidate with a real, measured Thumb `-Os` number
   (6298 bytes of application text this session) and a console-matching design
   (raw ANSI escapes, no curses tax). Its stock RAM use for a 20 KB file (~93-95 KB
   of heap alone, by the arithmetic above) does not fit as shipped, but the fix is a
   scoped, well-understood trim -- drop syntax highlighting, drop the cached
   tab-expanded copy, shrink `erow` -- not a redesign. Port effort: shim
   `tcgetattr`/`tcsetattr` over 2.11BSD `sgtty`, add `getline`, strip `hl` and
   `render` caching, re-measure. Ranked first because it is the only entry backed by
   both a real compiled number and a fully worked RAM estimate, and the path to
   closing the gap is short and concrete.

2. **levee (license unverified, needs confirmation before adoption)** -- the only
   other candidate with an explicit termcap-only build path (`--use-termcap`) and a
   stated size in the tens of KB on a 64-bit host, plus a build system that already
   treats small/embedded targets (DOS, Atari TOS, iRMX) as first-class. Ranked second,
   not first, because its `COPYRIGHT` terms were not read in this pass and its size
   figure is an x86-64 package number, not a comparable measurement. Before any work:
   read `COPYRIGHT` at https://github.com/Orc/levee and confirm it is BSD/MIT/ISC-
   equivalent; if it is not, this candidate drops out of the BSD-preferred list
   entirely (it would still beat busybox vi's GPLv2 only if its terms turn out
   permissive).

3. **busybox vi (GPLv2, listed for completeness per the task's instruction)** -- the
   best-evidenced *small, working, currently maintained* vi-workalike in this whole
   survey (termios-only, no curses, `FEATURE_VI_*` trimming knobs, ~23-26 KB by
   busybox's own unverified internal estimate), but it is GPLv2, which the task asks
   to note separately from the BSD-preferred group rather than rank alongside it. It
   occupies the third slot only if the project is willing to carry one GPL binary in
   an otherwise BSD-licensed base; otherwise the shortlist is effectively kilo and
   levee, with elvis-tiny as an unverified wildcard worth one follow-up search for its
   actual upstream before it can be scored at all.

Ruled out with numbers or architecture, not included above: xvi (100-104 KB text on
x86 `-Os`, already over the 96 KB ceiling before data/stack/buffer), nvi2 (~422 KiB
package, inherits the original overlay-era vi/ex's sizing problem), vile (curses-based
multi-window emacs/vi hybrid, GPL, no plausible fit), sam (GUI/mouse architecture,
inapplicable to a serial console), and 2.11BSD's own ex/vi (the task's own excluded
baseline, ~200 KB, unconfirmed from a primary source in this pass but consistent with
its documented PDP-11 overlay requirement).

## Sources

- kilo: https://github.com/antirez/kilo , https://github.com/antirez/kilo/blob/master/LICENSE , https://github.com/antirez/kilo/blob/master/kilo.c
- mg: https://github.com/ibara/mg , https://ftp.netbsd.org/pub/pkgsrc/current/pkgsrc/editors/mg2a/index.html
- stevie: https://github.com/udo-munk/stevie , https://en.wikipedia.org/wiki/Stevie_(text_editor)
- xvi: https://martinwguy.github.io/xvi/ , https://github.com/martinwguy/xvi , https://github.com/martinwguy/xvi/blob/master/src/tcap_scr.c
- elvis / elvis-tiny: https://github.com/kirkendall/elvis , https://spdx.org/licenses/ClArtistic.html , https://github.com/mbert/elvis , https://t2linux.com/packages/elvis-tiny
- levee: https://github.com/Orc/levee , https://www.pell.portland.or.us/~orc/Code/levee/
- toybox vi: https://github.com/landley/toybox (toys/pending/vi.c) , https://landley.net/toybox/license.html
- busybox vi: https://git.busybox.net/busybox , https://github.com/mirror/busybox/blob/master/editors/vi.c , https://embeddedrelated.com/showarticle/1639.php
- vile: https://invisible-island.net/vile/ , https://github.com/ThomasDickey/vile-snapshots
- nvi2: https://github.com/lichray/nvi2 , https://ftp.jaist.ac.jp/pub/pkgsrc/current/pkgsrc/editors/nvi2/index.html , https://freebsdsoftware.org/editors/nvi2.html
- sam: https://sam.cat-v.org/ , https://research.swtch.com/sam.pdf
- RetroBSD: https://github.com/RetroBSD/retrobsd
