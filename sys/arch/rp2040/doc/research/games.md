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

## fourfall, the fourth game

`games/fourfall` joins the box as a fourth tool (`TOOLS= fifteen keen
bubble fourfall`) and `/usr/games/fourfall` as a fourth hard link. The
box then measures 18,464 bytes raw and 16,484 packed, 18 packed blocks
and 20 raw blocks (`hsaout -s games/gamebox/gamebox`), with text 18,224,
data 208 and bss 18,690. The previous front-end loop measured 17,900 raw and
15,968 packed, 17 packed blocks, text 17,660 and bss 18,634 on the same
toolchain and libc; the loop described below adds 564 bytes of text and
crosses a block boundary: the `games` closure costs 19 root blocks and the
`full` image keeps 280 free (`sys/arch/rp2040/doc/PROFILES.md`). `share/mk/multicall-bss-overlay.mk` gives
the four games' private bss one shared range sized by the largest, fourfall's
7,476 bytes (bubble 1,952, keen 1,660, fifteen 78); the other 11,214 bytes
belong to the shared libc and startup code. fourfall adds eight libc
members to the box's shared copy, `getpid.o`, `kill.o`, `select.o`,
`sigaction.o`, `signal.o`, `sigprocmask.o`, `strchr.o` and `usleep.o`
(the undefined symbols of `fourfall.o`; the image-level member report of
`check-multicall-composition` is `not run`). Like the other three it uses no floating point and no
curses, and it draws through `games/gametty.h`; its rules, presentation
and evidence are in `docs/research/fourfall-implementation.md`.

The front-end loop waits in `select` for input, the next gravity or lock
deadline or a stalled escape sequence, charges elapsed time before it applies
a key (`ff_tick`), and leaves through one terminal-restore function on `q`,
end of input, a select failure or a signal. On the target the sgtty RAW flag
delivers ^C as the byte 3, so `q` is the keyboard quit and the signal handlers
serve `kill(1)` and hangup. The image recipe builds with
`-std=c17 -pedantic-errors`, which the sgtty headers accept on the cross
compiler.

## coil, hexagonal bubble and the puzzlebox

`games/coil` joins `gamebox` as a fifth tool and `/usr/games/coil` as a fifth
hard link. `games/bubble` gains a hexagonal mode (`bubble -x`) and is
otherwise the same tool. Measured by `hsaout -s` and `arm-none-eabi-size` on
the toolchain and libc of this tree: `gamebox` is 23,980 bytes raw and 21,693
packed, 23 packed blocks and 25 raw blocks (text 23,720, data 228, bss
23,600; before coil and the hexagonal mode: 18,464 raw and 16,484 packed, 18
blocks). The object text of `bubble.o` grew from 2,319 to 2,866 bytes and
`coil.o` is 4,993. coil's rules and front end share `games/gameterm.h`, the
timed `select` loop, escape parser and signal handling that
`games/fourfall` carries inline.

`games/puzzlebox` is a second multicall box for four of Simon Tatham's
puzzles (`flood`, `samegame`, `unruly`, `lightup`) behind one text front end
(`games/pzcommon`). The split from `gamebox` is a measured choice: the four
standalone programs are 18,172, 17,320, 20,692 and 23,064 bytes raw (17, 16,
19 and 21 packed blocks, 73 in all), and `puzzlebox` is 38,960 raw and 34,397
packed, 35 blocks. Put into `gamebox`, the five games and four puzzles would
come to more than 50 KB raw, past the 40 KB guideline of `STORAGE.md`. The split
is reversible: the four tools move to `games/gamebox/Makefile` `TOOLS`
and the manifest links retarget, and the owner reviews it. The `games`
closure holds `gamebox` and costs 24 root blocks and 2 inodes, and the
`puzzles` closure holds `puzzlebox` and costs 35 blocks and 1 inode
(`PROFILES.md`); the `full` and `picocalc` profiles keep both.

A puzzle is its wrapper (`games/<name>/<name>.c`) plus the collection's
source included into one translation unit built with `-fwhole-program`. The
collection's `struct game` table then has internal linkage and no reference,
so the compiler drops it and every presentation function it points to (they
need the collection's graphics layer, floating point and `libm`). The shared
object `games/pzcommon/pzcommon.o` keeps its globals, is built with a section
per function, and the box and the standalone links add `-Wl,--gc-sections` to
drop the support functions no puzzle reaches. `midend.c` is not linked.

Open points. The imported unruly generator abandons a game state, with every
block it owns, each time a candidate comes out too easy for the difficulty
(`unruly.c`, `new_game_desc`; the easy and normal settings reject, trivial
never does), and the `n` key repeats generation in one process, so the leaks
accumulate toward the 144 KB window. The wrapper redirects the generator's
allocator names to the tracking versions in `games/pzcommon/pzscope.h` and
frees every block still recorded when generation returns, which leaves the
imported file unchanged. Measured on the host heap over 100 games per size,
an unreclaimed game leaks a mean of 26 to 262 bytes and at most 2.7 KB
(6x6 normal 262 bytes, 24x16 normal 26 bytes); 280 further games at 6x6
normal grew the data segment by 52 KB before the change and by none after.
Within one generation the abandoned states still coexist until it returns,
so the peak is the single-game figure. The host gate runs leak detection on
all four puzzles. The collection's `flood.c` defines a non-static
`check_recursion_depth`, which the whole-program build makes local.
Board behavior of all four puzzles, of coil and of the hexagonal bubble mode
is unmeasured.

## ridge, the pixel-device platformer

`games/ridge` is a side-scrolling platformer for `/dev/lcdgfx`: a hooded
climber crosses four levels (`FOOTFALL`, `SCREE`, `CHIMNEY`, `SUMMIT`), runs,
jumps (a held key jumps higher), lands on rock and on one-way ledges,
collects shards, lights checkpoint flags, stomps crawlers and reaches the
cairn. Spikes, crawlers and pits cost a life; the third ends the game. Name,
rules, levels, tile art and sprites are original to this port; the only
borrowed idea is the dirty-tile refresh that `docs/research/picocalc-games-survey.md`
records from the Keen Dreams refresh manager; no code, data or art comes
from it.

Structure. The one file `games/ridge/ridge.c` has three halves. The rules
perform no I/O and read no clock: positions are sixteenths of a pixel, one
`rg_tick()` runs per 33 ms, and the camera, scoring, level progress and the
translation of raw key events into per-tick input live there.
`RIDGE_LOGIC` compiles that half alone for `tests/logic_test.c`. The render
half draws through `games/gametile.h`; `RIDGE_RENDER` compiles it with the
rules for `tests/render_test.c`, which binds the two device calls of
`gametile.h` to a model of `/dev/lcdgfx`. The front end opens
`/dev/lcdgfx` and the console (refusing a start outside the console's
foreground process group, and ignoring `SIGTTOU` so no tty call can stop it
with the panel held), sets RAW without echo, starts the raw key
session (`LCDIOC_KEYEVS`), runs a timed `select` loop that drops the
bytes the tty also queues with `TIOCFLUSH` after each successful key drain
(a flush never waits, and a drain fails once another program has changed
the mode or taken the foreground, so that program keeps its input), and on
exit restores the tty mode and flushes the queues only if a last drain
shows the session still holds the console, then ends the session and
closes the device, which returns the panel to
the text console. Without `/dev/lcdgfx` or without a key session it prints
the reason and exits: the game has no text-terminal fallback, because the
console cannot show it and the raw key events are what make held keys work.

Physics. The player's hit box is 10 by 14 pixels. Run speed is 2.5 pixels a
tick reached in five ticks on the ground (six units a tick of acceleration
in the air), gravity 0.75 pixel a tick squared with a fall cap of six pixels
a tick, and a jump leaves at 8.25 pixels a tick, which rises 41.25 pixels
(5.2 tiles) when the key stays down and 12 pixels when it is released at
once (a press and release are two raw events, so the cut is exact).
Movement splits into steps of at most four pixels, so no tick crosses a
tile. Coyote time (a jump up to three ticks after leaving an edge) and a
jump buffer (a press up to three ticks before landing) cover the keyboard's
one event per 32 ms. A direction pressed and released between two ticks
still moves one tick. The key state is kept per physical key, because a held
printable key repeats as PRESSED events (`picokbd_map.h`): jump, drop, pause,
quit and confirm fire on the first PRESSED of a key and not again until its
RELEASED, and an action is held while any key that drives it is down (Space and
Up both jump). Lost events clear every key's state. A stomp needs the tick's move to have carried the box down from a bottom
within 5 pixels of the crawler's top; `rg_tick()` records both before the move,
because a floor landing in the same tick clears the velocity. A life lost to a crawler respawns at the last checkpoint with 45 ticks of
grace, during which a crawler's touch does no harm (a stomp still scores); the
grace ends only once no crawler touches the climber, which blinks while it
lasts. Crawlers stand still while the climber is dying, so without grace a
crawler in the spawn box would kill it again at the first tick. The level-data
rules also keep every checkpoint's spawn box outside every crawler's patrol.
Level design keeps every gap at five columns or less (the jump crosses six)
and every climb at four rows or less.

Tiles and sprites. The atlas holds 57 static tiles (sixteen terrain and
item tiles, then a 41-glyph 5 by 7 font for the HUD and banners), all
generated in code and uploaded once, and 63 slots for sprites. A 16 by 16
sprite covers at most 3 by 3 cells, so each of up to seven sprites (the
player and six crawlers) owns nine slots; the renderer composites the
sprite over the world tiles under it in user memory and uploads the nine
tiles with one `LCDG_TILESET`, which costs no bus time. Two sprites whose
3 by 3 boxes meet share cells; every sprite composites all the sprites whose
boxes meet its own, the climber over the crawlers and a lower crawler over a
higher one, and a shared cell shows the slot of the lowest-numbered sprite
that covers it, which holds all of them. The allocation stays nine slots per
sprite and seven sprites; a sprite's unused slots are the cells another
sprite owns. A sprite whose position, frame, mirroring, camera column,
twinkle phase, the map's change counter and the position, frame, mirroring and
relative index of every neighbor match the previous frame uploads nothing, and
the cells it covers keep their numbers, so `gtile_set()` marks nothing. The far ridge behind the
action is a one-pixel contour, so an item or sprite cell loses at most a
stroke of it and no tile needs a variant per background. World cells are
chosen so that columns look alike: flat rock, the sky and the abyss are
single tile numbers, and only edges (ledges, pits, pillars, the contour)
change as the view moves.

Scroll cost. The panel has hardware scroll in one direction only, so the
camera moves by whole tile columns and the renderer sends only the cells
whose tile number changed, one `LCDG_TILEWIN` per run. Computed on the host
model for every one-column scroll of the four levels with the sprites out of
view: the mean step changes 68 (`FOOTFALL`), 94, 85 and 134 (`SUMMIT`) tiles,
the worst 85, 125, 102 and 166 tiles with 65, 109, 69 and 149 windows. At
123 microseconds a tile and 7 a window (`docs/research/picocalc-calculator-and-gui.md`)
the worst step is 21.5 ms of bus time. The camera follows with a dead zone,
so a column scrolls at most once per 3.2 ticks at top speed. The first
frame of a level repaints all 1,600 cells (about 197 ms predicted). System-call
overhead per window (two calls) is unmeasured.

Levels. A level is a list of five-byte commands (rock rectangle, ledge run,
spike run, shard run, flag, goal, crawler corridor, start) decoded into a
200 by 32 column-major map: 470 bytes of data for the four levels. The logic
test proves each level completable by a weighted A* search over the player's
inputs (a state that touches a spike or leaves through a pit is dropped) and
replays the winning inputs through `rg_tick()`; the routes found take
10, 14, 8 and 18 seconds against par times of 40, 60, 45 and 70, which feed
the time bonus. Crawlers are not in the search; the level rules require each
corridor to have floor and three rows of headroom, so a jump clears or
stomps it.

Sizes, from `arm-none-eabi-size` and `ls` on the toolchain of this tree at
`-Os`: `ridge.o` is 10,145 bytes of text and 11,452 of bss (the map and the
game state, `struct rg_game`, 6,796 bytes; the view with its tile map,
frame buffer and sprite signatures, 4,500); the standalone a.out is 10,876
bytes (text 10,832, data 4, bss 11,468). Inside `gamebox` it adds 10,224 raw
bytes (23,920 to 34,144) and 8,354 packed bytes (21,660 to 30,014). The survey's estimate was 5 to 6 KB
of engine text plus 4 KB of tile data; the tiles are generated, the sprites
(1,632 bytes of strings) and the font (287 bytes) are the stored art, and
the HUD, banners, compositing and front end made the code larger than the
estimate.

Open points. Board behavior is unmeasured: the bus time of a scroll step,
the keyboard controller's repeat rate and whether a release event always
follows a press (a lost event clears the held keys, and the next hold event
sets them again), and the frame rate with all sprites in view. The game
ends when `LCDIOC_GETKEY` fails, which happens when another process changes
the tty mode.
