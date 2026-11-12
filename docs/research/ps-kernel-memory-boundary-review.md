# Process listing and the kernel memory devices: design review

Status: review output, 2026-11-12. The reviewer was the Codex model
`gpt-daybreak-blue-latest` at high reasoning effort, run read-only on the
tree at `83bd4dde` with one change under review: a machdep node that
returned the whole `struct user` of a process held in the SwapRAM pool. The
reviewer built and booted nothing; its size figures are estimates from
existing objects. The question was how ordinary users get a working `ps`:
`ps` run by `operator` stops with `nproc not in namelist`, because
`knlist()` passes the symbol name as a sysctl new value and `__sysctl()`
grants a new value to root alone.

Outcome in the tree: the node became `machdep.swapram_psinfo`, the compact
record of step 2. Steps 1 and 3 to 5 are open.

The review text follows as returned, with file references shortened to
repository paths.

---

The current image has a more serious defect than the failed lookup: `operator` belongs to supplementary group 0 (`wheel`) (`etc/group:1`), and login installs supplementary groups (`usr.bin/login/login.c:398`). The devices inherit group 0 and mode 0664 (`distrib/rp2040/mi.rp2040:18`), while `/dev/mem` and `/dev/kmem` share a read/write driver (`sys/dev/mem.c:44`). Securelevel starts at 0 (`sys/arch/rp2040/rp2040/machdep.c:147`). Thus `operator` has kernel-memory write permission, not merely read permission.

Size estimates use the existing PICO Thumb objects without rebuilding: `__sysctl` is 328 text bytes; the `CPU_NLIST` branch is about 70; its table is 288 plus 276 name bytes; the reviewed `CPU_SWAPRAM_UAREA` branch is 92; `sysctl_doproc` and `fill_from_u` are 268 and 224. Exact deltas require a rebuild.

| Option | Required changes | Kernel text estimate | Security, breakage, residual risk |
|---|---|---:|---|
| 1. Exempt new-value lookup | Reorder and special-case authorization in `__sysctl()` (`sys/kern/kern_sysctl.c:90`); validate and `copyin` the query in both RP2040 and STM32 `cpu_sysctl()` (`sys/arch/rp2040/rp2040/sysctl.c:369`, `sys/arch/stm32/stm32/sysctl.c:195`); extend `test_syscall_authorization()` (`tests/kernel/sysctl_test.c:1023`). | +60-120 robustly; about +12-32 if implemented unsafely. | Ordinary users gain the fixed export addresses. Under current modes, `operator` can turn those addresses into targeted kernel writes, corruption, escalation, or panic. A naive exemption also exposes invalid-pointer/oversized-`newlen` kernel faults because `strncmp` currently dereferences `newp` directly. Functionally fixes every `knlist()` caller on both ports. Residual risk is unacceptable. |
| 2. Root-only lookup | Improve `knlist()` error preservation (`lib/libc/gen/knlist.c:8`) and `ps:getkvars()`'s message (`bin/ps/ps.c:640`). | 0 kernel bytes. | Ordinary `ps` remains unavailable; vmstat, iostat, pstat, fstat, and devupdate retain the restriction unless each diagnostic changes. Hiding addresses does not repair world-readable or wheel-writable memory devices. The reviewed SwapRAM node independently publishes an entire `struct user` (`sys/arch/rp2040/rp2040/sysctl.c:228`), including kernel pointers and descriptor state (`sys/sys/user.h:24`). |
| 3. Classic `kmem` boundary | Make the three device entries 0640 root:kmem and `/bin/ps` 02751 root:kmem in `mi.rp2040`; harden `ps:openfiles()` (`bin/ps/ps.c:617`) to open only fixed live devices read-only and drop group privilege immediately, or drop before caller-supplied `-k` paths. Add group-aware image checks to `fs_inode_print()` (`tools/fsutil/inode.c:200`) and the account-image checker. Replace, rather than merely exempt, the lookup as described below. | +20-100 with a safe MIB query; filesystem changes cost zero kernel bytes. | Ordinary users gain only `ps`'s intended output. Root retains writes; `operator` can still explicitly `su` to root, so the account policy prevents containment claims. RP2040 ships only standalone `ps` (`distrib/rp2040/mi.rp2040:217`). Never make `adminbox` setgid: its `nohup` and `timeout` execute arbitrary commands (`sbin/adminbox/tiny_applets.c:110`, `usr.bin/timeout/timeout.c:606`). If later shipped, vmstat/iostat/pstat/fstat need separately audited setgid executables; `vmstat -z` must remain root-only because it writes `/dev/kmem` (`usr.bin/vmstat/vmstat.c:166`). `w` also reads memory devices without `knlist`. |
| 4. Process-information syscall | Add an additive `KERN_PROC_PS` record in `sysctl.h`, implement a pointer-free snapshot beside `sysctl_doproc()`, and migrate `ps`. Do not enlarge existing `kinfo_proc`, which is consumed by `w` and RP2040 tests (`sys/sys/sysctl.h:190`). | +250-450 for command name/times; roughly +500-900 if arguments are reconstructed. | Best eventual boundary: no setgid or memory device for `ps`. Existing `KERN_PROC` supplies `proc`, real UID, and tty, but lacks `u_comm`, CPU times, signal dispositions, and arguments (`sys/kern/kern_sysctl.c:788`); `ps` obtains those from `struct user` and the stack (`bin/ps/ps.c:489`). Command-only output changes behavior; safe argument snapshots across resident, flash-swapped, and compressed images add substantial complexity. Other diagnostic tools would still need `/dev/kmem`. |

## FINAL RECOMMENDATION

Adopt option 3, hardened with a read-only MIB lookup, and defer option 4.

1. Change `knlist()` to carry a bounded, explicitly packed symbol name in `{CTL_MACHDEP, CPU_NLIST, ...}` with `new=NULL`; update both machine handlers. Known-good: exact `_nproc`. Known-bad: unknown, prefix-only, unterminated, and oversized names; a mutation accepting the old `newp` form must fail.
2. Replace `CPU_SWAPRAM_UAREA` with a compact, pointer-free `ps` record containing only command name, tty, times, sizes, and required signal state. Known-good: a pool-backed PID. Known-bad: resident/nonexistent PID returns `ESRCH`; a full-`struct user` size mutation fails.
3. Install the devices as 0640 root:kmem and standalone `ps` as 02751 root:kmem. Extend `check-account-image` to verify type, owner, group, and mode; calibrate wrong-group and each extra-write-bit cases.
4. Open fixed devices read-only, then irreversibly drop the effective/saved kmem group; drop before `-k` paths. Unit-test privilege-drop failure and alternate-path ordering.
5. Run `ps -ax` directly as `operator` in Renode; known-good requires init's time/name. Known-bad requires direct operator opens of all three devices to fail and a removed SwapRAM record to fail the process row.
6. Run the existing sysctl, nlist, image-metadata, Renode, and kernel-headroom gates.

I did not build, boot Renode, inspect a generated image, or validate STM32 runtime policy. Exact linked deltas and live permission behavior remain unverified.
