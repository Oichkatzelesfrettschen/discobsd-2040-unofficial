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

# c6_damage.py -- libwin damage_add (lib/libwin/win.c:60-266) ported line for line; cost of the
# painted set under the wire model
import random


class R:
    def __init__(s, x0, y0, x1, y1):
        s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1

    def copy(s):
        return R(s.x0, s.y0, s.x1, s.y1)

    def area(s):
        return max(0, s.x1 - s.x0) * max(0, s.y1 - s.y0)


def empty(r):
    return r.x0 >= r.x1 or r.y0 >= r.y1


def has(a, b):
    return a.x0 <= b.x0 and a.y0 <= b.y0 and a.x1 >= b.x1 and a.y1 >= b.y1


def overlap(a, b):
    return a.x0 < b.x1 and b.x0 < a.x1 and a.y0 < b.y1 and b.y0 < a.y1


def join(a, b):
    if a.y0 == b.y0 and a.y1 == b.y1 and a.x0 <= b.x1 and b.x0 <= a.x1:
        a.x0 = min(a.x0, b.x0)
        a.x1 = max(a.x1, b.x1)
        return 1
    if a.x0 == b.x0 and a.x1 == b.x1 and a.y0 <= b.y1 and b.y0 <= a.y1:
        a.y0 = min(a.y0, b.y0)
        a.y1 = max(a.y1, b.y1)
        return 1
    return 0


def trim(a, b):
    if b.y0 <= a.y0 and b.y1 >= a.y1:
        if b.x0 <= a.x0:
            a.x0 = b.x1
            return 1
        if b.x1 >= a.x1:
            a.x1 = b.x0
            return 1
    elif b.x0 <= a.x0 and b.x1 >= a.x1:
        if b.y0 <= a.y0:
            a.y0 = b.y1
            return 1
        if b.y1 >= a.y1:
            a.y1 = b.y0
            return 1
    return 0


def damage_add(lst, x0, y0, x1, y1, bbox_policy=False, W=320, H=320):
    n = R(max(x0, 0), max(y0, 0), min(x1, W), min(y1, H))
    if empty(n):
        return
    again = True
    while again:
        again = False
        i = 0
        while i < len(lst) and not again:
            e = lst[i]
            if has(e, n):
                return
            if has(n, e) or join(n, e):
                lst.pop(i)
                again = True
            elif overlap(n, e) and (trim(n, e) or trim(e, n)):
                again = True
            elif bbox_policy and overlap(n, e):
                bb = R(min(n.x0, e.x0), min(n.y0, e.y0), max(n.x1, e.x1), max(n.y1, e.y1))
                if bb.area() < n.area() + e.area():
                    n = bb
                    lst.pop(i)
                    again = True
            i += 1
    lst.append(n)


px = 16 / 12.5e6
setup = 88 / 12.5e6


def cost(lst):
    c = 0.0
    for r in lst:
        w = r.x1 - r.x0
        h = r.y1 - r.y0
        strips = -(-h // max(1, 5120 // w))  # repaint(): WIN_STRIP_BYTES / w rows per strip
        c += strips * setup + r.area() * px
    return c


def union_area(old, new):
    ox = max(0, min(old.x1, new.x1) - max(old.x0, new.x0))
    oy = max(0, min(old.y1, new.y1) - max(old.y0, new.y0))
    return old.area() + new.area() - ox * oy


print(
    "win_move of a w x h window by (dx, dy): painted area and wire time of the damage "
    "list, current policy vs bbox-merge-when-cheaper"
)
print(
    "%-9s %-9s | %7s %7s %7s | %8s %8s %8s | %s"
    % (
        "w x h",
        "(dx,dy)",
        "union",
        "current",
        "bbox",
        "cur ms",
        "bbox ms",
        "ideal ms",
        "rects(current)",
    )
)
for w, h in ((100, 100), (160, 120), (200, 20)):
    for dx, dy in ((5, 0), (0, 5), (5, 5), (2, 1), (16, 16), (50, 50)):
        old = R(10, 10, 10 + w, 10 + h)
        new = R(10 + dx, 10 + dy, 10 + dx + w, 10 + dy + h)
        res = []
        for pol in (False, True):
            lst = []
            damage_add(lst, old.x0, old.y0, old.x1, old.y1, pol)
            damage_add(lst, new.x0, new.y0, new.x1, new.y1, pol)
            res.append((sum(r.area() for r in lst), cost(lst), len(lst)))
        ua = union_area(old, new)
        print(
            "%-9s %-9s | %7d %7d %7d | %8.2f %8.2f %8.2f | %d"
            % (
                "%dx%d" % (w, h),
                "(%d,%d)" % (dx, dy),
                ua,
                res[0][0],
                res[1][0],
                res[0][1] * 1e3,
                res[1][1] * 1e3,
                (setup + ua * px) * 1e3,
                res[0][2],
            )
        )
print()
print(
    "random corner-overlapping pairs on the 320x320 panel: fraction kept as two "
    "overlapping rectangles, mean painted/union"
)
random.seed(3)
tot = 0
kept = 0
ratio = 0.0
for _ in range(20000):
    a = R(random.randint(0, 200), random.randint(0, 200), 0, 0)
    a.x1 = a.x0 + random.randint(20, 120)
    a.y1 = a.y0 + random.randint(20, 120)
    b = R(random.randint(0, 200), random.randint(0, 200), 0, 0)
    b.x1 = b.x0 + random.randint(20, 120)
    b.y1 = b.y0 + random.randint(20, 120)
    if not overlap(a, b):
        continue
    lst = []
    damage_add(lst, a.x0, a.y0, a.x1, a.y1)
    damage_add(lst, b.x0, b.y0, b.x1, b.y1)
    tot += 1
    painted = sum(r.area() for r in lst)
    u = union_area(a, b)
    ratio += painted / u
    if painted > u:
        kept += 1
print(
    "overlapping pairs",
    tot,
    "| painted twice in part:",
    kept,
    "(%.1f%%)" % (100.0 * kept / tot),
    "| mean painted/union %.3f" % (ratio / tot),
)
