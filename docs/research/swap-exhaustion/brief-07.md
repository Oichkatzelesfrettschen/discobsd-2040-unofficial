# Brief: in-place swap exchange with a routeless resident

Re-read this file at every item boundary and after every compaction.

## Objective

Restore origin/main's fork capacity while keeping the liveness that
`ce242bcd` gained. The board run in `review-06.md` (section "Board
measurement of owned departure extents") refuses a 100 KiB fork that main
admitted, because the resident owns a flash route of its own span. The
owner chose the invariant: **the resident owns no flash route; every
swapped image owns its extent; an eviction exchanges the outgoing image into
the incoming image's extent plus a tail allocated at eviction time, and
refuses (resident stays loaded, `sched()` sleeps on `runin`) when no tail
fits.** Read `in-place-exchange-research.md` first: it holds the prior art,
the layout, u-area, erase and fault constraints, and the invariant record.

## Items, in order

1. **Enumerate eviction paths (falsifier first).** List every path that
   moves a resident image to flash or the SwapRAM pool: `sched()`, fork
   child creation, exec spool, `TFALLOC`, SwapRAM evacuation, growth hooks,
   exit. For each, state which extent it consumes under the new invariant.
   The invariant is refuted if any path needs a destination it cannot get
   from an incoming extent, a tail, or the fork child's own reservation.
   Record the table in `review-07.md` before editing code. If it is
   refuted, stop and write the counterexample there.
2. **Choose the window layout.** Compare per-segment sector-aligned
   extents (data, stack, u area each start on a sector; cost up to 3 extra
   blocks per segment) against the contiguous layout with a computed
   save-before-overwrite order. Record the block cost and the ordering
   proof in `review-07.md`, then implement the choice.
3. **Remove the resident route.** Revert the resident-route parts of
   `ce242bcd` that the invariant no longer needs: the `swap_exchange`
   departure record, `swap_exchange_resize()` and its callers in
   `exec_subr.c`, `init_main.c`, `kern_mman.c`, `syscall.c` and
   `sig_machdep.c`, and the flash transfer in `swap_image_release()`. Keep
   fork's reservation of the child's extent, the scheduler refusal path and
   the SwapRAM pool tier.
4. **Implement the exchange in `sched()`.** Orchestrator decision,
   replacing the sliding hole (`review-07.md` section 2 shows the allocator
   guarantees no adjacent erased sector): use direct in-place sector
   exchange through one static 4 KiB (`FLASH_SECTOR_BYTES`) kernel SRAM
   buffer. The kernel RAM region has 26.9 KiB free above the SwapRAM pool
   (data, ramfunc, bss and pool end at 0x20037ee0; `U0AREA` starts at
   0x2003e800). Per sector k of the incoming extent: read incoming sector k
   into the buffer, erase sector k, program the outgoing bytes for sector k
   from the window (or u0), then copy the buffer into the window position
   the incoming image's sector k occupies, following the save-before-
   overwrite order proven in `review-07.md` section 2. One erase and one
   program per sector, no adjacency, wear spread over the extent. The tail
   sectors (outgoing larger) are plain erase-and-program. Save the outgoing
   u area before the incoming u area enters `u0`. Keep `flash_swap_rewrite()`
   and its spare sector untouched.
   Allocate the tail before any media write; on tail refusal, change
   nothing and take the existing refusal path. A pool-backed incoming
   image needs no exchange; keep its path.
5. **Fault policy and gate.** An erase or program failure mid-exchange
   panics `swap exchange: media fault` (neither image is whole). Add a host
   gate `check-swap-exchange-inplace` that links the production exchange
   with a modeled NOR device (erase sets 0xff, program only clears bits,
   program over unerased bytes is a detected fault) and checks:
   - every data/stack size pair over a grid that includes the incoming
     data reaching into the outgoing stack region, both images restored
     byte for byte after a round trip;
   - fork pairs need zero tail blocks;
   - an unequal pair with no free tail refuses with zero media writes;
   - injection at every sector index on erase and on program reaches the
     panic, never a silent corruption;
   - erase count per exchange equals the sector count.
   Calibrate with at least four mutants that must fail at named
   assertions: skip the sector erase, write the u area after reading the
   incoming one, allocate the tail after the first write, ignore a program
   fault, and copy the buffer into the window before the outgoing sector is
   programmed.
6. **Replay the board map.** Extend the host fixture to replay the boot's
   allocation order (init, update, login shell, su, then the test image)
   through the production allocator, and predict fb60, fb100 and forkbig
   (60, 100, 172 KiB bss `forktest` builds) under the new invariant. Record
   the predictions in `review-07.md`. If fb100 stays refused, say so with
   the free-run addresses; do not add a placement policy without an
   owner decision.
7. **Update the existing gates.** `check-swap-exchange-headroom`,
   `check-fork-swap-refusal`, `check-swapram-evac` and `check-kernel` must
   pass; retire or rewrite assertions that encode the resident route, and
   say which in `review-07.md`.

## Standing rules

- Work only in this worktree. Another session owns the product lock and is
  editing `share/mk/sys.mk`, `sys/arch/rp2040/conf/Makefile.rp2040`, the four
  `compile/*/Makefile`, the root `Makefile`, `sys/arch/rp2040/doc/TESTING.md`,
  `tools/aoututils/Makefile.inc` and `tools/check_debug_paths.py`. Do not
  edit those files and do not run any `logged-*` target. Register the new
  gate in `tests/kernel/Makefile` only; write the root Makefile and
  TESTING.md hunks as text in `review-07.md` for the orchestrator.
- Run host gates directly (`bmake -C tests/kernel TARGET`, `bmake
  MACHINE=rp2040 check-kernel` is allowed). Cross-compile the PICO kernel
  objects you touch with `-Wall -Wextra -Werror` to prove they build.
- No board access, no network, no flashing, no `git push`.
- Compile warnings are errors; Python passes `ruff`.
- Source comments state invariants, not history.
- Commit through requests: write `.commit-requests/exchange/NN.msg` (subject
  with component prefix, one-to-five sentence body, no attribution) and
  `NN.paths` (one path per line). Write `.commit-requests/exchange/READY`
  when every item is done and every gate passes.

## Done criterion

All seven items recorded in `review-07.md`, every gate in item 7 plus the
new gate passing, commit requests and READY written.
