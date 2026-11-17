# Discussion-to-implementation audit

The first three implementation passes covered graph/cache models and synthetic
systems experiments, but did not complete the earlier release-security work,
compiled ARM acquisition, MILP, a tensor-only MPS solver, constrained noisy
circuits, or formal proof checks. This audit records the executable frontier.

| Discussed item | Implemented mechanism | Remaining evidence or boundary |
| --- | --- | --- |
| Graph-PDE coarsening | MBO heat diffusion, recursive partitions, exact lifted-cut checks | Actual cache/byte-capacity constraints remain separate from a graph cut. |
| Exact classical comparator | Enumeration and SciPy/HiGHS MILP, with gap/status reporting | A timed-out incumbent is never labeled certified optimal. |
| Annealing and parallel tempering | Cardinality-preserving swaps and replica exchange | Costs/time budgets differ; no wall-clock advantage claim. |
| Oscillator search | Damped quartic spin heuristic | Five-seed evidence rejects reliability; not a reproduction of published simulated bifurcation. |
| Imaginary time / MPS | Dense Schmidt reference and actual quimb MPS gate splitting/sampling | The tensor-only solver is truncated and bounded; no optimum certificate. |
| QAOA depths 1..3 | Exact fixed-weight ring-XY subspace, with independent phase-flip noise | This noise channel is synthetic, not live calibrated Aer or hardware. |
| QPU-ready circuit artifact | p=1 portable QASM with independent gate-evolution test | Native routing, calibration, charge and live acquisition require the gororoba approval workflow. |
| Graph walks | Heat and coherent Laplacian evolution with probability checks | No claim that the ranking predicts board latency. |
| ARM trace acquisition | Zig M0+ compilation of the production HSX decoder, bounded Unicorn execution, CSV attribution/cache replay | Exercises production decoder code with synthetic input, not a booted BSD or physical trace. |
| Cache-aware linker selection | Training/held-out LRU/FIFO replay, bounded search, emitted selectors, actual host-link test | Current decoder trace gives no improvement; do not adopt a firmware relink without stronger evidence. |
| SwapRAM HJB/control | Finite-state discounted Bellman solver, policy export and direct solve check | Actual fragmentation/ownership/transition/cost measurements are not available here. |
| Flash-wear transport | Exact unit-capacity assignment including relocation cost | Real Dhara erase telemetry, persistent-state and power interruption tests require board access. |
| Wear/latency PDE | Conservative upwind drift-diffusion and analytic absorption control | Coefficients are supplied, not inferred from hardware. |
| Fractional latency model | Conditional Pareto/lognormal/Weibull fits and held-out comparison | No fractional PDE is adopted without measured memory/tail evidence. |
| Second-core selection | Exact SRAM budget and pair-contention subset solver | Actual core-1 measurements and firmware synchronization are not replaced by guessed costs. |
| Profile composition | Dependency/union-cost enumeration with blocks and inodes | Real filesystem rebuild remains the oracle for nonadditive allocation. |
| PQ authenticated release | Companion PR #437: fixed five-artifact manifest, real Ed25519 AND ML-DSA-65, strict UF2/hashes, rollback lock, private staging, verified delegate writes | Production keys, offline/HSM custody and release publication require owner infrastructure. No secure-boot claim. |
| Console security | Companion security branch: loopback defaults in detached/systemd launch paths, tests and opt-in LAN documentation | Actual WireGuard PSK deployment and SSH policy belong to the remote hosts, not this workspace. |
| Proof discipline / OpenAI math | Companion security branch: Lean policy, bounds and penalty proofs, Python geometry bridge and AND-to-OR rejection | Specification proofs do not verify Python/OpenSSL or import unreviewed math manuscripts as crypto authority. |

## Executed acquisition and falsification

The production `hsx_decoder.c` and its real header compile unchanged for
Cortex-M0+ using Zig 0.17.1. A freestanding harness supplies literal-encoded
streams; Unicorn 2.1.4 verifies decoded bytes and return length before traces
are accepted. Training and held-out runs execute 3,450 and 1,980 instructions.
Both show 31 cold instruction-line misses and zero warm misses in each cache
hypothesis. Reordering does not reduce them. This small code footprint gives
no justification for changing kernel layout or allocating more SRAM.

The same acquisition supports `--compiler gcc` with arm-none-eabi-gcc on
PATH. GCC 15.2.1 executes 3,341 training and 1,920 held-out instructions,
with 28 cold misses and zero warm misses in both cache models. Again no
layout improvement is found. Both compiler variants reject a corrupted-input
control before publishing a trace. This is code execution, not a BSD boot.

## Kernel build closure

The audit also installed GNU Arm GCC 15.2.1 (xPack 15.2.1-1.1), BSD make
20260912, libmd 1.1.0, libbsd 0.12.2, byacc 20260126, m4 1.4.19 and flex
2.6.4 in host-local tool directories. `logged-kernel` then compiled and linked
all four variants successfully with the production warning flags. Manpage
formatting was disabled with `MANROFF=true`; this does not change firmware.
The bmake bootstrap's own test suite had one missing-bc failure, so no claim
is made that its entire upstream suite passed. Its actual kernel invocation
completed successfully.

| Kernel | FLASH load span, excluding boot2 | Headroom in 130,816-byte region |
| --- | ---: | ---: |
| PICO | 95,988 B | 34,828 B |
| PICO_UART | 86,928 B | 43,888 B |
| PICOCALC | 111,964 B | 18,852 B |
| PICOCALC_PSRAM | 114,124 B | 16,692 B |

All four pass the existing 1,024-byte margin tool. These are worktree build
observations on the unchanged kernel sources, not a measured optimization.
The logged invocation completed in 42.3 seconds. No picotool was installed,
so the build explicitly skipped UF2 conversion; no full root/world build,
Renode boot, USB operation or physical-board measurement is claimed.

The 16-variable chain MPS experiment uses bond limit four, four imaginary
time steps and 64 samples. It stores 424 tensor elements, keeps norm one,
produces five cardinality-feasible samples and finds cut four. The exact
balanced chain optimum is one. Thus it demonstrates bounded tensor storage,
not optimization quality or advantage. The dense reference and tensor-only
solver use different Trotter/normalization schedules and are not numerically
identical implementations.

## Reproduce

```sh
python -m pip install -r tools/graph-layout-example/requirements-extended.txt
PYTHONDONTWRITEBYTECODE=1 python tools/layout_solver_extensions_test.py
PYTHONDONTWRITEBYTECODE=1 python tools/arm_trace_experiment.py --output /tmp/new-arm-trace
python tools/layout_mps.py tools/graph-layout-example/qasm.json
```

The output directory must be new. It receives ELF, sized symbol extents,
chronological traces and a source-hashed report. The dedicated CI repeats
the compiled/emulated run and retains the artifacts. These tools never open
a serial port, reset a board, change kernel layout or submit a provider job.
Firmware edits and live experiments should follow the measured deciding
capture rather than the presence of an optimizer alone.
