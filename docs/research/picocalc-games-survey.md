# PicoCalc games: source survey and game plan

Status: design note, 2026-11-05, with an implementation record at the end
(2026-11-06). Of the games it plans, rows 1 to 4 are implemented: `games/fourfall`,
the Tatham text batch (`games/puzzlebox`), the hexagonal mode of `games/bubble`
and `games/coil`. Every
figure carries one of three marks: measured (a named command produced it for a
named artifact), documented (a file or specification states it), or estimate
(derived here, with the derivation). Board behavior is not measured anywhere in
this note.

## Result

- The tree ships three original ASCII games (`fifteen`, `keen`, `bubble`) in
  one multicall a.out, `games/gamebox`, which measures 12,008 bytes of text,
  208 of data and 1,996 of bss. Every candidate below competes for the roughly
  28 KB that the 40 KB image guideline for a multicall box leaves
  (`sys/arch/rp2040/doc/STORAGE.md`, "The multicall executables").
- An owner repository (private) once assumed to hold Simon Tatham's collection
  instead holds the owner's bubble-shooter work, and its `pb_core` C library carries
  `SPDX-License-Identifier: BSD-3-Clause` in every source header at commit
  `b45e950`. The port's earlier note (`sys/arch/rp2040/doc/research/games.md`)
  calls that code "GPL or unlicensed"; the headers contradict it, and the repo
  has no `LICENSE` file at any commit. The owner decides the license before any
  import (see "License blockers").
- Tatham's collection (MIT, 40 game definitions, fetched from the upstream
  site) ports in two ways. The collection's own frontend API needs the
  16.5 KB `midend.o` plus 8.3 KB of shared support: a lone puzzle reaches
  an estimated 41-47 KB of a.out, past the box guideline, and wants the pixel
  device. A logic-only text front end that includes the puzzle source and
  drives its `new_game_desc`, `interpret_move` and `execute_move` directly
  drops the midend and adds an estimated 10.2-15.7 KB of text for the first
  puzzle (a.out about 18-24 KB with the 7.9 KB libc floor). Recommendation: the logic-only route for text mode, the frontend API
  only after `/dev/lcdgfx` exists.
- `games/bubble` is a shooter with wall bounces, three-or-more matching and
  ceiling anchoring on an 8-column square grid. It lacks the hexagonal row
  offset and the descending ceiling that define the genre, so a hexagonal mode
  is an upgrade of that file, not a new program.
- Blockers: `tftetris.c` (GPLv2 or later, with a third-party copyright line)
  and the Keen Dreams source (GPLv2 or later) are reference-only. `rustykeen`
  is dual-licensed MIT or GPL-2.0-only and is Rust. `flappy.c` is a Flappy
  Bird imitation whose art and name stay out of the port. No candidate needs
  new third-party code to ship the first three games.
- The port builds no game named after, and no character, art, level or trade
  dress of, Tetris, Puzzle Bobble, Zuma, Commander Keen or Keen Dreams. It
  builds original games in those genres under original names.

## Evidence base

- Tree: worktree branch `picocalc/calculator-and-games`, HEAD
  `076675a87decc6705ce991f364fb33273f438607`, clean. The calculator and GUI
  design note this plan depends on
  (`docs/research/picocalc-calculator-and-gui.md`) is not in that checkout; it
  lives in the sibling worktree `picocalc-lcd-keyboard-console` (branch
  `picocalc/lcd-keyboard-console`, HEAD `98cd07b2`). Facts taken from it are
  cited as "the GUI note".
- Tatham collection: the tarball named in `games.md`
  (a locally downloaded `puzzles-20251220.ecb576f.tar.gz`) no longer exists. The
  current upstream tarball `https://www.chiark.greenend.org.uk/~sgtatham/puzzles/puzzles.tar.gz`
  was fetched on 2026-11-05 and unpacks to `puzzles-20260923.616da16`;
  SHA-256 of the tarball `cc419c8060b4e22be398aa03e30651fbce146eee510645c303fb6f5f1b77b78e`.
  Its `LICENCE` was read: MIT, "copyright (c) 2004-2024 Simon Tatham" plus
  named contributors. Nothing downloaded was executed.
- Object sizes: `arm-none-eabi-gcc` 16.2.0, `-Os -mcpu=cortex-m0plus -mthumb
  -w`, compile only, `arm-none-eabi-size` text column (includes read-only
  data). The three shipped games compile with the tree's include directory
  and `-Wall -Wextra`. These are cross-compile object sizes: they establish
  compilation and size, not linkage against the DiscoBSD libc, and not
  behavior.
- Linked bound: the midend route was linked once with a stub frontend against
  newlib-nano (`--gc-sections`): `fifteen` 49,028 bytes of text, `samegame`
  50,924. Newlib-nano's stdio and soft-float library inflate both; the
  figures bound the DiscoBSD result from above and do not predict it.
- Owner repositories: `gh repo list Oichkatzelesfrettschen --limit 300` (300
  entries) and `gh api repos/.../contents` on each candidate, 2026-11-05.
  The list view reports no license for any entry; the per-repo API reports
  the SPDX ids used below.
- Platform: `games/gametty.h`, the three games and their tests, the GUI note
  (panel, glyph bank, `/dev/lcdgfx`, keyboard) and
  `docs/research/c17-shipped-resource-baseline.json` (gamebox record).

## 1. Platform budget

| Fact | Value | Mark and source |
| --- | --- | --- |
| Display | 53x32 cells of 6x10 pixels on a 320x320 panel; xterm-basic escapes; foreground 16 colors, background 8 colors | documented, GUI note section 4 |
| Cell redraw cost | 115 us per cell; full screen of 1,696 cells 195 ms | documented, GUI note (derived from SPI timing) |
| Square block on the text console | 2 columns by 1 row = 12x10 pixels (1.2:1) | derived |
| Keyboard | arrows, Enter, Esc, F1-F10, letters; auto-repeat arrives as repeated presses; no key-up; at most one event per 32 ms (about 31 per second) | documented, GUI note section 4 |
| Process window | one flat 144 KB for text, data, bss and stack | documented, `AGENTS.md` |
| Multicall box image | stays near 40 KB; `games/gamebox` raw 12,248 bytes, packed 10,835 (12 blocks) | documented guideline; measured gamebox record |
| Root free blocks | 270 in the `full` profile, 445 in `base`; 33 free inodes in `full`; the `games` closure costs 14 blocks and 2 inodes | documented, `PROFILES.md` |
| Float | no FPU; the boot ROM supplies `__aeabi_d*` and `__aeabi_f*` through `lib/libc/arm/gen/rom_float_*.S`; the existing games use none | documented, GUI note section 1 and `games.md` |
| Timers and input waits | `select`, `setitimer`, `gettimeofday` have syscall numbers (`include/syscall.h` 93, 83, 116); `FIONREAD` exists (`include/sys/ioctl.h` line 223); `games/atc` uses `setitimer`. No shipped game waits with a timeout | documented; behavior on the LCD console not measured |
| `rand()` | `lib/libc/compat/rand.c`: 32-bit LCG, returns `(randx >> 16) & 0x7fff` | documented; low output bits are the weak ones |
| Curses | `lib/libcurses` honors `TIOCGWINSZ` (`cr_tty.c` lines 123-142); the archive is 60,052 bytes (GUI note), so the shipped games use raw ANSI through `games/gametty.h` instead | documented |

Arrow keys arrive as `ESC [ A..D`; `gtty_getkey()` folds them to
`h j k l`. It reads the byte after `ESC` with a blocking `read`, so a bare Esc
press blocks until the next key, and F-key sequences (`ESC O P`, `ESC [ 15 ~`)
leave stray bytes. A timed game uses `FIONREAD` to tell a bare Esc from a
sequence and parses the F-key forms itself; `gametty.h` gains that parser once
for all games.

## 2. What the tree already ships

Read from `games/*/` at the HEAD above.

| Game | What it is | Source | Object text / bss (measured) | Tests |
| --- | --- | --- | --- | --- |
| `fifteen` | 4x4 sliding-tile puzzle; the blank moves with the arrows; shuffles by 200 legal slides | 179 lines | 858 / 78 | `tests/play.py`: pty replay of the inverse shuffle (`GAMEBOX_TEST` prints the shuffle) |
| `keen` | 3x3 to 6x6 Latin-square cage puzzle with a uniqueness-checked generator; original code, Tatham's `keen.c` and the Keen apps consulted for ideas only | 700 lines | 2,909 / 1,660 | pty play, 4 fixtures, 80 unchecked and 160 checked generator cases against an independent solver (`tests/keensolve.c`) |
| `bubble` | color-matching shooter: 8 columns by 10 rows, 4 colors, integer DDA aim with wall bounce and a preview path, 4-way flood match of 3 or more, ceiling anchoring, no descending ceiling, no hexagonal offset | 502 lines | 2,319 / 1,952 | `tests/play.py`: three matching shots pop, score 30 |

Conventions every new game follows:

- One `games/<name>/` directory with `<name>.c`, a `Makefile` that builds the
  target a.out and a `host` target (`cc -std=c99 -Wall -Wextra -Werror
  -DHOSTBUILD`), a `test` target and `tests/play.py`.
- Raw mode and keys through `games/gametty.h` (sgtty on the target, termios in
  `HOSTBUILD`), no `float`, no curses, 7-bit ASCII glyphs.
- `main` is the only global the multicall link keeps; `games/gamebox/Makefile`
  lists the tool in `TOOLS` and `gamebox.c.in` generates the dispatch table.
  Adding a game also adds its hard link to the `games` closure in
  `distrib/rp2040/mi.rp2040` and regenerates the shipped-resource baseline.
- `GAMEBOX_TEST` makes the game deterministic for the pty test regardless of
  the host `rand()`.
- A new file written for this port carries `Copyright (c) <year> DiscoBSD` and
  the ISC notice; `NOTICE` section 1 already names the three games as ISC.

## 3. Candidate sources

### 3.1 Simon Tatham's Portable Puzzle Collection

| Item | Value |
| --- | --- |
| Location | `https://www.chiark.greenend.org.uk/~sgtatham/puzzles/` (source tarball above); git: `https://git.tartarus.org/simon/puzzles.git` (HTTP 403 to a plain `curl` on 2026-11-05) |
| Revision | `puzzles-20260923.616da16` |
| License | MIT; the notice in `LICENCE` travels with any copied file; `NOTICE` gains a section |
| Language | C (C99 with `bool`, `stdint`); 125 top-level entries, 74 `.c` files; 40 `thegame` definitions (39 puzzles and `nullgame`); `CMakeLists.txt` links one common library (`combi`, `divvy`, `dsf`, `findloop`, `grid`, `latin`, `loopgen`, `matching`, `midend`, `misc`, `random`, `sort`, `tdq`, `tree234` and others) into every puzzle |
| Float use | `float` in the color table, animation and flash times, and `double` in `midend_size`'s pixel ratio (`puzzles.h`, `midend.c`); logic code is integer |
| Portability to this tree | Needs `<stdbool.h>`, `<stdint.h>`, `snprintf`/`vsnprintf`, `qsort`, `atof` in `midend.c`; the tree's libc coverage for each is a compile-time check before import, not tested here |

Measured object text (bytes), all compiled alone with the flags above. "Deps"
lists the support headers a puzzle's own `#include` lines name beyond
`puzzles.h` (the common library supplies the rest at link time); shared support measures
`random.o` 1,352, `malloc.o` 191, `dsf.o` 2,158, `misc.o` 3,200,
`drawing.o` 1,435, `tree234.o` 4,076, `latin.o` 3,808, `matching.o` 1,109,
`midend.o` 16,519. UI fit: T = fits the text grid (2 columns per cell,
26 columns by 28 rows), G = needs line or arrow glyphs from the glyph bank,
P = pixel device only.

| Puzzle | Lines | Object text | Deps | UI fit | Note |
| --- | --- | --- | --- | --- | --- |
| `fifteen` | 1,271 | 5,603 | none | T | already shipped as an original program |
| `sixteen` | 1,221 | 6,106 | none | T | sliding rows and columns |
| `twiddle` | 1,315 | 7,192 | none | T | rotate 2x2 blocks |
| `flip` | 1,351 | 6,749 | `tree234` | T | |
| `flood` | 1,394 | 6,494 | none | T | six colors map onto the eight background colors; lowest cost |
| `samegame` | 1,686 | 7,632 | none | T | colored groups; cursor plus Enter |
| `pegs` | 1,397 | 6,447 | `tree234` | T | |
| `mosaic` | 1,631 | 6,954 | none | T | |
| `untangle` | 2,072 | 8,087 | `tree234` | P | planar graph drawn with free lines |
| `guess` | 1,576 | 8,668 | none | T | row-by-row code breaker |
| `blackbox` | 1,587 | 8,878 | none | T | |
| `range` | 1,891 | 8,926 | none | T | |
| `unruly` | 2,186 | 9,211 | none | T | binary grid, two colors |
| `palisade` | 1,581 | 9,359 | none | G | wall segments between cells |
| `singles` | 2,070 | 9,480 | `latin` | T | |
| `towers` | 2,268 | 10,398 | `latin` | T | |
| `netslide` | 1,889 | 10,651 | `tree234` | G | |
| `filling` | 2,235 | 11,101 | none | T | |
| `unequal` | 2,480 | 11,129 | `latin` | G | inequality signs between cells |
| `pattern` | 2,376 | 11,230 | none | T | clue margins limit the grid to about 15x15 |
| `inertia` | 2,246 | 11,285 | none | T | |
| `slant` | 2,442 | 11,212 | none | G | `/` and `\` fit ASCII exactly |
| `lightup` | 2,489 | 11,584 | none | T | |
| `keen` | 2,687 | 12,276 | `latin` | T | shipped as an original program |
| `cube` | 1,787 | 12,460 | none | P | rolling polyhedron |
| `tents` | 2,764 | 12,541 | `matching` | T | |
| `signpost` | 2,568 | 12,992 | none | G | arrows |
| `magnets` | 2,697 | 13,440 | none | T | |
| `pearl` | 3,058 | 13,809 | `loopgen`, `grid` | G | loops |
| `dominosa` | 3,578 | 14,449 | none | T | |
| `rect` | 3,025 | 14,698 | none | T | |
| `map` | 3,474 | 15,327 | none | T | four-color maps need irregular regions |
| `undead` | 3,049 | 16,150 | none | T | |
| `net` | 3,357 | 16,546 | `tree234` | G | |
| `tracks` | 3,159 | 17,988 | none | G | |
| `bridges` | 3,350 | 18,027 | none | G | |
| `galaxies` | 4,485 | 18,666 | none | G | |
| `solo` | 5,789 | 26,668 | none | T | Sudoku and variants; 9x9 fits exactly |
| `mines` | 5,899 | 27,746 | `tree234` | T | |
| `loopy` | 3,912 | 15,612 | `grid`, `loopgen`, `tree234` | P | |

Already in the tree: `keen` and `fifteen`, both original code (their headers
say so). No import repeats them.

Two porting routes:

| Route | What it reuses | Shared cost | One puzzle added | Constraint |
| --- | --- | --- | --- | --- |
| Frontend API: implement `drawing_api` (`puzzles.h`, version 1: `draw_text`, `draw_rect`, `draw_line`, `draw_polygon`, `draw_circle`, `clip`, `blitter_*`, `status_bar`) plus `frontend_default_colour`, `get_random_seed`, `activate_timer` and a handful of print stubs, over `/dev/lcdgfx` | the whole collection unchanged, `midend.c` included | `midend` 16,519 + `misc` 3,200 + `random` 1,352 + `malloc` 191 + `dsf` 2,158 + `drawing` 1,435 = 24,855 bytes of object text, plus an estimated 3 KB frontend and the 7.9 KB libc floor (GUI note) | 5,603 (`fifteen`) to 26,668 (`solo`) | a lone small puzzle is an estimated 41-47 KB a.out; the 40 KB box guideline excludes a multi-puzzle box; the pixel route needs the pixel device; the frontend maps pixel coordinates, so a character-cell frontend cannot reuse it without a raster layer |
| Logic-only text front end: a program per puzzle `#include`s the puzzle `.c` (made `static` through a macro on `thegame`) and calls `new_game_desc`, `new_game`, `interpret_move`, `execute_move` and `game_status` directly; one common text UI draws the `game_state` | the generators, solvers and move logic | `misc` 3,200 + `random` 1,352 + `malloc` 191 = 4,743 bytes (the undefined symbols of `flood`, `samegame`, `unruly` and `lightup` resolve to these three files plus `combi` 292 for `lightup`; `dsf` 2,158 joins for a puzzle that references it), an estimated 1.5 KB common UI | the puzzle's function text minus its presentation functions: `flood` 3,132, `twiddle` 3,098, `samegame` 4,528, `range` 5,490, `unruly` 5,696, `singles` 6,136, `lightup` 7,880 (measured symbol sizes; presentation functions identified by name pattern, so the split is an estimate), plus an estimated 0.8-1.2 KB of per-puzzle drawing | the puzzle's private `game_state` is read directly, so each UI is bound to one revision of that puzzle; unreferenced callbacks drop only if `thegame` is static |

The logic route keeps `random.c`, a SHA-1 based generator, so a seed and a
parameter string produce the same puzzle on the host and the board. The
existing games depend on the platform `rand()`, which is why they carry a
`GAMEBOX_TEST` hook; a Tatham puzzle needs none for generation.

### 3.2 Keen derivatives

| Source | Commit | License (file read) | Language | Size | Disposition |
| --- | --- | --- | --- | --- | --- |
| `KeenClassik` (public) | `5315d632` | `LICENSE`: MIT, holders Simon Tatham, a 2016 contributor, and the owner's port contributors; `THIRD_PARTY_LICENSES` lists Tatham's `keen.c`, `latin.c`, `dsf.c`, `random.c`, `tree234.c` | C with JNI and Kotlin | 660 KB | `app/src/main/jni` holds a Tatham-derived `keen.c` of 302 lines split from `keen_generate.c`; Android-specific; not imported. `games/keen` already covers 3x3-6x6 |
| an owner repository (private, extended Keen port) | -- | `LICENSE`: MIT, same holders as `KeenClassik` | C, JNI, Kotlin | 9.5 MB | extended modes (to 16x16, DLX solver, hints); too large for the window; not imported |
| `rustykeen` (public) | `05949df2` | `LICENSE` is the GPL-2.0 text (340 lines); `LICENSE-MIT` is MIT under the owner's name; `README.md` states dual MIT or GPL-2.0-only | Rust | 12 MB | the toolchain has no Rust target for this OS; useful only as a second solver oracle on the host |
| an owner repository (private, Android Keen port) | -- | `LICENSE`: MIT under the owner's name; its `README` says it modifies some of Simon Tatham's level generation | C, Android | 6.6 MB | the license omits Tatham's notice that MIT requires to travel with derived code; not imported, and a reason to take Tatham code from the upstream tarball only |
| an owner repository (private, Compose Keen app) | -- | `LICENSE`: MIT | C, Compose | 182 KB | not reviewed beyond the license; not imported |

### 3.3 Other owner repositories matching the name patterns

Patterns: keen, puzzle, bubble, bobble, zuma, tetris, block, game, plus
marble, arcade, pinball. No repository name contains `bubble`, `bobble`,
`zuma`, `tetris` or `block`.

| Repository | License (API) | Content | Disposition |
| --- | --- | --- | --- |
| `puzzles` (private, 556 KB) | none at HEAD `098d275f`; `gororoba_puzzle/Cargo.toml` declares BSD-3-Clause; every `pb_core/src/core/*.c` at `b45e950` carries `SPDX-License-Identifier: BSD-3-Clause` | "Collection of Puzzle Bobble clone implementations": `pb_core` (C, hexagonal board, shot physics, trajectory, fixed-point math, replay), removed from HEAD by `3c544ef` in favor of a Rust workspace | owner-authored (7 commits, all by the owner's identities); behavior and test oracle for the shooter upgrade. Import only after a `LICENSE` file exists. `pb_core` objects at `b45e950`: board 2,044, hex 1,796, shot 3,816, trajectory 2,328, game 3,900, rng 740 bytes of text (measured, Thumb-1) |
| `gororoba_app` (private) | Apache-2.0 and MIT | Rust workspace on Bevy; "four physics games" | not portable; no use |
| `emilia-pinball` | GPL-2.0 (`COPYING`) | mirror of a C++ 3D pinball | not importable; no use |
| `SpaceCadetPinball` (fork) | MIT (code) | decompilation of a Microsoft game, resources not included | trade dress and derived binary assets; no use |
| `reactball` | none | Win32 pinball for ReactOS | no use |

### 3.4 `games/bubble` against Puzzle Bobble

`bubble` is a Puzzle Bobble-style shooter at the level of the core loop:
aim by integer slope table (13 angles), reflect off the side walls, stop below
the first occupied cell, remove a 4-connected group of 3 or more, then remove
whatever is no longer joined to row 0. It diverges in four ways: columns and
rows form a square grid with 4-neighbor adjacency, rows do not stagger,
the ceiling never descends, and a lost game is a column reaching the launcher.
A hexagonal mode adds a staggered row (even rows shifted one cell), six
neighbors, a descending ceiling after a fixed number of shots without a pop,
and a preview that follows the bounce. The mode adds an estimated 1.2 KB of
text over the 2,319 measured for the file. `pb_core` is a test oracle for the
neighbor and orphan rules; wholesale import costs the measured 9,984 bytes of
`pb_board`, `pb_hex`, `pb_shot` and `pb_trajectory`, which is four times the
shooter itself.

### 3.5 The RetroBSD gpanel examples

Location `legacy/non-arm/mips-pic32/share/examples/gpanel/`, imported from the
RetroBSD repository (revision 506, 2019-02-17) by commit `1f040555`
(2020-10-31). The archive builds nothing (`legacy/non-arm/README.md`).

| File | Bytes / lines | SHA-256 | License | Use |
| --- | --- | --- | --- | --- |
| `flappy.c` | 9,908 / 439 | `efaa1e6f0014dbcb67868beab536f992572142e8666f6b28cea02663c07e458c` | no header (grep for copyright, license and author markers finds none); governed by the repository `LICENSE` (BSD 3-Clause, `NOTICE` section 1) | A Flappy Bird imitation with a bird drawn as a rectangle stack. Take technique only: a fixed 50 ms redraw interval, a pillar and gap schedule, a high-score file. Take no art, name or layout. Needs `libgpanel` ioctls that `/dev/lcdgfx` replaces with different commands |
| `tft.c` | 3,297 / 193 | `846d7e8ba828ecbcd6d2007be7e7552cd3b38c270e72265e2dba7721259480b2` | same | Wrapper functions (`pixel`, `lineh`, `linev`, `box`) over the panel ioctls; shows the call shape the `/dev/lcdgfx` test program replaces |
| `speed.c` | 2,959 / 136 | `65b86c9c93eb2980a096716927617e519fca512cde0e4726dcde839c2db7ce52` | same | Times 100,000 pixels, 3,000 lines, 1,000 rectangles, 1,000 circles and 10,000 characters; the template for the `/dev/lcdgfx` speed benchmark |
| `tftetris.c` | 21,451 / 922 | `27aaba14c203ed90763e49707216d757ebe16f41127c6c88b1dc069e95eb4f54` | GPLv2 or later; "Copyright (C) 2015 Joao Andre Esteves Vilaca", adapted by Alexey Frunze (lines 2-6, 10-29) | Not importable and not copied. Behavior only, through the clean-room process in `falling-block-clean-room-spec.md` |

The three files without a header carry the repository's BSD 3-Clause by the
owner's direction; their authorship at RetroBSD is unverified, which is a
reason to import no more than ideas from them.

### 3.6 Keen Dreams

`github.com/keendreams/keen` at `4348b4719be87a0b873f5f4f77b4de82ddf7050c`
(default branch `master`, last push 2014-11-12). License GPLv2 or later
(`LICENSE`; the README says the same and excludes the game data files). Code
is C and assembly for Borland C++ 2.0. The port takes technique only, read
from `id_rf.c`'s header comment: a refresh manager that marks changed tiles
and redraws only those, with a full redraw when the view scrolls by more than
one tile. That idea is generic and the GUI note already derives the cost
table from panel timing. The port copies no Keen Dreams code, data, art,
level, sound, name or character, and does not recreate Commander Keen.

### 3.7 Boundary on names and trade dress

The port builds original games in these genres under original names, with
original art authored for 6x10 glyphs and 8x8 tiles:

- falling-block: no Tetris name, no Tetris piece names or colors treated as a
  brand, no Tetris theme;
- bubble shooter: no Puzzle Bobble or Bust-a-Move name, characters, levels or
  sounds;
- chain shooter: no Zuma name, frog, idol or level layout;
- platformer: no Commander Keen name, character, enemy, level or tile art.

Genre mechanics (seven tetrominoes, 3-match popping, a ball chain along a
path, tile platforming) are not protected expression and are described in the
port's own words.

## 4. Game plan

Ordered by dependency and by cost. Sizes are estimates unless marked;
"text" is object text, the quantity that adds to `gamebox` (compare the
measured 12,008).

| Order | Game (working name) | Genre and mechanics | UI mode | Source basis | Size estimate | Dependency | Acceptance test |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `fourfall` (alternatives `cairn`, `plumb`) | falling-block: 10x20 board with two hidden spawn rows, seven four-cell pieces, 7-bag, rotation with wall and floor kicks, gravity by level, line clears scored by triangular numbers; specified in `falling-block-clean-room-spec.md` | raw ANSI text: each cell is two columns with a background color (41-47 map one piece each); later a glyph-bank bevel | fresh; clean-room from the written spec | 2.5-3.0 KB text, 0.5 KB bss, about 480 lines (bubble: 2,319 text for 502 lines; keen: 2,909 for 700) | `select` or a timed read on the LCD console (not measured); no pixel device | host logic test over the 132 exact vectors in the spec plus the property checks; `tests/play.py` pty smoke tests with `GAMEBOX_TEST=O...`: five pieces clear two lines for score 500, eleven drops end the game at score 220 |
| 2 | Tatham logic-only text batch: `flood`, `samegame`, `unruly`, `lightup` (a program each, one shared text UI) | puzzles as upstream | raw ANSI text, 2 columns per cell, 8 background colors | MIT import of the puzzle source with the notice kept in the file and in `NOTICE`; the text UI is fresh | shared 4.7 KB + 0.3 KB + 1.5 KB, then 3.1, 4.5, 5.7 and 7.9 KB of logic plus about 1 KB of UI each: about 32 KB of text for the four; split into two boxes if the 40 KB guideline binds | upstream tarball pinned by SHA-256; `bool`, `stdint` and `snprintf` in the libc | host test: generate with a fixed seed string, run the puzzle's own `solve_game` move string through `execute_move`, require `game_status` solved; pty smoke test enters the solution moves printed under `GAMEBOX_TEST` |
| 3 | `bubble` hexagonal mode (working name `hexfoam`, flag in the existing program) | staggered rows, six neighbors, ceiling descends after eight shots without a pop, bounce preview | raw ANSI text; round-bubble glyph later | fresh; `pb_core` hex and orphan rules as test oracle (license decided first) | +1.2 KB text over bubble's 2,319 | none for text mode | extend `games/bubble/tests/play.py` with a hex-mode pop and an orphan-drop case; host test cross-checks neighbor and orphan sets against vectors derived from `pb_core` |
| 4 | `coil` (alternatives `rillrun`, `meander`) | chain shooter: marbles advance along a fixed spiral path of about 150 cells; a launcher in the center turns in 16 steps and fires along a straight integer line; the shot inserts into the chain at the nearest index; 3 or more same-color neighbors clear; the chain wins at an empty path and loses at the path end | raw ANSI text at one cell per step; glyph-bank round marbles later; pixel/tile at sub-cell speed on `/dev/lcdgfx` | fresh | 3.5-4.5 KB text, 0.9 KB bss (path, chain, shot) | the glyph bank for the round-marble skin only | host logic test: chain insertion index, gap closing, match and rejoin chain reaction, path-end loss, as golden vectors; pty smoke test with `GAMEBOX_TEST` fixing colors and spawn |
| 5 | Tatham second batch: `towers`, `singles`, `slant` (glyph), `solo` | as upstream | text; `slant` after the glyph bank | MIT import | `latin.o` 3.8 KB shared by `towers` and `singles`; `solo` 26.7 KB object text, own box | glyph bank for `slant`; a second box image for `solo` | same solver-replay test as the first batch |
| 6 | Tatham frontend-API box over `/dev/lcdgfx` (a handful of puzzles, pixel rendering) | as upstream, with real line art | pixel device | MIT import; the `drawing_api` frontend is fresh | 24.9 KB shared object text + about 3 KB frontend + 5.6-18 KB per puzzle | `/dev/lcdgfx` with `LCDG_WINDOW` text and `LCDG_FILL`; palette; the 40 KB box guideline lifted for this box | host test with a recording `drawing_api` that hashes the draw stream for a fixed seed and key sequence |
| 7 | `ridge` (alternatives `tarn`, `skerry`) | tile platformer, flip-screen rooms of 38x36 tiles of 8x8 pixels, integer jump arc, tile collision, collectibles and a goal per room; no scrolling (a full 320x160 viewport repaint costs 98 ms, GUI note) | pixel/tile on `/dev/lcdgfx` | fresh engine, original tile art authored in this tree, levels authored in this tree; technique from `id_rf.c` only | 5-6 KB engine text, 4 KB tile data (128 tiles x 32 bytes), about 2 KB of run-length coded rooms for ten rooms | `/dev/lcdgfx` with `LCDG_TILESET` and `LCDG_TILEWIN`, and the raw key event ioctl or a held-key inference measured on the board | host logic test for the integer physics (a golden table of jump apex height and horizontal reach) and the tile collision matrix; a device-double test that runs the game on the host against a recording panel and compares the hash of the emitted command stream |

Notes on the ordering:

- `fourfall` goes first because it has no dependency on unbuilt kernel work,
  its spec exists, and it forces the shared timed-input path (select with a
  timeout, bare-Esc handling) that the shooters and the platformer need.
- The Tatham logic-only batch follows because it adds the most distinct games
  per byte with code that exists and is tested upstream; its risk is the libc
  surface, which a compile-only import test resolves before any UI work.
- `ridge` is last. It depends on the pixel device, the tile atlas and a key
  model; the GUI note's staged plan puts those after the glyph bank.
- Every row ends in a host-side logic test plus a pty smoke test like
  `games/keen/tests/play.py`. Pixel programs replace the pty test with the
  recorded-command-stream hash because a pty carries no panel.
- No row reaches a board without the repository's exact opt-in; board timing
  of redraw and key rates stays a measurement the plan predicts and the board
  confirms.

### Held-key limits on action games

The keyboard reports repeats as new presses and no key-up. A falling-block
game needs only discrete moves and a soft drop per press, so it is
unaffected. A shooter needs aim steps per press, also unaffected. A platformer
needs held-direction movement; two designs satisfy it without key-up. The
first latches direction: a direction key sets the heading and a stop key or
the opposite key clears it. The second infers release when repeats stop for
longer than the controller's repeat interval, which the GUI note does not
state; measuring that interval on the board is a condition of the second
design.

## 5. License blockers

| Item | Blocker | Action |
| --- | --- | --- |
| `legacy/.../gpanel/tftetris.c` | GPLv2 or later with a third-party copyright line; the tree is BSD/ISC (`NOTICE`: "None is under the GNU General Public License") | no import; the analyst reads it and writes the behavioral spec; the implementer never sees it; a mechanical similarity review follows |
| Keen Dreams source | GPLv2 or later | technique only |
| `rustykeen` | GPL-2.0-only alternative; MIT alternative under a different holder name | use the MIT file only if Rust code were ever needed; none is |
| owner bubble-shooter repository (private) `pb_core` | no `LICENSE` file; per-file SPDX BSD-3-Clause; the earlier port note says GPL or unlicensed | owner adds a `LICENSE` and confirms the holder line before any import; until then the shooter upgrade is written fresh |
| the private Android Keen port | MIT file omits the Tatham notice | do not use as a source of Tatham code |
| Tatham tarball | MIT; the notice must accompany copied files | add a `NOTICE` section; keep each copied file's header |
| `flappy.c`, `tft.c`, `speed.c` | no header; authorship at RetroBSD unverified | treated under `NOTICE` section 1 by the owner's direction; port technique and measurements, not text |
| Any title in the genre | trade dress | original names, art and levels (section 3.7) |

## 6. Risks and open decisions

- The 40 KB box guideline limits how many Tatham puzzles share one a.out. The
  split between `gamebox` and a second box is the owner's decision once the
  first batch is measured.
- Timed input on the LCD console is unmeasured: `select(2)` on the console tty
  (`ttyselect`) and the grain of `gettimeofday` decide whether a 64 ms gravity
  interval holds. A measured interval above 20 ms of jitter falsifies the
  level 14 gravity value in the spec.
- Whether the libc provides every function `midend.c` and `misc.c` call is not
  tested; the logic-only route avoids `midend.c` but still needs `misc.c`,
  `random.c` and `dsf.c` to compile under `-Wall -Wextra -Werror`.
- The `pb_core` license and the three header-less gpanel files need the
  owner's answer; this note does not resolve them.
- Checks not run: any link against the DiscoBSD libc, any run on the board,
  Renode or qemu-user, any compile of a Tatham puzzle under the tree's own
  flags, any measurement of `select` on the console tty. The source-size
  estimates for the logic-only route rest on symbol-name partitioning of
  object files and are not link results.
- `docs/INDEX.md` does not list this note or the spec; the index update
  belongs in the commit that lands them.

## Implementation record: rows 2, 3 and 4

Row 3, `bubble -x`: written fresh (the `pb_core` license stays an open owner
decision and its source was not read). Staggered rows with seven bubbles in
the shifted ones, six neighbors, a descent after eight shots without a pop
that flips the stagger parity, loss when a bubble reaches the bottom row.
`bubble.o` text 2,319 to 2,866 bytes (estimate was +1.2 KB). The host test
(`games/bubble/tests/hex_test.c`) replays 111 vectors, among them the shot
path of every aim for both stagger parities against an independently coded
walk, instead of vectors derived from `pb_core`.

Row 4, `coil`: the specified chain shooter with a 150-cell spiral, 16 aim
directions, a straight integer shot line, insertion at the side of the hit
ball nearer the shot's last free cell, runs of three, gap closing with the
front part held, rejoin chain reactions at a combo that scores 10 per ball
times the combo, win at an empty path and loss at the last path cell. Original
name, glyphs and levels. `coil.o` text 4,993, bss 4,890 (estimate 3.5-4.5 KB
and 0.9 KB). 90 golden vectors, 200 random games, 13 pty cases.

Row 7, `ridge`: built as a side scroller instead of the flip-screen rooms the
row specified. The reason is the dirty-tile arithmetic of the GUI note: the
renderer sends only the cells whose tile number changed, so a one-column
scroll over mostly uniform rock and sky changes 68 to 134 tiles on average
(21.5 ms of predicted bus time at worst, `games/ridge/tests/render_test.c`),
against 98 ms for a full 40 by 20 repaint. Four levels in a five-byte
command format (470 bytes for all levels), tile art generated in
code, the sprites composited into reserved atlas slots, held keys from the raw
key events instead of an inferred release. `ridge.o` text is 10,153 bytes
(estimate 5 to 6 KB of engine text and 4 KB of tile data), bss 11,452. The
acceptance tests are the ones the row named in spirit: a golden table of jump
arcs and a collision matrix (`tests/logic_test.c`) and a model-panel test of
the emitted tile commands (`tests/render_test.c`), plus a search that proves
every level completable. Details: `sys/arch/rp2040/doc/research/games.md`.

Row 2, the Tatham batch. Source: `puzzles-20260923.616da16`, tarball SHA-256
`cc419c8060b4e22be398aa03e30651fbce146eee510645c303fb6f5f1b77b78e`
(fetched 2026-11-06 from
`https://www.chiark.greenend.org.uk/~sgtatham/puzzles/puzzles.tar.gz`, a
redirect to the dated name; the hash equals the one recorded above). Nine
files are imported unchanged with the collection's `LICENCE`
(`games/pzcommon/tatham`, per-file SHA-256 in `games/pzcommon/tatham.sha256`,
`NOTICE` section 17). The collection's files carry no per-file header. The
files are `puzzles.h`, `misc.c`, `random.c`, `malloc.c`, `combi.c` and the
four puzzles; `dsf.c`, `tdq.c` and `midend.c` are not needed. The text front
end is fresh (`games/pzcommon/pzfront.c`).

The estimate that four puzzles reach about 32 KB of text held: standalone
programs are 18,172 (`flood`), 17,320 (`samegame`), 20,692 (`unruly`) and
23,064 (`lightup`) bytes raw, 73 packed blocks together, and the box
`puzzlebox` measures 38,960 raw and 34,397 packed (35 blocks). Added to
`gamebox` the nine games would exceed the 40 KB guideline, so the puzzles
ship in a second box; the split is the owner's to review. The logic-only
route needed `-fwhole-program` to drop the collection's drawing code, since
a non-static puzzle table keeps every presentation function reachable.
Samegame has no solver, so its host test carries one (a depth-first search
over groups); the other three replay `solve_game`.

## Source index

- Tree: `games/{keen,fifteen,bubble,gamebox,gametty.h}`,
  `sys/arch/rp2040/doc/research/games.md`, `sys/arch/rp2040/doc/{STORAGE,PROFILES,TESTING}.md`,
  `docs/research/c17-shipped-resource-baseline.json`, `NOTICE`,
  `lib/libc/compat/rand.c`, `lib/libcurses/cr_tty.c`, `include/syscall.h`,
  `legacy/non-arm/mips-pic32/share/examples/gpanel/`.
- GUI note: `docs/research/picocalc-calculator-and-gui.md` in the
  `picocalc-lcd-keyboard-console` worktree.
- Owner repositories: the public `KeenClassik` and `rustykeen`, read through
  `gh api repos/Oichkatzelesfrettschen/<name>`, and three private owner
  repositories referred to by description only.
- Tatham: `https://www.chiark.greenend.org.uk/~sgtatham/puzzles/` and
  `puzzles.tar.gz` at the SHA-256 above.
- Keen Dreams: `https://github.com/keendreams/keen`.
