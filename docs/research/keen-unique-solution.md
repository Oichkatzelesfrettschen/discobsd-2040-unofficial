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

## The reported puzzle: exactly five solutions

The player's screenshot (Screenshot_20260913_093427) is the actual 5x5
puzzle, transcribed into tests/fixtures/reported-5x5.keen. Its cages and
clues:

    aabbc    a 40*   b 11+   c 2/
    aabdc    d 15*   e 24*   f 12+
    eeedd    g 9+    h 5
    feggd
    ffggh

The clues alone admit exactly five grids, confirmed by both the host
`--count` path and the independent C17 `tests/keensolve.c`. The five solutions:

    12534  12534  14532  21534  41532
    45312  45312  25314  45312  25314
    21453  23451  32451  12453  12453
    53241  51243  51243  53241  53241
    34125  34125  43125  34125  34125

The thirteen cells the player had filled in are exactly the thirteen
cells that hold the same value in all five solutions -- the cells the
clues force -- and the other twelve vary across the five. So the player
had deduced everything the clues determine and correctly saw that the
rest had five possible outcomes. Imposing the thirteen entries
(reported-5x5-entries.keen) therefore still counts five. This is the
"13 of 25 solved, 5 outcomes" report, confirmed cell for cell against the
puzzle rather than reconstructed.

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

- `bmake -C games/keen test` runs a pseudo-terminal test and nineteen bounded
  uniqueness shards. `tests/keensolve.c` is an independent row-permutation
  solution counter sharing no code with `keen.c`'s cell search;
  `tests/uniqueness.py` requires the two C counters to agree on every count.
- Four fixtures: the reported puzzle (exactly 5 grids), the same with the
  player's thirteen entries imposed (still 5, and the test checks those
  thirteen are the cells the clues force), unique (1), and inconsistent
  (0, a unique puzzle with one cage's subtraction target made impossible).
- The generator sweep: the unchecked generator is ambiguous in 54 of 80
  puzzles at sizes 4 and 5; the checked generator is unique in all 160
  puzzles across sizes 3 to 6 and forty seeds, by both counters.
- The fixtures own one target, each unchecked size owns one, and every checked
  size owns four ten-seed targets. Each process receives a private temporary
  file and a 30-second deadline calibrated against a sleeping negative
  control. A 12-job cold run proves all 244 paired counts in 0.51 seconds on a
  12-thread x86-64 host. The six-shard C17 graph took 1.25 seconds; the former
  Python permutation search consumed 92.4 seconds and 129,203,478 function
  calls in the same worktree.
- The C17 parser rejects malformed sizes, limits, duplicate or unused clues and
  truncated lines. Cppcheck, the Clang static analyzer and a complete
  AddressSanitizer plus UndefinedBehaviorSanitizer proof report zero findings.
  The host-only solver adds zero bytes to the RP2040 image.
- On the board: "keen 5 10" generates, draws and solves to the solved
  state; generation is under a second for most seeds and 2.05s for a 6x6
  worst case.
- Object size grew from 2,217 to 2,909 text bytes and 740 to 1,660 bss;
  the board a.out is 9,008 bytes and still shares gamebox.

## What the screenshot did and did not fix

The screenshot resolves what an earlier pass could not: the puzzle is now
transcribed from the image, the five solutions are enumerated, and the
thirteen filled cells are shown to be exactly the forced cells, so the
diagnosis is confirmed against the real data, not a reconstruction from a
different seed. The transcription is self-validating: a wrong cage border
or clue would not produce a solution set whose forced cells match the
thirteen visible entries.

One thing the screenshot cannot give is the seed and RNG that generated
this exact board. The board uses newlib's rand(), the host glibc's, so
the seed that drew this puzzle on the device does not reproduce it on the
host, and the pre-fix generator is gone from the current tree. This does
not weaken the diagnosis: the defect is that the shipped generator
emitted a puzzle with five solutions, and that puzzle is now a fixture.
