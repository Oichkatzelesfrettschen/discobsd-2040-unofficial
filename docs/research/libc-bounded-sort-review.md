# Bounded sorting source review

The owner authorizes a read-only OpenAI Codex review of the bounded-sort
branch on 2026-11-15 UTC. The recorded CLI is 0.169.0, model gpt-6.1-sol,
medium effort and standard tier, with a read-only sandbox. The immutable
range is main `a849d47a` through `555e7530`. The reviewer runs no builds,
tests, mutations, network services or hardware operations.

## Review finding

The sort mechanism, valid-input arithmetic, callback lifetimes, caller repairs, mutation oracles and native divider witness reveal no additional actionable defect; provider/caller hashes match the stack receipt, but maintained-consumer resource admission omits tclsh. This source-only review runs no builds or tests and leaves aggregate execution, workload timing, runtime peaks, physical-board qualification and shared XIP rollout unresolved. Set aside are unstable equal-key ordering (permitted), invalid arrays or inconsistent comparators (outside the input contract), pre-existing legacy defects and unreviewed denominator leaves (outside this bounded change), and full POSIX conformance (not claimed).

Review comment:

- [P2] Include tclsh in the bounded-sort resource admission — docs/research/libc-bounded-sort-resource-exception.md:51-57
  For the maintained `tclsh` build, the affected-consumer inventory omits a reachable qsort user: `usr.bin/tclsh/tclsh.c` calls `Tcl_CreateInterp`, whose command table in `lib/libtcl/tclbasic.c` registers `Tcl_LsortCmd`; that function calls qsort at `lib/libtcl/tclcmdil.c:1140`. Both libtcl and tclsh participate in the maintained build, and tclsh links `-ltcl`. Consequently, these three optional-program measurements do not cover the complete maintained consumer closure, and the exact-vector approval supplies no resource admission for tclsh. Add its matched resource measurements and callback/stack assessment, then obtain approval for any additional positive deltas rather than extending the existing exception implicitly.

## Parent verification and closure

Source inspection confirms the Tcl registration/call chain and maintained
build selection. A wider source and final-disassembly sweep identifies
standalone ls and four maintained game executables in the same omission
class. The complete target inventory is twelve executables, with explicit
host-only, fixture-only, commented-registration and unselected-csh exclusions
in the [contract record](libc-bounded-sort-contracts.md).

The parent adds exact callback types in atc, boggle and wump, then calibrates
real-module type and ARMv6-M behavior witnesses for the added games and Tcl.
The expanded compiler frame receipt includes Tcl_LsortCmd, its comparator,
tclsh main and each game module. The atc log_score frame is 56,496 bytes;
that inherited local allocation contributes separately to its process budget.
The callback gate establishes ordering on the exercised game-domain inputs,
not complete game or interpreter correctness. Tcl/robots retain their
compatible existing callback implementation.

The resource review retains the original approved vector and enumerates each
additional maintained optional executable. Their new positive costs require a
separate measured amendment. The first aggregate run stops intentionally for
this caller closure; its cleanup receipt requires the documented recovery tool,
which releases ownership after host inspection finds no surviving groups.
That interrupted run supplies no aggregate pass. Follow-up review and full
qualification remain open.
