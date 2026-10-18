# Bounded SUSv4 and SUSv5 contracts

This ledger separates normative requirements from installed names, source
symbols, linkage and exercised behavior. Issue 7 is POSIX.1-2017, the 2018
edition of the Single UNIX Specification Version 4. Issue 8 is POSIX.1-2024,
the publication boundary used here for Single UNIX Specification Version 5.
The Issue 7 base is `https://pubs.opengroup.org/onlinepubs/9699919799/`; the
Issue 8 base is `https://pubs.opengroup.org/onlinepubs/9799919799/`.

`sus-requirement-corpus.md`, `sus-requirement-corpus.json` and
`sus-requirement-entities.tsv` provide the full structural denominator behind
the selected rows below. The corpus covers 1,617 named Issue 7/Issue 8 Base
Definitions, System Interfaces and utility entities, decomposes each entity
into behavior facets and maps bounded DiscoBSD mechanisms without importing
standards prose or XINIM's Q35-specific closure state. A source page in that
corpus establishes membership only; the evidence and residual columns below
decide each selected DiscoBSD claim.

An unshaded row is a Base requirement. `XSI` names the XSI option. `UP` names
the User Portability Utilities option, and `vi` also requires
`POSIX2_CHAR_TERM`. `OB` marks an obsolescent interface in Issue 8. An option
mark limits the conformance claim; constrained RAM or storage does not remove
the requirement inside a selected option.

## Selected utility contracts

| Utility | Issue 7 clause and mark | Issue 8 clause and delta | DiscoBSD owner and evidence | Disposition |
| --- | --- | --- | --- | --- |
| `cut` | [XCU `cut`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/cut.html), Base, DESCRIPTION/OPTIONS | [XCU `cut`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/cut.html), Base; Issue 8 changes `NLSPATH` wording | `usr.bin/cut`, `/usr/bin/cut` to textbox, `check-textbox-host` | Installed with bounded behavior evidence; whole-page option and locale closure remains open. |
| `paste` | [XCU `paste`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/paste.html), Base | [XCU `paste`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/paste.html), Base; Issue 8 changes `NLSPATH` wording | `usr.bin/paste`, textbox, `check-textbox-host` | Installed with bounded behavior evidence. |
| `dirname` | [XCU `dirname`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/dirname.html), Base | [XCU `dirname`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/dirname.html), Base; Issue 8 aligns DESCRIPTION with `dirname()` and encourages diagnostics for newline bytes in displayed pathnames | `usr.bin/dirname`, textbox, `check-textbox-host` | Installed; Issue 8 pathname and diagnostic delta remains unadmitted. |
| `nl` | [XCU `nl`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/nl.html), XSI, OPTIONS | [XCU `nl`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/nl.html), XSI; no functional Issue 8 delta beyond `NLSPATH` wording | `usr.bin/nl`, textbox; the host gate covers the default mode | Partial. `getlinetype()` rejects the required `p<BRE>` selectors, and no POSIX regex provider is linked. |
| `cksum` | [XCU `cksum`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/cksum.html), Base | [XCU `cksum`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/cksum.html), Base; Issue 8 adds pathname-output guidance | `usr.bin/cksum`, textbox, `check-textbox-host` | Installed with bounded checksum behavior evidence. |
| `expand`, `unexpand` | [XCU `expand`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/expand.html) and [`unexpand`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/unexpand.html), Base | [Issue 8 `expand`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/expand.html) and [`unexpand`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/unexpand.html), Base; `NLSPATH` wording changes | matching sources, textbox, `check-textbox-host` | Installed with bounded behavior evidence. |
| `uuencode` | [XCU `uuencode`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/uuencode.html), Base | [XCU `uuencode`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/uuencode.html), Base; Issue 8 clarifies the historical algorithm and operands | `usr.bin/uuencode`, textbox, `check-textbox-host` | Installed with bounded traditional and base64 cases. Issue 8 operand closure remains open. |
| `uudecode` | [XCU `uudecode`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/uudecode.html), Base | [XCU `uudecode`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/uudecode.html), Base; Issue 8 requires `-o -` to mean standard output and strengthens newline-path guidance | `usr.bin/uudecode`, textbox, `check-textbox-host` | Installed; the Issue 8 `-o -` delta needs an explicit oracle. |
| `vi` | [XCU `vi`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/vi.html), UP and `POSIX2_CHAR_TERM` | [XCU `vi`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/vi.html), same option marks; Issue 8 changes error consequences | `/usr/bin/vi` links to `usr.bin/stevie`; `check-stevie-host` | Name and bounded pty behavior only. The image omits `ex`, and the tree reports `USER_POSIX2_CHAR_TERM` as zero. |
| `printf` | [XCU `printf`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/printf.html), Base; `%b` and `\c` are required | [XCU `printf`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/printf.html), Base; Issue 8 adds numbered conversion specifications and refines `\c` under `%b` precision | `usr.bin/printf`, `/usr/bin/printf` to utilbox | Partial and open. The bounded C17 parser implements `%b`, immediate `\c`, decoded-byte precision, embedded NUL output, ordinary and numbered conversion reuse, signed and unsigned target-width integers, width and precision bounds, numeric-error accumulation, and output-failure status. Native, sanitizer, ILP32, and Cortex-M0+ gates exercise that subset. Required floating and wide-character conversions remain open under the no-FPU and byte-locale footprint boundaries. Positional `*m$` fields are an optional extension and mixed numbered-format behavior is unspecified, so neither closes a Base requirement. |
| `command` | [XCU `command`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/command.html), Base, DESCRIPTION | [XCU `command`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/command.html), Base; Issue 8 requires declaration-utility propagation and records the intrinsic requirement | missing from the Bourne builtin table; `check-posix-sh` carries an expected failure | Open. A csh implementation does not satisfy the `/bin/sh` contract. |
| `getopts` | [XCU `getopts`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/getopts.html), Base, DESCRIPTION/EXIT STATUS | [XCU `getopts`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/getopts.html), Base; Issue 8 requires status one for end-of-options and greater than one for an error, and records the intrinsic requirement | missing from the Bourne builtin table; `check-posix-sh` carries an expected failure | Open. |
| `sh` | [XCU `sh`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/sh.html), Base, plus XCU chapter 2 | [XCU `sh`](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/sh.html), Base; Issue 8 marks only the `-h` option `OB` and applies multiple command-language clarifications | `bin/sh`; `check-posix-sh` runs ILP32 and records passing and expected-failure cases | Partial. Each expected failure remains an open behavior row; a shell name does not imply language closure. |

## Selected interface contracts

| Interface | Issue 7 and Issue 8 clause | Current interface | Required deciding evidence |
| --- | --- | --- | --- |
| `opendir()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/opendir.html), Base; [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/opendir.html) applies Austin Group Defect 368 and requires `FD_CLOEXEC` when `opendir()` associates a descriptor | The opened descriptor passes `fstat()`/`S_ISDIR`, receives `FD_CLOEXEC` through the existing `F_SETFD` syscall and reaches allocation only after admission. Every post-open failure closes once and restores errno; target `DIR` retains 1,040 bytes. | `check-dirent-contracts` executes native, optional ILP32, optional ASan/UBSan and five calibrated mutations; the cross gate pins layout and required imports. [libc-opendir-directory-cloexec.md](libc-opendir-directory-cloexec.md) records the syscall path, stack and linked costs. Atomic open-and-CLOEXEC and board execution remain separate boundaries. |
| `closedir()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/closedir.html) and [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/closedir.html), Base, return `int` and propagate failure | the public declaration returns `int`; the wrapper preserves the close result and errno through exactly-once stream release | Native, ILP32, sanitizer and Smaller C tests exercise the transition; four close mutations calibrate result, errno and ownership. See `libc-closedir-result-contract.md`. Physical-board execution stays open. |
| `scandir()`, `alphasort()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/scandir.html), first standardized in Issue 7; [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scandir.html) carries no new normative delta | Typed public declarations, transactional result publication, selected-entry growth, compact self-describing records, complete failure cleanup and per-call typed sorting. `alphasort()` uses the fixed bytewise locale and preserves errno; the historical `struct direct` header and absent locale-aware `strcoll()` retain a bounded compatibility claim. | `check-scandir-contracts` executes native, ILP32, sanitizer and six calibrated mutation lanes; `check-scandir-contracts-cross` compiles the target and calibrates import inspection. [libc-scandir-allocation-contract.md](libc-scandir-allocation-contract.md) records resource accounts and residuals. |
| `strcasecmp()`, `strncasecmp()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/strcasecmp.html) and [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcasecmp.html), Base; the POSIX locale acts as lowercase conversion followed by byte comparison; Issue 8 requires unchanged `errno` on valid POSIX-locale input | Typed definitions fold ASCII uppercase letters, preserve high bytes and compare unsigned byte values without permanent storage. | The exhaustive 65,536-pair oracle passes under signed and unsigned char at native/ILP32/sanitized widths, including errno and NUL/count boundaries. Four mutations calibrate rejection. `libc-ascii-casefold-contract.md` records the intentional high-byte compatibility change and linked savings; board execution remains open. |
| `vscanf()`, `vfscanf()`, `vsscanf()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/vscanf.html) and [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vscanf.html), Base | `include/stdio.h` declares all three interfaces; `scanf.o` implements them over `_doscan`, and `vsscanf()` shares the bounded string-stream constructor with `sscanf()` | `check-libc-scanf` executes all three paths at host width, ILP32 and under AddressSanitizer. The Smaller C `t18_stdio` fixture compiles, links and executes `vsscanf()` through the target archive. Board loading and execution remain a separate evidence class. |
| `getpgrp()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpgrp.html) and [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpgrp.html), Base, signature `pid_t getpgrp(void)` | `include/unistd.h` declares `pid_t getpgrp(void)`. Syscall 81 takes zero arguments and returns the calling process group without a failure path. | `check-libc-host-contracts` binds the function-pointer type and rejects an argument. `check-kernel` executes the current-process result while preserving poisoned argument storage, and `check-kernel-metadata` rejects an argument-bearing syscall 81. |
| `sysconf()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sysconf.html) and [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sysconf.html), Base; Issue 8 adds selectors including `_SC_NPROCESSORS_CONF`, `_SC_NPROCESSORS_ONLN`, `_SC_DEVICE_CONTROL` and `_SC_NSIG` | `lib/libc/gen/sysconf.c` implements ten explicitly named selectors: five inherited limits, the configured and online processor counts, signal count, software page size and the unsupported Device Control option. | `check-libc-runtime-limits` executes every selected value, distinguishes an unsupported option from an invalid selector through `errno`, and rejects five focused mutations; the cross gate proves target compilation and the expected errno import. `_SC_CHILD_MAX` remains absent because `fork1()` counts effective UIDs and admits a twenty-first process when `MAXUPRC` is 20. Every other required selector remains an explicit gap. |
| `confstr()` | [Issue 7 XSH](https://pubs.opengroup.org/onlinepubs/9699919799/functions/confstr.html) and [Issue 8 XSH](https://pubs.opengroup.org/onlinepubs/9799919799/functions/confstr.html), Base; Issue 8 advances `_V6_`/`_V7_` environment names to `_V7_`/`_V8_` | `lib/libc/gen/confstr.c` implements `_CS_PATH` only | `check-libc-runtime-limits` executes length, truncation, termination and unknown-selector behavior for `_CS_PATH`; the cross gate proves target compilation and the expected errno import. Versioned environment strings remain absent. |

The scanner count bound uses `EOVERFLOW=82`. Both editions of
`<errno.h>` require distinct positive values for `EOVERFLOW` and `ERANGE`;
their permitted alias exceptions cover `EAGAIN`/`EWOULDBLOCK` and
`ENOTSUP`/`EOPNOTSUPP`. The local errno authority adds a distinct value and
regenerates `kernel_errmsg()` metadata; `check-kernel-metadata` binds both headers,
the master and generated output.

`funopen` and `radixsort` remain BSD extensions. Their source or declaration
state never counts toward a POSIX row, and their absence never counts as a
POSIX gap.

## Feature and limit reporting

`include/unistd.h` exposes `_SC_ARG_MAX`, `_SC_CLK_TCK`,
`_SC_NGROUPS_MAX`, `_SC_OPEN_MAX`, `_SC_JOB_CONTROL`,
`_SC_NPROCESSORS_CONF`, `_SC_NPROCESSORS_ONLN`, `_SC_NSIG`, `_SC_PAGESIZE`,
`_SC_DEVICE_CONTROL` and `_CS_PATH`. `check-libc-runtime-limits` binds those
selectors to `ARG_MAX=5120`, `CLK_TCK=60`, `NGROUPS_MAX=16`, `OPEN_MAX=30`,
job-control value one, one configured and online kernel processor, `NSIG=32`,
the 1,024-byte software page and `_POSIX_DEVICE_CONTROL=-1`, plus
`_PATH_STDPATH`. The Device Control selector preserves `errno`; invalid
selectors set `EINVAL`. Five source mutations calibrate those distinctions and
values. The cross gate compiles the target objects and verifies their expected
errno import.

`_SC_CHILD_MAX` stays absent. `fork1()` counts `p_uid`, the effective UID, and
tests `a > MAXUPRC` before creating a child. With `MAXUPRC=20`, the kernel can
create a twenty-first process for that effective UID. A truthful selector
requires a separate kernel change that defines and enforces the real-UID limit
without increasing the process-table footprint.
The header defines neither `_POSIX_VERSION`, `_POSIX2_VERSION` nor
`_XOPEN_VERSION`.

The legacy `CTL_USER` compatibility path reports `USER_POSIX2_VERSION` and
all listed POSIX.2 option values as zero. The kernel reports
`KERN_JOB_CONTROL=1`, while `KERN_POSIX1` and `KERN_SAVED_IDS` remain zero.
Those values describe bounded implementation features and deliberately make
no edition-wide conformance claim. The shell's missing job-control utilities
remain separate behavior gaps even where the kernel process-group mechanism
exists.

The next interface PR must update this ledger when a positive or negative
oracle changes. The Issue 7 judgment stays recorded beside the Issue 8 delta;
an Issue 8 change never silently rewrites the earlier contract.
