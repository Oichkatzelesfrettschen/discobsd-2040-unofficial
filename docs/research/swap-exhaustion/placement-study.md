# Raw-swap placement study

A next-fit search that keeps one free run of 196 blocks removes the
fragmentation refusals of the 172 KiB fork in the modeled shell session, at no
flash write and no kernel RAM beyond one constant. Relocating one swapped
image when a refusal would otherwise occur covers the remainder for about one
rewritten block per thousand commands. Both results come from a host model;
neither policy is in the kernel, and the board has not run either.

`review-07.md` section 10 measured the defect this study starts from: the
fork of the 172 KiB image needs a 184-block extent, and one 20-block image of
a sleeping shell in the middle of the 380-block map can leave two free runs
that are each too short.

## Model

`placement/placement_sim.c` drives a session over the map `init_main.c`
builds, blocks 4 to 383 with `SWAP_IMAGE_ALIGN` 4, so one aligned unit is one
4 KiB erase sector and the map holds 95 of them.

- **Boot cursor.** Uniform over the 95 aligned positions, as
  `swap_cursor_init()` allows.
- **Sleepers.** One 20-block image (the login shell), or three images of 20,
  12 and 8 blocks (login shell, `init`, `update`, the geometries
  `review-07.md` section 6 measured). A sleeper is swapped out once and stays.
- **Command.** One or two 20-block extents are taken at the cursor. The last
  one, the invoking shell asleep in `wait`, is held until the command ends;
  an earlier one is released at once and stands for the fork copy or the exec
  spool.
- **Fork.** 45 percent of commands fork an image of their own: 68 blocks for
  20 percent of commands, 108 for 15 percent, 184 for 10 percent. The extent
  is held until the command ends.
- **Late sleepers.** In the late variant each sleeper goes out during a
  different early command, while that command's extents are held.

The model takes its extent sizes from board measurements and its event order
from reading `kern_fork.c` and `vm_swap.c`. It leaves out the SwapRAM pool,
which takes images away from flash and so lowers pressure, and the in-place
exchange, which reuses an incoming extent. The board capture in section 10
does not record the commands between its four map readings, so the model is
not fitted to that trace.

The next-fit policy is the kernel's own: the Makefile links
`sys/kern/subr_rmap.c`. The guarded search with a zero guard must choose the
production placement, and the program counts every allocation where it does
not. The count is 0 over 2000 sessions of 400 commands in each of the eight
scenarios.

## Policies

| Name | Rule |
| --- | --- |
| `next-fit` | `malloc3_contiguous_next()`: first fitting position at or after the cursor, wrapping once. |
| `guarded` | Cursor order, but the first placement that leaves a free run of `min(196, largest run)` blocks wins. Inside a run the cursor position is tried, then the lowest position that leaves the target below it, then the run's high boundary. When every placement breaks the target, the one that leaves the largest run wins. |
| `strict` | `guarded` with the target fixed at 196. |
| `boundary` | The cursor picks the run; the extent takes the run's low boundary. |
| `best-fit` | Smallest fitting run, low boundary, no cursor. |
| `next+move`, `guard+move` | The base policy, then relocation on refusal: while no run holds the request and enough blocks are free, the smallest held image moves to the placement that leaves the largest run. |

196 blocks is the largest image the port can produce: the 192 KiB window plus
`USIZE` rounded to the alignment.

## Result

`bmake run` in `placement/`, 2000 sessions of 400 commands, source
`sys/kern/subr_rmap.c` at `88b67724`. `ref184` is the share of 184-block
requests refused while at least 184 blocks were free. `wear max` is the
hottest sector's erase count over the mean. `moved` is blocks rewritten by
relocation per 1000 commands. The table shows one transient per command and
early sleepers; the other six scenarios differ by a few percent and
`placement_sim` prints them.

| Sleepers | Policy | ref108 | ref184 | wear max | moved |
| --- | --- | --- | --- | --- | --- |
| 1 | `next-fit` | 0.00% | 31.81% | 1.30 | 0 |
| 1 | `guarded` | 0.00% | 0.00% | 1.80 | 0 |
| 1 | `boundary` | 0.00% | 0.00% | 3.72 | 0 |
| 1 | `best-fit` | 0.00% | 0.00% | 5.58 | 0 |
| 1 | `next+move` | 0.00% | 0.00% | 1.37 | 552.2 |
| 1 | `guard+move` | 0.00% | 0.00% | 1.80 | 0 |
| 3 | `next-fit` | 4.53% | 71.51% | 1.54 | 0 |
| 3 | `guarded` | 0.00% | 0.64% | 2.10 | 0 |
| 3 | `boundary` | 0.00% | 10.19% | 3.40 | 0 |
| 3 | `best-fit` | 0.00% | 0.00% | 5.58 | 0 |
| 3 | `next+move` | 0.00% | 1.56% | 1.36 | 876.8 |
| 3 | `guard+move` | 0.00% | 0.00% | 2.10 | 0.7 |

No policy refused a 68-block request. `strict` matched `guarded` in every
cell.

## Conjectures

Each conjecture names the result that would refute it and what the model
returned.

1. **The cursor, not the sleeper, is the controllable cause.** Refuted if
   next-fit refuses at the same rate with the transients removed. The model
   cannot remove them, but it bounds the claim from the other side: with the
   sleeper fixed, changing only where the 20-block transients land takes the
   refusal rate from 31.81 percent to 0. Held.
2. **A placement rule alone, with no lifetime knowledge, is enough for one
   sleeper.** Refuted by any refusal under `guarded` with one sleeper. None
   in 2000 sessions in four scenarios. Held.
3. **A placement rule alone is enough for three sleepers.** Refuted: 0.64 to
   1.69 percent of 184-block requests are refused. In the dumped cases the
   three sleepers went out while a command's extents were held and landed at
   128, 168 and 288; no free run above 124 blocks remains once the command
   ends, and no later placement can repair that.
4. **Packing every extent at a run boundary beats the guard.** Refuted:
   `boundary` refuses up to 10.19 percent with three sleepers and its hottest
   sector takes 3.4 to 6.7 times the mean.
5. **Best-fit is the cheapest complete answer.** Refuted on wear: it refuses
   nothing, and its hottest sector takes 5.6 to 8.6 times the mean, because
   every short image returns to the same low sectors. The cursor exists to
   prevent that.
6. **Relocation alone, without a placement rule, is cheap.** Refuted: under
   next-fit it rewrites 550 to 1030 blocks per 1000 commands, because the
   cursor re-creates the split after every repair.
7. **Relocation after the guard is nearly free.** Refuted if `guard+move`
   rewrites more than a few blocks per 1000 commands or still refuses. It
   rewrites 0.5 to 1.2 and refuses nothing. Held.
8. **Fixing the guard target at 196 improves on `min(196, largest run)`.**
   Refuted: identical placements in every scenario.
9. **Alignment finer or coarser than 4 blocks helps.** Not run. The alignment
   is the erase sector; a finer one makes two images share a sector and adds
   erase-before-write coupling, and a coarser one wastes blocks in a map that
   has 380.
10. **Keeping sleepers in the SwapRAM pool removes the problem.** Not run.
    It holds by construction, since a pool image owns no flash extent, and
    costs pool RAM for as long as the sleeper lives, which is the opposite of
    the RAM goal.

## Cost

- **Flash writes.** `guarded` writes exactly the blocks next-fit writes for
  the same admitted work. The higher `kB/cmd` the program prints for it is
  the 184-block forks next-fit refuses.
- **Wear.** The guard confines short images to the part of the map outside
  the reserved run, so the hottest sector takes 1.7 to 2.2 times the mean
  against 1.25 to 1.58. At 68 blocks written per command over 95 sectors the
  mean sector is erased 0.18 times per command, the hottest 0.4 times.
- **RAM.** One constant and no state. The search reads the map twice.
- **Relocation.** One flash-to-flash copy of an image that is swapped out,
  through a bounce buffer, with the owner's `p_daddr`, `p_saddr` and `p_addr`
  rewritten. `swap_exchange_inplace()` already moves an image under the same
  constraints.

## Recommendation

Put the guard into `malloc3_contiguous_next()` as a parameter, 0 for the exec
spool and 196 for images, with the host gate asserting that a zero guard
keeps today's placements. Add relocation on refusal as a second change only
if the board shows the three-sleeper case.

The board experiment that decides it, on the bare Pico with a control kernel
from the same tree: run the 172 KiB fork test N times from one login, with
`swapmaptest` before each run.

- The model predicts a refusal in about one run in three on the control
  kernel while one 20-block sleeper is on flash, and none on the guarded
  kernel.
- A refusal on the guarded kernel with a single sleeper refutes the model's
  event order; the map printed before that run says which extent the model
  left out.
- A control kernel that never refuses means the pool held the sleeper for the
  whole session, and the run decides nothing.
