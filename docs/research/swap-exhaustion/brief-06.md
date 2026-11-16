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

- Work only in the `PICO-TO-DOS` worktree. Git metadata is read-only in the sandbox: use
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

## Shared worktree (added by the orchestrator)

The owner set `PICO-TO-DOS` as the one worktree and branch for this work. A
second agent works in the same tree on board bring-up. Edit only the files
this brief's work needs, leave every other modified or untracked file as you
find it, and run no `git checkout`, `restore`, `reset`, `stash` or `clean`.
Write commit requests under `.commit-requests/headroom/` instead of
`.commit-requests/`.

## Handoff state (added by the orchestrator)

The session that started this brief was interrupted when the work moved from
its own worktree to `PICO-TO-DOS`. Its uncommitted edits were carried over
unchanged: `sys/kern/vm_swap.c`, `vm_sched.c`, `exec_subr.c`, `kern_exit.c`,
`kern_mman.c`, `init_main.c`, `sys/sys/vmsystm.h`, `sys/dev/swap.c`,
`sys/arch/rp2040/rp2040/{swapram,syscall,sig_machdep}.c`,
`tests/kernel/{fork_swap_test.c,fork_swap_mutate.py,swap_ioctl_test.c,swap_ioctl_extract.py}`
and `review-06.md`. Read `review-06.md` and `git diff` of those paths, then
continue from the first work item they leave incomplete.
Shared files (the root `Makefile`, `tests/kernel/Makefile`, CI workflows,
`TESTING.md`) also receive edits from a board-descriptor session: re-read one
immediately before each edit and change only the lines your work adds.

## Board measurement (added by the orchestrator)

On a bare Pico running the origin/main `eb5b3081` PICO kernel, `forktest`
built with `BSSBYTES` of 172 KiB (text 5,771, data 200, bss 176,172 bytes;
an image of about 184 blocks) printed no FORKTEST line within 120 s while
other boot processes held their own swap. A console interrupt returned the
root shell, the process was gone from `ps`, and there was no panic. The
reading is that fork reserved the child, and then `sched()` refused every
eviction of the resident parent for lack of a second 184-block run, so the
child never ran and the parent waited forever. That inference is moderate:
storage counters were not captured. Admission must turn this case into a
prompt `ENOMEM` from fork, or the in-place exchange must make it alternate.

## Orchestrator review findings (fix before READY)

1. SERIOUS -- scheduler evictions no longer use the SwapRAM pool.
   `swap_exchange_switch()` (`sys/kern/vm_swap.c`) always builds a
   `SWAP_IMAGE_FLASH` reservation for the outgoing resident, so the 16 KiB
   compressed pool now serves only fork children. Before this series,
   `sched()` reached `swap_image_reserve()`, which tried `swapram_reserve()`
   first. Every switch now costs a full flash write of the resident image (the
   erase and program count of its span), even when it compresses into the
   pool. The invariant does not need that: when the outgoing resident is
   admitted to the pool, move its owned departure extent into its pool record
   (the same route field fork children use) instead of writing through it.
   It keeps its route for the return trip, so a pool return still needs no
   new allocation. Fall back to the flash route only when pool admission
   refuses. Add a gate case where a compressible resident leaves through the
   pool with zero media writes and its route stays off the free map, plus a
   mutant that always takes the flash route, and confirm it is rejected.
   Record the before and after behavior in `review-06.md`.
2. MINOR -- `swap_exchange_resize()` allocates the new span before it frees
   the old one, so growth needs `required` free blocks on top of the existing
   route. Growth that fits by extending the existing extent in place is
   refused. Either accept that and document it in `review-06.md` as a
   deliberate cost with its block arithmetic, or try the in-place extension
   first under the same spl region.
