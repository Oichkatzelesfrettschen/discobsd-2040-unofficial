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

# c8_sprite_wire.py -- wire bytes a moving ridge sprite costs under three drawing methods, from the
# sprite art in games/ridge/ridge.c (rg_art) and the RGB565 wire model of c5_wire.py
import re
import sys

PIXEL = 2  # RGB565 bytes a pixel; ridge selects COLMOD 0x55 (rg_view_init)
WINDOW = 11  # CASET, RASET and RAMWR setup bytes (games/gametile.h)
TILE = 64 * PIXEL
CELL = 8  # LCDG_TILE_W
BOX = None  # RG_BOX, read from ridge.c by main()
# (label, frame before the step, frame after it): the running and crawling frames alternate
# (rg_sprites), so a step may also change the frame.
FRAMES = (("climber running, same frame", 1, 1), ("climber running, frame 1 to 2", 1, 2),
          ("climber jumping", 3, 3), ("crawler, same frame", 4, 4),
          ("crawler, frame 4 to 5", 4, 5))
MOVES = (("run 1 px", 1, 0), ("rise 2 px", 0, -2), ("fall 4 px", 0, 4), ("run 1, rise 2", 1, -2))


def art(path):
    src = open(path).read()
    block = re.search(r"rg_art\[6\]\[16\]\[17\] = \{(.*?)\n\};", src, re.S)
    if block is None:
        raise SystemExit("rg_art not found in " + path)
    rows = re.findall(r'"([^"]{16})"', block.group(1))
    if len(rows) != 6 * 16:
        raise SystemExit("rg_art holds %d rows, expected 96" % len(rows))
    return [rows[i * 16:(i + 1) * 16] for i in range(6)]


def opaque(frame):
    return {(x, y) for y, row in enumerate(frame) for x, c in enumerate(row) if c != "."}


def row_runs(points):
    """Maximal runs of consecutive x in each row of a set of (x, y)."""
    n = 0
    for y in {p[1] for p in points}:
        xs = sorted(p[0] for p in points if p[1] == y)
        n += 1 + sum(1 for a, b in zip(xs, xs[1:]) if b != a + 1)
    return n


def cells(px, ox, oy):
    return {((ox + x) // CELL, (oy + y) // CELL) for x, y in px}


def box(ox, oy):
    cx, cy = ox // CELL, oy // CELL
    return {(cx + i, cy + j) for i in range(BOX) for j in range(BOX)}


def tiles(cs):
    """gtile_flush: one window per run of marked cells in a tile row, 64 pixels a cell."""
    return len(cs) * TILE + row_runs(cs) * WINDOW


def define(src, name):
    m = re.search(r"#define\s+%s\s+(\d+)" % name, src)
    if m is None:
        raise SystemExit(name + " not found")
    return int(m.group(1))


def expected(test):
    """The windows and tiles render_test.c requires of a step inside a cell and across an edge."""
    src = open(test).read()
    out = []
    for label in ("one pixel step", "step across a cell edge"):
        m = re.search(r"CHECK\(w == (\d+) && d->ntiles == (\d+)[^;]*\"%s" % label, src)
        if m is None:
            raise SystemExit("render_test.c check not found: " + label)
        out.append((int(m.group(1)), int(m.group(2))))
    return out


def calibrate(path, test):
    # The opaque-cell model must reproduce render_test.c's windows and tiles for a one-pixel step
    # and a step across a cell edge by the standing climber, whose top left pixel moves from
    # (80, 176) in view coordinates. The box model, which counts all nine cells of each box, must
    # disagree with the same expectations, or the check cannot tell the two rules apart.
    px = opaque(art(path)[define(open(path).read(), "RG_F_STAND")])
    inside = cells(px, 80, 176) | cells(px, 81, 176)
    edge = cells(px, 81, 176) | cells(px, 88, 176)
    got = [(row_runs(inside), len(inside)), (row_runs(edge), len(edge))]
    if got != expected(test):
        raise SystemExit("opaque model %s disagrees with render_test %s" % (got, expected(test)))
    in_box = box(CELL + 1, CELL) | box(CELL + 2, CELL)
    edge_box = box(2 * CELL - 1, CELL) | box(2 * CELL, CELL)
    boxes = [(row_runs(in_box), len(in_box)), (row_runs(edge_box), len(edge_box))]
    if boxes == expected(test):
        raise SystemExit("box model %s reproduces render_test %s: no known-bad rule is rejected"
                         % (boxes, expected(test)))


def main(path, test):
    global BOX
    BOX = define(open(path).read(), "RG_BOX")
    calibrate(path, test)
    frames = art(path)
    # The LCDG_SPRITE column counts drawing bytes alone; the LCDG_WAITLINE fence it needs
    # against flicker adds four bytes for every GSCAN poll, a number the scan phase decides.
    print("sprite / move | opaque px after | runs after | box atlas | opaque-cell atlas"
          " | LCDG_SPRITE unfenced")
    for name, f0, f1 in FRAMES:
        px0, px = opaque(frames[f0]), opaque(frames[f1])
        sprite_px = len(px) * PIXEL + row_runs(px) * WINDOW
        for move, dx, dy in MOVES:
            a = b = c = n = 0
            for ox in range(CELL, 2 * CELL):
                for oy in range(CELL, 2 * CELL):
                    old, new = cells(px0, ox, oy), cells(px, ox + dx, oy + dy)
                    a += tiles(box(ox, oy) | box(ox + dx, oy + dy))
                    b += tiles(old | new)
                    c += tiles(old) + sprite_px
                    n += 1
            print("%s / %s | %d | %d | %.0f | %.0f | %.0f"
                  % (name, move, len(px), row_runs(px), a / n, b / n, c / n))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "games/ridge/ridge.c",
         sys.argv[2] if len(sys.argv) > 2 else "games/ridge/tests/render_test.c")
