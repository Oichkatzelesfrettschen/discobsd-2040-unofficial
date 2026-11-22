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
"""Methods 3 and 4 on a 320x240 target: the 200 guest lines repeated to 240 lines (6:5).

Row j of the 240-line image is guest line floor(5j / 6). The scroll area is 240 lines at
panel line 40. This is the line-repeat reading of 320x240, not a resampling.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402

Y0 = 40
N = 240


def stretch(p):
    out = np.zeros((320, 320), np.uint16)
    idx = (np.arange(N) * 5) // 6
    out[Y0 : Y0 + N] = p[pl.GY0 : pl.GY0 + 200][idx]
    return out


def best_scroll(old, new, s, nbest=6):
    a = old[Y0 : Y0 + N]
    b = new[Y0 : Y0 + N]
    u = (b[:, None, :] != a[None, :, :]).sum(2)
    idx = np.arange(N)
    mism = np.array([u[idx, (idx + k) % N].sum() for k in range(N)])
    cand = [0] + [int(k) for k in np.argsort(mism)[: nbest + 1] if k != 0][:nbest]
    res = []
    for k in cand:
        s2 = (s + k) % N
        m = np.zeros((320, 320), bool)
        m[Y0 : Y0 + N] = np.roll(a, -k, axis=0) != b
        c, _nr = pl.cover(m, rowcuts=[] if s2 == 0 else [Y0 + N - s2])
        res.append((c + (3 if k else 0), k))
    res.sort()
    return res[0]


def run(name):
    recs = pl.load(name)
    prev = np.zeros((320, 320), np.uint16)
    s = 0
    paid = False
    m3 = 0
    m4 = 0
    px = 0
    ks = []
    for i, (_kind, _cols, _g, p) in enumerate(recs):
        q = stretch(p)
        diff = q != prev
        px += int(diff.sum())
        c3, _ = pl.cover(diff)
        if i == 0:
            c3 += pl.fillcost(102400)
        m3 += c3
        if i == 0:
            m4 += c3
        else:
            c4, k = best_scroll(prev, q, s)
            m4 += c4 + (7 if (k and not paid) else 0)
            paid = paid or bool(k)
            s = (s + k) % N
            if k:
                ks.append(k)
        prev = q
    return dict(m3_240=m3, m4_240=m4, img_px=px, scroll_ks=ks[:20], n_scroll=len(ks))


def main():
    pl.configure("Methods 3 and 4 on a 320x240 line-repeat image.")
    res = {}
    for name in ("dos", "win"):
        res[name] = run(name)
        print(name, res[name], flush=True)
    pl.write_json("analysis_240.json", res)


main()
