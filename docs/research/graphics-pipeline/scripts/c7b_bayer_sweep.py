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

# c7b_bayer_sweep.py -- Bayer spread sweep over four palettes, with Floyd-Steinberg, on one
# scikit-learn sample image named by argv[1] (china.jpg or flower.jpg).
import math
import re
import sys
import time

import numpy as np
from PIL import Image
from scipy.ndimage import uniform_filter
from sklearn.cluster import KMeans
from sklearn.datasets import load_sample_image

img = load_sample_image(sys.argv[1])
im = Image.fromarray(img).resize((320, 214), Image.LANCZOS)
X = np.asarray(im, dtype=float)
H, W, _ = X.shape
Xf = X.reshape(-1, 3)
src = open("sys/arch/rp2040/dev/st7365p.c").read()
m = re.search(r"lcd_palette\[16\]\[3\]\s*=\s*\{(.*?)\};", src, re.S)
nums = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", m.group(1))]
console = np.array(nums, dtype=float).reshape(16, 3)


def nearest(P, Q):
    d = ((P[:, None, :] - Q[None, :, :]) ** 2).sum(-1)
    return d.argmin(1)


def median_cut(Pix, k):
    boxes = [Pix]
    while len(boxes) < k:
        i = max(
            range(len(boxes)),
            key=lambda j: (boxes[j].max(0) - boxes[j].min(0)).max() * len(boxes[j]) ** 0.0
            if len(boxes[j]) > 1
            else -1,
        )
        b = boxes.pop(i)
        ch = int((b.max(0) - b.min(0)).argmax())
        b = b[b[:, ch].argsort()]
        h = len(b) // 2
        boxes += [b[:h], b[h:]]
    return np.array([b.mean(0) for b in boxes])


t0 = time.time()
uni = np.array(
    [[r, g, b] for r in (0, 255) for g in (0, 255) for b in (0, 85, 170, 255)], dtype=float
)
km = KMeans(16, n_init=4, random_state=0).fit(Xf).cluster_centers_
mc = median_cut(Xf, 16)


def psnr(a, b):
    return 10 * math.log10(255**2 / ((a - b) ** 2).mean())


def blur(a):
    return uniform_filter(a, size=(5, 5, 1), mode="nearest")


Xb = blur(X)
bay = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=float)
thr = (np.tile(bay, (H // 4 + 1, W // 4 + 1))[:H, :W] + 0.5) / 16.0 - 0.5


def ordered(P, spread):
    Y = (X + thr[:, :, None] * spread).reshape(-1, 3)
    return P[nearest(Y, P)].reshape(H, W, 3)


def fs(P):
    A = X.copy()
    out = np.zeros_like(X)
    for y in range(H):
        xs = range(W) if y % 2 == 0 else range(W - 1, -1, -1)
        sgn = 1 if y % 2 == 0 else -1
        for x in xs:
            old = A[y, x]
            q = P[((P - old) ** 2).sum(1).argmin()]
            out[y, x] = q
            e = old - q
            if 0 <= x + sgn < W:
                A[y, x + sgn] += e * 7 / 16
            if y + 1 < H:
                if 0 <= x - sgn < W:
                    A[y + 1, x - sgn] += e * 3 / 16
                A[y + 1, x] += e * 5 / 16
                if 0 <= x + sgn < W:
                    A[y + 1, x + sgn] += e * 1 / 16
    return out


print("== sweep of the Bayer spread; image", sys.argv[1])
for name, P in (
    ("console RGBI", console),
    ("uniform 2x2x4", uni),
    ("median cut", mc),
    ("k-means", km),
):
    base = psnr(blur(P[nearest(Xf, P)].reshape(H, W, 3)), Xb)
    sw = {s: psnr(blur(ordered(P, float(s))), Xb) for s in (8, 16, 24, 32, 48, 64, 96)}
    fsv = psnr(blur(fs(P)), Xb)
    bs = max(sw, key=sw.get)
    print(
        "%-14s none %.2f | Bayer by spread %s | best spread %d -> %.2f | Floyd-Steinberg %.2f"
        % (name, base, " ".join("%d:%.2f" % (s, v) for s, v in sw.items()), bs, sw[bs], fsv)
    )
