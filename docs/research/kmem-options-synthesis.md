# Process information and the kernel memory boundary: synthesis

This note combines five corpus reviews, each in its own file:
- `kmem-options-bsd-corpus.md`
- `kmem-options-titor-corpus.md`
- `kmem-options-rtos-corpus.md`
- `kmem-options-microkernel-corpus.md`
- `kmem-options-minix-corpus.md`

It also uses two Renode probes of the PICO_UART image, made on the `rp2040/kmem-device-group` branch. Each claim below names its evidence.

## Observed defect class

In the probe image, `ps` prints a diagnostic whenever `savcom()` drops a row. The boots showed three drops.

- **Resident processes, every boot.** These are pid 15 (ps itself) and pid 0 (the swapper). Each read of `/dev/mem` at the resident user area (0x2003F400, 0x2003E800) returns EFAULT. `mmrw()` in `sys/dev/mem.c:48` admits only kernel data and kernel flash through `badkaddr_span()`, and `__kernel_data_end` equals the first user area.
- **A folded swap address, one boot in four.** pid 12, the operator's shell, had `p_addr` block 387 on a 380-block swap device, and the read returned EIO. The ring is set up at `kern/init_main.c:159` (`m_ring_end = nswap`). `sys/map.h:29` documents that address `m_ring_end + n` names `m_ring_first + n`, and `kern/vm_swp.c:36` folds it in `swap()`. `ps` seeks `/dev/swap` to `p_addr * DEV_BSIZE` without folding. In an earlier boot the same fold dropped init's row.
- **The kernel's own reader.** `fill_from_u` in `kern/kern_sysctl.c:914` hands a raw `b_blkno = p_addr` to `d_strategy` without the fold. For such a process, `KERN_PROC` (and `w`) report a stale or wrong uid and tty.

The argument strings `ps` reads from the stack image (2048 bytes at `p_saddr`) can straddle the seam, and the read is linear, so a wrapped process shows garbage arguments. That case is inferred from source and was not observed.

The Renode process-list stage checks only init's row. On the boot that dropped the shell it passed while printing two of five processes.

## Falsification result

The claim "no system has met and solved this class" is false. Each of the systems below solved it by having the kernel, or a trusted server that knows the layout, produce a fixed record:

| System | Fix | Evidence |
|---|---|---|
| NetBSD (2000) | moved ps to `KERN_PROC2` and `KERN_PROC_ARGS` after "proc size mismatch" errors; ps lost setgid | commits 6bf3264a901 and a6b1354960d |
| FreeBSD (1999) | moved ps off the u-page reader; its ps carried an alpha `get_uarea` kludge for a user area outside the mapped window, the same shape as the resident failure here | commits f6968a45e69, 5a18be3c383 and fb47be8de1c |
| SVR3 on the 3B2 | `sys3b(RDUBLK)` reads a swapped u-block through the kernel's own swap descriptor | `ps.c:935`, `sys3b.c:676` |
| Solaris | `psinfo_t` with `pr_psargs` copied at exec (PRARGSZ 80) | |
| OSF/1 | `table()` with caller-sized, append-only records | |
| Minix 3 | ps uses sysctl; the MIB server copies arguments with `sys_datacopy` and truncates on failure | |
| NuttX, Zephyr, FreeRTOS | task lists copy kernel records | |
| Genode | TRACE `Subject_info` records | |

The narrower claim survives. No tree solved the class by modeling the kernel layout in user space; every user-space layout model in the corpus drifted:
- V8 ps needs a hand-written `/etc/ndmap`.
- NetBSD fixed libkvm's swap-slot arithmetic in ad4b383df80, broken since 1998.
- 386BSD `kvm_getu` refuses swapped processes outright.

No tree has a swap ring. `rg -l "m_ring_end|m_ring_first"` over the whole corpus finds nothing outside this port.

Older systems also kept the row rather than dropping it:
- 4.4BSD ps keeps the row with `u_valid = 0` and prints `-` (`bin/ps/ps.c:376`).
- SVR2, SVR3 and V8 ps print a reason, or the proc-table fields with a zeroed u.

## Shared invariants of the surviving designs

1. The kernel assembles a fixed, pointer-free record through the same path that wrote the data: `swap()` with its fold, the pool's prefix decode, or the resident user area.
2. Arguments come from one of three sources: an exec-time copy, a kernel read of the target's address space, or nothing.
3. A detail the kernel cannot read leaves a marked row. The process row is never dropped.
4. Privilege attaches to the transport (sysctl or a capability), never to a raw device.

## Optimal design for this port

| Element | Mechanism | Kernel bytes, estimated |
|---|---|---|
| Per-process record for every residency state | extend `KERN_PROC`, or a single `psinfo.<pid>` node: resident from the user area, flash-swapped by one block through `swap()`, pooled through `swapram_uarea_prefix()`. Replaces `swapram_psinfo_read()` | +110 to +320 gross, about +60 to +200 net |
| Exec-time argument snapshot | `u_psargs[48]` in the 52 bytes between `sizeof(struct user)` (972) and `UAREA_STACK_OFFSET` (1024), filled in `exec_setupstack()` beside `u_comm`. No RAM and no swap traffic change | +30 to +60 |
| `fill_from_u` fold | read through `swap()` in place of a raw `d_strategy` | about 0 |
| Unprivileged `ps` | ps reads only sysctl; no setgid; `/dev/mem`, `/dev/kmem` and `/dev/swap` become root-only | 0 kernel bytes |
| Marked rows | a record error yields a row with `-` fields | 0 kernel bytes |
| Gate | the Renode stage requires every process: pids 0, 1, update, the shell, and ps | 0 kernel bytes |

All byte figures are estimates from `nm -S` sizes of neighboring routines. The falsifier is `arm-none-eabi-size` on rebuilt PICO, PICO_UART and PICOCALC: a net delta above +320 bytes refutes the estimate.

### Blast radius

- **Kernel:**
  - `sys/sys/user.h`
  - `sys/kern/exec_subr.c`
  - `sys/kern/kern_sysctl.c`
  - `sys/sys/sysctl.h`
  - `sys/arch/rp2040/rp2040/sysctl.c`
  - `sys/arch/rp2040/include/cpu.h`
  - `sys/arch/rp2040/include/swapram.h`
  - `sys/kern/subr_capacity.c` (its bound assertion stays)
- **Userland:**
  - `bin/ps/ps.c`
  - `usr.bin/w/w.c`
  - `usr.bin/fstat/fstat.c:90`, which shifts by 9 where `DEV_BSIZE` is 1024
- **Image:**
  - `distrib/rp2040/mi.rp2040`
  - `distrib/base/mi`
  - `tools/check_rp2040_account_image.py`
- **Tests and docs:**
  - `tests/kernel/sysctl_test.c`
  - `tools/renode/check-boot.py`
  - `sys/arch/rp2040/doc/TESTING.md`
  - `share/man/man3/sysctl.3`
  - `sys/arch/rp2040/doc/PICOCALC.md:683`, which says init never raises securelevel while `sbin/init/init.c:534` raises it to 1
  - `tools/c17/source-denominator.json`

## Other defects found in passing

- `fstat` and `w` read swap block 0 for a pooled process.
- `sysctl_doproc()` can sleep in `fill_from_u` while holding a bare process-list cursor.
- NuttX `proc_cmdline` passes the wrong destination length (`fs_procfsproc.c:724`).
- seL4's utilisation error message names the wrong syscall.

The last two are upstream observations, recorded only.

## Design as reviewed and decided

`process-info-redesign-review.md` is a gpt-daybreak-blue design review written
from `process-info-redesign-brief.md`. Its invariants, its argument-source
comparison and its boundary findings were checked against the source, and
they hold. Six of its claims changed on that check:

- **Slack.** `arm-none-eabi-gdb` on the four `unix.elf` builds (PICO,
  PICO_UART, PICOCALC, PICOCALC_PSRAM) reports `sizeof(struct user)` 972,
  with `u_stack` at offset 968. `struct user` is 4-byte aligned, so a 48-byte
  and a 52-byte argument field both give a 1024-byte struct. `u_psargs`
  therefore takes the whole slack, 52 bytes, and the capacity assertion
  `sizeof(struct user) <= UAREA_STACK_OFFSET` in `sys/kern/subr_capacity.c`
  holds with equality. The stack sentinel that `locore0.S` paints starts at
  `UAREA_STACK_OFFSET`, so the field leaves the stack measurement unchanged.
- **Walker.** The process table is the fixed array `proc[NPROC]`
  (`sys/arch/rp2040/rp2040/machdep.c`). A walk by slot index holds no list
  cursor across a sleep, and a process keeps its slot for its lifetime. Every
  process that lives through the call therefore appears exactly once, with no
  key snapshot.
- **Identity.** Every user area begins with `u_procp`, the address of its own
  proc slot. A user-area block read from flash or decoded from the pool is
  accepted only when `u_procp` names the slot being read and the slot still
  holds the same pid, nonresident. This catches a block that another
  process's image overwrote during the read, which a `p_addr` comparison
  alone misses.
- **Widths.** `uid_t` and `gid_t` are 32-bit `u_int` (`sys/sys/types.h`), and
  the tree keeps `p_uid` at that width on purpose, so the record carries
  32-bit ids. The kernel has no percent-CPU, resident-set or session field, so
  the record carries none.
- **Signal authorization.** `cansignal()` in `sys/kern/kern_sig.c` reads the
  target's real uid through `fill_from_u()`. The missing ring fold therefore
  gives `kill()` a uid read from the wrong flash block, not only a wrong
  `ps` or `w` row.
- **Pool prefix.** `swapram_reserve()` records the user-area segment at
  `USIZE`, so the `DEV_BSIZE` prefix request in `fill_from_u()` is in range.

The owner decided four questions:

- Arguments are visible to the target's owner and to root. Other callers get
  the row with the argument group marked unavailable.
- `KERN_PROC` returns the pointer-free record in place of `struct proc`.
  `w` and the two board tests that read it move to the record in the same
  change.
- The crash-dump path lands as its versioned format header and stubs that
  compile to nothing without SD. The dump device moves off the live flash
  swap. The panic-time writer and `savecore` are a separate series.
- `ps -r` and `include/psout.h` are retired.
