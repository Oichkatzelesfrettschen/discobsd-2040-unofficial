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
"""Streams that do not cross the wire: bytes a session for each encoding, with decoders.

Pixel-domain streams (p0 to p2) carry rectangles. Guest-domain streams (z0 to z3) carry the
guest state itself: z0 skip/literal tokens (the same size as XOR with zero runs), z1 adds the
display-map quotient and defers refreshes whose panel image does not change, z2 adds row-copy
operations, z3 adds a per-line predictor message on graphics frames. Every encoder is paired
with a decoder; z0 must rebuild the guest frame exactly, z1 to z3 an image-equivalent frame
(equal per-unit display keys) whose render through D equals the recorded panel frame. A flipped
literal byte must change the decoded state (known-bad check).
"""

import lzma
import sys
import zlib
from functools import cache
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import panellib as pl  # noqa: E402

IOCTL = 12  # bytes copied in for one window or palette ioctl
MAP_BYTES = 2  # four 4-bit palette indexes: the code-to-index map of a 2-bpp rectangle
# panel value to adapter color index in text modes (the lowest index wins a tie)
TEXT_INDEX = {int(v): i for i, v in reversed(list(enumerate(pl.PAL16)))}


def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def rd_varint(buf, o):
    v = 0
    sh = 0
    while True:
        b = buf[o]
        o += 1
        v |= (b & 0x7F) << sh
        sh += 7
        if not b & 0x80:
            return v, o


def tokens(new, mask_units, u):
    """Skip/literal tokens (varint skip, varint literal count, literal bytes) for flagged units."""
    nu = new.reshape(-1, u)
    idx = np.nonzero(mask_units)[0]
    runs = []
    for i in idx:
        if runs and (i - runs[-1][1] - 1) * u < 2:
            runs[-1][1] = int(i)
        else:
            runs.append([int(i), int(i)])
    out = bytearray()
    pos = 0
    for a, b in runs:
        out += varint(a - pos) + varint(b - a + 1) + nu[a : b + 1].tobytes()
        pos = b + 1
    return bytes(out)


def apply_tokens(buf, o, state, u):
    sv = state.reshape(-1, u)
    pos = 0
    while o < len(buf):
        sk, o = rd_varint(buf, o)
        ln, o = rd_varint(buf, o)
        pos += sk
        sv[pos : pos + ln] = np.frombuffer(buf[o : o + ln * u], np.uint8).reshape(ln, u)
        o += ln * u
        pos += ln


def apply_ops(buf, o, state, rowlen):
    nops = buf[o]
    o += 1
    rows = state.reshape(-1, rowlen)
    for _ in range(nops):
        r0 = buf[o]
        n = buf[o + 1]
        sh = int.from_bytes(buf[o + 2 : o + 3], "little", signed=True)
        o += 3
        rows[r0 : r0 + n] = rows[r0 + sh : r0 + sh + n].copy()
    return o


@cache
def glyph_keys():
    """The 32 glyph pixels of each character code as one integer (0 is blank)."""
    return {ch: int(np.packbits(pl.glyph_planes()[ch].ravel()).view(">u4")[0]) for ch in range(256)}


def text_keys(g):
    """Display key of a cell: glyph, foreground if a pixel is set, background if one is clear."""
    tb = glyph_keys()
    out = []
    for ch, at in g.reshape(2000, 2):
        b = tb[int(ch)]
        out.append((b, (at & 15) if b else -1, ((at >> 4) & 7) if b != 0xFFFFFFFF else -1))
    return out


def text_changed_units(new, state):
    kn = text_keys(new)
    ks = text_keys(state)
    return np.array([a != b for a, b in zip(kn, ks)])


def g_key_arr(lines):
    """200x80 bytes to the four pair sums each byte displays."""
    b = np.unpackbits(lines, axis=1).reshape(200, 320, 2).sum(2).astype(np.uint8)
    return b.reshape(200, 80, 4)


def g_changed_units(lnew, ls):
    return (g_key_arr(lnew) != g_key_arr(ls)).any(2).reshape(-1)


def best_ops(new_rows, s_rows, min_gain):
    """Greedy row-copy operations (first row, rows, shift) and the state they leave."""
    ops = []
    cur = s_rows.copy()
    nr = len(new_rows)
    for _ in range(2):
        best = None
        for sh in [*range(-(nr - 1), 0), *range(1, nr)]:
            lo = max(0, -sh)
            hi = min(nr, nr - sh)
            if hi - lo < 3:
                continue
            eq = (new_rows[lo:hi] == cur[lo + sh : hi + sh]).all(1)
            neq = ~(new_rows[lo:hi] == cur[lo:hi]).all(1)
            good = eq & neq
            i = 0
            while i < len(eq):
                if eq[i]:
                    j = i
                    while j < len(eq) and eq[j]:
                        j += 1
                    gain = int(good[i:j].sum())
                    if gain >= min_gain and (best is None or gain > best[0]):
                        best = (gain, lo + i, j - i, sh)
                    i = j
                else:
                    i += 1
        if best is None:
            break
        _, r0, n, sh = best
        ops.append((r0, n, sh))
        cur[r0 : r0 + n] = cur[r0 + sh : r0 + sh + n].copy()
    return ops, cur


def blank_state(kind):
    """The decoder state after a mode set: spaces on the default attribute, or zeros."""
    if kind == "T":
        return np.tile(np.array([0x20, 0x07], np.uint8), 2000)
    return np.zeros(16000, np.uint8)


def png_filters(lnew, s):
    """Per-line predictor: 0 state line, 1 line above, 2 two above, 3 left byte."""
    r = np.zeros((4, 200, 80), np.uint8)
    r[0] = lnew ^ s.reshape(200, 80)
    r[1][1:] = lnew[1:] ^ lnew[:-1]
    r[1][0] = lnew[0]
    r[2][2:] = lnew[2:] ^ lnew[:-2]
    r[2][:2] = lnew[:2]
    r[3][:, 1:] = lnew[:, 1:] ^ lnew[:, :-1]
    r[3][:, 0] = lnew[:, 0]
    nz = (r != 0).sum(2)
    # ties prefer the state line, then up, two up, left
    pick = np.argmin(nz * 4 + np.arange(4)[:, None], axis=0)
    res = np.stack([r[pick[y], y] for y in range(200)])
    return pick, res


def png_encode(lnew, s):
    pick, res = png_filters(lnew, s)
    runs = []
    y = 0
    while y < 200:
        n = 1
        while y + n < 200 and pick[y + n] == pick[y] and n < 64:
            n += 1
        runs.append(bytes([int(pick[y]) << 6 | (n - 1)]))
        y += n
    body = tokens(res.reshape(-1), res.reshape(-1) != 0, 1)
    return bytes([3, len(runs)]) + b"".join(runs) + body


def png_decode(msg, s):
    nr = msg[1]
    o = 2
    pick = []
    for _ in range(nr):
        b = msg[o]
        o += 1
        pick += [b >> 6] * ((b & 63) + 1)
    res = np.zeros(16000, np.uint8)
    apply_tokens(msg, o, res, 1)
    res = res.reshape(200, 80)
    out = np.zeros((200, 80), np.uint8)
    old = s.reshape(200, 80)
    for y in range(200):
        p = pick[y]
        if p == 0:
            out[y] = res[y] ^ old[y]
        elif p == 1:
            out[y] = res[y] ^ (out[y - 1] if y >= 1 else 0)
        elif p == 2:
            out[y] = res[y] ^ (out[y - 2] if y >= 2 else 0)
        else:
            out[y, 0] = res[y, 0]
            for x in range(1, 80):
                out[y, x] = res[y, x] ^ out[y, x - 1]
    s[:] = out.reshape(-1)


def corrupt_ok(m, new, u, verify):
    """Known-bad check: one flipped literal byte must change what the decoder rebuilds."""
    if len(m) < 4:
        return
    bad = bytearray(m)
    bad[-1] ^= 0xFF
    a = np.zeros_like(new)
    b = np.zeros_like(new)
    apply_tokens(bytes(bad), 1, a, u)
    apply_tokens(m, 1, b, u)
    verify["corrupt_tested"] += 1
    verify["corrupt_detected"] += int((a != b).any())


def equivalent(kind, s, new):
    """True when state s displays the same as new (equal display keys in every unit)."""
    if kind == "T":
        return text_keys(s) == text_keys(new)
    return bool((g_key_arr(s.reshape(200, 80)) == g_key_arr(new.reshape(200, 80))).all())


def apply_message(m, state, rowlen, u):
    if m[0] == 2:
        o = apply_ops(m, 1, state, rowlen)
        apply_tokens(m, o, state, u)
    else:
        apply_tokens(m, 1, state, u)


def today_stream(kind, g, prev_g, full, stale, fill, sent, cur, i):
    """Bytes of today's write(2) stream for this refresh: payload plus 12 bytes a window ioctl."""
    if kind == "T":
        cells = g.reshape(2000, 2)
        if not full and not stale:
            chg = np.nonzero((cells != prev_g.reshape(2000, 2)).any(1))[0]
        else:
            chg = np.arange(2000)
        redraw = {int(c) for c in chg}
        if not full and not stale and cur[i - 1] != cur[i]:
            for c in (cur[i - 1], cur[i]):
                if c is not None and c >= 0:
                    redraw.add(c)
        nw, pay = pl.t_windows(g, redraw)
        p0 = IOCTL * nw + pay
    else:
        lines8 = pl.g_lines(g)
        if not full and not stale:
            lines = pl.g_dirty_lines(lines8 != pl.g_lines(prev_g))
        else:
            lines = np.ones(200, bool)
        ws = pl.runs_of_lines(lines)
        if int(lines.sum()) * 320 != sent:
            ws = [(0, sent // 320)] if sent else []
        p0 = sum(IOCTL + 320 * n for _, n in ws)
    return p0 + (IOCTL if fill else 0)


def level_values(lines8, p):
    """Panel value of each pair-sum level present in a graphics frame (palette entries 0 to 2)."""
    area = p[pl.GY0 : pl.GY0 + 200]
    lv = pl.g_levels(lines8)
    return {v: int(area[lv == v][0]) for v in (0, 1, 2) if (lv == v).any()}


def value_index(kind, levmap):
    """Panel value to palette index for this frame (the lowest index wins a tie)."""
    if kind == "T":
        return TEXT_INDEX
    return {val: v for v, val in sorted(levmap.items(), reverse=True)}


def palette_entries(kind, stale, levmap, last_map):
    """Palette entries set before this refresh: all at a mode set, three when a color changed."""
    if stale:
        return 16 if kind == "T" else 3
    if kind == "G" and last_map is not None:
        if any(levmap[v] != last_map[v] for v in levmap if v in last_map):
            return 3
    return 0


def rect_streams(diff, p, i, vidx):
    """Rectangle streams over the panel-space damage.

    p1 is 1 byte a pixel (the INDEX format). p2 is packed 1, 2 or 4 bits a pixel by the
    colors in the rectangle: 1 bpp takes its two palette indexes from the window header
    (fg and bg), 4 bpp is the palette index itself, and a 2-bpp rectangle whose colors
    are not palette entries 0 to 3 needs a code-to-index map of MAP_BYTES. The count of
    such rectangles comes back as the third value.
    """
    _c3, rl = pl.cover_rects(diff)
    p1 = sum(IOCTL + w * h for x, y, w, h in rl)
    p2 = 0
    nmap = 0
    kinds = {1: 0, 2: 0, 4: 0}
    for x, y, w, h in rl:
        vals = np.unique(p[y : y + h, x : x + w])
        try:
            idx = [vidx[int(v)] for v in vals]
        except KeyError as e:
            raise ValueError("panel value %s has no palette index" % e) from e
        bpp = 1 if len(vals) <= 2 else 2 if len(vals) <= 4 else 4
        kinds[bpp] += 1
        p2 += IOCTL + h * ((w * bpp + 7) // 8)
        if bpp == 2 and max(idx) > 3:
            p2 += MAP_BYTES
            nmap += 1
    if i == 0:
        p1 += IOCTL
        p2 += IOCTL
    return p1, p2, nmap, kinds


def render_check(kind, s3, new, p, cur_i):
    """The decoded state, rendered through D, must equal the recorded panel area."""
    area = p[pl.GY0 : pl.GY0 + 200]
    if kind == "T":
        c_i = cur_i if (cur_i is not None and cur_i >= 0) else None
        return bool((pl.render_text(s3, c_i) == area).all())
    lv_dec = pl.g_levels(s3.reshape(200, 80))
    lv_true = pl.g_levels(new.reshape(200, 80))
    lut = np.full(3, -1, np.int64)
    for v in (0, 1, 2):
        if (lv_true == v).any():
            lut[v] = int(area[lv_true == v][0])
    return bool((lut[lv_dec] == area).all())


def run(name, rows):
    recs = pl.load(name)
    cur = pl.read_json(name + "_cursor.json")
    tot = {
        k: 0
        for k in (
            "p0",
            "p1",
            "p2",
            "z0",
            "z1",
            "z2",
            "z3",
            "z1_msgs",
            "z0_msgs",
            "z3_png",
            "two_bpp_maps",
            "rects_1bpp",
            "rects_2bpp",
            "rects_4bpp",
            "pal_entries",
            "pal_bytes",
        )
    }
    last_map = None
    s0 = s1 = s2 = s3 = None
    prev_k = None
    prev_p = np.zeros((320, 320), np.uint16)
    prev_g = None
    streams = {k: bytearray() for k in ("z0", "z1", "z2", "z3")}
    verify = dict(
        z0_exact=0,
        z1_equiv=0,
        z2_equiv=0,
        z3_equiv=0,
        n=0,
        corrupt_detected=0,
        corrupt_tested=0,
        z3_render=0,
        z3_render_n=0,
    )
    per = []
    for i, (kind, _cols, g, p) in enumerate(recs):
        r = rows[i]
        stale = r["stale"]
        full = (prev_k != kind) or i == 0
        u, rowlen = (2, 160) if kind == "T" else (1, 80)
        new = g.copy() if kind == "T" else pl.g_lines(g).reshape(-1).copy()
        cbytes = 2 if (kind == "T" and (i == 0 or cur[i] != cur[i - 1])) else 0
        diff = p != prev_p
        p0 = today_stream(kind, g, prev_g, full, stale, r["fill"], r["sent"], cur, i)
        levmap = level_values(pl.g_lines(g), p) if kind == "G" else None
        p1, p2, nmap, nbpp = rect_streams(diff, p, i, value_index(kind, levmap))
        nent = palette_entries(kind, stale, levmap, last_map)
        pal = nent * IOCTL
        last_map = (
            None if kind == "T" else ({**last_map, **levmap} if last_map and not stale else levmap)
        )
        # the decoder state after a mode set or at the start is blank
        if full:
            s0, s1, s2, s3 = (blank_state(kind) for _ in range(4))
        flags = 1 if full else 0
        ch0 = (new.reshape(-1, u) != s0.reshape(-1, u)).any(1)
        m0 = bytes([flags]) + tokens(new, ch0, u)
        apply_tokens(m0, 1, s0, u)
        verify["n"] += 1
        verify["z0_exact"] += int((s0 == new).all())
        # z1: display-key quotient; a refresh whose panel image does not change is deferred
        defer = (not full) and (not diff[pl.GY0 : pl.GY0 + 200].any()) and bool(ch0.any())
        defer = defer and not stale
        if defer:
            m1 = b""
        else:
            if kind == "T":
                chg1 = text_changed_units(new, s1)
            else:
                chg1 = g_changed_units(new.reshape(200, 80), s1.reshape(200, 80))
            m1 = bytes([flags]) + tokens(new, chg1, u)
        if m1:
            apply_tokens(m1, 1, s1, u)
            corrupt_ok(m1, new, u, verify)
        verify["z1_equiv"] += int(True if defer else equivalent(kind, s1, new))
        # z2: z1 plus row-copy operations
        if defer:
            m2 = b""
        else:
            ops, sc = best_ops(
                new.reshape(-1, rowlen), s2.reshape(-1, rowlen), 3 if kind == "T" else 8
            )
            if kind == "T":
                chg2 = text_changed_units(new, sc.reshape(-1))
            else:
                chg2 = g_changed_units(new.reshape(200, 80), sc.reshape(200, 80))
            body = tokens(new, chg2, u)
            opsb = bytes([len(ops)]) + b"".join(
                bytes([o[0], o[1]]) + int(o[2]).to_bytes(1, "little", signed=True) for o in ops
            )
            use_ops = ops and 1 + len(opsb) + len(body) < len(m1)
            m2 = (bytes([2]) + opsb + body) if use_ops else m1
        if m2:
            apply_message(m2, s2, rowlen, u)
        verify["z2_equiv"] += int(True if defer else equivalent(kind, s2, new))
        # z3: z2 plus the per-line predictor message on graphics frames
        m3 = m2
        if kind == "G" and not defer:
            mp = png_encode(new.reshape(200, 80), s3)
            if len(mp) < len(m2):
                s3t = s3.copy()
                png_decode(mp, s3t)
                if not (s3t == new).all():
                    raise ValueError("line-predictor decode differs at refresh %d" % i)
                s3[:] = s3t
                m3 = mp
                tot["z3_png"] += 1
            else:
                apply_message(m2, s3, rowlen, u)
        elif m2:
            apply_message(m2, s3, rowlen, u)
        verify["z3_equiv"] += int(True if defer else equivalent(kind, s3, new))
        if not defer:
            verify["z3_render"] += int(render_check(kind, s3, new, p, cur[i]))
            verify["z3_render_n"] += 1
        for key, m in (("z0", m0), ("z1", m1), ("z2", m2), ("z3", m3)):
            streams[key] += m
        z = {
            "z0": len(m0) + cbytes,
            "z1": (len(m1) + cbytes) if m1 else 0,
            "z2": (len(m2) + cbytes) if m2 else 0,
            "z3": (len(m3) + cbytes) if m3 else 0,
        }
        # every stream carries the palette ioctls of this refresh
        p0 += pal
        p1 += pal
        p2 += pal
        z = {k: v + pal for k, v in z.items()}
        tot["p0"] += p0
        tot["p1"] += p1
        tot["p2"] += p2
        tot["two_bpp_maps"] += nmap
        for b, n in nbpp.items():
            tot["rects_%dbpp" % b] += n
        tot["pal_entries"] += nent
        tot["pal_bytes"] += pal
        for k, v in z.items():
            tot[k] += v
        tot["z1_msgs"] += int(bool(m1))
        tot["z0_msgs"] += 1
        per.append((i, kind, p0, p1, p2, z["z0"], z["z1"], z["z2"], z["z3"]))
        prev_k, prev_p, prev_g = kind, p, g
    lz = lzma.PRESET_EXTREME
    ref = {
        k: dict(
            raw=len(v),
            xz=len(lzma.compress(bytes(v), preset=9 | lz)),
            zlib=len(zlib.compress(bytes(v), 9)),
        )
        for k, v in streams.items()
    }
    return dict(tot=tot, verify=verify, ref=ref, per=per)


def self_check():
    """Known-bad and known-good rectangles for the 2-bpp code-to-index map."""
    prev = np.zeros((320, 320), np.uint16)
    prev[10:12, 10:16] = 0x1234
    p = prev.copy()
    p[10:12, 10] = pl.PAL16[0]
    p[10:12, 11:13] = pl.PAL16[5]
    p[10:12, 13:16] = pl.PAL16[9]
    _p1, size, nmap, kinds = rect_streams(p != prev, p, 1, TEXT_INDEX)
    # colors 0, 5 and 9: one 6x2 rectangle at 2 bpp (2 bytes a row) plus the map
    if (nmap, kinds[2], size) != (1, 1, IOCTL + 2 * 2 + MAP_BYTES):
        raise SystemExit("FAIL: colors 0, 5 and 9 in one rectangle need a map")
    q = prev.copy()
    q[10:12, 10] = 0
    q[10:12, 11:13] = 0x7BEF
    q[10:12, 13:16] = 0xFFFF
    vidx = value_index("G", {0: 0, 1: 0x7BEF, 2: 0xFFFF})
    _p1, size, nmap, kinds = rect_streams(q != prev, q, 1, vidx)
    if (nmap, kinds[2], size) != (0, 1, IOCTL + 2 * 2):
        raise SystemExit("FAIL: levels 0, 1 and 2 are codes 0 to 2 and need no map")


def main():
    pl.configure("Stream encodings for the paths that do not cross the wire.")
    self_check()
    res = {}
    for name in ("dos", "win"):
        res[name] = run(name, pl.read_json("analysis_a2_%s.json" % name))
    pl.write_json(
        "analysis_c.json", {n: {k: v for k, v in r.items() if k != "per"} for n, r in res.items()}
    )
    head = "i\tkind\tp0_today\tp1_rects_idx8\tp2_rects_packed\tz0_guest_delta\tz1_quotient"
    head += "\tz2_copy_ops\tz3_line_predictor\n"
    for n, r in res.items():
        with pl.out_file("stream_per_refresh_%s.tsv" % n).open("w") as f:
            f.write(head)
            for row in r["per"]:
                f.write("\t".join(str(x) for x in row) + "\n")
        print(n, r["tot"])
        print(" verify", r["verify"])
        print(" ref", r["ref"])


main()
