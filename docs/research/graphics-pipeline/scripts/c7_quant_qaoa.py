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

# c7_quant_qaoa.py -- palette quantization comparison and a simulated QAOA for palette selection
# (numpy statevector, cross-checked with qiskit)
import math
import random
import re
import time

import numpy as np
from PIL import Image
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector
from scipy.ndimage import uniform_filter
from scipy.optimize import minimize
from sklearn.cluster import KMeans
from sklearn.datasets import load_sample_image

img = load_sample_image("china.jpg")
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


print(
    "image: sklearn sample china.jpg resized to %dx%d; metric = PSNR dB of the "
    "5x5-blurred result against the blurred original (viewing-distance proxy), "
    "then raw per-pixel PSNR" % (W, H)
)
print("%-22s | %-11s | %9s | %9s" % ("palette (16)", "dither", "blur PSNR", "raw PSNR"))
for name, P in (
    ("console RGBI (st7365p)", console),
    ("uniform 2x2x4 grid", uni),
    ("median cut", mc),
    ("k-means", km),
):
    for dname, fn in (
        ("none", lambda P: P[nearest(Xf, P)].reshape(H, W, 3)),
        ("Bayer 4x4 s=64", lambda P: ordered(P, 64.0)),
        ("Floyd-Steinberg", fs),
    ):
        out = fn(P)
        print("%-22s | %-11s | %9.2f | %9.2f" % (name, dname, psnr(blur(out), Xb), psnr(out, X)))
print("elapsed %.1f s" % (time.time() - t0))

print()
print(
    "== palette selection as a QUBO: choose k=4 of n=12 candidate colors (the 12-means "
    "centroids) to maximize pairwise dispersion"
)
n, k = 12, 4
cand = KMeans(n, n_init=4, random_state=1).fit(Xf).cluster_centers_
Dm = ((cand[:, None, :] - cand[None, :, :]) ** 2).sum(-1)
Dm = Dm / Dm.max()
lam = 1.5
N = 1 << n
bits = ((np.arange(N)[:, None] >> np.arange(n)[None, :]) & 1).astype(float)
pop = bits.sum(1)
C = -0.5 * np.einsum("si,ij,sj->s", bits, Dm, bits) + lam * (pop - k) ** 2  # minimize
feas = pop == k
best = int(np.argmin(np.where(feas, C, np.inf)))
print(
    "exhaustive over %d feasible subsets: best mask %s cost %.4f"
    % (int(feas.sum()), format(best, "012b"), C[best])
)


def true_err(mask):
    sel = cand[[j for j in range(n) if (mask >> j) & 1]]
    return ((Xf - sel[nearest(Xf, sel)]) ** 2).mean()


errs = {s: true_err(s) for s in range(N) if feas[s]}
order = sorted(errs, key=errs.get)
rank = order.index(best) + 1
print(
    "true image error of the dispersion optimum: %.1f (rank %d of %d; best true %.1f, worst %.1f)"
    % (errs[best], rank, len(order), errs[order[0]], errs[order[-1]])
)
# classical: greedy and annealing on the same QUBO
t0 = time.time()
sel = []
rem = list(range(n))
a, b = max(((i, j) for i in range(n) for j in range(i + 1, n)), key=lambda t: Dm[t])
sel = [a, b]
while len(sel) < k:
    sel.append(max((j for j in range(n) if j not in sel), key=lambda j: sum(Dm[j, s] for s in sel)))
gm = sum(1 << j for j in sel)
tg = time.time() - t0
print(
    "greedy farthest-pair + marginal: cost %.4f (optimum %.4f), %.2f ms"
    % (C[gm], C[best], tg * 1e3)
)
random.seed(5)
t0 = time.time()
cur = random.getrandbits(n)
cc = C[cur]
bestsa = cur
for step in range(3000):
    T = 1.0 * (1 - step / 3000) + 1e-3
    j = random.randrange(n)
    nm = cur ^ (1 << j)
    if C[nm] < cc or random.random() < math.exp((cc - C[nm]) / T):
        cur, cc = nm, C[nm]
    if cc < C[bestsa]:
        bestsa = cur
print(
    "simulated annealing (3000 flips): cost %.4f, found optimum: %s, %.1f ms"
    % (C[bestsa], bestsa == best, (time.time() - t0) * 1e3)
)
# QAOA statevector
idx = np.arange(N)


def apply_mixer(psi, beta):
    c, s = math.cos(beta), -1j * math.sin(beta)
    for j in range(n):
        psi = c * psi + s * psi[idx ^ (1 << j)]
    return psi


def qaoa(params, p):
    psi = np.full(N, 1 / math.sqrt(N), dtype=complex)
    for layer in range(p):
        psi = psi * np.exp(-1j * params[2 * layer] * C)
        psi = apply_mixer(psi, params[2 * layer + 1])
    return psi


def energy(params, p):
    pr = abs(qaoa(params, p)) ** 2
    return float((pr * C).sum())


rng = np.random.default_rng(11)
res = {}
for p in (1, 2, 3):
    bestE, bestx = 1e9, None
    for _ in range(10):
        x0 = rng.uniform(0, 1, 2 * p) * np.tile([0.5, 1.0], p)
        r = minimize(energy, x0, args=(p,), method="COBYLA", options={"maxiter": 250})
        if r.fun < bestE:
            bestE, bestx = r.fun, r.x
    pr = abs(qaoa(bestx, p)) ** 2
    res[p] = (bestE, bestx, pr)
    print(
        "QAOA p=%d: <C>=%.4f (opt %.4f, uniform-state <C>=%.4f) | P(optimum)=%.4f vs "
        "1/4096=%.5f | P(any feasible)=%.3f | P(top-1 under measurement)=%s"
        % (
            p,
            bestE,
            C[best],
            float(C.mean()),
            pr[best],
            1 / N,
            pr[feas].sum(),
            int(pr.argmax()) == best,
        )
    )
# cross-check with qiskit statevector for p=2
p = 2
g = res[p][1]
qc = QuantumCircuit(n)
qc.h(range(n))
a_lin = np.zeros(n)
b_quad = {}
for j in range(n):
    a_lin[j] += lam * (
        1 - 2 * k
    )  # (sum z - k)^2 = (1 - 2k) sum z + 2 sum_{j<l} z z + k^2  (z^2 = z)
for j in range(n):
    for q in range(j + 1, n):
        b_quad[(j, q)] = -Dm[j, q] + 2 * lam
# C = sum a_j z_j + sum b_jl z_j z_l + const ; z = (1 - Z)/2
h = np.zeros(n)
J = {}
for j in range(n):
    h[j] += -a_lin[j] / 2
for (j, q), b in b_quad.items():
    h[j] += -b / 4
    h[q] += -b / 4
    J[(j, q)] = b / 4
for layer in range(p):
    for j in range(n):
        qc.rz(2 * g[2 * layer] * h[j], j)
    for (j, q), v in J.items():
        qc.rzz(2 * g[2 * layer] * v, j, q)
    for j in range(n):
        qc.rx(2 * g[2 * layer + 1], j)
sv = Statevector.from_instruction(qc)
pq = np.abs(sv.data) ** 2
print(
    "qiskit %s Statevector of the same p=2 circuit: max |P_numpy - P_qiskit| = %.2e ; "
    "gates: %d (n=%d, p=2)"
    % (__import__("qiskit").__version__, float(np.abs(pq - res[2][2]).max()), qc.size(), n)
)
print(
    "Grover on %d feasible subsets: ~%d oracle calls vs %d classical evaluations of an "
    "O(k^2) cost; classical wins on wall time"
    % (int(feas.sum()), int(math.pi / 4 * math.sqrt(feas.sum())), int(feas.sum()))
)
