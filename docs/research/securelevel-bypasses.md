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

## Result

Change. `ucall()` returns `EPERM` when `securelevel` is above 0, after the `suser()` test and before the priority is selected, in both ports. `openi()` (`sys/kern/sys_inode.c`) refuses a write open of a block node for which `blkoverlap(swapdev, dev)` holds at a level above 0, in the same critical section as the mounted-device test; reads, level 0 and the root partitions are unchanged. `ucall.2`, `MPU.md`, `PICOCALC.md`, `TESTING.md` and the comment in `sys/dev/swap.c` describe both refusals.

Cases and calibration. Against the parent `b40e1790` (the branch's tests with the parent's kernel sources, GCC 14.2.1, ALIENWARE): `ufs_disk_test` fails two checks (a write and a read-write open of `FL_SWAP` at level 1 return 0), `ucall_contract.py` exits 4 for both ports and both compilers, and `ucall_qemu.py` exits 4 at -O0 on the first control; all pass on the branch. Reads, level 0 and level 2 of `FL_SWAP` pass on both, as predicted. The mutants are rejected: the `swapdev` clause disabled, the clause at level 2 only, and the `ucall` level test relaxed (host fixture and the Thumb-1 fixture at every optimization level, compiler and port). One deviation from the record: the v7 format fixture also links `sys_inode.c` and needed `swapdev` and `blkoverlap()` defined; the first run on the branch failed to link there and a separate commit supplies them.

Sizes (bytes of kernel text, GCC 14.2.1 20241119): PICO 100819 to 100867 (+48), PICO_UART 90201 to 90249 (+48), PICOCALC 121163 to 121211 (+48), PICOCALC_PSRAM 123691 to 123731 (+40); `bss`, `data` and free RAM unchanged. The prediction was 42 to 72. All four kernels build without a warning; `check-kernel-headroom` passes with 29728, 40536, 9432 and 6704 bytes left at a margin of 1024.

Gates (exit codes read from the commands; ALIENWARE unless noted): build 0, `check-kernel` and `check-kernel-ilp32` 0 (with the two new mutants), `check-kernel-abi-contracts` 0, `check-ucall-qemu` 0, `check-storage-correctness` 0, `check-rp2040-fileops` 0, `check-root-noatime` 0, `verify_ufs_prototypes.sh` 0 on PICO and PICOCALC, `check-c17-compile-profiles` 0, `logged-distribution` 0, `logged-flash` 0, `check-renode` 0 (the boot runs `fsck -p` at level 0, `init` raises the level to 1 afterward, and the boot reaches login). On the build host: `check-lint`, `check-c17-source-denominator`, `check-test-execution`, `check-changed-text`, `check-changed-comments` 0.

Not added: a Renode case. The boot does not call `ucall` or open `fl1` for writing at level 1; a case needs a staged a.out in the board manifest. The cases above execute the production `openi()` and the production `ucall()` bodies; the refusals on silicon are not run.

## Round A of the review: writes through a descriptor held across the rise

Finding. The open-time refusal does not cover a descriptor opened for writing at level 0 and kept across the rise of the level: `rwip()` sends a block-device write to the buffer cache with no securelevel test, and the live swap blocks stay writable. The finding holds against `sys/kern/sys_inode.c`; the `sw` alias already asks at every transfer (`sys/dev/swap.c`) for the same reason. The flash driver has no character node (`chrtoblk()` in `conf.c` returns `NODEV` for it), so no raw path to `fl1` exists.

Change. The block case of the write switch in `rwip()` returns `EPERM` when `securelevel` is above 0 and `blkoverlap(swapdev, dev)` holds. The kernel's own swap traffic reaches `swapdev`'s strategy routine directly and is unaffected; mounted filesystems write through the buffer cache, not `rwip()`.

Case and calibration. `ufs_disk_test.c` opens `FL_SWAP` for writing at level 0, writes, raises the level to 1 and to 2 (each write returns `EPERM`), returns to 0 (the write succeeds), and checks that a descriptor of the root unit still writes at level 1. On the previous commit's `sys_inode.c` the held-descriptor writes fail the gate; the mutant that drops the test (`device-swapdev-held-descriptor-writes`) is rejected.

Cost. Kernel text (GCC 14.2.1 20241119) rises by 32 bytes per kernel over the open-time form: PICO 100899, PICO_UART 90281, PICOCALC 121243, PICOCALC_PSRAM 123763, that is +80, +80, +80 and +72 bytes over `b40e1790` (the open-time form measured +48, +48, +48 and +40; the earlier prediction of 24 to 32 bytes for the swap clause did not include this second site). Free RAM is unchanged; the four kernels build without a warning and `check-kernel-headroom` passes. The time and I/O fixture of `check-storage-correctness` also links `rwip()` and needed `securelevel`, `swapdev` and `blkoverlap()` defined, as the v7 fixture did.

Gates. On the build host's WSL: with the previous `sys_inode.c` and the new tests `check-kernel-ilp32` fails the two held-descriptor checks (exit 1); on the branch `check-kernel` and `check-kernel-ilp32` pass (including the third swap mutant), as do `check-rp2040-fileops`, `verify_ufs_prototypes.sh` on PICO and PICOCALC, `logged-distribution` and `logged-flash`. `check-storage-correctness` failed to link before the fixture definitions above and passes after (exit 0 on the build host).
