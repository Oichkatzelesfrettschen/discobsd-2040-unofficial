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

from sage.all import DiGraph


# c1b_permuted_windows.py -- run: sage c1b_permuted_windows.py
# Two windows onto a 2-page store (page = 4 bytes): A shows pages [0,1], B shows pages [1,0]; copy
# A[0..8) -> B[0..8) is a page swap.
def stor(conv):
    if 0 <= conv < 8:
        return conv
    if 64 <= conv < 72:
        return (conv - 64 + 4) % 8
    return None


def move(cs, cd, L, K, back):
    x = list(range(8))
    want = list(x)
    for i in range(L):
        want[stor(cd + i)] = x[stor(cs + i)]
    y = list(x)
    i = 0
    while i < L:
        n = min(K, L - i)
        k = (L - i - n) if back else i
        buf = [y[stor(cs + k + j)] for j in range(n)]
        for j in range(n):
            y[stor(cd + k + j)] = buf[j]
        i += n
    return y == want


print("chunk K | forward ok | backward ok   (copy A[0..8) -> B[0..8), a swap of the two pages)")
for K in range(1, 9):
    print(K, "|", move(0, 64, 8, K, False), "|", move(0, 64, 8, K, True))
# dependency graph on storage bytes: edge s -> d for every copied byte; cycles need one temp each
G = DiGraph([(stor(0 + i), stor(64 + i)) for i in range(8)], loops=False)
cyc = [c for c in G.strongly_connected_components() if len(c) > 1]
print(
    "storage byte dependency graph: acyclic =",
    G.is_directed_acyclic(),
    "| cycles:",
    sorted(map(sorted, cyc)),
    "| cycle lengths:",
    [len(c) for c in cyc],
)
# cycle-leader execution with one temporary byte
x = list(range(100, 108))
want = list(x)
for i in range(8):
    want[stor(64 + i)] = x[stor(i)]
y = list(x)
done = set()
f = {stor(i): stor(64 + i) for i in range(8)}  # content of s moves to f[s]
inv = {d: s for s, d in f.items()}
for lead in sorted(f):
    if lead in done:
        continue
    tmp = y[lead]
    p = lead
    while True:
        done.add(p)
        q = inv[p]
        if q == lead:
            y[p] = tmp
            break
        y[p] = y[q]
        p = q
print("one temporary byte, cycle by cycle, reproduces the buffered result:", y == want)
