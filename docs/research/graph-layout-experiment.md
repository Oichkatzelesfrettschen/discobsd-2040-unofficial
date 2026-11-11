# Host graph-layout experiment

The experiment implements the first locality-search stage for the RP2040
performance program. It runs entirely on a host and adds no firmware bytes.
It consumes measured symbol sizes and transition counts, coarsens a graph
with MBO heat diffusion, and compares balanced partitions using exact search,
simulated annealing, diffusion, a random baseline, and optional ideal p=1 QAOA.
The bundled fixture is synthetic. No board speedup has been measured.

Run from the repository root with a Python environment containing the pinned
NumPy dependency in `tools/graph-layout-example/requirements.txt`:

```sh
export PYTHON=python3
"$PYTHON" tools/graph_layout_test.py
"$PYTHON" tools/graph_layout.py \
  --symbols tools/graph-layout-example/symbols.json \
  --trace tools/graph-layout-example/transitions.csv \
  --clusters 8 --seed 1729 --ideal-qaoa > /tmp/graph-layout-report.json
```

These are standalone research commands, not registered firmware admission
gates. Their tests do not require or establish a target build. The output
contains input and tool SHA-256 digests, seed, Python/NumPy versions, cluster membership and
byte sizes, edge weights, solver partitions, cut scores, QUBO coefficients,
and QAOA sampling metadata. It contains no local input paths. Float results
are reproducible on a fixed numeric stack; cross-platform byte identity is
not promised. Retain the source commit alongside each report as well.

## Inputs and the measurement boundary

Symbols are a JSON array of unique `{ "name": "symbol", "bytes": 123 }`
records. Supply linked extents from the exact ELF/a.out build being profiled.
The CSV header is exactly `source,target,count`. Each row records observed
transitions between known symbols; repeated and reverse edges are summed.
Self transitions contribute to provenance totals but not cuts. Unresolved
symbols, nonpositive counts, duplicate symbols and malformed fields fail.
At most 512 symbols and 10^12 aggregate transitions are accepted. Aggregate
traces on the host before ingestion. This is not a raw PC trace collector.
Dynamic PC attribution, interrupt boundaries, inlined code and symbol aliases
must be resolved by the acquisition tool; static calls cannot stand in for
measured execution counts.

The graph is undirected because the cut counts traffic across a partition
in either direction. Directional ordering information is deliberately lost.
This limits the resulting model's usefulness for final linear code layout.

## Mathematics and solver limits

For symmetric nonnegative weights W, L=diag(W1)-W. Each MBO step applies
exp(-L/dmax) to a +/-1 vector, then retains the largest floor(n/2) entries.
This is the exact dense heat kernel up to floating-point eigensolver error,
with dimensionless diffusion time 1/dmax. Eight seeded starts and at most
40 iterations per start retain the best observed cut, including cycles.
Recursive splitting of the largest group yields the requested cluster count.
Coarse weights sum all inter-group edges, preserving every lifted cut exactly.

The final model minimizes cut traffic with exactly k=floor(m/2) of m clusters:

    E(x) = sum(i<j) W_ij (x_i + x_j - 2 x_i x_j)
           + A (sum_i x_i - k)^2
    A = sum(i<j) W_ij + 1

Every infeasible assignment costs at least A, while every feasible cut is
at most A-1. The QUBO uses one upper-triangular coefficient per pair and an
explicit constant offset. The exported Ising form uses integer numerators
with denominator four, including its constant offset. An exhaustive test
checks all QUBO and Ising energies against an independent cut oracle.
The Pauli convention is x_i=(1-Z_i)/2. Variable i
corresponds to bit i in the ideal simulator's integer state index.

Exact enumeration runs only for m<=20 and proves the optimum of the reduced
graph. Coarsening may exclude the original optimum. Annealing performs 4,000
cardinality-preserving swaps with a geometric temperature schedule. The
random baseline is one seeded feasible draw, not a statistical comparison.
Diffusion also preserves cardinality. Cluster bytes can differ substantially;
none of these partitions certifies cache capacity, SRAM capacity or alignment.

Optional ideal QAOA supports m<=12, applies the full penalized cost phase and
the X mixer, and evaluates a fixed 12-by-8 gamma/beta grid. It minimizes mean
penalized energy and draws 4,096 seeded shots from the best distribution.
The report includes the energy scaling, feasible-shot count and grid cost.
No feasible shot means no QAOA candidate. This small ideal rehearsal is a
mathematical check; it never invokes a provider, Qiskit, credentials or a QPU.
No hardware runtime estimate or quantum advantage follows from it.

## Next discriminating measurements

Acquire real traces from a pinned build for cold boot, shell regression,
compile/link, filesystem traffic, swap pressure and console load. Hold out
at least one workload. Repeat across seeds and workloads before comparing
solver quality; count preprocessing and search effort. Test penalty-induced
QAOA feasibility loss against a constraint-preserving mixer before hardware.

Before generating a linker change, model actual RP2040 cache geometry,
function alignment, relocation constraints and cache-set conflicts. A lower
cut is not automatically a faster layout. Compare with ordinary profile-guided
ordering, then validate linked extents, correctness, XIP misses, elapsed time,
stack and flash growth. Require board A/B runs with randomized order and cold
and warm caches. A cut improvement without a board improvement falsifies
the locality proxy for that workload. No linker, kernel, flash or board
modification is performed by this experiment.
