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
"""Calibrate the display map D against the model's panel frames.

Text frames must equal the font render of the guest cells, with at most one
cursor cell (rows 6 and 7 drawn in the foreground color). Graphics frames must
be a function of the pair-sum level. The cursor cell of each text record goes to
<corpus>_cursor.json for the later scripts.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


def calibrate(name):
    recs = pl.load(name)
    exact = 0
    gexact = 0
    gn = 0
    outside_black = 0
    cur = []
    bad = []
    maps = {}
    for i, (kind, _cols, g, p) in enumerate(recs):
        outside = np.concatenate([p[: pl.GY0], p[pl.GY0 + pl.LINES :]])
        outside_black += int(not outside.any())
        area = p[pl.GY0 : pl.GY0 + pl.LINES]
        if kind == "T":
            mism = pl.render_text(g) != area
            if not mism.any():
                exact += 1
                cur.append(None)
                continue
            cells = np.unique((np.nonzero(mism)[0] // 8) * 80 + np.nonzero(mism)[1] // 4)
            if len(cells) == 1 and (pl.render_text(g, int(cells[0])) == area).all():
                exact += 1
                cur.append(int(cells[0]))
            else:
                bad.append((i, len(cells)))
                cur.append(-1)
        else:
            gn += 1
            lv = pl.g_levels(pl.g_lines(g))
            m = {}
            ok = True
            for v in (0, 1, 2):
                u = np.unique(area[lv == v])
                if len(u) > 1:
                    ok = False
                if len(u):
                    m[v] = int(u[0])
            gexact += ok
            maps[i] = m
            cur.append(None)
    pl.write_json(name + "_cursor.json", cur)
    return dict(
        records=len(recs),
        text_exact=exact,
        text_bad=bad,
        g_records=gn,
        g_consistent=gexact,
        outside_black=outside_black,
        g_level_maps=sorted({tuple(sorted(m.items())) for m in maps.values()}),
    )


def main():
    pl.configure("Calibrate D against the recorded panel frames.")
    out = {name: calibrate(name) for name in ("dos", "win")}
    pl.write_json("calib_D.json", out)
    for name, o in out.items():
        print(name, {k: v for k, v in o.items() if k != "g_level_maps"})


main()
