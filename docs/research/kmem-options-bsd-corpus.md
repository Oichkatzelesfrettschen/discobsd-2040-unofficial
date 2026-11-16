# Process-information and kernel-memory mechanisms across the BSD corpus

Status: read-only research. Nothing was built, booted, flashed or run. Every
claim carries a tag: `[R path:line]` read in source, `[M]` measured from an
existing linked object by `arm-none-eabi-nm -S` (no rebuild), `[I]` inferred.
The question: how ordinary users obtain a correct `ps` on the RP2040 port
(a 128 KB kernel, one 192 KB user window, a flash swap and the SwapRAM pool)
while `/dev/mem`, `/dev/kmem` and `/dev/swap` stay out of reach.

Source revisions read (the BSD_Workspace corpus, shallow clones
where noted): 2.11BSD `0a38e59`, 386BSD `9eaf3b33`, 4.4BSD-Lite2 `b10da344`,
LiteBSD `03934b7`, LiteBSD-2 `25b8a3ff`, RetroBSD `10e2a8d`, NetBSD
`1e8e727e461`, OpenBSD `8849c5d46` (single-commit clone: no history, so the
reason OpenBSD `ps` left group kmem is inferred from the tree, not read from a
log), FreeBSD `88e7371d9`, DragonFly `94448f62`, HardenedBSD `521ab3484`,
CheriBSD `88f39900c`, NextBSD `5e199472222`, rtems-libbsd `fbc3c664`. The repo
under study is branch `rp2040/kmem-device-group` at `ae99bb6d`. Paths in the
repo column are repo-relative.

## 1. Baseline in this tree

| Fact | Evidence |
|---|---|
| `ps` is setgid kmem, opens kmem, mem and swap read-only, then calls `setgid(getgid())`; with `-k` or a swap path argument it drops before the caller-named file opens. | `bin/ps/ps.c:638-670` [R] |
| `ps` reads the proc table 8 entries at a time through `/dev/kmem`, then reads each `struct user` from `/dev/mem` (SLOAD) or `/dev/swap` (otherwise). The snapshot is torn across chunks and across the later per-process reads. | `bin/ps/ps.c:901-932`, `:454-478` [R] |
| A short or failed read of the user area drops the row with no message (`return(0)`); a pooled process whose psinfo sysctl fails and whose `p_addr` is 0 also drops the row. The four defects below (torn table, resident user areas outside the memory driver's window, ring-folded swap blocks, raw `b_blkno` in `KERN_PROC`) share one root: user space reconstructs process state from raw memory and swap without modeling the kernel's layout. | `bin/ps/ps.c:470-471`, `:476-477` [R] |
| For a pooled process whose psinfo node is missing, `ps` read block 0 of the swap device and printed a row with an empty name: a wrong row, not a dropped one. | `sys/arch/rp2040/doc/TESTING.md:1745-1750` [R] |
| `ps` reads argument strings from the stack image: top-of-stack word, then up to `2*DEV_BSIZE` bytes; pooled processes get `(comm)` only. | `bin/ps/ps.c:333-436`, `:352`; exec layout `sys/kern/exec_subr.c:241-283` [R] |
| `/dev/mem` and `/dev/kmem` share `mmrw()`; the only I/O-time gate is `badkaddr_span() && baduaddr_span()` (kernel RAM, kernel flash or the calling process's window). No securelevel or credential test exists in the driver. | `sys/dev/mem.c:44-55`; `sys/arch/rp2040/rp2040/machdep.c:993-1003` [R] |
| `mmrw` admits kernel data `[0x20030000, 0x2003E800)`, kernel flash `[0x10000100, 0x10020000)` and the calling process's window; writes pass the same filter, so `securelevel`, `proc[]` and every other kernel global are writable by a root writer. | `sys/dev/mem.c:48`; `sys/arch/rp2040/rp2040/machdep.c:993-1003`; bounds from `arm-none-eabi-nm` of `PICO/unix.elf` (`__kernel_data_start` 0x20030000, `__kernel_data_end` 0x2003E800, `__kernel_flash_start` 0x10000100, `__kernel_flash_end` 0x10020000) [R][M] |
| The two resident user areas, `u0` at 0x2003E800 and `u` at 0x2003F400, lie above `__kernel_data_end` (linker regions `RAM` 58K, then `U0AREA` and `UAREA`), so `mmrw` returns `EFAULT` for them: `badkaddr_span` and `baduaddr_span` are both true. `ps` reads a resident process's user area from `/dev/mem` at `p_addr` (`ps.c:454-457,476`), so the row of every process whose `p_addr` is `u0` or `u`, including `ps` itself and pid 0, always drops. | `sys/arch/rp2040/conf/RP2040.ld:44-60`; `sys/dev/mem.c:48-51`; `bin/ps/ps.c:454-457,476-477` [R]; `p_addr = &u0` after swap-in at `sys/kern/vm_swap.c:609` [R] |
| Swap images sit on a ring: `swap()` and `swap_with_buf_result()` fold a block at or past `m_ring_end` back to `m_ring_first`, `mfree()` folds the same way, and `init_main.c` sets `m_ring_end = nswap`. The image addresses `addr[0..2]` are computed by plain addition (`addr[2] = addr[1] + ...`), so `p_addr` can name a logical block at or past `nswap`. `ps` seeks `/dev/swap` to `p_addr * DEV_BSIZE` (1024) with no fold, so such a read returns nothing and the row drops. The first 972 bytes of the user area stay inside one 1024-byte block, so a seam inside the three-block extent does not split that read; the 2048-byte argument tail read from `saddr` can straddle the seam. | `sys/kern/vm_swp.c:36-41`; `sys/kern/subr_rmap.c:93-100`; `sys/kern/init_main.c:157-159`; `sys/kern/vm_swap.c:226-229,711-713`; `bin/ps/ps.c:460,386-399` [R]; the read-past-end result [I], consistent with `flstrategy` below |
| `fill_from_u` hands `b_blkno = p_addr` straight to `d_strategy`, bypassing the fold in `swap_with_buf_result()`. `flstrategy` answers a block exactly at the partition end as an end-of-file read with no error and `b_resid` set (so the buffer keeps stale bytes that the routine decodes as `ruid`, `ttyd`, `ttyp`), and a block past the end with `EINVAL` (so the routine returns uid -2 and `NODEV`). `KERN_PROC` therefore reports a wrong uid and tty for a wrapped image. | `sys/kern/kern_sysctl.c:906-921,928-934`; `sys/arch/rp2040/dev/flash.c:758-776`; `sys/kern/ufs_bio.c:359-369` [R]; the end-to-end effect on a wrapped image is [I] (not run) |
| `openi()` refuses a write open of `iskmemdev()` devices at securelevel >= 1; read opens are never refused. | `sys/kern/sys_inode.c:595-609`; `sys/arch/rp2040/rp2040/conf.c:287-292` [R] |
| `securelevel` initializes to 0. `init` raises it to 1 on entering multiuser when it reads 0 (`init.c:534-535`) and lowers it to 0 in single-user (`init.c:315-316`), yet `PICOCALC.md:683` says "init never raises it". The two statements contradict; the board state the task reports (0) agrees with the document. | `sys/arch/rp2040/rp2040/machdep.c:144-150`; `sbin/init/init.c:315-316,534-535`; `sys/arch/rp2040/doc/PICOCALC.md:683` [R]. Discriminating measurement: `sysctl kern.securelevel` on a booted image (not run). |
| `KERN_PROC` copies a raw `struct proc` plus a 4-field `eproc` to any caller; `__sysctl` demands root only for a new value. Raw `proc` carries `p_nxt`, `p_tip`, `p_wchan`: kernel pointers. Without MMU randomization this leaks nothing a link map lacks. [I] | `sys/kern/kern_sysctl.c:99,760-851`; `sys/sys/sysctl.h:200-210` [R] |
| `sysctl_doproc` walks `allproc` with a bare cursor while `fill_from_u` may sleep in `geteblk()` and `biowait()`; `memlock` serializes sysctls only, so a fork or exit during the sleep can relink the cursor. [I] | `sys/kern/kern_sysctl.c:760-851,886-945`, `:159-172` [R] |
| `machdep.swapram_psinfo.<pid>` returns a 100-byte pointer-free record (times, sizes, tty, two signal states, 64-byte command) for a pooled process only; it orders the `pfind` after `geteblk` so nothing sleeps between lookup and decode. | `sys/arch/rp2040/rp2040/sysctl.c:213-275,487-490`; `sys/arch/rp2040/include/swapram.h:88-108` [R] |
| `CPU_NLIST` now carries the name in the MIB and takes no new value, so every user may read it. | `sys/arch/rp2040/rp2040/sysctl.c:395`; `sys/kern/kern_sysctl.c:604-640`; `lib/libc/gen/knlist.c` [R] |
| The common state of a user process is swapped out: one 192 KB window means `sched()` exchanges the resident image on every switch between processes that do not fit together. The Renode stage records that init is swapped while `ps` runs. | `sys/kern/vm_sched.c:30-125`; `TESTING.md:1740-1745` [R]; the single-window consequence is [I] |
| `struct user` is 972 bytes at review time; bytes 972-1023 are layout slack, 1024-3071 are the 2048-byte kernel stack that swaps with it. `u_comm` is 64 bytes. | `docs/research/swap-exhaustion/review-03.md:89,166`; `sys/arch/rp2040/include/uarea_stats.h:21`; `sys/kern/subr_capacity.c:34-35`; `sys/sys/user.h:24` [R] (972 not re-measured on this tree) |
| `usr.bin/fstat/fstat.c:90` reads a swapped user area at `p_addr << 9` while `DEV_BSIZE` is 1024 on this port; `w` and `ps` use `DEV_BSIZE`. `fstat` therefore reads the wrong swap offset. | `usr.bin/fstat/fstat.c:90`; `sys/arch/rp2040/include/machparam.h:103`; `usr.bin/w/w.c:446` [R] |
| `vmstat -z` opens `/dev/kmem` read-write to zero the sum counters; `pstat`, `fstat`, `iostat`, `vmstat` and `devupdate` read kmem; `w` uses `KERN_PROC` then `/dev/mem` and `/dev/swap` for user area and arguments. | `usr.bin/vmstat/vmstat.c:166-169`; `usr.sbin/pstat/pstat.c:140-148`; `usr.bin/fstat/fstat.c:507-520`; `usr.sbin/iostat/iostat.c:104`; `usr.sbin/devupdate/devupdate.c:218`; `usr.bin/w/w.c:383-446` [R] |

Existing kernel symbol sizes used as the Thumb-1 yardstick, from
`sys/arch/rp2040/compile/PICO/unix.elf` (PICO has no SwapRAM; PICOCALC has
it) [M]: `__sysctl` 328, `sysctl_doproc` 268, `fill_eproc` 32,
`fill_from_u` 224, `sysctl_nlist` 134, `sysctl_rdstruct` 42,
`swapram_uarea_prefix` 62 (PICOCALC), `swapram_present` 10, `mmrw` 176,
`iskmemdev` 20, `openi` 244, `copyout` 42, `pfind` 36, `suser` 32,
`geteblk` 30, `brelse` 148, `biowait` 60, `uiomove` 104, `cpu_sysctl` 920.
`swapram_psinfo_read` is static and folded into `cpu_sysctl`, so its size is
not separately measured; the instruction count in the source puts it near
200 bytes [I]. All byte estimates below are [I] built from these yardsticks
and need `arm-none-eabi-size` on a rebuilt PICO, PICO_UART and PICOCALC to
become measurements.

## 2. Mechanisms in the corpus

### 2.1 2.11BSD (`211bsd`)

- Evidence: `ps` `bin/ps.c:355-380` (SLOAD picks `mem`, else `swap`; short
  read returns 0), `:836-900` (`getcmd` reads the stack tail), `:719-737`
  (opens kmem, mem, swap); install group `bin/Makefile:39-41,73` (setgid
  kmem, mode 2755); devices `etc/MAKEDEV:54-55` (0640 root:kmem);
  `sys/pdp/mem.c:20-77` (`mmrw` maps physical memory through `mapseg5`;
  `/dev/kmem` is an unchecked `uiomove` at the offset); `sys/sys/kern_sysctl.c:842-960`
  (`sysctl_doproc` and `fill_from_u`, the origin of this tree's copy);
  `sys/sys/sys_inode.c:655-669` (write open of mem/kmem refused at
  securelevel >= 1; disks at >= 2); `:253-255` (securelevel only rises
  unless pid 1) [R].
- Kernel algorithm: `KERN_PROC` copies `struct proc` plus a 4-field `eproc`;
  for tty and real uid the kernel reads the u area itself, from `p_addr`
  when SLOAD, else through `geteblk()` plus the swap strategy routine and
  `biowait()` [R `kern_sysctl.c:974+`]. Arguments are never exported.
- Privilege: group kmem read for the setgid `ps`; root write; securelevel
  gates write opens only.
- Port cost: zero (this is the baseline).

### 2.2 RetroBSD (`retrobsd`), PIC32 without an MMU

- Evidence: `src/cmd/ps.c:223-396` and `:504-555` are the 2.11 design
  verbatim (kmem, mem, swap; `knlist`); `sys/pic32/mem.c:39-49` (`mmrw`
  accepts any span for which `badkaddr && baduaddr` is not both true, reads
  and writes alike); `src/cmd/Makefile:54-56` (`ps iostat vmstat` setgid
  kmem); `sys/kernel/sys_inode.c:588-601` (same securelevel write-open
  gate); `sys/kernel/kern_sysctl.c:94` (`new != NULL && !suser()`),
  `:667-830` (`fill_from_u`, `sysctl_doproc`); `sys/pic32/sysctl.c:321-332`
  (`CPU_NLIST` does `strncmp(newp, name, newlen)` on the user pointer
  `newp` with a caller-controlled `newlen`), `src/libc/gen/knlist.c:14-28`
  (passes the name as the new value) [R].
- Algorithm and privilege: as 2.11BSD. Because `knlist` sends the name as a
  new value, only root resolves symbols; the unchecked `strncmp` on `newp`
  is the user-pointer dereference this tree already removed [R for the
  code; I for the consequence].
- Port cost: zero. RetroBSD offers no process-information syscall; its
  answer to "no MMU" is the kernel-memory device.

### 2.3 LiteBSD and LiteBSD-2 (`LiteBSD*`), PIC32MZ with a TLB

- Evidence: `LiteBSD-2/sys/mips/pic32/mem.c:57-143` (`/dev/kmem` accepts
  uncached RAM windows, text below `_etext`, and KSEG2 through `kernacc()`;
  writes permitted); `sys/miscfs/specfs/spec_vnops.c:156-178` (securelevel >=
  1 refuses write open of mem/kmem, exempting `FSCRED`);
  `sys/mips/pic32/conf.c:296-303` (`iskmemdev`); `etc/rootfs.manifest:55-63`
  (`/dev/mem`, `/dev/kmem` 0640 group kmem); `bin/ps/ps.c:259-263,284-300,
  384-407` and `bin/ps/Makefile:8-9` (setgid kmem mode 2555); `lib/libkvm/
  kvm_proc.c:344-419` (`kvm_getprocs` uses `sysctl(KERN_PROC)` when live),
  `:454-579,600-706` (`kvm_argv`/`kvm_uread` read the target's stack through
  `/dev/mem` after a page-table walk, falling to `kvm_readswap` that chases
  the VM map, object chain and swap pager through `/dev/kmem` into `/dev/
  swap`), `:581-598` (`proc_verify` re-reads the proc to confirm the pid
  survived), `lib/libkvm/kvm_mips.c:168-207` (`_kvm_uvatop` walks the segment
  table through `kvm_read`) [R].
- Algorithm: the kernel exports `kinfo_proc` (raw `struct proc`, `eproc`,
  credentials, `vmspace`); user space reconstructs times and arguments by
  reading kernel and user pages itself.
- Privilege: setgid kmem for the whole run; `ps` drops the group only when
  an alternate core or swap file is named.
- Pattern worth keeping: `saveuser()` leaves the user-area fields zero
  with `u_valid = 0` when the read fails instead of dropping the row
  (`4.4BSD-Lite2/bin/ps/ps.c:376-397`) [R].
- Port cost: `kvm_readswap` has no analogue here (no VM objects); the
  design reduces to the 2.11 one. Zero kernel bytes, no gain.

### 2.4 4.4BSD-Lite2 (`4.4BSD-Lite2`)

- Evidence: `sys/kern/kern_sysctl.c:79-172` (`__sysctl`: root required for a
  new value; user buffer checked with `useracc` and wired with `vslock`
  under `memlock`, so `copyout` cannot fault while the process list is
  walked), `:555-703` (`sysctl_doproc`, `fill_eproc`: copies `proc` and an
  eproc holding `pcred`, `ucred`, `vmspace`, tty, wmesg), `:219-225`
  (securelevel only rises unless pid 1); `sys/miscfs/specfs/spec_vnops.c:
  156-178` (open-time write refusal at securelevel >= 1); `sys/i386/i386/
  mem.c:103` and `sys/hp300/hp300/mem.c:107` (`kernacc` is the only I/O
  check; no securelevel); `bin/ps/ps.c:259-265` (drops setgid only for
  alternate files); `bin/ps/Makefile:8-9`; `lib/libkvm/kvm.c:195-223`
  (live `kvm_open` opens `/dev/mem`, `/dev/kmem`, swap); `lib/libkvm/
  kvm_proc.c:343,580,634,655` [R].
- Algorithm: kernel snapshot of the process list under the sysctl lock with
  wired user memory; per-process arguments are user-space reads
  (section 2.3).
- Privilege: group kmem; securelevel refuses write opens only.
- Port cost: already present as `sysctl_doproc` (524 bytes with
  `fill_eproc` and `fill_from_u`) [M].
- Shared invariant: the user buffer is wired before the walk so the walk
  never faults [R `kern_sysctl.c:142-152`]. DiscoBSD has no VM; its
  `copyout` bounds-checks against the window (`baduaddr_span`), which gives
  the same no-fault property [I].

### 2.5 NetBSD (`netbsd-src`): KERN_PROC2 and KERN_PROC_ARGS

- Evidence: `sys/kern/kern_proc.c:2070-2322` (`sysctl_doeproc`: for
  `KERN_PROC2` the caller supplies `elem_size` and `elem_count` in the MIB
  (`:2116-2121`), the kernel fills one zeroed `kinfo_proc2` at a time into a
  `kmem_zalloc` buffer (`:2127`), takes `proc_lock` and `p_lock` per
  process (`:2131,2156`), skips processes the credential may not see via
  `kauth_authorize_process(... CANSEE_EPROC)` (`:2157-2163`), drops the
  locks only around `sysctl_copyout` and keeps its place with a list marker
  (`:2243,2259-2286`), and walks zombies first so a dying process is never
  reported twice (`:2132-2137`)); `fill_kproc2` `:2769-2900` (pointers
  masked by `COND_SET_VALUE(..., allowaddr)`, `allowaddr =
  get_expose_address(curproc)` at `:2082`; command from `p_comm`, ticks from
  `p_uticks`/`p_sticks`, `p_flag = L_INMEM` with the comment "Process never
  swapped out" at `:2796`); `sysctl_kern_proc_args` `:2354-2468` and
  `copy_procargs` `:2471-2618` (pid lookup, `CANSEE_ARGS` allowed to all,
  `CANSEE_ENV` only to the owner at `:297-306`; zombies and system
  processes refused with `EINVAL` at `:2431-2435`; `rw_tryenter(p_reflock)`
  else `EBUSY` at `:2437`; the kernel takes a reference to the target's
  `vmspace` (`:2494-2495`) and reads argv and strings with `uvm_io`
  (`:2580`), so a paged-out page is brought in by the pager, never by the
  caller); `:308-317` (`CANSEE_KPTR` gated by `kern.expose_address`, level 1
  requires the process to have opened `/dev/kmem`, flag `PK_KMEM` set at
  `sys/dev/mm.c:94-105`); `bin/ps/ps.c:406-409` (`KVM_NO_FILES` live path);
  `bin/ps/Makefile:1-10` (no `BINGRP`); `lib/libkvm/kvm_proc.c:468-471`,
  `:1090-1161` (`kvm_doargv2` builds argv from the `KERN_PROC_ARGS` bytes);
  `sys/secmodel/securelevel/secmodel_securelevel.c:515-542` (securelevel > 0
  denies RAWIO write on `iskmemvp` vnodes; read is not denied), defined by
  `sys/miscfs/specfs/spec_vnops.c:185-207`; `sys/kern/vfs_mount.c:1485` (a
  mount on kmem is `EINVAL`); `sys/dev/mm.c:237-289` (`dev_kmem_readwrite`
  checks every page with `uvm_kernacc`) [R].
- Kernel algorithm: copy a fixed, versioned record under the process lock;
  mask kernel addresses unless the policy allows; arguments are read
  through the target's address space by the kernel, which faults pages in
  from swap; zombies and system processes refused.
- Privilege: no setgid, no device; visibility through kauth.
- Port cost: record fill about 180-260 bytes (the field count in
  `fill_kproc2` maps onto roughly 25 loads and stores plus a 64-byte
  command copy), `elem_size` handling 40-70 bytes [I]. `KERN_PROC_ARGS`
  needs the target's stack bytes; see section 3 for the three residency
  cases (about 400-550 bytes) [I].

### 2.6 OpenBSD (`openbsd-src`)

- Evidence: `sys/kern/kern_sysctl.c:1909-2055` (`sysctl_doproc`: `namelen ==
  4` with `name[2] = elem_size`, `name[3] = elem_count`; `show_pointers =
  suser(curproc) == 0` at `:1935`; `KERN_PROC_SHOW_THREADS` is an op flag
  OR'd into `name[0]` at `:1932-1933`; `fill_kproc(...)` then `copyout` of
  `elem_size` bytes; an `ENOMEM` return when the buffer is short), `:2061-
  2146` and `FILL_KPROC` in `sys/sys/sysctl.h:593-710` (memset the record,
  assign pointers only under `show_addresses`), `:2149-2365`
  (`sysctl_proc_args`: `prfind`, refuses `PS_SYSTEM|PS_EXITING` with `EINVAL`
  and `PS_INEXEC` with `EBUSY`, `ENV` only for the owner or root, reference
  on `ps_vmspace`, reads `ps_strings`, argv and each string with `uvm_io`
  and builds a reader-side argv in the caller's buffer); `kern_pledge.c:
  892-909` (the `ps` pledge admits exactly `kern.proc.*`, `kern.procargs.*`,
  `kern.proc_cwd.*`, `kern.ccpu`, `vm.maxslp`); `bin/ps/Makefile`,
  `bin/ps/ps.c:291-294,312-313` (`KVM_NO_FILES`, `pledge("stdio rpath getpw
  ps")`); `lib/libkvm/kvm.c:206-209` (`KVM_NO_FILES` sets `alive` without
  opening any file); `sys/arch/amd64/amd64/mem.c:80-111` (`mmopen` fails
  with `EPERM` for minor 0 and 1 whenever `securelevel > 0` and `allowkmem`
  is 0, read open included), `:150-162` (kmem write below `etext` refused at
  I/O time with `EFAULT`); `sys/kern/kern_sysctl.c:143,605-607,1203-1251`
  (`kern.allowkmem` is a plain int that becomes read-only when securelevel
  is above 0; `sysctl_securelevel` lowers only for pid 1 with a CAS loop);
  `sys/kern/spec_vnops.c:122-143` (the same write-open check that
  4.4BSD has, kept alongside); `etc/mtree/special:11-12` (`kmem`, `mem`
  0640 root:kmem); `etc/examples/sysctl.conf:29` and `usr.sbin/procmap/
  procmap.1:61` (the tools that still need kmem name the knob); `usr.bin/
  fstat/fstat.c:211-291` and `kern_sysctl.c:1679-1707` (`KERN_FILE` over
  `kinfo_file`, also `show_pointers = suser`); `usr.bin/top/machine.c:151-273`
  (sysctl only) [R].
- Why `ps` is not setgid kmem: the tree contains no setgid directive in
  `bin/ps/Makefile`, the live path opens no file, and the kernel answers
  list and arguments. The causal history is not in this clone [I].
- Kernel algorithm: same record pattern as NetBSD plus a stricter default:
  addresses masked unless root; the `ps` pledge limits the program to the
  records it needs.
- Privilege: `/dev/mem` and `/dev/kmem` stay 0640 root:kmem for the tools
  that remain (`procmap`, `pstat`), and the kernel refuses to open them at
  all above securelevel 0 unless the administrator opts in through
  `kern.allowkmem`.
- Port cost: record plus `show_pointers` 270-390 bytes [I]; `mmopen`
  replacing the shared `nullopen` for major 1 at about 36-48 bytes plus
  `allowkmem` (4 bytes bss, about 20 bytes of `kern_sysctl` case) [I].
  `nullopen` is shared by `/dev/null` and `/dev/zero` here
  (`sys/arch/rp2040/rp2040/conf.c:175-179` [R]), so the replacement must
  switch on minor.

### 2.7 FreeBSD (`freebsd-src`) and derivatives

- Evidence: `bin/ps/ps.c:243,481` (`memf = _PATH_DEVNULL` makes `kvm_openfiles`
  sysctl-only; `lib/libkvm/kvm.c:160-162` returns after opening `/dev/null`);
  `bin/ps/Makefile` (no `BINGRP`); `sys/kern/kern_proc.c:2137-2217`
  (`sysctl_kern_proc_args`: first, a single-threaded caller querying itself
  reads `p_args` without locks (`:2160-2162`); else `pget(pid, PGET_CANSEE)`,
  then if `p->p_args != NULL` the kernel copies the cached `struct pargs`
  (`:2171-2176`, reference held with `pargs_hold`), else `_PHOLD` and
  `proc_getargv()` reads `ps_strings` through the vmspace
  (`:2177-2187`; `:1841-1860` `proc_read_string`)); `sys/kern/kern_exec.c:
  840-848,996-998` (at exec time, `i = begin_envv - begin_argv` bytes of
  argv are copied into a `pargs` when `ps_arg_cache_limit >= i +
  sizeof(struct pargs)`, default `PAGE_SIZE/16` per `kern_exec.c:144`; the
  new cache replaces the old under the proc lock); `sys/sys/proc.h:122-126`
  (`struct pargs {ref, length, args[]}`); `sys/kern/kern_proc.c:1096`
  (`ki_args = p->p_args`); `sys/fs/procfs/procfs_status.c:163-189`
  (`cmdline` serves the cache, falls through to `proc_getargv`, deliberately
  returns an empty file for a vanished argv); `sys/fs/procfs/procfs.c:164-189`
  (a `pfs` directory tree per pid); `sys/dev/mem/memdev.c:72-89` (`memopen`:
  `priv_check(PRIV_KMEM_READ)` for read; `PRIV_KMEM_WRITE` plus
  `securelevel_gt(cred, 0)` for write), `sys/kern/kern_priv.c:240-250`
  (PRIV_KMEM_READ is allowed to every credential that passes file
  permissions); `sys/kern/kern_proc.c:2160-2176` (`PGET_CANSEE` is the
  visibility policy) [R].
- HardenedBSD `sys/dev/mem/memdev.c:72-89`, CheriBSD
  `sys/dev/mem/memdev.c:94-99` and NextBSD `sys/dev/mem/memdev.c:72-77`
  carry the same `memopen` as FreeBSD; CheriBSD and NextBSD also keep
  `sysctl_kern_proc_args` (`cheribsd/sys/kern/kern_proc.c:2383`,
  `NextBSD/sys/kern/kern_proc.c:1830`) [R].
- Kernel algorithm: a per-process, kernel-owned copy of the exec-time
  argument bytes (`p_args`) answers `kern.proc.args` without reading the
  target's memory; the reader path exists only for a process whose cache
  was too big or cleared. No swapped-image read is needed in the common
  case.
- Privilege: no setgid; `kern.proc.*` through `p_cansee`; kmem write needs
  `PRIV_KMEM_WRITE` and securelevel 0 at open.
- Port cost: exec-time copy 40-70 bytes plus a field in `struct user` (see
  section 3); `procfs` needs a pseudo-filesystem and vnode glue that this
  kernel lacks, more than 2 KB [I].

### 2.8 DragonFly (`DragonFlyBSD`)

- Evidence: `sys/kern/kern_proc.c:1866-1951` (`sysctl_kern_proc_args`:
  `p_trespass` visibility unless `ps_argsopen`; title from a user-mapped
  `proc_title`, else the `p_args` exec-time copy; no fallback that reads the
  target's address space); `sys/kern/kern_memio.c:141-160` (`mmopen` for
  minor 0 and 1: restricted-root capability, write open refused if
  `securelevel > 0 || kernel_mem_readonly`), `sys/kern/kern_mib.c:255`
  (`kernel_mem_readonly`, a boot-time global that also gates `kern_linker.c:
  317,486`); `bin/ps/Makefile:15-16` (`BINGRP=kmem` and mode commented out),
  `bin/ps/ps.c:332-336` (drops setgid for alternate files) [R].
- Kernel algorithm and privilege: arguments are exactly the exec-time
  snapshot or a reader-set title; absence yields an empty result. Kernel
  memory write opens are blocked by either securelevel or a dedicated
  switch.
- Port cost: `kernel_mem_readonly` as a compile-time or boot-time constant
  about 8-30 bytes [I].

### 2.9 386BSD (`386bsd`)

- Evidence: `usr/src/bin/ps/ps.c:248-356` (`kvm_openfiles`, `kvm_getprocs`,
  `kvm_getu`, `kvm_getargs`), `usr/src/bin/ps/Makefile` (`BINGRP=kmem`,
  mode 2555), `usr/src/kernel/mem/mem.c:64-120` (`/dev/kmem` guarded by
  `vmspace_access(&kernspace, ...)`, write permitted); `grep -rl securelevel`
  over the tree finds nothing, so no securelevel exists at all [R].
- Port cost: not applicable; this is the weakest model in the corpus and
  the one this tree has already exceeded.

### 2.10 rtems-libbsd, NextBSD and the remaining forks

- rtems-libbsd imports the FreeBSD `user.h` and `sysctl.h` headers but no
  `kern_proc.c` (`freebsd/sys/kern` lists `rtems-legacy-newproc.c` only) and
  no memory device; it has no process-information mechanism to port [R
  directory listing; I conclusion].
- midnightbsd, opnsense and pfsense trees share the FreeBSD mechanism
  (`memdev.c`, `kern.proc.args`); they were not re-read line by line [I].

### 2.11 Sibling tools in the corpus

| Tool | 2.11 family (this tree) | OpenBSD | NetBSD | FreeBSD |
|---|---|---|---|---|
| `w` | `KERN_PROC` list, then `/dev/mem` and `/dev/swap` for user area and arguments (`usr.bin/w/w.c:383-446`) [R] | `KVM_NO_FILES` when no core named (`usr.bin/w/w.c:165-169`) [R] | kvm over sysctl [I] | kvm with `memf` default (`usr.bin/w/w.c:195`) [R] |
| `fstat` | kmem, mem, swap (`usr.bin/fstat/fstat.c:507-520`) [R] | `KERN_FILE` per pid or per uid (`fstat.c:211-291`; `kern_sysctl.c:1679-1707`) [R] | `kern.file2` (`sys/kern/kern_descrip.c:2236`) [R] | `kern.proc.filedesc` [I] |
| `pstat` | kmem for proc, inode, file, tty, swapmap tables (`usr.sbin/pstat/pstat.c:140-148`) [R] | still kmem; documents `kern.allowkmem` (`pstat.8:81,344`) [R] | sysctl tables [I] | kvm and sysctl [I] |
| `vmstat`, `iostat` | kmem counters; `vmstat -z` writes kmem (`vmstat.c:166-169`) [R] | `vm.uvmexp`, `kern.cptime`, `hw.diskstats` sysctls (`kern_pledge.c:871-922`) [R] | `vm.uvmexp2`, `hw.iostats` [I] | `kern.cp_time`, devstat [I] |
| `top` | not shipped | sysctl only (`usr.bin/top/machine.c:151-273`) [R] | kvm over sysctl [I] | `kvm_open` on `/dev/null` (`usr.bin/top/machine.c:383`) [R] |

## 3. Residency cases and what each mechanism does with them

| Residency of the target | Record fields (comm, times, tty) | Argument strings |
|---|---|---|
| Resident in the window | direct read of `p_addr` (u area is addressable by the kernel) `[R kern_sysctl.c:906-909]` | the stack is at `USER_TOP` in the same address space (`exec_subr.c:253-254`), so a bounded `copyout`-style read of the top block works `[R]`; about 80 bytes `[I]` |
| Flash-swapped | one `DEV_BSIZE` block at `p_addr` through `geteblk()`, the swap strategy routine and `biowait()` `[R kern_sysctl.c:906-921]` | the last one or two blocks of the stack segment at `p_saddr`; the same strategy path with a block offset parameter, about 60-100 bytes `[I]`; arguments may span up to `ARG_MAX` = 5120 bytes (`sys/sys/syslimits.h:36`) `[R]`, so a bounded tail must define truncation |
| SwapRAM-pooled (`p_addr` 0) | `swapram_uarea_prefix()` decodes the u-area prefix without dismantling the image `[R swapram.c:482-505]` | the stack segment is a separate heatshrink stream (`SWAPRAM_STACK`, `swapram.h:66-69`); the tail needs a full decode of the stack stream into a window and a tail ring, a new function of about 150-250 bytes `[I]`; no existing code exports it |
| Zombie, system, exec-in-progress | NetBSD and OpenBSD refuse (`EINVAL`) or report no vm; `fill_from_u` returns `-2`/`NODEV` for a zombie `[R kern_sysctl.c:895-899]` | refused or empty (NetBSD `kern_proc.c:2431-2435`; OpenBSD `kern_sysctl.c:2193-2199`; FreeBSD `kern_proc.c:2177`) |

Mechanisms that avoid every swapped-image read for arguments: the FreeBSD
and DragonFly exec-time cache (`p_args`), and, on this port, a snapshot in
`struct user` (section 5).

## 4. Invariants the corpus shares

1. The kernel copies a fixed-size, versioned, pointer-masked record; user
   space never receives raw kernel structures. NetBSD `fill_kproc2`
   (`kern_proc.c:2769`), OpenBSD `FILL_KPROC` (`sysctl.h:593`). The 2.11
   and 4.4BSD `KERN_PROC` is the lone exception and is the legacy shape
   `w` still consumes.
2. The caller names the record size (`elem_size`, `elem_count`), the kernel
   returns at most that, and `ENOMEM` reports a short buffer. NetBSD
   `kern_proc.c:2116-2121`, OpenBSD `kern_sysctl.c:1924-1930`.
3. A process-list walk either holds a lock that excludes fork and exit, or
   keeps a marker so a relink cannot skip or duplicate (NetBSD
   `kern_proc.c:2131,2243,2285`), and walks zombies first (`:2136`). The
   4.4BSD tree wires the user buffer first (`kern_sysctl.c:142-152`) so
   `copyout` never sleeps inside the walk.
4. Arguments come from one of three places: the target's address space read
   by the kernel (NetBSD, OpenBSD), an exec-time kernel copy (FreeBSD,
   DragonFly), or nowhere (DragonFly with no cache). No mechanism lets the
   caller read the target's image.
5. A zombie, a system process and a process inside exec are refused or
   reported empty (NetBSD `:2431,2437`; OpenBSD `:2193-2198`).
6. Environment strings are private to owner and root; argument strings are
   world-readable by default (NetBSD `:297-306`; OpenBSD `:2201-2205`).
7. Kernel addresses in records are masked unless the caller is root or the
   policy allows (NetBSD `allowaddr`; OpenBSD `show_pointers`).
8. A failed read of per-process detail degrades the row (zero the fields
   and mark them invalid, as `saveuser` does at `4.4BSD-Lite2/bin/ps/ps.c:
   376-397`; FreeBSD procfs returns an empty `cmdline`) instead of dropping
   it.
9. `/dev/mem` and `/dev/kmem` open-time rules: write open refused at
   securelevel >= 1 (2.11BSD, RetroBSD, DiscoBSD `sys_inode.c`; 4.4BSD,
   LiteBSD, OpenBSD `spec_vnops.c`; FreeBSD, HardenedBSD, CheriBSD,
   NextBSD `memdev.c`; DragonFly `kern_memio.c`; NetBSD `secmodel_securelevel.c`).
   OpenBSD also refuses read open above securelevel 0 unless
   `kern.allowkmem`. No tree in the corpus checks securelevel at write
   time; a descriptor opened for write at securelevel 0 keeps writing after
   the level rises. [R for every cited line; absence of a write-time check
   from a `grep securelevel` over every `mem.c`, `mm.c`, `memdev.c` and
   `kern_memio.c`, whose hits are the open routines listed here.]
10. securelevel only rises, and only pid 1 may lower it (2.11BSD
    `kern_sysctl.c:253-255`, 4.4BSD `:219-225`, OpenBSD `:1203-1239`, DiscoBSD
    `kern_sysctl.c:249-257`).
11. Tools that still need raw memory are named and kept off the default
    path (OpenBSD `procmap`, `pstat`, `acpidump` manuals reference
    `kern.allowkmem`; its `ps`, `fstat` list, `top`, `w` and `systat` run on
    sysctl).

## 5. Options for this port

Evidence classes: the byte estimates are [I]; the security and correctness
statements are [R] from sections 1 to 3.

| Option | What it is | Security boundary | Swapped and pooled correctness (no silently dropped rows) | Kernel bytes (Thumb-1, [I]) |
|---|---|---|---|---|
| A. Status quo plus the pool record | setgid kmem `ps`; `machdep.swapram_psinfo` for pooled rows | Weak: a `ps` defect reads all kernel RAM; kmem group member reads and root writes | Torn table, stale `p_addr`, short read and `ESRCH` drop or mislabel rows; pooled arguments absent (`ps.c:470-477`) | 0 (baseline, with `swapram_psinfo_read` about 200 bytes) |
| B. 4.4BSD kvm style (`KERN_PROC` plus `kvm_read`) | adds nothing the tree lacks | same as A | same as A | 0 |
| C. Record sysctl for every residency state (NetBSD `KERN_PROC2` shape, OpenBSD op flag) with comm only | one kernel call returns the fixed record per process; `ps` unprivileged | Good: no device, no group; addresses never exported | The kernel picks the source under its own rules and a failed detail degrades the row; ESRCH only when the pid is gone | +60 to +200 net (record 180-260, op handling 40-110, minus the removed psinfo node about 200 and shared `fill_from_u` prefix code) |
| D. C plus an exec-time argument snapshot in `struct user` (FreeBSD `p_args` shape) | `u_psargs[48]` set in `exec_setupstack`, copied out with the record | as C | Arguments for resident, flash-swapped and pooled processes through the u-area prefix the record already fetches; constant time; no stack read | C plus 50 to 100 (exec hook 40-70, record 24); `struct user` +48 bytes inside the 52-byte slack, 0 flash |
| E. C plus kernel-read stack arguments (NetBSD `copy_procargs` shape) | resident window read, flash stack-tail block read, pooled stack decode | as C | Complete, but the pooled tail decodes the whole stack stream | C plus 400 to 550 |
| F. D plus OpenBSD-style device denial | `mmopen` refuses kmem read and write above securelevel 0 unless `kern.allowkmem` | Best: the devices are closed in multiuser | as D | D plus 60 to 70 |
| G. FreeBSD procfs `cmdline` | pseudo-filesystem | Good | Good | > 2000 (needs a pseudo-filesystem and vnode glue) |
| H. Device hardening alone (write-time securelevel check in `mmrw`, `init` raises the level) | no new interface | Moderate: root-only writes cannot survive a securelevel raise through an old descriptor | none | +14 to +24 (`mmrw`), 0 for `init` |

The `mmrw` write-time check is a new design: no tree in the corpus
performs it (invariant 9). Reasoning: `uio->uio_rw` is available in
`mmrw`, `securelevel` is a global; the guard is a load, a compare, a branch
and a return, 14-24 bytes [I].

### Ranking by (security boundary, correctness for swapped and pooled, kernel bytes)

1. Option D with Option H: the record sysctl for every residency state, the
   exec-time argument snapshot, an unprivileged `ps`, the `mmrw`
   write-time check, `init` at securelevel 1. Boundary: no non-root path
   to kernel memory for `ps`; correctness: one kernel request per snapshot,
   every residency state, no stack read; cost +110 to +320 bytes gross,
   +60 to +200 net of removing `swapram_psinfo_read` [I].
2. Option F (D plus H plus `mmopen`/`allowkmem`): the strongest boundary,
   +60 to +70 bytes, but `pstat`, `fstat`, `vmstat`, `iostat` and `devupdate`
   all open kmem and break above securelevel 0 until each has a sysctl
   replacement or the administrator opts in. Adopt only after those tools
   migrate or the operator accepts single-user-only diagnostics.
3. Option E with H: complete arguments with no struct growth, but +400 to
   +550 bytes and a pooled decode path that no caller exercises today;
   choose it only if the exec-time snapshot proves too short (the exec
   copy is bounded at 48 bytes against the 63 bytes `ps` prints today,
   `ps.c:350`).
4. Option C with H (command name only): cheapest of the record designs, but
   a swapped process (the common state) shows `(comm)` where an unswapped
   one shows arguments.
5. Option H alone: cheap and strictly better than A, but `ps` still needs
   the group and keeps its races.
6. Options A and B: the current tree.
7. Option G: budget impossible.

### The optimal combination

Option D with Option H: the elements and their sources are

- `KERN_PROC_PSINFO` as an op flag OR'd into the existing `KERN_PROC_*`
  selector (OpenBSD `KERN_PROC_SHOW_THREADS` precedent,
  `kern_sysctl.c:1932-1933`) so the existing filters (`ALL`, `PID`, `TTY`,
  `UID`) apply; the MIB is `{CTL_KERN, KERN_PROC, op|flag, arg}` with the
  record length chosen by the caller (NetBSD `elem_size`). `kinfo_proc`
  stays as it is for `w` and the capacity tests.
- The record holds only: pid, ppid, uid, ruid, pgrp, stat, flag, cpu, pri,
  nice, a wait-channel address masked for non-root, window sizes, tty
  device, four time values, two signal states, the 64-byte command and the
  48-byte argument snapshot. No pointer.
- One fill routine shared with `fill_from_u`: resident (the kernel reads
  `p_addr` itself, so the `u0`/`u` addresses outside the memory driver's
  window never matter), flash (`geteblk` plus `swap_with_buf_result()`,
  declared at `sys/sys/proc.h:228`, which applies the ring fold that raw
  `d_strategy` skips; replaces the six field stores, the strategy call and
  `biowait` in `fill_from_u`, so the byte cost is neutral or slightly
  negative [I]) or pooled (`swapram_uarea_prefix`). After
  a flash read the routine re-checks `pfind(pid) == p`, `p_addr` and
  `SLOAD` and retries once through the resident path when the process
  swapped in meanwhile, which closes the stale-`p_addr` window [I]. A
  failed read yields the proc-table fields with the u-area fields zero and
  a flag, never an absent row (invariant 8). `ESRCH` only for a vanished
  pid.
- `exec_setupstack` copies the first 48 bytes of space-joined argv into
  `u_psargs` after the existing copy loop (`exec_subr.c:289-300`) and
  `fork` already copies the u area to the child, so the child shows its
  parent's arguments until it execs, as the stack does.
- `ps` stops opening kmem, mem and swap; it is installed without setgid.
  The devices stay 0640 root:kmem for root tools.
- `mmrw` refuses `UIO_WRITE` on minors 0 and 1 when `securelevel > 0`;
  `init` raising the level to 1 in `multiple()` (`init.c:534-535`) takes
  effect after the contradiction with `PICOCALC.md:683` is resolved by
  measurement.

#### Blast radius (files this repository would touch)

Kernel and headers:
- `sys/sys/sysctl.h` (op flag, record type)
- `sys/kern/kern_sysctl.c` (`sysctl_doproc` branch, new fill routine,
  `fill_from_u` sharing and revalidation)
- `sys/sys/user.h` (`u_psargs[48]` in the 52-byte slack; keeps `u_stack`
  last; `sys/kern/subr_capacity.c:34-35` already asserts the bound)
- `sys/kern/exec_subr.c` (`exec_setupstack`)
- `sys/dev/mem.c` (write-time check)
- `sys/arch/rp2040/rp2040/sysctl.c` (remove `swapram_psinfo_read` and the
  `CPU_SWAPRAM_PSINFO` case)
- `sys/arch/rp2040/include/cpu.h` (retire number 43)
- `sys/arch/rp2040/include/swapram.h` (retire `struct swapram_psinfo`)
- no change to `sys/arch/rp2040/rp2040/swapram.c`; STM32 inherits the
  generic op and needs no edit beyond the shared header

Userland and distribution:
- `bin/ps/ps.c` (drop `openfiles`, the swap and stack paths, `getcmd`;
  decide the fate of `-k`), `include/psout.h` if the field set changes
- `usr.bin/w/w.c` (migrate to the record, drop `/dev/mem` and `/dev/swap`)
- `usr.bin/fstat/fstat.c:90` (`<<9` against `DEV_BSIZE`, independent fix)
- `distrib/rp2040/mi.rp2040` (`/bin/ps` mode from 02751 to 0755; comment at
  `:59-62`), `tools/check_rp2040_account_image.py` (expected modes)
- `sbin/init/init.c` or `etc/rc.rp2040` only if the securelevel
  measurement shows the board at 0 when multiuser

Tests and documents:
- `tests/kernel/sysctl_test.c` (record, filters, short buffer, ESRCH,
  zombie, pooled and flash cases, with mutants), `tests/libc_sysctl/`
  if the userland wrapper changes
- `tools/renode/check-boot.py` and `sys/arch/rp2040/doc/TESTING.md:1735-1760`
  (the `ps -ax` stage requires init's arguments, not only `(init)`)
- `docs/research/ps-kernel-memory-boundary-review.md` (outcome of the
  accepted option), `share/man/man3/sysctl.3`
- `tools/c17/source-denominator.json` (refresh before `logged-check`,
  per the repository's C17 ledger rule)
- not touched: `sys/kern/sys_inode.c` (open-time rule stays),
  `sys/arch/rp2040/rp2040/conf.c` (unless Option F adds `mmopen`)

### Predictions and falsifiers (state before running)

- Size: the PICO, PICO_UART and PICOCALC kernels grow by 60-200 bytes net
  in `text` (`arm-none-eabi-size`); a delta above +320 on PICO falsifies the
  estimate and sends the work back to Option C plus a shorter record.
- Layout: `sizeof(struct user)` stays at or below 1024 and the capacity
  assertion holds; a build failure on `subr_capacity.c:34-35` means the
  48-byte snapshot does not fit and the cold fields of `struct user` must
  move before the stack (`docs/research/kernel-footprint-handoff.md`).
- Correctness: `ps -ax` repeated under swap churn (four CPU-bound children
  exchanging through the window and the pool) never loses a row against
  `kern.proc` count; the current `ps` is the known-bad input and must show
  drops or empty names. Not run.
- Boundary: operator opens of the three devices fail with `EACCES` and the
  stage run as operator shows rows with arguments for a swapped init; an
  image with `ps` still setgid is the known-bad input.
- Write-time check: open `/dev/kmem` for write at securelevel 0, raise the
  level to 1, write: `EPERM`. The unpatched `mmrw` is the known-bad input.

### Open decisions for the owner

1. Snapshot length: 48 bytes fits the 52-byte slack; 63 bytes matches what
   `ps` prints today but requires moving 12 bytes of cold fields.
2. Whether Option F's device denial ships before the sibling tools
   (`pstat`, `fstat`, `vmstat`, `iostat`, `devupdate`) have sysctl
   replacements; each replacement costs about 20-60 bytes per node [I],
   as one `cpu_sysctl` case with `sysctl_rdstruct` costs.
3. Whether `-k corefile` survives in `ps`; with no kmem group it would read
   the file with the caller's permissions only.
4. The `init` securelevel contradiction (section 1).

## 6. Defects found while reading (independent of the choice)

- Resident user areas `u0` (0x2003E800) and `u` (0x2003F400) are outside the
  `mmrw` window, so `ps`, `w` and `fstat` always lose the rows of resident
  processes; ring-folded swap blocks make `ps`, `w` and `fstat` read past the
  swap device; `fill_from_u` reads the raw block. Section 1 gives the lines.
  Every user-space reader of the same raw memory shares these, so a fix
  inside one of them leaves the others broken.
- `usr.bin/fstat/fstat.c:90` shifts by 9 where `DEV_BSIZE` is 1024 [R].
- `fstat` and `w` read swap block 0 for a pooled process (`p_addr == 0`) with
  no SwapRAM branch (`fstat.c:90`, `w.c:446`) [R]; `ps` guards it
  (`ps.c:470-471`).
- `sysctl_doproc` sleeps inside `fill_from_u` with a bare list cursor
  (`kern_sysctl.c:760-851`, `:906-921`) [R]; the consequence on a relink is
  [I].
- `PICOCALC.md:683` and `init.c:534-535` disagree on whether `init` raises
  the securelevel [R].

## 7. Falsification search

Claim under test: "No BSD or relative has met and solved this class: a
user-space reader of raw kernel memory or swap diverging from a kernel
layout it does not model (swap layout, address translation, regions the
memory driver refuses)."

Falsification criterion, fixed before the search: the claim fails if any
tree in the corpus, in source or in history, shows (a) a user-space reader
that handles a swapped process correctly by modeling the kernel's swap
layout, (b) a `ps` that keeps a row whose raw read failed, (c) a relative
whose resident user area lay outside the readable window and was fixed, or
(d) a commit that moved `ps` or another kmem reader off kernel memory
because the reader drifted from the kernel.

Verdict: the claim fails as written and survives only in one narrower form.
Criteria (b), (c) and (d) have hits; criterion (a) has none, and its
nearest hit (NetBSD `_kvm_ureadm`) is a counterexample in the other
direction. What no tree in the corpus shows is this port's compound: a swap
ring that folds block numbers together with resident user areas outside the
memory driver's window. The narrower claim that survives: no tree solved
the class by modeling the kernel layout in user space; every solution moved
the reader into the kernel, refused the read, or marked the row.

### Hits

| # | Tree | Evidence | What it shows | Criterion |
|---|---|---|---|---|
| 1 | NetBSD | commit `6bf3264a901` (2000-05-26): "Add some new sysctls to help abolish the dreaded 'proc size mismatch' errors from ps(1) and some other kernel grovellers ... KERN_PROC2 - return an array of fixed sized struct kinfo_proc2 ... takes the size of each element ... KERN_PROC_ARGS"; `a0d23674fb5` (`KVM_NO_FILES`, `kvm_getproc2` and `kvm_getargv2` over sysctl); `a6b1354960d` (`bin/ps` "Use new sysctl/kvm interfaces ... Also remove the sgid bit - all live kernel data is accessed through the sysctl interface") [R, `git -C netbsd-src show`] | The drift class (reader and kernel disagree on a struct) met and solved by a kernel-built fixed record with caller-chosen length; `ps` unprivileged | (d) |
| 2 | NetBSD | commit `18fe0c6726d` (2006-12-31): "kvm_getproc2: retry on ENOMEM. it happens when the number of processes are changed in the mean time." [R] | The torn-snapshot variant (list size changes between the sizing call and the fetch) handled by retry on the two-call pattern | (d) |
| 3 | NetBSD | commit `ad4b383df80` (2026-04-03, pull-up of libkvm `kvm_proc.c` revisions 1.102-1.104): "_kvm_ureadm: fix anon address calculation, this has been broken since the initial uvm support in 1998" and "fix an integer overflow ... I suppose it was rare to use >2GB swap" [R]; code at `lib/libkvm/kvm_proc.c:191-258` (the swap-slot read through `kd->swfd`, refused when `kd->swfd < 0`) | The user-space model of the kernel's swap layout was wrong for 28 years and survives only for crash dumps. This is evidence for the class, not a solution | (a) not met |
| 4 | NetBSD | commit `80cad5db8ae` (2009-10-21, rmind): "Remove uarea swap-out functionality ... Cuts ~1.5k lines of code. Reduces amd64 kernel size by ~4k", touching `bin/ps` and `lib/libkvm/kvm_proc.c`; `3bc294174d2` (2007): "pstats ... isn't in the uarea any more" [R] | The kernel removed the swapped-u-area state itself, so no reader needs the swap layout for process fields; fields `ps` shows live outside the swappable image | (d) |
| 5 | NetBSD | `sys/kern/kern_proc.c:2796` (`ki->p_flag = L_INMEM; /* Process never swapped out */`); commit `40f15d847fa` (2016): "/dev/mem cannot lseek over 0x80000000 on 32bit architectures" (`sys/dev/mm.c`) [R] | The same tree records both the removed swap state and a memory-driver offset limit that broke a raw reader | (c) partial |
| 6 | FreeBSD (history in `NextBSD`) | commit `f6968a45e69` (1999-11-17): "Turn off setgid-kmem for /bin/ps ... The proc stats are now part of eproc (obtained via sysctl) and no longer needs to beat up the u-page reading code and the problems with that"; `fb47be8de1c`: "Add e_stats ... to eproc so it's fetchable via sysctl. This saves ps having to read the u-area for stats"; `5a18be3c383`: "Use eproc.e_stats, not a series of crude hacks to fetch it from the u-area", deleting `get_uarea()` whose comment read "a kludge to work around the fact that on the alpha, the uarea is not mapped into the process address space" and which re-opened kvm on kmem at `p_addr` [R, `git -C NextBSD show`] | The same shape as the RP2040 failure (resident user area outside what the reader can map) met and fixed by moving the fields into the sysctl record | (c), (d) |
| 7 | FreeBSD | commit `cc6b664e2eb` (1999-11-16, phk): "Introduce commandline caching in the kernel ... makes ps(1) run much faster, and makes ps(1) even less dependent on /proc"; `kern_exec.c` `ps_arg_cache_limit` (default `PAGE_SIZE/16`) [R]; current form at `freebsd-src/sys/kern/kern_exec.c:840-848`, `kern_proc.c:2160-2176` [R] | Arguments served from an exec-time kernel copy so a swapped or unmapped image is never read | (d) |
| 8 | FreeBSD | commit `cba301121bc` (2000-12-12, mckusick): "Change the proc information returned from the kernel so that it no longer contains kernel specific data structures, but rather only scalar values ... If any of these changed in size, ps, w, fstat, gcore, systat, and top would all stop working. The new structure has over 200 bytes of unassigned space for future values ... nearly 100 bytes smaller per entry" [R] | The pointer-free fixed record with reserved room, adopted because every kmem reader broke together | (d) |
| 9 | FreeBSD | commit `01c93854753` (1994-05-28): "Noop out swapread in kvm_proc.c as our vm system is different"; `8c5c420dd34` (2002-01-28): "GC the -W option. kvm(3) doesn't read swap for almost 10 years"; `f51993282f2` (2003): "Disable and lobotomize the kvm image reading swapinfo code, the kernel layout is about to change. The sysctl based method still returns correct information."; current code `freebsd-src/lib/libkvm/kvm_proc.c:716-720` ("cannot read user space from dead kernel"), same text in DragonFly `lib/libkvm/kvm_proc.c:850,930` [R] | Resolution by refusal: when the kernel layout differs, the reader stops reading swap and the sysctl answers | (a) by refusal, (d) |
| 10 | FreeBSD | commit `5a18be3c383` hunk: `#define UREADOK(ki) (forceuread || (KI_PROC(ki)->p_flag & P_INMEM))`, `saveuser()` sets `u_valid` only when `P_INMEM`, and for an unreadable process `needcomm` prints `(comm)` through `sprintf(ki->ki_args, "(%s)", ...)`; commit `69cfa0ad1d5` (2009): "Fix elapsed (etime) field for swapped out processes in ps: show '-'" [R] | A `ps` that keeps the row and marks the unreadable fields | (b) |
| 11 | 4.4BSD-Lite2, LiteBSD-2 | `bin/ps/ps.c:376-397` (`saveuser`: "The u-area might be swapped out, and we can't get at it ... leave them 0", `u_valid = 0`), `bin/ps/print.c:361-363,389-392,481-486,721-737` (fields print `-` and times print 0 when `!u_valid`) [R] | The row survives a failed u-area read and the fields are marked | (b) |
| 12 | 386BSD | `usr/src/lib/libutil/kvm.c:622` ("Reading from swap is too complicated right now." then `return(NULL)` for any process without `SLOAD`); `usr/src/bin/ps/ps.c:352-374` (`up != NULL` guard; the row stays, `ki_u` empty) [R] | Refusal plus a kept row, in the oldest tree of the corpus | (a) by refusal, (b) |
| 13 | LiteBSD-2 (PIC32MZ) | commit `134109f9` (2014-11-28): "Fixed bug in /dev/kmem: allow reading kernel .text segment" (`sys/mips/pic32/mem.c` gains `(v >= KERNBASE && v + c <= (u_long)_etext)`); `8094238b` (2014-11-16): "Fixed bugs in /dev/kmem and in sysctl" (`mem.c` range tests rewritten); `67d9ff7e`: "Kvm library modified to use sysctl() instead of nlist() for kernel symbols"; `dbee6b6b`: "Kernel: don't swap out active processes." [R, `git -C LiteBSD-2 show`] | The closest relative met the memory-driver-region class and fixed it by widening the driver, keeping `ps` setgid kmem (`bin/ps/Makefile:8-9`); it also reduced swapped state by policy | (c) |
| 14 | RetroBSD | `sys/pic32/olimex/bootloader.ld:12-13,24-26` (`u0area` at 0x80006800 and `uarea` at 0x80007400, 3 KB each, `u = ORIGIN(uarea)`), `sys/pic32/machparam.h:84-94` (`KERNEL_DATA_START` 0x80000000, size 32 KB, so the end is 0x80008000), `sys/pic32/machdep.c:801-810` (`badkaddr` admits that range), `sys/kernel/init_main.c:116` (`p->p_addr = &u`) [R] | RetroBSD's resident user areas lie inside the window its memory driver admits, so its `ps` never meets the resident-area failure. The port's `RP2040.ld:44-60` ends `RAM` at 0x2003E800 and puts `U0AREA` and `UAREA` after it: the divergence is in this port's memory map, not in the inherited `ps` [R]; that the carve-out was meant to protect the areas is [I] | (c) |
| 15 | 2.11BSD | `sys/sys/kern_sysctl.c:960-971` (the kernel's own `fill_from_u` carries "XXX - We rely on the fact that u_ttyp, u_ttyd, and u_ruid are all within the first 1kb of the u area. If this ever changes the logic below will break (and badly)"), `ucb/top/top.c:646-725` (reads `VM_COREMAP` and `VM_SWAPMAP` through sysctl, not kmem), patches 463 and 464 (commits `de64824`, `2362449`, 2020) fixing those sysctl nodes, `bin/ps.c:376-378` (drops the row on a short read, as this tree's `ps` does) [R] | A relative exports swap-map state through sysctl for a sibling tool and records the layout assumption as a comment; its `ps` has the same silent drop | (d) partial, (b) not met |
| 16 | 2.11BSD, RetroBSD | `211bsd/ucb/w.c:381`, `retrobsd/src/cmd/w.c:380`, `4.4BSD-Lite2/lib/libkvm/kvm_proc.c:379`, `DragonFlyBSD/lib/libkvm/kvm_proc.c:501` ("proc size mismatch (%d total, %d chunks)") [R] | The size-drift symptom NetBSD and FreeBSD removed is still printed by the 2.11 family; this tree's `w` keeps the same check at `usr.bin/w/w.c:400-402` | (d) context |

### Not found, with the searches that found nothing

| Question | Search | Result |
|---|---|---|
| Any swap-ring fold (`m_ring_end`, `m_ring_first`) in any corpus tree, which would give a precedent for a reader that must model it | from the BSD_Workspace corpus root, `rg -l "m_ring_end\|m_ring_first" -g '*.c' -g '*.h' .` (whole corpus, 28.8 s, exit 1: no match); `rg -n -i "swap.*wrap\|wrap.*swap\|swap.*\bring\b\|\bring\b.*swap"` over `retrobsd/sys/kernel/vm_swap.c`, `netbsd-src/sys/uvm/uvm_swap.c`, `openbsd-src/sys/uvm/uvm_swap.c`, `freebsd-src/sys/vm/swap_pager.c`, `DragonFlyBSD/sys/vm/swap_pager.c`, `freebsd-src/sys/vm/vm_object.c` (no match). A first `grep -rIl` over the whole corpus exceeded 120 s and was replaced by the `rg` runs | No tree has a ring-folded swap map; the compound is novel to this port |
| A user-space reader that models a swap layout correctly (criterion a) | `rg -n -i "swap\|swfd" lib/libkvm/kvm_proc.c` in `netbsd-src`, `openbsd-src`, `freebsd-src`, `DragonFlyBSD`; read of `LiteBSD-2/lib/libkvm/kvm_proc.c:74-178` | The remaining readers (`kvm_readswap` in 4.4BSD and LiteBSD, `_kvm_ureadm` in NetBSD and OpenBSD `kvm_proc.c:176-244`) model the pager's data structures for crash dumps; NetBSD's own history shows the model wrong for 28 years (hit 3). FreeBSD's `kvm_proc.c` has no swap code and DragonFly includes only the pager header. No reader is demonstrably correct |
| 2.11BSD or RetroBSD `ps` on a machine whose resident user area is outside the readable window | read of `211bsd/sys/pdp/mem.c:20-77` and `211bsd/bin/ps.c:355-380`; read of `retrobsd/sys/pic32/mem.c:39-49,machdep.c:801-810`, `bootloader.ld`, `machparam.h:84-94` | Not met: the PDP-11 `/dev/mem` reads physical memory through `mapseg5` (any address), and RetroBSD's window covers its areas (hit 14). Their `ps` therefore never had to handle the case |
| OpenBSD history for the reason `ps` left group kmem | `git -C openbsd-src rev-list --count HEAD` = 1 (shallow); `git log -- bin/ps/Makefile` returns only `8849c5d46` | Not checkable here; the tree shows the result only (`bin/ps/Makefile` without `BINGRP`, `KVM_NO_FILES` at `ps.c:291-294`) |
| Histories not searchable | `rev-list --count`: LiteBSD 1, RetroBSD 1, freebsd-src 1, DragonFlyBSD 1, hardenedBSD 1, cheribsd 1, midnightbsd 1, rtems-libbsd 1 (all shallow). Searchable: netbsd-src 312264, NextBSD 216090 (FreeBSD history through about 2015), LiteBSD-2 578, 211bsd 95, 4.4BSD-Lite2 15, 386bsd 5 | FreeBSD claims above rest on the `NextBSD` history, which predates 2015 |
| NetBSD, FreeBSD, LiteBSD-2 and 2.11 history searches | `git -C netbsd-src log -i -E --grep='(ps\|kvm).*(setgid\|kmem\|sysctl)\|(setgid\|kmem).*(ps\|kvm)\|kvm.*(out of sync\|drift\|mismatch\|struct size)\|KERN_PROC2\|proc2' -- bin/ps lib/libkvm sys/kern/kern_proc.c sys/sys/sysctl.h` (25 hits) and `--grep='swap\|swapped\|u-area\|uarea\|u area\|kvm_getu\|kvm_uread\|kvm_ureadm\|_kvm_uvatop' -- lib/libkvm bin/ps` (15 hits); `git -C NextBSD log -i -E --grep='kvm_getargv\|kern\.proc\.args\|KERN_PROC_ARGS\|proc_args\|swapped\|swap.*(ps\|kvm)\|(ps\|kvm).*swap\|setgid.*kmem\|kmem.*setgid\|sgid.*kmem\|no longer.*kmem' -- bin/ps lib/libkvm sys/kern/kern_proc.c usr.bin/top usr.bin/w` (29 hits), `-S'ki_structsize' -- sys/sys/user.h`, `--grep='pargs\|p_args\|ps_arg_cache...'`; `git -C LiteBSD-2 log -i -E --grep='\bps\b\|kvm\|kmem\|/dev/mem\|u-area\|uarea\|swap'`; `git -C 211bsd log -i -E --grep='ps\b\|kmem\|u-area\|u area\|uarea\|swap'` and `git show 1a1be23 -- bin/ps.c` | Hits above; no commit in any searched history mentions a swap ring, a resident u area outside the memory driver's window, or a `ps` correction for either |

### What the search changes in the options of section 5

1. The sentence "nobody solved it" becomes "nobody solved it by modeling the
   kernel layout in user space". The three solutions that exist are the
   kernel-built record (hits 1, 6, 8), refusal (hits 9, 12) and a marked
   row (hits 10, 11, 12). Options A and B stay on the one pattern the
   corpus abandoned; Option D combines the first and the third, and
   Option D's exec-time snapshot is hit 7's mechanism.
2. The record needs reserved room and a length the caller chooses, as hits
   1 and 8 do (NetBSD `elem_size`; FreeBSD "over 200 bytes of unassigned
   space"), so a later field does not retire every consumer. Add reserved
   words to the record of Option D and test an old caller against a longer
   kernel record. [I]
3. The row must survive a failed detail read: the kernel marks the u-area
   fields invalid and `ps` prints `-` (hits 10, 11). An absent row is a
   defect, a row of zeros with an invalid flag is the specified outcome.
   [I]
4. A reader of raw memory needs a gate that compares its window with the
   kernel layout: the memory driver's range and the linker map must agree
   on every address a shipped tool reads. If Option D ships, the three
   resident readers (`ps`, `w`, `fstat`) stop reading user areas and the
   mismatch (RP2040.ld against `mmrw`) no longer matters to them; if it does
   not ship, a host test must require `badkaddr_span()` to admit `u0`, `u`
   and `proc[]`. Known-bad input: the current map. [I]
5. The wrapped-image failures (ring fold in `ps`; raw `b_blkno` in
   `fill_from_u`) have no precedent fix in the corpus, because no tree has
   the ring. The kernel-side repair is local and cheap: route
   `fill_from_u` through `swap_with_buf_result()` (`vm_swp.c:17`), which
   folds, as section 5 specifies. Known-bad input for the gate: a process
   image whose `p_addr` is at or past `m_ring_end` must report the right uid
   and tty through `KERN_PROC`. Not run. [I]
