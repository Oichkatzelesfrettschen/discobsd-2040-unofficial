#!/usr/bin/env python3
# Regression coverage for keen's solution uniqueness. Every count is
# taken twice, by the host build's --count mode and by keensolve.py,
# which shares no code with it, and the two must agree.
#
#   fixtures/reported-5x5.keen           the puzzle a player reported with
#                                        multiple solutions: exactly five grids
#   fixtures/reported-5x5-entries.keen   the same with the player's 13 cells
#                                        imposed: still five grids, because the
#                                        13 are the cells common to all five
#   fixtures/unique-5x5.keen             exactly one grid
#   fixtures/inconsistent-5x5.keen       no grid
#
# Then the generator itself: with KEEN_NOUNIQUE the old behavior must
# still show ambiguity somewhere in the seed range, or this test could
# not tell a working check from an absent one, and without it every
# puzzle over sizes 3 to 6 and forty seeds must have exactly one grid
# by both counters.
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import keensolve  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")


def c_count(prog, path, limit=1000):
    out = subprocess.run([prog, "--count", path, str(limit)],
                         capture_output=True, text=True, check=True).stdout
    assert out.startswith("solutions "), out
    return int(out.split()[1])


def dump(prog, size, seed, nounique=False):
    env = dict(os.environ)
    env.pop("KEEN_NOUNIQUE", None)
    if nounique:
        env["KEEN_NOUNIQUE"] = "1"
    return subprocess.run([prog, "--dump", str(size), str(seed)],
                          capture_output=True, text=True, check=True,
                          env=env).stdout


def both(prog, text, limit=1000):
    path = os.path.join(HERE, ".count.tmp")
    with open(path, "w") as f:
        f.write(text)
    c = c_count(prog, path, limit)
    os.unlink(path)
    p = keensolve.count(text, limit)
    assert c == p, "counters disagree: keen %d, keensolve %d\n%s" % (c, p, text)
    return c


def fixture(name):
    with open(os.path.join(FIX, name)) as f:
        return f.read()


def main():
    prog = sys.argv[1] if len(sys.argv) > 1 else "./keen-host"

    # The reported puzzle: its clues alone admit exactly five grids. The
    # original request expected "at least two" here, before the puzzle was
    # transcribed; the real count is five, which the assertion pins exactly.
    reported = fixture("reported-5x5.keen")
    amb = both(prog, reported)
    assert amb >= 2, "reported fixture: expected multiple grids, got %d" % amb
    assert amb == 5, "reported fixture: expected exactly five grids, got %d" % amb

    # The player's thirteen entries are the cells that hold the same value in
    # every solution, so imposing them removes no grid: five remain. This is
    # checked two ways -- the count with entries is five, and the thirteen
    # forced cells (identical across all five clue-only solutions) are exactly
    # the thirteen the fixture imposes.
    ent = both(prog, fixture("reported-5x5-entries.keen"))
    assert ent == 5, "reported fixture with entries: expected five grids, got %d" % ent
    sols = keensolve.solutions(reported, 100)
    n = 5
    forced = {(r, c): sols[0][r][c] for r in range(n) for c in range(n)
              if len({g[r][c] for g in sols}) == 1}
    _, _, _, imposed = keensolve.parse(fixture("reported-5x5-entries.keen"))
    assert forced == imposed, \
        "the imposed entries are not the cells forced by the clues: forced %s, imposed %s" \
        % (sorted(forced), sorted(imposed))
    assert len(forced) == 13, "expected thirteen forced cells, got %d" % len(forced)

    assert both(prog, fixture("unique-5x5.keen")) == 1, "unique fixture"
    assert both(prog, fixture("inconsistent-5x5.keen")) == 0, "inconsistent fixture"
    print("keen: fixtures OK (reported %d grids, 13 forced cells match entries, "
          "unique 1, inconsistent 0)" % amb)

    seeds = range(1, 41)
    old_ambiguous = 0
    for size in (4, 5):
        for seed in seeds:
            if both(prog, dump(prog, size, seed, nounique=True), 50) >= 2:
                old_ambiguous += 1
    assert old_ambiguous > 0, "the unchecked generator never produced an ambiguous puzzle"

    checked = 0
    for size in (3, 4, 5, 6):
        for seed in seeds:
            k = both(prog, dump(prog, size, seed), 50)
            assert k == 1, "size %d seed %d: %d grids from the checked generator" % (size, seed, k)
            checked += 1
    print("keen: uniqueness OK (%d unchecked puzzles ambiguous of 80, %d checked puzzles unique)"
          % (old_ambiguous, checked))


if __name__ == "__main__":
    main()
