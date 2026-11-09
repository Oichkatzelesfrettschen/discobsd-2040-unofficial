# Brief: resolve `panic("out of swap space")` on the RP2040 port

Input for a read-only audit. The audit writes its report to
`docs/research/swap-exhaustion/audit-01.md` in this worktree.

## Observation

Board: bare Pico, PICO kernel from the `mpu/large-bonus-region` branch
(the MPU bonus region for the SwapRAM LARGE epoch, stacked on the fault-signal
fix of PR #398). Root shell as root ran `/usr/bin/bigtest`
(`tests/rp2040/swapram_large/bigtest.c`, bss 150,000 bytes, which runs
under the LARGE epoch). Its in-process MPU probes passed (`MPU OK`), and then
its single `fork()` printed

    swapout: pid 24 dsize 159744 ssize 2244, free: 120@4 4@144 28@324 12@372
    panic: out of swap space

Units are DEV_BSIZE = 1024-byte blocks; raw swap is 384 blocks; SWAP_IMAGE_ALIGN
is 4 blocks (4096 bytes, the QSPI erase sector). The child image needs
btod(159744 - tsize) + btod(2244) + btod(3072) blocks rounded to 4, about
164, in one aligned run; the largest free run is 120 of 164 free in total.
Other processes resident at the time: init, the pid-10 daemon, the operator
`-sh`, `su`, the root shell. Under LARGE the SwapRAM RAM pool is closed and
its images were evacuated to flash swap (`swapram_evacuate`).

## Mechanism as read

- `sys/kern/kern_fork.c` `newproc()` makes the child's copy by
  `swapout(child, X_DONTFREE, X_OLDSIZE, X_OLDSIZE)` after the child proc slot,
  credentials, `p_tip` reference and `swapram_inherit()` are already set up,
  and after `setjmp(&u.u_ssave)`. It has no failure return.
- `sys/kern/vm_swap.c` `swapout()` tries the SwapRAM tier (`swapram_out`),
  then `malloc3_contiguous_next(swapmap, ..., SWAP_IMAGE_ALIGN, &swapnext, a)`
  (`sys/kern/subr_rmap.c`), a next-fit cursor allocator that places the whole
  image in one aligned run; on failure it prints the map and panics.
- `sys/kern/vm_sched.c` `sched()` calls the same `swapout(..., X_FREECORE, ...)`
  to make room for a swap-in; it panics the same way when swap is short.
- Other swap consumers: `swapram_evacuate()` (`sys/arch/rp2040/rp2040/swapram.c`,
  which already fails cleanly and unwinds), the exec spool
  (`sys/kern/exec_subr.c`), and temporary swap (`TFALLOC`, `sys/dev/swap.c`).

## Candidate resolutions to evaluate (not exhaustive)

1. Make fork fail: reserve the child's flash extent (or SwapRAM reservation)
   in `newproc()` before any irreversible step, hand it to the copy, and
   return ENOMEM or EAGAIN from fork with the child slot, `p_tip` reference
   and `swapram_inherit` state unwound. POSIX lists EAGAIN and ENOMEM for fork.
2. Make `swapout()` able to fail and have each caller handle it: `newproc()`
   unwinds; `sched()` leaves the victim in core and picks another victim or
   sleeps on a swap-freed event instead of panicking.
3. Reduce fragmentation: change the allocation policy (best-fit for large
   images, or a separate region for large images), or split one image across
   several aligned extents (the deferred "three-extent" design noted in
   `docs/research/memory-ownership-plan.md`).
4. Compact: relocate swapped images to coalesce free runs when a request
   fails but total free space suffices (cost: flash writes, erase wear, time
   at the swapper).
5. Admission control: when a process enters the LARGE epoch (exec), require
   or reserve swap headroom for its fork copy; or refuse fork of a LARGE
   process when no run fits.
6. Change the copy mechanism for LARGE: copy the parent through the SwapRAM
   pool, or swap the parent out and let the child take the in-core image.

## What the audit must produce

Analyze along each of these dimensions, citing file:line for every claim:

- Correctness: every swap consumer and every path that can reach the panic,
  including `sched()`, exec spool, TFALLOC, and SwapRAM evacuate; races
  between a reservation and the swapper; unwind completeness in `newproc()`.
- Semantics: which errno fork should return, what 2.11BSD, 4.4BSD and
  RetroBSD do on the same shortage, and what callers (sh, init, getty) do
  with a failed fork.
- Capacity: whether 384 blocks can hold a LARGE image's fork copy plus the
  evacuated images at all, and how often the next-fit cursor and the 4-block
  alignment produce this layout; account for every used run in the free map
  above if the source allows it.
- Flash cost: erase-sector alignment, wear and write time of each option.
- Security: an unprivileged process forcing the panic (denial of service)
  through fork, exec spool, TFALLOC or stack growth.
- Complexity and testability: the host test harness (`tests/kernel`,
  `check-kernel`, the swap allocator fixtures) and which mutants would prove
  each guard; a board reproduction.

Then rank the candidates, recommend one design (or a combination) with the
exact functions to change, the invariant it establishes, the gates and
mutants that prove it, and residual risks. Separate what the source shows
from what is inferred.

## Rules

Read-only on source: write only `docs/research/swap-exhaustion/audit-01.md`.
No network, no board, no git commits, no pushes.
