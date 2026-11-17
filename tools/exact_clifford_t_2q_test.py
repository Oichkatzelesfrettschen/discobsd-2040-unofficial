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
"""Calibration for tools/exact_clifford_t_2q.py.

Known-good: 1500 seeded random Clifford+T circuits of 1 to 39 gates agree
with an independent complex-float reference to 1e-12 in every amplitude,
and each exact state sums to probability 1 with integer arithmetic. Exact
cases: H on qubit 0 then CX gives probabilities 1/2, 0, 0, 1/2; H T H on
|0> gives (2 + sqrt(2)) / 4; T**8 and H H return |00> with scale 0.

Mutants that must fail: a state that never reduces its scale and T replaced
by S each change the Matsumoto-Amano count (the unreduced state has no finite
closure, so the search stops one key past the expected count); a Hadamard
without its 1/sqrt(2) scale trips the exact norm check in every circuit that
contains one; T replaced by S (a rotation by w**2 where w is required)
changes the H T H probability
and the reference comparison on most random circuits.

Known answer from the literature (Matsumoto and Amano, arXiv 0806.3834):
circuits over H, S and T with at most n T gates realize exactly
192 * (3 * 2**n - 2) distinct one-qubit unitaries, global phase included.
The exact states of qubit 0 reproduce that count for n = 0 to 5, which also
holds the shared-scale reduction to one canonical form per unitary.

Width: 1000-gate random circuits keep every amplitude coefficient within
31 bits.
"""

import cmath
import math
import random
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import exact_clifford_t_2q as ex  # noqa: E402

W = cmath.exp(1j * math.pi / 4)
R2 = 1 / math.sqrt(2)


def ref_1q(st, m, j):
    out = list(st)
    for i in range(4):
        if not (i >> j) & 1:
            a, b = st[i], st[i | (1 << j)]
            out[i] = m[0][0] * a + m[0][1] * b
            out[i | (1 << j)] = m[1][0] * a + m[1][1] * b
    return out


MATS = {
    "h": [[R2, R2], [R2, -R2]],
    "x": [[0, 1], [1, 0]],
    "t": [[1, 0], [0, W]],
    "tdg": [[1, 0], [0, W.conjugate()]],
    "s": [[1, 0], [0, 1j]],
    "z": [[1, 0], [0, -1]],
}


def ref_apply(st, gate):
    name, q = gate
    if name == "cx":
        out = list(st)
        for i in range(4):
            if (i >> q) & 1 and not (i >> (1 - q)) & 1:
                out[i], out[i | (1 << (1 - q))] = st[i | (1 << (1 - q))], st[i]
        return out
    if name == "cz":
        return [st[0], st[1], st[2], -st[3]]
    return ref_1q(st, MATS[name], q)


def apply_exact(state, gate):
    name, q = gate
    if name == "cx":
        state.cx(q, 1 - q)
    elif name == "cz":
        state.cz()
    else:
        getattr(state, name)(q)


def random_circuit(rng, length):
    names = ["h", "h", "x", "t", "tdg", "s", "z", "cx", "cz"]
    return [(rng.choice(names), rng.randrange(2)) for _ in range(length)]


def floats(state):
    scale = math.sqrt(2) ** state.k
    return [(x[0] + x[1] * W + x[2] * W**2 + x[3] * W**3) / scale for x in state.v]


class NoReduceState(ex.ExactState):
    """Mutant: the shared scale is never reduced."""

    def _reduce(self):
        pass


class NoScaleHadamard(ex.ExactState):
    """Mutant: the butterfly without the 1/sqrt(2) scale."""

    def h(self, j):
        self._butterfly(j)


class TAsSState(ex.ExactState):
    """Mutant: T applies w**2 in place of w."""

    def t(self, j):
        self._phase(j, 2)


def run_exact(cls, circuit):
    state = cls()
    for gate in circuit:
        apply_exact(state, gate)
    return state


def run_ref(circuit):
    st = [1 + 0j, 0j, 0j, 0j]
    for gate in circuit:
        st = ref_apply(st, gate)
    return st


STATE = ex.ExactState


def _count(n):
    """Matsumoto-Amano count of one-qubit unitaries with at most n T gates."""
    return 192 * (3 * 2**n - 2)


def _fresh_pair():
    a = STATE()
    b = STATE()
    b.x(0)
    return (a, b)


def _key(pair):
    return tuple((tuple(s.v[:2]), s.k) for s in pair)


def _step(pair, name):
    out = []
    for s in pair:
        c = STATE()
        c.v = list(s.v)
        c.k = s.k
        getattr(c, name)(0)
        out.append(c)
    return tuple(out)


class ClosureOverflow(Exception):
    """The closure passed the size the caller allows."""


def _close_under_h_s(seen, stack, limit):
    """Close seen under H and S; raise once it holds more than limit keys.

    A correct state reduces to one canonical form per unitary, so the closure
    is finite. A state that never reduces its scale yields a new key on every
    H, so the limit is what ends the search for that mutant.
    """
    while stack:
        if len(seen) > limit:
            raise ClosureOverflow(len(seen))
        u = stack.pop()
        k = _key(u)
        if k in seen:
            continue
        seen[k] = u
        stack.append(_step(u, "h"))
        stack.append(_step(u, "s"))


class ExactCliffordT(unittest.TestCase):
    def test_matches_reference_and_norm_is_exact(self):
        rng = random.Random(0x44495343)
        for _ in range(1500):
            circuit = random_circuit(rng, rng.randrange(1, 40))
            state = run_exact(ex.ExactState, circuit)
            err = max(abs(a - b) for a, b in zip(floats(state), run_ref(circuit)))
            self.assertLess(err, 1e-12, circuit)
            self.assertTrue(state.norm_is_one(), circuit)

    def test_exact_cases(self):
        bell = ex.ExactState()
        bell.h(0)
        bell.cx(0, 1)
        p, k = bell.probabilities()
        expect = [(1, 0, 1), (0, 0, 1), (0, 0, 1), (1, 0, 1)]
        for (u, v), e in zip(p, expect):
            self.assertTrue(ex.prob_equal((u, v, k), e))
        hth = ex.ExactState()
        hth.h(0)
        hth.t(0)
        hth.h(0)
        p, k = hth.probabilities()
        self.assertTrue(ex.prob_equal((*p[0], k), (2, 1, 2)))
        self.assertTrue(ex.prob_equal((*p[1], k), (2, -1, 2)))
        t8 = ex.ExactState()
        for _ in range(8):
            t8.t(1)
        self.assertEqual((t8.v[0], t8.k), ((1, 0, 0, 0), 0))
        hh = ex.ExactState()
        hh.h(0)
        hh.h(0)
        self.assertEqual((hh.v, hh.k), (ex.ExactState().v, 0))

    def test_no_scale_hadamard_trips_norm(self):
        rng = random.Random(0x5155425)
        checked = 0
        for _ in range(300):
            circuit = random_circuit(rng, rng.randrange(2, 30))
            if not any(g[0] == "h" for g in circuit):
                continue
            checked += 1
            self.assertFalse(run_exact(NoScaleHadamard, circuit).norm_is_one(), circuit)
        self.assertGreater(checked, 200)

    def test_t_as_s_is_rejected(self):
        good = run_exact(ex.ExactState, [("h", 0), ("t", 0), ("h", 0)])
        bad = run_exact(TAsSState, [("h", 0), ("t", 0), ("h", 0)])
        pg, kg = good.probabilities()
        pb, kb = bad.probabilities()
        self.assertFalse(ex.prob_equal((*pg[0], kg), (*pb[0], kb)))
        rng = random.Random(0x53505253)
        rejected = 0
        for _ in range(200):
            circuit = random_circuit(rng, 20)
            err = max(
                abs(a - b) for a, b in zip(floats(run_exact(TAsSState, circuit)), run_ref(circuit))
            )
            rejected += err > 1e-9
        self.assertGreater(rejected, 100)

    def test_matsumoto_amano_unitary_counts(self):
        level = {}
        _close_under_h_s(level, [_fresh_pair()], _count(0))
        for n in range(6):
            self.assertEqual(len(level), _count(n), n)
            if n == 5:
                break
            nxt = dict(level)
            _close_under_h_s(nxt, [_step(u, "t") for u in level.values()], _count(n + 1))
            level = nxt

    def test_matsumoto_amano_mutants_break_the_count(self):
        global STATE
        saved = STATE
        try:
            for mutant, n in ((NoReduceState, 0), (TAsSState, 1)):
                STATE = mutant
                level = {}
                try:
                    _close_under_h_s(level, [_fresh_pair()], _count(0))
                    for m in range(n):
                        nxt = dict(level)
                        _close_under_h_s(
                            nxt, [_step(u, "t") for u in level.values()], _count(m + 1)
                        )
                        level = nxt
                    size = len(level)
                except ClosureOverflow as over:
                    size = over.args[0]
                self.assertNotEqual(size, _count(n), mutant.__name__)
        finally:
            STATE = saved

    def test_width_after_1000_gates(self):
        rng = random.Random(0x57494454)
        for _ in range(10):
            state = run_exact(ex.ExactState, random_circuit(rng, 1000))
            self.assertLessEqual(state.max_coeff_bits(), 31)


if __name__ == "__main__":
    unittest.main()
