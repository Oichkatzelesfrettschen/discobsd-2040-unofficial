# NetBSD 11 micro-backports for a C17 DiscoBSD process window

The first four adopted NetBSD 11 backports repair `strtol()`/`strtoul()`,
bounded `syslog()` formatting and `cat` block I/O, then add component-wise
parent removal to `rmdir`. Five small historical
integer-conversion changes compose into a C17
implementation that rejects invalid bases, preserves the input end pointer,
recognizes a hexadecimal prefix only when a digit follows, treats input bytes
as unsigned, and classifies digits without the ctype table. The DiscoBSD
adaptation also recognizes the six C-locale whitespace bytes directly because
the library has no `setlocale()` implementation.

The direct classification has a target-specific payoff. The former ARM a.out
objects occupied 260 bytes for `strtol.o` and 268 bytes for `strtoul.o`. The
C17 objects occupy 332 and 308 text bytes, an increase of 112 text bytes.
Both objects cease to reference `_ctype_`; `ctype_.o` carries 260 writable
data bytes. A process that links only `strtol()` and no other ctype user adds
72 text bytes and avoids 260 writable bytes, reducing its window use by 188
bytes. The corresponding `strtoul()` case reduces window use by 220 bytes. A
process that links both adds 112 text bytes and reduces window use by 148
bytes. Linkage determines whether a given program realizes a reduction, so
these values are bounded opportunities rather than claims about every image.

## Source identities and discovery

The comparison uses DiscoBSD commit
`0bf278739cd0db790eaba4727c4bb5562418d353` and the local NetBSD `netbsd-11`
branch at `1e8e727e461bb760bf82162f33b64afea02da2f3`. The following searches
found the contract history and the local dependency:

```sh
git -C ../netbsd-src log -S 'base < 2 || base > 36' -- \
    common/lib/libc/stdlib/_strtol.h common/lib/libc/stdlib/_strtoul.h
git -C ../netbsd-src log -S "s[1] >= '0'" -- \
    common/lib/libc/stdlib/_strtol.h common/lib/libc/stdlib/_strtoul.h
git -C ../netbsd-src log -G 'isalpha|isdigit' -- \
    common/lib/libc/stdlib/_strtol.h common/lib/libc/stdlib/_strtoul.h
git log -S 'register unsigned long acc' -- lib/libc/stdlib/strtol.c \
    lib/libc/stdlib/strtoul.c
rg -n '(_ctype_|isspace|isalpha|isdigit|isupper)' lib/libc include
tools/bin/nm -u lib/libc_aout/libc/strtol.o \
    lib/libc_aout/libc/strtoul.o
tools/bin/size lib/libc_aout/libc/strtol.o \
    lib/libc_aout/libc/strtoul.o lib/libc_aout/libc/ctype_.o
```

The path in the first three commands is relative to a parent directory that
contains both checkouts. An absolute checkout path may replace
`../netbsd-src`. The object files use DiscoBSD a.out, so the tree's `nm` and
`size` commands own those measurements; GNU `arm-none-eabi-nm` and
`arm-none-eabi-size` expect ELF and reject the files.

## The composable NetBSD corrections

| NetBSD commit | Mechanism adopted here |
| --- | --- |
| `72b2a18374adc60654e2ba1ddd899bdfa7cf0483` | Load through `unsigned char`; changing only the destination type still allows sign extension before assignment. |
| `5c134b071ceac1c6bdf11e33a83d584ae697dc2e` | Validate base 0 or 2 through 36 before cutoff division and use modern function definitions. |
| `4c7fde108a18ed14aeb1079a08887ab0ce321193` | Set `endptr` to the original input when the base is unsupported. |
| `23cadd9b7867fa0f0d14893be2009a63cfd2d143` | Classify only portable-set digits directly, avoiding locale-dependent alphabetic characters and ctype indirection. |
| `fdd15b80d9f205963a5fb00e8927de18491ef10a` | Consume `0x` only when a hexadecimal digit follows, leaving zero as the largest valid prefix of `0x` and `0xg`. |

The signed implementation adds one constrained-C refinement. It accumulates
an unsigned magnitude bounded by either `LONG_MAX` or the magnitude of
`LONG_MIN`. The return path handles `LONG_MIN` explicitly and converts every
other magnitude only after proving that `long` represents it. The old
`acc = -acc` path converted the unsigned magnitude of `LONG_MIN` to signed
before negation, which depends on implementation-defined behavior.

The gate compiles the exact tree sources as strict C17 with `-fsigned-char`,
at host and ILP32 widths. A ctype shim rejects arguments outside `EOF` or
`0..UCHAR_MAX`; the pre-change sources fail that oracle. Deterministic cases
compare the local functions with explicit values. Cases that perform a
conversion also compare against the host libc. Sign-only inputs retain the
explicit oracle because macOS libc chooses a different no-conversion end
pointer. Invalid-base and prefix cases also retain explicit oracles because
host extensions do not define the DiscoBSD contract.

Calibration against the pre-change functions failed 7 of 51 focused checks:
both signed-byte ctype calls, both missing `EINVAL` reports, the `strtoul()`
invalid-base input position, and both incomplete hexadecimal prefix positions.
The adopted functions pass all 51 checks at each width.

## Bounded syslog formatting

NetBSD commits `e0ecee6373cfb4b1f55d2590d28c23960d9d3488` and
`ed4b77539f5bd502b52d39170f36631b6e38ee3f` establish the governing
invariant: every fixed field, expanded `%m`, formatted body, and final line
ending must share one explicit bound, and a formatter's required length must
never advance a pointer beyond the bytes actually stored. The DiscoBSD
adaptation keeps that invariant while avoiding NetBSD's separate format-copy
array. A 512-byte record reserves its final two payload bytes for CRLF, and a
bounded string `FILE` sends the caller's format directly through `_doprnt()`.
The stream carries the saved error string in `_base` and marks the private
contract with `_IOSYSLOG`. `_doprnt()` expands `%m` only for that stream;
ordinary `snprintf("%m")` retains the historical literal `%m` result, and a
percent character inside `strerror()` remains data rather than a second
format string.

```sh
git -C ../netbsd-src show --stat e0ecee6373cf ed4b77539f5
rg -n 'sprintf|vsprintf|strcat|tbuf|fmt_cpy' lib/libc/gen/syslog.c
```

The `%m` check lives in `_doprnt()`'s existing unknown-conversion arm. A new
top-level `case 'm'` widened GCC's Thumb-1 switch dispatch and made
`doprnt.o` 184 bytes larger. Reusing the default arm limits that increase to
92 bytes. `snprintf()` and `vsnprintf()` each own their small bounded stream
setup rather than forcing `snprintf.o` to pull `vsnprintf.o` from a static
archive. The duplication costs source lines but preserves member-level
selection for programs that use only one interface. Both functions now
reserve the terminator, report the required length after truncation, accept
`(NULL, 0)`, and reject a capacity that the signed stream count cannot
represent. `_flsbuf()` recognizes an exhausted string stream and leaves its
pointer at the terminator slot.

The pre-change syslog shim prints literal `%m` and then triggers the host
stack protector on a long tag or message. The adopted gate compiles the exact
tree sources as strict C17 and checks fixed fields, maximum target PID, CRLF,
`LOG_PERROR`, repeated and escaped `%m`, a percent character in the error
text, long tag, literal format, argument and error-text truncation, and retry
after a failed logfile open. The companion
formatter gate runs at host and ILP32 widths and checks exact fit, truncation,
size one, `(NULL, 0)`, destination guards, required-length returns, and the
unrepresentable-capacity rejection.

Target `-fstack-usage` reports a 608-byte `vsyslog()` frame, down from 1,216
bytes because the 512-byte format-copy array is gone. The board library keeps
143 members and grows from 52,020 to 52,116 bytes. The linked programs below
measure the complete ARM a.out result; window delta is
`a_text + a_data + a_bss`, not file length.

| Program | Baseline text/data/bss | Adopted text/data/bss | Window delta |
| --- | ---: | ---: | ---: |
| `date` | 12968 / 1100 / 2344 | 13048 / 1080 / 2340 | +56 |
| `getty` | 16024 / 2008 / 5280 | 16120 / 1988 / 5276 | +72 |
| `init` | 15276 / 608 / 2844 | 15376 / 588 / 2840 | +76 |
| `reboot` | 13956 / 528 / 2676 | 14060 / 508 / 2672 | +80 |
| `shutdown` | 16016 / 944 / 4496 | 16120 / 924 / 4492 | +80 |
| `login` | 21484 / 889 / 5399 | 21568 / 869 / 5395 | +60 |
| `su` | 13928 / 1016 / 3784 | 14012 / 996 / 3780 | +60 |

The formatter safety therefore costs 56 to 80 process-window bytes in the
seven measured consumers while reclaiming 608 bytes of peak `vsyslog()`
stack. The initialized-data reduction comes from removing the mutable logfile
path and redundant connection state; a failed open remains represented by
`LogFile == -1`, which also makes the next call retry without another flag.

## Fixed-block cat I/O

NetBSD commits `75b95f389418367440fe5a480bdc00ebbc79060b`,
`9efe55fa450c8b18848289880417188c41e05758` and
`54817e25fc331082c64c35ce80695b65f7c15a82` repair zero, signed and
allocation-fallback cases in `raw_cat()`. Commits
`273bf08b7182e95d78e30c97a14b12a365cf4804`,
`8ba3eae714939b0271b57fd0b51b29ef0f06a54d` and
`05619d690de0699913a71835cbf9a5d9df021b59` then limit allocation to a
profitable size and reject invalid standard descriptors. NetBSD must preserve
dynamic sizing because its output block size varies. DiscoBSD has a stronger
invariant: `sys_inode.c` assigns `MAXBSIZE` to every inode's `st_blksize`, while
`MAXBSIZE`, RP2040 `DEV_BSIZE` and libc `BUFSIZ` are all 1,024 bytes.

The target adaptation turns that invariant into two static assertions and one
automatic `BUFSIZ` transfer buffer. Removing `ibsize`, `obsize`, `malloc()`
and `free()` deletes the zero-size and signed-narrowing paths rather than
adding recovery code for target states the kernel cannot produce. `size_t`
tracks offsets and remaining lengths; `ssize_t` carries `read()` and `write()`
results. Every positive read drains through complete short writes, and a zero
write establishes `EIO` and returns an output error rather than spinning.
`fastcat()` returns read and write failures to `main()` so the multicall applet
unwinds normally rather than calling `exit()` from a helper.

The option and line state lives in an 8-byte caller-owned object. Bit flags
replace nine mutable boolean globals, the unused `col` disappears, and modern
function definitions let `bin/cat` move from the legacy warning set to
`-Wall -Wextra -Werror`. The caller-owned state also makes repeated in-process
entry possible without inheriting a preceding invocation's line state.

The host shim forces the target's 1,024-byte transfer size and injects empty
input, 17-byte short writes, a read failure after valid data, invalid input and
output descriptors, a negative write and a zero write. A host executable pins
raw multi-file output, `-n`, `-b`, `-s`, `-e`, `-t`, `-v`, `-u` and self-output
refusal. The pre-change source fails the transfer-size and invalid-output
checks, then terminates the harness with status 2 on the write failure. The
adopted source passes the host suite, and the cross gate compiles the exact
source as strict C17 for Cortex-M0+ so the three size assertions use target
headers.

The linked ARM a.out measurements include all selected libc members. The
automatic buffer replaces a 1,024-byte heap request with a 1,024-byte stack
object; the static stack reports therefore describe the transient trade.

| Artifact | Baseline text/data/bss | Adopted text/data/bss | Loaded delta |
| --- | ---: | ---: | ---: |
| standalone `cat` | 8668 / 172 / 176 | 8600 / 172 / 124 | -120 |
| multicall `/bin/box` | 33296 / 1524 / 8776 | 33228 / 1524 / 8776 | -68 |
| `cat.o` | 1317 / 0 / 52 | 1250 / 0 / 0 | -119 |

`fastcat()`'s static frame rises from 32 to 1,056 bytes and `main()` rises
from 96 to 112 bytes. Conservative peak accounting adds 16 transient bytes
after replacing the old 1,024-byte allocation, before allocator metadata.
The standalone raw-copy process window therefore falls by at least 104 bytes;
the multicall window falls by at least 52 bytes because another applet already
sets the BSS overlay maximum. The raw path also stops linking allocator calls.

## Component-wise rmdir removal

NetBSD commit `142e676b8ef2c4630cd31cf659491395f2ec09b3` brought the
4.4BSD-Lite `rmdir -p` traversal into the command. Commit
`d350904dd87bc862ce50318c8a1b39e8e0b507c7` supplies the essential root
guard: after truncating a top-level operand such as `/leaf` at its separator,
the traversal stops before passing an empty pathname to `rmdir(2)`. The local
adaptation retains DiscoBSD's diagnostic style and adds only `-p`; NetBSD's
unrelated `-v` interface remains outside the selected feature.

The option parser walks `-p` groups and `--` directly. `rmdir` therefore does
not pull the general `getopt()` member into a standalone image. One removal
loop handles the operand and every selected parent, so the error path and
syscall occur once in the object. Parent lookup rescans the mutable operand
for its last slash rather than linking `strrchr()`. The scan also collapses
separator runs at component boundaries. The RP2040 name lookup rejects a
trailing empty DELETE component, while repeated leading separators still name
the root; normalizing those two cases in userland keeps `-p` consistent with
the target kernel.

The fixed usage line uses `fputs()` rather than the historical `fprintf()`.
The standalone image consequently stops linking the formatted-output engine,
which outweighs the feature's own code. The target object grows from 117 to
269 text bytes, while the complete standalone process shrinks by 3,808 loaded
bytes. Complete ARM a.out builds with the same source head and toolchain
measure the effect:

| Artifact | Baseline text/data/bss | Adopted text/data/bss | Loaded delta |
| --- | ---: | ---: | ---: |
| standalone `rmdir` | 6752 / 172 / 124 | 2944 / 172 / 124 | -3808 |
| multicall `/bin/box` | 33228 / 1524 / 8776 | 33380 / 1524 / 8776 | +152 |

The filesystem gate removes complete relative chains and operands with
trailing slashes. It also pins partial success, an initially non-empty leaf,
continued processing after a failed operand, `--`, missing operands and
unknown options. The exact-source shim also supplies an empty argument vector,
records every syscall path for a trailing-separator chain, and requires
top-level operands with one or several leading separators to stop before the
root. Those cases catch target-specific traversal failures independently of
host filesystem behavior. The pre-change binary fails the complete-chain
case, reports `-p` as a directory and leaves the parents; the adopted host and
strict C17 Cortex-M0+ gates pass.

```sh
git -C ../netbsd-src show 142e676b8ef:bin/rmdir/rmdir.c
git -C ../netbsd-src show d350904dd87 -- bin/rmdir/rmdir.c
bmake MACHINE=rp2040 check-rmdir-contracts check-rmdir-contracts-cross
```

## Remaining high-value frontier

| Priority | Surface | NetBSD evidence | Local finding | Required proof before adoption |
| --- | --- | --- | --- | --- |
| 1 | bounded `vis(3)` | Modern NetBSD separates buffer-length contracts from historical `vis()` | DiscoBSD carries the older API surveyed in `bsd44-backport.md`. | Define whether compatibility or a bounded new interface owns the ABI before copying implementation details. |

Two tempting searches produced no applicable repair. NetBSD's `LIST_MOVE`
hardening cannot backport as a line because DiscoBSD's queue header has no
`LIST_MOVE` macro or caller. NetBSD's tty `INT_MIN` process-group correction
protects a negation absent from DiscoBSD's process-group path. Source similarity
without the triggering operation is not a backport candidate.

The following commands reproduce the remaining frontier:

```sh
rg -n 'LIST_MOVE|pgrp|INT_MIN|vis\(' sys bin lib include
```
