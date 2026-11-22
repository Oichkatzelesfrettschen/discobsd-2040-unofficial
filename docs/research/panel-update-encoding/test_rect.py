# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Calibrate the rectangle cover on known-good and known-bad masks."""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


def blank(h=40, w=40):
    return np.zeros((h, w), bool)


def check(cond, what):
    if not cond:
        raise SystemExit("FAIL: " + what)


def hand_cases():
    wcost = pl.wcost
    m = blank()
    m[5:15, 5:15] = 1
    check(pl.cover(m) == (wcost(10, 10), 1), "one rectangle costs its window")
    m = blank()
    m[0, 0] = 1
    m[39, 39] = 1
    check(pl.cover(m) == (2 * (2 + 11), 2), "two far pixels cost two windows")
    m = blank()
    m[3, 0] = 1
    m[3, 3] = 1
    check(pl.cover(m) == (wcost(4, 1), 1), "a gap of two pixels bridges")
    m = blank()
    m[3, 0] = 1
    m[3, 10] = 1
    check(pl.cover(m)[0] == 2 * wcost(1, 1), "a gap of nine pixels does not bridge")
    m = blank()
    m[0:30, 2] = 1
    m[0:30, 35] = 1
    check(pl.cover(m)[0] == 2 * wcost(1, 30), "two tall columns stay two windows")
    m = blank()
    m[10:20, 5:15] = 1
    check(pl.cover(m, rowcuts=[15]) == (wcost(10, 5) * 2, 2), "a row cut splits a window")
    c, rl = pl.cover_rects(m, rowcuts=[15])
    check(all(not (y < 15 < y + h) for x, y, w, h in rl), "no window straddles the cut")


def random_cases():
    rng = np.random.default_rng(1)
    for _ in range(300):
        m = rng.random((12, 12)) < rng.choice([0.05, 0.2, 0.5])
        b, _n = pl.cover(m)
        check(b >= 2 * m.sum() or not m.any(), "cost is at least two bytes a damaged pixel")
        check(b <= int(m.sum()) * pl.wcost(1, 1), "cost is at most one window a pixel")
    rng = np.random.default_rng(7)
    for _ in range(400):
        m = rng.random((30, 40)) < rng.choice([0.01, 0.05, 0.2, 0.5])
        if rng.random() < 0.5:
            m[rng.integers(0, 20) :, rng.integers(0, 20) :][
                : rng.integers(1, 8), : rng.integers(1, 12)
            ] = True
        c, _n = pl.cover(m)
        c2, rl = pl.cover_rects(m)
        check(c == c2, "cover and cover_rects agree")
        cov = np.zeros_like(m)
        for x, y, w, h in rl:
            cov[y : y + h, x : x + w] = True
        check(not (m & ~cov).any(), "the rectangle list covers every damaged pixel")
        check(sum(pl.wcost(w, h) for x, y, w, h in rl) == c, "the list costs what the DP says")


def main():
    pl.configure("Calibrate the rectangle cover.", need_corpus=False)
    hand_cases()
    random_cases()
    print("rectangle cover calibrated: known-good costs, bridging, cuts and coverage hold")


main()
