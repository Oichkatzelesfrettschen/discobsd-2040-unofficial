# Process-status interfaces and kernel-memory guards in the titor-labs corpus

Scope: every non-BSD or pre-BSD mechanism in the titor-labs corpus by which a
process-status tool learns about a process, and by which the kernel guards its
memory while it does so. The question for the DiscoBSD RP2040 port is how
`ps(1)` obtains command name and arguments for a process whose image is
resident, on raw flash swap, or compressed in the SwapRAM pool, without the
`kmem` group and without silently dropping a row.

Evidence tags: **[R path:line]** marks a claim read in the named source;
**[I]** marks an inference, with the reasoning beside it. Corpus paths are
relative to the corpus root and abbreviated with these prefixes:

| Prefix | Corpus path |
|---|---|
| `S413` | `sources/sun/os/sunos/4.1.3/extracted/SunOS 4.1.3 sunsrc/413` |
| `S26` | `sources/sun/os/solaris/2.6/extracted/Solaris 2.6 Source Code/Solaris_2.6/os_net/src_ws/usr/src` |
| `OSN` | `sources/sun/os/opensolaris/schillix/collection/schillix-on-schillix-on/usr/src` |
| `OSF` | `extracted/archives/osf/Digital-OSF1-1.10/Digital-OSF1-1.10` |
| `SVR4` | `extracted/archives/att/sysvr4/svr4` |
| `SVR3` | `extracted/archives/att/sysvr3/32/usr/src` (the `.unpacked` directories hold the text) |
| `V7` | `extracted/archives/att/v7/usr/src/cmd` |
| `V8`, `V10` | members of `archives/tuhs/Distributions/Research/Dan_Cross_v8/v8.tar.bz2` and `Dan_Cross_v10/v10src.tar.bz2` |
| `COH` | members of `archives/tuhs/Distributions/Other/Coherent/kernelsource/{coh_tar,inc_tar}.gz` |
| `AIX` | `extracted/archives/ibm/aix-4.1.3/aix-4.1.3/bos/kernel/proc` |
| `IRIX` | `extracted/archives/sgi/IRIX/irix-6.5.5-src/6.5.5/f/irix/kern` |
| `XINU` | `extracted/archives/xinu/pc-xinu/XINU-68K.TAR/src/sys` |

Port-side paths are repository-relative. Port byte figures marked "measured"
come from `arm-none-eabi-nm -S` on the existing
`sys/arch/rp2040/compile/PICO/unix.elf` (text 99,662 bytes). That artifact
predates this document and was not rebuilt, so it may differ from the checked-out
sources; every Thumb-1 delta below is an estimate with roughly 40 percent
uncertainty until a build measures it.

## Result

Every surveyed design answers one of two questions in one of two ways. The
kernel either copies the command line when the program starts (SVR3, SVR4,
Solaris, IRIX, Coherent) or reads the process image when a tool asks (V7,
SunOS 4, V8, OSF/1 `TBL_ARGUMENTS`, AIX `getargs`). The tool either receives a
fixed, pointer-free record that the kernel assembled (SVR4 `PIOCPSINFO`,
Solaris `psinfo_t`, OSF/1 `tbl_procinfo`, Coherent `stMonitor`) or reads raw
memory and decodes the kernel's own structures (V7, SunOS 4 `libkvm`,
System V `/dev/kmem`).

For a 128 KB kernel whose per-process image lives behind a compressed pool, the
only design that adds no resident RAM and never touches a compressed stack is
the System V placement of the argument snapshot: copy at most 48 bytes of the
joined argument vector into `struct user` when `exec` finishes, and return it in
a fixed record through the process sysctl. `struct user` is 972 bytes inside a
1,024-byte region [R `docs/research/swap-exhaustion/review-03.md:166`], so 48
bytes fit in space that is already swapped and already compressed. The pool
decodes the `struct user` first and can stop after the prefix
[R `sys/arch/rp2040/rp2040/swapram.c:487-506`], so the snapshot costs one short
decode instead of a decode of the whole stack segment.

## Mechanism catalog

### SunOS 4.1.3: `libkvm` over `/dev/kmem`, `/dev/mem`, `/dev/drum`

- Tool path. `ps` opens the kernel through `kvm_open`
  [R `S413/bin/ps.c:544`], walks `kvm_nextproc` [R `S413/bin/ps.c:362`], pulls
  the `struct user` with `kvm_getu` [R `S413/bin/ps.c:902`] and rebuilds argv
  with `kvm_getcmd` [R `S413/bin/ps.c:1076`].
- Kernel record kept at exec. The kernel stores only the command name:
  `u_comm[MAXCOMLEN + 1]` with `MAXCOMLEN` 16
  [R `S413/sys/sys/user.h:40,46`], copied from the path at exec and truncated to
  16 bytes [R `S413/sys/os/kern_exec.c:497-504`]. No argument snapshot exists.
- Query-time algorithm. `kvm_getu` reads the process's `p_uarea` virtual
  address and returns NULL for a zombie or a short read
  [R `S413/usr.lib/libkvm/kvmgetu.c:21,43-44`]. `kvm_getcmd` gives up when
  `p_ssize == 0` [R `S413/usr.lib/libkvm/kvmgetcmd.c:102`], reads the last stack
  page, walks back over the environment strings to the argument strings and
  fetches earlier pages when needed
  [R `S413/usr.lib/libkvm/kvmgetcmd.c:109-143`]. A swapped page comes from
  `/dev/drum` through the library's own segment and anon-map traversal
  [R `S413/usr.lib/libkvm/kvmgetcmd.c:300-345`, `kvmopen.c:157-176`].
- Failure handling. When `kvm_getu` fails, `save()` returns without a row
  [R `S413/bin/ps.c:901-903`]. When `kvm_getcmd` fails the tool prints
  `(u_comm)` [R `S413/bin/ps.c:1076-1080`]. The first behavior is the same
  silent drop that DiscoBSD `savcom` shows at `bin/ps/ps.c:476-477`.
- Privilege. `ps` is installed `chgrp kmem; chmod 2755`
  [R `S413/bin/Makefile:52,67,138-141`]. `mem`, `kmem`, `drum` are mode 0640
  group `kmem` [R `S413/files/MAKEDEV:97-101`]. The `/dev/kmem` read path in
  the driver checks `kernacc` and falls back to `mmpeekio`, so a bad address
  returns an error instead of a fault [R `S413/sys/sun4c/mem.c:207-217`].
- Kernel bytes: zero beyond the memory driver. Thumb-1 port estimate: 0 bytes
  (this is the present DiscoBSD design; `mmrw` is 176 bytes measured).

### Solaris 2.6 and OpenSolaris (Schillix OS/Net): `/proc/<pid>/psinfo`

- Record. `psinfo_t` carries `pr_fname[16]` and `pr_psargs[80]` ("initial
  characters of arg list"), pids, ids, sizes, times, tty, plus `pr_argc`,
  `pr_argv`, `pr_envp` for debuggers
  [R `S26/uts/common/sys/procfs.h:183,215-248`]. `PRARGSZ` is 80 and `PSARGSZ`
  is 80 [R `S26/uts/common/sys/procfs.h:215`, `S26/uts/common/sys/user.h:84`].
- Where the snapshot lives. `u_psargs[PSARGSZ]` and `u_comm` sit in `struct
  user` [R `S26/uts/common/sys/user.h:112-113`], and `struct user` is embedded
  in `proc_t` as `p_user` [R `S26/uts/common/sys/proc.h:144`], so it is resident
  and never swapped [I: the user struct is a field of the resident proc; only
  kernel stacks swap in this design].
- Copy at exec. `exec` builds the argument strings on the new stack, then
  `bzero`es `u_psargs`, copies `min(strlen_of_args, PSARGSZ)` bytes from the
  freshly written string area, NUL-terminates, and rewrites every interior NUL as
  a space [R `S26/uts/sparc/os/sundep.c:1162,1206-1218`]. The interpreter name for
  `#!` scripts is already in the copied region because the copy starts before the
  interpreter strings [R `S26/uts/sparc/os/sundep.c:1162-1189`]. The newer tree
  does the same and zero-fills the tail
  [R `OSN/uts/common/os/exec.c:1685-1691`]. Kernel daemons write literal names
  ("sched", "fsflush", "pageout") into `u_psargs`
  [R `S26/uts/common/os/main.c:269`, `fs/fsflush.c:68`, `os/vm_pageout.c:547`].
- Query-time algorithm. `pr_read_psinfo` locks the proc, calls `prgetpsinfo`,
  unlocks, and `uiomove`s the whole record from a stack copy
  [R `S26/uts/common/fs/proc/prvnops.c:637-665`]. `prgetpsinfo` fills the
  record from `proc` fields under `p_lock`; for a zombie it zeroes the lwp part
  and sets state `SZOMB` [R `S26/uts/common/fs/proc/prsubr.c:1237-1330`]; for a
  live process it copies `u_comm` and `u_psargs` with `bcopy`
  [R `S26/uts/common/fs/proc/prsubr.c:1303-1306`]. The file comment states the
  record "never goes invalid" and tolerates an exec in progress
  [R `S26/uts/common/fs/proc/prvnops.c:640-645`].
- Tool. `ps` opens `/proc/<pid>/psinfo` per process, reads one `psinfo_t`, retries
  on `EAGAIN`, skips on `ENOENT` and prints other errors
  [R `S26/cmd/ps/ps.c:618-650`]. It prints `pr_psargs` as the `-f` command column
  [R `S26/cmd/ps/ps.c:1351-1363`].
- Privilege. `psinfo`, `lpsinfo`, `usage` are mode 0444, every other `/proc`
  file 0400 or 0600 [R `S26/uts/common/fs/proc/prvnops.c:1866-1868,2586-2609`]. The
  OpenSolaris tree adds visibility policy: `secpolicy_basic_procinfo` returns
  EPERM unless the caller is the target, holds `PRIV_PROC_INFO`, or passes the
  owner test, and `/proc` lookup returns no node on failure
  [R `OSN/uts/common/os/policy.c:2104-2111`,
  `OSN/uts/common/fs/proc/prvnops.c:3408-3412`]. A process invisible to the
  caller is therefore absent from the directory listing instead of unreadable.
- Thumb-1 port estimate. The exec-time copy is 40 to 60 bytes (compute the
  length, one `bcopy`, one loop that rewrites NUL to space, one terminator
  store; measured `bcopy` is 110 bytes and a call costs about 8). The record
  assembly beside `sysctl_doproc` (measured 268 bytes) is 150 to 300 bytes. No
  file system layer is needed when the transport is a sysctl node.

### System V Release 4 and 3: `/proc` ioctl `PIOCPSINFO`, and the `u_psargs` field

- SVR4 record. `prpsinfo_t` carries `pr_fname[16]` and `pr_psargs[80]`
  [R `SVR4/uts/i386/sys/procfs.h:124-153`]; the ioctl is `PIOCPSINFO`
  [R `SVR4/uts/i386/sys/procfs.h:59`]. `u_psargs[PSARGSZ]` lives in `struct user`
  with `PSARGSZ` 80 [R `SVR4/uts/i386/sys/user.h:66,166`], and the u-block is
  paged and can be swapped.
- Copy at exec. `setregs` copies `min(argsize, PSARGSZ - 1)` bytes from the
  argument area with `copyin`, rewrites NULs to spaces and terminates
  [R `SVR4/uts/i386/os/machdep.c:223-236`]. This is the earliest copy of the
  fixed-argument design in the corpus whose kernel source is held, and it shows
  that the design predates `/proc`: SVR3 on the 3B2 does the same from the
  `exec` argument buffer [R `SVR3/uts/3b2/os/exec.c.unpacked/exec.c:563-572`] with
  `PSARGSZ` 80 [R `SVR3/../301/usr/src/uts/3b2/sys/user.h.unpacked/user.h:39,101`;
  note the 3.1 header at `31/usr/src/uts/3b2/sys/user.h.unpacked/user.h:38`]. SVR3
  `ps` prints `u_psargs` for VAX, 3B and 3B2, and still scans the stack for PDP-11
  [R `SVR3/cmd/ps/ps.c.unpacked/ps.c:844-1170,1176-1190`; a comment there records
  that the field was 40 bytes before it became 80].
- Query-time algorithm. `PIOCPSINFO` calls `prlock`, then `prgetpsinfo`, then
  `copyout` [R `SVR4/uts/i386/fs/proc/prioctl.c:83-95,569-578`]. `prgetpsinfo`
  maps the u-block with `prumap`, which calls `ub_lock` to hold it resident while
  the fields are copied under a fault catcher (`CATCH_FAULTS(CATCH_SEGU_FAULT)`),
  then `prunmap` [R `SVR4/uts/i386/fs/proc/prsubr.c:560-636`,
  `SVR4/uts/i386/fs/proc/prmachdep.c:48-73`]. A zombie skips the u-block
  entirely [R `SVR4/uts/i386/fs/proc/prsubr.c:604-607`].
- Tool. One `open`, one `ioctl`, one `close` per process, `EAGAIN` retried,
  `ENOENT` skipped, other errors printed
  [R `SVR4/cmd/ps/ps.c:384-410`].
- Privilege. Each `/proc/<pid>` is mode 0600 owned by the target's user
  [R `SVR4/uts/i386/fs/proc/prvnops.c:558`], so the tool must see every user's
  files; it is installed `-m 4555 -u root`
  [R `SVR4/cmd/ps/ps.mk:24`]. SVR3's memory-reading `ps` is set-id and drops
  rights with `setuid(getuid())` before honoring a user-chosen `-n` or `-c` file
  [R `SVR3/cmd/ps/ps.c.unpacked/ps.c:292-304`]. DiscoBSD `ps` has the same drop at
  `bin/ps/ps.c:644-669`.
- Thumb-1 port estimate. The `/proc` file system layer (V8's `proca.c` is 716
  lines [R V8 `usr/sys/sys/proca.c`]) is an estimated 2 to 4 KB [I: 716 lines at
  roughly 3 to 5 Thumb-1 bytes per line]; the ioctl alone is the same record
  assembly as Solaris (150 to 300 bytes) and needs the file system for its
  transport. Rejected for a 128 KB kernel.

### SGI IRIX 6.5.5: argument snapshot in the proc-private u-info

- `p_psargs` is a macro for `p_ui.u_psargs` [R
  `IRIX/os/proc/pproc_private.h:95,101`] with `PSARGSZ` 80
  [R `IRIX/sys/uthread.h:33`]; `PIOCPSINFO` copies `sizeof - 1` bytes
  [R `IRIX/fs/procfs/priface.c:340`]. `exec` rewrites the interpreter file
  descriptor in the snapshot in place and guards the array end
  [R `IRIX/os/exec.c:980-995`]. Same design as Solaris: snapshot at exec, resident,
  fixed 80 bytes.

### OSF/1 1.0 and 1.10 (Mach-based): `table()` syscall, `TBL_PROCINFO`, `TBL_ARGUMENTS`, `TBL_UAREA`

- Interface. `table(id, index, addr, nel, lel)` is a CMU syscall
  [R `OSF/kernel/bsd/cmu_syscalls.c:112-125`]. `TBL_PROCINFO` indexes by proc
  table slot, `TBL_ARGUMENTS`, `TBL_ENVIRONMENT` and `TBL_UAREA` index by pid
  [R `OSF/kernel/sys/table.h:28-37`]. A call with `lel == 0` and `nel ==
  MAXSHORT` returns the table length [R `OSF/kernel/bsd/cmu_syscalls.c:100-108`].
- Fixed record. `struct tbl_procinfo` carries pid, ppid, pgrp, session, tty,
  status (`PI_EMPTY`, `PI_ACTIVE`, `PI_EXITING`, `PI_ZOMBIE`), flags, a 19-byte
  `pi_comm`, all four user and group ids and the signal sets
  [R `OSF/kernel/sys/table.h:137-165`]. It carries no kernel address and no
  arguments.
- Argument snapshot is a pointer, not bytes. At exec the kernel stores two
  fields in the per-task utask: `uu_argp` (address of the first argument string in
  the user map) and `uu_arg_size` (`u_short` length), and likewise `uu_envp` and
  `uu_env_size` [R `OSF/kernel/sys/user.h:229-232`; set at
  `OSF/kernel/bsd/ldr_exec.c:1101,1110,1113,1122`]. That is 12 bytes per task.
  `TBL_ARGUMENTS` then copies `min(uu_arg_size, caller_size)` bytes from the
  target's map with `vm_map_copyin`, which faults a paged-out page back in as
  needed, and releases the temporary mapping afterward
  [R `OSF/kernel/bsd/cmu_syscalls.c:334-432`]. The kernel reads the image at
  query time; the user process never touches the other process's memory.
- Failure handling. `ps` falls back to `[comm]` when the call fails and treats the
  buffer as NUL- and space-separated words
  [R `OSF/usr/bin/ps/ps.c:1944-1950`]. It reads the proc table sixteen slots per
  call [R `OSF/usr/bin/ps/ps.c:1692-1712`].
- `TBL_UAREA` fabricates a `struct user` from the task and thread on demand
  with `fake_u`, allocated from a pageable kernel map
  [R `OSF/kernel/bsd/cmu_syscalls.c:262-330`]. The Mach kernel holds no resident
  `u`; the BSD view is synthesized per query.
- Privilege. Reading another process through any of these ids requires `suser`
  (or `SEC_DEBUG` privilege under `SEC_BASE`)
  [R `OSF/kernel/bsd/cmu_syscalls.c:293-296,365-368,553-556`]. `ps` is installed
  with `SETUID_MODE` [R `OSF/usr/bin/ps/Makefile:19`].
- Thumb-1 port estimate. `TBL_PROCINFO` alone is the same record assembly (150 to
  300 bytes). `TBL_ARGUMENTS` on this port needs the equivalent of `vm_map_copyin`
  for a flash-swapped or compressed stack segment: a swap `bread` path (60 to 90
  bytes) plus a decode of the whole compressed stack segment to reach its top
  [I: the stack segment is compressed independently of the u-block, and the
  argument block sits at its highest addresses, so reaching it means decoding to
  the end of the stream]; 500 to 900 bytes with the buffer management, as the
  earlier review estimated [R `docs/research/ps-kernel-memory-boundary-review.md`,
  fourth table row].

### Mach task and thread information (OSF/1 1.10 kernel, `kernel/kern`)

- `task_info(TASK_BASIC_INFO)` returns suspend count, base priority, virtual and
  resident size and accumulated user and system time, and rejects any other
  flavor and any short caller count with `KERN_INVALID_ARGUMENT`
  [R `OSF/kernel/kern/task.c:729-775`, `OSF/kernel/mach/task_info.h`].
  `thread_info` adds run state, cpu usage, priority, sleep time and a 7-byte wait
  message [R `OSF/kernel/mach/thread_info.h:52-66`,
  `OSF/kernel/kern/thread.c:1455,1570-1573`].
- Neither call returns a command name or arguments. Mach leaves those to the BSD
  layer, which is why OSF/1 added `table()`.
- Guard. A task or thread right is a Mach port capability. Listing tasks needs a
  processor-set port, and the privileged processor-set port comes from a call that
  takes a `host_priv` port
  [R `OSF/kernel/kern/processor.c:952-974`, `OSF/kernel/mach/mach_host.defs:240-262`].
  Holding the right is the authorization; there is no uid test.
- Thumb-1 port estimate. Not portable: the model needs ports. The useful part is
  the field set of `task_basic_info` (virtual and resident size, user and system
  time), which the record already carries.

### AIX 4.1.3: `getprocs()` and `getargs()` syscalls

- `getprocs` returns an image of the process table; entries are individually
  consistent but the table is not [R `AIX/getproc.c:59-72`]. For each live
  process it holds `proc_tbl_lock` and `p_lock`, attaches the target's segment
  with `xmattach` to pin the u-area, copies `U_comm`, credentials, signal state,
  sizes and tty into a `procsinfo`, then detaches
  [R `AIX/getproc.c:150-215`].
- `getargs` reads the arguments from the target's stack at query time: it attaches
  the target address space with `xmattach`, fetches `argc` and `argv` from the
  stack top, then copies the strings one byte at a time with `xmemin` into the
  caller's buffer with `subyte`, stopping at the caller's length or `NCARGS`
  [R `AIX/getproc.c:484-640`]. It records no snapshot and may page fault
  [R `AIX/getproc.c:494-495` header comment].
- No privilege test appears in these two functions [I: a search for `suser`,
  `priv` in `AIX/getproc.c` finds none]. Any user can read any process's
  arguments.
- Same trade as `TBL_ARGUMENTS`: no per-process storage, kernel-mediated, byte-wise
  copy through the cross-memory service. Thumb-1 estimate 500 to 900 bytes with a
  compressed-image reader.

### Research Unix V7, V8, V10: raw memory, then a per-process `/proc` file

- V7. `ps` opens `/dev/mem` and `/dev/swap`, reads `/dev/mem` for `swplo`, walks
  the kernel proc table and reads each `struct user` from memory or swap by the
  `SLOAD` flag [R `V7/ps.c:109-130,213-222`]. A short read returns without a row
  [R `V7/ps.c:221-222`]. It reads the argument strings from the top of the
  process image: it finds the `sh` special pointer, then walks the image
  [R `V7/ps.c:279-295`]. This is the algorithm DiscoBSD inherited, including its
  silent drop.
- System III and V2 keep the algorithm and add a `-c corefile` option
  [R `extracted/archives/att/ATT-SYSIII/src/cmd/ps.c:103-114,252-257`;
  `.../sysvr2-vax/src/cmd/ps.c:175-190,339-343`].
- V8. `ps` is `-rwsr-xr-x` owned by root [R V8 member `bin/ps` mode `-rwsr-xr-x
  0/4`; `usr/src/cmd/ps/Makefile`: `chmod a=rx,u+ws`]. It lists `/proc`, opens
  `/proc/<pid>`, reads `struct proc` with `PIOCGETPR`
  [R V8 `usr/src/cmd/ps/doselect.c:15-30`; `usr/sys/h/pioctl.h`;
  `usr/sys/sys/proca.c:319-324`], and reads the u-area as file bytes at offset
  `UBASE` when the process is loaded or from `/dev/drum` at `p_swaddr` when it is
  not [R V8 `usr/src/cmd/ps/doselect.c:56-76`]. It reads the stack the same way and
  truncates the command line to 63 characters
  [R V8 `usr/src/cmd/ps/getargs.c:4-5,22-38`]. When the stack read fails it prints
  `(u_comm)` [R V8 `usr/src/cmd/ps/getargs.c:52`].
- V8 kernel. The `/proc` inode number is `pid + 64` (`PRMAGIC`), so naming a process
  costs no per-process storage [R V8 `usr/sys/sys/proca.c:41,74,133`]. A file
  is mode `IFREG | 0600` owned by the target's uid, and drops to mode 0 when the
  caller cannot read the process's text file, which blocks the image of an
  unreadable program [R V8 `usr/sys/sys/proca.c:76-86`]. I/O returns `EBUSY` when
  the process is locked, swapping, paging, exiting or in a vfork, a retry-able
  condition `ps` handles by looping four times
  [R V8 `usr/sys/sys/proca.c:21-24,421-440`;
  `usr/src/cmd/ps/doselect.c:56-72`]. `/proc` is mounted by `procmount`, a
  `gmount(2)` call on filesystem type 2 [R V8 `usr/src/cmd/procmount.c:1-12`].
- V10 keeps the same `ps`, `proca.c` and 63-character cap
  [R V10 `cmd/ps/getargs.c:4`; `sys/fs/proca.c:40,237`], with a slimmer busy mask
  `SLOCK|SPAGE|SKEEP|SWEXIT|SPROCIO` [R V10 `sys/fs/proca.c:23`].
- Thumb-1 estimate. V7: zero kernel bytes (current design). V8 `/proc`: 2 to 4 KB
  [I, see System V entry]. Rejected for the port.

### Tiny and embedded systems

- Coherent 4.2 (Mark Williams, 386). The memory driver carries a dedicated
  `/dev/ps` minor, read-only by an `IPR` open check
  [R `COH` `coh.386/null.c:18-23,97-102,191-208`]. A read walks the process queue
  under the process-table lock and writes one `stMonitor` record per process,
  stopping when the caller's buffer cannot hold another
  [R `COH` `coh.386/null.c:343-406`]. The record is pids, uids, state, flags, tty,
  nice, sizes in kilobytes, times, a 10-byte command, a 10-byte sleep-event name, a
  scheduling priority and a 64-byte `pr_argv` slot
  [R `COH` `include/sys/coh_ps.h:11-40`]. The kernel header comment calls the
  structure "fields of interest to the ps command" and says all new fields
  append at the end [R `COH` `include/sys/coh_ps.h:16,39`].
  Two defects sit in the source. The argument slot is copied from a local array
  that the loop never fills [R `COH` `coh.386/null.c:236,400`; a search for
  `psBuf` finds those two lines only], so the field would carry stack garbage.
  The command copy uses the 64-byte `ARGSZ` as its length into a 10-byte field
  [R `COH` `coh.386/null.c:371`; `p_comm` is 10 bytes at `include/sys/proc.h:84`]. The
  design is sound; this implementation never finished the argument part. Coherent
  has no `/dev/kmem` use for `ps`: `exec` stores a 10-byte `p_comm` in the resident
  proc [R `COH` `coh.386/exec.c:770`].
  Thumb-1 estimate: 150 to 300 bytes as a minor of the existing `mmrw` switch
  (measured `mmrw` 176 bytes); the transport is a character device and needs no
  file system.
- Xinu 68K. `ps` is a shell command linked into the kernel; it indexes `proctab`
  directly and prints `pname[16]`, state, priority, stack bounds and semaphore
  [R `XINU/sys/shell/x_ps.c:19-58`]. `pname` is copied at `create`
  [R `XINU/sys/create.c:52`]. One address space, no privilege boundary, no
  arguments kept (only `pargs`, a count).
- Minix: the corpus holds a NetBSD-derived userland tree
  (`archives/minix/UNRELEASED_UPDATED_MINIX.tar.gz`) whose `bin/ps` uses
  `kvm_openfiles` and sysctl `KERN_PROC_*`
  [R member `UNRELEASED_UPDATED_MINIX/bin/ps/ps.c:82,406-409`], which is the BSD
  design this report is not about. No Minix kernel or servers are held: the
  `Minix v3.zip` named in `REPORT-MACH-OSF.md` is absent from `archives/minix/`,
  and the `minix3.4.0rc6` ISO link under `vms/historic-machines` points into an
  empty `images/os-current/`. Not found.
- Plan 9 `/proc/n/status`, MP/M, QNX, seL4, L4: no Plan 9 source is held
  (`docs/papers/Computer_Science_Research/plan9.pdf` is the `rc` shell paper
  [R title lines of `pdftotext plan9.pdf`]); MP/M and QNX are absent from
  `CATALOG.tsv`; seL4 and L4 have no process-status tool. For the record, and as
  an inference from outside the corpus: Plan 9's `status` file is a fixed-width
  text record, and `args` returns a kernel-held copy of the command line saved at
  exec, so it belongs with the exec-time snapshot family. Not read in source here.

## Shared invariants

1. The tool never decodes a kernel structure. The kernel owns a fixed record with
   no kernel pointer (Solaris `psinfo_t` keeps `pr_addr`, `pr_wchan` addresses; a
   pointer-free port drops both). Evidence: `tbl_procinfo`, `stMonitor`,
   `prpsinfo`, `psinfo_t`.
2. The record is one atomic copy under the process lock. Solaris holds `p_lock`
   [R `S26/uts/common/fs/proc/prsubr.c:1243`]; AIX documents per-entry consistency
   [R `AIX/getproc.c:65-66`]; Coherent holds the process-table lock
   [R `COH` `coh.386/null.c:344`]. A reader sees a whole entry or none.
3. The record is bounded and NUL-terminated. Every fixed snapshot truncates to
   `PSARGSZ - 1` and terminates, and every one rewrites interior NULs to spaces
   before the tool sees the text (`machdep.c:228-236`, `sundep.c:1206-1218`,
   `exec.c:1685-1691`).
4. A busy process yields a retry code and an unreadable process yields a row. V8
   returns `EBUSY` and `ps` retries four times; Solaris and SVR4 return `EAGAIN`
   and `ps` retries; OSF/1 `ps` prints `[comm]`; SunOS 4 and V8 print `(comm)`.
   The single counterexample is the silent drop (V7, SunOS 4 `save()`, DiscoBSD
   `savcom`): every design that ships a record or a fallback string closed that
   hole.
5. A zombie has no arguments and no u-area. SunOS 4 returns empty
   [R `S413/bin/ps.c:1070`]; SVR4 skips the u-block
   [R `SVR4/uts/i386/fs/proc/prsubr.c:604-607`]; Solaris sets state `SZOMB`
   and zero lwp [R `S26/uts/common/fs/proc/prsubr.c:1275-1285`]; OSF/1 sets
   `PI_ZOMBIE` and no command [R `OSF/kernel/bsd/cmu_syscalls.c:593`].
6. Privilege attaches to the transport, never to the record's shape. Raw memory
   needs group `kmem` or root. Per-process files need the owner or root. A
   world-readable record is a policy choice (Solaris 0444), and the corpus's most
   recent tree adds a visibility gate in front of it (OpenSolaris
   `PRIV_PROC_INFO`). Mach alone puts authority in a capability, not a uid.

## The tradeoffs, with the evidence for each

| Axis | Choice | Cost the source shows | Gain the source shows |
|---|---|---|---|
| When arguments are captured | Copy at exec | K bytes of storage per process; shows the original argv even if the program rewrote it; truncates at K (SVR3 comment: 40 then 80) | O(1) read; independent of swap state; consistent with the process lock; the snapshot is already inside the data the system swaps anyway (SVR4 `u_psargs` swaps with the u-block) |
| | Read at query | No storage; but a swapped image needs I/O or decompression, the target must be locked against swap (V8 `EBUSY`, SVR4 `ub_lock`, AIX `xmattach`), and the result can change mid-read (AIX "not guaranteed consistent") | Always current; unbounded length up to `NCARGS`; can show environment (`TBL_ENVIRONMENT`) |
| Where a snapshot lives | In `struct proc` (Solaris 2.6 `p_user`, IRIX) | RAM times process count; this port's `proc` is 100 bytes pinned by `_Static_assert` [R `sys/arch/rp2040/rp2040/machdep.c:137`] and 16 slots [R `sys/arch/rp2040/include/machparam.h:70`] | Read without touching swap; no u-area decode |
| | In the swappable u-area (SVR3, SVR4) | Needs the u-area to be readable when swapped | Zero extra resident RAM; rides the existing u-area decode |
| | A pointer into the image (OSF/1 `uu_argp`, 6 bytes, 12 with env) | Needs a kernel image reader | Smallest per-process cost; no length cap beyond `u_short` |
| Interface | Raw memory (V7, SunOS 4) | Zero kernel code; `ps` bound to kernel struct layouts and symbol addresses; kmem group or root; silent short reads | Cheapest kernel |
| | Fixed record (OSF/1, Solaris, Coherent, SVR4) | 150 to 300 Thumb-1 bytes for assembly; a versioned layout | Pointer-free by construction; any user may call; the row survives a failed u read |

## Candidate designs for this port, scored

Port facts the scores rest on: `struct proc` is 100 bytes and `NPROC` is 16, so
the proc table is 1,600 bytes [R measured: `nm -S` `proc` size 0x640]; `struct
user` is 972 bytes with 52 bytes of layout space before the 2,048-byte kernel
stack, and `swapin` and `swapout` move all 3,072 bytes
[R `docs/research/swap-exhaustion/review-03.md:166`]; the kernel already stores
the argument pointer in the last word of the process window "for /bin/ps"
[R `sys/kern/exec_subr.c:283`] and copies `argv[0]` into the 64-byte `u_comm`
[R `sys/kern/exec_subr.c:329`, `sys/sys/user.h:22,27`]; `ps` reads
`ARGLIST` (2,048) stack bytes through `/dev/mem` or `/dev/swap`
[R `bin/ps/ps.c:334-341`]; the SwapRAM pool keeps three separately compressed
segments (data, stack, u) and decodes only the prefix of the u segment for the
present `machdep.swapram_psinfo` node
[R `sys/arch/rp2040/rp2040/swapram.c:495-506`,
`sys/arch/rp2040/rp2040/sysctl.c:229-270`].

| Rank | Design | Resident RAM | Kernel text (Thumb-1, estimate) | Privilege | Row survives a failed u read | Verdict |
|---|---|---:|---:|---|---|---|
| 1 | Exec-time snapshot inside `struct user`, returned by a pointer-free sysctl record | 0 | +40 to 60 at exec, +150 to 300 record; net +150 to +350 after the pooled-only node is folded in | Any user reads the record; args field gated by owner or root if policy wants it | Yes, from `proc` fields | Optimal |
| 2 | Same snapshot reusing `u_comm` as the joined argument string | 0 and no `struct user` growth | -10 to +30 versus the present `copystr` call | Same | Yes | Smallest byte cost; changes what `u_comm` means for core naming and the fault message |
| 3 | Snapshot inside `struct proc` (Solaris 2.6, IRIX) | 16 x K: 256 at K=16, 512 at K=32, 1,280 at K=80 | +40 to 60, +100 to 250 record | Same | Yes | Costs RAM the port lacks; alters the `proc` size pin and the `kern.proc` ABI that `ps`, `w`, `fstat` compile against [R `docs/research/hardware-review-reconciliation.md:91`] |
| 4 | Coherent-style `/dev/ps` stream of fixed records as a minor of the memory driver | 0 | +150 to 300 | File mode on the node | Yes | Sound; a sysctl node reuses the existing `kern.proc` plumbing and the MIB gate instead of a new device node |
| 5 | Kernel-mediated read at query (OSF/1 `TBL_ARGUMENTS`, AIX `getargs`) | 0 | +500 to 900 | Same | Yes | Needs a flash swap reader and a decode to the tail of the compressed stack |
| 6 | Hardened SunOS 4 and V7 raw memory (the tree today) | 0 | 0 | `kmem` group, `ps` setgid | No: `savcom` returns 0 at `bin/ps/ps.c:476-477` | Works; keeps the memory devices open and the silent drop |
| 7 | `/proc` file system (V8, SVR4, Solaris) | per-vnode state [I] | 2,000 to 4,000 [I] | Per-file mode, setuid `ps` | Yes | Does not fit the budget; rejected |
| 8 | Mach `task_info` and `thread_info` | n/a | n/a | Port capability | n/a | No command or arguments; no ports on this kernel |

## The design this report recommends

**Name: argument snapshot at exec in the swappable `struct user`, served by one
pointer-free process record.**

Mechanism, as the sources support it:

1. In `exec_setupstack` (`sys/kern/exec_subr.c`), after the argument strings are
   copied and before the environment, copy at most `PSARGSZ - 1` bytes of the
   contiguous argument region into `u.u_psargs`, rewrite interior NULs as spaces
   and terminate. The Solaris copy is the model
   [R `S26/uts/sparc/os/sundep.c:1206-1218`]. Reuse the 64-byte `u_comm` for the
   short name as today. Place `u_psargs[48]` at the end of `struct user`: 972 plus
   48 is 1,020, inside the 1,024-byte limit already asserted in
   `sys/kern/subr_capacity.c:34`. The 3,072-byte `USIZE` does not change, so
   resident RAM does not change and flash-swap traffic does not change.
2. Serve a fixed record, such as a `KERN_PROC_PS` sub-node beside `sysctl_doproc`,
   holding `u_comm`, `u_psargs`, times, sizes, tty, two signal dispositions and
   state. The resident case reads the `struct user` at `p_addr`; the SwapRAM case
   calls `swapram_uarea_prefix` as the present node does; the flash case reads
   the first swap block of the image with `geteblk`, the strategy routine and
   `brelse`, the same buffers the present node uses. The existing `proc` fields
   fill every field that needs no u-area, so a failed u read still produces a row
   with `(comm)` as the command.
3. `ps` stops opening `/dev/kmem`, `/dev/mem` and `/dev/swap` for the process
   rows. It keeps them only for the symbol names `-l` prints for wait channels,
   behind the existing `kmem` group, or drops the wait-channel column for
   unprivileged callers.

Why this wins on the port's constraints:

- Zero resident bytes. Each rival that stores bytes per process
  (Solaris 2.6 and IRIX in `proc`) costs 256 to 1,280 bytes of the 43 KB bss the
  kernel already uses (`arm-none-eabi-size` bss 43,136).
- The argument block of a pooled image sits at the end of the compressed stack
  segment [I], while the u segment decodes from its first byte and the pool
  stops decoding at the requested length
  [R `sys/arch/rp2040/rp2040/swapram.c:495-506`]. Putting the snapshot in the u
  area keeps the read at one short decode.
- No new memory exposure. The stack that holds the same strings is already written
  to flash swap whole and compressed into the pool.
- Net kernel bytes land near +150 to +350, because the pooled-only node's
  decoding is a subset of the general node. That is below the earlier
  review's 500 to 900 byte estimate for argument reconstruction.
- The privilege problem disappears by construction: no mode on a device node
  decides who reads kernel memory, because ordinary users never read it.

What the design gives up, each with the source that shows the same loss in the
original:

- Only the exec-time arguments are shown. A program that rewrites `argv` (a title
  setter) is not reflected; SVR4, Solaris and IRIX share the limit.
- Arguments cap at 47 characters on screen (Solaris caps at 79; SVR3 started at
  39). `ps -w` style full lines and `ps e` (environment) disappear; SunOS 4 and
  OSF/1 `TBL_ENVIRONMENT` kept them by reading the stack.
- WAIT-CHANNEL names need `nlist`, which is a root-only sysctl on this port
  [R `docs/research/ps-kernel-memory-boundary-review.md`, first paragraph]. A
  pointer-free record cannot carry the address, so `WCHAN` is blank for
  unprivileged callers.

## Falsification criteria for the recommended design

Run these before the design lands. Each one, if it fails, retires part of the
claim above.

1. `arm-none-eabi-size` on the rebuilt PICO kernel shows a text delta between
   +100 and +450 bytes against the baseline. A result above +450 retires the
   byte estimate and moves the design toward the `u_comm` reuse variant.
2. `sizeof(struct user)` is at most 1,024 after the field is added, as the
   existing `_Static_assert` in `sys/kern/subr_capacity.c:34` requires, and
   `UAREA_STACK_OFFSET` consumers (heatshrink config, `capacity_state`) still
   pass.
3. A resident, a flash-swapped and a SwapRAM-pooled process of identical argv
   report identical `u_psargs` through the record. Any difference means the pool
   prefix or the flash first-block read decodes a different region.
4. With the u read forced to fail, `ps` still prints the row with `(comm)`. The
   silent-drop line at `bin/ps/ps.c:476-477` is the known-bad input.
5. `ps -ax` run by `operator` succeeds with `/dev/kmem`, `/dev/mem` and `/dev/swap`
   mode 0640 root:kmem and `ps` not setgid. The Renode stage that already refuses
   the operator those devices is the oracle.
6. A shebang script run through an interpreter reports the interpreter and script
   words, as Solaris does by starting its copy before the interpreter strings
   [R `S26/uts/sparc/os/sundep.c:1162-1189`]. The check follows from DiscoBSD's
   final argv after shebang processing and has not been read in the port's
   `exec_aout` path.

## Decisions that belong to the owner

- Argument visibility. Solaris publishes arguments to every user (0444). OpenSolaris
  hides other users' processes unless the caller holds `PRIV_PROC_INFO`. The port
  can show arguments to everyone, blank them for other users, or hide the process
  entirely. The gate costs about 12 to 20 Thumb-1 bytes at the record fill.
- Snapshot placement variant: add 48 bytes to `struct user`, or reuse the 64-byte
  `u_comm` as the joined string. The first keeps `u_comm` as a file name for core
  naming at `sys/kern/kern_sig.c:564` and the fault message at
  `sys/arch/rp2040/rp2040/fault.c:260`; the second saves the 48 bytes.

## Not found in the corpus

Plan 9 source (`/proc/n/status`, `/proc/n/args`), MP/M, QNX, Minix kernel and
servers, a Minix `ps` that reads `PM` or `SYSTEM` tables, and a V6 `ps` kernel
interface beyond the V7 algorithm above. The V8 and V10 `ps` programs use
`/dev/kmemr` for file selection only and are not a separate mechanism.

## Falsification search

Claim under test: no system in the corpus has met and solved the class of
failure that the RP2040 `ps` shows, namely a user-space reader of raw kernel
memory or swap that diverges from a kernel layout it does not model. The port's
Renode probes attribute four failures to one root cause: `ps` knows neither the
swap ring (a swapped image's block address can fold past the device end back to
the ring start, so a 972-byte `struct user` that straddles or passes the seam
reads short and the row drops; argument strings read the same way return
garbage) nor which RAM `/dev/mem` admits (the memory driver admits kernel data
and flash only, so the resident user areas of `ps` itself and of the swapper
always fail with `EFAULT` and always drop).

Prediction fixed before the search: if the claim holds, every `ps` in the corpus
either models the layout and breaks when it drifts, or avoids raw reads by
asking the kernel, and none contains a layout-negotiation mechanism between a
raw reader and a kernel. A single reader that survived a drifting layout, or a
documented rationale for leaving raw reads, falsifies it.

**Verdict: the claim fails as stated.** Four systems met the class and each
solved it by one of three routes. A narrower claim survives and is the one the
recommended design rests on: no corpus system solved it for a reader that
decodes raw memory without either modeling the kernel's layout from the kernel's
own headers or asking the kernel for the answer. Every working design models,
asks, or negotiates.

### Hits that falsify the strong claim

Route one, model the layout and ship it with the kernel (SunOS 4.1.3):

- `libkvm` carries its own copy of the swap geometry. `_anonoffset` reads the
  chain of `swapinfo` structures from the kernel and accumulates each swap
  file's page count into a running offset, converting an anon pointer into an
  offset in the "unified swap file"
  [R `S413/usr.lib/libkvm/kvmgetcmd.c:736-801`, accumulation at `:764-785`].
  That is the seam the port's `ps` lacks: where one swap area ends and the next
  begins, the library computes the fold instead of reading linearly. The symbol
  it needs, `_swapinfo`, is in its `nlist` table
  [R `S413/usr.lib/libkvm/kvmopen.c:37,330`].
- The library walks the segment and anon-map structures of the target
  [R `S413/usr.lib/libkvm/kvmgetcmd.c:300-345,436-478`] and looks a resident page
  up through the kernel's page hash
  [R `S413/usr.lib/libkvm/kvmgetcmd.c:1200-1250`; `kvmopen.c:332-333`]. It knows
  which memory exists because it reads the kernel's own `memseg` list
  [R `S413/usr.lib/libkvm/kvmopen.c:328-329`]; the driver agrees, since `/dev/mem`
  admits exactly the pages in the kernel's `physmemory` list and faults outside
  it [R `S413/sys/sun4c/mem.c:150-161`]. The ps-side half of the port's second
  failure therefore cannot occur there: the reader and the driver share one
  definition of "which RAM".
- When the reader cannot do the arithmetic it says so with a status instead of
  returning bytes: `_swapread` returns -1 for any read that is not
  block-aligned, with the comment "swap offsets not implemented"
  [R `S413/usr.lib/libkvm/kvmgetcmd.c:719-731`]. The port's `ps` returns short
  data that later code interprets as a different process.
- Coping with drift is by construction, not by detection: the library and the
  kernel ship in one release (every file carries the SCCS stamp `1.1 92/07/30`,
  for example `S413/usr.lib/libkvm/kvmgetu.c:2`), and `libkvm` compiles against
  the kernel headers it decodes (`vm/as.h`, `vm/seg_vn.h`, `vm/anon.h`,
  `vm/swap.h`) [R `S413/usr.lib/libkvm/kvmgetcmd.c:10-27`]. The namelist is the
  on-disk `/vmunix` unless the caller names another
  [R `S413/usr.lib/libkvm/kvm_impl.h:20`]. I found no version handshake: a
  search of `kvmopen.c`, `kvmrdwr.c` and `kvm_impl.h` for `version`, `mismatch`,
  `drift` and `consisten` returns nothing, and the `version` file holds the
  string `0.3` [R `S413/usr.lib/libkvm/version:1`].

Route two, delegate the one piece the reader cannot model to the kernel:

- SunOS 4 `kvm_getu` reads the `p_uarea` virtual address through `/dev/kmem`
  and states why: "On a running system references to these addresses will
  automatically fault the u-area pages back in as needed"
  [R `S413/usr.lib/libkvm/kvmgetu.c:34-37`]. `kvm_as_rdwr` prefers the kernel's
  virtual-address read over its own translation: "if virtual addresses may be
  used, by all means use them"
  [R `S413/usr.lib/libkvm/kvmrdwr.c:113-121`]. The driver does the swap-in
  because it dereferences a kernel address
  [R `S413/sys/sun4c/mem.c:207-217`]. This is the model the port's `/dev/kmem`
  lacks for a swapped `struct user`: the port's driver rejects an address
  outside kernel data and flash and cannot fault an image in.
- SVR3 on the 3B2 replaced the user-space page-table walk with a system call.
  `ps` calls `sys3b(RDUBLK, pid, &u, sizeof u)` and returns 0 on failure
  [R `SVR3/cmd/ps/ps.c.unpacked/ps.c:935-938`]. The kernel locates the pid with
  `prfind`, rejects an idle, free or zombie process, and for an image that is not
  resident reads the u-block from the device and block named by the process's
  own swap descriptor, `swaptab[dbd->dbd_swpi].st_dev` and `dbd->dbd_blkno`
  [R `SVR3/uts/3b2/os/sys3b.c.unpacked/sys3b.c:676-722`, swap lookup at `:703`];
  for a resident one it copies page by page from the user page table
  [R `SVR3/uts/3b2/os/sys3b.c.unpacked/sys3b.c:709-719`]. The case body holds no
  `suser()` test (the only two tests past line 600 are at `:632` and `:794`,
  outside it), so any user may read any process's u-block [I: absence of a check
  in the cited lines]. The same release keeps the user-space walk for the VAX
  (`ps.c:940-951`, page tables through `/dev/mem`) and the 3B20
  (`ps.c:926-933`), which is the evidence that the walk was the thing being
  replaced.
- Solaris keeps the `libkvm` API but removes the modeling. `kvm_getu` returns
  `&p->p_user`, because the user structure is a field of the resident proc
  [R `OSN/lib/libkvm/common/kvm.c:426-428`]; `kvm_uread` on a live system opens
  `/proc/<pid>/as` and lets the kernel resolve the address
  [R `OSN/lib/libkvm/common/kvm.c:280,319-322`]; the symbol table comes from
  the kernel itself as `/dev/ksyms`
  [R `OSN/lib/libkvm/common/kvm.c:163`]; `kvm_open` refuses a kernel of the
  wrong word size with "is not a %d-bit kernel namelist"
  [R `OSN/lib/libkvm/common/kvm.c:168-170`], and the manual states the dependence
  and the refusal [R `OSN/man/man3kvm/kvm_open.3kvm:156-166`]. The manual lists
  `kvm_read` and `kvm_write` as Obsolete and the rest Stable
  [R `OSN/man/man3lib/libkvm.3lib:117`]. This is the whole of the "move" from
  `/dev/kmem` that the corpus shows. No sentence states why the move was made
  (see the search ledger).
- Research V8 keeps a raw reader for the swapped case and patches drift by hand.
  `ps` reads a swapped u-area from `/dev/drum` at `p_swaddr * NBPG`
  [R V8 `usr/src/cmd/ps/doselect.c:73-76`] and the swapped stack through the
  per-process swap map `u_smap.dm_map[...] + DMMIN`
  [R V8 `usr/src/cmd/ps/getargs.c:26-28`]. The size of that map is a compile-time
  constant of the kernel, 64 with `BIGMEM` and 32 without
  [R V8 `usr/sys/h/dmap.h:5-11`], and `ps` learns the kernel's value from an
  operator-maintained file: `delta = atoi(cbuf) - NDMAP` read from `/etc/ndmap`
  [R V8 `usr/src/cmd/ps/getargs.c:66-80`]. A search of the whole V8 archive for
  the string `ndmap` finds that consumer only, with no producer [I: the file is
  written by hand or by a site script not in the archive]. It is the one
  out-of-band layout-negotiation hack in the corpus, and it exists because the
  raw reader diverged from the kernel it ran on.
- The V8 `/proc` file system moves the live-image case into the kernel. Its
  manual says `PIOCGETPR` exists because the proc entry "resides in system
  space, it is not accessible via a normal read"
  [R V8 `usr/man/man4/proc.4`, `PIOCGETPR` entry], and the lock path "also causes
  swapin" before any transfer [R V8 `usr/sys/sys/proca.c:441-442`]. The manual
  records the cost: "A process must be swapped in for reading and writing (but
  not ioctl); this consumes minimal system resources, but may involve a
  noticeable delay" [R V8 `usr/man/man4/proc.4`, BUGS]. `ps` still reads `/dev/drum`
  for the swapped case, so V8 solved the resident half in the kernel and left the
  swapped half with `ps`.

Route three, negotiate the record size so a raw layout can drift:

- OSF/1 `table()` states its purpose in the header comment. It is a "general
  purpose mechanism for retrieving or updating individual or sequential
  elements of various system tables and data structures"; a future use is "to
  make most of the standard system tables available via this mechanism so as to
  permit non-privileged programs to access these common SYSTAT types of data"
  [R `OSF/kernel/bsd/cmu_syscalls.c:70-76`]. The `lel` argument is "the
  expected size of a single element": a smaller value truncates, a larger one
  leaves holes, and "the intent ... is to insulate user programs as much as
  possible from the common change in the size of system data structures when a
  new field is added. This works so long as new fields are added only to the
  end, none are removed, and all fields remain a fixed size"
  [R `OSF/kernel/bsd/cmu_syscalls.c:85-95`; applied at `:846` as
  `size = MIN(size, uap->lel)`]. `ps` passes `sizeof(proc[0])` as that argument
  [R `OSF/usr/bin/ps/ps.c:1706`]. This is the corpus's only written statement of
  the rule that fixes layout drift for a record reader. It is in tension with
  the stated intent: the implementation then requires `suser` to read another
  process [R `OSF/kernel/bsd/cmu_syscalls.c:553-556`], so `ps` stays set-user-id.
- Mach `task_info` takes a flavor and a count and returns
  `MIG_ARRAY_TOO_LARGE` with the needed count when the caller's array is short
  [R `kernel_interface.ps` task_info entry (text converted with `gs
  -sDEVICE=txtwrite`), under `archives/mach/cmu/doc/osf/`]; the kernel code
  rejects a wrong flavor or a short count with `KERN_INVALID_ARGUMENT`
  [R `OSF/kernel/kern/task.c:744-749`]. The same document says the interface
  gives "various random statistics about the task" through `task_info`
  [R `kernel_principles.ps`, task section]. The stated privilege model is the
  name port versus control port split: "many entities have a name port and a
  control port ... Access to the control port allows the entity to be
  manipulated; access to the name port simply names the entity, for example, to
  return statistics" [R `kernel_principles.ps`, ports section]. No sentence in
  the Mach documents compares `task_info` with reading kernel memory; the choice
  is implicit in the port model, which has no memory device [I].

### Corroborating hits on the failure itself

- The failure the port shows is documented as a textbook problem. Bach, chapter
  10 exercise 24: "In traditional implementations, ps reads the information in
  the process table directly from kernel memory. Such a method is unstable in a
  development environment where the size of process table entries changes and ps
  cannot easily find the correct fields in the process table. Encode a driver
  that is impervious to a changing environment"
  [R `docs/papers/Computer_Science_Research/01095_thedesignofunixoperatingsystem_m_bach.pdf`,
  PDF page 369]. The chapter-10 text on software devices says "The ps command,
  for instance, reads kernel data structures from physical memory"
  [R same file, PDF page 327]. The Coherent `/dev/ps` driver in the main section
  is such a driver.
- SunOS 4 `ps` drops the row exactly as the port's does: `save()` returns when
  `kvm_getu` fails [R `S413/bin/ps.c:901-903`]. The library man page admits the
  gap: with no swap file given, `kvm_getu` "may fail if the desired information
  is swapped out" [R `S413/man/man3/kvm_open.3k`, `swapfile` paragraph], and
  `kvm_getcmd` makes "the best attempt possible, returning -1 if the user
  process data is unrecognizable" [R `S413/man/man3/kvm_getu.3k`, last
  paragraph].
- Three programs in the corpus print a row, or a reason, instead of dropping
  silently, which is the behavior the port's `ps` should have on a failed u read:
  V8 `ps` prints `ps <pid>: drum read error` through its `Error` macro and moves
  on [R V8 `usr/src/cmd/ps/doselect.c:8-9,74-76`]; SVR3 3B15 `ps` prints "ps can
  not read <file>" when `/dev/mem` refuses the u-block
  [R `SVR3/cmd/ps/ps.c.unpacked/ps.c:915-921`]; SVR3 and SVR2 `ps` zero the
  u-block when a swap read returns no bytes under `-s` and still print the
  process-table fields (`uzero`)
  [R `SVR3/cmd/ps/ps.c.unpacked/ps.c:962-970,1053-1054`;
  `extracted/archives/att/sysvr2-vax/src/cmd/ps.c:670-676`]. The 3B2 SVR2 `ps`
  computes the swapped u-block address as `swplo + p_swaddr +
  ctod(p_swsize - USIZE - p_ssize)`, a user-space model of the swap image
  layout, with that same fallback
  [R `extracted/archives/att/sysvr2-vax/src/cmd/ps.c:655-676`].

### What the hits do not contain

- A drift mechanism in the SunOS 4 library: no version check, no layout
  hash, nothing that detects a `struct user` or `struct proc` of a different
  size. It works because the library and kernel ship together.
- Any corpus system that reads a swap image written as a ring and handles the
  fold in user space. The closest are the cumulative `swapinfo` offsets (SunOS 4)
  and the per-chunk swap map (V8), both of which model a layout that has chunk
  boundaries but no wrap. The port's ring fold has no precedent in the sources I
  read; its seam is a property of this kernel's allocator that only the kernel
  can state [I].
- A rationale sentence for Solaris. See the next section.

### Search ledger (what was run, so absence is checkable)

Each entry is a command class and its scope; a "no hits" line means the pattern
returned nothing in that scope.

- SunOS 4.1.3 library behavior and drift: read `S413/usr.lib/libkvm/*.c`,
  `kvm_impl.h`, `kvm.h`, `version`, `S413/man/man3/kvm_{open,getu,getcmd}.3k`;
  `grep -n -i 'drift|version|mismatch|does not match|out of date|consisten'` over
  `kvmopen.c kvmrdwr.c kvm_impl.h kvm.h` returned only the `kvm_open` description
  comment, so no hit.
- Solaris rationale: `grep -rIl -i PSARC` over `solaris/2.6/extracted`,
  `solaris/2.8/extracted` and `sunos/4.1.4/extracted` listed device-driver and
  installer files only; `grep -rIn -i 'PSARC|ARC case|/dev/kmem|libkvm|kvm_'` over
  `S26/uts/common/fs/proc`, `S26/cmd/ps`, `S26/head/procfs.h`,
  `S26/uts/common/sys/procfs.h`, the same five paths under
  `solaris/2.8/.../osnet_volume/usr/src`, and `OSN/uts/common/fs/proc`,
  `OSN/cmd/ps`, `OSN/man/man4/proc.4` returned no hits. `grep -rIl -i 'psargs|psinfo'`
  over the Schillix `webrev` archive directory listing returned no hits (the
  webrev items are compressed archives that I did not unpack, so this covers
  their names only). The nearest statements are structural: `procfs.h` records
  that the ioctl interface "will be removed in a future version of Solaris"
  [R `S26/head/procfs.h:12-17`, `S26/uts/common/sys/procfs.h:12-20`];
  `prvnops.c` explains why `psinfo` takes no full lock ("world-readable and never
  goes invalid") [R `S26/uts/common/fs/proc/prvnops.c:640-645`]; and `proc.4`
  says `psinfo` is "needed by the ps(1) command", survives a zombie, and that
  fields such as `pr_addr` "refer to internal kernel data structures and should
  not be expected to retain their meanings across different versions of the
  operating system" [R `OSN/man/man4/proc.4:1338-1345,1394-1399`]. That last
  sentence is the corpus's one explicit split of a record into a stable part and
  a version-bound part, and it names `pr_addr`, the kernel-address field a
  pointer-free record for the port would omit.
- OSF/1 rationale: read `OSF/kernel/bsd/cmu_syscalls.c:1-108` (comment block
  above); converted `docs/osf/OSF1_Internals_Vol1_1992.pdf` and
  `OSF1_Internals_Vol2_1992.pdf` with `pdftotext -layout` and ran
  `grep -n -i 'tbl_|table system call|table (|ps command|ps(1)|/dev/|kmem|task_info|utask|u-area'`:
  no hit on `table()` or `ps`; the volumes cover task and port tables of Mach
  and STREAMS, not process status.
- Mach rationale: converted `archives/mach/cmu/doc/osf/kernel_principles.ps` and
  `kernel_interface.ps` with `gs -sDEVICE=txtwrite` and grepped for
  `task_info|thread_info|kmem|/dev/mem|name port|control port`; read the hits
  quoted above. Also read `docs/mach/johannes_helander-unix_under_mach-the_lites_server.pdf`
  (`pdftotext`, grep `kmem|/dev/mem|psinfo|table(`: no hits). The Lites 1.1 server
  tarball holds the 4.4BSD `miscfs/procfs` and no `ps` program (member list of
  `archives/mach/cmu/src/lites/lites-1.1.tar.gz`), so it is BSD lineage and outside
  this report.
- System V swapped-process handling: read `SVR3/cmd/ps/ps.c.unpacked/ps.c:844-1290`,
  `extracted/archives/att/sysvr2-vax/src/cmd/ps.c:640-700`,
  `SVR3/uts/3b2/os/sys3b.c.unpacked/sys3b.c:660-725`, and
  `SVR4/uts/i386/fs/proc/prmachdep.c:48-73`; `grep -n 'RDUBLK'` over the SVR3
  `uts` tree found the single handler.
- Killian, "Processes as Files": not held. Bach cites it [R Bach PDF page 374]
  and the corpus search `find docs books -iname '*killian*' -o -iname '*proc*fs*'`
  returned only unrelated names. The V8 manual page above is the only rationale
  text for the first `/proc`.
- Plan 9, QNX, MP/M, Minix kernel: no source in the corpus (see "Not found").

### Consequence for the recommended design

The falsified claim changes two sentences of the recommendation, not its content.

1. The record design has direct precedent for the swapped-image problem: SVR3
   3B2 `RDUBLK` reads the u-block from the device and block the kernel's own swap
   descriptor names, SunOS 4 `kvm_getu` leaves the swap-in to a kernel-address
   read, and Solaris and V8 resolve the live image inside the kernel. Each moves
   the ring or chunk arithmetic to the code that owns the allocator, which is
   what `swapram_psinfo` already does for the pooled case and what a flash path in
   the same node would do for the ring fold.
2. The port needs one more rule than the precedents supply, because the seam is
   not a chunk boundary: the kernel record must be assembled by the swap code that
   wrote the image, never by a block address the tool computes. SunOS 4 libkvm,
   the only corpus reader that models swap geometry in user space, depends on
   shipping with its kernel and still returns -1 on an offset it cannot
   compute.

Falsification criteria for this section's own conclusion: a corpus reader that
reads a wrapped swap region linearly and recovers (none found in the files
listed in the ledger); a stated Solaris rationale for leaving `/dev/kmem` (none
found in the paths searched, so the absence is bounded by that scope and the
unopened `webrev` archives and Mercurial history under `OSN/../schillix-on-hg-2017-07-25.tar.xz`).
