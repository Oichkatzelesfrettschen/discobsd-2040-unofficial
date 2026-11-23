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
"""Method 4: the best vertical scroll of the 200-line area, replayed on a controller model.

The controller model has a fixed top of 60 lines, a 200-line scroll area with a scroll start,
and a fixed bottom of 60 lines. Windows address RAM rows (scroll start + display row) modulo
200 and may not wrap inside one window. Each refresh sends VSCSAD for the chosen shift (3
bytes; the first scroll also pays VSCRDEF, 7 bytes) and the windows of the cover, cut at the
wrap seam. The replay must reproduce every recorded panel frame. Two known-bad runs must
fail: a wrong shift at the first scroll, and the same cover without the seam cut.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


class Straddle(Exception):
    """A window wraps inside the scroll area, or crosses a fixed/scroll boundary."""


class Mismatch(Exception):
    """The model's display differs from the recorded frame."""


class Ctl:
    def __init__(self):
        self.top = np.zeros((60, 320), np.uint16)
        self.bot = np.zeros((60, 320), np.uint16)
        self.ram = np.zeros((200, 320), np.uint16)
        self.s = 0

    def display(self):
        return np.concatenate([self.top, np.roll(self.ram, -self.s, axis=0), self.bot])

    def scroll(self, k):
        self.s = (self.s + k) % 200

    def clear(self):
        self.top[:] = 0
        self.bot[:] = 0
        self.ram[:] = 0

    def write(self, x, y, w, h, pix):
        gy0 = pl.GY0
        if y >= gy0 and y + h <= gy0 + 200:
            rows = [(self.s + (y - gy0) + j) % 200 for j in range(h)]
            if any(rows[j + 1] != rows[j] + 1 for j in range(h - 1)):
                raise Straddle((x, y, w, h, self.s))
            self.ram[rows[0] : rows[0] + h, x : x + w] = pix
        elif y + h <= gy0:
            self.top[y : y + h, x : x + w] = pix
        elif y >= gy0 + 200:
            self.bot[y - gy0 - 200 : y - gy0 - 200 + h, x : x + w] = pix
        else:
            raise Straddle(("fixed/scroll boundary", x, y, w, h))


def rowcuts_for(s):
    """Panel row of the wrap seam for scroll start s, as a cut list."""
    return [] if s % 200 == 0 else [pl.GY0 + 200 - (s % 200)]


def best_scroll_rects(old, new, s):
    """Cheapest (bytes, shift, rectangles, shifts priced) over every shift 0..199.

    A cover costs at least 2 bytes for each damaged pixel, so the shifts are priced in
    order of mismatch count and the search stops when 2 * mismatches (plus the outside
    cover and the 3-byte VSCSAD) exceeds the best price found. The result is exact for
    the cover's own prices.
    """
    gy0 = pl.GY0
    a = old[gy0 : gy0 + 200]
    b = new[gy0 : gy0 + 200]
    outside = np.zeros((320, 320), bool)
    outside[:gy0] = old[:gy0] != new[:gy0]
    outside[gy0 + 200 :] = old[gy0 + 200 :] != new[gy0 + 200 :]
    oc, orl = pl.cover_rects(outside)
    u = (b[:, None, :] != a[None, :, :]).sum(2)
    idx = np.arange(200)
    mism = np.array([u[idx, (idx + k) % 200].sum() for k in range(200)])
    best = None
    priced = 0
    for k in sorted(range(200), key=lambda k: (int(mism[k]), k)):
        lower = oc + 2 * int(mism[k]) + (3 if k else 0)
        if best is not None and lower > best[0]:
            break
        m = np.zeros((320, 320), bool)
        m[gy0 : gy0 + 200] = np.roll(a, -k, axis=0) != b
        c, _n = pl.cover(m, rowcuts=rowcuts_for((s + k) % 200))
        priced += 1
        cost = c + oc + (3 if k else 0)
        if best is None or (cost, k) < best[:2]:
            best = (cost, k)
    m = np.zeros((320, 320), bool)
    m[gy0 : gy0 + 200] = np.roll(a, -best[1], axis=0) != b
    c, rl = pl.cover_rects(m, rowcuts=rowcuts_for((s + best[1]) % 200))
    return best[0], best[1], rl + orl, priced


def replay(recs, plan, wrong_k_at=None):
    ctl = Ctl()
    for i, (_kind, _cols, _g, p) in enumerate(recs):
        k, rl = plan[i]
        if i == 0:
            ctl.clear()
        else:
            if wrong_k_at == i:
                k = (k + 1) % 200
            ctl.scroll(k)
        for x, y, w, h in rl:
            ctl.write(x, y, w, h, p[y : y + h, x : x + w])
        if not (ctl.display() == p).all():
            raise Mismatch(i)
    return True


def run(name):
    recs = pl.load(name)
    prev = np.zeros((320, 320), np.uint16)
    s = 0
    paid = False
    plan = []
    rows = []
    nocut_straddle = 0
    seam_refreshes = 0
    priced_max = 0
    priced_sum = 0
    for i, (_kind, _cols, _g, p) in enumerate(recs):
        c3, rl3 = pl.cover_rects(p != prev)
        if i == 0:
            m4 = pl.fillcost(102400) + c3
            k = 0
            rl = rl3
            m3 = m4
        else:
            m3 = c3
            m4, k, rl, priced = best_scroll_rects(prev, p, s)
            priced_max = max(priced_max, priced)
            priced_sum += priced
            if k and not paid:
                m4 += 7
                paid = True
            s2 = (s + k) % 200
            if s2:
                # the same choice without the seam cut must straddle where a seam exists
                seam_refreshes += 1
                gy0 = pl.GY0
                m = np.zeros((320, 320), bool)
                m[gy0 : gy0 + 200] = (
                    np.roll(prev[gy0 : gy0 + 200], -k, axis=0) != p[gy0 : gy0 + 200]
                )
                _, rl_nocut = pl.cover_rects(m)
                if any(y < gy0 + 200 - s2 < y + h for (_x, y, _w, h) in rl_nocut):
                    nocut_straddle += 1
            s = s2
        plan.append((k, rl))
        rows.append(dict(i=i, m3=m3, m4=m4, k=k, nr=len(rl)))
        prev = p
    ok = replay(recs, plan)
    bad = {}
    first_k = next((i for i, (k, _) in enumerate(plan) if k), None)
    if first_k is not None:
        try:
            replay(recs, plan, wrong_k_at=first_k)
            bad["wrong_k"] = "NOT detected"
        except (Mismatch, Straddle) as e:
            bad["wrong_k"] = "detected (%s) at the first scroll refresh" % type(e).__name__
    return dict(
        m3=sum(r["m3"] for r in rows),
        m4=sum(r["m4"] for r in rows),
        replay_ok=ok,
        bad=bad,
        seam_refreshes=seam_refreshes,
        nocut_would_straddle=nocut_straddle,
        n_scroll=sum(1 for r in rows if r["k"]),
        shifts_priced_max=priced_max,
        shifts_priced_mean=round(priced_sum / max(1, len(rows) - 1), 1),
        rows=rows,
    )


def main():
    pl.configure("Method 4 with a controller-model replay.")
    res = {}
    for name in ("dos", "win"):
        res[name] = run(name)
        print(name, {k: v for k, v in res[name].items() if k != "rows"}, flush=True)
    pl.write_json("analysis_v2.json", res)


main()
