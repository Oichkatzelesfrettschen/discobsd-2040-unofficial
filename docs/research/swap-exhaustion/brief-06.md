# Brief 06: exchange-headroom admission for the fixed 192 KiB window

Re-read this file at every work-item boundary and after every compaction. It
is the goal file; the orchestrator steers by editing it.

## Problem

`docs/research/swap-exhaustion/plan.md` item 6 asks for one shared
admission rule so that a runnable swapped image always leaves a route for the
resident image to leave the process window. The tree runs one process window
(`USER_DATA_SIZE`, 192 KiB at `0x20000000..0x20030000`), so every switch in
`sched()` (`sys/kern/vm_sched.c`) evicts the resident process before the
incoming one is read back. The eviction needs one free aligned run of
`swap_image_span(resident)` blocks (`sys/kern/vm_swap.c`), or a SwapRAM pool
admission (`swapram_reserve`, `sys/arch/rp2040/rp2040/swapram.c`).

Facts to confirm from source before relying on them:

- Flash swap holds 380 allocatable 1-KiB blocks handed out as
  `SWAP_IMAGE_ALIGN`-aligned contiguous runs by a next-fit cursor
  (`malloc3_contiguous_next`, `sys/kern/subr_rmap.c`;
  `sys/arch/rp2040/dev/flash.h`).
- A flash-tier swap-in calls `swap_image_release()`, which frees exactly
  `swap_image_span(incoming)` as one aligned run. That run is the incoming
  process's own future route.
- A SwapRAM-tier swap-in (`swapram_present()` branch of `swapin()`) frees no
  flash. A process that returns from the pool after the resident took the last
  flash route can be resident with no route out; if the pool is also full, the
  swapper refuses forever and a parent waiting on another process hangs.
- `sched()` already refuses a failed reservation: it restores `SLOAD`,
  requeues an `SRUN` victim, and sleeps on `runin`.
- Flash consumers other than the swapper: fork (`newproc()`/`fork1()`,
  `sys/kern/kern_fork.c`), exec spool (`sys/kern/exec_subr.c`), `TFALLOC`
  (`sys/dev/swap.c`), SwapRAM evacuation (`swapram.c`), and growth of data or
  stack, which enlarges the resident's span (`sys/kern/kern_mman.c`,
  `sys/arch/rp2040/rp2040/syscall.c`, `sys/arch/rp2040/rp2040/sig_machdep.c`).

Prior design text: `review-02.md` sections "Rank 5" and "Commit 6" (written
for the retired 144 KiB LARGE layout; its 216/156/528 numbers do not apply)
and `audit-01.md`, which rejects check-then-allocate because swap I/O can
sleep between the check and the allocation.

## Work, in order

1. **Decompose.** Write `review-06.md` section "Options". Develop at least
   these three mechanisms, plus any better one you find, at the granularity of
   functions, state fields and spl regions:
   - Owned departure extent: the resident process owns a reserved flash
     extent of its span; swapout consumes it; a flash swap-in inherits its own
     freed extent; a pool swap-in reserves one before the resident is evicted
     or is refused; growth extends the extent or fails; other consumers cannot
     take it.
   - Computed largest-run check: each flash grant allocates, then verifies a
     remaining aligned free run of at least the resident span inside one spl
     region, and rolls back on failure; `sched()` pre-checks pool swap-ins.
   - Route sized for the largest live image.
2. **Falsify.** For each option state the invariant, then search for a
   counterexample against the production source: an interleaving across a
   sleeping swap I/O, a pool swap-in, an exit, a `TFALLOC` hold, a growth
   step, alignment and fragmentation, process 1, and `SWAPRAM` off (PICO_UART).
   Record each option's surviving and refuted claims with file:line evidence,
   its RAM, text and flash cost, and how many currently admitted workloads it
   would refuse.
3. **Synthesize.** Choose the design that keeps the strongest surviving part of
   each option. Record the choice, the rejected alternatives and why, and the
   falsifier that would reopen the choice.
4. **Build.** Implement it on this branch in commits that each build and
   bisect. Add the `check-swap-exchange-headroom` host gate that links the
   production code (follow the pattern of `tests/kernel/fork_swap_test.c`,
   `fork_swap_extract.py` and `fork_swap_mutate.py`). Write the
   `count-free-not-largest-run` mutant first. Add one mutant per consumer that
   skips the rule, plus `release-headroom-early` and a pool-swap-in mutant.
   Calibrate: the gate passes on the branch and rejects every mutant. Register
   the gate in the root `Makefile` and `tests/kernel/Makefile` aggregate, in
   CI where `check-kernel` already runs, and in
   `sys/arch/rp2040/doc/TESTING.md`.
5. **Existing fixtures.** Admission makes fork need room for the child image
   and the route. Run `bmake MACHINE=rp2040 check-kernel` right after wiring
   the fork caller; when an existing fixture flips, decide from the invariant
   whether the fixture or the code is wrong and record the decision in
   `review-06.md`. Never relax an assertion to pass.
6. **Board prediction.** Add a section "Board prediction" naming the exact
   observable a bare Pico run would show (a refusal with `ENOMEM` from fork or
   talloc where the route would be consumed; no panic; the pool-return case).
   The orchestrator runs the board.

## Acceptance (each exits 0 with zero warnings)

    : "${PYTHON:?}"; export PYTHON
    bmake MACHINE=rp2040 logged-build
    bmake MACHINE=rp2040 check-kernel
    bmake MACHINE=rp2040 check-swap-exchange-headroom
    bmake MACHINE=rp2040 check-swapram-evac
    bmake MACHINE=rp2040 check-kernel-headroom
    shellcheck -S error on every changed shell script; ruff check and
    ruff format --check on every changed Python file

## Standing rules

- Work only in this worktree. Git metadata is read-only in the sandbox: use
  the commit protocol below and never retry a Git write.
- No network, no package installs. Never open `/dev/ttyACM*` or
  `/dev/serial/*`, and never run `discobsd-flash` or a `picotool` command
  that talks to a device.
- C builds with `-Wall -Wextra -Werror`; shell is POSIX `sh` with `set -eu`;
  Python runs as `${PYTHON}`.
- Checked-in text is emoji-free American English with `--` for dashes. Source
  comments state the invariant, not the history. No copyright line on a new
  file outside `sys/arch/rp2040`; a new file under `sys/arch/rp2040` carries
  `Copyright (c) 2026 DiscoBSD` and the ISC notice.
- Commit subjects use a component prefix and the mechanism (`swap: ...`),
  bodies one to five sentences: invariant, change, one sentence of evidence.
  No AI attribution trailer or line of any kind.
- Keep secrets, local absolute paths and private hostnames out of every file.

## Commit protocol

For each commit, in series order, write `.commit-requests/NN.paths` (one
repository-relative path per line) and `.commit-requests/NN.msg` (subject,
blank line, body), NN from 01. A path belongs to one request. When the series
is written, write `.commit-requests/READY`. The orchestrator commits in order
and writes each hash to `.commit-requests/NN.hash`. `.commit-requests/` is
excluded from Git.

## Done

`review-06.md` holds Options, Falsification, Synthesis, gate results with
counts, Board prediction, and a Handoff section of at most fifteen lines; every
change is in a commit request; acceptance passes.
