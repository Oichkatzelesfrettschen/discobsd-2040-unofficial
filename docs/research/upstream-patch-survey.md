# Upstream DiscoBSD and 2.11BSD patches 432 to 500: survey for import

## Findings

2.11BSD patch 500, "35th Anniversary Edition", is a released code patch rather
than an announcement. The text at `http://www.2bsd.com/2.11BSD/500` is a
3,998,518-byte shell archive dated 2026-11-11 that patches 1795 files, removes
289 and adds 56. The donor ledger `PATCHES.md` in the BSD_Workspace 2.11BSD
checkout still describes 500 as an announcement (its 2026-09-15 text), and that
checkout ends at patch 499, so `tools/analysis/bsd211_patch_scope.py` cannot see
patch 500. Every patch-500 disposition below rests on the patch text and on
reading this tree.

Upstream DiscoBSD has two commits outside `origin/main`. Neither applies: one
moves the version string to `2.7-current`, a choice that belongs to the GizmoBSD
rename, and the other copies the PIC32 USB serial driver, which the fork already
carries byte-identically under `legacy/non-arm/mips-pic32`, into the STM32 port.

Across 172 rows (2 upstream commits, 68 numbered patches 432 to 499, and the
102 numbered items of patch 500), the classes are:

| Source | Present | Superseded | Applicable | Not applicable |
| --- | ---: | ---: | ---: | ---: |
| upstream DiscoBSD | 0 | 0 | 0 | 2 |
| 2.11BSD 432 to 499 | 12 | 9 | 8 | 39 |
| 2.11BSD 500 items | 17 | 3 | 15 | 67 |

The applicable set is small and cheap. Four kernel items (439, 451, 456 and 458)
cost between +4 and +80 bytes of kernel text, with 439's stack half unmeasured,
and at most 4 bytes of kernel RAM; the shipped userland
items live in `/bin/box` or a packed program and cost between -20 and 1328 bytes
of program text (the lower bound is 500.83 `ls` without `cfree`, ~-20 B (I)), none of it kernel text or kernel RAM. The two highest-value
items are POSIX conformance fixes in shipped box applets: `chown owner:group`
and option parsing in `rm` and `mv`.

Two of patch 500's libc defects are absent here by measurement: `%.0f` of zero
prints `0`, and `snprintf` truncates and returns the full length. The printf
gates already hold truncation vectors (`tests/libc_contracts/printf_contract_test.c:122-125,151`);
the `%.0f` zero case has none.

## Sources and identity

| Surface | Value |
| --- | --- |
| Fork | `origin/main` at `32927ed04857da4a703a2845d4e643801de711e8` |
| Upstream | `upstream/master` at `2c841439f950d06f17a032046417b3d0c0de9169`, the only head `git ls-remote --heads upstream` lists; merge base `f7635b65` ("DiscoBSD: Time for DiscoBSD 2.7 release.") |
| 2.11BSD donor | BSD_Workspace `211bsd` checkout at `0a38e59d742ff4371b78ea65c5bcc50477b31708`; tape-431 root `5a38336`; one commit per patch 432 to 499 as `PATCHES.md` lists them |
| Patch 500 | `http://www.2bsd.com/2.11BSD/500`, fetched over plain HTTP 2026-11-16 07:56 UTC, SHA-256 `d52e80258543dfc08298ca3d60212b0d4bb7b6b4d95b579eedd4c42a3d7f6b16`; the directory index also lists `PL500.tap.bz2`, `PL500readme.txt` and `PL500setup.pdf` |
| Patch 500 parts | the three `sed 's/^X//'` sections of the archive were extracted by a parser, never executed: `500.new` (659,229 characters), `500.rm` (10,208), `500.patch` (3,092,844, 1795 context-diff file headers) |
| Textual mapper | `BSD211=<checkout> ${PYTHON} tools/analysis/bsd211_patch_scope.py --base 5a38336 --donor-ref HEAD --recipient-ref origin/main --format json`, exit 0 |
| Cross compiler | `arm-none-eabi-gcc` 16.2.0 (local); no CI compiler figure is quoted or subtracted |
| Host compiler | `cc` (GCC) 16.2.1 |

The 432-to-499 classification joins three inputs: the mapper's exact-path
intersection, a basename join of every donor path against the fork tree (which
finds relocations such as `bin/dd.c` to `bin/dd/dd.c` and `sys/sys/kern_mman.c`
to `sys/kern/kern_mman.c`), and a reading of the donor hunk for each hit. For
patch 500, 594 of the 1795 patched paths map to fork paths by prefix
(`usr/src/`, `usr/src/sys/sys/` to `sys/kern/`, `usr/src/sys/h/` to `sys/sys/`,
`usr/include/` to `include/`) and 248 more by basename. Patch 500's own
numbered items (Description 1 to 102, Fix 1 to 102) organize its rows.

## Class rule

A row takes the most actionable class among its hunks, in the order applicable,
superseded, present, not applicable, so one live hunk makes a multi-topic patch
applicable.

- **present**: the fork carries the invariant; the row cites the fork line and
  the commit `git blame` attributes it to, or the `211bsd-semantic-ledger.json`
  fix identifier.
- **superseded**: the fork reaches the same end through a different mechanism or
  a rewrite of the program.
- **applicable**: a candidate found by reading the fork line the hunk targets,
  marked (I) where the defect was not executed. Text distance alone never sets
  this class (`211bsd-patch-scope.md`).
- **not applicable**: PDP-11 hardware, a program or subsystem absent from this
  tree, the network stack, or documentation for those.

The appendix `upstream-patch-survey.tsv` carries one row per upstream commit,
per numbered patch 432 to 499 and per patch-500 item, with the shipped route,
kernel text and RAM, userland cost, gate and evidence.

## Upstream DiscoBSD

| Commit | Class | Mechanism |
| --- | --- | --- |
| `d143605c` DiscoBSD 2.7-current | not applicable | sets `S="-current"` in `sys/conf/newvers.sh`; the fork keeps the release form at `sys/conf/newvers.sh:23-27` (`b71a32ae`), and its version string is part of the GizmoBSD rename decision rather than an upstream import |
| `2c841439` stm32 uartusb import | not applicable | `sys/arch/stm32/dev/usb_uart.c` is byte-identical to upstream's `sys/arch/pic32/dev/usb_uart.c` (SHA-256 prefix `fa60f08deb0bb966` for both), and the commit states it is not in the build; the fork carries the same PIC32 file at `legacy/non-arm/mips-pic32/sys/arch/pic32/dev/usb_uart.c` with the same hash, and its RP2040 CDC-ACM driver is its own |

## 2.11BSD patches 432 to 499

Most of this range was already worked: `211bsd-patch-scope.md` and
`211bsd-semantic-ledger.md` record getty, `_doscan`, `random`, pushback, update
mode, sysctl, tar, umount and inode-rescan work. The survey adds these:

- **Patch 439** carries a kernel hunk the earlier path mapping missed: `brk` and
  stack growth honor `RLIMIT_DATA` and `RLIMIT_STACK`. The fork's
  `sys/kern/kern_mman.c:24-37` checks only `MAXMEM` and the stack bottom, although
  `u_rlimit` exists (`sys/sys/user.h:94`). The stack half does not land in
  `kern_mman.c`: the donor patches `grow()`, which this fork lacks, and RP2040 stack
  growth runs in `sys/arch/rp2040/rp2040/syscall.c:196-200` and `sig_machdep.c:87-90`,
  where `p_ssize` rises with no limit check, so an import covers three sites.
- **Patch 441** bounds the libvmf locked-segment walk at the list head and rejects
  a negative segment number; `lib/libvmf/vmf.c:101,118` carry the pre-patch loop.
  Only `usr.bin/med` links libvmf, and med is unshipped.
- **Patch 447** fixes `apropos` allocating `argc` bytes for an `int` array; the
  same line is at `usr.bin/man/apropos.c:71`. apropos is unshipped.
- **Patch 451** lets any root process lower `kern.securelevel` while it is at or
  below 0; `sys/kern/kern_sysctl.c:262` refuses unless the caller is init. This is
  a policy change and an owner decision.
- **Patch 454** removes the kernel timezone from `date` and libc. The fork still
  builds the old mechanism: `bin/date2/date.c:304-380` (shipped as `/bin/date`)
  declares `struct timezone`, parses `-d` and `-t` into `tz_dsttime` and
  `tz_minuteswest` and adds `tz_minuteswest` to the typed time;
  `lib/libc/gen/tzset.c:195,247` keeps `tzsetkernel`. The patch moves `date` to
  `localtime` and `tm_gmtoff` and drops both options, so it is a policy
  decision, not a bug fix; an earlier draft called it superseded.
- **Patch 466** makes `size` report `strerror`; `usr.bin/size/size.c:38` still
  prints "not found".
- **Patches 456 and 458** remain open in the ledger
  (`ufs-clean-state-transition`, `ufs-superblock-clock-order`). Patch 458 sets
  `fs_time` at mount and update; the fork seeds the boot clock from `fs_time`
  (`sys/kern/init_main.c:133`) and updates it at `sys/kern/ufs_syscalls2.c:218`
  and `sys/kern/ufs_alloc.c:100`.
- **Patch 461** is present. `sys/arch/rp2040/rp2040/shutdown_sync.c` counts busy
  buffers, prints "timeout: %d buffers busy" and forces the root checkpoint
  (`79f465d9`, `cdfe77be`). That implements the drain report the ledger row
  `shutdown-busy-drain-report` names missing at its recipient snapshot `cc67531a`
  (`git merge-base --is-ancestor 79f465d9 cc67531a` exits 1). The row
  `shutdown-pre-reset-flush` asks for physical persistence evidence, which
  `check-rp2040-shutdown-cross` states it does not establish, so that row stays open.

## Patch 500

Of the 102 items, 67 concern PDP-11 hardware and tapes, f77, efl, ingres,
sendmail, uucp, networking, programs absent here, or manuals. The rest:

**Present (17).** `touch -r/-t` (`usr.bin/touch/touch.c:268`, `4328e80e`),
`vm.nswap` (ledger), `gets` removal (`a8d2e6da`), `/root` as root's home
(`etc/passwd:1`, `distrib/rp2040/mi.rp2040:51`), `fcntl.h` prototypes, `err.h`,
`stdlib.h` prototypes, `strftime %b`, the `usleep(long)` prototype, the
`void (*)(int)` `sig_t`, one `signal.h` (`include/signal.h` links to
`sys/signal.h`), battlestar's stringizing `PATH(x)`, no `timevalfix` macro, no gcos code in make, and, by the probe below,
`%.0f` of zero and `snprintf` truncation, and the `hostid` crash (500.99):
`usr.bin/hostid/hostid.c:36` compiles the `gethostbyname` branch only under
`HAVE_NET`, which no Makefile or header defines, so the program parses its
operand numerically and the crash path is absent. `hostid` is in
`usr.bin/Makefile:33` and not in `mi.rp2040`.

**Superseded (3).** `xinstall` `__STDC__` blocks, `pstat` and `XPAGI`, and `xargs`
(the fork reports through `fatal()`, `usr.bin/xargs/xargs.c:52`). Part (c) of
500.57 (`ps` through `getopt`, `bin/ps/ps.c:294`) is present, but part (a) is
live, so that item is applicable.

**Applicable (15).** `ls -T` (500.1), `mkdir -m` (500.9), the `find`
descend repair (500.11), `iso646.h` and `locale.h` (500.13), `chown
owner:group` (500.17), the find-database last entry (500.30), `w` with no
utmp (500.51), `rm` and `mv` through `getopt`
(500.80), `cfree` removal (500.83), `man` section names (500.86), the
`sleep(3)` range (500.92, below), the `ps` static tty table (500.57, ranked 12), the `mkstr` `\a` and `\v`
escapes (500.68: `games/battlestar/mkstr.c:142-166` has no such cases, and
battlestar is built through it), the unused phantasia test files (500.74: `phant_run.c`
and `test.c` remain, cosmetic, on a game outside `games/Makefile`'s `SUBDIR`; its
setuid-root install and three-argument `signal` calls are absent) and the
`who am i` separator (500.97).

**Live defect, 500.92 `sleep(3)`.** Part (b) of the item, an unsigned parameter
used as signed, is absent: `seconds` is a `u_int` with no signed comparison.
Part (a) is live: `lib/libc/gen/sleep.c:35-37` passes the
whole `u_int` to one `select(2)`. The kernel refuses a timeout above
100,000,000 seconds: `select1()` calls `itimerfix()` and returns `EINVAL`
(`sys/kern/sys_generic.c:454`, `sys/kern/kern_time.c:348-350`). `sleep` ignores
the status, reads a clock that has not moved and returns the full request
unslept, at once, for every request from 100,000,001 through `UINT_MAX`. The
host probe below executes that range against `sleep.c` (M, with a stub that
applies the kernel bound). A second ceiling is read from source and not run:
`hzto()` clamps a wait to `0x7fffffff` ticks, 2,147,483 seconds at `HZ` 1000
(`sys/kern/kern_clock.c:447-453`, `sys/arch/rp2040/include/machparam.h:44`),
and an expired `tsleep` ends `select1()` with no wall-clock recheck, so a
request between 2,147,484 and 100,000,000 seconds sleeps short as well (I).
Upstream's loop over a bounded chunk, or an explicit documented bound, closes
both. No shipped program passes such a value, so the reach is the libc contract.

Item 32 adds `DOSCCS` guards around `sccsid` strings to recover PDP-11 data
space. Under GCC an unreferenced `static` array is discarded:
`arm-none-eabi-strings -a` finds no `@(#)` string in `bin/ls/ls`, `bin/cat/cat`,
`usr.bin/find/find`, `usr.bin/w/w` or `sbin/adminbox/adminbox` as built in the
primary checkout.

## Probes

**Formatter.** The tree's `snprintf.c`, `vsnprintf.c`, `flsbuf.c`, `doprnt.c`,
`doprnt_float.c` and `doprnt_llong.c` at `32927ed0` were compiled for x86-64 with
the `check-libc-printf-float` flags (`-std=c17 -Wall -Wextra -Werror -nostdinc
-isystem include -Derrno=printf_test_errno -DPRINTF_FLOAT_TEST`) and linked to a
driver. Host execution class:

| Call | Output | Return |
| --- | --- | ---: |
| `snprintf(b, 64, "%.0f", 0.0)` | `0` | 1 |
| `snprintf(b, 64, "%.0f", 0.4)` | `0` | 1 |
| `snprintf(b, 64, "%.0f", -0.4)` | `-0` | 2 |
| `snprintf(b, 64, "%5.0f", 0.0)` | four spaces, `0` | 5 |
| `snprintf(b, 64, "%.0e", 0.0)` | `0e+00` | 5 |
| `snprintf(b, 9, "%s%s", "/dev/", "swap")` | `/dev/swa` | 9 |
| `snprintf(b, 1, "%s", "abc")` | empty | 3 |

These are patch 500's items 22 and 54 executed against this tree; both behave
correctly. `printf_contract_test.c:122-125` and `:151` already check truncation;
no printf contract file holds a `%.0f` vector, so a zero-precision zero case is a
cheap addition to `check-libc-printf-float`.

**Lone dash under `getopt`.** A review of the `rm` and `mv` recommendation claimed that
`lib/libc/stdlib/getopt.c:63-88` scans an empty option for the argument `-` and
reads one byte past the option string. The claim does not reproduce. The file was
compiled for the host with AddressSanitizer and UBSan, with the option string and
every argument in exact-size heap blocks, and driven over six option strings
(`dfiPRrW`, `ab:`, `:a`, empty, `-`, `a:`) and every argument vector of length 0
to 3 drawn from `-`, `--`, `-a`, `-ab`, `x`, the empty string and `-:`: 2,193
option returns and no report. `-` with `dfiPRrW` returns `EOF` at `optind` 1, so
the dash stays an operand. The guard `place[1] &&` at `getopt.c:67` skips the
empty-option scan. A mutant without that guard reads one byte past the option
string and ASan reports a heap-buffer-overflow, so the harness detects the
failure the review described. The recommendation therefore needs no `getopt`
repair; it needs a lone-`-` case in the new gate. Host x86-64 run, not the target.

**Sleep range.** `tests/libc_contracts/sleep_contract_test.c`'s harness
(`-Dselect=test_select -Dgettimeofday=test_gettimeofday`) was rebuilt with a
`select` stub that returns -1 above 100,000,000 seconds, as `itimerfix()` does,
and otherwise advances the replayed clock by the timeout. It runs on the host
against the tree's `lib/libc/gen/sleep.c`. The contract test itself never
requests more than 10 seconds.

| Call | select calls | select timeout (s) | Unslept returned |
| --- | ---: | ---: | ---: |
| `sleep(100000000)` | 1 | 100,000,000 | 0 |
| `sleep(100000001)` | 1 | 100,000,001 | 100,000,001 |
| `sleep(4294967295)` | 1 | 4,294,967,295 | 4,294,967,295 |

**Object sizes.** Single objects compiled with `arm-none-eabi-gcc` 16.2.0. The
kernel objects use the flags `bmake -n` prints for `sys/arch/rp2040/compile/PICO`;
the userland objects use `-std=gnu17 -fno-common -ffunction-sections
-fno-delete-null-pointer-checks -mcpu=cortex-m0plus -mthumb -Os -nostdinc`. Object
deltas exclude link-time alignment and `--gc-sections` effects.

| Change | Object | Before text | After text | Delta |
| --- | --- | ---: | ---: | ---: |
| 451 securelevel at or below 0 | `kern_sysctl.o` | 3528 | 3532 | +4 |
| 439 `RLIMIT_DATA` in `brk` (the data half only) | `kern_mman.o` | 88 | 108 | +20 |
| 500.17 `owner:group` | `chown.o` | 969 | 981 | +12 |
| 500.92 chunked `select` loop (sketch, not run) | `sleep.o` | 74 | 96 | +22 |
| 500.9 `mkdir -m` through `setmode` | `mkdir.o` | 328 | 428 | +100 |
| `setmode` member | `setmode.o` | 0 | 1228 | +1228 when first linked |

Data and BSS did not change in any of these objects. None of the 18
`sbin/box/*.tool.o` objects in the primary checkout has an undefined `setmode` or
`getmode` (`arm-none-eabi-nm`), so a symbolic `mkdir -m` adds the member to
`/bin/box`.

**Panic strings.** `grep -rhoE 'panic[[:space:]]*[(]+"[^"]*"'` over `sys/kern`,
`sys/arch/rp2040`, `sys/dev`, `sys/fs` and `sys/sys` finds 103 calls with 96
distinct single-line literals. 79 of them appear verbatim in
`arm-none-eabi-strings -n 2` output for a local `sys/arch/rp2040/compile/PICO/unix.elf`
(built 2026-11-13 with GCC 16.2.0 at an unrecorded commit, SHA-256
`db52e353...`), totaling 1603 bytes with terminators. This is an upper bound on
what shortening can recover, in flash only, and the artifact is not comparable to
the CI headroom figure.

## Ranked applicable items

Rank weighs reach on the shipped image, correctness or conformance gained, and
cost against the two tight budgets. Kernel figures are `.text` and kernel RAM;
userland figures are program text inside the 192 KB process window and, for box
applets, packed blocks in the root.

| Rank | Item | Shipped | Kernel text | Kernel RAM | Userland cost | Risk | Gate |
| ---: | --- | --- | ---: | ---: | --- | --- | --- |
| 1 | 500.17 `chown owner:group`, keeping `owner.group` (`sbin/chown/chown.c:164`) | `/bin/box` | 0 | 0 | +12 B measured | low; a user name containing `:` becomes unreachable, as in 4.4BSD | none; a host contract on the parser is the new gate |
| 2 | 500.80 `rm` and `mv` through `getopt`. Today a lone `-` ends options (`bin/rm/rm.c:41`, `bin/mv/mv.c:64`) and `--` reaches the `default` case, prints usage and exits 1 (`rm.c:59-61`, `mv.c:76`), so the POSIX `rm -- -x` fails. The tree's `getopt` keeps a lone `-` as an operand (probe below) | `/bin/box` | 0 | 0 | ~+40 B per applet, `getopt` already in box (I) | low | `check-rmdir-contracts` is the pattern; no rm or mv gate exists, and the new one needs a lone-`-` case so a future `getopt` change cannot regress it |
| 3 | 500.11 `find`: return 1 when `chdir` fails and restore `/` rather than an empty `Pathname` (`usr.bin/find/find.c:730,748`). Latent on the shipped image: the default directory mode is 0775 (`distrib/rp2040/mi.rp2040:21`), so it fires only on a directory a non-root user cannot search (I) | packed `/usr/bin/find` | 0 | 0 | ~+10 B (I) | low | `check-find-contracts`, with a new unreadable-directory case |
| 4 | 456 clean-state flag across mount, unmount and fsck (ledger open) | kernel, fsck | ~+80 (I: donor kernel hunks add 34 lines to `ufs_subr.c`, 14 to `ufs_mount.c`, 4 to `mount.h`) | 0 to +4 (I) | fsck ~+100 B (I: 35 lines in `fsck/main.c`) | medium; touches the on-disk superblock | `check-v7-format`, `check-fsck-inode-range` |
| 5 | 458 superblock time ordering at mount (ledger open) | kernel | ~+8 (I: the donor adds one `fs_time` assignment each to `ufs_mount.c` and `ufs_subr.c`) | 0 | 0 | low | none |
| 6 | 500.9 `mkdir -m` | `/bin/box` | 0 | 0 | +100 B, or +1328 B with symbolic modes | low | none |
| 7 | 500.13 `iso646.h`, `locale.h`, C-locale `setlocale` and `localeconv` | headers, libc | 0 | 0 | header-only for iso646; ~150 B per linking program (I) | low | `check-c17-headers` |
| 8 | 451 securelevel at or below 0 may be lowered | kernel | +4 measured | 0 | 0 | policy | none; `sysctl_test` (`check-kernel-ilp32`) links `kern_sysctl.c` and declares `securelevel` (`sysctl_test.c:118`) but holds no `KERN_SECURELVL` case, and `check-libc-sysctl` links only `uname.c`, `gethostname.c` and `knlist.c` against a userland shim, so the new gate is a `KERN_SECURELVL` case in `sysctl_test.c` |
| 9 | 439 `RLIMIT_DATA` in `brk` (`sys/kern/kern_mman.c:24`) and `RLIMIT_STACK` in the two RP2040 stack-growth paths, `sys/arch/rp2040/rp2040/syscall.c:196-200` and `sig_machdep.c:87-90`, which raise `p_ssize` with no limit check (the donor patches `grow()`, which this fork lacks). `setrlimit` is a system call (`sys/kern/init_sysent.c:466`), but the shipped callers set only `RLIMIT_CPU`, `RLIMIT_FSIZE` and `RLIMIT_CORE` (`usr.bin/passwd/passwd.c:90-91`, `usr.bin/timeout/timeout.c:448`); csh's `limit` builtin is unshipped, so nothing on the image sets a data limit today | kernel | +20 measured for the `brk` half; the two stack checks are unmeasured (I) | 0 | 0 | low; `RLIM_INFINITY` is the initial limit (`sys/kern/init_main.c:88`) | none; a limit case for each of `brk`, a system-call stack growth and a signal-frame stack growth is the new gate, and `check-kernel-headroom` covers size |
| 10 | 500.92 `sleep(3)` above 100,000,000 s returns the whole request unslept at once, and above 2,147,483 s sleeps short (I); a chunked `select` loop, or a documented bound | libc, every program linking `sleep.o` | 0 | 0 | +22 B in `sleep.o` for the unrun sketch, GCC 16.2.0 `-Os` | low; the loop must keep the interrupted-sleep remainder and the 2038 clock arithmetic of `sleep.c:44-51` | `check-sleep`, with requests above the bound (no such case today) |
| 11 | 454 `date` and libc without the kernel timezone (`bin/date2/date.c:304-380`, `lib/libc/gen/tzset.c:195`); drops `date -d` and `-t` | `/bin/date`, libc | 0 | 0 | shrinks `date` and `tzset.o` (I, unmeasured) | medium; two user options disappear and the fork stores the zone in the kernel today | none; a `date` contract on `-t` and `-d` is the new gate |
| 12 | 500.57 `ps` keeps a static 24-entry tty table (`bin/ps/ps.c:509-513`) where the patch looks names up with `devname(3)`; part (c), `getopt`, is present. Prerequisite: `lib/libc/gen/devname.c:65-67` returns `entry->d_name` after `closedir(dir)` has freed the `DIR` that holds the entry, so the pointer dangles; a stable-buffer `devname()` comes first | packed `/bin/ps` | 0 | 0 | removes the table from `ps` data (I, unmeasured; see the adaptation below); the `devname()` repair adds libc text, unmeasured | medium until `devname()` is repaired: a corrupted or crashing terminal name; low after | none; a `devname()` runtime case that reads the name after another allocation is the new gate |

The rest are unshipped or cosmetic: 500.51 `w` dereferences a failed `fopen` of
utmp (`usr.bin/w/w.c:129,164`); 447 `apropos` under-allocation; 441 libvmf walk;
500.1 `ls -T`; 500.68 `mkstr` `\a` and `\v` in the battlestar build tool; 500.83 `cfree` in `ls`; 500.86 `man` section names; 500.97 `who`
separator; 500.74 phantasia `phant_run.c` and `test.c`, which patch 500 deletes from an unbuilt game; 466 `size` message; 500.30 find-database last entry (I). The two
formatter vectors from the probe are gate work with no target cost.

## Mechanisms worth adapting

- **External message strings (500.85, `mkstr -e`, `-l`).** Program text loads
  into RAM inside the process window, so a message file read on demand moves
  string bytes from the window to the flash root. `games/battlestar/mkstr.c`
  already uses the old form. A program close to the window or packed-block budget
  can trade one read per diagnostic for its message bytes.
- **Short unique panic tags (500.76b).** Patch 500 shortens `uipc_usrreq.c`
  panics to unique numbered strings. The donor item is not applicable (the fork
  has no `uipc_*.c` or mbuf source under `sys/kern`; the item targets the network
  image), so this is an adaptation of the idea to RP2040 panic strings, with no
  rank: up to 1603 bytes of flash, measured as an upper bound below. Its other half, one shared `char mgetStr[]`
  per repeated literal, is moot under GCC, which merges identical literals.
- **Count every buffer the size checker must see (500.40).** `_sofub_size` becomes
  initialized data so `checksys` includes the bounce buffer. The fork already
  places its run-time pools in named sections (`.swapram_pool` 16384 B and
  `.scratch` 8192 B in `arm-none-eabi-size -A`), which the headroom gate reads.
- **Completion must wake a closer (500.84).** The xp(4) interrupt drains the
  controller queue without waking `umount`, which slept in `xpclose` before the
  last transfer completed. The pattern to audit here is any RP2040 close path
  that sleeps on a drain the interrupt handler finishes (I).
- **Device name through a lookup rather than a resident table (500.57).**
  `ps` replaces its static tty table with `devname(3)` and saves over 1 KB of
  PDP-11 data. The fork's `ps` keeps a 24-entry table (`bin/ps/ps.c:512`); a lazy
  `devname` call per row removes that table from `ps`'s data (I), but only after
  `devname()` is repaired: `lib/libc/gen/devname.c:65-67` calls `closedir(dir)` and
  then returns `entry->d_name`, a pointer into `dd_buf`, which `closedir()` frees
  with the `DIR` (`lib/libc/gen/closedir.c:16-25`, `include/dirent.h:64-67`). The
  one shipped caller, `sbin/sysctl/sysctl.c:471`, already reads freed storage.
  Source reading, not executed.
- **Kernel-derived device identity with a fallback (500.49).** `swapdevname(3)`
  asks `sysctl` first and falls back to `/dev/swap`. Swap here is a fixed flash
  region, so the pattern matters only for tools that still hard-code device paths.

## BSD_Workspace and DragonFly references

| Ranked item | Reference | Revision and SHA-256 |
| --- | --- | --- |
| 1, `owner:group` | `DragonFlyBSD/usr.sbin/chown/chown.c:135-140` tries `:` and then `.`, the order the measured variant uses | `94448f622a06`, `e983a8d87aef2da25bef0e0742040f367fd9ff6e1fea3e660e0b807305ce1cca` |
| 1 | `4.4BSD-Lite2/usr.sbin/chown/chown.c:140-145` tries `.` and then `:` | `b10da34408c8`, `989e4f6cf300d2a0eff4ababe3bb97cbf5b08e032aba9bc539ca3cbc9c41524d` |
| 2, `rm` options | `4.4BSD-Lite2/bin/rm/rm.c:80` parses with `getopt(argc, argv, "dfiPRrW")` | `b10da34408c8`, `7243d78ec0f64bcbcee7d2e486a0722df183a7077f9f9e6705d16807ea695043` |
| 6, `mkdir -m` | `DragonFlyBSD/bin/mkdir/mkdir.c:64,86-93` (`setmode`, `getmode`); `4.4BSD-Lite2/bin/mkdir/mkdir.c:73,95` | `954b9709db5f61ecb477a3a3b04ecf12c36ed505d883689432caac3f5e942b3b`; `43a9898c69b5532f8c384658d6b3370fa52d345d17bbf5a66537c3f8f8ffb0c8` |
| 7, C99 headers | `DragonFlyBSD/include/iso646.h`, `DragonFlyBSD/include/locale.h` | `18411814a9522581864b529134a450a178530af6f58615b503ba02badd277490`; `0608b8ba72c57cea2d1ee613bb207a1efdef16eae1b3f7521b458d860b596d46` |

## Defects in the surveying instruments

- `tools/analysis/bsd211_patch_scope.py:65-66`: `patch_number` searches three
  alternatives, and the anchored `^\s*(\d+)[.:) -]` matches the `2.` of
  "2.11BSD patch 480" at offset 0 before `patch\s+#?(\d+)` can match later. The
  JSON labels patch 460 and patches 480 to 499 as patch 2; "(Path 451)" and
  "#448" subjects get no number. Joining on the commit identifiers in `PATCHES.md`
  avoids it; the regex wants a commit of its own with a calibrated case.
- `211bsd-semantic-ledger.json`: `shutdown-busy-drain-report` reads `missing` at a
  recipient snapshot that predates `79f465d9`, which implements that report.
- Donor `PATCHES.md` describes patch 500 as an announcement.

## Owner decisions

1. Add patch 500 to the BSD_Workspace 2.11BSD donor as one commit with a
   `PATCHES.md` row and patch-text hash, so the mapper and the donor witness
   reach it.
2. Kernel `securelevel` policy: keep init-only lowering, or adopt patch 451.
   Kernel timezone: keep `date -d` and `-t` and `tzsetkernel`, or adopt patch 454.
3. Kernel panic text: readable strings, or short unique tags for up to 1.6 KB of
   flash.
4. `mkdir -m`: octal only (+~60 B, I), or symbolic through `setmode` (+1328 B in
   `/bin/box`).
5. Patch 456: whether the root superblock gains a clean-state field, which is a
   format change on the Dhara root.
6. Import order after the GizmoBSD rename; ranks 1, 2, 3 and 7 stand alone and
   need no format or policy choice.

## Not established

No item was imported, built into a kernel, run under Renode or run on a board.
Kernel sizes are single-object compilations; the `unix.elf` panic figure comes
from a local artifact of unrecorded revision. The `find`, `w`, `apropos` and
libvmf defects were found by reading and not executed, and so was the kernel
half of 500.92 (the `itimerfix()` refusal and the `hzto()` clamp): the host
probe stubs `select`, and the +22 B sketch of a chunked loop was compiled but
never run. The 1795-file patch-500
diff was classified at item level; individual hunks outside the 842 mapped paths
were not read. Patch 499's remaining hunks keep the text-distance disposition of
`211bsd-patch-scope.md`.
