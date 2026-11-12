# Raw-swap wear: placement on a ring

Joining the two ends of the swap map into a ring, and keeping the guarded
next-fit search of `placement-study.md` on it, removes the fragmentation
refusals of the 172 KiB fork and lowers the wear a flash part accumulates
below what the kernel's next-fit produces. In the host model the hottest
sector takes 1.01 to 1.04 times the mean erases over 2000 sessions, against
1.13 to 1.24 for the kernel's next-fit and 1.36 to 1.51 for the guard on the
interval. The RP2040 kernel has the ring, and on the bare Pico it admits the
172 KiB fork under free maps where the interval kernel refuses it; the wear
figures remain model results.

## Why the guard alone costs wear

`placement-study.md` measured wear inside one session. A flash sector wears by
the erases of every session added together, so `placement/ring_sim.c` reports
both: the mean over sessions of each session's hottest sector against that
session's mean (`wear/sess`), and the same ratio over the erases of all
sessions summed (`wear/all`).

On the interval the guard reserves the one run that can hold the 184-block
extent and sends every short image to the other side of the sleeping image.
Each session then writes its short images into one part of the map and its
long ones into another, and the parts depend on where the map ends, which is
the same in every session. The imbalance survives the sum: `wear/all` is 1.40
for the guard on the interval with one sleeper.

## The ring

Blocks 4 to 383 form 95 sectors. On the ring an extent may start near block
383 and continue at block 4, so the free blocks before and after the map end
are one run.

- **One sleeper leaves one free run.** A circle with one cut is an interval.
  The 360 free blocks around one 20-block sleeper are contiguous wherever the
  sleeper lies, so the sleeper's position stops mattering; on the interval it
  leaves two runs unless it lies at an end.
- **k sleepers leave k runs, against k + 1.**
- **The placement rule has no preferred origin.** The boot cursor is uniform
  over the sectors (`swap_cursor_init()`), and a search that runs from the
  cursor round the ring gives the same placements, rotated, for a rotated
  cursor. Every sector therefore has the same expected erase count, and the
  summed erase counts differ from uniform only by sampling noise, which
  shrinks with the square root of the number of sessions. The interval breaks
  this: its wrap always restarts at the low end.

The last point predicts that `wear/all` on the ring falls toward 1 as
sessions accumulate and that the interval's does not. The run confirms it
(one sleeper, one transient, early sleepers):

| Sessions | `lin-next` | `lin-guard` | `ring-guard` |
| --- | --- | --- | --- |
| 500 | 1.141 | 1.411 | 1.025 |
| 2000 | 1.147 | 1.404 | 1.015 |
| 8000 | 1.148 | 1.381 | 1.007 |

The ring's excess over 1 halves for each fourfold increase in sessions; the
interval policies stay where they are.

## Result

`ring_sim` with 2000 sessions of 400 commands, the session model of
`placement-study.md`. `lin-next` is the kernel's next-fit restated on a
sector bitmap; it draws the same random numbers as `placement_sim` and
reproduces that program's production-allocator rows in all eight scenarios
(refusal rate, per-session wear and blocks written agree to the printed
digits). `ref184` is the share of 184-block requests refused with at least
184 blocks free. `moved` is blocks rewritten by relocation per 1000
commands. One transient per command:

| Sleepers | Late | Policy | ref184 | wear/sess | wear/all | moved |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | no | `lin-next` | 31.81% | 1.30 | 1.147 | 0 |
| 1 | no | `lin-guard` | 0.00% | 1.80 | 1.404 | 0 |
| 1 | no | `ring-next` | 3.89% | 1.21 | 1.016 | 0 |
| 1 | no | `ring-guard` | 0.00% | 1.54 | 1.015 | 0 |
| 3 | no | `lin-next` | 71.51% | 1.54 | 1.181 | 0 |
| 3 | no | `lin-guard` | 0.64% | 2.10 | 1.355 | 0 |
| 3 | no | `ring-next` | 50.63% | 1.45 | 1.016 | 0 |
| 3 | no | `ring-guard` | 0.00% | 1.97 | 1.030 | 0 |
| 3 | yes | `lin-guard` | 1.69% | 2.20 | 1.404 | 0 |
| 3 | yes | `ring-guard` | 0.69% | 2.13 | 1.035 | 0 |
| 3 | yes | `ring-g+move` | 0.00% | 2.13 | 1.035 | 0.3 |

The ring without the guard (`ring-next`) already levels wear but still
refuses, because the invoking shell can land where it splits the one free
run. The guard on the ring removes those refusals. With three sleepers that
went out while a command held its extents, 0.27 to 0.69 percent of requests
are still refused; relocation on refusal removes them for 0.1 to 0.3
rewritten blocks per 1000 commands.

## Lifetime

The flash part on the measured board is rated for 100,000 program-erase
cycles per 4 KiB sector (Winbond W25Q16JV datasheet, feature list). The
hottest sector's erase rate per command is the mean rate times `wear/all`.

| Policy | Blocks written per command | Hottest sector, erases per command | Commands to 100,000 erases |
| --- | --- | --- | --- |
| `lin-next` | 62.3 | 0.188 | 532,000 |
| `lin-guard` | 68.2 | 0.252 | 397,000 |
| `ring-guard` | 68.2 | 0.182 | 549,000 |

The kernel's next-fit writes fewer blocks because it refuses a third of the
184-block forks. The ring admits all of them and its hottest sector still
erases less often.

## Bounds from the literature

- **No placement rule is complete without relocation.** Robson's bound for
  allocation without relocation needs store of about `M (1 + 1/2 log2 n)` for
  live size `M` and largest block `n` (J. ACM 21(3), 1974). The swap map has
  380 blocks for a 184-block request, far below that, so some sequence of
  sleepers defeats every placement rule. The three-sleeper remainder above is
  that case, and relocation on refusal is the smallest answer to it.
- **Within one session no policy reaches a ratio of 1.** The sectors under
  the sleepers take no erase, so a session's ratio is at least 95/90 with one
  sleeper and 95/85 with three. Levelling across sessions comes from the boot
  cursor moving the sleepers, which is the static wear levelling of the flash
  literature obtained without a copy (Gal and Toledo, ACM Computing Surveys
  37(2), 2005).
- **A cyclic log is the reference dynamic leveller.** Ben-Aroya and Toledo
  (ACM Trans. Algorithms 7(2), 2011) give the first analysis of wear
  levelling and find that online algorithms do not beat simple ones. The
  root file system's Dhara journal (`sys/arch/rp2040/dhara/journal.h`) is a
  ring for the same reason.

## Conjectures

1. **The ring removes the single-sleeper refusals by itself.** Refuted:
   `ring-next` refuses 3.9 to 6.8 percent, where the shell splits the free
   run. The guard is still needed.
2. **The ring with the guard refuses nothing with one sleeper.** Refuted by
   one refusal. None in 2000 sessions in four scenarios. Held.
3. **The ring's accumulated wear is below the kernel's.** Refuted if
   `wear/all` for `ring-guard` is at or above `lin-next` in any scenario. It
   is 1.010 to 1.035 against 1.127 to 1.239. Held.
4. **The ring's excess wear is sampling noise.** Refuted if it does not fall
   with more sessions. It falls 1.025, 1.015, 1.007. Held.
5. **A topological change cannot be carried into this kernel.** Refuted: the
   next two sections describe the change and its board measurement.

## The change in the kernel

Every extent stays contiguous in a logical block space that runs past the map
end: logical block `b` at or above `nswap` names physical block
`b - nswap + SWAP_IMAGE_ALIGN`. The image code keeps its base-plus-offset
arithmetic, and three places learn the fold.

- **I/O.** `swap_with_buf_result()` (`sys/kern/vm_swp.c`) is the one function
  through which `swap()`, the exec spool, the in-place exchange and the
  SwapRAM evacuation reach the device. It folds the block number and ends a
  transfer at the map end.
- **Allocation.** `struct map` (`sys/sys/map.h`) carries the ring bounds and
  the guard. With them set, `malloc3_contiguous_next()` treats the run that
  ends at `nswap` and the run that starts at `SWAP_IMAGE_ALIGN` as one,
  applies the guard, and removes both pieces from the map. With them zero it
  is the interval search, which the STM32 port and every earlier fixture
  keep.
- **Release.** `mfree()` folds an address above the ring end and returns an
  extent that passes it as two pieces, so no caller changed.
  `swap_exchange_tail_reserve()` takes the free blocks after an incoming
  extent from the run before the end and the run after it.

`init_main.c` sets the bounds when the port defines `SWAP_IMAGE_GUARD`
(`sys/arch/rp2040/include/machparam.h`, 196). The cost is no flash write, no
per-process field and three words in the map.

Host gates, each linking the production source:

- `tests/kernel/rmap_test.c`: an extent that passes the end, the guard, the
  full-descriptor case, and a 20,000-step random session in which the
  allocator must return the placement of the model in `ring_sim.c` on the
  RP2040 geometry. `check-rmap-ring-mutants` removes the run join, the guard
  and the `mfree()` fold in turn; each is rejected.
- `tests/kernel/swap_ring_io_test.c` (`check-swap-ring-io`): the transfer
  ends at the last block and resumes at the first, and a block above the end
  is folded; the mutant without the fold is rejected.
- `swap_exchange_inplace_test.c` repeats its exchange grid on an image that
  starts 8 blocks below the end of a ring, and `fork_swap_test.c` reserves,
  cancels and releases an image at every cursor of a ring.

## Board measurement

Bare Pico, boot ROM V3, tree `9e57fc33` plus the change as it stood before
`swap_exchange_tail_reserve()` learned to take a tail from both sides of the
end; that kernel declined such a tail and took a fresh extent. Two PICO kernels
from that tree differ only in `SWAP_IMAGE_GUARD`: defined (`unix.uf2`
SHA-256 `2d1b43cc...87c748c`) and undefined, which leaves the interval map
(`82035931...ef3a46c`). The fork test is `tests/rp2040/fork_refusal` built
with a 172 KiB bss; on this tree its image and its fork child each need 180
blocks. `tests/rp2040/swapmaptest` prints the free runs.

**Twelve forks from one login, no holds.** Both kernels admitted all twelve.
This run decides nothing between them, and it corrects the model: the
invoking shell kept one extent for eight consecutive commands (blocks 184 to
203 on the interval kernel), where the model moves it at every command. The
in-place exchange returns the shell to the extent it left. The model's
per-command rotation of short images, and with it the wear figures above, do
not describe this kernel's steady state.

**Forks under a free map that only a ring can serve.** Swap holds on
`/dev/temp0` and `/dev/temp1` (`talloc`) were moved until `swapmaptest`
showed a longest free run under 180 blocks while the run ending at block 383
plus the run starting at block 4 reached 180; then the fork test ran.

| Kernel | Trials | Admitted | Refused |
| --- | --- | --- | --- |
| interval | 6 | 0 | 6 |
| ring | 6 | 6 | 0 |

On the ring kernel the joined run was 200 blocks in five trials and exactly
180 in one (free runs 4+32, 76+60, 236+148), so the child's extent passed
block 383 in every trial; each child verified its parent's 172 KiB of
xorshift bytes. The prediction held: a refusal on the ring kernel or an
admission on the interval kernel would have refuted it.

**The final kernel.** The kernel built from commit `34602fd3`, with the
two-sided tail (`unix.uf2` SHA-256 `6309d0b5...d3259bf2`), repeated the
adverse procedure: six trials, six admissions, each child verified. In one
trial the joined run was exactly 180 blocks (free runs 4+152, 176+20,
216+20, 356+28), so the child's extent took every block of the run below
the end and 152 above the start.

**Console.** The first boot of the earlier ring kernel stopped echoing in
the middle of a typed command after login, before any test. Every later
boot ran clean: three more of that kernel, eight of the final ring kernel
and eight of the interval kernel, each flashed, logged in and driven through
`sysctl`, `ls`, `ps` and `date`; the Renode boot gate also passes. The
event did not recur in the 11 later boots of ring kernels, which bounds its
rate on this board but does not name its cause;
`sys/arch/rp2040/doc/BOARD-CONSOLE.md` records a USB console wedge here
that predates the change.

## Not run

- Per-sector erase counts on the board. The lifetime table rests on the
  model, whose event order the twelve-fork run contradicts.
- The three-sleeper remainder and relocation on refusal.
- A session model fitted to a recorded allocation trace.

## Reproduce

    cc -std=gnu17 -O2 -Wall -Wextra -Werror -o ring_sim \
        docs/research/swap-exhaustion/placement/ring_sim.c
    ./ring_sim 2000 400

`bmake ring` in `docs/research/swap-exhaustion/placement` does the same.
