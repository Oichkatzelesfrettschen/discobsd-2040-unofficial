# Build plan: swap exhaustion under the fixed 192 KiB process window

Derived from `audit-01.md`, `review-02.md` and its addendum, and restated for
the single 192 KiB user window (`USER_DATA_SIZE`, `0x20000000..0x20030000`)
that replaced the LARGE large-process mode and its 144 KiB window. The raw
swap partition stays at 384 KiB.

## First principles

- Flash swap is 380 allocatable 1-KiB blocks (`sys/arch/rp2040/dev/flash.h`,
  four reserved as rewrite scratch), handed out as 4-block erase-aligned
  contiguous runs (`SWAP_IMAGE_ALIGN`) by a next-fit cursor
  (`malloc3_contiguous_next`, `sys/kern/subr_rmap.c`).
- One process is resident at a time. `sched()` (`sys/kern/vm_sched.c`) writes
  the resident image out before `swapin()` (`sys/kern/vm_swap.c`) releases the
  incoming one, so a switch needs one free aligned run of the resident's
  `swap_image_span()` or a SwapRAM pool admission.
- A flash-tier swap-in frees exactly its own span as one aligned run. A
  SwapRAM-tier swap-in frees no flash.
- An image that fills the window spans 196 blocks: 192 data and stack blocks
  plus three for the 3,072-byte `USIZE`, rounded up to the 4-block alignment.
  Two such images need 392 blocks, more than the 380 the partition holds, so
  a parent and child that both fill the window cannot both live in flash.
  This is arithmetic over the constants; no board run has measured it.
- Swap images are never read after a reboot, so an exchange interrupted by
  power loss needs no recovery. A flash operation that fails mid-exchange
  does, because RAM and flash then each hold part of both images.
- The swapper may sleep only when a release it does not itself perform can
  occur; otherwise it refuses and returns to eligible work (`review-02.md`,
  J2).

## Landed

1. Temporary swap extent authorization: `TFALLOC` requires `FWRITE` and
   `suser()` (`5ed8d698`, PR #404). Gate `swap_ioctl_test` with the
   `ignore-descriptor-mode` and `omit-root-check` mutants.
2. Owned image reservations in `vm_swap.c` (`swap_image_reserve`, copy-out,
   commit, cancel, release); `swapout()` panics without a valid reservation
   (PR #407).
3. Fork reserves the child's backing before publication and returns `ENOMEM`
   with a complete unwind (`newproc()`, `fork1()`, PR #407). Gate
   `check-kernel` through `tests/kernel/fork_swap_test.c`.
4. Swapper refusal: `sched()` reserves before eviction; on refusal it restores
   `SLOAD`, requeues an `SRUN` victim and sleeps on `runin`, which every sleep,
   exit and the once-a-second `schedcpu()` wake (`9339f5fb`, PR #407). A bare
   Pico run held every flash swap extent with `talloc`, forked a 100 KiB
   incompressible image to `ENOMEM`, then forked it successfully after
   release, with no panic.
5. init retries a refused fork with a bounded delay (`35b6fcaf`, PR #405).

## Remaining, each its own pull request

6. Exchange-headroom admission across fork, exec spool, `TFALLOC`, SwapRAM
   evacuation and data or stack growth, so a runnable swapped image always
   leaves a route for the resident image to leave the window. Open design
   decision: a SwapRAM-tier swap-in frees no flash, so a rule that relies on
   each flash swap-in returning its own route leaves a process that comes back
   from the pool with no route out. `brief-06.md` assigns the comparison of an
   owned departure extent, a computed largest-run check and a route sized for
   the largest live image, and the gate `check-swap-exchange-headroom`.
7. In-place sector exchange in `sched()`: when the outgoing resident and the
   incoming swapped image have equal layouts (a fork pair), exchange them
   sector by sector through the 4,096-byte bounce that
   `sys/arch/rp2040/conf/kern.ldscript` reserves at link time, then swap the
   two processes' addresses. The peak becomes one image plus the bounce, which
   lets a parent and child that each exceed 190 blocks alternate. Gates: a
   host fixture over the production exchange with an emulated NOR device that
   fails each erase and program position, then a bare Pico run where a
   near-window image forks, parent and child alternate at least twice, and
   the child exits and is reaped. A board run of item 6 that already shows
   such a pair alternating removes the need for this item.
