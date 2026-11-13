"""Bounded tensor-network imaginary time without a dense 2**n state."""

import argparse
import hashlib
import json
import math
import time
from pathlib import Path

import numpy as np
import quimb.tensor as qtn
from graph_layout import cut, qubo, require


def solve(weights, seed=1729, bond=8, steps=12, shots=128, seconds=60):
    n = len(weights)
    require(
        2 <= n <= 64
        and 1 <= bond <= 32
        and 1 <= steps <= 100
        and 1 <= shots <= 4096
        and 0 < seconds <= 600,
        "MPS budget exceeded",
    )
    require(
        weights.shape == (n, n)
        and np.issubdtype(weights.dtype, np.integer)
        and np.all(np.isfinite(weights))
        and np.all(weights >= 0)
        and np.max(weights) <= 10**12
        and np.all(weights == weights.T)
        and np.all(np.diag(weights) == 0),
        "invalid graph",
    )
    state = qtn.MPS_product_state([np.ones(2) / math.sqrt(2)] * n)
    model = qubo(weights, n // 2)
    scale = max(abs(coefficient) for _, _, coefficient in model["upper_triangular_terms"])
    start = time.monotonic()
    maximum_bond = 1
    for step in range(steps):
        s = (step + 1) / steps
        for i, j, coefficient in model["upper_triangular_terms"]:
            require(time.monotonic() - start <= seconds, "MPS deadline exceeded")
            values = (
                np.array([0.0, coefficient / scale])
                if i == j
                else np.array([0.0, 0.0, 0.0, coefficient / scale])
            )
            gate = np.diag(np.exp(-0.2 * s * (values - values.min())))
            if i == j:
                state.gate_(gate, i, contract=True)
            else:
                state.gate_(gate, (i, j), contract="swap+split", max_bond=bond, cutoff=1e-10)
        theta = 0.2 * (1 - s)
        mixer = np.array([[np.cosh(theta), np.sinh(theta)], [np.sinh(theta), np.cosh(theta)]])
        for i in range(n):
            state.gate_(mixer, i, contract=True)
        state.normalize()
        maximum_bond = max(maximum_bond, state.max_bond())
    winner = None
    feasible = 0
    for bits, _probability in state.sample(shots, seed=seed):
        require(time.monotonic() - start <= seconds, "MPS sampling deadline exceeded")
        selected = tuple(i for i, bit in enumerate(bits) if bit)
        if len(selected) == n // 2:
            feasible += 1
            value = (cut(weights, selected), selected)
            winner = value if winner is None else min(winner, value)
    return {
        "selected": list(winner[1]) if winner else None,
        "cut": winner[0] if winner else None,
        "shots": shots,
        "feasible_shots": feasible,
        "maximum_bond": maximum_bond,
        "tensor_elements": sum(t.data.size for t in state),
        "norm": float(state.norm()),
        "seed": seed,
        "steps": steps,
        "scope": "truncated nonunitary MPS search; no dense state or optimum certificate",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graph", type=Path)
    parser.add_argument("--seed", type=int, default=1729)
    parser.add_argument("--bond", type=int, default=8)
    parser.add_argument("--steps", type=int, default=12)
    parser.add_argument("--shots", type=int, default=128)
    args = parser.parse_args()
    raw = args.graph.read_bytes()
    graph = json.loads(raw)
    result = solve(
        np.array(graph.get("cluster_weights", graph.get("weights"))),
        args.seed,
        args.bond,
        args.steps,
        args.shots,
    )
    result["input_sha256"] = hashlib.sha256(raw).hexdigest()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
