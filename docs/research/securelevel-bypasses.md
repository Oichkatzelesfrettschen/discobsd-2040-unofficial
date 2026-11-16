# Securelevel bypasses: `ucall(2)` and the swap blocks

Scope: owner decision D135 closes the two paths that go around the level-1 guards of the kernel securelevel (`docs/research/securelevel-small-device.md`, branch `docs/securelevel-study`): `ucall(2)` and a write open of the swap blocks under their device name. This note records the change as a defect fix.

## Record before the first test run

Order of events. The two source edits were made before this record; no test had run on them. The prediction below is therefore a statement about the cases as written, not a blind one.

Observations (source, `origin/main` b40e1790).
- O1. `ucall()` (`sys/arch/rp2040/rp2040/sysctl.c`, `sys/arch/stm32/stm32/sysctl.c`) tests `suser()` and then calls the function at a caller-given address, at `spl0` or `splhigh`, with two integer arguments. No securelevel test guards it. The routine runs at kernel privilege, so it can write any kernel memory the level-1 refusal of `/dev/kmem` writes protects.
- O2. `openi()` (`sys/kern/sys_inode.c`), block case, refuses a write open of a disk block device at level 2 (`isdisk()`) and a mounted one at level above 0 (`ufs_mountedon()`). `swapdev` is `fl1` (major 0, minor 8) on a flash kernel, the PSRAM block device on a PSRAM kernel; neither is mounted, and `isdisk()` lists only the flash, swap and SD majors. A write open of `fl1` therefore succeeds at level 1, and `tests/kernel/ufs_disk_test.c` asserted it (`test_overlap_flash`). The `sw` alias of the same blocks refuses a write at level above 0 (`sys/dev/swap.c`).

Rule. The level-1 guards exist so that a process cannot write the kernel's memory or the live swap blocks once the system is secure.

Hypotheses and falsifiers.
- H1. Refusing `ucall()` with `EPERM` at level above 0, after the `suser()` test and before the priority change, closes the path and leaves level 0 behavior unchanged. Falsifier: the routine called at level 1, or not called at level 0, in the host fixture (`ucall_contract.py`) or the ARMv6-M fixture (`ucall_qemu.py`, all optimization levels, three compilers).
- H2. Refusing a write open under the lock in `openi()` when `blkoverlap(swapdev, dev)` holds and the level is above 0 closes the name `fl1` at level 1 as the alias already is, without touching reads, level 0, the root partitions or the boot `fsck`. Falsifier: `FL_SWAP` opened for writing at level 1 or 2, refused at level 0 or for reading, or the root `fl0a` answers changing.

Predicted movement against `origin/main`.
- `ucall_contract.py`: the new cases exit with status 4 on main (the routine is called at level 1); the added mutant (the level test relaxed) is rejected.
- `ucall_qemu.py`: the same, with exit 4 for the level mutant at every level, compiler and architecture.
- `ufs_disk_test.c`: on main the two level-1 write checks of `FL_SWAP` (write, read-write) fail; the read, level 0 and level 2 checks pass on main; all pass on the branch. Two new mutants (the swap clause disabled, the clause at level 2 only) are rejected.
- Kernel text: the `ucall` test costs 18 to 26 bytes in the earlier measurement; the `swapdev` test measured 24 to 32 bytes with an equality compare and may cost more with `blkoverlap()`; the sum is predicted at 42 to 72 bytes per kernel and RAM does not move.
