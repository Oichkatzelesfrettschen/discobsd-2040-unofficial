# Brief 04: relocate the SwapRAM pool and enlarge the user window

This brief is the goal file for one interactive program. Re-read it at every
item boundary and after every context compaction. The program writes
`docs/research/swap-exhaustion/review-04.md` and commits the implementation to
branch `kern/user-window-relocation` in this worktree.

## Decisions already made by the operator

1. The SwapRAM pool leaves `0x20024000`, the address directly above the
   144-KiB user window, and moves into kernel RAM above the kernel's data and
   BSS. Every kernel configuration leaves at least 62,400 bytes there
   (`review-03.md`, addendum: PICO 76,304, PICO_UART 93,200, PICOCALC 62,936,
   PICOCALC_PSRAM 62,400 bytes between `_ebss` and `u0` at `0x2003e800`).
2. The user window grows permanently. The candidate sizes are 160 KiB
   (128K + 32K MPU regions), 176 KiB (three regions) and 192 KiB (128K + 64K;
   kernel area `0x20030000..0x2003e800` is 59,392 bytes against a worst-case
   need of 29,760 bytes of non-pool data and BSS in PICOCALC_PSRAM, plus the
   16-KiB pool and a 4-KiB exchange bounce). The operator delegated the choice
   to this program's adversarial review: choose, justify against the
   falsifiers below, and record the choice in `review-04.md` before editing.
3. The in-place sector exchange of `plan.md` item 7 is a later change on its
   own branch. Its 4,096-byte bounce must fit the chosen layout in every
   configuration, so reserve it in the budget; do not implement it here.

## Adversarial review first (`review-04.md`)

Decompose to first principles and attack the design before building:

- Measure, for each of PICO, PICO_UART, PICOCALC and PICOCALC_PSRAM, the
  `.data` + `.bss` bytes excluding `swapram_pool_mem`, using the built ELFs
  (`arm-none-eabi-nm -S`, `readelf -S`). State the kernel RAM each window
  size leaves, after pool and bounce, and the growth margin that remains.
- Inventory every consumer of the window size and the pool address:
  `USER_DATA_SIZE` (`sys/arch/rp2040/include/machparam.h`), `USERRAM` and
  `RAM` in `sys/arch/rp2040/conf/RP2040.ld` and its tiling assertions in
  `kern.ldscript`, the MPU map and static assertions in
  `sys/arch/rp2040/rp2040/mpu.c` (PMSAv6 regions are power-of-two sized and
  aligned, datasheet table 117), `user_top()`, `swapram_init()`'s
  pool-at-window-end panic, exec admission in `sys/kern/exec_subr.c`, the
  user stack top, `machdep.c`'s boot size check, `sys/arch/rp2040/doc/MPU.md`
  and `STORAGE.md`, `usr.bin/mputest`, and every host gate or fixture that
  hardcodes 144 KiB or `0x20024000` (grep the tree, including `tests/`,
  `tools/`, `lib/` and `distrib/`).
- Decide the fate of the LARGE epoch. Its only mechanism is borrowing the
  pool's bytes above the window (`swapram.h` "SMALL/LARGE", `P_LARGE`,
  `swapram_evacuate`, `machdep.swapram_epoch`, `machdep.swapram_large`,
  `tests/rp2040/swapram_large`, `tests/rp2040/swapram_epoch`). Once the pool
  is no longer adjacent to the window, judge whether the epoch can still mean
  anything; if it cannot, remove it completely (kernel, sysctl, userland
  tools, tests, docs) rather than leaving dead code. Record each removed
  interface and every caller that read it.
- Falsifiers, any one of which rejects a candidate size: a configuration's
  kernel data plus pool plus bounce exceeds its kernel RAM; an MPU region
  cannot be aligned to its size; the kernel-headroom or link assertions fail;
  a program, test or document assumes the old layout and is not updated; the
  SwapRAM pool's address or alignment requirement (heatshrink workspace,
  `swapram_pool_mem` attributes) breaks in its new home.

## Build

After `review-04.md` records the decision, implement it as a short series of
commits, each of which builds on its own:

1. Move the pool into kernel RAM and drop the pool-at-window-end coupling.
2. Enlarge the window: linker regions, `USER_DATA_SIZE`, the MPU map and its
   assertions, and every consumer the inventory found.
3. Remove the LARGE epoch, if the review rejects it, with its tests and docs.
4. Update `MPU.md`, `STORAGE.md`, `TESTING.md` and the research notes the
   change makes stale (cite, do not rewrite, retained evidence ledgers).

Ordering rule (added by the orchestrator after reviewing commit `07c24980`):
every commit must be safe to boot, not only to build. A commit that moves
the pool while `user_top()` can still grant `SWAPRAM_BONUS` to a `P_LARGE`
process hands kernel RAM to that process. Remove the LARGE epoch before or in
the same commit as the pool move: rework the unpushed local commits (an
interactive rebase is unavailable; use `git reset --soft` and recommit) so
the series reads LARGE removal, then pool move, then window enlargement, then
docs. Commits the orchestrator has pushed (`0380c48c` and earlier) stay as
they are.

Commit messages: component-and-mechanism subject; a body of one to five
sentences giving the invariant, the change and one sentence of evidence. No
AI attribution of any kind in commits, comments or documents.

## Acceptance (run all; each must exit 0 with zero warnings)

```
export PYTHON=/usr/bin/python3
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 logged-check
bmake MACHINE=rp2040 check-kernel-headroom
```

`logged-check` takes about an hour; run it once at the end, and run
`check-kernel`, the swapram fixtures under `sys/arch/rp2040/test/swapram`
and the C17 ledger (`tools/c17/source_denominator.py`, with `--sync-paths`
for added or removed files) after each commit.

## Standing rules

- Work only in this worktree. `git commit` is authorized on this branch.
  `git push`, opening PRs, and any history rewrite of a pushed commit are
  not: the orchestrator pushes.
- No network and no package installs.
- Never open `/dev/ttyACM*` or `/dev/serial/*`, and never run `discobsd-flash`
  or a `picotool` command that talks to a device; host-only
  `picotool uf2 convert` inside the build is permitted.
- Do not touch other worktrees or `~/Github/discobsd-2040-unofficial` except
  through git's own object store.
- When done, append a "Handoff" section to `review-04.md` naming each commit,
  the gate results with counts, and anything left for a board run.

## Commit protocol (added by the orchestrator)

The sandbox mounts this worktree's registered Git metadata read-only, so
`git add` and `git commit` fail on `index.lock`; the orchestrator's shell can
write it. Do not retry Git writes. For each commit, in series order, write
`.commit-requests/NN.paths` (one repository-relative path per line, every
path the commit adds, modifies or deletes) and `.commit-requests/NN.msg`
(subject line, blank line, body), with NN starting at 01. A path belongs to
exactly one request; split a file's hunks across commits only by finishing
the earlier commit's state first. When every request of the series is written,
write `.commit-requests/READY` and keep working on acceptance. The
orchestrator commits the requests in order, writes each resulting hash to
`.commit-requests/NN.hash`, and pushes when the brief authorizes it. Cite
those hashes in the Handoff section. `.commit-requests/` is excluded from Git.
