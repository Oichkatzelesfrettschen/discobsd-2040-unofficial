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

# c2_color_dither.py -- run: sage c2_color_dither.py
import itertools
import random
import re
from math import comb

import numpy as np
from sage.all import ZZ, Zmod, matrix


def ones_matrix(R, n, m):
    return matrix(R, n, m, [1] * (n * m))


def kronecker_product(A, B):
    return A.tensor_product(B)


print("== A. distinct mean colors of k pixels mixed from the 8 RGB corners: (k+1)^3")
print("k | distinct_means | (k+1)^3 | multisets")

corners = [(r, g, b) for r in (0, 1) for g in (0, 1) for b in (0, 1)]
for k in range(1, 7):
    s = set()
    for ms in itertools.combinations_with_replacement(range(8), k):
        s.add(tuple(sum(corners[i][c] for i in ms) for c in range(3)))
    print(k, "|", len(s), (k + 1) ** 3, comb(8 + k - 1, k))
print("closed form (k+1)^3 for k=7,9,10,15:", [(k + 1) ** 3 for k in (7, 9, 10, 15)])

print(
    "== B. same count for the console's 16-entry palette (lcd_palette, st7365p.c), "
    "cells of k pixels"
)
src = open("sys/arch/rp2040/dev/st7365p.c").read()
m = re.search(r"lcd_palette\[16\]\[3\]\s*=\s*\{(.*?)\};", src, re.S)
nums = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", m.group(1))]
pal = [tuple(nums[i : i + 3]) for i in range(0, 48, 3)]
print("palette entries:", len(pal), "first:", pal[:3], "last:", pal[-2:])
print("k | multisets | distinct_means")
for k in (1, 2, 3, 4):
    s = set()
    n = 0
    for ms in itertools.combinations_with_replacement(range(16), k):
        n += 1
        s.add(tuple(sum(pal[i][c] for i in ms) for c in range(3)))
    print(k, "|", n, "|", len(s))

print(
    "== C. CGA composite artifact colors as a 4-point DFT of the hdot pattern (VileR "
    "pp. 6-9: 4 hdots per color cycle)"
)
pats = list(itertools.product((0, 1), repeat=4))


def X1(p):
    return (p[0] - p[2], -(p[1] - p[3]))  # Gaussian integer a + b i, DFT bin 1


def mulI(z):
    return (-z[1], z[0])


classes = {}
for p in pats:
    classes.setdefault((sum(p), X1(p)), []).append("".join(map(str, p)))
print("16 patterns -> distinct (luma=popcount, chroma=X1):", len(classes))
print("zero-chroma patterns:", ["".join(map(str, p)) for p in pats if X1(p) == (0, 0)])
ok = all(
    X1(p[-1:] + p[:-1]) == mulI(mulI(mulI(X1(p)))) for p in pats
)  # a one-hdot delay multiplies X1 by -i: a 90 degree turn
print("a one-hdot delay of a pattern multiplies X1 by -i (90 degree hue step):", ok)
print(
    "chroma magnitudes^2 by pattern count:",
    {m2: sum(1 for p in pats if X1(p)[0] ** 2 + X1(p)[1] ** 2 == m2) for m2 in (0, 1, 2, 4)},
)
print(
    "1024 = 4 character waveforms x 16 foreground x 16 background =",
    4 * 16 * 16,
    "; 512 = 2 waveforms; 256 = 1 (VileR pp. 11-14)",
)

print("== D. Bayer index matrices as a Kronecker object")
D2 = matrix(ZZ, [[0, 2], [3, 1]])


def bayer_rec(m):
    M = matrix(ZZ, [[0]])
    for _ in range(m):
        n = M.nrows()
        M = 4 * kronecker_product(M, ones_matrix(ZZ, 2, 2)) + kronecker_product(
            ones_matrix(ZZ, n, n), D2
        )
    return M


def bayer_closed(m):
    n = 1 << m

    def v(r, c):
        return sum(
            4**k * (2 * (((r >> k) & 1) ^ ((c >> k) & 1)) + ((r >> k) & 1)) for k in range(m)
        )

    return matrix(ZZ, n, n, [v(r, c) for r in range(n) for c in range(n)])


for m in (1, 2, 3, 4):
    A = bayer_rec(m)
    B = bayer_closed(m)
    perm = sorted(A.list()) == list(range(4**m))
    # every aligned 2^j block holds a permutation of residues? check 2x2 blocks hold {t, t+1.. }
    # digits 0..3 pattern
    print(
        "m",
        m,
        "size",
        1 << m,
        "recursion == closed form (interleave(r xor c, r)):",
        A == B,
        "| entries are a permutation of 0..%d:" % (4**m - 1),
        perm,
    )
M8 = np.array(bayer_rec(3).list(), dtype=float).reshape(8, 8)
F = np.fft.fft2(M8 - M8.mean())
nz = int((abs(F) > 1e-9).sum())
rnd = []
random.seed(7)
for _ in range(200):
    P = np.array(random.sample(range(64), 64), dtype=float).reshape(8, 8)
    rnd.append(int((abs(np.fft.fft2(P - P.mean())) > 1e-9).sum()))
print(
    "8x8 Bayer: nonzero non-DC DFT coefficients:",
    nz,
    "of 63; random 8x8 permutations (200): min",
    min(rnd),
    "max",
    max(rnd),
)
# gray level l of 16 on a 4x4 cell lights exactly l pixels
M4 = bayer_rec(2)
print(
    "4x4 cell, level l lights l pixels for all l in 0..16:",
    all(sum(1 for x in M4.list() if x < lv) == lv for lv in range(17)),
)

print(
    "== E. SWAR RGB565 blend with guard bits, mask 0x07E0F81F: exhaustive over 65536 pixels x alpha"
)
MASK = 0x07E0F81F
px = np.arange(65536, dtype=np.uint64)


def spread(c):
    return (c | (c << np.uint64(16))) & np.uint64(MASK)


def unspread(s):
    return ((s & np.uint64(0xFFFF)) | ((s >> np.uint64(16)) & np.uint64(0xFFFF))) & np.uint64(
        0xFFFF
    )


fr = (px >> np.uint64(11)) & np.uint64(31)
fg = (px >> np.uint64(5)) & np.uint64(63)
fb = px & np.uint64(31)


def check(alpha, other):
    a = spread(px)
    b = spread(np.full_like(px, other))
    s = (a * np.uint64(alpha) + b * np.uint64(32 - alpha)) >> np.uint64(5)
    s = s & np.uint64(MASK)
    got = unspread(s)
    o = np.uint64(other)
    orr = (o >> np.uint64(11)) & np.uint64(31)
    og = (o >> np.uint64(5)) & np.uint64(63)
    ob = o & np.uint64(31)
    wr = (fr * np.uint64(alpha) + orr * np.uint64(32 - alpha)) >> np.uint64(5)
    wg = (fg * np.uint64(alpha) + og * np.uint64(32 - alpha)) >> np.uint64(5)
    wb = (fb * np.uint64(alpha) + ob * np.uint64(32 - alpha)) >> np.uint64(5)
    want = (wr << np.uint64(11)) | (wg << np.uint64(5)) | wb
    return int((got != want).sum())


bad = {}
for alpha in (0, 1, 8, 16, 31, 32):
    bad[alpha] = sum(check(alpha, o) for o in (0x0000, 0xFFFF, 0x7BEF, 0xF81F, 0x07E0, 0xA5A5))
print("mismatching pixels by alpha (6 partner colors x 65536 pixels):", bad)
# why: field window widths vs worst product
print(
    "worst field products at alpha=32 vs window: b 31*32=%d <2^11?" % (31 * 32),
    31 * 32 < 2**11,
    "| r",
    31 * 32 < 2**10,
    "| g",
    63 * 32,
    63 * 32 < 2**11,
    "| alpha=33 g:",
    63 * 33,
    63 * 33 < 2**11,
)

print("== F. Z/2^k is a local ring: lane isolation is not a ring decomposition")
for k in (8, 16):
    R = Zmod(2**k)
    print(
        "Z/2^%d: idempotents" % k,
        R.idempotents(),
        "| nilpotents",
        sum(1 for z in range(2**k) if z % 2 == 0) if k <= 16 else None,
        "of",
        2**k,
        "(all even elements)",
    )
z = 256
print(
    "z = 2^8 in Z/2^32: z^3 mod 2^32 =",
    pow(z, 3, 2**32),
    ", z^4 mod 2^32 =",
    pow(z, 4, 2**32),
    "(nilpotent of index 4; a byte-lane shift kills the top byte)",
)

print(
    "== G. interpolator lane model calibrated on the datasheet's own examples (RP2040 "
    "datasheet pp. 34-38)"
)


def lane(accum, shift, lsb, msb, signed):
    v = (accum & 0xFFFFFFFF) >> shift
    mask = ((1 << (msb - lsb + 1)) - 1) << lsb
    v &= mask
    if signed and (v >> msb) & 1:
        v |= (0xFFFFFFFF << msb) & 0xFFFFFFFF
    return v & 0xFFFFFFFF


exp_u = [
    0x0000000D,
    0x000000C0,
    0x00000B00,
    0x0000A000,
    0x00040000,
    0x00300000,
    0x02000000,
    0x10000000,
]
exp_s = [
    0xFFFFFFFD,
    0xFFFFFFC0,
    0xFFFFFB00,
    0xFFFFA000,
    0x00040000,
    0x00300000,
    0x02000000,
    0x10000000,
]
got_u = [lane(0x1234ABCD, 0, 4 * i, 4 * i + 3, False) for i in range(8)]
got_s = [lane(0x1234ABCD, 0, 4 * i, 4 * i + 3, True) for i in range(8)]
print(
    "moving_mask unsigned matches datasheet:", got_u == exp_u, "| signed matches:", got_s == exp_s
)
print(
    "worked example 0xdeadbeef >>8 mask 4..7 signed:",
    hex(lane(0xDEADBEEF, 8, 4, 7, True)),
    "(datasheet 0xffffffb0)",
)
blend = [500 + ((1000 - 500) * (255 * i // 6)) // 256 for i in range(7)]
print(
    "blend example (BASE0 + alpha*(BASE1-BASE0)>>8):",
    blend,
    "(datasheet 500 582 666 748 832 914 998)",
)
clamp = [(i, min(max(i >> 2, 0), 255)) for i in range(-1024, 1025, 256)]
print("clamp example:", [c[1] for c in clamp], "(datasheet 0 0 0 0 0 64 128 192 255)")
# palette address generation: lane k extracts 2*index of nibble k; BASE0 adds the table address
random.seed(1)
okp = True
TBL = 0x20001000
for _ in range(2000):
    w = random.getrandbits(32)
    for k in range(1, 8):
        a = lane(w, 4 * k - 1, 1, 4, False) + TBL
        okp &= a == TBL + 2 * ((w >> (4 * k)) & 15)
print(
    "lane (shift 4k-1, mask 1..4) + BASE0 == table + 2*nibble_k for k=1..7:",
    okp,
    "(nibble 0 would need a left shift: not available in one lane)",
)
