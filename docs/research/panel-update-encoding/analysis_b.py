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
"""Today's wire bytes with window headers, the kernel of D, and the 2:1 halving loss.

Reads analysis_a.json. Today's headers come from simulating gfx.c: text redraws runs of
changed cells of one attribute, graphics redraws every line of each written 1 KB page.
The kernel counts compare guest changes with panel changes between consecutive refreshes.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402


def run(name, rows):
    recs = pl.load(name)
    cur = pl.read_json(name + "_cursor.json")
    today_h = 0
    simmis = 0
    gcount = 0
    ker = dict(
        t_guest_cells=0,
        t_image_cells=0,
        t_invisible_cells=0,
        g_guest_bytes=0,
        g_image_bytes=0,
        g_invisible_bytes=0,
        g_swap_pairs=0,
        g_changed_pairs=0,
        refresh_invisible=0,
        refresh_invisible_today_bytes=0,
    )
    mixed = []
    prev_p = np.zeros((320, 320), np.uint16)
    prev_g = None
    prev_k = None
    for i, (kind, _cols, g, p) in enumerate(recs):
        r = rows[i]
        stale = r["stale"]
        sent = r["sent"]
        fill = r["fill"]
        hdrb = 11 if fill else 0
        r_inv = False
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
            nw, pay = pl.t_windows(g, redraw)
            hdrb += 11 * nw
            if prev_k == "T" and not stale:
                a = pl.render_text(g)
                b = pl.render_text(prev_g)
                cim = (a != b).reshape(25, 8, 80, 4).any(axis=(1, 3)).reshape(-1)
                ker["t_guest_cells"] += len(chg)
                ker["t_image_cells"] += int(cim[chg].sum())
                ker["t_invisible_cells"] += int((~cim[chg]).sum())
                r_inv = len(chg) > 0 and cim.sum() == 0
            r["today_hdr_bytes"] = 2 * (sent + fill) + hdrb
            r["text_payload_bits_bytes"] = pay
        else:
            lines8 = pl.g_lines(g)
            if prev_k == "G" and not stale:
                chgb = lines8 != pl.g_lines(prev_g)
                lines = pl.g_dirty_lines(chgb)
            else:
                lines = np.ones(200, bool)
                chgb = np.ones((200, 80), bool)
            simsent = int(lines.sum()) * 320
            if simsent != sent:
                simmis += 1
            gcount += 1
            ws = pl.runs_of_lines(lines)
            if simsent != sent and sent:
                ws = [(0, sent // 320)] if sent == 64000 else ws
            hdrb += sum(11 * (-(-n // 16)) for _, n in ws)
            r["today_hdr_bytes"] = 2 * (sent + fill) + hdrb
            r["g_sim_sent"] = simsent
            if prev_k == "G" and not stale:
                lv_new = pl.g_levels(lines8)
                lv_old = pl.g_levels(pl.g_lines(prev_g))
                bits_new = pl.g_bits(lines8)
                bits_old = pl.g_bits(pl.g_lines(prev_g))
                a = p[pl.GY0 : pl.GY0 + 200]
                b = prev_p[pl.GY0 : pl.GY0 + 200]
                imgb = (a != b).reshape(200, 80, 4).any(2)
                ker["g_guest_bytes"] += int(chgb.sum())
                ker["g_image_bytes"] += int((imgb & chgb).sum())
                ker["g_invisible_bytes"] += int((chgb & ~imgb).sum())
                pairs_changed = (bits_new[:, 0::2] != bits_old[:, 0::2]) | (
                    bits_new[:, 1::2] != bits_old[:, 1::2]
                )
                ker["g_changed_pairs"] += int(pairs_changed.sum())
                ker["g_swap_pairs"] += int((pairs_changed & (lv_new == lv_old)).sum())
                r_inv = bool(chgb.any() and not (a != b).any())
            # a pair 01 or 10 loses one bit when two pixels become one
            bn = pl.g_bits(lines8)
            mixed.append(
                (
                    i,
                    int((bn[:, 0::2] != bn[:, 1::2]).sum()),
                    int((bn[:, 0::2] & bn[:, 1::2]).sum()),
                    int(bn.sum()),
                    int(len(np.unique(p[pl.GY0 : pl.GY0 + 200]))),
                )
            )
        if r_inv:
            ker["refresh_invisible"] += 1
            ker["refresh_invisible_today_bytes"] += 2 * (sent + fill)
        today_h += r["today_hdr_bytes"]
        prev_p, prev_g, prev_k = p, g, kind
    return dict(
        today_with_headers=today_h,
        g_sim_sent_mismatch=simmis,
        g_refreshes=gcount,
        ker=ker,
        mixed=mixed,
    )


def main():
    pl.configure("Today's headers, the kernel of D and the halving loss.")
    a = pl.read_json("analysis_a.json")
    out = {}
    for name in ("dos", "win"):
        out[name] = run(name, a[name])
        pl.write_json("analysis_a2_%s.json" % name, a[name])
    pl.write_json("analysis_b.json", out)
    for n, o in out.items():
        print(n, "today+hdr", o["today_with_headers"], "G sim mismatches", o["g_sim_sent_mismatch"])
        print(" ker", o["ker"])


main()
