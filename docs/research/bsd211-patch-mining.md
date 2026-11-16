# 2.11BSD patches 432 to 500: mechanisms, invariants and C17 integration plan

## Findings

The 170 units (68 numbered patches 432 to 499, 437 being a placeholder, and the
102 numbered items of patch 500) fall into 14 defect-and-change mechanisms.
Interface and prototype alignment (22 units), dead-code removal (21) and
error-path and lifecycle repairs (19) lead; time and date repairs number 9 and
bounds and input repairs 6. The mechanisms with a live
instance in this tree are error paths (unchecked allocation in three shipped
programs), time arithmetic (a leap-year rule that fails in 2100, a year printed
as "19" plus a three-digit offset) and signed or unsigned conversion at a kernel
bound (`alarm(3)` above 100,000,000 s arms nothing and returns 4,294,967,295; the
`sleep()` repair note on its own branch records the same defect and leaves it
open).
The invariant search of section 2 reports six new ledger candidates and one
cross-reference to a defect another note already records. It also reports six
negative results with their sampling conditions, so the absence of a hit is
recorded as evidence.

Three results change the import plan. First, patch 456's `MNT_CLEAN` value
`0x2000` is already this tree's `MNT_V7` (`sys/sys/mount.h:102`), so a verbatim
import gives one bit of the superblock word two meanings. `statfs(2)` copies
`m_flags & MNT_V7` (`ufs_syscalls2.c:47`), so it would report "v7" for a native
volume while the clean mark is set; by the hunks read, `MNT_CLEAN` sits in the
mounted word only around the unmount and reboot sync and `fsck` sets it on the
disk image, so the window is narrow (I; not traced). Second, three sysctl
identifiers that patch 500 allocates (`KERN_LINEHZ 25`, `HW_TRACEBUF 10`,
`VM_SWAPDEV 6`) collide with identifiers this tree already assigned;
`VM_CP_TIME 7` takes the next free number. Third, upstream converted almost nothing from K&R to
prototypes in the range: the donor has 17,193 K&R definitions at patch 499 and
16,090 at patch 500 (after the removals of `500.rm`), and only three fork definitions (one line each, in
`lib/libc/stdio`) have a fully prototyped donor twin, so the C17 distance closes
by a converter and not by donor text. The converter in this change
(`tools/c17/kr_to_ansi.py`) rewrites 3,763 of 4,182 K&R definitions it reads under
the production source set, refuses 419 with a stated reason, and leaves
byte-identical object text in all 110 libc files that compile in the harness.

The plan of section 6 has 11 slices that may land before the GizmoBSD rename (defect
fixes and gates, owner decisions D106 and D109) and 12 that wait for it (8
imports and 4 mechanical conversion waves).

## Sources, method and instruments

| Item | Value |
| --- | --- |
| Fork | `origin/main` at `b95a36bc`, the commit after the survey merge (#645) |
| 2.11BSD donor | the reference workspace's `211bsd` checkout at `0a38e59`; patch 432 to 499 commits from its `PATCHES.md` (the log between those commits interleaves CI and README commits, so commit ranges are not used) |
| Patch 500 | `http://www.2bsd.com/2.11BSD/500`, fetched over plain HTTP on 2026-11-16, 3,998,518 bytes, SHA-256 `d52e80258543dfc08298ca3d60212b0d4bb7b6b4d95b579eedd4c42a3d7f6b16` (equal to the survey's hash). The three `sed 's/^X//'` sections were extracted by a parser and never executed; the nested archive of new files (93 files) was extracted by a second parser that refuses any path leaving its root. The download and the extraction were removed afterwards. |
| Post-patch-500 tree | the donor tree with the 1,795 context diffs applied by `patch -p0` in a scratch directory (1,768 files changed; five files reject hunks: `etc/hosts`, `lib/libc/stdio/findfp.c` where this donor deliberately deviates, `local/zmodem/crc.1` and two kernel configuration files, `sys/GENERIC/Makefile` and `sys/conf/GENERIC`) |
| Cross compiler | `arm-none-eabi-gcc` 16.2.0; host compiler `cc` (GCC) 16.2.1; `python3` 3.14.9 |
| Counting rule | each unit takes one primary mechanism (the largest part of its subject or diff) and any number of secondary labels; the "total" columns use the primary label, the "secondary" column counts every label. The labeling is a reading of subjects, descriptions and diffs, recorded per row in `bsd211-patch-mining.tsv`. |

| Instrument | Version | Question | Result |
| --- | --- | --- | --- |
| `git` | 2.56.0 | per-patch file and line counts, diffs, fix-after-fix chains | section 1, section 5 |
| `kr_scan.py` (this tree) | -- | K&R definition census of the fork and the donor | section 5 |
| Coccinelle `spatch` | 1.3.3 | semantic patch for K&R definitions | not usable: SmPL cannot parse an identifier-list definition (parse error on `R f(a, b)` in the minus slice), so a script carries the conversion |
| `pahole` | v1.32 | layouts of `struct fs`, `mount`, `timeval`, `itimerval`, `rlimit`, `direct`, `dinode` | section 3; DWARF 4 from `arm-none-eabi-gcc` 16.2.0 with the PICO kernel flags `bmake -n` prints, calibrated against `sizeof` and `offsetof` below |
| `arm-none-eabi-objcopy` | binutils of the same package | .text identity before and after conversion | section 6 |
| `ruff` | 0.14.11 | format and lint of the two new scripts | clean |
| `cscope` 15.9 | -- | callers of `devname` | invoked once with its output cut; no result is used (the callers come from `git grep` and the survey) |
| `ctags` 6.2.1, `cflow` 1.8, `cppcheck` 2.22.0, `clang-tidy` | -- | symbol, call-graph and static-analysis passes | not run: the questions here (callers of one function, a pattern across 1,500 files) were answered by `git grep` and the scripts named in each section, and a whole-tree cppcheck pass exceeds the single-quick-gate allowance for this host |
| `cproto`, `pmccabe` | -- | prototype extraction, cyclomatic complexity | not run: not installed (install packages `cproto` and `pmccabe`); `protoize` and `unprotoize` left GCC with release 4.x and have no package, and `ansi2knr` ships inside the Ghostscript source tree |

## 1. Taxonomy of what the patches change

| Mechanism | Patches 432-499 | Items 500.1-500.102 | Total | With secondary labels | Applicable (survey) |
| --- | ---: | ---: | ---: | ---: | ---: |
| interface-and-prototype | 6 | 16 | 22 | 27 | 4 |
| dead-code-removal | 2 | 19 | 21 | 31 | 0 |
| error-path-and-lifecycle | 9 | 10 | 19 | 33 | 4 |
| build-system | 6 | 12 | 18 | 27 | 0 |
| pdp11-hardware | 14 | 1 | 15 | 22 | 1 |
| feature-or-option | 5 | 10 | 15 | 24 | 4 |
| logic-defect | 4 | 11 | 15 | 20 | 5 |
| documentation | 1 | 10 | 11 | 21 | 5 |
| time-and-date | 7 | 2 | 9 | 12 | 2 |
| compiler-toolchain | 7 | 2 | 9 | 14 | 0 |
| bounds-and-input | 2 | 4 | 6 | 13 | 0 |
| security-and-permission | 2 | 3 | 5 | 7 | 2 |
| signedness-and-width | 2 | 2 | 4 | 7 | 1 |
| release-bookkeeping | 1 | 0 | 1 | 1 | 0 |
| Total | 68 | 102 | 170 | 259 | 28 |

Commits by primary mechanism for patches 432 to 499 (donor commit ids are in the
TSV):

- interface-and-prototype (6): 440 442 460 477 483 499
- dead-code-removal (2): 459 498
- error-path-and-lifecycle (9): 441 447 453 457 461 482 485 489 492
- build-system (6): 443 455 467 470 478 491
- pdp11-hardware (14): 433 434 438 439 445 448 449 450 474 476 480 484 493 494
- feature-or-option (5): 456 471 481 487 497
- logic-defect (4): 463 464 466 486
- documentation (1): 468
- time-and-date (7): 432 435 454 458 469 472 479
- compiler-toolchain (7): 462 465 473 488 490 495 496
- bounds-and-input (2): 444 475
- security-and-permission (2): 436 451
- signedness-and-width (2): 446 452
- release-bookkeeping (1): 437

Patch 437 is upstream's placeholder and has no commit. The survey classes cross
the mechanisms as follows: the 28 applicable units are 5 logic defects, 5
documentation, 4 interface, 4 error-path, 4 feature, 2 time, 2 security, 1
signedness and 1 hardware (patch 439's brk limit). The 17
not-applicable build-system units are rated so in the survey's rows (PDP-11 trees,
tape builds, programs this tree lacks).

Volume, by file class (donor paths; patches 460 and 499 are the bulk):

| File class | Patches 432-499 (files) | Patch 499 | Patch 500 (files) |
| --- | ---: | ---: | ---: |
| build files | 457 | 354 | 66 |
| commands (`bin`, `usr.bin`, `ucb`) | 350 | 147 | 652 |
| kernel (`sys`, mostly PDP-11 and network) | 268 | 171 | 38 |
| manual pages | 146 | 80 | 246 |
| toolchain (cc, cpp, f77, as) | 110 | 18 | 52 |
| libc | 90 | 60 | 19 |
| system administration | 116 | 46 | 218 |
| games | 72 | 20 | 181 |
| other libraries | 15 | 13 | 289 |
| headers, documents, boot code, other and installed data | 164 | 53 | 34 |
| Total | 1,788 (+51,667 -101,651 lines) | 962 (+12,694 -82,575) | 1,795 (+13,134 -25,345) |

Patch 500 also deletes 289 paths (`500.rm`) and adds 56 (93 files in the nested
archive). The line counts are context-diff changed lines.

Lexical feature counts over added and removed lines of C files show what each
large patch did mechanically. They are regular-expression counts, so they bound
the real figures; patch 499's 900 removed K&R headers are mostly deleted files
(82,575 removed lines).

| Feature | Patch 460 | Patch 499 | Patch 500 |
| --- | ---: | ---: | ---: |
| prototyped definition headers added | 100 | 66 | 9 |
| K&R definition headers removed | 9 | 900 | 141 |
| `const` added | 65 | 26 | 36 |
| `unsigned` or `u_*` added | 15 | 41 | 43 |
| `size_t` added | 0 | 27 | 11 |
| bounded string call or `sizeof` added | 21 | 42 | 101 |
| `free`, `close` or `unlink` calls added | 8 | 18 | 102 |
| `gets()` calls removed | 0 | 41 | 54 |
| time-related tokens removed or added | 1 / 5 | 53 / 0 | 75 / 49 |

Reading: patch 460 is the prototype conversion of libc and the compiler; patch
499 is deletion and stdio modernization; patch 500 is a clean-up wave whose
distinct trait is resource release (102 added release calls), bounded string
operations (101) and the retirement of `gets()` (54 removals).

## 2. Invariants the patches restore, and the search of this tree

Each row states the rule as a checkable predicate, the donor evidence, and the
result of searching `origin/main` for a violation. A search covers tracked C
files outside `legacy/`, `tests/`, `tools/`, `distrib/` and the STM32 and PIC32
ports. "Shipped" means a name in `distrib/rp2040/mi.rp2040`. Evidence classes:
(M) executed, (I) source reading.

| # | Rule | Donor evidence | Search and result |
| --- | --- | --- | --- |
| R1 | An input line never exceeds its buffer; no routine reads without a bound | 6, 26 (`gets` in tftp, mtools), 54 (`snprintf`), 62 (`uucpd` domain), 444 (ftp password), 436 (format string from remote text) | `gets(` has no call in built paths (every hit is a comment or a picoc table row). `sprintf(` appears 623 times in 196 files and `strcpy(`/`strcat(` 833 times in 227 files; the count is a lead and no hit is read. The printf and snprintf probes of the survey cover the formatter. |
| R2 | An allocation result is tested before use | 441 (cpp `calloc`), 500.53, 500.83 | Hits C1 to C3 below; 85 candidate lines read by pattern, 15 read by eye. |
| R3 | Calendar arithmetic is Gregorian and a year prints in full | 435 (MDTM prints 19100), 472 (at leap year), 479, 453 (welcome), 500.55, 500.58 | Hits C4, C5. The other `tm_year` sites in the grep (`asctime.c:56`, `strftime.c:216,221`, `mktime.c`, `getty/get_date.c:53`, `calendar.c:174`) use the full year or `TM_YEAR_BASE`. |
| R4 | A caller's unsigned count reaches the kernel's signed bounded timeout without wrapping or refusal | 500.91 (`usleep`), 500.92 (`sleep`), 452 (`srandom` argument) | Hit C6 (`alarm`); `sleep` is LIBC-4; `usleep(long)` is present. |
| R5 | A returned pointer never refers to storage the function released | 500.50 | The only `closedir`-then-return-into-entry site is `devname` (LIBC-3). `getcwd`, `getwd`, `ttyname` and `scandir` copy the name before `closedir` (read). |
| R6 | Completion wakes every sleeper, including a closer | 500.84 (`xp` drain) | Scripted scan of `sys/`: 35 sleep channels, 30 wakeup expressions; the 14 channels with no literal wakeup are variables or locals (`kern_synch.c`, `ufs_bio.c`, `picopcm.c` through `pcm_wake(arg)`). No RP2040 close routine sleeps; the one close with a loop is `kern_descrip.c:266` (descriptor table). No candidate. |
| R7 | A size checker sees every buffer | 500.40 | Already in the survey: `check_kernel_headroom.py:128-137` measures loaded segments only, so the 16,384 B `.swapram_pool` and 8,192 B `.scratch` lack a budget gate. Gate gap G3. |
| R8 | A structure result passes the object's address and byte length | 463, fixed by 464 (`swapmap` passed by value, then `.m_map`) | All nine `sysctl_rdstruct` call sites in `kern_sysctl.c` (lines 247, 281, 368, 390, 499, 505, 523, 754, 786) pass an address or array. Holds. |
| R9 | A format argument is a literal or a table entry | 436 | Calls with a variable format (`csh`, `id`, `nl`, `diff`, `wump`, `warp`, `forth`, the uucp `syslog(text)` wrappers) read in part: the csh, id and nl sites pick from local literals. uucp is built, not shipped, not read further. No candidate. |
| R10 | A declaration agrees with its definition | 473 (missing prototypes), 442, 500.83 | Hit C7 (`cfree`). |
| R11 | A sysctl identifier or flag bit a patch allocates is free in the receiver | 456, 463, 500 | section 3: four collisions (one flag bit, three identifiers). |
| R12 | A clock-derived stamp is written only after the clock is valid | 458 (superblock time written as 0 at boot) | `mountfs` writes no time (no `fs_time` reference in `ufs_mount.c`); `init_main.c:133` seeds the clock from `fs_time` after the root mount, and the three stamps (`ufs_syscalls2.c:218,225`, `ufs_alloc.c:100`) sit in `ufs_sync` and `balloc`, which run on a mounted volume. Holds in the boot order read (I); not traced under Renode. |
| R13 | A kernel probe with a short buffer copies the minimum, reports the full length, and fails with ENOMEM | 475 | Present: `__sysctl` keeps `savelen` as `size_t` and arbitrates ENOMEM last (`kern_sysctl.c:98,160,198`). |

### Ledger candidates

| Id | Location | Violated rule | Class | Proposed test |
| --- | --- | --- | --- | --- |
| C1 | `bin/ls/ls.c:244-245` (`ls -R`; shipped as `/bin/ls`, `mi.rp2040:395`) | R2: `malloc` result dereferenced at once (`dp->sd_name`) | (I) | host build of `ls.c` with a `malloc` that fails on the n-th call, run over a tree with subdirectories; known-bad is today's source |
| C2 | `bin/ed/ed.c:212` (`zero` buffer; shipped, `mi.rp2040:258`) | R2 | (I) | same harness, `nlall` allocation |
| C3 | `usr.bin/awk/run.c:544` (concatenation; shipped, `mi.rp2040:271`) | R2: `strcpy` into an unchecked `malloc` | (I) | same harness over a concatenation |
| C4 | `bin/date2/date.c:189,203` (shipped as `/bin/date`, `mi.rp2040:381`); a copy at `usr.bin/basic/basic.c:948,962` | R3: `%t` and `%L` test `(tm_year+1900) % 4`, which calls 2100 a leap year | (I) | a `struct tm` harness around the `%t` and `%L` cases against the Gregorian rule for 1900 to 2400 (`date -d` is the DST option here and D104 removes it); latent until the int64 `time_t` of D89 reaches 2100 |
| C5 | `usr.bin/chpass/util.c:44` (built, not shipped) | R3: `"%s %d, 19%d"` with `tm_year` prints "19126" for 2026, the defect of patch 435 | (I) | host call of `ttoa` with a 2026 time |
| C6 (cross-reference, not new) | `lib/libc/gen/alarm.c:20-24` | R4: `it_value.tv_sec = secs` above 100,000,000 s fails `itimerfix` (`sys/kern/kern_time.c:348-350`), so `alarm` arms nothing and returns 4,294,967,295; the `sleep()` repair note on its branch already records the defect and asks whether to document or clamp it | (M) | the executed probe below adds the numbers; the repair follows the owner's answer to that note |
| C7 | `bin/ls/ls.c:81,205-206,226,250-254` against `lib/libc/gen/calloc.c:36-44` | R10: `void cfree();` is called with one argument; the definition has three parameters and ignores two | (I); `-Wlto-type-mismatch` (GCC 16.2.0, executed on the two objects) reports nothing, because a non-prototype declaration is compatible with any arity | `-Wstrict-prototypes -Werror` on `ls.c` rejects the declaration; patch 500.83 removes the calls |

**C6 probe (host execution).** `alarm.c` compiled verbatim with `setitimer`
stubbed to return -1 above 100,000,000 s as `itimerfix()` does (host GCC 16.2.1,
64-bit `long`, the 32-bit target maps a request above `LONG_MAX` to a negative
`tv_sec` and fails the same way):

| Call | `setitimer` calls | `tv_sec` passed | Returned |
| --- | ---: | ---: | ---: |
| `alarm(10)` | 1 | 10 | 0 |
| `alarm(100000000)` | 1 | 100,000,000 | 0 |
| `alarm(100000001)` | 1 | 100,000,001 | 4,294,967,295 |
| `alarm(4294967295)` | 1 | 4,294,967,295 | 4,294,967,295 |

Cross-references that this note does not duplicate: LIBC-3 (`devname`, R5),
LIBC-4 (`sleep`, R4), the retracted LIBC-2 (`getopt` lone dash), the patch 451
and 456 studies of the briefs for `securelevel` and the filesystem overhaul.

Negative results and their sampling: R6 (35 channels; static scan over `sys/*.c`
without the STM32 and PIC32 ports), R8 (nine sites read), R9 (regex for a
variable first argument of the printf family, syslog and err family over built
paths; sites read only in part), R12 (three stamps read), R13 (one function
read), and the `closedir` search of R5 (every `closedir` in `lib/libc`).

## 3. Structures, on-disk layout and interfaces

The headers the patches alter, found by scanning every hunk of `sys/h`,
`include`, `sys/pdp` and `sys/sys` headers for `struct`, `typedef`, ioctl and
sysctl identifier lines (PDP-11-only and network headers excluded):

| Patch | Header | Change | Fork state |
| --- | --- | --- | --- |
| 442, 473 | `stdint.h` | fixed-width typedefs (PDP-11 widths) | the fork's ARM header is its own |
| 456 | `sys/h/mount.h` | `MNT_CLEAN 0x2000`, `MNT_WASCLEAN 0x4000` | **collision**: `MNT_V7 0x2000` at `sys/sys/mount.h:102` |
| 459 | `stdarg.h` | `va_list` typedef | the fork has its own `include/stdarg.h` |
| 460 | `ioctl.h` and five device headers | ioctl group letters become character constants (`_IOR('d', 101, ...)`) because ISO cpp does not substitute inside quotes | present: `sys/sys/ioctl.h:98-102` takes `(x)<<8` and 80 uses are quoted; `include/termios-todo.h:141-144` keeps unquoted letters, which an ISO preprocessor would reject as undeclared identifiers (a loud failure); the file is a stub that no source includes (`sys/arch/rp2040/doc/research/games.md:24`) |
| 463 | `sys/h/vmparam.h` | `VM_NSWAP 5` | present at the same number (`sys/sys/vmparam.h:14`) |
| 499 | `gprof.h` | `struct phdr` removed | no gprof here |
| 500 | `sys/h/sysctl.h` | `KERN_LINEHZ 25`, `HW_TRACEBUF 10`, `HW_DISKSTATS` comment | **collision**: `KERN_OSVERSION 25`, `HW_MACHINE_ARCH 10` (`sys/sys/sysctl.h`) |
| 500 | `sys/h/vmparam.h` | `VM_SWAPDEV 6`, `VM_CP_TIME 7`, `VM_MAXID 8` | **collision** for 6 (`VM_STATS 6`); 7 is free, the fork's `VM_MAXID 7` being the bound |
| 500 | `usr/include/utmp.h` | `ut_time` becomes `time_t` | the fork stores `utime32_t ut_time` (`include/utmp.h:38`); layout-neutral, same seam as D89 |
| 500 | `usr/include/ctype.h` | prototypes for the 14 ctype functions placed ahead of the macros; the `_U`..`_B` bits and the `_ctype_` table stay | the `_ctype_` data symbol is unchanged; `lib/libc/gen/isctype.c` (new) supplies the functions |
| 500 | `usr/include/time.h`, `unistd.h`, `fcntl.h`, `stdlib.h`, `signal.h`, `stdint.h` | prototypes and include guards for functions the fork's headers already declare (survey rows 500.34, 500.89, 500.93, 500.95) | present |
| 500 | `usr/include/inttypes.h` (new) | `imaxdiv_t` as `struct { intmax_t quot, rem; }` | present: `include/inttypes.h:195` |
| 500 | `usr/include/locale.h` (new) | `struct lconv` (9 pointer and further `char` fields), `setlocale`, `localeconv` | absent: no `include/locale.h`; the structure is new here and unmeasured (I) |
| 500 | `sys/h/time.h` | `struct timeval` field `tv_sec` changes from `long` to `time_t` | the kernel keeps `long` (pahole below); same seam as D89 |
| 500 | `sys/h/text.h` | `XPAGI 0x20` removed | no text table in this tree |
| 500 | `sys/h/mount.h` | `mount(2)`, `umount(2)` prototypes for non-kernel code | present: `sys/sys/mount.h:124-125` |

The scan finds no changed on-disk structure in patches 432 to 500 beyond the use
of two more bits of `fs_flags` by patch 456. The sysctl identifier numbers and the
mount flag bits are the only layout-bearing allocations.

**Measured layouts on this ARM ABI** (`pahole` v1.32, DWARF 4, kernel flags of
`bmake -n` for the PICO configuration, `arm-none-eabi-gcc` 16.2.0 `-Os
-mcpu=cortex-m0plus`):

| Structure | Size | Fields the patches touch |
| --- | ---: | --- |
| `struct fs` (superblock) | 1,024 | `fs_time` at 968, `fs_flags` at 1016, `fs_magic2` at 1020; no spare field |
| `struct mount` | 1,040 | `m_flags` is `m_filsys.fs_flags` (`sys/sys/mount.h:57`) |
| `struct dinode` | 64 | `di_flags` at 48 |
| `struct direct` | 72 | `d_name[64]` at 8 (the storage behind LIBC-3) |
| `struct timeval` | 8 | `tv_sec` `long` at 0 |
| `struct itimerval` | 16 | two `timeval` |
| `struct rlimit` | 8 | `rlim_cur` at 0, `rlim_max` at 4 (patch 439) |

Calibration of the layout instrument: a translation unit asserting
`sizeof(struct fs) == 1024` and `offsetof(struct fs, fs_flags) == 1016` compiles;
the same unit with `sizeof(struct fs) == 1023` must fail to compile, and does
(the assertion rejects the known-bad value, so the layout figures above come
from an instrument that rejects a false size).

**Compatibility under the adopted SemVer contract list.** The list (`version-scheme.md`
on branch `docs/version-scheme` at `e4fc69cc`) names on-disk and flash formats,
installed header layouts and fundamental-type widths, system-call numbers and the
sysctl names host tools read. The consequences:

- Patch 456 sets two bits in a word the mount path overwrites (`ufs_mount.c:275`),
  so an older kernel ignores them and a newer kernel reading an older image sees
  "not clean" and checks it. The change is compatible (moves PATCH) if the bits are
  free; with the verbatim values the clean mark would be visible as `MNT_V7` to
  `statfs` for the duration it is set (`ufs_syscalls2.c:47` copies
  `m_flags & MNT_V7` into `f_flags`, and `sbin/mount/mount.c:74,344` prints "v7").
- Patch 500's three colliding sysctl identifiers, imported by number, would renumber
  `KERN_OSVERSION`, `HW_MACHINE_ARCH` and `VM_STATS` for programs built against
  the old header: an interface break in the sense of the list. Imported by name
  with this tree's next free numbers, the change is additive.
- Patch 500's `time_t` typing of `tv_sec` is layout-neutral while `time_t` is 32
  bits and becomes the 8-byte change of D89 afterwards; it lands inside the
  pre-release states, which the baseline rule of the same note leaves unpromised.
- A registry of reserved sysctl and flag numbers is the missing control (slice B11).

## 4. Algorithms worth carrying

| Change | Idea | Cost here | Modern form in the reference trees |
| --- | --- | --- | --- |
| 456 + 458 + 461 filesystem clean state | The superblock records "clean at unmount"; mount copies the on-disk bit to a "was clean" bit before it overwrites `fs_flags`; first write clears it; reboot writes the clean mark before the busy-buffer drain (461 moved `fsclean()` ahead of the drain) and stamps `fs_time` at the call sites, not inside `ufs_sync` (458); `fsck` skips a clean volume unless `-f` forces the check. The three patches touch `ufs_subr.c` in sequence, which is a measured cluster (section 5). | +80 B kernel text (I, survey), fsck +100 B (I); bits need remapping | 4.4BSD-Lite2 `fs.h:219` and OpenBSD `sys/ufs/ffs/fs.h:256` keep a dedicated `fs_clean` byte (OpenBSD adds `fs_flags FS_UNCLEAN` for the mount-time state); this superblock has no spare field, so a dedicated byte needs a field repurposed |
| 500.92 | A chunked `select` loop bounded by the kernel's 100,000,000 s limit | +22 B sketch | FreeBSD `lib/libc/gen/sleep.c` recurses on `seconds > INT_MAX` (return the remainder); OpenBSD and NetBSD pass the value to `nanosleep` with no clamp, which works because `nanosleep` has no 100,000,000 s limit; this kernel has no `nanosleep` |
| 500.57, 500.50 | `devname` looks the name up on demand, so `ps` drops a static tty table | -table in `ps` data; +libc text | OpenBSD `lib/libc/gen/devname.c` copies the name into a static `buf[NAME_MAX+1]` with `strlcpy` before `closedir` (the repair LIBC-3 needs) and uses `fstatat` against the open directory |
| 500.80 | `getopt`-driven option parsing in `rm` and `mv` | ~+40 B per applet (I) | 4.4BSD-Lite2 `bin/rm/rm.c:80` `getopt(argc, argv, "dfiPRrW")` |
| 500.9 | `mkdir -m` through `setmode`/`getmode` | +100 B octal path; +1,328 B with the `setmode` member (survey) | OpenBSD `bin/mkdir/mkdir.c` uses `setmode`/`getmode`; this tree already has `lib/libc/gen/setmode.c` |
| 500.11 | `find` keeps its place when `chdir` or `opendir` fails | ~+10 B (I) | OpenBSD `usr.bin/find/find.c` walks with `fts(3)`; this libc has no `fts`, so the in-place repair is the cheaper form |
| 500.13 | C-locale `setlocale` and `localeconv`, `iso646.h` | ~150 B per linking program (I) | OpenBSD `lib/libc/locale/setlocale.c` (191 lines) is a larger superset; `strtoimax`, `strtoumax`, `imaxabs` and `imaxdiv` already exist in `lib/libc/stdlib` |
| 439 | `RLIMIT_DATA` and `RLIMIT_STACK` compared at three sites | +20 B measured for `brk` only | the 4.4BSD `vm_map` checks are address-space based; a flat window needs one integer comparison per site |
| 475 | length-first sysctl copy | present | -- |

No algorithm in the range needs a data structure absent from the tree. The
patches add no new table, tree or cache; every change in the table is a state
bit, an ordering, a bound or a lookup.

## 5. What else is worth mining

**Regression patterns.** A file touched by several patches marks a subsystem the
maintainers had to revisit. Across patches 432 to 499, 177 of 1,464 touched files
(excluding `VERSION` and the workflow file) were touched by two or more patches.
The largest clusters:

| File | Patches | Reading |
| --- | --- | --- |
| `ucb/top/top.c` | 465, 466, 467, 471, 475, 482, 489, 494 | a new program stabilized over eight patches (swap calculation 467, malloc 489, long user names 494) |
| `sys/sys/kern_sysctl.c` | 432, 451, 463, 464, 475 | 464 repairs 463 (object passed for its address); 475 reworks the length protocol |
| `sys/sys/ufs_subr.c` | 456, 458, 461 | the clean-state machine took three patches to settle |
| `sys/pdpstand/boot.c` | 448, 449, 450 | 449 reverts the single-user breakage of 448; 450 re-adds the multi-user boot behind a switch |
| `libexec/telnetd/telnetd.c` | 481, 483, 484, 487, 489, 493 | feature accretion |
| `lib/cpp/cpp.c` | 439, 441, 460, 495, 496, 499 | compiler churn |

Other fix-after-fix pairs: 494 (DZ driver syntax error) after 480 (DZ 8-bit
support); 495 (`__DATE__` placed wrongly) after 488 (`__TIME__` and `__DATE__`);
500.7 (`tcpd` cruft), 500.77 (`fgetln` omitted) and 500.96 (`xargs` `err` clash)
after 499. Two lessons transfer to the integration plan: an import of 456 carries
458 and 461 with it; and a change that edits an interface a later unit consumes
(463) lands with the consumer's test.

**Build and release mechanics.** Each numbered patch is a text with fixed
sections (Subject, Index, Description, Repeat-By, Fix), a shar archive of new
files, a removal script and a context-diff set; patch 500 has 102 Description,
102 Repeat-By and 102 Fix entries, 13 of the Repeat-By entries stating an
observation in place of a command. The apply recipe is `sh 500.rm; sh 500.new;
patch -p0 < 500.patch`, a manual shutdown and rebuild sequence, removal of the
`*~` files patch creates, and a line appended to `PATCH-LOG`. Every patch edits
`VERSION` (`Current Patch Level`). This tree has equivalents (the ledger rows, the
nightly issue); the transferable convention is the **Repeat-By** field: a defect
report carries the command that shows it, which is this tree's known-bad input
requirement stated by the donor for 102 items.

**Manual-page conventions.** 500.94 puts the header include in the SYNOPSIS of
every section 2 and 3 page, 500.33 corrects two pages that name `<sys/file.h>`,
500.47 and 500.48 add `strdup.3` and the `setpassent` link, 468 adds mdoc
support, 470 keeps `makewhatis.sed` in step with the macro package. A checkable
rule follows: the include named in a section 2 or 3 SYNOPSIS declares the
function (survey item 13 holds 42 of 99 and 48 of 134 pages without one).

**K&R-to-ANSI conversions upstream already made.** Method: `kr_scan.py` over the
fork's tracked C files (1,558 files after the exclusions of section 2; 889 files with
4,451 definition findings, a count that differs from the AST audit's 3,825
because the scan also reads inactive conditional branches) and over the donor at
patch 499 (3,077 files, 2,866 with K&R, 17,193 definitions) and at patch 500
(2,906 files, 2,689 with K&R, 16,090 definitions once the 198 removals of
`500.rm` that exist in the tree apply; 3,100 files and 17,131 definitions before
them), joined on program name and file name. Result: 19
joined files carry fewer definitions in the donor than in the fork, three of
them fully prototyped there (`lib/libc/stdio/fread.c`, `getw.c`, `puts.c`, one
definition each, consistent with the stdio rewrite of 499 that the fork declined
on measured cost), and none with 80 percent line similarity. Patch 460 added about 100
prototyped definition headers (a lexical count) and the diffs of patch 500 net 62
definitions; its removals of dead source delete another 1,041. A donor-text adoption therefore removes
about three definitions of the fork's 4,451; the distance is closed by the
converter of section 6. Negative result, sampled on the 211bsd donor checkout and its
patch-500 reconstruction.

**Other mining targets, ranked by yield for this tree.**

1. The Repeat-By discipline above, applied to ledger rows (cheap, documentation).
2. The invariant searches of section 2 re-run against each new donor patch, since the
   rules R1 to R13 are reusable predicates; five found nothing new (R5, R6, R8,
   R9, R13), which bounds the search cost at a few hours per patch range.
3. The 500.rm list (289 removals) as a deletion checklist for dead source in
   `usr.bin` and `games` that this tree still builds (a name join against `SUBDIR`
   is the cheap check).
4. The 500.32 data-space lesson: under GCC an unreferenced `static` string is
   discarded, so no gain exists here (the survey's `strings` check); the useful
   transfer is the checker that proved it.

## 6. Integration plan

Rules. A slice is one pull request in dependency order. The C17 form follows
`STYLE-GUIDE.md` section 7 and section 8: full prototype, `const` on read-only pointers,
fixed-width types where a width is part of a contract, no K&R definition, every
new check calibrated on a known-bad input. Defect fixes (code written for this
port from the donor's idea, no donor text) may land before the rename (D106,
D109); imports and mechanical waves land after it. Sizes name the compiler
(`arm-none-eabi-gcc` 16.2.0, `-Os`) and mark estimates (I).

### Before the rename (11 slices)

| Id | Slice | Donor | C17 form and gate with known-bad input | Cost (text / RAM) |
| --- | --- | --- | --- | --- |
| B1 | Converter and calibration (this change) plus its Makefile target and `TESTING.md` row | -- | `tools/c17/kr_to_ansi.py` with `kr_to_ansi_test.py`; known-good fixture, known-bad fixture, three mutants killed (below) | 0 / 0 (host tool) |
| B2 | `alarm` bound: document it or clamp it, per the owner's answer to the `sleep()` note (C6) | 500.92 idea | host stub harness as in section 2; known-bad is today's `alarm.c` (returns 4,294,967,295) | 0 B documented; a few bytes clamped (I) / 0 |
| B3 | `devname` stable buffer, `sysctl.c:471` caller (LIBC-3) | 500.50; OpenBSD `devname_nodb` | ASan case reading the name after another allocation; known-bad is today's source | small (I) / 0 |
| B4 | `chown owner:group` | 500.17 | parser contract test; known-bad: today's parser rejects "root:wheel" as an unknown user | +12 B measured (survey) / 0 |
| B5 | `rm` and `mv` through `getopt`, with a lone-dash case | 500.80 | contract test modeled on `check-rmdir-contracts`; known-bad: `rm -- -x` exits 1 today | ~+40 B per applet (I) / 0 |
| B6 | `find` descend repair | 500.11 | unreadable-directory case in `check-find-contracts`; known-bad: today's source continues in the wrong directory | ~+10 B (I) / 0 |
| B7 | `ls`: check `malloc` in `-R` (C1) and drop `cfree` (C7, 500.83) | 441, 500.83 | malloc-failure harness; known-bad: today's `ls.c` faults | ~-20 B to +12 B (I) / 0 |
| B8 | `ed` and `awk` allocation checks (C2, C3) | 441 | same harness | ~+12 B each (I) / 0 |
| B9 | Gregorian leap rule in `date` and `basic` (C4); `chpass` year (C5) | 472, 435 | 2000, 2100, 2104 February fixtures; known-bad: today's rule prints 29 days for 2100 | ~+16 B (I) / 0 |
| B10 | `%.0f` of zero vector in `check-libc-printf-float` | 500.22 | the survey's measured vectors; gate-only | 0 / 0 |
| B11 | Reserved-number registry: a test that fails when an imported sysctl identifier or `fs_flags` bit lies in the fork's allocation | 456, 463, 500 | known-bad: the four colliding donor numbers of section 3 must be rejected; known-good: this tree's own tables | 0 / 0 |

### After the rename (12 slices)

| Id | Slice | Donor | C17 form and gate | Cost |
| --- | --- | --- | --- | --- |
| A1 | Clean state across mount, sync, unmount, reboot and `fsck` (456, 458, 461 together), bits remapped to 0x4000 and 0x8000 | e4dc5d6, 43746ac, 47b8ef2 | `u_int` flag constants with `_Static_assert` against `MNT_V7`; `check-v7-format` and `check-fsck-inode-range`; known-bad: a mount with the clean mark set must leave the `statfs` flags unchanged | ~+80 B kernel text (I), fsck ~+100 B (I) |
| A2 | `RLIMIT_DATA` and `RLIMIT_STACK` at the three sites | 178c297 | one helper comparing `rlim_cur`; a limit case per site; known-bad: a program with a 4 KB data limit grows past it today | +20 B measured for `brk`; the other two (I) |
| A3 | `mkdir -m` octal and symbolic (D102) | 500.9 | `setmode`/`getmode` with `const char *`; mode-string table; known-bad: `mkdir -m 750` is rejected today | +100 B octal, +1,328 B symbolic (survey) |
| A4 | `iso646.h`, `locale.h`, C-locale `setlocale`/`localeconv` | 500.13 | `check-c17-headers`; known-bad: a translation unit using `and` fails today | header-only; ~150 B per linking program (I) |
| A5 | `ps` tty names through `devname` (after B3) | 500.57 | removes the 24-entry table; a row with an unknown tty prints "??" | -table (I) |
| A6 | `date` and libc without the kernel timezone (D104) | 043475c | `tm_gmtoff`-based; `date` contract on `-t`/`-d` removal | shrinks `tzset.o` (I) |
| A7 | `ls -T`, `find` database last entry, `w` without utmp | 500.1, 500.30, 500.51 | per-applet contract tests | small (I) |
| A8 | Manual-page bundle: `access.2`, `lseek.2`, `bk.4` deletion, `strdup.3`, `setpassent.0` link, SYNOPSIS includes | 500.33, 37, 47, 48, 94 | `makewhatis` and a link check; rule: the SYNOPSIS include declares the function | 0 / 0 |
| W1-W4 | Mechanical K&R conversion waves with `kr_to_ansi.py`: W1 libc, W2 box applets, W3 other userland, W4 kernel | -- | each wave: the converter, the strict compile of section 2, object identity of `.text` on the touched files, then `-Werror=old-style-definition`; known-bad: a file left with a K&R definition fails the wave's gate; the kernel wave regenerates `tools/c17/kernel-ledger.txt` with its generator | 0 B text by object identity on the 110 libc files measured; W4 unmeasured |

Order: B1 first (the converter feeds A and W). B11 before A1 and before any slice
that adds a sysctl identifier. B3 before A5. A1 after the filesystem-overhaul study
of D105. W1 before W2 (libc prototypes first so box applets compile against them).
Slices not listed (securelevel patch 451, panic strings, the clock) belong to the
briefs that own them (D101, D108).

### Converter calibration (executed)

`kr_to_ansi.py` rewrites an identifier-list definition when every parameter has a
type the default argument promotions leave unchanged, and refuses the rest:

| Check | Input | Result |
| --- | --- | --- |
| known-good | five definitions (two identifiers, a `register` pointer, an array parameter, an empty list, an implicit-int parameter) | output equals the expected text; a second run changes nothing; host `cc` accepts it under `-Werror=old-style-definition -Werror=strict-prototypes`; the `.text` bytes equal those of the K&R input |
| known-bad | a `u_char` parameter (typedef of unknown width), `char` and `float` parameters, a function-pointer parameter, a comment in the declaration list | output byte-identical to the input; each refusal reported with its reason |
| mutant: no promotion refusal | -- | killed (the float rewrite changes the object text, which the test also asserts) |
| mutant: reversed parameter order | -- | killed (output text differs) |
| mutant: declaration list kept | -- | killed (the output no longer compiles) |

On the production source set (`lib/libc`, `bin`, `sbin`, `usr.bin`, `usr.sbin`,
`libexec`, `games`, `sys/kern`, `sys/dev`, `sys/fs`; 1,415 files) the converter
reads 4,182 definitions: 3,763 convert (801 files change, 652 of them completely) and
419 are refused: 211 for a promoted type or a typedef of unknown width (`u_char`,
`u_short` and similar), 189 for a comment, literal or directive between the
identifier list and the brace, 11 for a function or function-pointer parameter, 5
for no declarator, 3 for a declared name outside the list. Executed validation on
the target compiler: of the 133 libc files that convert, 110 compile before and
after under the userland flags of the survey (`arm-none-eabi-gcc` 16.2.0, `-Os
-mcpu=cortex-m0plus -mthumb -nostdinc`, with `-fpermissive` for implicit int) and
the `.text` bytes are identical in all 110; 23 do not compile before conversion in this harness (headers outside its include
path) and are not run. The converter is not a preprocessor: a definition in each
conditional arm converts alone, and a macro-generated definition stays as
written; the `-Werror=old-style-definition` gate of each wave is the check that
nothing remains. Not wired into a Makefile target in this change: B1 owns the
target, the `TESTING.md` row and the nightly tier.

## 7. Residual owner decisions

The `alarm()` and `setitimer` bound is already a question of the `sleep()` repair
note and is not asked again here. Decisions already answered are not repeated: D99 to D110 settle `securelevel`
(study first), panic strings (shorten), `mkdir -m` (both forms), `date` (adopt
454), the clean flag (import after measuring) and import timing (after the
rename). The following remain, one each.

1. **Patch 500 in the donor repository.** Options: (a) add one donor commit with
   a `PATCHES.md` row and the patch-text hash (recommended: the scope mapper and
   the witness then reach patch 500, and the post-patch tree this note rebuilt in
   scratch becomes reproducible); (b) keep the text as a hashed input that
   notes cite (no donor change, but every future study re-extracts it); (c) skip
   (the mapper keeps reporting patch 499 as the last patch).
2. **Which side keeps its sysctl numbers.** Options: (a) this tree keeps its
   numbers and an import takes the next free identifiers (recommended: programs
   built against the current headers keep working, the contract list is not
   touched, and the host tools read names); (b) adopt the donor numbers and
   renumber this tree (identical numbers to upstream, at the cost of an
   interface break for every installed program).
3. **Where the clean bit lives.** Options: (a) `0x4000` and `0x8000` inside `fs_flags`
   (recommended: no layout change, the mount path already overwrites the word, a
   `_Static_assert` pins the values); (b) repurpose a superblock field as a
   dedicated `fs_clean` byte as 4.4BSD and OpenBSD do (clearer semantics, but the
   1,024-byte `struct fs` has no spare field and the format changes); (c) skip the
   feature (`fsck` stays unconditional, no kernel change). This feeds D105.
4. **Order of the mechanical K&R waves against the rename.** Options: (a) after
   the rename (recommended: the rename edits 622 files and the converter changes
   801 of the 1,415 files it read; sequencing them avoids rebase conflicts in
   the same files); (b) the libc
   wave W1 before the rename (starts the C17 distance reduction sooner, at the
   cost of a conflict surface with the rename's identity edits).
5. **Resource limits.** Options: (a) enforce `RLIMIT_DATA` in `brk` only (+20 B,
   recommended: it is measured and the stack paths need a design for what a
   limit violation means for a flat window); (b) enforce both limits (three sites,
   stack half unmeasured); (c) skip (nothing on the image sets a data limit today).
6. **Shipping the unshipped defects.** Options: (a) fix C5 (`chpass`) and the
   `basic` copy of C4 now although neither ships (recommended: both build in
   `usr.bin/Makefile` and a profile can ship them); (b) fix only the shipped
   `date` path and record the others as unshipped ledger rows.

## Not established

Nothing here was run on a board or under Renode, and no donor text was
imported. C1, C2, C3, C4, C5 and C7 are source readings (I); only C6 and the
converter were executed. The 15 allocation sites read by eye are a sample of 85
pattern hits; the 70 others are unread. Size estimates marked (I) were not
compiled. The mechanism labels are judgments recorded per row; a different reader
could move a unit between neighboring labels, and the primary-label counts are
the figures to compare, not a measurement of the patches. The K&R census joins
files by name, so a renamed or split file is missed; 226 fork files with K&R
definitions have no donor file of the same key (relocated or fork-written). The
post-patch-500 tree came from applying the context diffs to the patch-499 donor
and then the removals of `500.rm`, so the five rejected files are absent from it
and 91 of its 289 removal lines name paths the source-only donor lacks; the
nine `usr/include` diffs and four new headers lie outside the donor tree and were
read from the patch text, not from a patched tree. Donor commits 480 to 499 state
that they skip hunks outside `/usr/src` and `/VERSION`, so their manual-page and
include coverage is partial. Patch 500 hunks outside the 842
paths that map to this tree were counted and not read. The invariant searches R6,
R8, R9, R12 and R13 are negative with the sampling conditions above; a negative
there establishes absence in what was searched, not in the tree.
