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
"""Build the derived count tables from the analysis outputs.

Writes per_refresh_<corpus>.tsv, summary_wire.tsv, summary_streams.tsv and summary_misc.tsv
to the output directory. Every value is a count of bytes, pixels or refreshes; no frame or
guest-memory bytes enter a table.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402

CORPORA = ("dos", "win")
FIRST_PANEL_WRITE = pl.wcost(320, 320)  # unknown panel: every pixel written once
GUEST_AREA_WRITE = pl.wcost(320, 200)  # outside lines known black
WIRE_SECONDS_A_BYTE = 0.64e-6  # 12.5 MHz bit clock


def write_tsv(name, header, rows):
    with pl.out_file(name).open("w") as f:
        f.write("\t".join(header) + "\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")


def merge(name, a2, v2):
    """Per-refresh rows with method 4 joined in."""
    rows = []
    for r, v in zip(a2[name], v2[name]["rows"]):
        row = dict(r)
        row["m4"] = v["m4"]
        row["k"] = v["k"]
        rows.append(row)
    return rows


def wire_row(name, rows, b, v2, m2b):
    s = lambda key: sum(r[key] for r in rows)  # noqa: E731
    m4 = s("m4")
    g_ns = [r for r in rows if r["kind"] == "G" and not r["stale"]]
    m2_span = s("m2") - sum(r["m2"] for r in g_ns) + (m2b["m2b_G_nonstale_bytes"] if g_ns else 0)
    return dict(
        refreshes=len(rows),
        m1=s("today"),
        m1h=b[name]["today_with_headers"],
        m2_span=m2_span,
        m2_exact=s("m2"),
        m3=s("m3"),
        m4=m4,
        m5a=m4 - rows[0]["m4"] + FIRST_PANEL_WRITE,
        m5b=m4 - rows[0]["m4"] + GUEST_AREA_WRITE,
        m5c=m4 - pl.fillcost(102400),
    )


def misc_rows(rows_by, w, a2, b, v2, c, m240, m2b):
    out = []

    def add(key, fn):
        out.append((key, *[fn(n) for n in CORPORA]))

    add("refreshes", lambda n: len(rows_by[n]))
    add("stale_refreshes", lambda n: sum(r["stale"] for r in rows_by[n]))
    add("stale_today_bytes", lambda n: sum(r["today"] for r in rows_by[n] if r["stale"]))
    add("scroll_refreshes", lambda n: sum(1 for r in rows_by[n] if r["k"]))
    add("scroll_gain_bytes", lambda n: sum(r["m3"] - r["m4"] for r in rows_by[n] if r["k"]))
    add(
        "scroll_shifts_multiple_of_8",
        lambda n: sum(1 for r in rows_by[n] if r["k"] and r["k"] % 8 == 0),
    )
    add("panel_pixels_changed", lambda n: sum(r["img_px"] for r in rows_by[n]))
    add("pixels_sent_today", lambda n: sum(r["sent"] for r in rows_by[n]))
    add("fill_pixels_today", lambda n: sum(r["fill"] for r in rows_by[n]))
    add("guest_span_pixels", lambda n: sum(r["guest_px"] for r in rows_by[n]))
    add("wire_seconds_today", lambda n: "%.2f" % (w[n]["m1"] * WIRE_SECONDS_A_BYTE))
    add("wire_seconds_method4", lambda n: "%.2f" % (w[n]["m4"] * WIRE_SECONDS_A_BYTE))
    add("wire_seconds_method5", lambda n: "%.2f" % (w[n]["m5c"] * WIRE_SECONDS_A_BYTE))
    add(
        "refreshes_with_no_panel_change",
        lambda n: sum(1 for r in rows_by[n] if r["img_px"] == 0 and r["today"] > 0),
    )
    add("their_bytes_today", lambda n: sum(r["today"] for r in rows_by[n] if r["img_px"] == 0))
    for k in (
        "t_guest_cells",
        "t_invisible_cells",
        "g_guest_bytes",
        "g_invisible_bytes",
        "g_changed_pairs",
        "g_swap_pairs",
    ):
        add("kernel_" + k, lambda n, k=k: b[n]["ker"][k])
    add("graphics_refreshes", lambda n: len(b[n]["mixed"]))
    add("mixed_pairs_all_frames", lambda n: sum(m[1] for m in b[n]["mixed"]))
    vis = {n: [m for m in b[n]["mixed"] if m[4] > 1] for n in CORPORA}
    add("frames_showing_content", lambda n: len(vis[n]))
    add("mixed_pairs_in_those", lambda n: sum(m[1] for m in vis[n]))
    add("mixed_pairs_max_a_frame", lambda n: max((m[1] for m in vis[n]), default=0))
    add(
        "mixed_pairs_percent_of_pairs",
        lambda n: "%.1f" % (100.0 * sum(m[1] for m in vis[n]) / max(1, 64000 * len(vis[n]))),
    )
    add("m3_on_320x240", lambda n: m240[n]["m3_240"])
    add("m4_on_320x240", lambda n: m240[n]["m4_240"])
    add("scroll_refreshes_on_320x240", lambda n: m240[n]["n_scroll"])
    add("replay_matches_every_frame", lambda n: v2[n]["replay_ok"])
    add("replay_wrong_shift", lambda n: v2[n]["bad"].get("wrong_k", "not run"))
    add("refreshes_with_a_wrap_seam", lambda n: v2[n]["seam_refreshes"])
    add("seam_cut_needed_in", lambda n: v2[n]["nocut_would_straddle"])
    add(
        "native_frame_every_refresh",
        lambda n: sum(4000 if r["kind"] == "T" else 16000 for r in rows_by[n]),
    )
    glines = {n: sum(r["sent"] for r in rows_by[n] if r["kind"] == "G") // 320 for n in CORPORA}
    add("today_damage_graphics_lines_at_80_bytes", lambda n: c[n]["tot"]["p0"] - 240 * glines[n])
    add("shifts_priced_max_a_refresh", lambda n: v2[n]["shifts_priced_max"])
    add("shifts_priced_mean_a_refresh", lambda n: v2[n]["shifts_priced_mean"])
    add("packed_rects_1bpp", lambda n: c[n]["tot"]["rects_1bpp"])
    add("packed_rects_2bpp", lambda n: c[n]["tot"]["rects_2bpp"])
    add("packed_rects_4bpp", lambda n: c[n]["tot"]["rects_4bpp"])
    add("two_bpp_rects_needing_a_map", lambda n: c[n]["tot"]["two_bpp_maps"])
    add("palette_entries_set", lambda n: c[n]["tot"]["pal_entries"])
    add("palette_bytes", lambda n: c[n]["tot"]["pal_bytes"])
    add("line_predictor_messages", lambda n: c[n]["tot"]["z3_png"])
    add(
        "decoded_frames_render_equal",
        lambda n: "%d/%d" % (c[n]["verify"]["z3_render"], c[n]["verify"]["z3_render_n"]),
    )
    add(
        "flipped_byte_detected",
        lambda n: "%d/%d" % (c[n]["verify"]["corrupt_detected"], c[n]["verify"]["corrupt_tested"]),
    )
    out.append(
        (
            "all_black_refreshes_win",
            "-",
            "%d from %d to %d"
            % (
                len(m2b["black_invisible_records"]),
                min(m2b["black_invisible_records"]),
                max(m2b["black_invisible_records"]),
            ),
        )
    )
    out.append(("line_span_pixels_non_stale_graphics_win", "-", m2b["px_line_span_nonstale_G"]))
    out.append(("log_span_pixels_non_stale_win", "-", m2b["log_span_nonstale_all"]))
    return out


def main():
    pl.configure("Derived count tables.")
    a2 = {n: pl.read_json("analysis_a2_%s.json" % n) for n in CORPORA}
    b = pl.read_json("analysis_b.json")
    v2 = pl.read_json("analysis_v2.json")
    c = pl.read_json("analysis_c.json")
    m240 = pl.read_json("analysis_240.json")
    m2b = pl.read_json("analysis_m2b.json")
    rows_by = {n: merge(n, a2, v2) for n in CORPORA}
    w = {n: wire_row(n, rows_by[n], b, v2, m2b) for n in CORPORA}
    cols = [
        "i",
        "kind",
        "stale",
        "today",
        "today_hdr_bytes",
        "m2",
        "m3",
        "m4",
        "k",
        "img_px",
        "guest_px",
        "floor",
        "gunits",
        "sent",
        "sim_sent",
        "units",
        "span",
    ]
    for n in CORPORA:
        write_tsv("per_refresh_%s.tsv" % n, cols, [[r[k] for k in cols] for r in rows_by[n]])
    keys = ["refreshes", "m1", "m1h", "m2_span", "m2_exact", "m3", "m4", "m5a", "m5b", "m5c"]
    names = [
        "refreshes",
        "m1_today_pixel_bytes",
        "m1h_today_with_sim_headers",
        "m2_guest_span_rects",
        "m2_byte_exact",
        "m3_image_diff_rects",
        "m4_plus_scroll",
        "m5a_unknown_panel",
        "m5b_outside_known_black",
        "m5c_clear_dropped",
    ]
    write_tsv(
        "summary_wire.tsv", ["corpus", *names], [[n, *[w[n][k] for k in keys]] for n in CORPORA]
    )
    sk = ["p0", "p1", "p2", "z0", "z1", "z2", "z3"]
    sn = [
        "p0_today",
        "p1_rects_idx8",
        "p2_rects_packed",
        "z0_skip_lit",
        "z1_quotient_defer",
        "z2_row_copy",
        "z3_line_predictor",
    ]
    write_tsv(
        "summary_streams.tsv",
        ["corpus", *sn, "xz_z3_messages", "palette_bytes_in_every_row"],
        [
            [n, *[c[n]["tot"][k] for k in sk], c[n]["ref"]["z3"]["xz"], c[n]["tot"]["pal_bytes"]]
            for n in CORPORA
        ],
    )
    misc = misc_rows(rows_by, w, a2, b, v2, c, m240, m2b)
    write_tsv("summary_misc.tsv", ["quantity", "dos", "win"], misc)
    for t in ("summary_wire.tsv", "summary_streams.tsv", "summary_misc.tsv"):
        print(pl.out_file(t).read_text())


main()
