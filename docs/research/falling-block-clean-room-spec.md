# Falling-block game: clean-room behavioral specification

Status: specification, 2026-11-05, presentation and board size revised the
same day (12 by 22 well, glyph textures, plumb line, two-piece preview). The document has two parts separated by
the marker line `REVIEWER-ONLY BELOW`. Part 1 is the specification an
implementer builds from. Part 2 holds the analyst's notes and the
similarity-review checklist for the reviewer. Cut the file at the marker
before handing it to the implementer:

    sed '/REVIEWER-ONLY BELOW/,$d' falling-block-clean-room-spec.md

The implementer never opens `legacy/non-arm/mips-pic32/share/examples/gpanel/tftetris.c`
or any other falling-block source. Every figure in Part 1 is a design value
chosen here, with a stated reason where one exists; none is a measured board
result. Board behavior (timer grain, key rate, console redraw time) is not
measured anywhere in this document.

## Part 1: implementer specification

### Name and scope

The game is a one-player falling-block puzzle for the DiscoBSD RP2040 port
on the PicoCalc console (53 columns by 32 rows, 6x10-pixel cells, ASCII-only
Misc-Fixed font, ANSI escapes with bold, 8 background colors, 16 foreground
colors including the bright-black gray). Working name
`fourfall`. Alternatives: `cairn`, `plumb`. No trademark search was run on
any of the three. The game, its source, its screens and its documentation
must not use the name Tetris, the names of any commercial falling-block
product, their theme music, their logo or their trade dress.

The deliverable is a program `games/fourfall/fourfall.c` that follows the
conventions of `games/fifteen`, `games/keen` and `games/bubble`: raw terminal
mode through `games/gametty.h`, no floating point, no curses, 7-bit ASCII
output, a `host` target built with `-DHOSTBUILD`, a `test` target, and
`tests/play.py`. The words "must" and "should" carry their usual force.

### Board and coordinates

- The board has 24 rows and 12 columns. Row 0 is the top, column 0 the left.
  Rows 0 and 1 are hidden spawn rows; rows 2 to 23 are the 22 visible rows.
  The well is 12 by 22 visible cells, not the 10 by 20 of the common
  falling-block well; the size is a deliberate departure from that layout.
- A cell holds 0 (empty) or a piece type 1 to 7. Type numbers: 1 = I,
  2 = O, 3 = T, 4 = S, 5 = Z, 6 = J, 7 = L. The letters name the types in
  this document only and have no relation to any other program's tables.
- A piece state is `(type, rot, row, col)`: `rot` is 0 to 3, and `(row, col)`
  is the board position of the top-left corner of the piece's box. `col` may
  be negative and `row` may be above the board while the box overhangs.

### Pieces

Each type is a set of four cells inside a square box of side `n`. The
orientation-0 cell sets, as `(row, col)` offsets inside the box:

| Type | Box side n | Orientation-0 cells |
| --- | --- | --- |
| I | 4 | (1,3) (1,2) (1,1) (1,0) |
| O | 2 | (1,1) (0,0) (1,0) (0,1) |
| T | 3 | (1,2) (1,1) (1,0) (0,1) |
| S | 3 | (1,1) (1,0) (0,2) (0,1) |
| Z | 3 | (1,2) (1,1) (0,1) (0,0) |
| J | 3 | (1,2) (1,1) (1,0) (0,0) |
| L | 3 | (1,2) (1,1) (1,0) (0,2) |

Cells are listed last row first and right to left, and rotation carries that
order cell by cell, except O, whose four cells keep the order (1,1) (0,0)
(1,0) (0,1) in every orientation because O does not change under rotation.
Collision and drawing do not depend on the order; the order is fixed so that
the shape tables share no expressive enumeration with the reference
program, which a copying review checked.

Rotation rule. One clockwise quarter turn moves the cell at `(r, c)` to
`(c, n-1-r)` inside the same box. One counterclockwise quarter turn moves it
to `(n-1-c, r)`. Orientation `k` is orientation 0 turned clockwise `k` times.
All seven types have four orientations in the table of vectors (the O piece
repeats its single shape). The vector list "SHAPE" gives all 28 cell sets.

Glyphs and colors. By default a cell is drawn as a two-character glyph
texture of its piece type, with no hue: I `[]`, O `##`, T `<>`, S `//`,
Z `\\` (two backslashes), J `()`, L `{}` (vector list "GLYPH"). The seven
glyphs are pairwise different, and each is printable 7-bit ASCII. The falling
piece prints bold white (`ESC [ 1 ; 37 m`); a settled cell prints bright black
(`ESC [ 90 m`), the console's gray. Empty cells print two spaces. Option `-c`
(color) prints the same glyphs in a foreground hue per type, the piece
falling in bold: I = 37 (white), O = 34 (blue), T = 32 (green), S = 35
(magenta), Z = 36 (cyan), J = 31 (red), L = 33 (yellow); these are the
foreground forms of the background palette (47, 44, 42, 45, 46, 41, 43) a
copying review fixed. Option `-m` (monochrome letters) draws a cell as the
type letter twice (`II`, `OO`, ...) with no attributes, for a console or a
reader without attributes; `-m` wins when `-c` is also given. Color is never
the default, and no cell is a solid block of one color.

### Spawn, movement and collision

- Spawn state for a new piece: `rot` 0, `row` 0, and `col = (12 - n) / 2` with
  integer division, which centers the box of side `n` in the 12-column well:
  `col` 4 for types I, T, S, Z, J, L and `col` 5 for the O piece. The I box
  leaves four free columns on each side; a 3-wide box leaves four on the left
  and five on the right; the O box leaves five on each side. The SPAWN rows of
  the TOPOUT list confirm each.
- A state collides if any of its four cells is outside columns 0 to 11, above
  row 0, below row 23, or on a nonzero board cell. A cell in a hidden row is
  inside the board.
- A spawn that collides ends the game ("top-out"). A lock whose four cells all
  lie in hidden rows (rows 0 and 1) also ends the game ("lock-out").
- A move left or right changes `col` by one if the new state does not collide
  and is ignored otherwise.
- A move down changes `row` by one under the same condition. A piece is
  grounded when the move down would collide.

### Rotation with kicks

A rotation request carries a direction (clockwise or counterclockwise). The
program computes the new `rot` and tries offsets `(dr, dc)` added to `(row,
col)` in this order, accepting the first state that does not collide:

- type I: (0,0) (0,-1) (0,1) (0,-2) (0,2) (-1,0) (-2,0)
- types T, S, Z, J, L: (0,0) (0,-1) (0,1) (-1,0)
- type O: the rotation always succeeds and changes nothing.

If no offset works, the request is rejected: the state and the lock timer stay
unchanged. A rotation that succeeds counts as a successful adjustment for the
lock timer (see "Gravity and lock"). The vector list "KICK" fixes 14 cases
including the rejections.

### Gravity and lock

- The piece moves down one row every `interval(level)` milliseconds, from the
  table below. The first interval starts when the piece spawns.
- When the piece is grounded, a lock timer of 500 ms runs. A successful move
  or rotation that leaves the piece grounded restarts the timer, at most 10
  times per piece; the eleventh adjustment leaves the timer running. A move
  that leaves the piece ungrounded stops the timer. When the timer expires, the
  piece locks.
- Soft drop (key Down): move down one row and add 1 point if the move
  succeeds; if the piece is grounded the piece locks at once and adds nothing.
- Hard drop (keys Space and Enter): move down by the largest distance that
  does not collide (vector list "DROP"), add 2 points per row moved, and lock
  at once.
- Gravity interval by level, in milliseconds:

| Level | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 and above |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Interval | 800 | 700 | 610 | 530 | 450 | 380 | 320 | 260 | 210 | 170 | 135 | 105 | 84 | 64 |

The 64 ms floor is twice the keyboard's 32 ms exchange period, so a player
can enter at least two key events per gravity row at every level. The program
gets time from `gettimeofday()` and waits for input with `select()` on file
descriptor 0 with a timeout equal to the time to the nearest deadline. Neither
call is verified on the console tty; the implementer checks both on the host
and reports the board check as `not run`.

### Locking, line clears, scoring and levels

On lock, in this order:

1. Write the piece's type number into its four board cells.
2. If all four cells are in rows 0 and 1, the game ends (lock-out).
3. Remove every row in which all 12 cells are nonzero, among all 24 rows. Rows
   above a removed row shift down by the number of removed rows beneath them;
   empty rows enter at the top. Cells do not fall further (no cell-level
   cascade). The count removed is `n`, from 0 to 4 (vector list "CLEAR").
4. Add `level * 100 * n * (n + 1) / 2` points, using the level in effect before
   step 5 (vector list "SCORE": 100, 300, 600, 1000 times the level for
   `n` = 1, 2, 3, 4).
5. Add `n` to the line count and recompute the level as
   `min(20, start_level + lines / 10)` with integer division (vector list
   "LEVEL").
6. Spawn the next piece; if the spawn collides the game ends (top-out).

The rules collapse the cleared rows at once in step 3. The front end shows the
rows removed by the lock as `~~` in every cell for about 150 ms (see
"Rendering"); that wait changes no rules state, gravity or lock time.

The score is an unsigned 32-bit integer that saturates at 99,999,999 (eight
digits). `start_level` is 1 to 9 from the command line, default 1.

### Piece generator

The generator is a 7-bag. A bag holds the seven types in some order; the
queue is the concatenation of bags. Requirements:

- Bag construction by Fisher-Yates from the identity order I O T S Z J L (types
  1 to 7): for `i` from 6 down to 1, draw `j = rnd(i + 1)` in `0..i` and swap
  positions `i` and `j`. Vector list "BAG" gives scripted draws and the
  resulting order.
- `rnd(m)` is `(rand() * m) >> 15`, which takes the high bits of the libc
  generator (`lib/libc/compat/rand.c` returns 15 bits, and the low bits of a
  power-of-two LCG are the weak ones). Its relative bias is below 0.03 percent for `m`
  up to 7.
- Every aligned group of seven pieces of the queue is a permutation of the
  seven types, so two consecutive pieces of one type are at most 13 queue
  positions apart.
- The queue always holds at least two pieces, so the program can show the next
  two and spawn without a pause.
- Seed from the command line when given, else from `time(NULL)` combined with
  `getpid()`.

### Input mapping

The PicoCalc keyboard sends arrows as `ESC [ A`/`B`/`C`/`D`, which
`gtty_getkey()` folds to `k j l h`; letters pass through. The keyboard sends
no key-release event and repeats a held arrow as repeated presses. Each
received key is one action.

| Key | Playing | Paused | Game over |
| --- | --- | --- | --- |
| Left, `h` | move left | none | none |
| Right, `l` | move right | none | none |
| Up, `k`, `x` | rotate clockwise | none | none |
| `z` | rotate counterclockwise | none | none |
| Down, `j` | soft drop | none | none |
| Space, Enter (CR or LF) | hard drop | resume | start a new game |
| `p`, F1, Esc | pause | resume | none |
| `g` | toggle the plumb line | none | none |
| Ctrl-L | repaint everything | repaint | repaint |
| `q` | quit | quit | quit |

F1 arrives as `ESC O P` or `ESC [ 11 ~`; Esc alone arrives as a single `ESC`
byte. After reading `ESC` the program checks `FIONREAD` on descriptor 0 and
treats the byte as a bare Esc when nothing follows. Unknown sequences are
consumed and ignored.

### Rendering on the 53x32 console

One board cell is two terminal columns and one row, which is 12x10 pixels, close
to square. All coordinates are 1-based terminal rows and columns. The well,
its two walls and the right-hand panel use columns 2 to 52, so the layout
clears the 53-column edge, and rows 1 to 26 plus the cursor park at row 31
clear the 32-row edge.

- Board cell `(r, c)` with `r` in 2..23 draws at terminal row `r`, columns
  `3 + 2c` and `4 + 2c`. Hidden rows are not drawn (vector list "SCREEN").
- The frame: the left wall `!` at column 2 and the right wall `!` at column 27
  for rows 2 to 23; the floor at row 24 is `=` in every column from 2 to 27.
  The wall and floor characters are neither `|`, `+` nor `-`.
- Row 1 holds the title `FOURFALL` above the well, centered over columns 3 to
  26 (column 11).
- A cell prints according to "Glyphs and colors": glyph texture and attributes,
  then `ESC [ 0 m`. An empty cell prints two spaces with no attributes.
- The plumb line (off by default; option `-g` turns it on, key `g` toggles it)
  prints `.` followed by a space, in bright black, in every empty cell below
  the piece's lowest cell in each column the piece occupies, down to the first
  filled cell or the floor (vector list "PLUMB"). It draws no outline of the
  piece, and the program prints no landing aid unless `-g` or `g` asks.
- Panel (columns 30 to 52): rows 2, 3 and 4 hold the labels `SCORE`, `LEVEL`,
  `LINES` at column 30 and their values left-aligned at column 37. Row 7
  holds the title `NEXT` at column 30. The next two pieces draw stacked in
  that panel, each as its glyph texture in orientation 0: the first in a
  two-row box at terminal rows 9 and 10, the second at rows 12 and 13, box
  column `c` at terminal columns `30 + 2c` and `31 + 2c` (vector list
  "SCREEN"; every orientation-0 piece lies in box rows 0 and 1). Previews print
  in the settled style. Rows 16 to 22 hold the key help at column 30, one
  line each, none longer than 23 characters.
- Row 26 holds the status line `score N lines N level N` in lower case from
  column 1, redrawn after every lock. This line is the pty tests' oracle.
- Row 32 is never written, so the console never scrolls.
- A lock that clears rows draws the board as it stood before the collapse with
  every cell of each cleared row as `~~` (bold unless `-m`), waits 150 ms,
  then draws the collapsed board. Keys pressed during the wait stay queued.
  The game clock excludes the wait.
- Drawing is differential. The program keeps a shadow of the 22 by 12 visible
  cells and the panel strings. It writes only the cells that changed, each as a
  cursor-position escape `ESC [ row ; col H` followed by the cell, collects
  one frame in a buffer, and sends it with one `write(2)`. A full repaint
  happens at start, on Ctrl-L and after the pause overlay clears. After each
  frame the cursor parks at row 31, column 1.
- Layout diagram (columns run left to right; `X` is a well cell, 22 rows from
  2 to 23 by 24 columns from 3 to 26; the first preview is shown as an I and
  the second as a T):

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

- Pause draws `PAUSED` centered across the board at row 13 and blanks every
  board cell and both previews so the pause gives no planning time; resume
  redraws the board. Pause and resume keep the remaining gravity and lock time.
- Game over draws `GAME OVER` at row 12, `Enter  play again` at row 14 and
  `q  quit` at row 15, each centered across columns 3 to 26, and updates the
  status line once more.

The cell drawing function is one place in the program, so a later glyph-bank
version (bevelled block glyphs in codes 0x80 and above) replaces it without
touching the rules.

### Command line, test hook and exit

- `fourfall [-cgm] [start-level [seed]]`. `-c` turns color on, `-g` turns the
  plumb line on, `-m` selects monochrome letters; the flags may be combined in
  one word (`-cg`) or given separately. An unknown flag, or arguments beyond
  the seed, print `usage: fourfall [-cgm] [start-level [seed]]` on standard
  error and exit with status 2.
- If the environment variable `GAMEBOX_TEST` is set, gravity and the lock timer
  are off (no piece moves or locks except by player action), and the piece queue is the letters of the variable's value
  when it contains only characters from `IOTSZJL`, repeated cyclically, else
  `IOTSZJL` repeated. The seed is ignored. This lets a pty test play without a
  clock and without depending on the host `rand()`, as `games/keen` and
  `games/bubble` do.
- The program restores the terminal mode (`gtty_restore()`) on `q`, on game end
  followed by `q`, and on SIGINT and SIGTERM, and exits with status 0 after
  `q`.

### Pure-logic boundary

The rules above live in functions with no I/O and no clock, in the same source
file, with `main` compiled out under `-DFOURFALL_LOGIC` so a host test
includes the file. The functions the vectors need: the cell set of a type and
orientation; collision of a state against a board; rotation with kicks; lock
(returning the lock-out flag); line clear (returning the count); points for a
clear; level from start level and lines; gravity interval; hard-drop
distance; the plumb line of a column; bag construction from a draw function;
spawn state; the glyph of a type; and the screen position of a board cell and
of a preview cell. Names are the implementer's.

### Test vectors

Notation. A board is written as its bottom rows from top to bottom joined by
`/`; the last row listed is board row 23; `-` is the empty board; `.` is an
empty cell and the digits 1 to 7 are piece types. A state is
`type rot row col`. A result `reject` means the rotation request is refused.
All 161 vectors below are exact. The harness runs every line and reports the
count it ran.

Vector counts by list: SHAPE 28, COLL 22, KICK 14, CLEAR 11,
SCORE 13, LEVEL 14 (including the gravity-interval rows), BAG 4, DROP 6,
TOPOUT 15 (including the spawn rows), SCREEN 13 (board cells and previews),
GLYPH 7, PLUMB 9, REPLAY 5; total 161.

List SHAPE: the 28 cell sets (type, orientation, cells as row,col in the box)

    SHAPE-I0  type=I rot=0  cells 1,3 1,2 1,1 1,0
    SHAPE-I1  type=I rot=1  cells 3,2 2,2 1,2 0,2
    SHAPE-I2  type=I rot=2  cells 2,3 2,2 2,1 2,0
    SHAPE-I3  type=I rot=3  cells 3,1 2,1 1,1 0,1
    SHAPE-O0  type=O rot=0  cells 1,1 0,0 1,0 0,1
    SHAPE-O1  type=O rot=1  cells 1,1 0,0 1,0 0,1
    SHAPE-O2  type=O rot=2  cells 1,1 0,0 1,0 0,1
    SHAPE-O3  type=O rot=3  cells 1,1 0,0 1,0 0,1
    SHAPE-T0  type=T rot=0  cells 1,2 1,1 1,0 0,1
    SHAPE-T1  type=T rot=1  cells 2,1 1,2 1,1 0,1
    SHAPE-T2  type=T rot=2  cells 2,1 1,2 1,1 1,0
    SHAPE-T3  type=T rot=3  cells 2,1 1,1 1,0 0,1
    SHAPE-S0  type=S rot=0  cells 1,1 1,0 0,2 0,1
    SHAPE-S1  type=S rot=1  cells 2,2 1,2 1,1 0,1
    SHAPE-S2  type=S rot=2  cells 2,1 2,0 1,2 1,1
    SHAPE-S3  type=S rot=3  cells 2,1 1,1 1,0 0,0
    SHAPE-Z0  type=Z rot=0  cells 1,2 1,1 0,1 0,0
    SHAPE-Z1  type=Z rot=1  cells 2,1 1,2 1,1 0,2
    SHAPE-Z2  type=Z rot=2  cells 2,2 2,1 1,1 1,0
    SHAPE-Z3  type=Z rot=3  cells 2,0 1,1 1,0 0,1
    SHAPE-J0  type=J rot=0  cells 1,2 1,1 1,0 0,0
    SHAPE-J1  type=J rot=1  cells 2,1 1,1 0,2 0,1
    SHAPE-J2  type=J rot=2  cells 2,2 1,2 1,1 1,0
    SHAPE-J3  type=J rot=3  cells 2,1 2,0 1,1 0,1
    SHAPE-L0  type=L rot=0  cells 1,2 1,1 1,0 0,2
    SHAPE-L1  type=L rot=1  cells 2,2 2,1 1,1 0,1
    SHAPE-L2  type=L rot=2  cells 2,0 1,2 1,1 1,0
    SHAPE-L3  type=L rot=3  cells 2,1 1,1 0,1 0,0

List COLL: collision of a state against a board (1 = collides)

    COLL-01  board=-  type=I rot=0 row=0 col=4  -> 0
    COLL-02  board=-  type=I rot=0 row=0 col=-1  -> 1
    COLL-03  board=-  type=I rot=0 row=0 col=8  -> 0
    COLL-04  board=-  type=I rot=0 row=0 col=9  -> 1
    COLL-05  board=-  type=T rot=0 row=22 col=4  -> 0
    COLL-06  board=-  type=T rot=0 row=23 col=4  -> 1
    COLL-07  board=-  type=O rot=0 row=22 col=5  -> 0
    COLL-08  board=-  type=O rot=0 row=23 col=5  -> 1
    COLL-09  board=-  type=I rot=1 row=20 col=0  -> 0
    COLL-10  board=-  type=I rot=1 row=21 col=0  -> 1
    COLL-11  board=-  type=I rot=1 row=5 col=-2  -> 0
    COLL-12  board=-  type=I rot=1 row=5 col=-3  -> 1
    COLL-13  board=-  type=I rot=0 row=-1 col=4  -> 0
    COLL-14  board=-  type=I rot=0 row=-2 col=4  -> 1
    COLL-15  board=1...........  type=O rot=0 row=22 col=0  -> 1
    COLL-16  board=1...........  type=O rot=0 row=22 col=1  -> 0
    COLL-17  board=..1.........  type=J rot=0 row=22 col=0  -> 1
    COLL-18  board=..1.........  type=J rot=0 row=21 col=0  -> 0
    COLL-19  board=111111111111/111111111111  type=T rot=0 row=20 col=4  -> 0
    COLL-20  board=111111111111/111111111111  type=T rot=0 row=21 col=4  -> 1
    COLL-21  board=-  type=O rot=0 row=10 col=10  -> 0
    COLL-22  board=-  type=O rot=0 row=10 col=11  -> 1

List KICK: rotation with kicks (result is the new rot row col, or reject)

    KICK-01  board=-  type=I rot=0 row=20 col=4  dir=cw  -> rot=1 row=20 col=4
    KICK-02  board=-  type=I rot=0 row=22 col=4  dir=cw  -> rot=1 row=20 col=4
    KICK-03  board=-  type=I rot=1 row=5 col=-2  dir=cw  -> rot=2 row=5 col=0
    KICK-04  board=-  type=I rot=1 row=5 col=9  dir=cw  -> rot=2 row=5 col=8
    KICK-05  board=-  type=T rot=1 row=10 col=-1  dir=cw  -> rot=2 row=10 col=0
    KICK-06  board=-  type=T rot=3 row=10 col=10  dir=cw  -> rot=0 row=10 col=9
    KICK-07  board=-  type=S rot=0 row=22 col=4  dir=cw  -> rot=1 row=21 col=4
    KICK-08  board=-  type=O rot=0 row=22 col=0  dir=cw  -> rot=1 row=22 col=0
    KICK-09  board=-  type=J rot=0 row=22 col=4  dir=ccw  -> rot=3 row=21 col=4
    KICK-10  board=-  type=T rot=0 row=10 col=4  dir=ccw  -> rot=3 row=10 col=4
    KICK-11  board=1111..111111/1111..111111/1111..111111/1111..111111  type=T rot=3 row=20 col=4  dir=cw  -> reject
    KICK-12  board=1111..111111/1111..111111/1111..111111/1111..111111  type=T rot=3 row=20 col=4  dir=ccw  -> reject
    KICK-13  board=-  type=I rot=3 row=8 col=9  dir=ccw  -> rot=2 row=8 col=8
    KICK-14  board=-  type=I rot=0 row=10 col=8  dir=cw  -> rot=1 row=10 col=8

List CLEAR: line clear (count removed and the board's bottom rows after)

    CLEAR-01  board=11111111111./11111111111.  -> cleared=0  bottom 2 rows after=11111111111./11111111111.
    CLEAR-02  board=...........1/111111111111  -> cleared=1  bottom 2 rows after=............/...........1
    CLEAR-03  board=111111111111/2.........33/111111111111  -> cleared=2  bottom 3 rows after=............/............/2.........33
    CLEAR-04  board=..1........./111111111111/111111111111/111111111111/111111111111  -> cleared=4  bottom 5 rows after=............/............/............/............/..1.........
    CLEAR-05  board=111111111111/3.........3.  -> cleared=1  bottom 2 rows after=............/3.........3.
    CLEAR-06  board=111111111111/111111111111/111111111111/1.1.1.1.1.1.  -> cleared=3  bottom 4 rows after=............/............/............/1.1.1.1.1.1.
    CLEAR-07  board=111111111111/.11111111111/111111111111  -> cleared=2  bottom 3 rows after=............/............/.11111111111
    CLEAR-08  board=111111111111/.2........../111111111111/111111111111/..3.........  -> cleared=3  bottom 5 rows after=............/............/............/.2........../..3.........
    CLEAR-09  board=row 1 full, cell (2,3)=2, all else empty  -> cleared=1  cell (3,3) after=0, row 1 after empty=1
    CLEAR-10  board=1111111111../1111111111..  -> cleared=0  bottom 2 rows after=1111111111../1111111111..
    CLEAR-11  board=..1111111111/111111111111  -> cleared=1  bottom 2 rows after=............/..1111111111

List SCORE: points per clear

    SCORE-L1-1  level=1 lines_cleared=1  -> 100
    SCORE-L1-2  level=1 lines_cleared=2  -> 300
    SCORE-L1-3  level=1 lines_cleared=3  -> 600
    SCORE-L1-4  level=1 lines_cleared=4  -> 1000
    SCORE-L3-1  level=3 lines_cleared=1  -> 300
    SCORE-L3-2  level=3 lines_cleared=2  -> 900
    SCORE-L3-3  level=3 lines_cleared=3  -> 1800
    SCORE-L3-4  level=3 lines_cleared=4  -> 3000
    SCORE-L10-1  level=10 lines_cleared=1  -> 1000
    SCORE-L10-2  level=10 lines_cleared=2  -> 3000
    SCORE-L10-3  level=10 lines_cleared=3  -> 6000
    SCORE-L10-4  level=10 lines_cleared=4  -> 10000
    SCORE-0  level=7 lines_cleared=0  -> 0

List LEVEL: level from lines, and gravity interval

    LEVEL-1/0  start=1 total_lines=0  -> level 1
    LEVEL-1/9  start=1 total_lines=9  -> level 1
    LEVEL-1/10  start=1 total_lines=10  -> level 2
    LEVEL-1/95  start=1 total_lines=95  -> level 10
    LEVEL-5/10  start=5 total_lines=10  -> level 6
    LEVEL-9/100  start=9 total_lines=100  -> level 19
    LEVEL-1/400  start=1 total_lines=400  -> level 20
    LEVEL-20/0  start=20 total_lines=0  -> level 20
    GRAV-1  level=1  -> 800 ms per row
    GRAV-2  level=2  -> 700 ms per row
    GRAV-8  level=8  -> 260 ms per row
    GRAV-14  level=14  -> 64 ms per row
    GRAV-15  level=15  -> 64 ms per row
    GRAV-20  level=20  -> 64 ms per row

List BAG: scripted Fisher-Yates draws (the six values are j for i = 6 down to 1)

    BAG-1  rnd results (i=6..1) 0 0 0 0 0 0  -> O T S Z J L I
    BAG-2  rnd results (i=6..1) 6 5 4 3 2 1  -> I O T S Z J L
    BAG-3  rnd results (i=6..1) 3 1 4 1 2 0  -> L I T J Z O S
    BAG-4  rnd results (i=6..1) 2 0 3 1 1 0  -> L J Z O S I T

List DROP: hard-drop distance

    DROP-01  board=-  type=I rot=0 row=0 col=4  -> distance 22
    DROP-02  board=-  type=I rot=1 row=0 col=0  -> distance 20
    DROP-03  board=111111111111/111111111111  type=O rot=0 row=0 col=5  -> distance 20
    DROP-04  board=1...........  type=O rot=0 row=0 col=0  -> distance 21
    DROP-05  board=1...........  type=O rot=0 row=0 col=1  -> distance 22
    DROP-06  board=.....1....../111111111111  type=T rot=0 row=0 col=3  -> distance 20

List TOPOUT and SPAWN: game-end conditions and spawn states

    TOPOUT-1  occupied cell (1,4); spawn T  -> spawn position row=0 col=4 collides=1
    TOPOUT-2  occupied cell (1,3); spawn T  -> collides=0
    TOPOUT-3  occupied cell (1,6); spawn T  -> collides=1
    TOPOUT-4  occupied cell (1,7); spawn T  -> collides=0
    TOPOUT-LO0  lock O rot=0 row=0 col=5 on empty board  -> lock_out=1
    TOPOUT-LO1  lock O rot=0 row=1 col=5 on empty board  -> lock_out=0
    TOPOUT-LI0  lock I rot=0 row=0 col=4 on empty board  -> lock_out=1
    TOPOUT-LI1  lock I rot=0 row=1 col=4 on empty board  -> lock_out=0
    SPAWN-I  type=I  -> row=0 col=4
    SPAWN-O  type=O  -> row=0 col=5
    SPAWN-T  type=T  -> row=0 col=4
    SPAWN-S  type=S  -> row=0 col=4
    SPAWN-Z  type=Z  -> row=0 col=4
    SPAWN-J  type=J  -> row=0 col=4
    SPAWN-L  type=L  -> row=0 col=4

List SCREEN: board and preview cells to terminal positions

    SCREEN-2,0  board cell row=2 col=0  -> terminal row 2, columns 3-4
    SCREEN-2,11  board cell row=2 col=11  -> terminal row 2, columns 25-26
    SCREEN-23,0  board cell row=23 col=0  -> terminal row 23, columns 3-4
    SCREEN-23,11  board cell row=23 col=11  -> terminal row 23, columns 25-26
    SCREEN-11,4  board cell row=11 col=4  -> terminal row 11, columns 11-12
    SCREEN-HID  board cell row=1 col=5  -> not drawn
    SCREEN-WIDE  board cell row=10 col=12  -> not drawn
    NEXT-I0  preview slot=0 type=I rot=0, cell offset (1,0)..(1,3)  -> terminal row 10, columns 30-37 (box row r at terminal row 9+r, column c at 30+2c..31+2c)
    NEXT-O0  preview slot=0 type=O rot=0, cell offset (0,0)..(1,1)  -> terminal rows 9-10, columns 30-33
    NEXT-T0  preview slot=0 type=T rot=0, cell offset (0,1) (1,0)..(1,2)  -> terminal rows 9-10, columns 30-35
    NEXT-I1  preview slot=1 type=I rot=0, cell offset (1,0)..(1,3)  -> terminal row 13, columns 30-37 (box row r at terminal row 12+r)
    NEXT-O1  preview slot=1 type=O rot=0, cell offset (0,0)..(1,1)  -> terminal rows 12-13, columns 30-33
    NEXT-S1  preview slot=1 type=S rot=0, cell offset (0,1)..(0,2) (1,0)..(1,1)  -> terminal rows 12-13, columns 30-35

List GLYPH: the two-character texture of each type (the result is the two
characters; for Z, two backslashes)

    GLYPH-I  type=I  -> []
    GLYPH-O  type=O  -> ##
    GLYPH-T  type=T  -> <>
    GLYPH-S  type=S  -> //
    GLYPH-Z  type=Z  -> \\
    GLYPH-J  type=J  -> ()
    GLYPH-L  type=L  -> {}

List PLUMB: the plumb line of one board column (first dot row and dot count;
a count of 0 has no first row)

    PLUMB-01  board=-  type=T rot=0 row=0 col=4  plumb_col=5  -> first=2 count=22
    PLUMB-02  board=-  type=T rot=0 row=0 col=4  plumb_col=3  -> count=0
    PLUMB-03  board=.....1....../111111111111  type=T rot=0 row=0 col=4  plumb_col=5  -> first=2 count=20
    PLUMB-04  board=.....1....../111111111111  type=T rot=0 row=0 col=4  plumb_col=4  -> first=2 count=21
    PLUMB-05  board=-  type=I rot=1 row=10 col=0  plumb_col=2  -> first=14 count=10
    PLUMB-06  board=-  type=O rot=0 row=22 col=0  plumb_col=0  -> count=0
    PLUMB-07  board=-  type=S rot=0 row=10 col=0  plumb_col=2  -> first=11 count=13
    PLUMB-08  board=-  type=S rot=0 row=10 col=0  plumb_col=1  -> first=12 count=12
    PLUMB-09  board=-  type=I rot=0 row=10 col=4  plumb_col=7  -> first=12 count=12

List REPLAY: whole-sequence results with gravity off

    REPLAY-1  sequence O O O O O O; keys: left x5, space, left x3, space, left, space, right, space, right x3, space, right x5, space  -> score 564 lines 2 level 1 game_over 0
    REPLAY-2  sequence O repeated; key: space x12  -> score 264 lines 0 game_over 1
    REPLAY-3  sequence I repeated; keys: right x4, space, left x4, space  -> score 88 lines 0 game_over 0; bottom row after = 1111....1111
    REPLAY-4  sequence I; keys: up, space  -> score 40; locked cells column [6] rows [20, 21, 22, 23]
    REPLAY-5  sequence I repeated; keys: right x4, space, left x4, space, space  -> score 232 lines 1 level 1 game_over 0; bottom row after = ............

A replay vector feeds the type sequence and the key sequence to the logic
with gravity off. Score, lines and level are those after the last key. In
REPLAY-1 and REPLAY-2 the sequence is `O` repeated; the first piece spawns at
column 5, so "left x5" places it at columns 0 and 1, and six pieces across the
12 columns clear the two rows the O pieces fill. REPLAY-2 stacks twelve O
pieces: eleven fill rows 2 to 23 and the twelfth locks out in the hidden rows.

Property checks (run in addition to the 161 vectors):

1. For each of the seven types, four clockwise turns return orientation 0, and
   a clockwise then a counterclockwise turn returns the start.
2. Every orientation has four distinct cells inside its box.
3. Line clear on 10,000 boards of random fill: no full row remains, the number
   of nonzero cells drops by 12 times the count removed, and the rows that
   were not full keep their relative order.
4. Hard-drop distance equals the number of single moves down before the first
   collision, on 10,000 random boards and states that start without collision.
5. Bag: for 1,000 bags from each of seeds 1 to 20, every aligned group of seven
   draws is a permutation, and two pieces of one type are at most 13 queue
   positions apart.
6. Bag fairness: over 7,000 bags, for each queue position and type the count is
   within 15 percent of 1,000. The analyst's reference run over 300 seeds had
   a worst deviation of 11.2 percent, so a correct implementation passes with
   margin and a plain `rand() % 7` per piece fails check 5.
7. Preview and spawn geometry: the two preview boxes are two rows tall, do not
   overlap, and stay inside column 52; the glyphs of the seven types are two
   characters and pairwise different; the spawn columns of every type leave
   free columns on the left and right that differ by at most one.

Mutation calibration. The harness is accepted only after it rejects each of
these deliberately wrong implementations (each changes at least one listed
vector, or its precondition):

- the rotation direction swapped (KICK, REPLAY);
- the right wall check using `col >= 11` (COLL, KICK preconditions);
- the floor check removed (COLL, DROP);
- no kick table for types T, S, Z, J, L (KICK);
- no kick table for type I (KICK);
- line score `100 * n * level` (SCORE, REPLAY);
- a line clear that skips the row after a removed row (CLEAR, REPLAY);
- a level step every 9 lines (LEVEL);
- the O piece spawning at column 4 (SPAWN, REPLAY);
- the spawn column computed for a 10-column well (SPAWN, TOPOUT);
- a 10-column or a 22-row well (COLL, KICK, CLEAR, DROP, REPLAY);
- a plumb line that starts inside the piece or ignores the stack (PLUMB);
- two glyphs equal (GLYPH, property check 7);
- previews that overlap, or a single preview slot (SCREEN, property check 7).

The front end is calibrated the same way: the pty tests must reject a plumb
line on by default, color on by default, solid-background cells, settled cells
as bright as the falling piece, a well of 10 columns or 20 rows, a single
preview, `|` walls, a `-` floor, a title off center, a skipped or shortened
clear flash, and the `-c`, `-g` options ignored.

### Acceptance tests

1. Host logic test: `bmake -C games/fourfall test-logic` compiles the file with
   `-DHOSTBUILD -DFOURFALL_LOGIC -std=c99 -Wall -Wextra -Werror`, runs the 161
   vectors from `tests/vectors.txt` (the lines above) and the property
   checks, prints the counts, and fails on any mismatch or on a harness run of
   fewer than 161 vectors.
2. pty smoke tests, `tests/play.py`, built on the `games/keen` pattern: run
   `GAMEBOX_TEST=O ./fourfall-host`, send `h` five times and a space, `h` three
   times and a space, `h` and a space, `l` and a space, `l` three times and a
   space, `l` five times and a space; the screen must contain
   `score 564 lines 2 level 1` (REPLAY-1) and the stream must hold the 24 flash
   cells. Run `GAMEBOX_TEST=O` again and send twelve spaces; the screen must
   contain `score 264` and `GAME OVER` (REPLAY-2); then `q` must exit with
   status 0. The default screen must show glyph textures, bold white while
   falling and bright black once settled, with no hue and no plumb line; `-c`,
   `-g` and `-m` change exactly what "Rendering" says.
3. Target build: `bmake MACHINE=rp2040` builds the a.out and `gamebox` links it
   with `fourfall` added to `TOOLS`. No board run is part of the acceptance
   without the repository's exact opt-in.

<!-- REVIEWER-ONLY BELOW -->

## Part 2: reviewer-only material (withheld from the implementer)

### Analyst notes

The analyst read `legacy/non-arm/mips-pic32/share/examples/gpanel/tftetris.c`
in full (922 lines) and wrote Part 1 from three kinds of input: public
mechanics of the falling-block genre (the seven four-cell shapes, line
clearing, a 7-bag, a level that rises with cleared lines, a next-piece
preview), the port's own platform facts, and design values chosen
independently. Part 1 copies no table, string, identifier, constant or
comment from the file. Where a Part 1 value equals a value in the file, the
row below says why the coincidence carries no expression.

| Parameter | In the legacy file (behavioral fact, no code quoted) | Part 1 | Independence |
| --- | --- | --- | --- |
| Board size | 11 columns by 20 rows | 12 by 22 visible rows plus two hidden rows | differs; Part 1 widens and deepens the well away from both the legacy file and the 10 by 20 genre default |
| Shape representation | one coordinate table per shape listing each rotation's cells | orientation-0 cell sets plus a matrix rotation rule | differs; the legacy file gives S, Z and I two orientations and O one, Part 1 gives all four orientations |
| Piece set | seven shapes | seven shapes | equal; the seven tetrominoes are the genre's public vocabulary |
| Generator | a retry loop that draws a random shape until it is new, repeated for seven slots | Fisher-Yates over the seven types | same public idea (a 7-bag), different algorithm and different draw function |
| Gravity | initial 1100 ms, halved every 10 lines | 14-row table, 800 ms down to 64 ms | differs |
| Level step | every 10 lines | every 10 lines | equal; 10 lines per level is a common published convention and the only value that fits one decade of the line counter |
| Scoring | none; lines and level only | triangular line points times level, soft and hard drop points | differs; the legacy file has no score to copy |
| Rotation collision | rotate in place or refuse | four-offset kick lists | differs |
| Lock | locks on the first blocked gravity tick | 500 ms timer with 10 restarts | differs |
| Spawn | column `width/2 - 1`, row 0 | column `(12 - box side) / 2`: 4 for I, T, S, Z, J and L, 5 for O; row 0 | the spawn near the middle top is a public convention; Part 1 centers the piece's bounding box, a different formula |
| Input | analog joystick on two ADC channels with 128-count dead zone | arrows, letters, Space, Enter | differs; platform-specific |
| Drawing | 480x320 pixel panel, one pixel box per cell, gradient background | two-column ASCII glyph textures per piece on a text console, bright while falling and dim (SGR 90) once settled, `!` walls and `=` floor; color is opt-in | differs |
| Sound | beeps through a stub that only delays | none | differs |

Facts in the legacy file the analyst judged defects, so Part 1 excludes them:
a second I orientation that the file marks as a to-do (it does not rotate about
the piece's center); a scan window for completed lines limited to the four rows
from the piece's row; a drop wait that halves at every level without a floor,
reaching zero; a rotation that is refused next to a wall because no kick exists.

### Similarity-review checklist

Source under review: `legacy/non-arm/mips-pic32/share/examples/gpanel/tftetris.c`,
21,451 bytes, 922 lines, SHA-256
`27aaba14c203ed90763e49707216d757ebe16f41127c6c88b1dc069e95eb4f54`, tree
commit `076675a87decc6705ce991f364fb33273f438607`, last touched by commit
`7771c72b`, imported from RetroBSD revision 506 by commit `1f040555`.
License: GPLv2 or later (lines 11-29). Credit lines: 2 (title), 3 (copyright
holder), 5 (upstream URL), 6 (adapter), 7-9 (hardware). Run the checks on the
finished `fourfall.c` and its test files; a hit is not proof of copying, so
the reviewer records each hit and its disposition.

Identifiers, by line of the legacy file (case-sensitive; any hit in
`fourfall.c` needs justification):

| Group | Identifiers (line numbers) |
| --- | --- |
| Helpers | `millis` (52), `delay` (70), `lineh` (75), `linev` (88), `box` (101), `beep` (125), `boxb` (131) |
| Input | `Joystick_fire` (137), `Joystick_getX` (142), `Joystick_getY` (155), `Joystick_waitForRelease` (168), `jx` and `jy` (39) |
| Game functions | `randomizer` (179, 737), `score` (180, 666), `scoreBoard` (181, 426), `draw` (182, 610), `moveDown` (183, 580), `userInput` (184, 548), `chooseNewShape` (185, 527), `touches` (186, 650), `hint` (436), `gameLoop` (454), `drawPreGameScreen` (767), `gameOver` (775), `bcackground` (393) |
| Text drawing | `drawChar` (343), `drawString` (357), `drawCenteredString` (366), `drawNumber` (373), `FONT_SPACE` (194), `FONT_X` (195), `simpleFont` (196, 789) |
| State | `Backup` (240), `old` (337), `newShape` (339), `lastInput`, `lastDrop` (332), `dropWait` (341), `next` and `next_c` (329-330), `current` (324), `all_shapes` (303), `shapes` (318), `colors` (315), `board` (334) |
| Shape tables | `l_shape` (254), `j_shape` (262), `o_shape` (270), `s_shape` (275), `z_shape` (281), `t_shape` (287), `i_shape` (295) |
| Constants | `DROP_WAIT_INIT` (245), `INPUT_WAIT_ROT`, `INPUT_WAIT_MOVE` (247-248), `INPUT_WAIT_NEW_SHAPE` (250), `BOARD_WIDTH`, `BOARD_HEIGHT`, `BLOCK_SIZE`, `BOARD_LEFT`, `BOARD_RIGHT`, `BOARD_TOP`, `BOARD_BOTTOM` (200-208), `LCD_WIDTH`, `LCD_HEIGHT` (189-190), `PIT_COLOR`, `BG_COLOR` (234-235) |

Tables and literals:

| Item | Lines | Check |
| --- | --- | --- |
| Shape coordinate tables, seven arrays of `{x, y}` pairs | 254-299 | no `{0, 0}, {0, 1}, {0, 2}, {1, 2}`-style pair lists in `fourfall.c`; the spec's offsets are `(row, col)` in boxes |
| Orientation-count array `{ 4, 4, 1, 2, 2, 4, 2 }` | 318 | absent |
| Color array and 16-bit color macros (`ORANGE`, `LIGHTGREEN`, `DARKCYAN`, `GRAY1`..`GRAY3`) | 211-235, 315 | absent; Part 1 draws in grayscale by default and its opt-in color mode uses foreground SGR codes 31-37 |
| Bitmap font, 95 rows of 8 bytes | 789-887 | absent; text uses the console font |
| Strings `"Level"` (429), `"Lines"` (430), `"Tetris"` (770), `"Move joystick to start"` (771), `"http://vilaca.eu"` (772), `"Game Over"` (778) | as listed | case-sensitive search finds none; Part 1 uses `LEVEL`, `LINES`, `GAME OVER`, which differ in case or wording |
| Device paths `"/dev/tft0"` (892), `"/dev/adc0"` (899), `"/dev/adc1"` (906) | as listed | absent |
| Numeric constants: 1100 (245), 200 and 100 and 400 (247-250), 480 and 320 (189-190), 11 and 20 (200-201), 350 (733), 1500 and 25 (600), 3000 and 50 (693), 600 and 200 and 300 (781-786), 128 and 512 (145-163) | as listed | a hit among the timing numbers is reviewed; the values 10 (columns, lines per level) and 20 (rows) are genre conventions and expected |
| Sentinel value 255 written into the board while drawing | 621, 629 | absent |
| Comment phrases and misspellings: `TBD???` (127, 139, 298), `tetris guidelines have all 7 shapes` (326-328), `postioned` (536), `bellow` (583), `wont` (589), `BUG!!! was uninitialized` (669), `create a sequence of 7 random shapes` (736), `shapes definitions` (252) | as listed | case-insensitive fixed-string search in `fourfall.c` and its comments finds none |

Algorithm fingerprints (read the finished code for these shapes):

- A line scan limited to the four rows from the piece's row (lines 669-674)
  and a per-row shift that redraws each cell during the shift (706-719). The
  spec removes full rows over the whole board and draws differentially.
- A `touches(xi, yi, roti)` function taking three offsets (650) and the
  indexing expression `(rot * 4 + i) * 2` into a flat byte table (594, 615,
  627, 641, 655).
- A draw that writes 255 into the board as a marker, erases the old position,
  then clears the marker (610-648).
- A bag built by rejection of duplicates (737-765).

Mechanical review procedure:

1. Fixed-string search. Put the distinctive identifiers, strings and comment
   phrases above, one per line, in a file `distinct.txt` outside the tree, and
   the generic words `score`, `draw`, `box`, `delay`, `next`, `old`, `current`,
   `board`, `colors`, `shapes`, `hint` in `generic.txt`. Run
   `grep -n -w -F -f distinct.txt games/fourfall/fourfall.c games/fourfall/tests/*`
   and expect no hit. Run the same with `generic.txt` and read every hit: a
   generic word is allowed as a variable, and a hit that also matches the
   legacy function signature or call shape (for example a three-argument
   `touches`-like collision function taking offsets) is recorded and resolved by
   renaming or restructuring. Record every hit and its disposition.
2. Token-shingle comparison. Strip comments, map every string to `STR`, every
   number to `NUM` and every non-keyword identifier to `ID`, then compare the
   sets of 16-token runs between the legacy file and the candidate. The script
   below computes the shared fraction; it was calibrated on 2026-11-05 with
   Python 3.14.9:

       known-bad  legacy file with 29 identifiers renamed and braces
                  re-flowed             shared 100.0 percent of runs
       known-good games/keen/keen.c     0.4 percent
                  games/bubble/bubble.c 0.5 percent
                  games/fifteen/fifteen.c 0.8 percent
                  gpanel/flappy.c       1.4 percent
       lexer      a built-in sample with "//" and "/*" inside strings,
                  an escaped quote and character literals must lex to
                  the expected token list before any comparison runs;
                  a lexer that strips comments before reading strings
                  fails it

   The lexer reads comments, strings and character literals in one
   left-to-right pass, so a comment marker inside a literal stays data.

   Reject the candidate at 5 percent shared runs or more, and read every shared
   run below that threshold.

```python
import re, sys
KW = {"int","unsigned","long","short","char","void","const","static","struct",
      "typedef","enum","if","else","for","while","do","return","break",
      "continue","switch","case","default","sizeof","volatile","extern","byte"}
# One left-to-right pass: a comment marker inside a string or character
# literal is data, and a quote inside a comment is comment.
LEX = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\''
                 r'|[A-Za-z_]\w*|\d[\w.]*|==|!=|<=|>=|&&|\|\||\+\+|--|->'
                 r'|[-+*/%&|^~!<>=?:;,.(){}\[\]]', re.S)
def toks(text):
    out = []
    for m in LEX.finditer(text):
        s = m.group(0)
        if s.startswith(("/*", "//")):
            continue
        out.append("STR" if s[0] == '"' else "CHR" if s[0] == "'"
                   else "NUM" if s[0].isdigit()
                   else (s if s in KW else "ID") if re.match(r"[A-Za-z_]", s) else s)
    return out
SELFTEST = ('a = "//"; /* "x" */ b = \'/\'; c = "/*" "q\\"//"; // d "e"\n'
            'f = \'\\\'\';')
EXPECT = ["ID", "=", "STR", ";", "ID", "=", "CHR", ";", "ID", "=", "STR", "STR", ";",
          "ID", "=", "CHR", ";"]
if toks(SELFTEST) != EXPECT:
    sys.exit("lexer self-test failed: %r" % toks(SELFTEST))
def read(path): return open(path, encoding="utf-8", errors="replace").read()
def runs(tk, k): return {tuple(tk[i:i+k]) for i in range(len(tk) - k + 1)}
ref, cand, k = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 16
a, b = runs(toks(read(ref)), k), runs(toks(read(cand)), k)
print("shared %d of %d candidate runs (%.1f%%)" % (len(a & b), len(b), 100.0 * len(a & b) / max(1, len(b))))
```

3. Table audit. Diff `cells()` of the implementation against the Part 1 table by
   running the SHAPE vectors; a candidate that also matches the legacy
   coordinate lists in line order fails item 2 of the identifier table.
4. Provenance of tests. The vectors and the pty script in Part 1 are the
   analyst's; confirm that the test files contain no line from the legacy file.

Residual uncertainty. The review establishes absence of copied text, tables and
identifiers at the stated thresholds. It does not establish that the genre's
mechanics are free of third-party rights, and it does not replace legal review
of the final name. The shingle gate is calibrated on one renamed copy and four
unrelated programs; a candidate that paraphrases structure without sharing
16-token runs passes the gate and is caught only by the algorithm
fingerprints, which the reviewer reads by hand.
