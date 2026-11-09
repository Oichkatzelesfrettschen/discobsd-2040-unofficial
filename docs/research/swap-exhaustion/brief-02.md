# Brief 02: adversarial review, then first-principles redesign for LARGE fork

Input for the second review round. The round writes `docs/research/swap-exhaustion/review-02.md`.

## Operator decisions this round must respect

1. Swap fix scope (decided): staged, all three layers, each with a calibrated
   host gate and mutants:
   - A. `TFALLOC` requires a write-open descriptor and root (closes the
     unprivileged swap-pinning path).
   - B. Transactional swap reservation: fork returns `ENOMEM` with a complete
     unwind before child publication; `sched()` restores its victim and sleeps
     on a capacity event instead of panicking; init handles a failed fork.
   - C. Exchange-headroom admission so a runnable swapped image always leaves a
     route for the resident image to leave the window.
2. Item 1 (MPU region 4 for the LARGE bonus) and the LARGE fork: decompose to
   first principles and find a design that makes a LARGE process's fork work
   on this hardware, not only fail cleanly.

## Justifications and falsifiers (attack these)

Each selection states the claim it rests on and the observation that would
refute it. Treat every one as a hypothesis; confirm or refute each from source,
binaries or execution, never from this text.

- J1, order A then B then C. Claim: A is independent of B and C, is small, and
  removes the only unprivileged way found to hold swap indefinitely, so it lands
  first. Falsifier: a second unprivileged indefinite holder (another ioctl, a
  spool that survives its exec, a mapping, a pipe or tty buffer backed by swap)
  makes A insufficient as the DoS fix; or a privileged caller (fsck on a large
  root uses `/dev/temp0`) runs as non-root, which would break it.
- J2, B before C. Claim: B's reservation object is the primitive C needs, and B
  alone already converts every panic into a refusal or a sleep. Falsifier: a
  path other than `newproc()` and `sched()` reaches `swapout()` or an
  equivalent unconditional panic on swap shortage (grep every `panic` that
  names swap, `malloc3*`, `swap_with_buf`, `swapram_*`); or B's sleeping
  swapper deadlocks the system (every runnable process swapped out, nothing
  ever frees), which would make B a hang instead of a fix.
- J3, ENOMEM. Claim: `share/man/man2/fork.2` assigns `ENOMEM` to insufficient
  swap, and `bin/sh` already prints "cannot fork: no swap space" for it.
  Falsifier: the shell or another shipped caller treats `ENOMEM` from fork as
  fatal in a way `EAGAIN` would not, or the libc stub maps it differently.
- J4, the 544-block exchange peak. Claim: `sched()` writes the resident image
  (`swapout`) before `swapin()` frees the incoming one, so the first switch
  from a LARGE parent to its LARGE child needs both full images in flash at
  once. Falsifier: any path that swaps the parent out while the child's image
  is already freed, or a measured `p_tsize` that changes the arithmetic enough
  to fit. Measured: `tests/rp2040/swapram_large/bigtest` is OMAGIC with a_text
  0x1f68 (8040), a_data 0xc8, a_bss 0x24a8c, so if `swaptext()` subtracts the
  clean text the child image is about 160 blocks, not 164. Verify which.
- J5, the 216-block occupancy is real and stable. Three board runs printed
  `free: 120@4 4@144 28@324 12@372`, `60@24 4@104 100@284` and
  `20@4 60@44 4@124 80@304`: 164 free every time. Claim: the occupants are the
  evacuated small processes (init, the pid-10 daemon, `-sh`, `su`, the root
  shell) stored raw after `swapram_evacuate()` expands them out of the
  compressed pool, plus alignment padding. Falsifier: an attribution that
  finds a temporary-device extent, a stale exec spool, or a leaked image in
  that 216.

## Candidate directions for a LARGE fork that succeeds (develop and rank)

Develop each from first principles (RP2040 datasheet, flash geometry, the
kernel's own constants), estimate its cost in RAM, flash blocks, erase count,
time and code size, and falsify it. Find better ones.

- D1, in-place sector exchange. The parent and child of a fork share one
  layout, so RAM chunk k and image sector k correspond. Exchange them a sector
  at a time with one sector of scratch (a free flash sector, the reserved
  scratch sector, or a 4 KB RAM bounce). The peak becomes one image plus one
  sector instead of two images. Weigh scratch-sector wear, a free-sector
  rotation that makes the image a per-sector scatter map, XIP-off windows, and
  interrupt latency.
- D2, keep evacuated images compressed in flash. `swapram_evacuate()` expands
  pool images to raw flash. Writing the compressed form (the port already has a
  heatshrink decoder used by exec and swap) could shrink the 216 blocks.
  Measure the real compression ratio of the evacuated processes from the built
  binaries or a Renode run.
- D3, vfork or a spawn path for LARGE callers. bigtest and the shell could
  avoid the copy entirely; judge whether that is a fix or a workaround, and
  what the kernel's vfork path costs under LARGE.
- D4, copy-on-swap sharing. At fork the child refers to the parent's image
  until one of them writes; judge feasibility without an MMU.
- D5, LARGE admission that reserves exactly the exchange headroom at exec, so
  a LARGE process that cannot fork never starts.
- Anything better: for example reclaiming kernel RAM, a smaller USIZE for
  swapped images, or a swap partition sized from the root's measured slack.

## Dynamic multidimensional analysis to perform

Use the installed instruments (see the operator's tool registry; all of
these are present): `arm-none-eabi-objdump`/`nm`/`size`, `bloaty`, `pahole`,
`cflow`, `cppcheck`, `semgrep`/`weggli`/`ast-grep` for pattern sweeps,
`rizin`/`r2` or `ghidra-analyzeHeadless` for disassembly of the built kernel,
python `capstone`/`unicorn`/`pyelftools` for emulating a routine, and the
host fixtures under `tests/kernel` and `sys/arch/rp2040/test/swapram`.
Built artifacts of the LARGE-bonus kernel are in the sibling worktree
`../../mpu/large-bonus-region/sys/arch/rp2040/compile/PICO/unix.elf` and
`../../mpu/large-bonus-region/tests/rp2040/swapram_large/bigtest`. Measure
kernel RAM headroom (`.data`, `.bss`, stacks), the code size each design
would add against the flash headroom gate, the evacuated images' compressed
sizes, and the exact `p_tsize` arithmetic.

## Output

`review-02.md`: (1) an adversarial review of `audit-01.md` claim by claim,
each marked confirmed, refuted or unproven with its evidence; (2) the verdict
on each of J1-J5; (3) the ranked LARGE-fork designs with costs and
falsifiers; (4) a first-principles build plan for layers A, B, C and the
chosen LARGE-fork design: commits in order, functions, invariants, gates and
mutants per commit, and what needs a board run. Cite file:line, commands and
measured outputs.

## Standing rules

Write only `docs/research/swap-exhaustion/review-02.md` and scratch files
under `docs/research/swap-exhaustion/scratch-02/`. Builds go into a
throwaway copy if they would modify tracked files. No network, no package
installs, no git commits or pushes. Never open `/dev/ttyACM*`,
`/dev/serial/*`, or run `picotool` or `discobsd-flash`: the board belongs to
another session.
