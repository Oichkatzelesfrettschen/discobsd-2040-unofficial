# Minix 3 process-state access: ps, procfs, and raw memory devices

Source: shallow clone of Minix 3 at the shallow upstream clone `corpora/minix` (HEAD `4db99f401`, commit subject "Remove building with NOCRYPTO option"). The tree was read only. Nothing was built or executed.

Tags: **[read]** means the path and line were opened in this lookup. **[inferred]** means the conclusion follows from read code but was not executed. Paths below are relative to the clone root.

## Summary answer

Minix 3's `ps` does not read raw memory or swap. Its default path asks the MIB server for process data through `sysctl(2)`, and that server builds the data from the kernel process table, the PM process table, and VFS. The raw-memory route still exists: `/dev/mem` and `/dev/kmem` are served by the memory driver with no address-range refusal, and their device nodes are mode 640, group `kmem`. The process-state sysctl nodes carry no caller-identity check. So Minix 3 does let an account with `/dev/mem` access reconstruct process state from raw memory, and `ps` does not use that route.

Falsifier for the "no raw reads by default" claim: a `ps` invocation that opens `/dev/mem` without a memory-file argument. Falsifier for the "no caller check on argv" claim: a non-root `sysctl` call for `CTL_KERN.KERN_PROC_ARGS` that returns `EPERM`. Neither was run (`not run`: the tree may not be built or executed).

## 1. Minix ps and how it gets process info

- **[read]** `bin/ps/ps.c:385-389`: with no memory-file argument, `kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, ...)` runs and `donlist_sysctl()` fills the symbol values. With a memory-file argument, `kvm_openfiles(nlistf, memf, swapf, O_RDONLY, ...)` runs.
- **[read]** `bin/ps/ps.c:567-571` (`getkinfo_kvm`): calls `kvm_getproc2`, which is the process-list interface.
- **[read]** `bin/ps/nlist.c:144-187` (`donlist_sysctl`): reads `fscale`, `memsize`, `ccpu`, `maxslp`, `uspace` by `sysctl(mib, 2, ...)` (lines 155-187).
- **[read]** `bin/ps/print.c:303` and `:315`: command line and environment come from `kvm_getenvv2` and `kvm_getargv2`. When argv is empty, `print.c:315-323` falls back to `ki->p_comm`.
- **[read]** `bin/ps/Makefile`: `LDADD=-lm -lkvm`. There is no `/proc` reference in `bin/ps`.
- **[read]** `lib/libkvm/kvm.c:259-262`: `KVM_NO_FILES` sets `kd->alive = KVM_ALIVE_SYSCTL` and returns before any device is opened.
- **[read]** `lib/libkvm/kvm.c:344-350`: only when the memory file is `_PATH_MEM` does libkvm also open `_PATH_KMEM`. The default `_PATH_MEM` is set at `kvm.c:307`.
- **[read]** `lib/libkvm/kvm_proc.c:502` and `:510` (the `kvm_getproc2` sysctl path, six-element MIB), `:1133` and `:1161` (`KERN_PROC_ARGS` count and data sysctls). The `KERN_PROC2` attribution is **[inferred]** from the six-name MIB length and the `procbase2` buffer.
- **[read]** `minix/usr.bin/mtop/mtop.c` reads `psinfo`, which is the only in-tree consumer of the procfs `psinfo` file found.

Fields `ps` obtains (from `minix/servers/mib/proc.c:792-913` and `:919-1172`): `p_comm` = `mp_name` (`proc.c:743`), pid, uid, gid, tty, signal masks, nice (`proc.c:747`), start time from `mp_started` (`proc.c:748-749`), and argv/env through `KERN_PROC_ARGS`.

The procfs server is not used by `ps`. A search of the rest of the tree found `/proc` references only in `minix/tests/common.c:197` (`/proc/meminfo`), `minix/tests/testrelpol.sh:12-13` and `:364` (`/proc/service`), and the `psinfo` consumer above.

## 2. The procfs server (minix/fs/procfs)

Size: 1703 lines of `.c` (`root.c` 227, `tree.c` 462, `service.c` 348, `pid.c` 230, `cpuinfo.c` 153, `buf.c` 125, `main.c` 93, `util.c` 65), plus 193 lines of headers. **[read]** by `wc -l`.

Process-table acquisition, **[read]**:
- `minix/fs/procfs/tree.c:85-96` (`update_list`): one `__sysctl(CTL_MINIX, MINIX_PROC, PROC_LIST)` into `proc_list[NR_PROCS]`. Called from `init_tree` (`tree.c:108`), `lookup_hook` (`tree.c:368`, at most once per tick), and `getdents_hook` (`tree.c:407`).
- `minix/fs/procfs/pid.c:42-52` (`get_proc_data`): one `__sysctl(CTL_MINIX, MINIX_PROC, PROC_DATA, pid)` per read.
- `minix/fs/procfs/service.c:212-261` (`service_update`): `getsysinfo(RS_PROC_NR, SI_PROCALL_TAB, ...)` at `service.c:221`.
- `minix/fs/procfs/root.c:165`: `getsysinfo(VFS_PROC_NR, SI_DMAP_TAB, ...)`. `root.c:92` (`sys_getkinfo`) and `root.c:106` (`vm_info_stats`) serve the top-level files.

procfs does not call `sys_getproctab`, `getsysinfo(PM_PROC_NR, SI_PROC_TAB)`, or any VM call directly for process rows. It delegates all of that to the MIB server.

Per-PID files, **[read]** `minix/fs/procfs/pid.c:18-24`:

| File | Lines | Source | Fields |
|---|---|---|---|
| `psinfo` | `pid.c:60-133` (74) | PROC_DATA sysctl (`pid.c:44`) and `vm_info_usage` (`pid.c:106-108`) | version, type, endpoint, name (cut at first space, `pid.c:111-112`), state letter, blocked-on, priority, user/sys time, cycles, IPC cycles, kcall cycles, total memory, nice, uid (from `proc_list`, `pid.c:100-101`) |
| `cmdline` | `pid.c:139-160` (21) | `CTL_KERN.KERN_PROC_ARGS.KERN_PROC_ARGV` (`pid.c:143`) | argv bytes, copied to a `BUF_SIZE` buffer (`pid.c:142`) |
| `environ` | `pid.c:166-187` (21) | `KERN_PROC_ARGS.KERN_PROC_ENV` (`pid.c:170`) | environment bytes |
| `map` | `pid.c:192-230` (38) | `vm_info_region` (`pid.c:213`) | address ranges and protections |

Failure handling, **[read]**: `cmdline` and `environ` return with no output when the sysctl fails (`pid.c:156-157`, `pid.c:183-184`). `psinfo` ignores a `vm_info_usage` failure (`pid.c:108`, "We do not care if this fails"). A failed `PROC_DATA` returns with no output (`pid.c:72-73`). So a row can be absent or partly empty without an error. This matches the silent-drop pattern in the port's `ps`.

Locking and staleness, **[read]** `minix/fs/procfs/NOTES`: the file states that procfs "is not able to deal with security-sensitive information" and that a check-then-use race exists for `/proc/<pid>/mem`-style files. The tables are refreshed lazily, and PID entries can be stale.

### argv and command line, the MIB side

Read from `minix/servers/mib/proc.c` (the `KERN_PROC_ARGS` producer):
- `proc.c:919-1172` (254 lines): `mib_kern_proc_args`.
- `proc.c:949`: `update_tables()` runs first.
- `proc.c:962-965`: the target's `ps_strings` is read with `sys_datacopy(mp_endpoint, mp_frame_addr + mp_frame_len - sizeof(pss), SELF, ...)`. If this copy fails the call returns `EINVAL`.
- `proc.c:1067` and `:1096`: vector and string pages are copied from the target one page at a time with `sys_datacopy`. A failed copy breaks the loop (`:1067`) or aborts it (`:1096`).
- The comment block in this function states that on failure the call "just return[s] a truncated result" (the "Generally speaking, upon failure" paragraph). Truncation, not an error, is the failure mode.
- The copy count is capped at `(ARG_MAX / PAGE_SIZE) * 2` (the `copybudget` comment, string-page branch near `proc.c:1090`).

So argv is read from the target's own frame memory through the kernel's `sys_datacopy`, page by page, by the MIB server. The MIB server holds the process endpoint and frame address from PM's table, not from procfs.

### Process table producers (lines)

| Routine | File:lines | Size | Source |
|---|---|---|---|
| `update_tables` | `minix/servers/mib/proc.c:47-137` | 91 | `sys_getproctab` (`:75`), `getsysinfo(PM_PROC_NR, SI_PROC_TAB)` (`:90`), `getsysinfo(VFS_PROC_NR, SI_PROCLIGHT_TAB)` (`:106`); at most once per tick (`:68-69`) |
| `mib_kern_proc2` (KERN_PROC2) | `proc.c:792-913` | 122 | `update_tables` (`:838`), `vm_info_usage` (`:621`) |
| `mib_kern_proc_args` | `proc.c:919-1172` | 254 | `update_tables` (`:949`), `sys_datacopy` (`:962`, `:1067`, `:1096`) |
| `mib_minix_proc_list` (PROC_LIST) | `proc.c:1178-1211` | 34 | `update_tables` (`:1190`) |
| `mib_minix_proc_data` (PROC_DATA) | `proc.c:1217-1288` | 72 | `update_tables` (`:1235`), `proc_tab` and `mproc_tab` |
| `get_lwp_stat` | `proc.c:225-390` | 166 | per-LWP state |

Kernel and PM and VFS producers, **[read]**:
- `minix/kernel/system/do_getinfo.c:96-100` (`GET_PROCTAB`): copies the whole `proc` array, `sizeof(struct proc) * (NR_PROCS + NR_TASKS)`, after `update_idle_time()`.
- `minix/servers/pm/misc.c:125-128` (`SI_PROC_TAB`): copies the whole `mproc` array. PM refuses unauthorized `do_getsysinfo` calls with `EPERM` before the switch (`misc.c:118-123`, read from the surrounding `printf`/`return EPERM` block).
- `minix/servers/vfs/misc.c:73` (`SI_PROC_TAB`) and `:81` (`SI_PROCLIGHT_TAB`).
- `minix/servers/is/dmp_pm.c:47`, `:82` and `minix/servers/is/dmp_fs.c:31` are other in-tree consumers of the same tables, the information service dumpers.

## 3. Sysctl access control in the MIB server

- **[read]** `minix/servers/mib/kern.c:428-431`: `KERN_PROC2` and `KERN_PROC_ARGS` are `_P | _RO | CTLTYPE_NODE`. `minix/servers/mib/mib.h:322-324`: `_RO` is `CTLFLAG_READONLY`, and `_P` is `CTLFLAG_PERMANENT`. Neither is `CTLFLAG_PRIVATE`.
- **[read]** `minix/servers/mib/minix.c:63-76`: `PROC_DATA` and `PROC_LIST` are `_P | _RO`. The only `CTLFLAG_PRIVATE` nodes in those files are the test nodes at `minix.c:30` (`TEST_PRIVATE`) and `minix.c:36` (`TEST_SECRET`).
- **[read]** `minix/servers/mib/tree.c:1388-1390`, `:934`, `:995`: the dispatcher calls `mib_authed(call)` only for nodes flagged `CTLFLAG_PRIVATE`. `minix/servers/mib/main.c:265` (`getnuid(call->call_endpt) == SUPER_USER`) is the `mib_authed` body.
- **[read]** `grep -n 'uid|euid|suser|PRIV|priv|caller|kauth|EPERM|EACCES' proc.c` over `:919-1172` returns no match. The argv handler checks no credential.
- **[inferred]** A non-root caller can read `KERN_PROC_ARGS` (argv and environment) of any process whose frame is mapped. The `not run` item here is the live `sysctl` call from an unprivileged uid.

## 4. Swapped and unreadable processes

- **[read]** `grep -rniE 'pageout|page_out|swap_out|swapout|swap_in|swapin|backing store|pager' minix/servers/vm` returns no match. The only `swap` in `minix/servers/vm` is `swap_proc_slot` and `swap_proc_dyn_data` (`minix/servers/vm/utility.c:186-213`, `:310-328`), which swap process-table slots, not pages.
- **[inferred]** Minix 3's VM in this tree does not page user memory out to a backing store, so "swapped" process memory does not occur. The `/dev/swap` concern in the port does not map onto this tree. Confidence: moderate. What would lower it: a pageout or backing-store path found in another VM source file or in the kernel `vm_*` calls.
- **[read]** Unreadable process memory produces truncated or empty output, not an error row. See section 2 and `proc.c:962-965`, `:1067`, `:1096`.

## 5. Raw memory consumers and the memory driver

Memory driver, **[read]** `minix/drivers/storage/memory/memory.c` (599 lines):
- `:134-146`: `sys_getkinfo` supplies `kmem_base` and `kmem_size`, which are mapped into the driver with `vm_map_phys` (`:139-141`). `/dev/kmem` is the kernel's own virtual range.
- `:155-159`: `/dev/mem` has `dv_size = 0xffffffff` (`:157`). The driver does not refuse any physical range. `m_transfer_mem` clamps only at the end of the device (`EOF` and `position + size > dv_size`).
- `:189-215` (`m_transfer_kmem`, about 27 lines, end approximate): copies from the mapped kernel range with `sys_safecopyto` or `sys_safecopyfrom`, bounded by `dv_size` only.
- `:360-377` (`m_char_open`): on i386 only, `MEM_DEV` opens call `sys_enable_iop(user_endpt)` (`:368-370`). That grants I/O-port access to the opener, which is the only open-time gate in this file.
- `grep -nE 'EPERM|EACCES' memory.c` returns no match. The errors returned are `ENXIO` (bad minor), `EINVAL` (ioctl), `EIO` (uninitialized KMEM), and `ENOMEM`. No region is refused by the driver itself.

Device nodes, **[read]** `minix/commands/MAKEDEV/MAKEDEV.sh:371-373`: `mem` is `c 1 1 ... kmem 640`, `kmem` is `c 1 2 ... kmem 640`, `null` is 666, `zero` is 644. So the raw-memory nodes are readable by root and by the `kmem` group.

Other consumers of `/dev/mem` or `/dev/kmem` in the tree, **[read]**:
- `minix/commands/screendump/screendump.c:27`, `:52-55`, `:77`: opens `/dev/mem` and seeks to `BIOS_CRTBASE`. It reads the VGA text buffer. This is a real raw-memory consumer, i386 only.
- `minix/drivers/clock/readclock/arch/i386/arch_readclock.c:14` and `:27`: comments only, naming `/dev/mem`. No open call was found in this file.
- `lib/libkvm/kvm.c:307`, `:344-350`: `_PATH_MEM` default and the `/dev/kmem` open. Used only when a memory file is named.
- `include/paths.h:76`, `:82`: `_PATH_KMEM` and `_PATH_MEM`.
- `minix/include/minix/dmap.h:22`, `:86-87`: minor numbers `MEM_DEV` (1) and `KMEM_DEV` (2).
- `minix/usr.bin/trace/mem.c`: uses `ptrace(T_GETRANGE)`, not a device. This is a name match only, not a raw-memory consumer.
- `minix/commands/DESCRIBE/DESCRIBE.sh:42`: label text only.

Consumers of `/dev/swap` in the tree: none found. `grep -rnE '/dev/swap|_PATH_DRUM' minix lib bin sys include` returns only libkvm's default swap-file name, `lib/libkvm/kvm.c:309` (`sf = _PATH_DRUM`), defined as `/dev/drum` at `include/paths.h:70`. The `/dev/swap` hits in `external/` and `crypto/` are third-party. No Minix source opens `/dev/drum`, and `ps` reaches libkvm's swap default only when a memory file is named.

Privileges that gate the memory driver's kernel calls (`sys_getkinfo`, `sys_enable_iop`, `vm_map_phys`): **[not run]**. The driver's privilege table entry was not located in this lookup.

## 6. Size of producing routines

| Routine | Lines | Basis |
|---|---|---|
| `bin/ps` sources, total | 783 (`ps.c`) + 1483 (`print.c`) + 419 (`keyword.c`) + 207 (`nlist.c`) + 60 (`fmt.c`) | `wc -l` |
| `bin/ps` `getkinfo_kvm` | 5 | `ps.c:567-571` |
| `bin/ps` `donlist_sysctl` | about 47, from `nlist.c:144` (end at or after `:187`, not measured) | read |
| `lib/libkvm/kvm_proc.c` total | 1238 | `wc -l` |
| procfs `.c` total | 1703 | `wc -l` |
| procfs `get_proc_data` / `pid_psinfo` / `pid_cmdline` / `pid_environ` / `pid_map` | 11 / 74 / 21 / 21 / 38 | awk over function boundaries |
| procfs `update_list` / `init_tree` / `construct_pid_dirs` | 12 / 18 / 77 | awk |
| MIB `proc.c` total | 1288 | `wc -l` |
| MIB `update_tables` / `mib_kern_proc2` / `mib_kern_proc_args` | 91 / 122 / 254 | awk |
| MIB `mib_minix_proc_list` / `mib_minix_proc_data` | 34 / 72 | awk |
| memory driver `memory.c` total | 599 | `wc -l` |

Sizes marked by awk were measured from function start to the first closing brace at column 0. Only the `donlist_sysctl` and `m_transfer_kmem` ends are approximate.

## Grep patterns and paths used

- Files: `bin/ps/*.c` `bin/ps/*.h` `bin/ps/Makefile`, `minix/fs/procfs/*.c` `minix/fs/procfs/*.h` `minix/fs/procfs/NOTES`, `minix/servers/mib/*.c` `minix/servers/mib/mib.h`, `minix/drivers/storage/memory/memory.c`, `minix/servers/vm/*.c`, `minix/commands/MAKEDEV/MAKEDEV.sh`, `lib/libkvm/*.c`.
- `grep -rnE '/dev/kmem|/dev/mem\b|"/dev/mem"|/dev/swap'` over the clone with `--include=*.c --include=*.h --include=*.sh --include=*.S --include=Makefile*`, excluding `external/`.
- `grep -nE '/proc|kvm|kmem|sysctl|KERN_PROC|getsysinfo|sys_getproctab' bin/ps/*.c bin/ps/*.h`.
- `grep -nE 'getsysinfo|sys_getproctab|sys_datacopy|SI_|PM_PROC|vm_|psinfo|mp_name|argv|cmdline|swap|PROC_TAB|proc_tab|sys_' minix/fs/procfs/*.c`.
- `grep -rnE 'PROC_DATA|MINIX_PROC\b|KERN_PROC_ARGS|KERN_PROC_ARGV|KERN_PROC_ENV|KERN_PROC_ALL|KERN_PROC2' --include=*.c --include=*.h minix sys lib`.
- `grep -rniE 'pageout|page_out|swap_out|swapout|swap_in|swapin|backing store|pager' minix/servers/vm`.
- `grep -nE 'uid|euid|suser|PRIV|priv|caller|kauth|EPERM|EACCES'` over `minix/servers/mib/proc.c` (argv and proc2 ranges) and `minix/servers/mib/tree.c` and `main.c`.
- `grep -nE 'EPERM|EACCES|ENXIO|EINVAL' minix/drivers/storage/memory/memory.c`.
- `grep -rnE '/proc|psinfo' bin/ps lib/libkvm lib/libc/gen minix/tests minix/usr.bin` (the `/proc` hits in the tests are the ones listed in section 1).
- Sizes: `wc -l` and `awk '/^[a-z_].*\(/{...} /^}/{...}'` over the file list above.

## Open items

- **[not run]** Live `sysctl` call from an unprivileged uid for `KERN_PROC_ARGS` and `PROC_DATA`. Needed to confirm section 3's inferred result.
- **[not run]** Privilege table for the memory driver (`sys_enable_iop`, `sys_getkinfo`, `vm_map_phys`). Needed to say which uids can reach `/dev/mem` beyond the file mode.
- **[not run]** Whether `minix/kernel` or `minix/servers/vm` contains a page-out path under a name the grep did not match. Needed to confirm section 4.
- **[inferred]** The default `ps` path cannot read a swapped or non-resident page as a row. It returns truncated or empty argv instead.
