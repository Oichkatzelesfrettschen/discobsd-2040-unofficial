"""The search that chose calibrate.FITS. The gate does not run it.

A random local search over calibrate.VECTOR: each step perturbs one to
three entries and keeps the result when the loss, the sum over FITTED of
(residual / bound) squared with each term capped at 100, does not rise.
Each vector in calibrate.FITS is the best of one run:

  $PYTHON fit.py <search seed> <steps> <jitter seed> [start vector as JSON]

Trace model 3 started from the best vector of trace model 2 (the same
process structure without update running ahead of a fresh child).
"""

import json
import random
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = "swap_tier_model"     # noqa: A001

from . import calibrate, trace  # noqa: E402

BOUNDS = [(300, 3000), (500, 1300), (200, 2000), (500, 1300), (1500, 6000),
          (600, 1500), (2500, 6000), (600, 1500), (3500, 9000), (600, 1500),
          (8000, 20000), (1000, 8000), (600, 1500)]
START = [1332, 1280, 486, 1233, 3150, 1444, 3423, 758, 4927, 904, 10466, 2654,
         1278, 80, 30, 15, 5, 12, 0]


def loss(vector, seed):
    rows, _held, _m = calibrate.evaluate(
        trace.generate(calibrate.from_vector(vector, seed)))
    return sum(min((r[3] / r[4]) ** 2, 100) for r in rows)


def step(vector, rng):
    v = list(vector)
    n = len(BOUNDS)
    for _ in range(rng.randint(1, 3)):
        k = rng.randrange(len(v))
        if k < n:
            lo, hi = BOUNDS[k]
            v[k] = min(hi, max(lo, v[k] + rng.gauss(0, (hi - lo) * 0.08)))
        elif k == n:
            v[k] = min(800, max(0, v[k] + rng.gauss(0, 80)))
        elif k == n + 1:
            v[k] = rng.randint(8, 30)
        elif k == n + 2:
            v[k] = rng.randint(0, 29)
        elif k == n + 3:
            v[k] = rng.randint(0, 8)
        elif k == n + 4:
            v[k] = rng.randint(0, 23)
        else:
            v[k] = rng.choice([0] + list(range(10, 80)))
    return [int(x) for x in v]


def main(argv):
    rng = random.Random(int(argv[0]))
    steps, seed = int(argv[1]), int(argv[2])
    best = json.loads(argv[3]) if len(argv) > 3 else START
    best_loss = loss(best, seed)
    for _ in range(steps):
        candidate = step(best, rng)
        value = loss(candidate, seed)
        if value <= best_loss:
            best, best_loss = candidate, value
    print(f"loss {best_loss:.3f}")
    print(json.dumps(best))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
