# Keen generated ambiguous puzzles; the fix and its evidence

The DiscoBSD RP2040 keen game (games/keen/keen.c) generated puzzles whose
clues did not force a single grid, so it was not a puzzle: a player who
deduced everything the clues give still had to guess between grids. This
note records the confirmed defect, the fix, and the evidence, and marks
what the photographs left uncertain.

## Confirmed defect

gen_cages grew cages by random adjacent unions and gen_clues wrote each
cage's target, with no check that the clues admit one grid. The header
comment said so plainly: "Cages are grown by random adjacent unions with
no uniqueness check, so a puzzle can (rarely) admit solutions besides the
one generated." "Rarely" was wrong. A host count over the pre-check
generator (KEEN_NOUNIQUE=1) finds:

| size | ambiguous puzzles (>= 2 grids) |
|---|---|
| 3x3 | 49 of 60 seeds |
| 4x4 | 49 of 60 seeds |
| 5x5 | 120 of 200 seeds |
| 6x6 | 57 of 60 seeds |

"keen 5 10" admits thirteen grids. This is the reproduction the user hit:
they solved 13 of 25 cells and the rest still had multiple outcomes.

## The five outcomes

The exact count of five is a property of the clues plus the player's own
entries, not of the clues alone. Taking the thirteen-grid puzzle from
"keen 5 10" and imposing thirteen cells of one of its grids as player
entries leaves exactly five grids consistent with clues and entries. The
regression asserts at least two grids for the clues alone and exactly
five once the reconstructed entries are imposed, matching the user's
statement. The entries are a reconstruction chosen to leave five; they
are not a transcript of the played game, which the photographs did not
capture.

## Fix

keen.c gains a backtracking solver (count_solutions) pruned by row and
column masks and a per-cage partial-clue check, and gen_puzzle regrows
the cages, and after 64 failures the Latin square, until exactly one grid
satisfies the clues. solved() now checks the clues and the Latin property
rather than only matching the stored grid; the uniqueness guarantee makes
the two equivalent. This mirrors what KeenClassik
(app/src/main/jni/keen_generate.c), the source-of-truth Keen, does with
its latin_solver: it regenerates until the solver reaches the target
difficulty and rejects diff_ambiguous, so a shipped puzzle is unique. The
port cannot run KeenClassik's technique-rated solver in the box, so it
uses a plain uniqueness count, which is the weaker guarantee that the
puzzle is solvable at all, without a difficulty rating.

## Evidence

- `bmake -C games/keen test` runs two host tests. tests/keensolve.py is
  an independent solution counter sharing no code with keen.c;
  tests/uniqueness.py requires the two to agree on every count.
- Four fixtures: ambiguous (>= 2 grids), the same with thirteen entries
  imposed (exactly 5), unique (1), inconsistent (0, a unique puzzle with
  one cage's subtraction target changed to an impossible value).
- The generator sweep: the unchecked generator is ambiguous in 54 of 80
  puzzles at sizes 4 and 5; the checked generator is unique in all 160
  puzzles across sizes 3 to 6 and forty seeds, by both counters.
- On the board: "keen 5 10" generates, draws and solves to the solved
  state; generation is under a second for most seeds and 2.05s for a 6x6
  worst case.
- Object size grew from 2,217 to 2,909 text bytes and 740 to 1,660 bss;
  the board a.out is 9,008 bytes and still shares gamebox.

## Uncertain because of the photographs

The photographs the user referenced are not of the puzzle: the files in
~/Pictures and ~/Downloads from the period are insurance cards, a Pico
board, and qwen-apu screenshots, and the earlier session's only attached
image was a device shell screenshot. The specific grid the user played,
their thirteen solved cells, and which five outcomes they saw are
therefore not reconstructed from source data. The ambiguity is confirmed
against the actual generator; the exact five-outcome fixture is a faithful
reconstruction of the described situation, not a transcript.
