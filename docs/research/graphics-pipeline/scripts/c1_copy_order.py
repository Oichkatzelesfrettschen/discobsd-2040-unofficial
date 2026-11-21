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

# c1_copy_order.py -- run: sage -python c1_copy_order.py
import random
from itertools import permutations
from math import ceil, factorial, gcd

from sage.all import ZZ, DiGraph, identity_matrix, matrix


def simul(x, src, dst):
    y = list(x)
    for s, d in zip(src, dst):
        y[d] = x[s]
    return y


def seq(x, src, dst, order):
    y = list(x)
    for t in order:
        y[dst[t]] = y[src[t]]
    return y


def count_valid(N, src, dst):
    x = list(range(N))
    want = simul(x, src, dst)
    return sum(1 for o in permutations(range(len(src))) if seq(x, src, dst, o) == want)


def graph_of(src, dst):
    G = DiGraph()
    G.add_vertices(range(len(src)))
    for t in range(len(src)):
        for u in range(len(src)):
            if t != u and src[t] == dst[u]:
                G.add_edge(t, u)  # t reads a cell that u writes: t before u
    return G


def nil_index(C):
    P = C
    k = 1
    while P != 0:
        P = P * C
        k += 1
        if k > 64:
            return None
    return k


print(
    "== A. shift copy x[p+d] <- x[p], p in [0,L): dependency graph, valid orders, nilpotency index"
)
print("L d | paths longest acyclic | valid_orders multinomial | nil_index predicted ceil(L/d)+1")
for L, d in [(4, 1), (5, 2), (6, 2), (6, 3), (7, 3), (7, 1), (6, 6), (4, 5), (7, 2)]:
    src = list(range(L))
    dst = [p + d for p in range(L)]
    N = L + d
    G = graph_of(src, dst)
    comps = (
        G.connected_components_subgraphs()
        if hasattr(G, "connected_components_subgraphs")
        else [G.subgraph(c) for c in G.connected_components()]
    )
    longest = max(len(c.vertices()) for c in comps)
    acyc = G.is_directed_acyclic()
    pathlike = all(G.in_degree(v) <= 1 and G.out_degree(v) <= 1 for v in G.vertices())
    vo = count_valid(N, src, dst)
    mult = factorial(L)
    for c in comps:
        mult //= factorial(len(c.vertices()))
    C = matrix(ZZ, N, N)
    for s_, d_ in zip(src, dst):
        C[d_, s_] = 1
    print(
        L,
        d,
        "|",
        len(comps),
        longest,
        acyc and pathlike,
        "|",
        vo,
        mult,
        "|",
        nil_index(C),
        ceil(L / d) + 1,
    )

print("== B. rotation of an n-ring by d (all cells move): cycles, valid orders, one temp cell")
print("n d | cycles(gcd) cycle_len | acyclic valid_orders | one_temp_ok")
for n, d in [(4, 1), (6, 2), (6, 3), (7, 3), (8, 2), (5, 5)]:
    if d % n == 0:
        continue
    src = list(range(n))
    dst = [(p + d) % n for p in range(n)]
    G = graph_of(src, dst)
    sccs = [c for c in G.strongly_connected_components() if len(c) > 1]
    vo = count_valid(n, src, dst)
    # one temp cell, one cycle at a time
    x = list(range(100, 100 + n))
    want = simul(x, src, dst)
    y = list(x)
    seen = set()
    for lead in range(n):
        if lead in seen:
            continue
        tmp = y[lead]
        p = lead
        while True:
            seen.add(p)
            q = (p - d) % n  # the cell whose value moves into p
            if q == lead:
                y[p] = tmp
                break
            y[p] = y[q]
            p = q
    print(
        n,
        d,
        "|",
        len(sccs),
        (len(sccs[0]) if sccs else 0),
        "|",
        G.is_directed_acyclic(),
        vo,
        "|",
        y == want,
        "gcd=",
        gcd(n, d),
    )

print(
    "== C. overlapping exchange of ranges [0,L) and [d,d+L): distinct results over all swap orders"
)
print("L d | distinct_results (1 = well defined)")
for L, d in [(3, 1), (4, 2), (4, 4), (3, 5), (5, 2)]:
    N = L + d
    res = set()
    for order in permutations(range(L)):
        y = list(range(N))
        for i in order:
            y[i], y[i + d] = y[i + d], y[i]
        res.add(tuple(y))
    print(L, d, "|", len(res), "(overlap)" if d < L else "(disjoint)")

print("== D. one-byte copy T(d<-s) = I + e_d(e_s - e_d)^T is idempotent; commutation criterion")
N = 5


def T(s, d):
    M = identity_matrix(ZZ, N)
    M[d, s] += 1
    M[d, d] -= 1
    return M


bad_idem = 0
bad_comm = 0
npairs = 0
cells = [(s, d) for s in range(N) for d in range(N) if s != d]
for s, d in cells:
    t = T(s, d)
    if t * t != t or (identity_matrix(ZZ, N) - t) * t != 0:
        bad_idem += 1
for s1, d1 in cells:
    for s2, d2 in cells:
        t1, t2 = T(s1, d1), T(s2, d2)
        commute = t1 * t2 == t2 * t1
        pred = (s1 != d2) and (s2 != d1) and (d1 != d2 or s1 == s2)
        npairs += 1
        if commute != pred:
            bad_comm += 1
print(
    "copy matrices",
    len(cells),
    "idempotent failures:",
    bad_idem,
    "| pairs",
    npairs,
    "commutation-criterion mismatches:",
    bad_comm,
)

print(
    "== E. chunked move through a buffer of K bytes; two conv windows A (base 0) and B "
    "(base 64) show the same storage"
)
M = 16


def st(a):
    if 0 <= a < M:
        return a
    if 64 <= a < 64 + M:
        return a - 64
    return None


def run_move(cs, cd, L, K, rule):
    x = list(range(M))
    want = list(x)
    for i in range(L):
        want[st(cd + i)] = x[st(cs + i)]
    conv_overlap = cs < cd + L and cd < cs + L
    if rule == "code":
        back = (
            conv_overlap and cd > cs
        )  # ems.c move(): back set only inside the same-type overlap test
    else:
        back = st(cd) > st(cs)  # storage order
    y = list(x)
    i = 0
    while i < L:
        n = min(K, L - i)
        k = (L - i - n) if back else i
        buf = [y[st(cs + k + j)] for j in range(n)]
        for j in range(n):
            y[st(cd + k + j)] = buf[j]
        i += n
    return y == want


stats = {}
first = {}
for ws in (0, 64):
    for wd in (0, 64):
        for o1 in range(M):
            for o2 in range(M):
                for L in range(1, M - max(o1, o2) + 1):
                    for K in (1, 2, 4, 8):
                        for rule in ("code", "storage"):
                            ok = run_move(ws + o1, wd + o2, L, K, rule)
                            key = (
                                rule,
                                "same-window" if ws == wd else "cross-window",
                                "L<=K" if L <= K else "L>K",
                            )
                            a = stats.setdefault(key, [0, 0])
                            a[1] += 1
                            if not ok:
                                a[0] += 1
                                first.setdefault(key, (ws, wd, o1, o2, L, K))
for k in sorted(stats):
    print(k, "mismatches", stats[k][0], "of", stats[k][1], "first:", first.get(k))

print(
    "== F. ems.c alias() (usr.bin/i86/ems.c:404-424) as a Python port vs byte-set "
    "intersection, random configurations"
)
random.seed(20261121)


def trial():
    PAGE = random.choice([4, 8])
    nphys = random.randint(1, 4)
    npool = random.randint(1, 6)
    nh = random.randint(1, 3)
    # conv blocks of the physical pages: distinct, page aligned, within 0..16 pages
    blks = random.sample(range(16), nphys)
    blk = blks
    owner = [random.randint(0, nh) for _ in range(npool)]  # 0 = free, h+1 otherwise
    lnum = [0] * npool
    cnt = {}
    for k in range(npool):
        if owner[k]:
            lnum[k] = cnt.get(owner[k], 0)
            cnt[owner[k]] = lnum[k] + 1
    cur = [random.choice([None] + list(range(npool))) for _ in range(nphys)]
    # a move needs: a handle h that owns >= 1 page, region inside its pages
    hs = [h for h in range(1, nh + 1) if cnt.get(h, 0) > 0]
    if not hs:
        return None
    h = random.choice(hs)
    pages = cnt[h]
    x_page = random.randint(0, pages - 1)
    x_at = random.randint(0, PAGE - 1)
    maxlen = pages * PAGE - (x_page * PAGE + x_at)
    L = random.randint(1, min(maxlen, 3 * PAGE))
    c_at = random.randint(0, 16 * PAGE - 1)
    if c_at + L > 16 * PAGE:
        L = 16 * PAGE - c_at
    xs = x_page * PAGE + x_at
    # port of alias()
    code = 0
    for p in range(nphys):
        b = blk[p] * PAGE
        if cur[p] is None or c_at >= b + PAGE or b >= c_at + L:
            continue
        k = cur[p]
        if owner[k] != h:
            continue  # owner[k] != x->h + 1, with h stored +1 here
        m = lnum[k] * PAGE
        lo = c_at - b if c_at > b else 0
        hi = c_at + L - b if c_at + L < b + PAGE else PAGE
        if m + lo < xs + L and xs < m + hi:
            code = 1
    # truth: storage identity of every conventional byte
    cset = set()
    for a in range(c_at, c_at + L):
        for p in range(nphys):
            b = blk[p] * PAGE
            if cur[p] is not None and b <= a < b + PAGE:
                k = cur[p]
                if owner[k]:
                    cset.add((owner[k], lnum[k] * PAGE + (a - b)))
    xset = set((h, xs + i) for i in range(L))
    truth = 1 if cset & xset else 0
    return code, truth


agree = dis = skipped = 0
pos = 0
for _ in range(400000):
    r = trial()
    if r is None:
        skipped += 1
        continue
    if r[0] == r[1]:
        agree += 1
    else:
        dis += 1
    pos += r[1]
print(
    "trials",
    agree + dis,
    "agree",
    agree,
    "disagree",
    dis,
    "overlapping cases",
    pos,
    "skipped",
    skipped,
)
