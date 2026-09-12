# POSIX Utility Coverage and Porting Candidates for DiscoBSD/RP2040

Target platform (fixed, from task context): DiscoBSD (2.11BSD-derived Unix) on
a Raspberry Pi Pico, RP2040, Cortex-M0+, no MMU, at most 96 KB per process,
a.out binaries cross-built with `arm-none-eabi-gcc -Os`, 2.11BSD libc with
partial POSIX support (no threads, no mmap, no dynamic linking). Root
filesystem is 840 KB total, about 340 KB free. Every utility is a separate
a.out binary, typically 15-40 KB, so binary count is a real cost, not just
per-binary size.

## Part 1: POSIX coverage gap analysis

The POSIX.1-2017 (SUSv4, IEEE Std 1003.1-2017) "Shell and Utilities" volume
(XCU) enumerates the utilities a conforming system must provide. The set is
drawn from IEEE Std 1003.2-1992 plus the `c99` utility, and the full index
of 256 names is published at
[the XCU utilities index](https://pubs.opengroup.org/onlinepubs/9699919799/idx/utilities.html).
The Linux Standard Base (LSB) Core module command list overlaps heavily with
POSIX XCU and adds no additional items relevant to this comparison; see the
[LSB Core Specification 4.0, Command Behaviour chapter](https://refspecs.linuxfoundation.org/LSB_4.0.0/LSB-Core-generic/LSB-Core-generic.html#COMMAND)
and the [Linux Standard Base overview](https://en.wikipedia.org/wiki/Linux_Standard_Base).

Shell built-ins that POSIX also lists as "utilities" (`alias`, `bg`, `cd`,
`command`, `fc`, `fg`, `getopts`, `hash`, `jobs`, `read`, `type`, `ulimit`,
`umask`, `unalias`, `wait`) are provided by the tree's `sh` and `csh` and are
excluded from the gap table below -- they require no separate a.out. The
SCCS suite (`admin`, `delta`, `get`, `prs`, `rmdel`, `sact`, `unget`, `val`,
`what`) and the batch-queueing suite (`qalter`, `qdel`, `qhold`, `qmove`,
`qmsg`, `qrerun`, `qrls`, `qselect`, `qsig`, `qstat`, `qsub`, `at`, `batch`)
are mandatory in POSIX but essentially unused outside SCCS-based source
control and POSIX batch schedulers; they are listed once each as a single
row rather than exploded, since none is a realistic port target for this
platform. `talk`, `uustat`, `uux`, `lp` are networked or print-spooler tools
that are out of scope for a networkless embedded target.

### Missing-but-mandatory utilities

| Utility | Purpose |
|---|---|
| `cut` | Extract selected fields/columns from each line of a file. |
| `paste` | Merge corresponding lines of files side by side. |
| `dirname` | Strip the last path component, printing the parent directory. |
| `expand` | Convert tabs to spaces. |
| `unexpand` | Convert runs of spaces back to tabs. |
| `nl` | Number the lines of a file. |
| `patch` | Apply a diff (patch file) to an original file. |
| `cksum` | Compute a POSIX checksum and byte count of a file. |
| `mkfifo` | Create a FIFO (named pipe) special file. |
| `link` | Call `link(2)` directly to create a hard link (distinct from `ln`). |
| `unlink` | Call `unlink(2)` directly to remove a directory entry (distinct from `rm`). |
| `getconf` | Print POSIX configuration variable values (e.g. `PATH_MAX`). |
| `logger` | Write a message into the system log. |
| `uuencode` | Encode a binary file into ASCII for transport. |
| `uudecode` | Decode a `uuencode`-produced file back to binary. |
| `vi` / `ex` | POSIX standard screen/line text editor (`ed` is present, `vi` is not). |
| `csplit` | Split a file into sections determined by context lines. |
| `pax` | Portable archive interchange (POSIX archive tool; `tar` is present but `pax` is a distinct mandated utility). |
| `tput` | Query/manipulate the terminal capability database (terminfo/termcap). |
| `strings` | Print printable-character sequences found in binary files. |
| `iconv` | Convert text between character encodings. |
| `locale` / `localedef` | Query/define locale information (system has no locale support). |
| `mailx` | POSIX mail user agent (`mail` and `rmail` are present, `mailx` specifically is not). |
| `crontab` | User interface to install/list/edit cron jobs (`cron` daemon is present, `crontab` is not). |
| `gencat`, `fuser`, `ipcs`, `ipcrm`, `c99`, `asa`, `cflow`, `cxref`, `fort77`, `pathchk`, `newgrp`, `tabs` | Assorted low-value mandatory utilities (message catalogs, IPC inspection, a `c99`-named compiler invocation, FORTRAN tooling, path validation) unlikely to be worth porting to this target. |
| SCCS suite (`admin`, `delta`, `get`, `prs`, `rmdel`, `sact`, `unget`, `val`, `what`) | Source Code Control System; obsolete version-control tooling. |
| Batch queueing suite (`at`, `batch`, `qalter` ... `qsub`) | POSIX batch job scheduling; not meaningful on a single-process-at-a-time 96 KB target. |

### Explicit resolution of the requested name list against the tree

Checked one by one against the `bin`/`usr.bin`/`sbin`/`usr.sbin`/`libexec`
lists given in the task:

- `cut` -- **absent**.
- `paste` -- **absent**.
- `tr` -- **present** (`usr.bin/tr`).
- `sed` -- **present** (`usr.bin/sed`).
- `awk` -- **present** (`usr.bin/awk`).
- `dirname` -- **absent**.
- `uniq` -- **present** (`usr.bin/uniq`).
- `xargs` -- **present** (`usr.bin/xargs`).
- `cksum` -- **absent**.
- `cmp` -- **present** (`usr.bin/cmp`).
- `expand` -- **absent**.
- `unexpand` -- **absent**.
- `fold` -- **present** (`usr.bin/fold`).
- `head` -- **present** (`usr.bin/head`).
- `nl` -- **absent**.
- `od` -- **present** (`usr.bin/od`).
- `patch` -- **absent**.
- `tee` -- **present** (`usr.bin/tee`).
- `wc` -- **present** (`usr.bin/wc`).
- `which` -- **absent** (`usr.bin/whereis` is present but is not the same utility as `which`; `which` is not POSIX-mandatory but is an extremely common LSB/shell convenience).
- `chmod` -- **present** (`bin/chmod`).
- `cp` -- **present** (`bin/cp`).
- `mkfifo` -- **absent**.
- `ln` -- **present** (`bin/ln`).
- `mv` -- **present** (`bin/mv`).
- `rm` -- **present** (`bin/rm`).
- `rmdir` -- **present** (`bin/rmdir`).
- `touch` -- **present** (`usr.bin/touch`).
- `sh` -- **present** (`bin/sh`).

## Part 2: Candidate source projects

### toybox

[toybox](https://github.com/landley/toybox) is licensed 0BSD (relicensed
from BSD in March 2013), confirmed by the
[project LICENSE file](https://github.com/landley/toybox/blob/master/LICENSE)
and by [Wikipedia's Toybox article](https://en.wikipedia.org/wiki/Toybox),
which notes the permissive license is deliberately compatible with Android's
build (toybox ships as Android's core userland, including its rooting-adjacent
tooling). toybox is normally built as a single multicall binary that
dispatches on `argv[0]`; [main.c](https://github.com/landley/toybox/blob/master/main.c)
implements that dispatch. Reported static-binary sizes for toybox builds run
around 596 KB for a build covering roughly half of BusyBox's applet count,
and full BusyBox multicall binaries land near 1.1 MB with about 397 commands,
per discussion summarized in the [Toybox vs BusyBox size comparison thread](https://www.mail-archive.com/toybox@lists.landley.net/msg09479.html)
and [Toybox on Grokipedia](https://grokipedia.com/page/Toybox). Those figures
are for glibc/musl Linux x86/ARM builds with dozens of applets compiled in --
not directly comparable to this platform, but they establish that a toybox
multicall binary with a broad applet set is one to several hundred KB, which
does not fit in 96 KB per process nor in the 340 KB free on this root
filesystem. A single-applet or few-applet toybox multicall build (e.g. just
`cut`+`paste`+`dirname`+`nl` compiled in) would be far smaller, but toybox's
build system, its generated `generated/config.h`/Kconfig-style option
selection, `lib/*.c` helper layer, and its assumption of a reasonably modern
POSIX/Linux libc (getopt_long-based option parsing, `PATH_MAX`-independent
dynamic buffers, some reliance on `/proc`) all require rework for 2.11BSD
libc: K&R-free but still needs stubbing of absent syscalls, no `mmap`-backed
buffer tricks, and conversion of long-option parsing to short-only or a
bundled `getopt_long`. Net assessment: per-tool extraction from toybox
source (compiling one applet's `.c` file plus `lib/` helpers into its own
a.out) is more plausible than the multicall model on this target; expect a
single extracted-and-trimmed applet in the 15-30 KB range after `-Os`, but
each extraction is nontrivial engineering, not a stock build.

### busybox

[BusyBox](https://busybox.net/) is licensed strictly under GPLv2 (not
GPLv2-or-later; only pre-1.2.2 releases may be redistributed under other GPL
terms), per the project's own
[license notice](https://web.nettworks.org/git/projects/FLI4L/repos/busybox/raw/docs/busybox.net/license.html?at=ad88d5a4cfe200191bce4db41b447969719cb8ac)
and corroborated by [Wikipedia's BusyBox article](https://en.wikipedia.org/wiki/BusyBox)
and the [LWN report on the GPLv2-only decision for 1.3.0](https://lwn.net/Articles/202113/).
DiscoBSD/2.11BSD is BSD-licensed. Copying BusyBox source (even a single
applet) into a BSD-licensed tree creates a GPLv2 compliance obligation
(source availability, copyleft propagation) that the rest of the tree does
not carry, and would require the distributed image to satisfy GPLv2 terms
for at least that component. **This is a license conflict**, not just a
style mismatch: BusyBox source should not be used as a porting source for
this tree; treat toybox, sbase, or BSD-derived sources as the priority
instead, given they are all permissively (BSD-style/MIT/0BSD) licensed and
compatible with 2.11BSD's own license.

### sbase (suckless)

[sbase](https://git.suckless.org/sbase/file/LICENSE.html) is MIT-licensed,
confirmed by the project's LICENSE file and by the
[Wikipedia sbase article](https://en.wikipedia.org/wiki/Sbase), which
describes it as implementing portable UNIX tools "in a minimal way" per
POSIX, intended (with ubase) to form a base system comparable to BusyBox but
smaller and simpler, and already ported to Linux, *BSD, OS X, Haiku, Solaris,
and SCO OpenServer. sbase's own tool list already includes `cut`, `dirname`,
`nl`, `mkfifo`, and `paste` (confirmed against the
[sbase README](http://git.suckless.org/sbase/file/README.html)), which
covers a large fraction of this report's gap list in one MIT-licensed
codebase. sbase targets POSIX and C99; porting to 2.11BSD libc requires:
replacing any C99-only library assumptions (`<stdint.h>` fixed-width types
are fine, but any reliance on `getline`, `reallocarray`, `err(3)`/`warn(3)`
family behavior, or wide-character functions needs verification against
2.11BSD's actual libc surface), no `mmap` (sbase's `cat`/`sort` and similar
tools that may try `mmap`-then-`fread` fallback should already fall back to
`read()` since sbase deliberately avoids GNU/Linux-only APIs, but each tool
needs individual verification), narrow `ino_t`/`off_t`/`time_t` on 2.11BSD's
16-bit-era type definitions, and no threads (sbase is single-threaded by
design, so this is a non-issue). Because sbase tools are individually small,
single-purpose C files with minimal dependencies, each is a realistic
single-binary port; expect 10-25 KB a.out size per tool after `-Os`
(consistent with the 15-40 KB range already observed for the existing tree's
utilities of comparable complexity).

### 2.11BSD and 4.4BSD-Lite original sources

The DiscoBSD/RetroBSD lineage's own ancestor, 2.11BSD, is archived at
[TUHS's 2.11BSD distribution tree](https://www.tuhs.org/Archive/Distributions/UCB/2.11BSD/)
and the [TUHS utree browser for 2.11BSD](https://www.tuhs.org/cgi-bin/utree.pl?file=2.11BSD),
with setup documentation at
[minnie.tuhs.org's 2.11BSD Setup page](https://minnie.tuhs.org/PUPS/Setup/2.11bsd_setup.html)
and a git mirror at
[RetroBSD/2.11BSD on GitHub](https://github.com/RetroBSD/2.11BSD). This is
the single best-fit source for any missing utility that 2.11BSD itself
shipped historically (many of the "missing" POSIX utilities above, e.g.
`cut`, `nl`, were never part of the 2BSD/2.11BSD `usr.bin` set and must come
from 4.4BSD-Lite instead). 4.4BSD-Lite-derived sources for the missing
utilities exist in the current [NetBSD src usr.bin tree](https://github.com/NetBSD/src/tree/trunk/usr.bin)
and [OpenBSD's src tree](https://github.com/openbsd/src) (see below); both
descend from 4.4BSD-Lite and carry BSD-style licenses per file. Because
2.11BSD source is already K&R/pre-ANSI in many files, pulling a utility
directly from 2.11BSD needs the least adaptation for calling convention and
type width but the utility may simply not exist there (it predates POSIX.2).
Pulling from 4.4BSD-Lite-derived NetBSD/OpenBSD source gets the utility but
requires down-porting: replacing ANSI-only constructs is not the issue (both
NetBSD and OpenBSD source is modern C), the issue is their use of
`err(3)`/`warn(3)`, `getopt_long`, `sysexits.h`, `reallocarray`, `strlcpy`
(2.11BSD may lack some of these -- verify against the tree's actual libc),
and wide `ino_t`/`dev_t`/`off_t`/`time_t` typedefs that must be narrowed
back for the 16-bit-era ABI.

### OpenBSD src/usr.bin and src/bin

OpenBSD's source tree is mirrored at
[github.com/openbsd/src](https://github.com/openbsd/src), and the project's
[copyright policy](https://www.openbsd.org/policy.html) states that the ISC
license is preferred for new code, functionally equivalent to a two-clause
BSD license with redundant Berne-convention language removed; individual
files still carry a mix of ISC, 2-clause BSD, 3-clause BSD, and (rarely)
other licenses, so each file's license must be checked before use --
[Wikipedia's ISC license article](https://en.wikipedia.org/wiki/ISC_license)
confirms the ISC-BSD equivalence. OpenBSD's small single-purpose utilities
(e.g. `usr.bin/paste`, browsable at the
[OpenBSD CVSweb paste directory](http://cvsweb.openbsd.org/cgi-bin/cvsweb/src/usr.bin/paste/))
are exactly the class of tool needed here: `cut`, `paste`, `nl`, and similar
are typically a few hundred lines of straightforward C. A curated
"[baseutils](https://github.com/ibara/baseutils)" project already exists
that repackages OpenBSD userland utilities for portability to non-OpenBSD
systems, which may shortcut some of the adaptation work. Porting
requirements mirror the general 4.4BSD-Lite-descendant case above:
`getopt_long` replacement (2.11BSD has traditional short-option `getopt`
only -- either bundle a minimal `getopt_long` shim or rewrite call sites to
short options), narrow integer/`ino_t` types, no `mmap`, no threads, and
`-Os` code-size discipline (OpenBSD sources are not written with an 80 KB
process ceiling in mind, so buffer sizes and any recursive algorithms need a
size audit).

### NetBSD src/usr.bin and src/bin

NetBSD's source is BSD-licensed and mirrored at
[github.com/NetBSD/src](https://github.com/NetBSD/src) (usr.bin subtree
directly browsable at
[NetBSD/src usr.bin](https://github.com/NetBSD/src/tree/trunk/usr.bin)),
with an additional automated unofficial mirror at
[IIJ-NetBSD/netbsd-src](https://github.com/IIJ-NetBSD/netbsd-src) synced
from anoncvs every two hours. NetBSD utilities are functionally equivalent
porting candidates to OpenBSD's for this purpose (same 4.4BSD-Lite lineage,
same class of adaptation work: `getopt_long`, wide types, `-Os` size
discipline, no `mmap`/threads). NetBSD's tree tends to carry the more
conservative traditional 4-clause-turned-3-clause BSD license text on older
files rather than ISC, but this is still fully permissive and compatible
with a BSD-licensed target tree.

### RetroBSD userland

[RetroBSD](https://github.com/RetroBSD/retrobsd) is a direct port of
2.11BSD to the Microchip PIC32 microcontroller family (128 KB RAM, 512 KB
flash), implementing the standard POSIX process API (`fork`, `exec`,
`wait4`, etc.) on bare embedded hardware, per the
[RetroBSD repository](https://github.com/RetroBSD/retrobsd) and the
[RetroBSD wiki](https://retrobsd.org/wiki/doku.php). A companion repository,
[RetroBSD/retrobsd-early](https://github.com/RetroBSD/retrobsd-early),
preserves the early 2.11BSD-based development history. Because DiscoBSD is
itself a 2.11BSD-derived embedded port occupying the same niche (small MCU,
no MMU, tight RAM/flash budget, a.out toolchain) and RetroBSD targets the
same 2.11BSD userland ABI and the same size-constrained embedded model,
**RetroBSD's userland is the closest-fitting source of all the candidates
listed here** -- its utilities are already proven to build and run within a
comparable KB-scale process budget under the same libc lineage, so this
should be the first place to look for any of the missing utilities before
reaching for OpenBSD/NetBSD/sbase/toybox source. Where RetroBSD ships a
given missing utility, porting is expected to require only build-system and
header-path adjustment (RetroBSD's own cross-toolchain differs from
`arm-none-eabi-gcc`, targeting PIC32/MIPS rather than Cortex-M0+, so
architecture-specific assembly, if any, must be excluded and any
RetroBSD-vs-DiscoBSD libc divergences reconciled), not the K&R-header,
`getopt_long`, or type-width rework that OpenBSD/NetBSD/sbase/toybox sources
need.

## Part 3: Specific tool verification and source recommendation

| Tool | Present in tree? | Recommended source | Notes |
|---|---|---|---|
| `cut` | No | RetroBSD userland first; else sbase (MIT) | Simple field/column extraction, small. |
| `paste` | No | RetroBSD userland first; else sbase (MIT) or OpenBSD `usr.bin/paste` (see [CVSweb listing](http://cvsweb.openbsd.org/cgi-bin/cvsweb/src/usr.bin/paste/)) | Straightforward, low LOC. |
| `dirname` | No | sbase (MIT) | Trivial string-manipulation tool, near-trivial port. |
| `which` | No (`whereis` present, not equivalent) | sbase-style small implementation or a short local script/tool | Not POSIX-mandatory; low priority given `whereis` already covers most lookup needs. |
| `nl` | No | sbase (MIT) or RetroBSD userland | Small, no unusual dependencies. |
| `patch` | No | 4.4BSD-Lite-derived NetBSD/OpenBSD `usr.bin/patch` (BSD-style license) | Historically not a tiny tool (diff-parsing engine); expect it to sit at the upper end of the platform's 15-40 KB binary range, and evaluate whether it is worth the flash budget versus simply re-applying changes with `ed`/`sed` on this platform. |
| `cksum` | No | sbase (MIT) or 4.4BSD-Lite-derived source | Small, POSIX checksum algorithm is simple to implement compactly. |
| `mkfifo` | No | sbase (MIT) or RetroBSD userland | Thin wrapper around `mkfifo(3)`/`mknod(2)`; verify 2.11BSD's `mknod`/FIFO support in-kernel before porting (the tree already ships `sbin/mknod`, so FIFO device-node support likely already exists). |
| `expand` | No | sbase (MIT) | Simple tab-to-space converter. |
| `unexpand` | No | sbase (MIT) | Simple inverse of `expand`; often built as one shared source file with `expand`. |
| `getconf` | No | 4.4BSD-Lite-derived NetBSD/OpenBSD source, trimmed | Needs a `confstr`/`sysconf`/`pathconf` table appropriate to 2.11BSD's actual limits; low practical value on a single-configuration embedded image and thus low priority. |
| `logger` | No | sbase (MIT) already includes `logger` | Requires a working `syslog(3)`-equivalent in 2.11BSD libc; verify the tree has one before porting the client. |
| `uudecode` / `uuencode` | No | sbase (MIT) already includes both | Useful for getting binary data on/off the target without a network stack, given the platform has no `telnet`/`ftp`. |
| `xargs` | Yes (`usr.bin/xargs`) | N/A | Confirmed present in the tree's `usr.bin` list; no porting action needed. |
| `less` | No (`more` present) | Not recommended | `more` already covers the paging need this tiny a fileystem is likely to have; `less` is substantially larger (terminfo/regex search/back-scroll buffering) for a feature set unlikely to be used on a resource-constrained console. Skip in favor of the already-present `more`. |
| `telnet` / `ftp` | No | Out of scope | The target has no network stack, per the fixed platform description; these tools have no function on this image and are excluded from the porting list entirely. |
| `bsdtar` (libarchive) vs. tree's `bin/tar` | tree ships `bin/tar` already | Keep the existing `bin/tar`; do not port `bsdtar`/libarchive | libarchive is a large, feature-rich, dynamically-extensible archive library (multi-format, filter chains, ACL/xattr support) built for systems with `mmap`, threads, and megabytes of flash/RAM headroom -- none of which this platform has. The existing `bin/tar` almost certainly already satisfies this platform's archive needs (read/write of plain tar images for firmware/rootfs packaging); pulling in libarchive would consume a large fraction of the 340 KB free filesystem budget for capability this platform will not exercise (no compression filters worth the flash cost, no ACL/xattr concept in 2.11BSD). No action recommended here beyond confirming the existing `tar`'s format coverage meets the actual workflow. |

## Prioritized porting list

1. Port `cut` from RetroBSD userland if present there, else sbase (MIT) -- expected a.out size approximately 12-18 KB.
2. Port `paste` from RetroBSD userland if present there, else sbase (MIT) -- expected a.out size approximately 12-18 KB.
3. Port `dirname` from sbase (MIT) -- expected a.out size approximately 8-12 KB (near the tree's smallest existing utilities, e.g. `basename`).
4. Port `nl` from sbase (MIT) -- expected a.out size approximately 12-18 KB.
5. Port `mkfifo` from sbase (MIT), after confirming 2.11BSD kernel FIFO support via the existing `sbin/mknod` -- expected a.out size approximately 10-15 KB.
6. Port `cksum` from sbase (MIT) or 4.4BSD-Lite-derived NetBSD/OpenBSD source -- expected a.out size approximately 10-15 KB.
7. Port `expand` and `unexpand` together from sbase (MIT), as one shared-source build -- expected a.out size approximately 10-16 KB each.
8. Port `uuencode`/`uudecode` from sbase (MIT), useful for moving binary data on/off the target with no network stack -- expected a.out size approximately 15-22 KB each.
9. Port `logger` from sbase (MIT), contingent on confirming a working syslog facility in 2.11BSD libc -- expected a.out size approximately 10-15 KB.
10. Port `patch` from 4.4BSD-Lite-derived NetBSD or OpenBSD source (BSD-style license), expect the heaviest port on this list given its diff-parsing engine -- expected a.out size approximately 25-38 KB, near the top of the platform's established 15-40 KB per-binary range.
