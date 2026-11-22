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
"""Methods 1 to 3 per refresh: today's bytes, guest-space span rectangles, panel-space rectangles.

Method 1 is 2 x (sent + fill) from the log. Method 2 covers the guest cells or bytes
that changed since the previous refresh (plus the old and new cursor cells in text); a
stale refresh keeps today's fill and full redraw. Method 3 covers the pixels where the
panel frame differs from the previous one; the first refresh also pays the fill.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


def mask_cells(cells):
    m = np.zeros((320, 320), bool)
    for c in cells:
        r, cc = divmod(int(c), 80)
        m[pl.GY0 + 8 * r : pl.GY0 + 8 * r + 8, 4 * cc : 4 * cc + 4] = True
    return m


def run(name):
    recs = pl.load(name)
    lg = pl.logs(name)
    cur = pl.read_json(name + "_cursor.json")
    rows = []
    prev_p = np.zeros((320, 320), np.uint16)
    prev_g = None
    prev_k = None
    for i, (kind, _cols, g, p) in enumerate(recs):
        lk, sent, fill, units, span, shifted, kshift = lg[i]
        if lk != kind:
            raise ValueError("log and dump disagree at refresh %d" % i)
        stale = fill > 0
        diff = p != prev_p
        if kind == "T":
            cells = g.reshape(2000, 2)
            if prev_k == "T" and not stale:
                chg = np.nonzero((cells != prev_g.reshape(2000, 2)).any(1))[0]
            else:
                chg = np.arange(2000)
            redraw = {int(c) for c in chg}
            if prev_k == "T" and not stale:
                co, cn = (cur[i - 1] if i else None), cur[i]
                if co != cn:
                    for c in (co, cn):
                        if c is not None and c >= 0:
                            redraw.add(c)
            gmask = mask_cells(redraw)
            gchg_units = len(chg)
            sim_sent = 32 * len(redraw)
        else:
            lines = pl.g_lines(g)
            if prev_k == "G" and not stale:
                chgb = lines != pl.g_lines(prev_g)
            else:
                chgb = np.ones((200, 80), bool)
            gmask = np.zeros((320, 320), bool)
            gmask[pl.GY0 : pl.GY0 + pl.LINES] = np.repeat(chgb, 4, axis=1)
            gchg_units = int(chgb.sum())
            sim_sent = None
        m2 = pl.fillcost(102400) + pl.wcost(320, 200) if stale else pl.cover(gmask)[0]
        c3, n3 = pl.cover(diff)
        m3 = pl.fillcost(102400) + c3 if i == 0 else c3
        rows.append(
            dict(
                i=i,
                kind=kind,
                stale=int(stale),
                today=2 * (sent + fill),
                sent=sent,
                fill=fill,
                m2=m2,
                m3=m3,
                img_px=int(diff.sum()),
                guest_px=int(gmask.sum()),
                floor=2 * int(diff.sum()),
                gunits=gchg_units,
                sim_sent=sim_sent,
                units=units,
                span=span,
                logshift=shifted,
                logk=kshift,
                rects3=n3,
            )
        )
        prev_p, prev_g, prev_k = p, g, kind
    return rows


def main():
    pl.configure("Methods 1 to 3 per refresh.")
    res = {name: run(name) for name in ("dos", "win")}
    pl.write_json("analysis_a.json", res)
    for name, rows in res.items():
        tot = {k: sum(r[k] for r in rows) for k in ("today", "m2", "m3", "img_px", "guest_px")}
        print(name, len(rows), tot)


main()
