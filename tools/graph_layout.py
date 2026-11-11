"""Host-only graph diffusion and binary locality experiments (no board I/O)."""

import argparse
import csv
import hashlib
import itertools
import json
import math
import random
import sys
from pathlib import Path

import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_graph(symbol_path, trace_path):
    """CSV rows are observed transitions, not inferred static call edges."""
    raw = symbol_path.read_bytes()
    trace = trace_path.read_bytes()
    symbols = json.loads(raw)
    require(
        isinstance(symbols, list) and 2 <= len(symbols) <= 512, "expected 2..512 symbol records"
    )
    names, sizes = [], []
    for row in symbols:
        require(
            isinstance(row, dict) and set(row) == {"name", "bytes"},
            "symbol fields must be name and bytes",
        )
        name, size = row["name"], row["bytes"]
        require(
            isinstance(name, str)
            and name
            and name.isascii()
            and all(c.isalnum() or c in "_.$" for c in name),
            "invalid symbol",
        )
        require(name not in names, "duplicate symbol")
        require(type(size) is int and 0 < size <= 131072, "invalid symbol byte size")
        names.append(name)
        sizes.append(size)
    index = {name: i for i, name in enumerate(names)}
    weights = np.zeros((len(names), len(names)), dtype=np.int64)
    reader = csv.DictReader(trace.decode("utf-8").splitlines())
    require(reader.fieldnames == ["source", "target", "count"], "invalid trace header")
    total = 0
    for row in reader:
        require(set(row) == {"source", "target", "count"}, "invalid trace row")
        require(row["source"] in index and row["target"] in index, "unknown trace symbol")
        count = int(row["count"])
        require(0 < count <= 10**12, "count must be in 1..10^12")
        total += count
        require(total <= 10**12, "trace total exceeds 10^12")
        i, j = index[row["source"]], index[row["target"]]
        if i != j:
            weights[i, j] += count
            weights[j, i] += count
    require(weights.sum() > 0, "trace needs an inter-symbol transition")
    return (
        names,
        sizes,
        weights,
        {
            "symbols_sha256": hashlib.sha256(raw).hexdigest(),
            "trace_sha256": hashlib.sha256(trace).hexdigest(),
            "observed_transitions": total,
        },
    )


def cut(weights, selected):
    selected = set(selected)
    return sum(
        int(weights[i, j]) for i in selected for j in range(len(weights)) if j not in selected
    )


def mbo_split(weights, seed, steps=40, restarts=8):
    """Normalized heat time: exp(-L/dmax), then cardinality projection.

    Every iterate keeps exactly floor(n/2) nodes. This constrains node count,
    not bytes. Return the best cut seen, since thresholding can cycle.
    """
    n = len(weights)
    lap = np.diag(weights.sum(axis=1)) - weights
    scale = max(1, int(weights.sum(axis=1).max()))
    values, vectors = np.linalg.eigh(lap / scale)
    heat = (vectors * np.exp(-np.maximum(values, 0))) @ vectors.T
    rng = random.Random(seed)
    best = None
    for _ in range(restarts):
        chosen = set(rng.sample(range(n), n // 2))
        for _ in range(steps):
            candidate = (cut(weights, chosen), tuple(sorted(chosen)))
            if best is None or candidate < best:
                best = candidate
            u = np.array([1.0 if i in chosen else -1.0 for i in range(n)])
            diffused = heat @ u
            new = set(sorted(range(n), key=lambda i: (-diffused[i], i))[: n // 2])
            if new == chosen:
                break
            chosen = new
    return best[1]


def coarsen(weights, count, seed):
    groups = [list(range(len(weights)))]
    while len(groups) < count:
        eligible = [i for i, group in enumerate(groups) if len(group) > 1]
        slot = max(eligible, key=lambda i: (len(groups[i]), -i))
        group = groups.pop(slot)
        chosen = set(mbo_split(weights[np.ix_(group, group)], seed + len(groups)))
        groups.extend(
            [
                [v for i, v in enumerate(group) if i in chosen],
                [v for i, v in enumerate(group) if i not in chosen],
            ]
        )
    groups.sort(key=lambda group: min(group))
    coarse = np.zeros((count, count), dtype=np.int64)
    for i in range(count):
        for j in range(i + 1, count):
            coarse[i, j] = coarse[j, i] = weights[np.ix_(groups[i], groups[j])].sum()
    return groups, coarse


def qubo(weights, k):
    """Upper triangular Q: E=offset+sum(Qij*xi*xj), xi=(1-Zi)/2.

    A=total edge weight+1 makes every cardinality violation more expensive
    than any feasible cut. All arithmetic is integer, including the offset.
    """
    n = len(weights)
    penalty = sum(int(weights[i, j]) for i in range(n) for j in range(i + 1, n)) + 1
    terms = [[i, i, int(weights[i].sum()) + penalty * (1 - 2 * k)] for i in range(n)]
    terms += [
        [i, j, 2 * penalty - 2 * int(weights[i, j])] for i in range(n) for j in range(i + 1, n)
    ]
    return {
        "variables": n,
        "selected_count": k,
        "penalty": penalty,
        "offset": penalty * k * k,
        "upper_triangular_terms": terms,
        "energy": "offset + sum(q_ij*x_i*x_j); x_i=(1-Z_i)/2",
        "bit_order": "array index i is variable i; no string endianness implied",
    }


def exact(weights, k):
    return min(
        (cut(weights, bits), bits) for bits in itertools.combinations(range(len(weights)), k)
    )[1]


def ising(model):
    """Integer numerators over four avoid rounded QUBO-to-Pauli conversion."""
    linear = [0] * model["variables"]
    offset = 4 * model["offset"]
    pairs = []
    for i, j, coefficient in model["upper_triangular_terms"]:
        if i == j:
            offset += 2 * coefficient
            linear[i] -= 2 * coefficient
        else:
            offset += coefficient
            linear[i] -= coefficient
            linear[j] -= coefficient
            pairs.append([i, j, coefficient])
    return {
        "denominator": 4,
        "offset_numerator": offset,
        "z_numerators": linear,
        "zz_numerators": pairs,
        "convention": "Z_i=+1 for x_i=0; Z_i=-1 for x_i=1",
    }


def anneal(weights, k, seed, steps=4000):
    rng = random.Random(seed)
    chosen = set(rng.sample(range(len(weights)), k))
    energy = cut(weights, chosen)
    best = (energy, tuple(sorted(chosen)))
    initial = max(1, max(map(int, weights.sum(axis=1))))
    for step in range(steps):
        remove = rng.choice(sorted(chosen))
        add = rng.choice(sorted(set(range(len(weights))) - chosen))
        trial = chosen - {remove} | {add}
        new_energy = cut(weights, trial)
        temperature = initial * (0.001 ** (step / max(1, steps - 1)))
        if new_energy <= energy or rng.random() < math.exp((energy - new_energy) / temperature):
            chosen, energy = trial, new_energy
        best = min(best, (energy, tuple(sorted(chosen))))
    return best[1]


def qaoa_p1(weights, k, seed, shots=4096):
    """Ideal statevector p=1 on the exported penalized QUBO; at most 12 bits.

    Fixed coarse angle grid, then independent seeded sampling. This is a
    rehearsal; it neither submits jobs nor measures quantum advantage.
    """
    n = len(weights)
    model = qubo(weights, k)
    energies = np.array(
        [
            cut(weights, [i for i in range(n) if (s >> i) & 1])
            + model["penalty"] * (s.bit_count() - k) ** 2
            for s in range(1 << n)
        ],
        dtype=float,
    )
    scale = max(1.0, float(energies.max()))
    best = None
    for gamma in np.linspace(0, 2 * np.pi, 12, endpoint=False):
        for beta in np.linspace(0, np.pi / 2, 8, endpoint=False):
            state = np.exp(-1j * gamma * energies / scale) / math.sqrt(1 << n)
            for bit in range(n):
                view = state.reshape(-1, 2, 1 << bit)
                a, b = view[:, 0, :].copy(), view[:, 1, :].copy()
                view[:, 0, :] = np.cos(beta) * a - 1j * np.sin(beta) * b
                view[:, 1, :] = np.cos(beta) * b - 1j * np.sin(beta) * a
            probabilities = np.abs(state) ** 2
            mean = float(probabilities @ energies)
            if best is None or mean < best[0]:
                best = (mean, gamma, beta, probabilities.copy())
    rng = np.random.default_rng(seed)
    draws = rng.choice(1 << n, size=shots, p=best[3] / best[3].sum())
    feasible = [int(s) for s in draws if int(s).bit_count() == k]
    bits = None
    if feasible:
        winner = min(feasible, key=lambda s: (energies[s], s))
        bits = [i for i in range(n) if (winner >> i) & 1]
    return bits, {
        "shots": shots,
        "feasible_shots": len(feasible),
        "gamma": float(best[1]),
        "beta": float(best[2]),
        "energy_scale": scale,
        "grid_evaluations": 96,
        "mean_penalized_energy": best[0],
    }


def experiment(names, sizes, weights, clusters, seed, quantum=False):
    require(not quantum or clusters <= 12, "ideal QAOA rehearsal is limited to 12 clusters")
    groups, reduced = coarsen(weights, clusters, seed)
    k = clusters // 2
    rng = random.Random(seed)
    choices = {
        "random": tuple(sorted(rng.sample(range(clusters), k))),
        "diffusion": mbo_split(reduced, seed),
        "annealing": anneal(reduced, k, seed),
    }
    if clusters <= 20:
        choices["exact"] = exact(reduced, k)
    qmeta = None
    if quantum:
        bits, qmeta = qaoa_p1(reduced, k, seed)
        if bits is not None:
            choices["ideal_qaoa_p1"] = bits
    results = {}
    for solver, selected in choices.items():
        lifted = [v for i in selected for v in groups[i]]
        require(cut(weights, lifted) == cut(reduced, selected), "coarsening cut mismatch")
        results[solver] = {
            "selected_clusters": list(selected),
            "cut_transitions": cut(reduced, selected),
            "selected_bytes": sum(sizes[i] for i in lifted),
        }
    model = qubo(reduced, k)
    return {
        "schema": "discobsd-graph-layout-v1",
        "seed": seed,
        "evidence_class": "host model; no cache, linker, or board timing claim",
        "numpy_version": np.__version__,
        "python_version": sys.version,
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "clusters": [
            {"symbols": [names[i] for i in group], "bytes": sum(sizes[i] for i in group)}
            for group in groups
        ],
        "cluster_weights": reduced.tolist(),
        "qubo": model,
        "ising": ising(model),
        "results": results,
        "qaoa": qmeta,
        "exact_scope": "reduced graph only; coarsening may exclude global optima",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbols", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--clusters", default=8, type=int)
    parser.add_argument("--seed", default=1729, type=int)
    parser.add_argument("--ideal-qaoa", action="store_true")
    args = parser.parse_args()
    try:
        names, sizes, weights, provenance = load_graph(args.symbols, args.trace)
        require(2 <= args.clusters <= min(64, len(names)), "invalid cluster count (2..64)")
        require(0 <= args.seed < 2**32, "seed must be a uint32")
        report = experiment(names, sizes, weights, args.clusters, args.seed, args.ideal_qaoa)
        report["inputs"] = provenance
        json.dump(report, sys.stdout, indent=2, allow_nan=False)
        print()
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, f"graph-layout: {error}\n")


if __name__ == "__main__":
    main()
