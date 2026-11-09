# Build plan: swap exhaustion and the LARGE fork

Assistant-drafted (Claude Code, claude-opus-5-5) from `audit-01.md`,
`review-02.md` and its addendum. The operator chose the staged swap fixes and
packed compressed evacuation as the LARGE-fork success design; the raw swap
partition stays at 384 KiB.

## First principles

- Flash swap is 380 allocatable 1-KiB blocks (`sys/arch/rp2040/dev/flash.h`,
  four reserved as rewrite scratch), handed out as 4-block erase-aligned
  contiguous runs by a next-fit cursor (`malloc3_contiguous_next`).
- `sched()` writes the resident image before `swapin()` frees the incoming
  one (`sys/kern/vm_sched.c`, `sys/kern/vm_swap.c`), so alternating two
  processes of one layout needs both whole images in flash at once.
- The exact `bigtest` image is 156 blocks (clean text excluded through
  `p_tsize`); its fork alternation peaks at 216 + 156 + 156 = 528 blocks.
- 216 blocks belong to processes `swapram_evacuate()` expanded raw out of the
  16-KiB compressed SwapRAM pool at LARGE entry; a board `ps alx` attributes
  about 188 of them. Their compressed streams already coexisted in 16 KiB, so
  keeping them compressed in flash bounds that occupancy at 16 blocks plus
  alignment, and 16 + 156 + 156 = 328 fits.
- The swapper may sleep only when a release it does not itself perform can
  occur; otherwise it refuses and returns to eligible work (`review-02.md`,
  J2).

## Order, each its own pull request

1. Temporary swap extent authorization. `swcioctl()` honors `flag`:
   `TFALLOC` requires `FWRITE` and `suser()`. `talloc` opens read/write and
   tests `fd < 0`. Gate: `swap_ioctl_test` gains descriptor-mode and
   privilege cases with `ignore-descriptor-mode` and `omit-root-check`
   mutants. No board run required.
2. Owned image reservations in `vm_swap.c` (`swap_image_reserve`, copy,
   commit, cancel, release); `swapout()` consumes a reservation. Gate
   `check-swap-reservation` over the three recorded maps and every aligned
   cursor, with the review's mutant list.
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
7. Packed compressed evacuation: `swapram_evacuate()` writes the pool's
   compressed streams and a small directory to a packed flash arena and
   restores them through the existing heatshrink decoder. Prerequisite: a
   board owner-ledger capture at the panic that attributes the 216 blocks.
   Gate: evacuation fixtures at 16 and 64 KiB with injected callback failure,
   then a board run where `bigtest` forks, alternates twice, and the child
   exits and is reaped.

Item 1 of `docs/research/xip-shared-libc/README.md` (MPU region 4 for the
LARGE bonus) lands after the refusal commits, with its board gate expecting
either `BIGTEST OK` after the compressed evacuation commit or a clean
`ENOMEM` before it.
