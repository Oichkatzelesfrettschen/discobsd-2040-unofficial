#!/usr/bin/env python3
# Regression coverage for keen's solution uniqueness. Every count is
# taken twice, by the host build's --count mode and by keensolve.py,
# which shares no code with it, and the two must agree.
#
#   fixtures/ambiguous-5x5.keen          the pre-check generator's puzzle
#                                        for seed 10: at least two grids
#   fixtures/ambiguous-5x5-entries.keen  the same with 13 cells imposed:
#                                        exactly five grids
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

    amb = both(prog, fixture("ambiguous-5x5.keen"))
    assert amb >= 2, "ambiguous fixture: expected at least two grids, got %d" % amb
    ent = both(prog, fixture("ambiguous-5x5-entries.keen"))
    assert ent == 5, "ambiguous fixture with entries: expected five grids, got %d" % ent
    assert both(prog, fixture("unique-5x5.keen")) == 1, "unique fixture"
    assert both(prog, fixture("inconsistent-5x5.keen")) == 0, "inconsistent fixture"
    print("keen: fixtures OK (ambiguous %d, with entries %d, unique 1, inconsistent 0)"
          % (amb, ent))

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
