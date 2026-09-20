# A fourth multicall box: three ASCII puzzle games

`games/gamebox` links fifteen, keen and bubble into one a.out of
12,132 bytes (text 11,678 + data 420 + bss 2,024 not counted in the
file + a.out header), built the same way as `sbin/box`, `sbin/sysbox`
and `sbin/textbox`: each game builds in its own directory under
`games`, `ld -r` combines its one object, `objcopy --redefine-sym
main=<game>_main --keep-global-symbol=<game>_main` localizes
everything else, and `games/gamebox/gamebox.c.in` generates the
dispatch table. This is well inside the 30 KB budget in
`sys/arch/rp2040/doc/STORAGE.md`, with room to spare for a fourth
game later. Standalone a.out sizes, built and linked the ordinary
way for comparison: fifteen 8,476, keen 9,800, bubble 8,888 bytes;
the box's shared libc copy is most of the saving.

All three games avoid floating point (no `float`/`double` anywhere,
so `PRINTF_FLOAT` stays unset and no program pulls in
`doprnt_float.o`) and avoid curses, drawing the board with raw ANSI
escapes (`\033[2J\033[H` to clear and home, `\033[H` to home without
clearing) and the port's own sgtty raw-mode ioctls (`TIOCGETP`,
`TIOCSETP`, the `RAW` and `ECHO` bits in `include/sys/ioctl.h`), the
same mechanism `usr.bin/re`'s `r.ttyio.c` sets up through the sgtty
branch of its `TERMIOS` `#ifdef` -- this port has no `<termios.h>`
yet (`include/termios-todo.h` is a stub), only `<sgtty.h>`. The
shared helper is `games/gametty.h`, included by all three; it also
carries a `HOSTBUILD` branch that uses termios instead, since a
Linux or BSD host has no sgtty ioctl wired to a pty. Cursor keys
arrive as `ESC [ A/B/C/D` and `gtty_getkey()` folds them to single
codes so no game parses escapes itself.

## Sources checked and their licenses

- `~/Downloads/puzzles-20251220.ecb576f.tar.gz` -- Simon Tatham's
  Portable Puzzle Collection. `LICENCE` in that tarball is MIT
  (copyright Simon Tatham and named contributors). Measured with
  `wc -l` and `grep '#include "'`:

  | Puzzle | Lines | Extra dependency |
  |---|---|---|
  | fifteen.c | 1271 | none (puzzles.h only) |
  | towers.c | 2265 | latin.h |
  | singles.c | 2070 | latin.h |
  | unequal.c | 2469 | latin.h |
  | lightup.c | 2489 | none |
  | keen.c | 2661 | latin.h |
  | net.c | 3357 | tree234.h |
  | mines.c | 3452 | tree234.h |
  | dominosa.c | 3578 | none |

  Every one of these files is written against `puzzles.h`'s midend
  and drawing API (game_params, game_state, game_drawstate,
  `interpret_move`/`execute_move`/the draw callbacks), which exists
  to serve a GUI front end shared across ~40 puzzles and is far
  larger than this box's budget on its own, before any puzzle logic.
  No code from this tarball is reused; fifteen's well-known
  shuffle-by-legal-moves generation technique (predates this
  codebase, not an expression original to it) is the only idea
  taken, described in `games/fifteen/fifteen.c`'s header comment.
  mines.c and net.c also depend on `tree234.h`, called out in the
  task as heavy, and were excluded from consideration for that
  reason on top of their generator size.

- `~/Github/puzzles` (`pb_core`, `gororoba_puzzle`) -- its
  `AGENTS.md` and `LICENSE` mark the code GPL or unlicensed
  (reference-only per that repo's own instructions). Read for
  behavior only: "aim, shoot, pop groups of three". No code taken;
  `games/bubble/bubble.c` is a clean-room implementation with a
  different internal model (see below).

- `~/Github/KeenClassik`, `~/Github/KeenKenning` -- Android KenKen
  apps (Gradle, `core`/`external`). Not consulted for algorithm
  extraction: `games/keen/keen.c`'s generator (cyclic Latin square,
  row/column/symbol shuffle, random adjacent-cell cage union) is
  simple enough that copying from a larger, Java/Kotlin-oriented
  codebase would cost more time than writing it, and it avoids a
  second license to track. Listed here because the task named them
  as sources to check.

- `~/Github/rustykeen` -- Rust, MIT. Same disposition: not consulted
  beyond confirming its existence, for the same reason.

- The tree's own `games/` (2.11BSD games, `BINDIR=/usr/games`):
  `wump`, `hangman`, `bcd`, `fortune`, `adventure`, `battlestar`,
  `boggle`, `caesar`, `cribbage`, `mille`, `monop`, `pom`, `pig`,
  `quiz`, `robots`, `rogue`, `sail`, `snake`, `trek`, `banner`,
  `aclock`, `arithmetic`, `factor`, `fish`, `morse`, `number`, `ppt`,
  `primes`, `rain`, `worm`, `worms`, `canfield`, `atc`, `backgammon`,
  `btlgammon`. None of these overlap a grid puzzle, a KenKen, or a
  matching game, so nothing here is a duplicate port.

## What was written new

- **fifteen** (`games/fifteen/fifteen.c`, 4x4 sliding-tile puzzle).
  Novel implementation. Generation: start solved, apply 200 random
  legal slides, tracking the direction of each so the next slide
  never immediately undoes the last -- this alone guarantees
  solvability, with no separate solver. Play: arrow keys move the
  blank, `q` quits. `GAMEBOX_TEST=1` prints the shuffle log
  (`u`/`d`/`l`/`r`, one line) before the board, so
  `tests/play.py` can feed back the exact reverse sequence and reach
  "solved!" without needing to know anything about the host's
  `rand()`.

- **keen** (`games/keen/keen.c`, 3x3 to 6x6 KenKen/Kakuro-style
  Latin-square puzzle, the user's favorite). Novel implementation.
  Generation: a cyclic base Latin square (`(c + r) % n`) has its
  rows, columns and symbols each independently shuffled with
  Fisher-Yates (each operation preserves the Latin-square property,
  so no backtracking search is needed), then cages are grown by
  repeatedly union-ing random adjacent cells (union-find, capped at
  4 cells) and each cage's clue (+, -, x, / for size 2; + or x for
  larger) is computed directly from the solution values.
  **Simplification, stated plainly**: there is no uniqueness check,
  so a generated puzzle can rarely admit a second valid filling
  besides the one generated, and "solved" means "matches the
  generated grid" rather than "satisfies every clue" -- checking the
  latter in general needs a constraint solver, which is what
  Tatham's keen.c (plus its `latin.c` helper) spends most of its
  2,661 + moderate lines on, and that solver alone would not fit
  this box's budget. This is the same shortcut most small ASCII
  KenKen clones take. Play: arrows move the cursor, `1`-`9` fill,
  `0`/backspace/DEL clears, `q` quits. `GAMEBOX_TEST=1` prints the
  solution grid as one line of digits before play starts, so
  `tests/play.py` can fill it in without a solver of its own.

- **bubble** (`games/bubble/bubble.c`, puzzle-bobble-style matching
  game). Clean-room implementation, behavior-only reference to
  `~/Github/puzzles`' description. **Simplification, stated
  plainly**: bubbles rest on the floor in independent columns
  (Connect-Four style) rather than hanging from a ceiling with
  floating pieces that drop after a pop, which needs no "is this
  still connected to the top" flood-fill pass separate from the
  match check -- one 4-way flood fill both finds the popped group
  and (after removal) every column is gravity-settled to close the
  gaps. A shot lands on top of its column; 3 or more connected
  same-color bubbles pop (10 points each); the board starts with 3
  rows of random color on the floor; the game is won by clearing the
  board and lost when a column fills to the ceiling. Play:
  left/right aim, space fires, `q` quits. `GAMEBOX_TEST=1` starts
  the board empty and holds the shot color for 3 shots before
  advancing, independent of `rand()`, so `tests/play.py` can fire
  three shots into one column and see them pop deterministically.

None of the three needs `long long` or K&R-style function
definitions; every function is declared with a full ANSI prototype
and every block's declarations are grouped at its top, both required
by `usr.bin/smlrc/README.rp2040.md`. `rand()`/`srand()` come from
`lib/libc/compat/rand.c` (an integer-only linear congruential
generator, no float); the LCG's output is not required to match
between the host build and the device build -- "deterministic given
a seed" means each build reproduces its own play given the same
seed and inputs, which the tests below confirm, not that both
builds draw the same sequence from the same seed.

## Sizes

Cross-built with `bmake MACHINE=rp2040`, `arm-none-eabi-size`:

| Program | text | data | bss | a.out bytes |
|---|---|---|---|---|
| fifteen (standalone) | 8028 | 416 | 152 | 8,476 |
| keen (standalone) | 9352 | 416 | 956 | 9,800 |
| bubble (standalone) | 8434 | 420 | 1060 | 8,888 |
| gamebox (all three) | 11678 | 420 | 2024 | 12,132 |

27,164 bytes of standalone code and one shared libc copy each
becomes 12,132 bytes as one box -- comfortably under the 30 KB
budget, with about 18 KB of headroom against it.

## Tests

Each game directory has a `host` bmake target
(`${HOSTCC} -std=c99 -Wall -Wextra -DHOSTBUILD -o <game>-host
<game>.c`, same source as the device build, `HOSTBUILD` picks the
termios branch of `gametty.h`) and a `tests/play.py` invoked through
`${PYTHON}` that forks a pty, plays the game to
completion with a fixed seed using each game's `GAMEBOX_TEST`
determinism hook, and asserts the win condition is reached:

- `games/fifteen`: `bmake test`
  -- solves a 200-move shuffle by replaying its exact inverse.
- `games/keen`: `bmake test` drives the game and the decomposed C17
  uniqueness proof
  -- fills a 5x5 grid from the printed solution and reaches "solved!".
- `games/bubble`: `bmake test`
  -- fires three matching shots and confirms the score reaches 30.

All three pass, and all three host builds are `-Wall -Wextra` clean.

## Cross-build and Smaller C proof

`bmake MACHINE=rp2040 CFLAGS="-Os -fcommon -Wall -Wextra" <game>.o`
is clean (no warnings) for all three against
`arm-none-eabi-gcc`. Each game was also taken through
`usr.bin/smlrc`'s own test harness path by hand: preprocessed with
`arm-none-eabi-gcc -E -P` (smlrc has no function-like macro
expansion, matching how `usr.bin/cc` runs `usr.bin/cpp` first on the
device), compiled with a host-built `smlrc -DTHUMB`, assembled with
`arm-none-eabi-as -mcpu=cortex-m0plus` (rejects any encoding wider
than ARMv6-M), and linked against the tree's own `crt0.o`/`libc.a`
with `elf32-arm.ld`. All three produced a fully resolved ELF (no
undefined symbols) and converted to a loadable a.out with
`tools/bin/elf2aout`: fifteen 10,400, keen 15,224, bubble 11,988
bytes (smlrc's own code generation is less dense than GCC's `-Os`,
which is expected and does not affect the GCC-built box that ships).
This confirms a tinkerer can `cc -o fifteen fifteen.c` (and the
same for keen and bubble) on the board itself.

## Manifest and layout

`games/gamebox` is a new SUBDIR entry in `games/Makefile` alongside
`games/fifteen`, `games/keen` and `games/bubble` (each still builds
and installs standalone, the same relationship `sbin/textbox` has to
`usr.bin/cut` and its siblings). `distrib/rp2040/mi.rp2040` adds
`dir /usr/games` (not previously declared on this root) and installs
`/usr/games/gamebox` as the one real file, with `/usr/games/fifteen`,
`/usr/games/keen` and `/usr/games/bubble` as hard links to it,
matching the `box`/`sysbox`/`textbox` convention exactly. `BINDIR`
for each game's own Makefile is `/usr/games`, the tree's existing
convention (see `games/wump/Makefile`), so this ships at
`/usr/games/gamebox` rather than falling back to `/usr/bin`.
