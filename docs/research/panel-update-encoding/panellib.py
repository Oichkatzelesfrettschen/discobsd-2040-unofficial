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
"""Shared pieces of the panel-update-encoding analyses.

The module loads the recorded corpora, evaluates the display map D (guest
state to panel pixels), covers a damage mask with windows under the wire cost
rule, and reproduces today's i86 redraw rules. Every script calls
configure() first; the corpus directory, the output directory and the i86
font header come from the command line.
"""

import argparse
import ctypes
import json
import re
from functools import cache
from pathlib import Path

import numpy as np

GY0 = 60  # panel line of the guest's line 0
LINES = 200  # the guest's scan lines
CFG = {}


def configure(description, extra=None, need_corpus=True):
    """Parse the common arguments, remember them and return the namespace."""
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=description)
    ap.add_argument(
        "--corpus",
        type=Path,
        required=need_corpus,
        help="directory holding dos.dump, dos.log, win.dump and win.log",
    )
    ap.add_argument(
        "--out",
        type=Path,
        required=True,
        help="directory for derived files (librectopt.so, JSON, TSV)",
    )
    ap.add_argument(
        "--font-header",
        type=Path,
        default=here.parents[2] / "usr.bin" / "i86" / "font.h",
        help="usr.bin/i86/font.h, the source of the 80-column glyphs",
    )
    if extra is not None:
        extra(ap)
    args = ap.parse_args()
    CFG.update(corpus=args.corpus, out=args.out, font=args.font_header)
    args.out.mkdir(parents=True, exist_ok=True)
    return args


def out_file(name):
    return CFG["out"] / name


def read_json(name):
    return json.loads(out_file(name).read_text())


def write_json(name, obj):
    out_file(name).write_text(json.dumps(obj))


def load(name):
    """Records of <name>.dump: (kind, columns, guest bytes, 320x320 RGB565 panel)."""
    raw = (CFG["corpus"] / (name + ".dump")).read_bytes()
    recs = []
    o = 0
    while o < len(raw):
        kind = chr(raw[o])
        cols = raw[o + 1]
        o += 2
        n = 4000 if kind == "T" else 16000
        guest = np.frombuffer(raw[o : o + n], dtype=np.uint8)
        o += n
        panel = np.frombuffer(raw[o : o + 204800], dtype="<u2").reshape(320, 320)
        o += 204800
        recs.append((kind, cols, guest, panel))
    if o != len(raw):
        raise ValueError(name + ".dump ends inside a record")
    return recs


def logs(name):
    """Rows of <name>.log: kind, sent, fill, units, span, shifted pixels, shift."""
    out = []
    for line in (CFG["corpus"] / (name + ".log")).read_text().strip().split("\n"):
        f = line.split()
        out.append((f[0], *[int(x) for x in f[1:]]))
    return out


@cache
def glyphs4():
    """font4 as (256, 4) bytes: two rows of the 4x8 cell in each byte."""
    text = CFG["font"].read_text()
    i = text.index("font4[256][4]")
    j = text.index("};", i)
    vals = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", text[i:j])]
    a = np.array(vals, dtype=np.uint8)
    if a.size != 256 * 4:
        raise ValueError("font4 has %d bytes" % a.size)
    return a.reshape(256, 4)


@cache
def glyph_planes():
    """Glyph bits of the 80-column cell: (256, 8, 4), bit 3 of a nibble is the left pixel."""
    f4 = glyphs4()
    gl = np.zeros((256, 8, 4), dtype=bool)
    for ch in range(256):
        for y in range(8):
            bits = (int(f4[ch][y >> 1]) >> (0 if (y & 1) else 4)) & 15
            for x in range(4):
                gl[ch, y, x] = (bits >> (3 - x)) & 1
    return gl


def rgb565(irgb, half=0):
    """The panel value of adapter color irgb as usr.bin/i86/gfx.c pal_set() builds it."""
    lo = 0x55 if irgb & 8 else 0
    hi = 0xAA
    r = (hi if irgb & 4 else 0) + lo
    g = (hi if irgb & 2 else 0) + lo
    b = (hi if irgb & 1 else 0) + lo
    if irgb == 6:
        g = 0x55
    if half:
        r >>= 1
        g >>= 1
        b >>= 1
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


PAL16 = np.array([rgb565(i) for i in range(16)], dtype=np.uint16)


def render_text(g, cursor=None):
    """D for 80x25 text: 4000 guest bytes to 200x320 RGB565 (cursor is a cell index or None)."""
    cell = g.reshape(25, 80, 2)
    ch = cell[..., 0]
    at = cell[..., 1]
    fg = PAL16[at & 15]
    bg = PAL16[(at >> 4) & 7]
    gl = glyph_planes()[ch]  # 25, 80, 8, 4
    px = np.where(gl, fg[:, :, None, None], bg[:, :, None, None]).astype(np.uint16)
    if cursor is not None:
        r, c = divmod(cursor, 80)
        px[r, c, 6:8, :] = fg[r, c]
    return px.transpose(0, 2, 1, 3).reshape(200, 320)


def g_lines(g):
    """CGA guest data (even bank, odd bank) to 200 x 80 bytes, line y = 2 * row + bank."""
    a = g.reshape(2, 100, 80)
    out = np.empty((200, 80), np.uint8)
    out[0::2] = a[0]
    out[1::2] = a[1]
    return out


def g_levels(lines):
    """Mode 6: 200x80 bytes to 200x320 levels 0..2, the sum of each pixel pair."""
    bits = np.unpackbits(lines, axis=1)  # 200x640, most significant bit first
    return (bits[:, 0::2] + bits[:, 1::2]).astype(np.uint8)


def g_bits(lines):
    return np.unpackbits(lines, axis=1)


def fillcost(px):
    """Wire bytes of one fill of px pixels: one window header plus 2 bytes a pixel."""
    return 2 * px + 11


def wcost(w, h):
    """libwin's rule: 2wh + 11 * ceil(h / floor(5120 / w))."""
    per = max(1, 5120 // w)
    return 2 * w * h + 11 * (-(-h // per))


@cache
def _lib():
    lib = ctypes.CDLL(str(CFG["out"] / "librectopt.so"))
    lib.rect_cover.restype = ctypes.c_long
    lib.rect_cover.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
    ]
    lib.rect_emit.restype = ctypes.c_long
    lib.rect_emit.argtypes = [*lib.rect_cover.argtypes, ctypes.c_void_p, ctypes.c_int]
    return lib


def _orient(m, tr, rowcuts, colcuts):
    if tr == 0:
        return m, np.array(rowcuts, dtype=np.int32), np.array(colcuts, dtype=np.int32)
    return (
        np.ascontiguousarray(m.T),
        np.array(colcuts, dtype=np.int32),
        np.array(rowcuts, dtype=np.int32),
    )


def _ptr(a):
    return a.ctypes.data if len(a) else None


def cover(mask, rowcuts=(), colcuts=()):
    """Cheapest stripe cover of a boolean mask: (wire bytes, rectangle count).

    A row cut c is the boundary between rows c-1 and c that no window may straddle.
    """
    m = np.ascontiguousarray(mask.astype(np.uint8))
    if not m.any():
        return 0, 0
    best = None
    for tr in (0, 1):
        mm, rc, cc = _orient(m, tr, rowcuts, colcuts)
        n = ctypes.c_int(0)
        r = _lib().rect_cover(
            mm.ctypes.data,
            mm.shape[0],
            mm.shape[1],
            tr,
            _ptr(rc),
            len(rc),
            _ptr(cc),
            len(cc),
            ctypes.byref(n),
        )
        if best is None or r < best[0]:
            best = (int(r), int(n.value))
    return best


def cover_rects(mask, rowcuts=(), colcuts=()):
    """Like cover, with the rectangle list [(x, y, w, h)] in panel orientation."""
    m = np.ascontiguousarray(mask.astype(np.uint8))
    if not m.any():
        return 0, []
    best = None
    for tr in (0, 1):
        mm, rc, cc = _orient(m, tr, rowcuts, colcuts)
        n = ctypes.c_int(0)
        out = np.zeros(4 * 4096, np.int32)
        r = _lib().rect_emit(
            mm.ctypes.data,
            mm.shape[0],
            mm.shape[1],
            tr,
            _ptr(rc),
            len(rc),
            _ptr(cc),
            len(cc),
            ctypes.byref(n),
            out.ctypes.data,
            4096,
        )
        rl = [tuple(int(v) for v in out[4 * i : 4 * i + 4]) for i in range(n.value)]
        if tr == 1:
            rl = [(y, x, h, w) for (x, y, w, h) in rl]
        if best is None or r < best[0]:
            best = (int(r), rl)
    return best


def t_windows(g, redraw):
    """Windows and payload bytes of today's text refresh of the cells in redraw.

    A window is a run of redrawn cells of one attribute in one row; the payload is
    one bit a pixel with each row padded to a byte.
    """
    cell = g.reshape(2000, 2)
    n_win = 0
    rows = {}
    for c in sorted(redraw):
        rows.setdefault(c // 80, []).append(c)
    pay = 0
    for cs in rows.values():
        s = set(cs)
        used = set()
        for c in cs:
            if c in used:
                continue
            n = 1
            while (c + n) % 80 != 0 and (c + n) in s and cell[c + n][1] == cell[c][1]:
                n += 1
            for q in range(n):
                used.add(c + q)
            n_win += 1
            pay += 8 * ((4 * n + 7) // 8)
    return n_win, pay


def g_dirty_lines(chg):
    """Scan lines today's graphics refresh redraws: every line a written 1 KB page covers."""
    lines = np.zeros(200, bool)
    for bank in (0, 1):
        for p in range(8):
            ys = [r for r in range((p << 10) // 80, 100) if r * 80 < (p + 1) << 10]
            offs = np.arange(1024 * p, min(1024 * (p + 1), 8000))
            if len(offs) == 0:
                continue
            rr, cc = offs // 80, offs % 80
            if chg[2 * rr + bank, cc].any():
                lines[[2 * r + bank for r in ys]] = True
    return lines


def runs_of_lines(lines):
    """(first line, count) of each run of consecutive redrawn lines."""
    ws = []
    y = 0
    while y < 200:
        if lines[y]:
            n = 1
            while y + n < 200 and lines[y + n]:
                n += 1
            ws.append((y, n))
            y += n
        else:
            y += 1
    return ws
