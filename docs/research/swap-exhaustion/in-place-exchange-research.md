# In-place swap exchange: prior art and design constraints

The board run in `review-06.md` (revision `ce242bcd`) shows that owned
departure extents end the `sched()` stall and cost capacity. The resident
image owns a flash route of its own span, so a 100 KiB fork that origin/main
admitted is refused: the boot leaves free runs of 104, 100 and 34 blocks
and the child needs one 108-block run. This note records what the BSD trees
offer for an in-place exchange, the constraints this port's swap path
imposes, and the invariant choice the exchange forces.

## Prior art

Searches ran against the BSD_Workspace checkout (`~/Github/BSD_Workspace`).

| Tree | Command | Result |
| --- | --- | --- |
| 2.11BSD | `sed -n '/^sched()/,/^}/p' 211bsd/sys/sys/vm_sched.c` | `sched()` calls `swapin()` first and, failing core, `swapout()` of the chosen victim; `swapout()` panics `out of swap space` when `malloc3` fails. No exchange. |
| 2.11BSD | `sed -n '/^swap(/,/^}/p' 211bsd/sys/sys/vm_swp.c` | `swap()` moves one contiguous span per call through one buffer header and panics on `hard err: swap`. No partial-failure recovery. |
| RetroBSD | `sed -n '/^sched/,/^}/p' retrobsd/sys/kernel/vm_sched.c` | PIC32, one user window like this port: swap out the one loaded process, then swap in, in two separate passes. `swapout()` panics `out of swap space`. No exchange. |
| RetroBSD | `grep -n u0 retrobsd/sys/pic32/startup.S retrobsd/sys/kernel/vm_swap.c` | The loaded process's u area lives in a fixed `u0` buffer that `swtch` exchanges with `u`; `swapin()` reads the u area into `u0`. This port's `locore.S` and `swapin()` use the same model. |
| all | `grep -rniE "in.place\|exchange\|swap.?pair\|xchg"` over the 2.11BSD and RetroBSD `vm_*.c` and `kern_fork.c`, LiteBSD `vm_swap.c` and `swap_pager.c` | No match. No tree in the workspace exchanges two process images in place. |
| NetBSD chfs | `sys/ufs/chfs/chfs_gc.c` `chfs_gc_thread_should_wake()` | Relocation under erase-before-write is driven by free-plus-erasable block counts against a reserve (`chm_resv_blocks_gctrigger`). That is the closest analog: a fixed reserve that makes relocation always possible. |
| FreeBSD | `sys/kern/subr_blist.c` header | The swap allocator keeps per-subtree hints bounding the largest allocation in each range, so a largest-run query costs a tree walk instead of a map scan. Relevant to route placement, not to the exchange itself. |

The absence of an in-place exchange in every tree is the finding: the
historical systems either had swap space to spare or panicked.

## Constraints in this port

- **Layout.** `swapin()` places data at `__user_data_start`, the stack
  at `USER_TOP(p) - p_ssize` and the u area in `u0`. The flash image is
  laid out contiguously as data | stack | u area. Sector k of an outgoing
  image and sector k of an incoming image therefore map to the same window
  bytes only inside the data segment, and only when both data segments start
  at the same sector. At every segment boundary where the sizes differ,
  writing an incoming sector into the window overwrites outgoing bytes that
  are not yet saved. The exchange must either lay each segment out on its
  own sector-aligned extent (at most 3 extra blocks per segment) or follow a
  computed save-before-overwrite order.
- **u area.** The outgoing u area (3 blocks, smaller than one 4 KiB sector)
  must leave `u0` before the incoming u area enters it, and `sched()` runs on
  process 0's stack outside `u0`.
- **Erase granularity.** `SWAP_IMAGE_ALIGN` is one 4 KiB erase sector
  (`flash.c` asserts it). `flash_swap_rewrite()` already rewrites through a
  fixed spare sector with a 256-byte page scratch, but it costs two erases
  per sector, one of them on the same spare every time. A sliding hole costs
  one erase and one program per sector, the same as a plain `swapout()`.
  It starts with one erased sector beside the incoming extent; for each
  sector the outgoing data is programmed into the hole, the incoming
  sector is read into the window, and the vacated sector is erased to
  become the next hole. Wear then spreads over the extent.
- **Faults.** An erase or program that fails partway through leaves
  neither image whole. The flash program path panics today (`hard err:
  swap` in 2.11BSD; `swap_image_copy_out` cannot fail after reservation
  here), so the exchange inherits that policy unless a rollback is designed.

## The invariant choice

The exchange recovers capacity only if the resident stops owning a route.

- **Resident keeps its route; the exchange is a fallback.** Every eviction
  stays guaranteed, and the board capacity in `review-06.md` does not
  change. The exchange gains nothing on hardware.
- **Resident owns nothing; each swapped image owns its extent.** An
  eviction places the outgoing image into the incoming image's extent,
  plus a tail allocated at eviction time when the outgoing image is larger.
  A fork pair has identical geometry and needs no tail, so the fork case
  recovers origin/main's capacity. A large resident that must leave so a
  smaller image can enter needs a tail of `span(out) - span(in)` blocks;
  with the map full, the scheduler refuses, the resident stays loaded and
  the incoming image waits. That is the stall class owned extents removed,
  narrowed to an unequal pair with no free tail.
- **Resident owns only a shared reserve.** One reserve of the largest
  live span difference serves every tail, in the manner of chfs's
  relocation reserve. It keeps evictions guaranteed at a cost below a full
  route, but the bound rises toward a full route whenever a small image is
  swapped out beside a large resident.

The owner chose the second invariant: the resident owns nothing, each
swapped image owns its extent, and an eviction exchanges into the incoming
extent plus an eviction-time tail, refusing when no tail fits.
