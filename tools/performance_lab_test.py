"""Independent analytic and brute-force controls for performance experiments."""

import itertools
import re
import unittest

import numpy as np
import performance_lab as lab
from graph_layout import cut, experiment


class LabTests(unittest.TestCase):
    def test_extended_graph_integration(self):
        w = np.array([[0, 10, 0, 0], [10, 0, 1, 0], [0, 1, 0, 10], [0, 0, 10, 0]])
        report = experiment(["a", "b", "c", "d"], [8] * 4, w, 4, 7, extended=True)
        self.assertEqual(len(report["extensions"]["tool_sha256"]), 64)
        self.assertIn("imaginary_time_bond_8", report["results"])
        self.assertIn("tempering", report["results"])
        for result in report["results"].values():
            self.assertGreaterEqual(
                result["cut_transitions"], report["results"]["exact"]["cut_transitions"]
            )

    def test_qasm_evolution_matches_cost_and_mixer(self):
        weights = np.array([[0, 3], [3, 0]])
        gamma, beta, scale = 0.7, 0.3, 15
        qasm = lab.qaoa_qasm(
            {"weights": weights.tolist(), "gamma": gamma, "beta": beta, "scale": scale}
        )
        state = np.array([1, 0, 0, 0], dtype=complex)
        for line in qasm.splitlines():
            if line.startswith("cx "):
                a, b = map(int, re.findall(r"q\[(\d+)\]", line))
                state = state[[s ^ (1 << b) if s >> a & 1 else s for s in range(4)]]
            elif line.startswith(("h ", "rz(", "rx(")):
                bit = int(re.search(r"q\[(\d+)\]", line)[1])
                if line.startswith("h "):
                    matrix = np.array([[1, 1], [1, -1]]) / np.sqrt(2)
                else:
                    angle = float(line.split("(")[1].split(")")[0])
                    if line.startswith("rz"):
                        matrix = np.diag([np.exp(-0.5j * angle), np.exp(0.5j * angle)])
                    else:
                        matrix = np.array(
                            [
                                [np.cos(angle / 2), -1j * np.sin(angle / 2)],
                                [-1j * np.sin(angle / 2), np.cos(angle / 2)],
                            ]
                        )
                new = state.copy()
                for s in range(4):
                    zero, one = s & ~(1 << bit), s | (1 << bit)
                    new[s] = matrix[(s >> bit) & 1] @ state[[zero, one]]
                state = new
        # Two-node cut: balanced states cost three; unbalanced states cost four.
        phases = np.exp(-1j * gamma * np.array([4, 3, 3, 4]) / scale) / 2
        mixer = np.array(
            [
                [
                    np.cos(beta) ** (2 - (a ^ b).bit_count())
                    * (-1j * np.sin(beta)) ** (a ^ b).bit_count()
                    for b in range(4)
                ]
                for a in range(4)
            ]
        )
        np.testing.assert_allclose(np.abs(state) ** 2, np.abs(mixer @ phases) ** 2, atol=1e-12)

    def test_profile_shared_costs_and_dependencies(self):
        model = {
            "blocks": 8,
            "inodes": 1,
            "objects": {
                "box": {"blocks": 4, "inodes": 1},
                "a": {"blocks": 2, "inodes": 0},
                "b": {"blocks": 2, "inodes": 0},
            },
            "features": [
                {"name": "a", "benefit": 2, "needs": [], "objects": ["box", "a"]},
                {"name": "b", "benefit": 3, "needs": ["a"], "objects": ["box", "b"]},
            ],
        }
        self.assertEqual(lab.profile_select(model)["selected"], ["a", "b"])
        model["blocks"] = 6
        self.assertEqual(lab.profile_select(model)["selected"], ["a"])

    def test_tail_parameter_recovery_and_invalid_threshold(self):
        samples = (1 + np.random.default_rng(7).pareto(0.6, 1000)).tolist()
        model = {"threshold": 1, "samples": samples}
        result = lab.tail_audit(model)
        low, high = result["pareto_alpha_95_percentile_interval"]
        self.assertLess(low, 0.6)
        self.assertGreater(high, 0.6)
        self.assertEqual(set(result["fits"]), {"pareto", "lognormal", "weibull"})
        model["threshold"] = 2
        with self.assertRaises(ValueError):
            lab.tail_audit(model)

    def test_bellman_delayed_reward_and_direct_check(self):
        model = {
            "discount": 0.5,
            "states": [
                [
                    {"name": "flash", "cost": 3, "probabilities": [1, 0]},
                    {"name": "ram", "cost": 1, "probabilities": [0, 1]},
                ],
                [{"name": "release", "cost": 0, "probabilities": [1, 0]}],
            ],
        }
        r = lab.solve_mdp(model)
        self.assertEqual(r["policy"], ["ram", "release"])
        np.testing.assert_allclose(r["values"], [4 / 3, 2 / 3], atol=1e-8)
        self.assertLess(r["direct_policy_check_max_error"], 1e-8)
        model["states"][0][1]["cost"] = 10
        self.assertEqual(lab.solve_mdp(model)["policy"][0], "flash")
        model["states"][0][1]["probabilities"] = [0, 0.9]
        with self.assertRaises(ValueError):
            lab.solve_mdp(model)

    def test_transport_against_every_assignment(self):
        model = {
            "erase_counts": [10, 0, 2],
            "relocation_cost": [[0, 3, 7], [2, 0, 1], [0, 0, 0]],
            "wear_weight": 1,
            "writes": 2,
        }
        result = lab.wear_transport(model)
        brute = min(
            sum(
                model["relocation_cost"][i][j] + 2 * model["erase_counts"][j] + 1
                for i, j in enumerate(assignment)
            )
            for assignment in itertools.permutations(range(3), 2)
        )
        self.assertEqual(result["objective"], brute)
        self.assertEqual(len(set(result["destinations"])), 2)

    def test_offload_contention_and_budget(self):
        model = {
            "sram_budget": 10,
            "tasks": [
                {"name": "decode", "bytes": 6, "saved_cycles": 10},
                {"name": "hash", "bytes": 4, "saved_cycles": 8},
            ],
            "pair_cycles": [[0, 20], [20, 0]],
        }
        self.assertEqual(lab.offload(model)["selected"], ["decode"])
        model["pair_cycles"] = [[0, 0], [0, 0]]
        self.assertEqual(lab.offload(model)["net_saved_cycles"], 18)
        model["sram_budget"] = 0
        self.assertEqual(lab.offload(model)["selected"], [])

    def test_pde_absorption_matches_poisson_process(self):
        # Two cells, drift one, no diffusion: two exponential jumps to absorption.
        model = {
            "cells": 2,
            "dx": 1.0,
            "drift": 1.0,
            "diffusion": 0.0,
            "time": 2.0,
            "initial": [1, 0, 0],
        }
        r = lab.drift_diffusion(model)
        self.assertAlmostEqual(r["absorbed_probability"], 1 - 3 * np.exp(-2))
        self.assertLess(r["conservation_error"], 1e-12)
        model["time"] = 0
        self.assertEqual(lab.drift_diffusion(model)["mass"], [1, 0, 0])
        model["diffusion"] = -1
        with self.assertRaises(ValueError):
            lab.drift_diffusion(model)

    def test_mps_reconstruction_and_entanglement_loss(self):
        bell = np.array([1, 0, 0, 1]) / np.sqrt(2)
        rebuilt, loss = lab.compress_mps(bell, 2, 2)
        np.testing.assert_allclose(rebuilt, bell)
        self.assertEqual(loss, 0)
        rebuilt, loss = lab.compress_mps(bell, 2, 1)
        self.assertAlmostEqual(loss, 0.5)
        self.assertAlmostEqual(np.linalg.norm(rebuilt), 1)
        vector = np.random.default_rng(3).normal(size=32)
        vector /= np.linalg.norm(vector)
        rebuilt, loss = lab.compress_mps(vector, 5, 8)
        np.testing.assert_allclose(rebuilt, vector, atol=1e-12)
        self.assertEqual(loss, 0)

    def test_graph_solvers_and_walk_conservation(self):
        weights = np.array([[0, 10, 0, 0], [10, 0, 1, 0], [0, 1, 0, 10], [0, 0, 10, 0]])
        for solver in (lab.temper, lab.oscillator, lab.imaginary_time):
            selected, meta = solver(weights, 2, 7)
            self.assertEqual(len(selected), 2)
            self.assertEqual(cut(weights, selected), 1)
            self.assertTrue(meta)
        r = lab.walks(weights)
        self.assertAlmostEqual(sum(r["heat"]), 1)
        self.assertAlmostEqual(sum(r["quantum_probability"]), 1)
        self.assertGreaterEqual(min(r["heat"]), -1e-12)
        zero = lab.walks(weights, time=0)
        np.testing.assert_allclose(zero["quantum_probability"], [1, 0, 0, 0], atol=1e-12)


if __name__ == "__main__":
    unittest.main()
