# Brief 03: every RAM and storage resource on the board, and the in-place exchange

Input for the third review round. The round writes
`docs/research/swap-exhaustion/review-03.md` and scratch files under
`docs/research/swap-exhaustion/scratch-03/`.

## Decisions this round must respect

1. The LARGE-fork success design is an in-place sector exchange in `sched()`
   (`plan.md`, item 7). The board capture in the `review-02.md` addendum is
   the deciding evidence: fork succeeds, the scheduler's eviction of the
   sleeping LARGE parent panics, small processes own 60 blocks, and the
   exchange needs 372 of 380 blocks.
2. The raw swap partition stays at 384 KiB; the root keeps its capacity.
3. Before the exchange is designed, decompose the hardware to first
   principles: find every byte of RAM or storage on the bare Pico (RP2040,
   W25Q16JV) that sits idle, idle part of the time, or is spent on something
   that could live elsewhere, and judge each as a home for the exchange bounce
   buffer, for kernel state that would free main SRAM, or for swap capacity.

## Resource inventory to build (measure each; cite the datasheet section from
`sys/arch/rp2040/doc/DATASHEET-INDEX.md` and the source or linker symbol)

For every resource give: address range, size, access width and speed, who
uses it under the PICO kernel (source citation or linker symbol), when it is
idle, what it costs to borrow (latency, a mode switch, interrupt masking,
lost cache), what breaks if it is borrowed, and a verdict as a bounce home, a
relocation target for existing kernel data, or neither.

- Main SRAM banks 0-3 (striped) and the direct-mapped SRAM4 and SRAM5 4-KiB
  banks (scratch X and Y): which kernel objects occupy each, and the measured
  static RAM gap (review-02 E7 measured 76,304 bytes between BSS end and the
  process-0 user area). Account for that gap exactly: is it free, a stack, a
  guard, or reserved by a layout rule?
- The 8-KiB scratch region at `0x20040000` and what it holds.
- The 16-KiB XIP cache usable as SRAM when the cache is disabled or bypassed,
  and the cost of losing it while the kernel executes in place.
- USB DPRAM (4 KiB at `0x50100000`): the CDC-ACM console's actual buffer use
  and the idle remainder.
- PIO instruction memory and FIFOs, the DMA engine (and whether it can
  overlap a flash program with a RAM copy), the SIO divider and interpolators,
  the boot ROM's routines and tables (including the 256-byte boot2 copy in
  SRAM, if any), and the second core's stack and idle state.
- Flash: the reserved rewrite scratch sector `[0,4)`, the 256-byte program
  page, the free-block slack of the root image, and whether the 4-KiB erase
  sector, the 32-KiB and 64-KiB block erases, or W25Q16JV security registers
  (three 256-byte pages) offer anything.
- The 3,072-byte `USIZE` per process (`struct user` is 972 bytes): what
  fraction of each swapped image is padding, and whether a smaller swapped
  user area is sound.
- Kernel buffers: the buffer cache, clists, the SwapRAM pool (16 KiB, closed
  during LARGE), the heatshrink workspaces, and per-process kernel stacks.
  During a LARGE exchange the SwapRAM pool and its window overlap are closed:
  is the pool region itself free to serve as the bounce?

## The exchange to design from that inventory

Design `sched()`'s in-place exchange for two processes with equal layouts
(fork lineage, equal `p_dsize`, `p_ssize`, `p_tsize`, same text inode),
including:

- Segment mapping: the swapped image is data-after-text, stack, user area in
  one aligned run; the resident is the window. State the exact sector-to-RAM
  correspondence, including the partial sectors at segment boundaries and the
  clean text that is not in the image.
- The bounce buffer's home from the inventory (smallest viable size: 256,
  512, 1,024 or 4,096 bytes), the erase/program sequence per sector, and the
  read path (XIP read of the swap sector versus the flash driver).
- Ordering against the ROM flash routines (XIP off, interrupts, the second
  core), the console's interrupt latency budget, and a measured or bounded
  time per 4-KiB sector.
- Failure semantics for an erase or program failure at any position: what
  state RAM and flash hold, and the recovery or panic rule (power loss needs
  none because swap is not read after reboot; justify or refute that).
- How the exchange composes with the reservation layer, the headroom
  admission, and the fallback when layouts differ.
- Code-size and RAM budgets against the measured flash headroom.

## Output

`review-03.md`: (1) the resource inventory table with citations and verdicts;
(2) clever reuse opportunities ranked by bytes recovered and risk, each with
its falsifier; (3) the exchange design with its sector sequence, bounce home,
failure rules, budgets and gates (host fixture with an emulated NOR device
failing every erase and program position, mutants, and the board run).
Cite file:line, datasheet sections, commands and measured outputs. Separate
measured facts from inference.

## Standing rules

Write only `docs/research/swap-exhaustion/review-03.md` and files under
`docs/research/swap-exhaustion/scratch-03/`; builds go into a throwaway copy
under `/tmp`. No network, no package installs, no git commits or pushes.
Never open `/dev/ttyACM*` or `/dev/serial/*`, and never run `picotool` or
`discobsd-flash`. The written report carries no attribution or authorship
line for any tool or model.
