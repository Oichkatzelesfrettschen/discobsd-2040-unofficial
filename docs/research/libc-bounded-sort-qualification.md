# Bounded sorting qualification

Production sort and caller repair `5b823bb3` includes main `a849d47a`.
Resource/review record head `f4ec52c6` preserves those production sources.
The tracked tree remains unchanged throughout the logged ILP32 inventory;
local build products and retained logs stay ignored. Physical-board execution
remains unrun and unauthorized.

## Completed evidence

| Surface | Identity and result |
| --- | --- |
| Source review | Initial review identifies the maintained Tcl omission; follow-up review of `5b823bb3` reports no actionable findings. `libc-bounded-sort-review.md` owns scope and residuals. |
| Caller cross/QEMU | Eight real modules pass exact-type and incompatible-declaration controls; ARMv6-M executes each comparator through production qsort. |
| Compiler frames | All fourteen module hashes and the retained frame-receipt hash match `libc-bounded-sort-stack.json`. |
| Clean distribution | `libc-bounded-sort-resource-complete.json` binds completed instrumented capture to `5b823bb3`; its entire paired comparison equals the approved shipped vector. |
| Resource admission | The owner approves the original vector at `caee317e` and the additional six optional consumers at `f4ec52c6`, as recorded in `libc-bounded-sort-resource-exception.md`. |
| Logged build | Current caller/record candidate completes the maintained warning-profile build with exit zero. |
| ILP32 inventory | Record head `f4ec52c6`, rp2040-ilp32-local-20261115T024510Z-210542-676dd421, completes with exit zero in 854.238 seconds. The ordinary/context suites include the 708,596-case native, ILP32 and sanitizer matrix and calibrated work-bound controls. |
| Hosted source CI | Firmware run `43668978350` completes successfully for source repair `5b823bb3`. Final publication head still requires its own CI result. |

The immutable ILP32 status SHA-256 is `2fd8ad9066e091ca1afb0482eadce6b72f4d5274fc14c407cf2d50e8d70d3aa1`;
its complete log SHA-256 is `00361e8c1060396ee05e493a937b1b63db632ada534861f2e4c64c6ce18e2202`.
The full receipts remain under `.build-logs/bounded-sort-qualification/`.
The status/log hashes bind this result without publishing local paths from
execution receipts. Caller-selected PYTHON is the system Python 3 interpreter;
target compilation uses ARM GCC 16.2.0 and the evaluated RP2040 profiles.

## Open qualification

Current-image flash construction, Renode boot and union runs, the final full
logged-check aggregate and publication-head CI remain open. The first aggregate
run stops intentionally for caller closure and receives no pass. Its terminal
cleanup receipt and inspected recovery remain retained separately.

Compiler-local and conditional stack bounds, finite algorithm matrices,
emulator instruction execution and silicon measurements retain their own
claims. General workload timing, runtime process peaks, storage traffic and
physical-board qualification remain open. Final merge and canonical main
synchronization follow qualification and review.
