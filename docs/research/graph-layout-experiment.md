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

These research commands also run through the optional `check-graph-layout`
target and their dedicated CI workflow. Their tests do not establish a target build. The output
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

## PC attribution and cache-aware candidate search

`tools/graph_layout_trace.py` implements the next stage. Supply a filtered
`nm -S -n --defined-only` listing containing only the XIP text extents to be
reordered. The tool rejects zero-sized aliases, overlaps, duplicate names,
odd addresses, and extents outside 0x10000000..0x10ffffff. Do not include
SRAM routines or deduce zero-sized symbol lengths from the next address.
Each CSV record is one chronological instruction fetch with header
`pc,width`; PC is an even absolute address (for example 0x10000180), and width
is 2 or 4 bytes. A fetch crossing its attributed symbol is rejected.
Provide isolated captures with no intervening reset/cache invalidation.

```sh
"$PYTHON" tools/graph_layout_trace_test.py
"$PYTHON" tools/graph_layout_trace.py \
  --nm tools/graph-layout-example/collision.nm \
  --training tools/graph-layout-example/training.csv \
  --heldout tools/graph-layout-example/heldout.csv \
  --trials 100 --seed 1729 > /tmp/cache-layout-report.json
```

The same invocation accepts `--emit symbols` and `--emit transitions` to
produce the JSON and CSV consumed by `graph_layout.py`. It aggregates
transitions only from the training capture. Identical training and held-out
files are rejected. Choosing truly independent workloads remains the
experimenter's responsibility; file inequality cannot establish independence.

Cache geometry is 16 KiB, two ways and eight-byte lines. The companion
notes' RP2040 datasheet section 2.6.3.1 confirms capacity and associativity;
`hardware-review-reconciliation.md` records the line size. Replacement is
tested under both LRU and FIFO hypotheses. Neither is asserted to model the
silicon's exact replacement, prefetch, instruction bus transaction formation,
literal/data reads or interrupt traffic. A 32-bit fetch crossing a line
contributes two line accesses. A warm run first replays one entire capture
without counting it. These scores are line misses, not processor cycles or
XIP_CTRL hardware counter predictions.

The search first tries packed original order with eight-byte alignment, then
bounded seeded pair swaps, accepting only strict improvements in the sum of
training cold LRU and FIFO misses. It retains the original addresses as the
fallback and never exceeds the original text span. Held-out cold/warm results
are reported for both policies but never affect candidate selection. The
search is a local heuristic, with no optimality guarantee. It reports every
new address and input/tool hashes. Holes and alignment can change the span;
this is not proof of a smaller linked image.

The collision fixture places three eight-byte functions 8,192 bytes apart.
They collide in one two-way set: nine training fetches miss nine times.
Packing them gives three cold misses and zero warm misses. The independent
reverse-order held-out capture changes from six cold misses to three. Both
LRU and FIFO agree for this fixture. These are synthetic model observations.

`--emit linker` produces ordered `*(.text.SYMBOL)` selectors with explicit
alignment for insertion after the vector table and before `*(.text*)` inside
the existing `.text` output section. No improving layout means no fragment.
The host test compiles a deliberately reversed C fixture with function
sections, links with the emitted fragment, and checks symbol order with nm.
It proves the host GNU linker mechanism only. The current kernel does not
enable per-function sections; its wildcard alone cannot reorder functions.
Before kernel integration, build with `-ffunction-sections`, check unique
input-section membership in the map, establish actual sizes and alignment,
account for unprofiled sections/literals/veneers, and verify the relinked
addresses match the model. The tool never edits or invokes the kernel linker.
Full trace acquisition and physical XIP counter calibration remain open.

## Remaining host experiments

Install the pinned NumPy and SciPy requirements and add `--extended` to the
graph command to compare replica-exchange tempering, quartic spin dynamics,
and imaginary-time annealing with bond limits 1, 2, 4 and 8. Both classical
and coherent graph walks start at vertex zero and report full probability
distributions. The extended report hashes the additional solver source.

Tempering uses eight temperatures and 500 sweeps with balanced exchanges.
Quartic spin dynamics is a damped classical relaxation heuristic; it is not
an implementation or reproduction claim for Goto's published simulated
bifurcation machine. Imaginary time applies symmetric cost/mixer splitting
with dt=0.2 over 60 steps, then sequential Schmidt truncation. It retains and
reconstructs a dense state at each step, deliberately limited to 12 variables.
This validates MPS approximation sensitivity, not scalable tensor-network
performance. Candidate selection uses 4,096 seeded samples, not exhaustive
inspection of every nonzero amplitude. Reports include feasible sample count,
mean penalized energy, bond limit and summed discarded norm squared; the last
is a diagnostic, not a rigorous final-state error bound. All solvers use the
same reduced graph, but their compute budgets differ. No speedup follows
from recovery of the same optimum.

The eight-symbol fixture recovers cut 5 with every extended solver at seed
1729. Increasing the bond limit from 1 to 8 reduces summed discarded norm
squared from about 0.0167 to 2.0e-11. This is a constructed calibration case,
not a population benchmark. Multi-seed, multi-workload studies remain needed.

`graph-layout-five-seed.json` retains the additional seeds 0..4 comparison.
The oscillator scores 5, 370, 5, 402 and 5, missing the optimum twice. All other
tested optimizers recover 5 for these seeds. The oscillator therefore fails
the reliability criterion even on this fixture and stays a research comparator.
This small seed study still does not establish performance on real graphs.

`tools/performance_lab.py MODE INPUT.json` runs the following independent
models. Inputs are hashed and reports record source and numeric versions.
The checked-in JSON fixtures are synthetic and have no board calibration.

| Mode | Input and implementation | Deciding control |
| --- | --- | --- |
| `mdp` | Discount in [0,1), and states containing named actions with scalar cost and next-state probability vector. Discounted Bellman iteration exports a bounded table policy, transitions, values and convergence bound. Infeasible actions must be absent. | Known delayed-reward policy, changed-cost reversal, stochastic-row rejection and direct policy linear solve. |
| `wear` | Erase counts, nonnegative relocation matrix, wear weight and number of writes. Exact unit-capacity assignment minimizes relocation plus increase in squared wear. | Exhaustive assignment comparison. |
| `offload` | Tasks with bytes and measured saved cycles, symmetric pair contention costs and SRAM budget. Enumerates all subsets up to 20 tasks. | Excess contention changes the winner; zero budget selects nothing. |
| `pde` | Cells, dx, drift, diffusion, time and initial probability mass including an absorbing state. Upwind finite-volume generator with reflecting left boundary and absorbing right boundary, propagated by matrix exponential. | Two successive rate-one jumps absorb probability 1-exp(-t)(1+t); positivity and mass checks. |
| `profiles` | Features with benefit, dependencies and object names; objects with block/inode costs; two budgets. Exact union-cost enumeration up to 20 features. | Shared multicall object is charged once; removing budget excludes its dependent feature. |
| `tails` | 100..100,000 positive samples in temporal order and an explicitly chosen positive tail threshold. All samples must exceed the threshold. | Seeded Pareto exponent recovery, bootstrap interval, invalid-threshold control. |
| `qasm` | Symmetric nonnegative integer weights, gamma, beta and positive energy scale. Emits portable OpenQASM 2 for p=1 H/cost/X-mixer/measurement, with CNOT-RZ-CNOT ZZ gates. | Independently interpret the emitted gates and compare probabilities with direct cost and mixer matrices. |

For example:

```sh
"$PYTHON" tools/performance_lab_test.py
"$PYTHON" tools/performance_lab.py mdp tools/graph-layout-example/policy.json
"$PYTHON" tools/performance_lab.py wear tools/graph-layout-example/wear.json
"$PYTHON" tools/performance_lab.py offload tools/graph-layout-example/offload.json
"$PYTHON" tools/performance_lab.py pde tools/graph-layout-example/pde.json
"$PYTHON" tools/performance_lab.py profiles tools/graph-layout-example/profiles.json
"$PYTHON" tools/performance_lab.py qasm tools/graph-layout-example/qasm.json
```

The MDP is a finite-state counterpart to the proposed HJB control model.
It does not invent SwapRAM transition probabilities or state definitions.
Calibrate free bytes, largest extent, compressed-image class, allocation
ownership and release events from actual workload traces before exporting
its policy into any runtime. Costs must use normalized units or explicit
conversion weights. A policy table itself does not establish firmware savings.

Wear assignment handles one erase per assigned write, unique destinations,
and one finite batch. It does not model Dhara garbage collection, recovery,
page programming, repeated destinations or unknown erase persistence. The
PDE coefficients have units dx/time and dx squared/time; erase count is a
possible state coordinate, not a physical space claim. Require mesh and time
convergence, fitted coefficients and held-out tail predictions before claiming
flash lifetime. Offload input must include launch, synchronization, SRAM code
residency and flash-off safety costs measured on the target configuration.
Profile union costs also need real fsutil allocation and dependency validation;
the model cannot derive nonadditive directory block allocation from names.

The tail audit uses the first half for fitting and second half for likelihood
comparison. Pareto is fit analytically; lognormal and Weibull are fitted as
distributions conditional above the same fixed threshold. Bounded optimizer
solutions report boundary hits. Two hundred seeded bootstrap resamples give
an exponent interval under an independent-sample assumption; serial dependence
requires block bootstrap or independent captures. Thresholds must be chosen
before evaluation. A Pareto fit, including an exponent below one, cannot prove
a fractional PDE or justify its use without a dynamical memory experiment.

## Hardware and integration frontier

The host paths above are implemented and tested. Real PC acquisition, measured
SwapRAM transitions, erase telemetry, contention measurements, ARM relinking,
board A/B testing, formal Lean proofs and provider experiments remain open.
The implementation environment lacks bmake, arm-none-eabi-gcc and Renode.
The current firmware/CI build tiers remain the authority for target compilation.
No new solver, Python dependency or policy is included in the shipped root.

A first QPU case should freeze a real 16..24-cluster training graph and its
held-out workload, source hashes, QUBO/Ising mapping, angles and seed. Use
offline angle training, depths 1..3, four gauge encodings and 4,096 shots per
arm with equal-shape null controls. The QASM emitter currently supplies p=1
only; it does not claim routed/native circuit equivalence or estimate runtime.
Refresh the live Heron Target, compile, inspect routing growth and run the
repository's noise rehearsal before calculating a seconds ceiling. Simulation
answers the ideal optimization question; hardware tests how device errors
change feasible-sample probability and the low-energy distribution. A written
case with the resulting backend, calibration, circuit hashes, controls and
charge ceiling must receive the approval required by gororoba-quantum's
AGENTS.md before submission. No provider account was accessed in this stage.
