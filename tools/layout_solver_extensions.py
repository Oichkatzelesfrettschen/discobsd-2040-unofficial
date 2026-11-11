"""MILP and number-conserving noisy QAOA baselines for layout graphs."""

import itertools
import math

import numpy as np
from graph_layout import cut, require
from scipy.linalg import expm
from scipy.optimize import Bounds, LinearConstraint, milp
from scipy.sparse import lil_matrix


def integer_baseline(weights, k):
    n = len(weights)
    edges = [(i, j) for i in range(n) for j in range(i + 1, n) if weights[i, j]]
    objective = np.zeros(n + len(edges))
    objective[:n] = weights.sum(axis=1)
    matrix = lil_matrix((1 + 3 * len(edges), len(objective)))
    lower = np.full(matrix.shape[0], -np.inf)
    upper = np.zeros(matrix.shape[0])
    matrix[0, :n] = 1
    lower[0] = upper[0] = k
    for index, (i, j) in enumerate(edges):
        y = n + index
        objective[y] = -2 * weights[i, j]
        for row, vertex in ((1 + 3 * index, i), (2 + 3 * index, j)):
            matrix[row, y], matrix[row, vertex] = 1, -1
        row = 3 + 3 * index
        matrix[row, i], matrix[row, j], matrix[row, y] = 1, 1, -1
        upper[row] = 1
    result = milp(
        objective,
        integrality=[1] * n + [0] * len(edges),
        bounds=Bounds(0, 1),
        constraints=LinearConstraint(matrix.tocsr(), lower, upper),
        options={"time_limit": 30, "mip_rel_gap": 0},
    )
    require(result.x is not None, "MILP found no feasible incumbent within its budget")
    chosen = [i for i, value in enumerate(result.x[:n]) if value > 0.5]
    require(len(chosen) == k, "MILP returned an invalid cardinality")
    return chosen, {
        "certified_optimal": bool(result.success),
        "status": int(result.status),
        "gap": float(result.mip_gap),
        "objective": cut(weights, chosen),
    }


def conserved_qaoa(weights, k, seed, depth=1, noise=0.01, shots=4096):
    """Ring-XY mixer in an exact fixed-weight subspace; local Z noise per layer."""
    n = len(weights)
    require(
        2 <= n <= 8 and 1 <= depth <= 3 and 0 <= noise <= 0.5,
        "density-matrix rehearsal supports 2..8 qubits, depth 1..3",
    )
    basis = [sum(1 << i for i in group) for group in itertools.combinations(range(n), k)]
    lookup = {bits: i for i, bits in enumerate(basis)}
    energies = np.array(
        [cut(weights, [i for i in range(n) if bits >> i & 1]) for bits in basis], dtype=float
    )
    scale = max(1.0, float(energies.max()))
    mixer = np.zeros((len(basis), len(basis)))
    ring = {tuple(sorted((i, (i + 1) % n))) for i in range(n)}
    for row, bits in enumerate(basis):
        for i, j in ring:
            if ((bits >> i) ^ (bits >> j)) & 1:
                mixer[row, lookup[bits ^ (1 << i) ^ (1 << j)]] = 1
    damp = np.array([[(1 - 2 * noise) ** (a ^ b).bit_count() for b in basis] for a in basis])
    best = None
    # Parameter search is noiseless; noise is evaluated only after angles are frozen.
    for gamma in (0.5, 1.5, 3.0):
        for beta in (0.2, 0.6, 1.0):
            unitary = expm(-1j * beta * mixer)
            phase = np.exp(-1j * gamma * energies / scale)
            state = np.ones(len(basis), dtype=complex) / math.sqrt(len(basis))
            for _ in range(depth):
                state = unitary @ (phase * state)
            mean = float(np.abs(state) ** 2 @ energies)
            if best is None or mean < best[0]:
                best = (mean, gamma, beta, unitary, phase)
    rho = np.ones((len(basis), len(basis)), dtype=complex) / len(basis)
    for _ in range(depth):
        rho *= best[4][:, None] * best[4].conj()[None, :]
        rho = best[3] @ rho @ best[3].conj().T
        rho *= damp
    probabilities = rho.diagonal().real.copy()
    require(
        abs(float(probabilities.sum()) - 1) < 1e-10 and probabilities.min() > -1e-12,
        "noisy evolution violated probability conservation",
    )
    probabilities = np.maximum(probabilities, 0)
    probabilities /= probabilities.sum()
    draws = np.random.default_rng(seed).choice(len(basis), shots, p=probabilities)
    winner = min(draws, key=lambda i: (energies[i], basis[i]))
    return [i for i in range(n) if basis[winner] >> i & 1], {
        "depth": depth,
        "phase_flip_probability": noise,
        "shots": shots,
        "feasible_shots": shots,
        "gamma": best[1],
        "beta": best[2],
        "ideal_mean_cut": best[0],
        "noisy_mean_cut": float(probabilities @ energies),
        "purity": float(np.trace(rho @ rho).real),
        "angle_grid_evaluations": 9,
        "scope": "exact ring-XY subspace simulation; synthetic independent phase flips",
    }
