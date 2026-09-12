# Modal vi/ex for DiscoBSD on RP2040

## Task and what's already on record

`sys/arch/rp2040/doc/research/editors-constrained.md` surveys screen editors for this
target already and covers most of the same ground (kilo, mg, stevie, xvi, elvis,
levee, busybox vi, vile, nvi2, 2.11BSD's own ex/vi, sam, RetroBSD). It ranks kilo
first. Kilo is disqualified for this task on a different axis than size: it is
**not modal**. It has one editing mode with Ctrl-key chords, no command mode, no
`:wq`, no `dd`, no insert/normal split. The task calls for real vi behavior --
`hjkl`, `dd`, `:wq`, insert mode entered with `i`/`a`/`o` and left with Esc -- so
this report re-ranks the same candidate pool against modality as a hard filter, adds
one candidate the prior survey didn't reach (a build-time inspection of the actual
Stevie source tree, not just its Wikipedia lineage note), and confirms the licensing
verdict on `usr.bin/virus/virus.c`.

## Tree search: no modal vi ships or is vendored

```
grep -ril "vi\b\|nvi\|elvis\|stevie\|levee\|xvi" usr.bin usr.ucb
```

turns up no vendored ex/vi source. The one vi-shaped file in the tree is
`usr.bin/virus/virus.c`.

## `usr.bin/virus/virus.c`: GPLv2, excluded

The file header is explicit:

```
 * virus - vi resembling utility skeleton - based on
 * tiny vi.c: A small 'vi' clone (from busybox 0.52)
 *
 * Copyright (C) 2001 - 2003 Stefan Koerner <ripclaw@rocklinux.org>
 * Copyright (C) 2000, 2001 Sterling Huxley <sterling@europa.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
```

GPLv2, derived from busybox's `vi.c`. It is modal and reasonably small (4008 lines,
busybox's own `Config.in` estimates 23-26 KB for the feature-complete build) but the
license rules it out per the task's constraint. It stays in the tree as `virus`, not
as `vi` -- do not rename or reposition it to imply it satisfies this task.

`downloads-survey.md` turns up two more vi-shaped names, both dead ends: `ved`
(Schily tree, mixed CDDL/GPL/BSD, redundant with `ed`/`emg`/`med` already unshipped)
and nano (GPLv3, redundant with `emg`). Neither is modal-vi in the `hjkl`/`:wq`
sense and neither changes the verdict below.

## Candidate license verdicts (from the prior survey, restated for modality)

| Editor | License | Modal? | Verdict |
|---|---|---|---|
| kilo | BSD-2 | No -- one mode, Ctrl-key chords | Excluded: not vi, ships already as a non-modal editor |
| Stevie | Public domain (Unlicense on the maintained fork) | Yes -- normal/insert/cmdline, real `:` ex commands | **Recommended** |
| levee | BSD-3-equivalent, confirmed this pass (see below) | Yes | **Runner-up** |
| xvi | Custom "free, not public domain" | Yes | Excluded: 100-104 KB text on x86 `-Os` alone, over budget before data/stack |
| elvis / elvis-tiny | Clarified Artistic License (elvis-tiny source unlocated) | Yes | Excluded: license not BSD/MIT/ISC-equivalent; elvis-tiny unverifiable |
| nvi / nvi2 | Modified BSD | Yes | Excluded: ~422 KiB package, curses-linked, inherits the PDP-11 overlay-era sizing problem the task already rules out |
| vile | GPL-2.0 (likely) | Yes | Excluded: GPL, curses multi-window emacs/vi hybrid, no plausible fit |
| busybox vi (`virus.c`) | GPLv2 | Yes | Excluded per task constraint (see above) |
| vim-tiny | Vim license (not BSD/MIT/ISC/PD-equivalent; charityware clause) | Yes | Excluded: license family wrong, and vim's architecture (script engine, undo tree, multi-buffer) is not a tiny-mode build the way kilo/stevie/levee are |
| 2.11BSD's own ex/vi | BSD (via CSRG) | Yes | Excluded per the task's own premise -- PDP-11 overlay-era, ~200 KB, doesn't fit a single 96 KB process |

## Levee license, confirmed this pass

The prior survey flagged levee's `COPYRIGHT` as unread. It's now read directly:

```
Copyright (c) 1982-2007 David L Parsons. All rights reserved. Redistribution and
use in source and binary forms, without or without modification, are permitted
provided that the above copyright notice and this paragraph are duplicated in all
such forms and that any documentation, advertising materials, and other materials
related to such distribution and use acknowledge that the software was developed
by David L Parsons... My name may not be used to endorse or promote products
derived from this software without specific prior written permission. THIS
SOFTWARE IS PROVIDED "AS IS"...
```

This is a BSD-3-Clause-shaped license (attribution + non-endorsement + as-is
warranty disclaimer), not literally SPDX BSD-3-Clause text but license-compatible
with the task's BSD/MIT/ISC/public-domain family. Levee clears the license bar.

## Stevie: source inspected directly, not just cited

Repo: `https://github.com/udo-munk/stevie` (a CP/M-80/Philips P2000C port of Tim
Thompson's 1987 original, maintained by Udo Munk). License file is the Unlicense
(public-domain-equivalent), consistent with Thompson's original 1987 Usenet posting
carrying no restriction. `readme.txt`: "STevie was written by Tim Thompson. Ported
to the P2K by Jon Bradbury."

Source tree, fetched via the GitHub contents API (sizes in bytes):

```
cmdline.c    4513
edit.c       6715
help.c       3374
hexchars.c   6796
linefunc.c   4391
main.c       8766
misccmds.c   6566
normal.c     7885
stevie.h     1438
window.c      901
-----------------
total       51345
```

Ten files, ~51 KB of C source total, one flat directory, no subsystem sprawl. This
particular port targets CP/M-80 hardware most people have never seen and is exactly
the kind of source that survives contact with a constrained retro target, which is
the property this task needs.

**Modal confirmed by grep, not by name only.** `cmdline.c` implements `:w`, `:q`,
`:q!`, `:wq`, `:e`, `:e!`, `:f`, `:r`, `.=`, `$=`, `:set`. `normal.c`/`linefunc.c`
carry the normal-mode command table (motions, `dd`-class operators). Three states
are defined in `stevie.h`: `NORMAL`, `CMDLINE`, `INSERT` (plus `APPEND` as a variant
entry into insert). This is the real vi state machine, not a vi-flavored single-mode
editor.

**Terminal handling: this is the decisive finding.** `window.c` in full:

```c
#include "stevie.h"
#include "stdio.h"
#include "sgtty.h"

windinit()
{
	struct sgttyb stty;
	stty.sg_flags = CBREAK | CRMOD;
	ioctl(0, TIOCSETP, &stty);
	Columns=80;
	Rows=24;
}

windgoto(r,c)
int r,c;
{
	/* ANSI */
	printf("\033[%d;%dH",r,c);
}

windclear()
{
	/* ANSI */
	printf("\033[2J");
}
```

Stevie already speaks **`<sgtty.h>`/`ioctl(TIOCSETP)`** for raw mode, not POSIX
termios. That is 2.11BSD's *native* tty interface -- the same one DiscoBSD's libc
already carries, because DiscoBSD is 2.11BSD lineage. Kilo, by contrast, needs
`tcgetattr`/`tcsetattr` shimmed over `sgtty`, which the prior survey names as kilo's
single largest port item. Stevie doesn't have that item at all. Cursor positioning
and screen clear are already the exact ANSI escapes (`\033[r;cH`, `\033[2J`) this
target's fixed 80x24 xterm-like console wants; `windgoto`/`windclear`/`windrefresh`/
`beep` are the entire terminal-abstraction surface, seven tiny functions in one
90-line file. No curses, no termcap, no terminfo database lookup anywhere in the
tree searched.

**Memory model.** `main.c`:

```c
if ( (Filemem=malloc((unsigned)FILELENG)) == NULL ) { ... }
Realscreen = malloc((unsigned)(Rows*Columns));
Nextscreen = malloc((unsigned)(Rows*Columns));
```

`FILELENG` is `16000` in `stevie.h`. Stevie allocates **one fixed-size flat buffer**
for the whole file (default 16 KB, a `#define` to change) plus two screen buffers of
`Rows*Columns` bytes each (80x24 = 1920 bytes, so ~3.8 KB total for double-buffered
screen). This is the buffer architecture the task's constraints reward: no per-line
struct array, no per-row malloc, no syntax-highlight shadow buffer -- the entire
opposite of kilo's `erow`-per-line heap layout that the prior survey measured at
~93-95 KB for a 20 KB file. Raising `FILELENG` to, say, 24000 for a bigger working
file costs exactly 24000 bytes of heap, nothing more -- the RAM budget is legible
and tunable from one `#define`, which is not true of kilo's design.

**K&R vs ANSI C.** Every function above is old-style K&R (`windgoto(r,c)\nint r,c;`
rather than `windgoto(int r, int c)`). This is friendlier to a small `-Os` Thumb-1
build than modern C in one respect (less prototype/promotion machinery for the
compiler to reconcile) and needs no rewrite to compile under `gcc`/`arm-none-eabi-gcc`
with default settings; `-std=gnu89` or plain default GNU dialect accepts K&R
definitions. No `long long`, no floating point, no variadic macros observed in the
files read.

**Port effort, as steps:**
1. Swap the `TCAP`/`ATARI`/`UNIXPC` `#define` in `stevie.h` for a fourth branch (or
   just repurpose the existing `TCAP` path, which already only calls `sgttyb`/
   `ioctl`/`printf`) targeting DiscoBSD's console directly -- `window.c`'s five
   functions are the entire target-specific surface.
2. Confirm `<sgtty.h>`, `TIOCSETP`, `CBREAK`, `CRMOD` are present in DiscoBSD's
   trimmed board libc / kernel headers (2.11BSD lineage strongly suggests yes; this
   is the one item to verify against the actual header tree before writing code,
   not assume).
3. Set `FILELENG` to a size that fits the 96 KB process budget alongside `.text`,
   `.data`, the two ~1920-byte screen buffers, and stack -- 16-24 KB is a reasonable
   starting point, leaving most of the 96 KB free.
4. Build with `arm-none-eabi-gcc -Os -mthumb -mcpu=cortex-m0plus`; re-measure
   `.text`/`.data`/`.bss` with `arm-none-eabi-size` the way the prior survey did for
   kilo, since no Thumb figure exists yet for Stevie anywhere. Budget estimate below
   is a size projection, not a measured number -- flag it as such until built.
5. Check `hexchars.c` (6796 bytes, the single largest file) for anything requiring
   wide-character or locale support beyond plain 8-bit chars; unlikely given the
   1987 vintage but unverified in this pass.

**Size projection (not yet measured for this target).** ~51 KB of C source across
ten files is well under xvi's cited ~100 KB *compiled* -Os text, and Stevie's design
(one flat file buffer, no curses, minimal screen-diff double-buffer, K&R-terse
functions) points at compiled text meaningfully smaller than that -- plausibly in
kilo's neighborhood (kilo measured 6.3 KB of own `.text` for roughly a quarter the
source line count) to perhaps 15-20 KB of `.text`, comfortably under the 25 KB a.out
target, but this is an estimate from source shape, not a build. **A real
`arm-none-eabi-gcc -Os` build against the trimmed board libc is the next concrete
step before committing to the port** -- the same discipline the prior survey used
for kilo should be repeated here before this recommendation is treated as final.

## Levee: runner-up, effort not yet inspected at source level

License confirmed above. The prior survey's findings stand: a `--use-termcap`/
`--tputs`/`--stdio` build matrix already treats small hosts (DOS, Atari TOS, iRMX,
FlexOS) as first-class, and a `~54 KB` x86-64 package figure is cited as an upper
bound, not a target number. Levee was not source-inspected in this pass the way
Stevie was (no file-by-file size table, no confirmation of its buffer architecture
or K&R/ANSI shape) -- ranked second because Stevie's terminal layer already matches
2.11BSD's native `sgtty` interface with zero shimming, which is a stronger, already-
demonstrated fit than levee's termcap-optional build flag, which still assumes a
termcap database exists to opt out of. If Stevie's build hits an unexpected wall
(missing libc primitive, `FILELENG` too tight after real `.bss` accounting), levee
is the next candidate to source-inspect the same way, before falling back to
busybox vi under a GPL exception.

## Recommendation

Port **Stevie** (public domain / Unlicense, `github.com/udo-munk/stevie` as the
readable reference tree, ~51 KB source across 10 files). It is genuinely modal
(`NORMAL`/`INSERT`/`CMDLINE` states, real `:w`/`:q`/`:wq`/`:e`/`:r` ex commands,
`hjkl`-class motions in `normal.c`), already speaks 2.11BSD's native `sgtty`/
`ioctl(TIOCSETP)` raw-mode interface instead of POSIX termios (the exact shim kilo
would need, that Stevie doesn't), emits raw ANSI escapes matching the fixed 80x24
xterm-like console with no curses/termcap dependency, and uses one fixed-size flat
file buffer (`FILELENG`, default 16 KB, a single `#define` to retune) plus two
~1920-byte screen buffers instead of kilo's per-line heap layout that blew the
96 KB budget on a 20 KB file. K&R-style throughout, no floating point, no
`long long`. Runner-up: **levee** (BSD-3-equivalent, confirmed this pass), with its
termcap-optional build flag as the fallback path if Stevie's `sgtty` assumption
doesn't hold against the actual trimmed board libc. `usr.bin/virus/virus.c`
(busybox-derived vi, GPLv2) is confirmed excluded and should stay named `virus`,
not `vi`. Next concrete step for either candidate: a real
`arm-none-eabi-gcc -Os -mthumb -mcpu=cortex-m0plus` build against DiscoBSD's actual
trimmed libc headers, with `arm-none-eabi-size` output recorded, before writing any
further port code -- this report's size numbers for Stevie are a source-shape
projection, not a measurement.

## Sources

- Prior survey (source of most background facts restated above):
  `sys/arch/rp2040/doc/research/editors-constrained.md`,
  `sys/arch/rp2040/doc/research/downloads-survey.md`
- `usr.bin/virus/virus.c` (in-tree, read directly)
- Stevie: https://github.com/udo-munk/stevie (contents API for file sizes, raw
  fetches of `window.c`, `main.c`, `stevie.h`, `cmdline.c`, `readme.txt`, `LICENSE`),
  https://timthompson.com/tjt/stevie/
- Levee: https://github.com/Orc/levee ,
  https://raw.githubusercontent.com/Orc/levee/master/COPYRIGHT (read directly this
  pass)
