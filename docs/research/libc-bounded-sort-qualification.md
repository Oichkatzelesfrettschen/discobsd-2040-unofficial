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
| Hosted CI | Firmware run `43668978350` passes for source repair `5b823bb3`; run `43670454608` passes for admission record head `59f07e60`. Later publication heads retain their own CI requirement. |
| Image distribution/flash | Both logged targets pass at record head `59f07e60`. The sparse flash image reports 761 of 989 sectors live, including one retained free-list chain block; 228 free sectors remain unwritten. |
| UART Renode | `rp2040-sort-renode` completes with exit zero in 742.0 seconds. Boot, process/file/memory/capacity checks, MPU/residue probes, native compilation through 2046, core-1 checks and warning ceilings pass. |
| Union Renode | `rp2040-sort-renode-union` completes with exit zero in 578.01 seconds against the same root and the descriptor-patched union kernel. Its applicable boot/native/clock/fault and warning checks pass. |

The immutable ILP32 status SHA-256 is `2fd8ad9066e091ca1afb0482eadce6b72f4d5274fc14c407cf2d50e8d70d3aa1`;
its complete log SHA-256 is `00361e8c1060396ee05e493a937b1b63db632ada534861f2e4c64c6ce18e2202`.
The full receipts remain under `.build-logs/bounded-sort-qualification/`.
The status/log hashes bind this result without publishing local paths from
execution receipts. Caller-selected PYTHON is the system Python 3.14.9 interpreter;
target compilation uses ARM GCC 16.2.0 and the evaluated RP2040 profiles.

## Open qualification

The final full logged-check aggregate and final publication-head CI remain
open. The [image identities](libc-bounded-sort-image-artifacts.json) bind
seven retained inputs to the emulator runs; the five original input hashes
remain unchanged throughout those runs. The first aggregate
run stops intentionally for caller closure and receives no pass. Its terminal
cleanup receipt and inspected recovery remain retained separately.

Compiler-local and conditional stack bounds, finite algorithm matrices,
emulator instruction execution and silicon measurements retain their own
claims. General workload timing, runtime process peaks, storage traffic and
physical-board qualification remain open. Final merge and canonical main
synchronization follow qualification and review.

The emulator is Renode `1.17.0+20260907gitf1dd1b4af`, with model revision
`205a5e4b25440582008a4292074bb07f80a72328` and Peripherals.dll SHA-256
`789d85b68c88374e0c9b71df757d045985a75d59179e3b0c2b3358f34dae2032`.
The observed kernel u-area peaks/minimum-free values are 1,376/672 bytes for
UART and 1,360/688 for union, with saturation zero. These kernel/model
observations supply no physical-board or whole-process stack measurement.
Full results and image bytes remain retained before subsequent build jobs.

| Emulator gate | Immutable status SHA-256 | Complete log SHA-256 |
| --- | --- | --- |
| `rp2040-sort-renode` | `468cde2d5cb59363124810363b6926809c8c7d7e4d9d2aad86249559688b7f1e` | `fa38fff8e5f51cd9f46172ac371ef47717bde0dce5fbea898ee9b2cf28da75c9` |
| `rp2040-sort-renode-union` | `6a917985f99981ec5cacd3dbfacd3e5084c3e47fc1282c154a7b51f097d790db` | `38ae6b74f21db0157c493224a1a9c5ffaf8d809551e302a8a0c6593abe12169e` |
