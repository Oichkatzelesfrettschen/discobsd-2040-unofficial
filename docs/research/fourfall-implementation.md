# fourfall implementation record

`games/fourfall` is an original one-player falling-block game for the PicoCalc
console of the DiscoBSD RP2040 port (53 columns by 32 rows, 6x10-pixel cells,
xterm-basic escapes). The author wrote it from the implementer half of a
clean-room behavioral specification and never opened the legacy
`tftetris.c`, the legacy `gpanel` examples, the reviewer half of the
specification, `docs/research/falling-block-clean-room-spec.md`, or any
falling-block source on the network. The game, its screens and this record
avoid the name of the commercial products and their trade dress.

A later presentation redesign (12 by 22 well, glyph textures, plumb line,
two-piece preview; see "Presentation") followed a similarity review and edited
only Part 1 of the specification, the implementer half, in addition to the code
and tests.

## Files

| Path | Role |
| --- | --- |
| `games/fourfall/fourfall.c` | rules (no I/O, no clock) and the terminal front end in one file; `-DFOURFALL_LOGIC` compiles the rules alone |
| `games/fourfall/Makefile` | `all` (target a.out), `host`, `test-logic`, `test-mutants`, `test-play`, `test-play-mutants`, `test`, `clean`, `install` |
| `games/fourfall/.gitignore` | program, host program, logic test binary, objects |
| `games/fourfall/tests/vectors.txt` | the 161 specification vectors, transcribed verbatim |
| `games/fourfall/tests/logic_test.c` | includes `../fourfall.c`; runs every vector, the six property checks and the gravity, lock-timer and queue checks |
| `games/fourfall/tests/mutants.py` | mutation calibration of the rules (32 mutants) and of the front end (`--play`, 27 mutants) |
| `games/fourfall/tests/play.py` | pty tests against a 53x32 terminal model that applies the game's escape sequences |

New files outside `sys/arch/rp2040` carry no copyright line and no license
header; each opens with a comment stating its purpose. `NOTICE` section 1
names keen, fifteen and bubble as ISC. The Makefile takes `PYTHON` from the
caller and sets no default.

## Design

The rules half holds the board (24 rows by 12 columns, rows 0 and 1 hidden, so
the well shows 22 rows),
the seven cell tables, rotation, collision, kicks, lock, line clear, scoring,
levels, the gravity table, the 7-bag, and the gravity and lock timers. Time
enters through `ff_advance(game, ms)`, which steps to each deadline in turn.
The test therefore drives the timers with no clock, and a stepping check
confirms that one long advance equals the same span of 1 ms advances over 200
random games. Only the orientation-0 cell table exists, listed last row first and right to
left within a row; `ff_cells()` applies
the clockwise rule `(r, c) -> (c, n-1-r)` k times, so the 28 shapes follow
from seven rows of data.

The front end draws differentially from a 22x12 shadow of the visible cells,
builds one frame in a 6,144-byte buffer and sends it with one `write()`.
`ob_cell()` is the single place a cell is drawn. A full repaint (start,
Ctrl-L) of the empty board writes 1,543 bytes (1,939 with `-g`); a repaint of a
full stack of 264 settled cells adds 11 bytes a cell, and `ob_put()` flushes
early if a frame ever exceeds the buffer. Time comes from
`gettimeofday()` and waiting from `select()` on descriptor 0 with the time
to the nearest deadline. Keys are read by the game's own `readkey()` because
`gtty_getkey()` blocks after a bare ESC; the shared header stays untouched
and its two unused helpers are silenced for that include only with a
`#pragma GCC diagnostic` pair.

## Decisions where the specification is silent or ambiguous

1. Floor. The floor at row 24 is `=` in every column from 2 to 27, corners
   included, so the walls and the floor form one closed frame with no `+`.
2. O rotation. The specification says the O piece "changes nothing" but KICK-08
   expects `rot=1`. The game updates `rot` and keeps the position.
3. Lock timer start versus restart. A piece that becomes grounded starts the
   timer; that start is not a restart and is not counted. Only a successful
   player move or rotation while the timer already runs restarts it, at most 10
   times per piece. Gravity steps and soft drops are not adjustments. Leaving
   the ground stops the timer and keeps the restart count, which resets at
   spawn.
4. A soft drop leaves the gravity accumulator alone. Spawn, which follows each
   lock, clears gravity and lock state.
5. Lock-out ends the game before any line clear. Top-out and lock-out both set
   the active piece to none, so the final frame draws the board without a
   colliding piece.
6. Esc and F1 map to the same key as `p` inside `readkey()`, so one handler
   covers pause and resume. F2 to F10, other CSI and SS3 sequences, and Alt
   combinations are consumed and ignored.
7. Pause blanks the board and both next-piece boxes, because the previews
   would otherwise give planning information the pause is meant to deny.
   Enter, Space, `p`, F1 and Esc resume.
8. The status line (row 26) refreshes after each lock, at game over, and on a
   full repaint, as specified. The panel values refresh on every change, so
   the status line can show an older score between locks (after soft drops).
9. Start level is clamped to 1..9. An unknown option, or arguments beyond
   `[-cgm] [level [seed]]`, print a usage line and exit 2. The default seed is
   `time(NULL) * 31 + getpid()`.
10. `ff_rnd()` is `(rand() * m) >> 15`. On a host whose `rand()` returns more
    than 15 bits (`RAND_MAX > 0x7fff`) the top 15 bits stand in, so the tests
    on glibc exercise the same bit selection. The target `rand()` already
    returns 15 bits.
11. `GAMEBOX_TEST` empty or holding a character outside `IOTSZJL` selects the
    default `IOTSZJL` cycle; the other games in the tree set it to `1`.
12. Time handling. One loop iteration advances gravity by at most 1,000 ms
    (after a stall, no catch-up burst). When `select()` times out and the clock
    reports less than the requested wait, the game trusts `select()` and
    advances by the requested wait, so a coarse clock cannot spin the loop.
13. `select()` failing with anything other than `EINTR` restores the terminal,
    prints `fourfall: select failed` and exits 1. There is no polling
    fallback; a console that cannot select needs a driver fix, and the board
    check below is `not run`.
14. SIGINT and SIGTERM restore the terminal mode and exit with status 130 and
    143. Normal exit (`q`, or end of input) restores the mode, resets
    attributes and clears the screen.
15. Glyphs and colors. The default cell is a two-character glyph (I `[]`,
    O `##`, T `<>`, S `//`, Z `\\`, J `()`, L `{}`), bold white while falling and
    SGR 90 (bright black) once settled; the previews use the settled style.
    SGR 90 stands in for the dim attribute because the PicoCalc console lists
    the gray palette entries and does not document SGR 2. `-c` prints the
    glyph in the type's foreground hue (I 37, O 34, T 32, S 35, Z 36, J 31,
    L 33; the palette set by the copying review, as foreground colors), bold
    while falling, plain once settled. `-m` prints the type letter twice with
    no attributes and wins over `-c`.
16. Host build flags: `-std=c17 -D_DEFAULT_SOURCE -DHOSTBUILD`. Strict C17
    hides `select`, `gettimeofday` and `FIONREAD` on glibc, and
    `_DEFAULT_SOURCE` exposes them without changing the language level. The
    logic test uses `-std=c17 -pedantic` with no feature macro. The target
    build keeps the tree's `-std=gnu17 -Wall -Wextra -Werror`.
17. The test directory is `games/fourfall/tests`, the location keen, bubble and
    fifteen use. The specification names `test-logic` with `-std=c99`; the
    target here uses C17 as the assignment requires.

## Changes after the copying review

A separate reviewer, who had read the reference program, found that two
expressive choices of the first version matched it: the per-type color mapping
(the specification's palette) and the row-major order of the cells in the
base shape table. Both are free choices of expression, so two changes
followed.

1. Palette. `ff_color` now maps I to white (47), O to blue (44), T to green
   (42), S to magenta (45), Z to cyan (46), J to red (41) and L to yellow
   (43). The outline foreground follows (30 plus the last digit of the new
   background code, so O is 34). Monochrome mode is unchanged and independent.
   `play.py` color assertions and the front-end mutant `T-color-yellow` were
   updated.
2. Cell order. The base shape table in `fourfall.c` lists each piece's four
   cells last row first and right to left within a row, and every rotated
   orientation inherits that order cell by cell. The SHAPE rows of
   `tests/vectors.txt` were rewritten in the same order; their cell sets are
   unchanged from the specification (checked before overwriting). The harness
   compares cell sets after sorting, so order is not asserted as behavior.

3. O piece enumeration. A second pass found that the O piece's cells still
   came out in row-major-derived order after rotation. `ff_cells()` now
   returns the O cells as (1,1), (0,0), (1,0), (0,1) for every orientation
   and skips the rotation matrix for O, which maps the square onto itself.
   The O rows of `tests/vectors.txt` use that order, `logic_test.c` asserts the
   exact enumeration for all four orientations (`check_o_enumeration`), and
   the mutant `O-cells-rotated` restores the rotation and is rejected.

All gates were rerun after the changes; see the counts under Evidence.

## Presentation

A similarity review found that the combination of a 10 by 20 well, an outline
ghost piece on by default, a single next-piece preview and one solid color per
piece repeats the look-and-feel elements protected in a 2012 District of
New Jersey look-and-feel decision cited in the reviewer-only copying record. Rules and
mechanics stay as specified; the presentation departs from each element, and
the departures suit the PicoCalc's grayscale-leaning, ASCII-only console (6x10
cells in the Misc-Fixed font, bold and the gray palette entries available):

| Element | Genre convention | fourfall |
| --- | --- | --- |
| Well | 10 columns by 20 rows | 12 columns by 22 visible rows (24 with two hidden spawn rows); spawn column `(12 - box side) / 2` centers each piece |
| Cell look | one solid color per piece | a two-character glyph texture per piece (I `[]`, O `##`, T `<>`, S `//`, Z `\\`, J `()`, L `{}`), bold white falling, bright black settled, no hue |
| Landing aid | outline ghost, on by default | off by default; `-g` (or key `g`) draws a plumb line of `.` below each occupied column down to its surface, no outline |
| Preview | one next piece | two, stacked under the title `NEXT`, each as its glyph texture in spawn orientation |
| Frame | `\|` walls, `+--+` floor | `!` walls, `=` floor, title `FOURFALL` above the well |
| Line clear | immediate collapse | cleared rows show `~~` in every cell for about 150 ms, then collapse |
| Color | on | opt-in with `-c` (the palette from the copying review, as foreground hues); `-m` keeps the letter mode |

The 12-column spawn rule gives column 4 to the I and the 3-wide pieces and
column 5 to the O. A 3-wide box leaves four free columns left and five right,
because the well has an even width and the box an odd one; the property check
`prop_spawn_centered` bounds the imbalance at one column.

The plumb line is per column, not per drop position: each occupied column gets
dots from the row under its lowest piece cell to the first filled cell or the
floor (`ff_plumb`). The line shows where that column's surface lies and leaves
the player to judge where the piece lands, which an outline would show
directly.

The clear flash is presentation only. `ff_lock` records the full rows
(`flash_mask`) and the board before the collapse (`flash_board`) and then
collapses the board as before; `flash_cleared()` in the front end draws that
record with `~~` cells, waits `FLASH_MS` (150 ms) with `usleep()` and clears
the record. Keys pressed during the wait stay in the tty queue, and `main()`
resets its clock after a flash, so gravity and the lock timer do not count the
wait. When two locks fall inside one frame (a stall of up to 1,000 ms), the
flash shows the later clear. Board execution of `usleep()` on the console is
`not run`.

Layout on the 53 by 32 console (columns across, rows down; `X` is a well cell,
two columns wide; the first preview is shown as an I and the second as a T):

    col  1         2         3         4         5
         12345678901234567890123456789012345678901234567890123
    r1   ..........FOURFALL.................................
    r2   .!XXXXXXXXXXXXXXXXXXXXXXXX!..SCORE..N...............
    r3   .!XXXXXXXXXXXXXXXXXXXXXXXX!..LEVEL..N...............
    r4   .!XXXXXXXXXXXXXXXXXXXXXXXX!..LINES..N...............
    r7   .!XXXXXXXXXXXXXXXXXXXXXXXX!..NEXT...................
    r9   .!XXXXXXXXXXXXXXXXXXXXXXXX!........................
    r10  .!XXXXXXXXXXXXXXXXXXXXXXXX!..[][][][]...............
    r12  .!XXXXXXXXXXXXXXXXXXXXXXXX!....<>....................
    r13  .!XXXXXXXXXXXXXXXXXXXXXXXX!..<><><>.................
    r16  .!XXXXXXXXXXXXXXXXXXXXXXXX!..key help, rows 16 to 22
    r23  .!XXXXXXXXXXXXXXXXXXXXXXXX!..........................
    r24  .==========================..........................
    r26  score N lines N level N

The well and its walls fill columns 2 to 27 and rows 1 to 24; the panel starts
at column 30 and its longest help line ends at column 50, so no text reaches
column 53, and row 26 carries the status line with row 32 untouched. The pty
test `layout` asserts every position above on a 53 by 32 terminal model and the
`text reached column` and `row 32` checks.

## Evidence

Source revision: working tree on `076675a8` plus the uncommitted
`games/fourfall` files. Host: Linux 6.18 (WSL2), `cc` GCC 16.2.1, `bmake`,
Python 3.14.9. `PYTHON=python3` exported.

Command: `bmake -C games/fourfall test` (runs the four gates below).

| Gate | Result |
| --- | --- |
| `test-logic`: strict C17 (`-std=c17 -pedantic -Wall -Wextra -Werror`), AddressSanitizer and UndefinedBehaviorSanitizer with `-fno-sanitize-recover` | 161 vectors run, 0 failed (SHAPE 28, COLL 22, KICK 14, CLEAR 11, SCORE 13, LEVEL and GRAV 14, BAG 4, DROP 6, TOPOUT and SPAWN 15, SCREEN and NEXT 13, GLYPH 7, PLUMB 9, REPLAY 5); 1,472,151 checks, 0 failed |
| property checks 1 to 7 | pass; worst same-type gap over seeds 1 to 20 is 13 (limit 13); worst position and type deviation over seeds 1 to 20 is 9.2 percent (limit 15) |
| gravity, lock-timer and queue checks | 800 ms first step and the level 9 step of 210 ms; 500 ms lock after landing; ten restarts then the eleventh adjustment leaves the timer running; sliding off a ledge stops the timer; soft and hard drop points; saturation at 99,999,999; clear scoring at the pre-update level; cyclic script queue; queue of at least two pieces over 7,000 spawns on 20 seeds; 200 stepping-equivalence games |
| `test-mutants` | baseline passes; 32 of 32 mutants rejected |
| `test-play` | 16 cases, 327 checks pass on `fourfall-host` |
| `test-play-mutants` | 27 of 27 front-end mutants rejected |
| `test-play` against a sanitizer build of the host program | `not run` after the redesign |

Mutants of the rules, all rejected (the first ten are the specification's
list, the rest are added calibration):

| Mutant | Rejected by |
| --- | --- |
| rotation direction swapped | KICK-01 to KICK-14 family, REPLAY-4 |
| right wall `col >= 11` | COLL-03, COLL-21, KICK-04, KICK-06, KICK-13, REPLAY-1 |
| floor check removed | COLL-06, COLL-08, COLL-10, KICK-02, KICK-07, KICK-09 and more |
| no kicks for T, S, Z, J, L | KICK-05, KICK-06, KICK-07, KICK-09 |
| no kicks for I | KICK-02, KICK-03, KICK-04, KICK-13 |
| line score `100 * n * level` | SCORE-L1-2 to SCORE-L10-4 |
| clear skips the row after a removed row | CLEAR-04, CLEAR-06, CLEAR-08, REPLAY-1 |
| level step every 9 lines | LEVEL-1/9, LEVEL-1/95, LEVEL-9/100 |
| O spawns one column left of center | SPAWN-O, REPLAY-1 |
| spawn column computed for a 10-column well | SPAWN-I to SPAWN-L, TOPOUT-2, TOPOUT-3 |
| 10-column well, 22-row board | COLL-03, COLL-19 to COLL-21, KICK-04, KICK-06 and more; COLL-05, COLL-07, COLL-09 and more |
| plumb line starting inside the piece, or ignoring the stack | PLUMB-01, PLUMB-03 to PLUMB-07; PLUMB-03, PLUMB-04 |
| two glyphs equal (L as J, Z as S) | GLYPH-L, GLYPH-Z, property check 7 |
| overlapping preview boxes, a single preview slot | NEXT-I1, NEXT-O1, NEXT-S1; property check 7 |
| flash record not set, flash board not saved | state checks on `flash_mask` and `flash_board` |
| plain `rand()` per piece instead of a bag | bag permutation check (seed 1 bag 0) |
| lock restarts allowed eleven times, lock delay 400 ms, level 2 interval 710 ms | state checks, GRAV-2 |
| hard drop 1 point a row, soft drop 2 points | REPLAY-1 to REPLAY-4, state check |
| lock-out needs row 3 | TOPOUT-LO1, TOPOUT-LI1 |
| score not saturated, queue keeps one piece, O rotation keeps old `rot`, hidden row drawn | state checks, KICK-08, SCREEN-HID |

Front-end mutants, all rejected by `play.py`: right wall at column 28, status
line wording, pause that keeps the board, bare Esc ignored, wrong F1 CSI code,
SIGTERM exit status 0, resume without repaint, T drawn yellow under `-c`,
cursor parked on row 32, Ctrl-L ignored, Enter not a hard drop, gravity that
never advances, plumb line on by default, color on by default, solid
background cells, settled cells as bright as the falling piece, a 10-column
well, a 22-row board, a single preview, `|` walls, a `-` floor, a title
left-aligned, a skipped clear flash, a flash of 20 ms, `-g` ignored, `-c`
ignored, and an unknown option accepted.

The pty tests apply the game's output to a 53x32 model (cells carry
foreground, background and bold) and check: both REPLAY vectors through the
screen (`score 564 lines 2 level 1`, then `score 264` with `GAME OVER`,
`Enter  play again` and `q  quit` at their rows and columns, Enter starting a
new game, `q` exiting with 0); arrow keys, `x`, `z`, CR and LF; the frame
(`!` walls, `=` floor, title), the panel labels, both stacked previews as glyph
textures and the cursor park at row 31; no text on row 32, no scroll and no
text past column 52; the default look (glyph per type, bold white falling,
bright black settled, SGR codes limited to 0, 1, 37 and 90, no background, no
dots); the `-c` hues; the `-g` plumb line and its `g` toggle, including dots
stopping on a stack; monochrome output and `-m` winning over `-c`; the clear
flash (12 `~~` cells in one frame, a gap of 0.12 to 0.6 s to the next frame,
board collapsed after); the usage exit status 2; pause and resume by `p`,
`ESC O P`, `ESC [ 11 ~` and a bare Esc, with unknown sequences ignored; the
Ctrl-L repaint; SIGINT and SIGTERM exit statuses with the terminal mode
restored; and, without `GAMEBOX_TEST`, real gravity at level 9 (a falling
piece, a lock followed by a status redraw, no plumb line, and no lock while
paused).

Lint: `ruff check`, `ruff format --check`, `flake8 --max-line-length=100` and
`mypy` pass on the two Python files.

## Target build and size

Command: `bmake MACHINE=rp2040 fourfall` in `games/fourfall`, compiled with
the tree's flags (`arm-none-eabi-gcc -Wall -Wextra -Werror -std=gnu17
-mcpu=cortex-m0plus -mthumb -nostdinc -Os`). The worktree holds no built
`lib/crt0.o`, `lib/libc.a` or `tools/bin/rp2040`, so the link used the primary
checkout's `lib/` and `elf2aout` through `LDFLAGS` and `ELF2AOUT` overrides on
the command line. The compile used this worktree's `include/`.

| Artifact | text | data | bss |
| --- | --- | --- | --- |
| `fourfall.o` (after the redesign, same flags, `bmake MACHINE=rp2040 fourfall.o`) | 5,445 | 0 | 7,448 |
| standalone ELF, before the redesign (not relinked) | 5,534 | 8 | 6,968 |
| standalone a.out, before the redesign (not relinked; 5,576 bytes on disk) | 5,536 | 8 | 6,968 |
| for comparison, keen / bubble / fifteen a.out | 8,440 / 7,848 / 6,564 | 204 / 208 / 204 | 1,704 / 1,996 / 124 |

Of the bss, 6,144 bytes are the frame buffer; the rest is the game state
(about 0.7 KB with the flash board), the shadow and view arrays (528 bytes)
and panel caches. The multicall trial below predates the redesign and was not
rerun; the object grew by 704 bytes of text and 496 bytes of bss.

Multicall trial, outside the tree: a scratch copy of `games/` with symlinked `share`, `include`, `lib` and `tools`, `TOOLS`
extended to `fifteen keen bubble fourfall`, and `bmake MACHINE=rp2040 gamebox`.
`verify_multicall_bss_objects.sh` accepted `fourfall.tool.o` (no COMMON, bss in
`.app_bss_fourfall`), the overlay script generated, and the link succeeded.
The four-game `gamebox` has text 16,900, data 208, bss 7,000 (the shared
overlay is set by fourfall) and a 17,140-byte a.out, against 12,248 bytes for
the three-game `gamebox` built in the primary checkout. Both stay below the
30 KB budget in `sys/arch/rp2040/doc/STORAGE.md`.

`select()` and `gettimeofday()` were exercised on the host only. The board
check of both on the console tty is `not run`: it needs the repository's board
opt-in. The kernel names `usbselect` in `sys/arch/rp2040/rp2040/conf.c`, so
the device exposes a select entry, but no run here shows it waking on input
or on timeout. Board timer grain, key rate and console redraw time are
unmeasured.

## Integration lines left for the parent

None of these files was edited.

- `games/Makefile`: add `fourfall` after `bubble` in `SUBDIR`
  (`fifteen keen bubble fourfall gamebox`) and change
  `gamebox:        fifteen keen bubble` to
  `gamebox:        fifteen keen bubble fourfall`.
- `games/gamebox/Makefile`: `TOOLS=		fifteen keen bubble fourfall`; the
  header comment says "the three games".
- `distrib/rp2040/mi.rp2040`, inside `#closure games` after the bubble pair:
  `link /usr/games/fourfall` then `target /usr/games/gamebox`; the comment
  above names the games.
- Root `Makefile`: add

      check-fourfall-host:
      		${MAKE} -C games/fourfall test

  and add `check-fourfall-host` to `HOST_PROGRAM_GATES` (line 946) and to the
  gate list near line 1292. `sys/arch/rp2040/doc/TESTING.md` line 576 lists the
  game gates and takes a fourfall row. `NOTICE` section 1 and
  `sys/arch/rp2040/doc/research/games.md` name the three games.

## Files read

Instruction and build context: the owner's global agent instructions
(supplied in the session), the repository
`AGENTS.md` (byte-identical to `.claude/CLAUDE.md`, supplied in the
session), `share/mk/sys.mk`, `share/mk/architecture.mk` (first 60 lines),
`share/mk/warnings.mk` (first 40 lines), `share/mk/multicall-bss-overlay.mk`
(first 40 lines), `tools/verify_multicall_bss_objects.sh` (first 50 lines),
`tools/check-lint.sh` (first 40 lines), `ruff.toml`.

Game structure: `games/gametty.h`, `games/Makefile`, `games/gamebox/Makefile`,
`games/gamebox/gamebox.c.in` (first 60 lines), `games/keen/Makefile`,
`games/keen/keen.c` (header, tail and `main`), `games/keen/tests/play.py`
(first 60 lines), `games/keen/.gitignore`, `games/bubble/Makefile`,
`games/bubble/bubble.c` (first 140 lines and `main`),
`games/bubble/tests/play.py`, `games/fifteen/Makefile`,
`games/fifteen/fifteen.c` (header), `games/fifteen/.gitignore`,
`games/fifteen/tests/play.py` (header).

Tree facts: `NOTICE` (lines 40-60), `tools/flashimg/flashimg.c` (header),
`lib/libc/compat/rand.c`, `include/stdlib.h` (RAND_MAX), `include/sys/ioctl.h`
(FIONREAD), `include/sys/select.h`, `include/sys/time.h`, `include/signal.h`,
`include/unistd.h` (grep), `usr.bin/kilo/kilo.c` (the bare-Esc FIONREAD poll),
`sys/arch/rp2040/rp2040/conf.c` (grep), `Makefile` (the `check-*-host` targets
and gate lists), `distrib/rp2040/mi.rp2040` (games closure),
`sys/arch/rp2040/doc/TESTING.md` (line 576), `sys/arch/rp2040/doc/research/games.md`
(first 30 lines and grep).

Specification: only the implementer half, a copy of Part 1 of
`docs/research/falling-block-clean-room-spec.md` as first written, cut at its
`REVIEWER-ONLY BELOW` marker. The presentation
redesign read and edited Part 1 of `docs/research/falling-block-clean-room-spec.md`
(above its `REVIEWER-ONLY BELOW` marker) and `games/fourfall/*`.

Not read by the original implementation pass: `legacy/non-arm/mips-pic32/share/examples/gpanel/tftetris.c`, anything
under `legacy/non-arm/mips-pic32/share/examples/gpanel`,
`docs/research/falling-block-clean-room-spec.md`, the reviewer half of the
specification, and any falling-block source on the network.
