"""Independent enumeration and malformed-input controls for graph_layout."""

import csv
import itertools
import json
import tempfile
import unittest
from pathlib import Path

import graph_layout as gl
import numpy as np


class LayoutTests(unittest.TestCase):
    def graph(self):
        # Two strong pairs, a weak bridge: optimum balanced cut is exactly 1.
        return np.array(
            [[0, 10, 0, 0], [10, 0, 1, 0], [0, 1, 0, 10], [0, 0, 10, 0]], dtype=np.int64
        )

    def test_qubo_all_states_and_penalty(self):
        w = self.graph()
        q = gl.qubo(w, 2)
        h = gl.ising(q)
        energies = []
        for bits in itertools.product((0, 1), repeat=4):
            value = q["offset"] + sum(
                c * bits[i] * bits[j] for i, j, c in q["upper_triangular_terms"]
            )
            independent_cut = sum(
                int(w[i, j]) * (bits[i] != bits[j]) for i in range(4) for j in range(i + 1, 4)
            )
            self.assertEqual(value, independent_cut + q["penalty"] * (sum(bits) - 2) ** 2)
            spins = [1 - 2 * bit for bit in bits]
            numerator = (
                h["offset_numerator"]
                + sum(c * z for c, z in zip(h["z_numerators"], spins))
                + sum(c * spins[i] * spins[j] for i, j, c in h["zz_numerators"])
            )
            self.assertEqual(numerator, h["denominator"] * value)
            energies.append((value, sum(bits)))
        self.assertEqual(min(energies), (1, 2))
        self.assertGreater(
            min(v for v, k in energies if k != 2), max(v for v, k in energies if k == 2)
        )

    def test_known_optimum_and_edge_mutation(self):
        w = self.graph()
        self.assertEqual(gl.cut(w, gl.exact(w, 2)), 1)
        self.assertEqual(gl.cut(w, gl.mbo_split(w, 7)), 1)
        self.assertEqual(gl.cut(w, gl.anneal(w, 2, 7)), 1)
        w[1, 2] = w[2, 1] = 30
        self.assertEqual(gl.cut(w, gl.exact(w, 2)), 20)

    def test_lift_and_reproducibility(self):
        args = (["a", "b", "c", "d"], [10, 20, 30, 40], self.graph(), 2, 7)
        a = gl.experiment(*args, quantum=True)
        self.assertEqual(a, gl.experiment(*args, quantum=True))
        self.assertEqual(sum(c["bytes"] for c in a["clusters"]), 100)
        self.assertEqual(a["results"]["exact"]["cut_transitions"], 1)
        self.assertEqual(a["qaoa"]["shots"], 4096)
        self.assertGreater(a["qaoa"]["feasible_shots"], 0)

    def test_qaoa_against_dense_mixer_oracle(self):
        w = self.graph()
        _, metadata = gl.qaoa_p1(w, 2, 19)
        energies = np.array(
            [
                sum(
                    int(w[i, j]) * (((s >> i) & 1) != ((s >> j) & 1))
                    for i in range(4)
                    for j in range(i + 1, 4)
                )
                + 22 * (s.bit_count() - 2) ** 2
                for s in range(16)
            ]
        )
        # Independent dense tensor-product mixer, indexed by Hamming distance.
        beta = metadata["beta"]
        mixer = np.array(
            [
                [
                    np.cos(beta) ** (4 - (a ^ b).bit_count())
                    * (-1j * np.sin(beta)) ** (a ^ b).bit_count()
                    for b in range(16)
                ]
                for a in range(16)
            ]
        )
        initial = np.exp(-1j * metadata["gamma"] * energies / metadata["energy_scale"]) / 4
        state = mixer @ initial
        self.assertAlmostEqual(float(np.abs(state).dot(np.abs(state))), 1)
        self.assertAlmostEqual(
            float(np.abs(state) ** 2 @ energies), metadata["mean_penalized_energy"]
        )

    def test_coarsening_preserves_every_lifted_cut(self):
        w = np.random.default_rng(42).integers(0, 50, (9, 9))
        w = np.triu(w, 1)
        w = w + w.T
        groups, coarse = gl.coarsen(w, 5, 7)
        self.assertEqual(sorted(v for g in groups for v in g), list(range(9)))
        for bits in itertools.product((0, 1), repeat=5):
            selected = [i for i, bit in enumerate(bits) if bit]
            lifted = [v for i in selected for v in groups[i]]
            self.assertEqual(gl.cut(w, lifted), gl.cut(coarse, selected))

    def test_trace_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            symbols = Path(folder) / "symbols.json"
            trace = Path(folder) / "trace.csv"
            symbols.write_text(json.dumps([{"name": "a", "bytes": 10}, {"name": "b", "bytes": 20}]))

            def rows(data):
                with trace.open("w", newline="") as handle:
                    writer = csv.writer(handle)
                    writer.writerow(["source", "target", "count"])
                    writer.writerows(data)

            rows([("a", "b", 2), ("b", "a", 3)])
            _, _, w, meta = gl.load_graph(symbols, trace)
            self.assertEqual(w.tolist(), [[0, 5], [5, 0]])
            self.assertEqual(meta["observed_transitions"], 5)
            for bad in [
                [("a", "missing", 1)],
                [("a", "b", -1)],
                [("a", "b", "nan")],
                [("a", "a", 1)],
            ]:
                rows(bad)
                with self.assertRaises(ValueError):
                    gl.load_graph(symbols, trace)


if __name__ == "__main__":
    unittest.main()
