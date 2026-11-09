# Build plan: swap exhaustion and the LARGE fork

Derived from `audit-01.md`, `review-02.md` and its addendum. The operator
chose the staged swap fixes and an in-place sector exchange as the LARGE-fork
success design; the raw swap partition stays at 384 KiB.

## First principles

- Flash swap is 380 allocatable 1-KiB blocks (`sys/arch/rp2040/dev/flash.h`,
  four reserved as rewrite scratch), handed out as 4-block erase-aligned
  contiguous runs by a next-fit cursor (`malloc3_contiguous_next`).
- `sched()` writes the resident image before `swapin()` frees the incoming
  one (`sys/kern/vm_sched.c`, `sys/kern/vm_swap.c`), so alternating two
  processes of one layout needs both whole images in flash at once.
- The board capture in the `review-02.md` addendum shows fork already
  succeeds: the child owns its exact 156-block image, and the panic is the
  scheduler evicting the sleeping LARGE parent. Small processes own 60
  blocks, so the exchange needs 372 of 380 blocks and fails on
  fragmentation.
- A LARGE parent and its child share one layout: RAM chunk k of the 144-KiB
  window and sector k of the child's image hold the same addresses. Exchanging
  them a sector at a time through a 4-KiB bounce leaves one 156-block image in
  flash throughout, so the peak is 60 + 156 = 216 blocks.
- Swap images are never read after a reboot, so an exchange interrupted by
  power loss needs no recovery; a flash operation that fails mid-exchange
  does, because RAM and flash then each hold part of both images.
- The swapper may sleep only when a release it does not itself perform can
  occur; otherwise it refuses and returns to eligible work (`review-02.md`,
  J2).

## Order, each its own pull request

1. Temporary swap extent authorization: `TFALLOC` requires `FWRITE` and
   `suser()`, and `talloc` opens read/write. Gate: `swap_ioctl_test` with
   `ignore-descriptor-mode` and `omit-root-check` mutants. No board run.
2. Owned image reservations in `vm_swap.c` (`swap_image_reserve`, copy,
   commit, cancel, release); `swapout()` consumes a reservation. Gate
   `check-swap-reservation` over the recorded maps and every aligned cursor.
3. Fork reserves the child's backing before publication and returns `ENOMEM`
   with a complete unwind (`newproc()`, `fork1()`). Gate
   `check-fork-swap-refusal`.
4. Swapper refusal restores its victim and either sleeps on a capacity
   channel with an independent releaser or records a bounded refusal. Gate
   `check-scheduler-swap-refusal`, including `sleep-without-releaser`.
5. init handles a negative fork result with bounded delayed retry. Gate
   `check-init-fork-refusal`.
6. Shared exchange-headroom admission across fork, exec spool, `TFALLOC`,
   evacuation and growth. Gate `check-swap-exchange-headroom`.
7. In-place sector exchange in `sched()`: when the outgoing resident and the
   incoming swapped image have equal layouts (a LARGE fork pair), exchange
   them sector by sector through a bounce buffer, then swap the two
   processes' addresses together. The bounce's home and the exchange's
   segment boundaries come from the memory survey in `brief-03.md`. Gates: a
   host fixture over the production exchange with an emulated NOR device that
   fails each erase and program position, then a board run where `bigtest`
   forks, alternates at least twice, and the child exits and is reaped.

Item 1 of `docs/research/xip-shared-libc/README.md` (MPU region 4 for the
LARGE bonus) lands after the exchange, with its board gate expecting
`BIGTEST OK`.
