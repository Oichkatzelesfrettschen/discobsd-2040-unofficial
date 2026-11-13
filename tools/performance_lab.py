"""Bounded host experiments for the remaining RP2040 optimization tracks."""

import argparse
import hashlib
import itertools
import json
import math
import random
import sys
from pathlib import Path

import numpy as np
import scipy
from graph_layout import cut, qubo, require
from scipy.linalg import expm
from scipy.optimize import linear_sum_assignment, minimize
from scipy.stats import lognorm, weibull_min


def temper(weights, k, seed, sweeps=500):
    """Replica exchange with cardinality-preserving Metropolis swaps."""
    rng = random.Random(seed)
    n = len(weights)
    scale = max(1, int(weights.sum(axis=1).max()))
    temperatures = np.geomspace(scale * 0.001, scale, 8)
    states = [set(rng.sample(range(n), k)) for _ in temperatures]
    energies = [cut(weights, s) for s in states]
    best = min((e, tuple(sorted(s))) for e, s in zip(energies, states))
    exchanges = 0
    for _ in range(sweeps):
        for j, temperature in enumerate(temperatures):
            old = states[j]
            a = rng.choice(sorted(old))
            b = rng.choice(sorted(set(range(n)) - old))
            new = old - {a} | {b}
            energy = cut(weights, new)
            delta = energy - energies[j]
            if delta <= 0 or rng.random() < math.exp(-delta / temperature):
                states[j], energies[j] = new, energy
                best = min(best, (energy, tuple(sorted(new))))
        for j in range(len(states) - 1):
            log_ratio = (1 / temperatures[j] - 1 / temperatures[j + 1]) * (
                energies[j] - energies[j + 1]
            )
            if math.log(max(rng.random(), 1e-300)) < min(0, log_ratio):
                states[j], states[j + 1] = states[j + 1], states[j]
                energies[j], energies[j + 1] = energies[j + 1], energies[j]
                exchanges += 1
    return list(best[1]), {"sweeps": sweeps, "replicas": 8, "exchanges": exchanges}


def compress_mps(state, n, bond):
    """Dense bounded reference with sequential Schmidt truncation and reconstruction.

    This deliberately retains a dense state: it tests truncation sensitivity,
    not scalable MPS performance. Axis zero is the most significant bit.
    """
    residual = state.reshape(1, -1)
    cores = []
    left = 1
    discarded = 0.0
    for _ in range(n - 1):
        u, singular, vh = np.linalg.svd(residual.reshape(left * 2, -1), full_matrices=False)
        rank = min(bond, len(singular))
        discarded += float(np.sum(singular[rank:] ** 2))
        cores.append(u[:, :rank].reshape(left, 2, rank))
        residual = singular[:rank, None] * vh[:rank]
        left = rank
    reconstructed = residual.reshape(left, 2, 1)
    for core in reversed(cores):
        reconstructed = np.tensordot(core, reconstructed, axes=(-1, 0))
    vector = reconstructed.reshape(-1)
    require(np.linalg.norm(vector) > 0, "MPS truncation lost the state")
    return vector / np.linalg.norm(vector), discarded


def imaginary_time(weights, k, seed, bond=8, steps=60, shots=4096):
    """Trotter imaginary-time annealing with an X mixer and Schmidt truncation."""
    n = len(weights)
    require(2 <= n <= 12 and bond >= 1, "imaginary-time reference needs 2..12 bits")
    model = qubo(weights, k)
    energies = np.array(
        [
            cut(weights, [i for i in range(n) if s >> i & 1])
            + model["penalty"] * (s.bit_count() - k) ** 2
            for s in range(1 << n)
        ],
        dtype=float,
    )
    scale = max(1.0, float(energies.max()))
    energies /= scale
    state = np.ones(1 << n) / math.sqrt(1 << n)
    discarded = 0.0
    for step in range(steps):
        s = (step + 1) / steps
        # dt=0.2, exp(-dt*s*Hc/2), exp(dt*(1-s)*sum X), same half cost.
        damping = np.exp(-0.1 * s * energies)
        state *= damping
        for bit in range(n):
            view = state.reshape(-1, 2, 1 << bit)
            a, b = view[:, 0, :].copy(), view[:, 1, :].copy()
            theta = 0.2 * (1 - s)
            view[:, 0, :] = np.cosh(theta) * a + np.sinh(theta) * b
            view[:, 1, :] = np.cosh(theta) * b + np.sinh(theta) * a
        state *= damping
        state /= np.linalg.norm(state)
        state, loss = compress_mps(state, n, bond)
        discarded += loss
    probabilities = np.abs(state) ** 2
    draws = np.random.default_rng(seed).choice(
        len(state), shots, p=probabilities / probabilities.sum()
    )
    feasible = [int(s) for s in draws if int(s).bit_count() == k]
    selected = None
    if feasible:
        winner = min(feasible, key=lambda s: (energies[s], s))
        selected = [i for i in range(n) if winner >> i & 1]
    return selected, {
        "bond": bond,
        "steps": steps,
        "shots": shots,
        "feasible_shots": len(feasible),
        "summed_discarded_norm2": discarded,
        "mean_penalized_energy": float(probabilities @ energies) * scale,
        "dense_state_bytes": state.nbytes,
        "scope": "dense MPS reference; no scalability or speedup claim",
    }


def oscillator(weights, k, seed, steps=1000):
    """Damped quartic spin relaxation, not a claim to reproduce a published SB solver."""
    n = len(weights)
    q = qubo(weights, k)
    x = np.random.default_rng(seed).normal(0, 0.01, n)
    y = np.zeros(n)
    h = np.zeros(n)
    coupling = np.zeros((n, n))
    for i, j, coefficient in q["upper_triangular_terms"]:
        if i == j:
            h[i] -= coefficient / 2
        else:
            h[i] -= coefficient / 4
            h[j] -= coefficient / 4
            coupling[i, j] = coupling[j, i] = coefficient / 4
    scale = max(1, np.max(np.abs(h)), np.max(np.sum(np.abs(coupling), axis=1)))
    best = None
    for step in range(steps):
        pump = 2 * step / steps
        y += 0.02 * (-(1 - pump) * x - x**3 - (h + coupling @ x) / scale - 0.3 * y)
        x += 0.02 * y
        require(np.all(np.isfinite(x)), "oscillator integration diverged")
        selected = tuple(sorted(np.argsort(x)[:k].tolist()))  # x=+1 represents bit zero.
        value = (cut(weights, selected), selected)
        best = value if best is None else min(best, value)
    return list(best[1]), {"steps": steps, "dt": 0.02, "damping": 0.3}


def walks(weights, time=1.0):
    """Return distributions from vertex zero for heat flow and a coherent walk."""
    lap = np.diag(weights.sum(axis=1)) - weights
    lap = lap / max(1, float(weights.sum(axis=1).max()))
    values, vectors = np.linalg.eigh(lap)
    heat = (vectors * np.exp(-time * np.maximum(values, 0))) @ vectors[0]
    amplitude = (vectors * np.exp(-1j * time * values)) @ vectors[0]
    return {
        "time": time,
        "initial_vertex": 0,
        "heat": heat.tolist(),
        "quantum_probability": (np.abs(amplitude) ** 2).tolist(),
    }


def solve_mdp(model):
    """Discounted finite-state Bellman solver; dynamics must come from measurements."""
    discount = model["discount"]
    require(0 <= discount < 1, "discount must be in [0,1)")
    states = model["states"]
    require(isinstance(states, list) and 1 <= len(states) <= 256, "need 1..256 states")
    actions = []
    for state in states:
        require(state and isinstance(state, list), "each state needs actions")
        row = []
        for action in state:
            p = np.asarray(action["probabilities"], dtype=float)
            cost = float(action["cost"])
            require(
                p.shape == (len(states),)
                and np.all(np.isfinite(p))
                and np.all(p >= 0)
                and abs(float(p.sum()) - 1) < 1e-12
                and math.isfinite(cost),
                "invalid action",
            )
            row.append((str(action["name"]), cost, p))
        require(len({a[0] for a in row}) == len(row), "duplicate action name")
        actions.append(row)
    value = np.zeros(len(states))
    for _iteration in range(100000):
        updated = np.array([min(c + discount * p @ value for _, c, p in row) for row in actions])
        delta = float(np.max(np.abs(updated - value)))
        value = updated
        if delta / max(1e-15, 1 - discount) <= 1e-8:
            break
    else:
        raise ValueError("Bellman iteration did not converge")
    indices = [
        min(range(len(row)), key=lambda i: row[i][1] + discount * row[i][2] @ value)
        for row in actions
    ]
    transition = np.array([row[i][2] for row, i in zip(actions, indices)])
    costs = np.array([row[i][1] for row, i in zip(actions, indices)])
    direct = np.linalg.solve(np.eye(len(states)) - discount * transition, costs)
    return {
        "policy": [row[i][0] for row, i in zip(actions, indices)],
        "values": value.tolist(),
        "iterations": _iteration + 1,
        "value_error_bound": delta / max(1e-15, 1 - discount),
        "direct_policy_check_max_error": float(np.max(np.abs(value - direct))),
        "transition": transition.tolist(),
        "scope": "finite-state discounted control; not a measured SwapRAM policy",
    }


def wear_transport(model):
    """One write per source to a distinct destination; erase relocation cost is explicit."""
    wear = np.asarray(model["erase_counts"], dtype=float)
    relocation = np.asarray(model["relocation_cost"], dtype=float)
    n = len(wear)
    require(
        1 <= n <= 512
        and relocation.shape == (n, n)
        and np.all(np.isfinite(wear))
        and np.all(wear >= 0)
        and np.all(np.isfinite(relocation))
        and np.all(relocation >= 0),
        "invalid wear model",
    )
    weight = float(model["wear_weight"])
    require(math.isfinite(weight) and weight >= 0, "invalid wear weight")
    # Increase in squared wear from one destination erase: (e+1)^2-e^2.
    costs = relocation + weight * (2 * wear + 1)[None, :]
    writes = model["writes"]
    require(type(writes) is int and 1 <= writes <= n, "invalid write count")
    rows, columns = linear_sum_assignment(costs[:writes])
    return {
        "destinations": columns.tolist(),
        "objective": float(costs[rows, columns].sum()),
        "scope": "unit-capacity batch transport; no FTL or endurance claim",
    }


def offload(model):
    tasks = model["tasks"]
    require(1 <= len(tasks) <= 20, "exact offload selection limited to 20 tasks")
    budget = model["sram_budget"]
    require(type(budget) is int and budget >= 0, "invalid SRAM budget")
    penalties = model["pair_cycles"]
    require(
        len(penalties) == len(tasks) and all(len(row) == len(tasks) for row in penalties),
        "invalid contention matrix",
    )
    for i, task in enumerate(tasks):
        require(
            type(task["bytes"]) is int
            and task["bytes"] >= 0
            and math.isfinite(task["saved_cycles"]),
            "invalid task",
        )
        for j in range(len(tasks)):
            require(
                math.isfinite(penalties[i][j])
                and penalties[i][j] >= 0
                and penalties[i][j] == penalties[j][i],
                "invalid pair cost",
            )
    best = (0.0, ())
    for bits in itertools.product((0, 1), repeat=len(tasks)):
        selected = tuple(i for i, bit in enumerate(bits) if bit)
        if sum(tasks[i]["bytes"] for i in selected) > budget:
            continue
        gain = sum(tasks[i]["saved_cycles"] for i in selected)
        gain -= sum(penalties[i][j] for i in selected for j in selected if i < j)
        if gain > best[0]:
            best = (gain, selected)
    return {
        "selected": [tasks[i]["name"] for i in best[1]],
        "net_saved_cycles": best[0],
        "bytes": sum(tasks[i]["bytes"] for i in best[1]),
        "scope": "exact supplied-cost model; no core-1 execution or measured speedup",
    }


def drift_diffusion(model):
    """Conservative finite-volume generator with reflecting left/absorbing right edge."""
    n, dx, drift, diffusion, time = (
        model[k] for k in ("cells", "dx", "drift", "diffusion", "time")
    )
    require(
        type(n) is int
        and 2 <= n <= 256
        and all(math.isfinite(v) for v in (dx, drift, diffusion, time))
        and dx > 0
        and diffusion >= 0
        and time >= 0,
        "invalid drift-diffusion parameters",
    )
    q = np.zeros((n + 1, n + 1))  # last state is absorbed probability mass.
    right = max(drift, 0) / dx + diffusion / dx**2
    left = max(-drift, 0) / dx + diffusion / dx**2
    for i in range(n):
        q[i + 1, i] += right
        if i > 0:
            q[i - 1, i] += left
        q[i, i] = -q[:, i].sum()
    p = np.asarray(model["initial"], dtype=float)
    require(
        p.shape == (n + 1,)
        and np.all(np.isfinite(p))
        and np.all(p >= 0)
        and abs(float(p.sum()) - 1) < 1e-12,
        "invalid initial mass",
    )
    result = expm(time * q) @ p
    require(
        np.min(result) > -1e-12 and abs(float(result.sum()) - 1) < 1e-9,
        "mass or positivity failure",
    )
    return {
        "mass": result.tolist(),
        "absorbed_probability": float(result[-1]),
        "conservation_error": abs(float(result.sum()) - 1),
        "scope": "upwind finite-volume drift-diffusion; coefficients supplied, not fitted",
    }


def profile_select(model):
    """Exact union-cost closure selection; shared objects are paid only once."""
    features, objects = model["features"], model["objects"]
    require(1 <= len(features) <= 20, "profile enumeration limited to 20 features")
    names = {f["name"] for f in features}
    require(len(names) == len(features), "duplicate feature")
    for feature in features:
        require(
            set(feature["needs"]) <= names
            and set(feature["objects"]) <= set(objects)
            and math.isfinite(feature["benefit"]),
            "invalid profile dependencies",
        )
    for obj in objects.values():
        require(
            all(type(obj[key]) is int and obj[key] >= 0 for key in ("blocks", "inodes")),
            "invalid object cost",
        )
    require(
        all(type(model[key]) is int and model[key] >= 0 for key in ("blocks", "inodes")),
        "invalid profile budget",
    )
    best = (0.0, [], 0, 0)
    for bits in itertools.product((0, 1), repeat=len(features)):
        chosen = [f for f, bit in zip(features, bits) if bit]
        selected = {f["name"] for f in chosen}
        if any(not set(f["needs"]) <= selected for f in chosen):
            continue
        used = set().union(*(set(f["objects"]) for f in chosen))
        blocks = sum(objects[o]["blocks"] for o in used)
        inodes = sum(objects[o]["inodes"] for o in used)
        benefit = sum(f["benefit"] for f in chosen)
        if blocks <= model["blocks"] and inodes <= model["inodes"] and benefit > best[0]:
            best = benefit, sorted(selected), blocks, inodes
    return {
        "benefit": best[0],
        "selected": best[1],
        "blocks": best[2],
        "inodes": best[3],
        "scope": "exact union-cost model; rebuild fsutil image to verify actual allocation",
    }


def tail_audit(model):
    """Conditional tail fits at a supplied fixed threshold; temporal holdout."""
    samples = np.asarray(model["samples"], dtype=float)
    xmin = float(model["threshold"])
    require(
        samples.ndim == 1
        and 100 <= len(samples) <= 100000
        and np.all(np.isfinite(samples))
        and math.isfinite(xmin)
        and xmin > 0
        and np.all(samples >= xmin),
        "need 100..100000 finite observations above threshold",
    )
    train, test = np.array_split(samples, 2)
    z = np.log(train / xmin)
    require(float(z.mean()) > 0, "degenerate tail")
    alpha = 1 / float(z.mean())
    fits = {
        "pareto": {
            "alpha": alpha,
            "heldout_mean_log_likelihood": float(
                np.mean(math.log(alpha / xmin) - (alpha + 1) * np.log(test / xmin))
            ),
        }
    }
    for label, distribution in (("lognormal", lognorm), ("weibull", weibull_min)):

        def logs(parameters, data, distribution=distribution):
            shape, scale = np.exp(parameters)
            # Optimizer probes may overflow; objective rejects nonfinite values.
            with np.errstate(over="ignore", invalid="ignore", divide="ignore"):
                return (
                    distribution.logpdf(data / xmin, shape, scale=scale)
                    - math.log(xmin)
                    - distribution.logsf(1, shape, scale=scale)
                )

        def objective(parameters):
            values = logs(parameters, train)
            return -float(values.mean()) if np.all(np.isfinite(values)) else 1e100

        result = minimize(objective, [0.0, 0.0], bounds=[(-4, 4), (-8, 8)], method="L-BFGS-B")
        require(result.success, f"{label} tail fit did not converge")
        heldout = logs(result.x, test)
        require(np.all(np.isfinite(heldout)), "nonfinite tail likelihood")
        fits[label] = {
            "shape": float(np.exp(result.x[0])),
            "scale": float(np.exp(result.x[1]) * xmin),
            "heldout_mean_log_likelihood": float(heldout.mean()),
            "at_parameter_bound": bool(
                any(
                    abs(x - a) < 1e-4 or abs(x - b) < 1e-4
                    for x, (a, b) in zip(result.x, [(-4, 4), (-8, 8)])
                )
            ),
        }
    # Seeded nonparametric uncertainty on the Pareto exponent, conditional on this threshold.
    rng = np.random.default_rng(1729)
    bootstrap = [1 / float(rng.choice(z, len(z), replace=True).mean()) for _ in range(200)]
    low, high = np.quantile(bootstrap, [0.025, 0.975])
    return {
        "fits": fits,
        "pareto_alpha_95_percentile_interval": [float(low), float(high)],
        "training_count": len(train),
        "heldout_count": len(test),
        "scope": "tail model comparison; cannot establish a fractional dynamical law",
    }


def qaoa_qasm(model):
    """Portable ideal p=1 circuit; physical routing/calibration is a later step."""
    weights = np.asarray(model["weights"])
    n = len(weights)
    require(
        2 <= n <= 64
        and weights.shape == (n, n)
        and np.issubdtype(weights.dtype, np.integer)
        and np.all(weights >= 0)
        and np.all(weights == weights.T)
        and np.all(np.diag(weights) == 0)
        and int(weights.max()) <= 10**9,
        "invalid QASM graph",
    )
    gamma, beta = float(model["gamma"]), float(model["beta"])
    scale = float(model["scale"])
    require(
        all(math.isfinite(x) for x in (gamma, beta, scale))
        and scale > 0
        and abs(beta) <= 1e6
        and abs(gamma) <= 1e6,
        "invalid QASM angle/scale",
    )
    from graph_layout import ising

    h = ising(qubo(weights, n // 2))
    lines = ["OPENQASM 2.0;", 'include "qelib1.inc";', f"qreg q[{n}];", f"creg c[{n}];"]
    lines += [f"h q[{i}];" for i in range(n)]
    for i, coefficient in enumerate(h["z_numerators"]):
        angle = gamma * coefficient / (2 * scale)
        require(math.isfinite(angle), "QASM rotation overflow")
        lines.append(f"rz({angle:.17g}) q[{i}];")
    for i, j, coefficient in h["zz_numerators"]:
        angle = gamma * coefficient / (2 * scale)
        require(math.isfinite(angle), "QASM rotation overflow")
        lines += [f"cx q[{i}],q[{j}];", f"rz({angle:.17g}) q[{j}];", f"cx q[{i}],q[{j}];"]
    lines += [f"rx({2 * beta:.17g}) q[{i}];" for i in range(n)]
    lines += [f"measure q[{i}] -> c[{i}];" for i in range(n)]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode", choices=("mdp", "wear", "offload", "pde", "profiles", "tails", "qasm")
    )
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    try:
        raw = args.input.read_bytes()
        model = json.loads(raw)
        if args.mode == "qasm":
            print(qaoa_qasm(model), end="")
            return
        result = {
            "mdp": solve_mdp,
            "wear": wear_transport,
            "offload": offload,
            "pde": drift_diffusion,
            "profiles": profile_select,
            "tails": tail_audit,
        }[args.mode](model)
        result["input_sha256"] = hashlib.sha256(raw).hexdigest()
        result["tool_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        result["numpy_version"] = np.__version__
        result["scipy_version"] = scipy.__version__
        json.dump(result, sys.stdout, indent=2, allow_nan=False)
        print()
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(2, f"performance-lab: {error}\n")


if __name__ == "__main__":
    main()
