# NetBSD 1.0 against DiscoBSD/rp2040: what fits the tinyspace

This note is named netbsd110 so that NetBSD 1.0, the 1994 release it
reads, is not mistaken for NetBSD 10 or the 11.0 line that carries the
name today; the disparity between the two is the point of the note.

Authority for NetBSD 1.0: the `netbsd-1-0` branch of
`Oichkatzelesfrettschen/src-netbsd`, a git conversion of NetBSD's CVS,
at commit `94ac458bb17df2e7dd6716a2ed86c153bd3b8110` (1994-11-29, "patch
6: fix core dump on end of group"). The SHA is the conversion's own
identifier; what ties it to NetBSD's history is the branch name, which is
the project's `netbsd-1-0` maintenance branch, and `NetBSD1_0` in
`sys/sys/param.h`. That is the 1.0 branch six weeks after the 26 October
1994 release, not trunk. Authority for DiscoBSD: this tree, `BSD 211` in `sys/sys/param.h`,
PICO `Config`, `sys/arch/rp2040/include/machparam.h`,
`sys/arch/rp2040/conf/RP2040.ld`, `share/mk/sys.mk` (`-std=gnu17`).

This note is the investigation. It does not change code. `bsd44-backport.md`
already surveyed 4.4BSD-Lite2 against an earlier 96 KB window; several of
its top picks have landed. NetBSD 1.0 is contemporaneous with 4.4BSD-Lite
and is not a second independent source for those files. What follows names
the overlap, then the remainder that still has a tinyspace case.

## What NetBSD 1.0 is

NetBSD 1.0 is the first numbered NetBSD release: 4.4BSD-Lite plus the
386BSD/NetBSD 0.9 portability work, in one tree, for twelve machines
(`sys/arch`: amiga, da30, hp300, i386, m68k, mac68k, mips, pc532, pmax,
sparc, sun3, vax). The kernel is a 32-bit virtual-memory UNIX:

- vnode VFS (`sys/kern/vfs_*.c`, `sys/sys/vnode.h`)
- 4.4BSD VM (`sys/vm/`, pmap per arch); UVM does not exist yet (NetBSD 1.4)
- mbuf networking (`sys/kern/uipc_mbuf.c`, `sys/netinet`)
- NFS, FFS, LFS, MFS, msdosfs, isofs, adosfs, miscfs (procfs, fdesc, kernfs,
  null, union, umap, portal)
- sysctl, ktrace, LKM (`sys/kern/kern_lkm.c`), DDB, SCSI, autoconf
  (`sys/kern/subr_autoconf.c`)
- POSIX.1 libc with the 4.4BSD stdio rewrite, Berkeley DB, POSIX regex,
  locale/rune, `fts`/`glob`, termios

Counts on this checkout: 4049 `.c` files, 2063 `.h` files, 945 of the C
files under `sys/`. DiscoBSD has 1875 `.c` files in the whole tree.

`param.h` stamps `BSD 199306`, `BSD4_4 1`, `NetBSD 1994100`, `NetBSD1_0 1`.
DiscoBSD stamps `BSD 211` and `DiscoBSD 202608`. They share a grandparent
(Berkeley) and diverge at the 16-bit 2BSD line versus the 32-bit 4BSD line.

## What DiscoBSD/rp2040 is, as a filter

The PICO kernel is 2.11BSD-lineage, one UFS root on Dhara, raw flash swap,
no vnode, no mbuf stack (`init_sysent.c` routes sockets to `nonet`/`nosys`
when `INET` is off; PICO Config does not set `INET`). One process image is
resident in a 144 KB window (`USERRAM` at `0x20000000`); `NPROC` is 25;
the rest live on 384 KB flash swap plus a 16 KB SwapRAM tier. The kernel
flash region is 128 KB; the linked PICO `unix.map` puts boot2 plus text
and rodata at about 99 KB. Root is 1536 KB Dhara, about 989 KB of blocks.

Cross compilation is GNU C17 (`-std=gnu17 -fno-common`) for kernel and
userland. On-device compilation is Smaller C (`usr.bin/smlrc`), which is
not C17: no preprocessor, no headers on the root, no K&R definitions, a
Thumb-1 back end. A libc member is built by the cross compiler and is
available to native programs as a symbol in `libc.a`. A header a native
program would `#include` is unused on the board; native sources declare
what they call. C17 features that only the cross compiler sees are still
usable in kernel and shipped userland.

The 96 KB window in `bsd44-backport.md` and in `usr.bin/smlrc/README.rp2040.md`
is stale. The live figure is 144 KB.

## Method

Set-diff of `bin/`, `sbin/`, `usr.bin/`, `include/`, `lib/libc/{gen,stdlib,string,stdio}/`,
`sys/sys/`, then read the NetBSD 1.0 files that DiscoBSD lacks. Each
candidate is scored against:

1. Does the mechanism exist here already under another name?
2. Does it need vnode, mmap, mbuf, pty, kvm, or a kernel hook this PICO
   Config does not compile?
3. Does the extra RAM or flash exceed the 144 KB window / ~29 KB kernel
   slack / remaining root blocks?
4. Can the 1994 source be rewritten as GNU C17 without `__P`, K&R
   definitions, `register`, or `machine/ansi.h`, and still compile with
   `-Wall -Wextra` under the existing `WARNERR`?

A candidate that is also in 4.4BSD-Lite2 is marked as such. Prefer the
Lite2 text when both exist: Lite2 is a year later and this tree already
took files from it.

How found: `ls` set-diff of both trees; `wc -l` on named sources; `rg`
for `strmode`, `tcgetattr`, `sysconf`, `fts_open`, `glob(`, `utime`,
`msgbuf`, `crunchgen`; reading `PICO/Config`, `param.h` on both sides,
`share/mk/sys.mk`, `sys/sys/cdefs.h`, `lib/libc/gen/malloc.c` on both
sides. Falsifier for any "NetBSD 1.0 has X and DiscoBSD needs it" row:
the symbol or equivalent already exists here, or the dependency is a
subsystem PICO Config omits.

## Kernel: subsystem by subsystem

NetBSD 1.0 `sys/kern` is a different operating system. The files that
share names (`kern_fork.c`, `kern_sysctl.c`, `tty.c`, `vfs_vnops.c`) do
not share the inode/u-area/swap contract this port runs.

| NetBSD 1.0 piece | Lines (this checkout) | DiscoBSD | Verdict |
| --- | --- | --- | --- |
| vnode VFS (`vfs_*.c`, `vnode.h`) | thousands | inode UFS in `sys/kern/ufs_*.c`; `inode.h` states 2.11BSD has no vnodes | reject; 128 KB kernel cannot carry a second FS layer |
| 4.4BSD VM / pmap | `sys/vm/` | `vm_swap.c` / `vm_swp.c` / `vm_sched.c`; no MMU | reject |
| mbuf / uipc / netinet | `uipc_mbuf.c` plus `sys/net*` | sockets stub to `nonet` | reject |
| NFS, LFS, MFS, msdos, iso, ados, miscfs | whole `sys/{nfs,ufs,msdosfs,isofs,adosfs,miscfs}` | `NMOUNT=1`, `SINGLE_UFS_ROOT` | reject |
| `kern_malloc.c` (zone allocator) | 383 | 2.11BSD rmap (`subr_rmap.c`) plus fixed tables | reject; a general kernel malloc spends SRAM this port recovered by compacting inode/swapmap/buf |
| `subr_autoconf.c` + `device.h` | 359 plus per-driver `cfdata` | static `conf.c` + `config(8)` `devspec` tables | reject; one flash disk, one USB console, one UART, one LED. Probe-and-attach buys nothing on a board whose devices are wired in the linker script |
| `kern_lkm.c` | file present; comment says unload is unsafe | none | reject; no MMU isolation, 29 KB kernel slack, and the 1994 author already flags it |
| `kern_ktrace.c` | present | `options SYSTRACE` / `kern.systrace` | reject as a port; the tracing need is already a smaller local mechanism |
| sysv shm/sem/msg | `sysv_*.c` | none | reject; shm without an MMU is a shared buffer, which `kern_glob.c` already is (256-byte root-only `rdglob`/`wrglob`) |
| `vfs_cluster.c` | 769 | `LINEAR_BUFFER_CACHE`, `NBUF=4` | reject; clustered I/O wants a vnode pager and more than four buffers |
| `tty.c` termios (`sys/sys/termios.h`, 281) | kernel termios | kernel sgtty (`sys/sys/tty.h`, `sgtty.h`); `include/termios-todo.h` is a draft; `lib/libtermlib/tcattr.c` is `#if 0` | do not lift NetBSD termios into the kernel. Board `bin/sh/edit.c` uses sgtty (`CBREAK`, not RAW) so Ctrl-C still hits `ttyinput()`. Host builds use termios behind `HOSTBUILD` |
| `subr_prf.c` / `subr_log.c` | present | both present; PICO Config does not attach `log` | keep the 2.11BSD log; a dmesg port is userland, below |
| `queue.h` | 247, Berkeley 8.4 | 259-line 8.5 from Lite2 already in `sys/sys/queue.h` | already have the later text |
| `cdefs.h` | `__P`, `__BEGIN_DECLS`, `__CONCAT` | 27-line file with `__unused` only | take the *idea* (one home for attributes), not the 1994 macros. C17 makes `__P` a defect |

The MI/MD split in NetBSD 1.0 (`sys/arch/$MACHINE` plus `sys/arch/m68k`
shared) is the same shape `sys/arch/rp2040` already has. Copying the 1994
framework would replace a working `files.rp2040` / `config` path with a
larger one.

## libc: what is already here

DiscoBSD `lib/libc/gen` is not a small 2.11BSD gen. It already contains,
among others: `err.c`, `vis.c`, `unvis.c`, `fnmatch.c`, `getcwd.c`,
`daemon.c`, `sysctl.c`, `syslog.c`, `setmode.c`, `scandir.c`, `malloc.c`
(first-fit circular, PDP-11 `GRANULE` still in an `#ifdef pdp11`),
`qsort.c`. `lib/libc/stdlib` has `getopt.c`, `getsubopt.c`, `heapsort.c`,
`bsearch.c`. `lib/libc/string` has `strlcpy.c`, `strlcat.c`, `strsep.c`,
`strtok_r.c`. Headers: `err.h`, `sysexits.h`, `stdint.h`, `stdbool.h`,
`vis.h`, `fnmatch.h`, `paths.h`, `sys/queue.h`.

NetBSD 1.0 libc pieces that look missing on a filename diff are often
man pages (`err.3`) or the same function living in DiscoBSD `gen/`
instead of `stdlib/` (`malloc.c`, `qsort.c`, `abort.c`). Filename diffs
are not symbol diffs.

### Kingsley malloc: tempting, wrong

NetBSD 1.0 `lib/libc/stdlib/malloc.c` is Chris Kingsley's 1982 allocator:
power-of-two buckets, "designed for use in a virtual memory environment"
(file comment). Internal fragmentation on a 144 KB process that mixes
32-byte and 1 KB allocations is the failure mode. DiscoBSD's first-fit
circular arena returns exact word multiples and was built for machines
that cannot hide waste behind a pager. Do not swap it.

`radixsort.c` (319 lines) and `merge.c` (348, mergesort) are the same
class: they buy time with extra pointers. `heapsort.c` already landed
because it is O(1) extra memory. Radixsort's stable path wants N extra
pointers; on a directory of a few hundred names that is fine, on a box
that also holds libc and the tool it is not the first lever.

## libc and headers worth taking, rewritten as C17

Ranked by (benefit / footprint). Sizes are NetBSD 1.0 source lines, not
Thumb-1 text; a cross-gcc `-Os` object is the number that decides a land.

### 1. `strmode(3)` -- 148 lines, already copied into `usr.bin/ar`

NetBSD 1.0 `lib/libc/string/strmode.c` fills an 11-byte `drwxr-xr-x`
buffer from a `mode_t`. DiscoBSD `usr.bin/ar/strmode.c` is the same
function, local to `ar`. `bin/ls/ls.c` still hand-rolls a type letter
and leaves the rwx string to a different path. One libc member, C17
prototype `void strmode(mode_t, char *);`, lets `ar` drop its copy and
gives `ls`/`find` a single encoder.

Falsifier: a libc `strmode` already in `lib/libc/string/`. It is not.
Validation: `bmake MACHINE=rp2040` of `ar` and `ls` after the move;
`arm-none-eabi-size` on `strmode.o`. Expected text of a few hundred
bytes, linked only when referenced.

C17 rewrite: drop `__P` and K&R, keep the `S_IF*` switch, no locale.

### 2. `bitstring.h` -- 114 lines, header only

NetBSD 1.0 `include/bitstring.h` (Paul Vixie / UCB) is macros over
`unsigned char`: `bit_alloc`, `bit_set`, `bit_clear`, `bit_test`,
`bit_nset`, `bit_ffs`. Zero object bytes. Useful for a compact flash
bad-block map, a USB endpoint bitmap, or a small capability mask
without inventing another ad-hoc bit op. `COMPACT_SWAPMAP` already
narrowed the swap map to 16-bit entries; this header does not replace
that, it stops the next bitmap from being a `u_long` array.

C17 rewrite: drop `register`, use `uint8_t` from `stdint.h`, wrap
multi-statement macros in `do { ... } while (0)`, `_Static_assert`
that `CHAR_BIT == 8`. Falsifier: the header already exists. It does not
(`include/` set-diff).

### 3. `utime(3)` -- 57 lines; `times(3)` -- 68 lines

POSIX wrappers. DiscoBSD has `utimes` at least in `usr.sbin/cron`; no
`include/utime.h` and no `times.c`. Both are thin syscalls or `gettimeofday`
shims. `times` needs `struct tms` and clock-tick conversion; the kernel
already counts `ru_utime` in ticks (`kern_resource.c`).

C17: `int utime(const char *, const struct utimbuf *);` and
`clock_t times(struct tms *);` with `stdint.h`/`time.h` types. Do not
pull NetBSD's `machine/ansi.h` size_t dance.

### 4. `pwcache(3)` -- 113 lines, with a smaller cache

`user_from_uid` / `group_from_gid` with `NCACHE 64`. `ls -l` and `find`
pay `getpwuid` per inode today. 64 slots is large for this image; cut
`NCACHE` to 8 or 16 at port time (`_Static_assert((NCACHE & (NCACHE-1)) == 0)`
because the code uses a mask). Linked only into tools that call it.

Falsifier: `ls` already caches names. `bin/ls/ls.c` stores `fuid`/`fgid`
and does not show a pwcache include.

### 5. `sysconf(3)` / `confstr(3)` -- 188 + 86 lines, then maybe `getconf(1)`

NetBSD 1.0 `sysconf.c` is a switch from `_SC_*` to `sysctl` MIBs plus
`getrlimit`. DiscoBSD has `sysctl` in kernel and libc and no `_SC_*` in
`include/unistd.h`. A table of the constants this kernel actually has
(`ARG_MAX` = `NCARGS` 5120, `CHILD_MAX` related to `NPROC` 25,
`OPEN_MAX` = `NOFILE` 30, `CLK_TCK` = `HZ` 1000, `PAGESIZE` = the
process window is not a page) is a C17 `switch` with designated
initializer records:

```c
static const struct { int name; int mib0; int mib1; long value; } sc[] = {
    { .name = _SC_ARG_MAX, .value = NCARGS },
    { .name = _SC_CLK_TCK, .value = HZ },
    ...
};
```

`bsd44-backport.md` called `getconf` low value on a single-configuration
image. That still holds for shipping `getconf(1)` (234 lines) on the
root. The libc table is cheap, documents the real limits, and is what a
later POSIX test would call. Do not copy NetBSD's `KERN_ARGMAX` path
until the sysctl MIB exists; hard-code the constants this `param.h`
already publishes.

### 6. `rewinddir(3)` -- missing beside `seekdir`/`telldir`

DiscoBSD has `seekdir.c` / `telldir.c` / `opendir.c`. `rewinddir` is
`seekdir(dirp, 0)` plus a possible `dd_loc` clear. NetBSD 1.0's file is
small. Add it rather than copying a larger directory package.

### 7. `search.h` linear search only -- not the tree

NetBSD 1.0 `include/search.h` declares `lfind`/`lsearch`/`insque`/`tsearch`
family. DiscoBSD already has `insque.c` (25 lines) and `remque.c` (23)
and `bsearch.c`. `hsearch` in NetBSD 1.0 lives under `lib/libc/db/hash/`
and pulls Berkeley DB. `tsearch` is a libc binary tree with malloc per
node.

Take `lsearch`/`lfind` as two short C17 functions (walk an array, optional
append). Leave `tsearch`/`hsearch` until a caller exists. Falsifier: a
caller in this tree. `rg` for `lsearch`/`tsearch`/`hsearch` in libc and
include is empty.

### 8. `cdefs.h` as a C17 attribute home -- not a `__P` museum

DiscoBSD `sys/sys/cdefs.h` already exists to put `__unused` in one place
(`uart.c`, `usb.c`, `flash.c` use it). Extend it with what GNU C17 and
C17 actually give:

```c
#define __dead          _Noreturn
#if defined(__GNUC__) || defined(__clang__)
#define __printflike(f,n) __attribute__((__format__(__printf__, (f), (n))))
#else
#define __printflike(f,n)
#endif
#define __restrict      restrict
```

Do not add `__P((...))`, `__const`, `__signed`, or `__BEGIN_DECLS`. Those
existed to paper over K&R and C++. This tree compiles as C17 and does not
ship C++. `__packed` stays out, for the reason the current file already
states (conflict with vendored STM32/ARM headers).

`_Static_assert` belongs at the data, not in cdefs: inode compact fields,
`USIZE`, USB DPSRAM alignment, `MSG_BSIZE`, `NPROC`. That is the C17
replacement for "we hope the 16-bit truncation is honest."

### Held: `fts(3)` 972 lines, `glob(3)` 847 lines, `getcap(3)` 1049 lines

`find` walks with its own `anode` graph (`usr.bin/find/find.c`). `bin/sh`
and `csh` glob themselves. termcap is `lib/libtermlib`. Each of these
three is a real library, not a routine. `glob` would be the first of
them worth a size measurement if a userland program other than the shell
needs POSIX glob; until a caller exists it is speculative, the same
rule `bsd44-backport.md` used for `fnmatch` before a caller appeared.
`fnmatch` then landed at 384 bytes of text. `glob` will not be 384 bytes.

## Userland: set-diff, then the filter

NetBSD 1.0 `usr.bin` names DiscoBSD lacks: apropos, asa, at, biff,
cap_mkdb, checknr, colcrt, colrm, crontab (DiscoBSD has `usr.sbin/cron`),
ctags, error, finger, fpr, from, fsplit, ftp, gencat, getconf, gprof,
hexdump, indent, ipcrm, ipcs, kdump, ktrace, lastcomm, leave, locate,
lock, logger, logname, machine, mkstr, modstat, msgs, netstat, newsyslog,
nfsstat, patch, quota, rdist, rlogin, rpcgen, rpcinfo, rsh, rup, ruptime,
rusers, rwall, rwho, script, shar, showmount, skey, soelim, talk, tcopy,
telnet, tftp, tn3270, tput, tset, ul, unifdef, units, unvis, vacation,
vgrind, vi, vis, what, whatis, which, whois, window, xstr, ypcat, ...

Almost all networking, RPC, YP, quota, ktrace, and locale tools die at
filter (2). `logname` is already an `id` hard link in `utilbox`. `vi` is
`stevie`. `which` in NetBSD 1.0 is `which.csh`, a csh script. `vis`/`unvis`
programs wrap libc that already landed.

DiscoBSD `usr.bin` already has, contrary to the older `posix-utilities.md`
gap list: `cut`, `paste`, `nl`, `cksum`, `expand`, `unexpand`, `mkfifo`,
`column`, `fmt`, `fold`, `uuencode`, `uudecode`. Those names live in
`textbox` / `utilbox` / standalone. Treat `posix-utilities.md` as stale
on presence.

### Worth a C17 port (userland)

| Program | NB 1.0 size | Why | Catch |
| --- | --- | --- | --- |
| `colrm` | 127 lines | column-cut companion to `cut`/`paste` already in textbox | trivial; measure a.out before a manifest line |
| `asa` | 117 lines | FORTRAN carriage-control to newlines; only if a workflow prints it | otherwise skip |
| `unifdef` | 639 lines | on-device C: strip `#ifdef` without a full preprocessor. Smaller C has no pp, so this is the board-side substitute for a class of edits | keep buffers bounded; no `mmap` |
| `dmesg` | 157 lines | kernel log | NetBSD's uses `kvm_nlist` on `/dev/kmem`. Rewrite against 2.11BSD `struct msgbuf` (`sys/sys/msgbuf.h`, `MSG_BSIZE` 2048) and only after PICO Config attaches `log` / `subr_log.c`. A `sysctl kern.msgbuf` would be smaller than kvm |
| `logger` | 186 lines | write to syslog | libc `syslog.c` exists; no `syslogd`. Without a daemon or `/dev/klog` it is a no-op. Sequence: enable log device, then logger, not the reverse |
| `hexdump` | 1545 lines across 6 `.c` | NetBSD 1.0 `hexdump` *is* `od` | DiscoBSD already has `usr.bin/od/od.c` (909 lines). Do not ship both. Replacing od with hexdump is a taste change, not a gap |

### Measure, do not assume: `indent`

3794 lines. A C pretty-printer next to on-device `cc`/`smlrc` is the
right kind of tool for this image *if* the a.out fits the window after
`-Os` and packed exec. NetBSD 1.0 indent is a known large small-tool.
Build it off-manifest first; if text+data+bss exceeds a comfortable
fraction of 144 KB, stop. Do not put it in a box with other applets.

### Do not port

`patch` (4254 lines) -- `bsd44-backport.md` already rejected it; the
engine is the same 4.4BSD text. `pax` (15149) -- `tar` and `cpio` ship.
`libedit` (14006) -- `bin/sh/edit.c` is 760 lines, static 128-byte buffer
plus a 32 x 96 history ring, no malloc beyond `opendir`. That is the
tinyspace answer to libedit. `script` needs pty; PICO Config has no pty.
`window` (thousands) needs pty multiplexing. `units` is cute and not
load-bearing. `tput`/`tset`/`ul`/`colcrt`/`soelim` are nroff pipeline
pieces; the image's man reader does not run that pipeline on the board.

## NetBSD 1.0 unique versus 4.4BSD-Lite2

`find` on the Lite2 checkout shows `fts.c`, `glob.c`, `strmode.c`,
`radixsort.c`, `hexdump`, `dmesg`, `unifdef`, `indent`, `logger`,
`script`, `libedit`. Those are 4.4BSD files that NetBSD 1.0 also carries.
The Lite2 survey remains the authority for them.

What NetBSD 1.0 has that Lite2 does not, of any size:

- **crunchgen / crunchide** (`distrib/crunch/`, University of Maryland,
  1994, ISC-like permission notice). DiscoBSD already builds seven
  multicall boxes with `ld -r`, renamed `*_main`, localized globals, and
  BSS overlay (`sys/arch/rp2040/doc/MULTICALL-BSS-OVERLAY.md`,
  `STORAGE.md`). crunchide hides a.out globals after `-dc` so common BSS
  becomes real bss. This port's boxes already force that shape in the
  generator. Borrowing crunchgen would replace a working, measured path
  with a host tool that emits Makefiles. The leftover idea is
  `--gc-sections` / function sections, already written up in
  `storage-techniques.md`, not a crunchgen import.
- **LKM** -- reject, above.
- **config.new / files.newconf** -- a second config language. This port
  already runs `tools/bin/config` on `PICO/Config`.
- **Twelve-arch MI/MD layout** -- already mirrored in `sys/arch/rp2040`.

So the "NetBSD 1.0 unique" set does not contain a kernel or box
mechanism this port still lacks. The remaining value is the small POSIX
libc and text tools listed above, which happen to live in a 1.0 tree
because they lived in 4.4BSD.

## C17 rewrite recipe for a 1994 file

A file copied from this NetBSD 1.0 checkout is not dropped in. The
mechanical conversion:

1. Keep the copyright block. UCB 4-clause: the advertising clause was
   retracted by the Regents in 1999; follow whatever `NOTICE` already
   does for other UCB files in this tree. Maryland crunchgen and Winning
   Strategies `search.h` keep their own terms. Do not invent a copyright
   line.
2. Delete `LIBC_SCCS` rcsid / sccsid objects. They cost bytes in `.text`
   or `.data` for no function.
3. Replace `__P((...))` with real prototypes. Delete K&R definition
   headers (`foo(a) int a; {`). GNU C17 still *accepts* old-style
   definitions; this port's reason for `-std=gnu17` is that C23 makes
   them errors (`sys.mk` comment). New files do not reintroduce them.
4. Delete `register`. The compiler's allocator is the authority.
5. Do not include `sys/cdefs.h` for `__BEGIN_DECLS` / `machine/ansi.h`
   `_BSD_SIZE_T_`. Include `stddef.h` / `stdint.h` / `sys/types.h` as
   this tree already does.
6. Prefer `uint8_t`/`uint32_t` in new code; keep `u_int`/`mode_t`/`ino_t`
   at the 2.11BSD ABI so on-disk and syscall layouts do not change.
7. Add `_Static_assert` where the 1994 code comments a size
   ("buffer must be 11 bytes", "NCACHE is a power of two").
8. Compile with the arm `CC` in `sys.mk` (`-std=gnu17 -fno-common
   -Werror` as `WARNERR` already requires). A new warning is a defect.
9. Smaller C does not see these files unless a native program copies
   declarations. Do not use designated initializers, `_Static_assert`,
   or `restrict` in a header a native program is expected to paste from
   unless smlrc is measured to accept them. Kernel and cross-built libc
   may use the full GNU C17 set.

Worked example, `strmode`:

```c
void
strmode(mode_t mode, char *p)
{
    /* type letter, three rwx triples, extra bit, NUL -- 11 bytes */
    ...
}
```

No `__P`, no `register char *p`, no rcsid.

Worked example, sysctl-style table in `sysconf`:

```c
static const struct sc_ent {
    int  name;
    long value;
} sc[] = {
    { .name = _SC_ARG_MAX,  .value = NCARGS },
    { .name = _SC_OPEN_MAX, .value = NOFILE },
    { .name = _SC_CLK_TCK,  .value = HZ },
};
```

NetBSD 1.0 used a `switch` and sysctl. The table is the C17 form and
avoids inventing MIBs this kernel does not have.

## C17 techniques on code this tree already has

Importing NetBSD 1.0 is the smaller half. Reading it next to this kernel
makes a short list of conversions that do not copy a file:

- **Designated initializers in `cdevsw[]`**
  (`sys/arch/rp2040/rp2040/conf.c`). Entries are positional:
  `cnopen, cnclose, cnread, ...` with a comment `{ /* 0 - console */ }`.
  A new field in `struct cdevsw` silently mis-binds every driver. C17
  `.d_open = cnopen, .d_read = cnread, ...` makes a missing member a
  zero and a renamed member a compile error. Same for `bdevsw[]` and
  `sysent[]`.
- **`_Static_assert` on compact layouts.**
  `COMPACT_INODE_FIELDS` truncates `i_flag`/`i_count`/`i_id`/`i_flags`
  to `u_short` (`inode.h`). Assert `sizeof(u_short) == 2` and that
  `INODE_PERSISTENT_FLAGS_SUPPORTED` matches the width. Assert
  `USER_DATA_SIZE == 144 * 1024` against `RP2040.ld`. Assert
  `MSG_BSIZE` is a power of two if the log wrap code treats it as a
  ring. Assert USB DPSRAM buffer alignment next to the datasheet 4.1.2
  comment in `dev/usb.c`.
- **`_Noreturn` on `panic`, `err`, `exit`.** The compiler then deletes
  dead edges after `err(1, ...)` and does not warn on missing returns.
- **`restrict` on copy paths** (`ufs_bio`, flash read/write, `memcpy`
  wrappers) where the contract is already non-overlap. Do not sprinkle
  it on namei.
- **Flexible array member** for `msgbuf.msg_bufc` if the ring is ever
  sized by a config option; today it is a fixed `[MSG_BSIZE]` and can
  stay that way.
- **Do not take C11 threads, atomics-as-concurrency, `aligned_alloc`,
  or `uchar.h`.** The kernel is uniprocessor with interrupt disable.
  Cortex-M0+ atomics are not a reason to change `NPROC` tables.

## Reject list (kernel and heavy userland), with the falsifier

| Claim | Falsifier that holds |
| --- | --- |
| Port vnode / FFS | `inode.h` and `ufs_*.c` are the FS; 99 KB of 128 KB kernel is already used |
| Port mbufs / INET | PICO Config has no `INET`; `init_sysent.c` stubs sockets |
| Port LKM | `kern_lkm.c` itself says unload is unsafe; no MMU |
| Port autoconf | devices are fixed in `RP2040.ld` and `conf.c` |
| Port Kingsley malloc | comment in NB `malloc.c`: designed for VM; this process has no pager |
| Port POSIX regex | `lib/libc/gen/regex.c` (400 lines) already feeds grep/sed/ed; POSIX engine is ~9x source (`bsd44-backport.md`) |
| Port Berkeley DB | needs mmap; `ndbm.c` covers the one consumer |
| Port locale / runes | C locale only; `strcoll` == `strcmp` here |
| Port libedit | `bin/sh/edit.c` is the sized editor |
| Port crunchgen | boxes already exist; ICF/gc-sections is the leftover, elsewhere |
| Port termios into the kernel | board shell depends on sgtty CBREAK vs RAW for SIGINT |
| Port `fgetln` / `funopen` | 4.4BSD stdio internals (`_r`/`_p`); this `FILE` is `_cnt`/`_ptr`/`_iob` |
| Ship `patch`, `pax`, `window`, networking usr.bin | size, or no stack, or both |

## Evidence

| Subclaim | Authority | How found | Falsifier | Validation |
| --- | --- | --- | --- | --- |
| Checkout is NetBSD 1.0 patch 6 | `git log -1` in the checkout; `sys/sys/param.h` `NetBSD1_0` | clone `--branch netbsd-1-0` | a different SHA or `NetBSD1_1` | `git rev-parse HEAD` |
| DiscoBSD is 2.11BSD, not NetBSD | `sys/sys/param.h` `BSD 211` | read | `BSD4_4` defined | `rg '^#define BSD'` |
| No vnode / no INET on PICO | `inode.h`; PICO Config; `init_sysent.c` | read + `rg INET` | `options INET` or `sys/sys/vnode.h` with real vnodes | `rg vnode.h sys/sys` |
| 144 KB window, ~99 KB kernel | `machparam.h`, `RP2040.ld`, `unix.map` `.text` `0x18a05` | read + map | 96 KB or 90 KB as live figures | `rg USER_DATA_SIZE`; `arm-none-eabi-size unix` |
| Cross dialect is GNU C17 | `share/mk/sys.mk`, `Makefile.rp2040` `CSTDFLAGS=-std=gnu17` | read | `-std=c23` or no `-std` | `rg std=gnu17 share/mk sys/arch/rp2040` |
| crunchgen is in NB 1.0, not Lite2 | `distrib/crunch` vs `find` on Lite2 | `find -iname '*crunch*'` | crunchgen in Lite2 | rerun the find |
| `strmode` local to `ar` only | `usr.bin/ar/strmode.c`; no `lib/libc/string/strmode.c` | `rg strmode` | a libc member | `ls lib/libc/string` |
| `bitstring.h` absent | include set-diff | `comm -23` | file appears | `ls include/bitstring.h` |
| termios kernel path not live | `tcattr.c` `#if 0`; `edit.c` sgtty on `!HOSTBUILD` | read | `TIOCGETA` in `sys/kern/tty.c` | `rg TIOCGETA sys/kern` |
| Many POSIX tools already boxed | `distrib/rp2040/mi.rp2040`, `sbin/*/Makefile` | inventory | `cut` missing from textbox | `rg 'cut' distrib/rp2040/mi.rp2040` |

## Residual

Object sizes for `strmode`, `pwcache` (with NCACHE=8), `sysconf`,
`unifdef`, and `indent` are not measured in this note. They are
hypotheses until `arm-none-eabi-size` on a PICO build. Kernel slack
(~29 KB in the 128 KB region) is from one linked `unix.map`; a Config
change moves it. Smaller C's acceptance of `_Static_assert` and
designated initializers is unmeasured; those stay in cross-built files
until a smlrc compile proves otherwise.

A land order that stays inside one PR each: `cdefs.h` attributes,
`bitstring.h`, `strmode` into libc and `ar` switched over, `utime`/
`times`/`rewinddir`, `pwcache` with a small NCACHE, `sysconf` table
without `getconf(1)`, then `unifdef` off-manifest for a size number.
Kernel termios, dmesg, and logger wait on an explicit `log` device
decision, which is a Config change, not a libc copy.
