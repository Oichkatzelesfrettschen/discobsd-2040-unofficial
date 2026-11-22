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
"""Method 2 with the log's definition of a span, and the refreshes D maps to black.

The log's span column counts, on each line, the pixels from the first to the last changed
byte. This script recomputes it on Windows graphics refreshes (reconciling with the log),
covers those spans for method 2, and lists the refreshes whose guest memory is not empty
but whose panel area is entirely black.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


def main():
    pl.configure("Method 2 per-line spans and the all-black refreshes.")
    rows = pl.read_json("analysis_a2_win.json")
    recs = pl.load("win")
    px_line_span = 0
    px_byte = 0
    log_span_nonstale = 0
    m2b = 0
    prev_g = None
    prev_k = None
    black_inv = []
    for i, (kind, _cols, g, p) in enumerate(recs):
        r = rows[i]
        area = p[pl.GY0 : pl.GY0 + 200]
        if kind == "G" and prev_k == "G" and not r["stale"]:
            lines = pl.g_lines(g)
            chg = lines != pl.g_lines(prev_g)
            spanm = np.zeros((200, 80), bool)
            for y in range(200):
                nz = np.nonzero(chg[y])[0]
                if len(nz):
                    spanm[y, nz[0] : nz[-1] + 1] = True
            px_line_span += int(spanm.sum()) * 4
            px_byte += int(chg.sum()) * 4
            log_span_nonstale += r["span"]
            mk = np.zeros((320, 320), bool)
            mk[pl.GY0 : pl.GY0 + 200] = np.repeat(spanm, 4, axis=1)
            m2b += pl.cover(mk)[0]
        elif kind == "T" and not r["stale"]:
            log_span_nonstale += r["span"]
        if kind == "G" and not area.any() and g.any():
            black_inv.append(i)
        prev_g, prev_k = g, kind
    out = dict(
        px_line_span_nonstale_G=px_line_span,
        px_byte_nonstale_G=px_byte,
        log_span_nonstale_all=log_span_nonstale,
        m2b_G_nonstale_bytes=m2b,
        black_invisible_records=black_inv,
    )
    pl.write_json("analysis_m2b.json", out)
    print(out)


main()
