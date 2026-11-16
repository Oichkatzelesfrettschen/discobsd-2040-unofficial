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
"""Exact independent checks for MILP, conserved noisy circuits and actual MPS."""

import unittest

import numpy as np
from graph_layout import cut, exact
from layout_solver_extensions import conserved_qaoa, integer_baseline


class SolverTests(unittest.TestCase):
    def test_milp_matches_enumeration(self):
        for seed in range(3):
            w = np.random.default_rng(seed).integers(0, 10, (6, 6))
            w = np.triu(w, 1)
            w += w.T
            selected, metadata = integer_baseline(w, 3)
            self.assertTrue(metadata["certified_optimal"])
            self.assertEqual(cut(w, selected), cut(w, exact(w, 3)))

    def test_number_conservation_depths_and_noise_purity(self):
        w = np.array([[0, 10, 0, 0], [10, 0, 1, 0], [0, 1, 0, 10], [0, 0, 10, 0]])
        for depth in (1, 2, 3):
            selected, clean = conserved_qaoa(w, 2, 7, depth, noise=0)
            self.assertEqual(len(selected), 2)
            self.assertEqual(clean["feasible_shots"], clean["shots"])
            self.assertAlmostEqual(clean["purity"], 1)
            _, noisy = conserved_qaoa(w, 2, 7, depth, noise=0.2)
            self.assertLess(noisy["purity"], 1)

    def test_actual_tensor_network(self):
        from layout_mps import solve

        w = np.array([[0, 3], [3, 0]])
        result = solve(w, steps=3, shots=128)
        self.assertGreater(result["feasible_shots"], 0)
        self.assertEqual(result["cut"], 3)
        self.assertAlmostEqual(result["norm"], 1)
        self.assertLessEqual(result["maximum_bond"], 8)


if __name__ == "__main__":
    unittest.main()
