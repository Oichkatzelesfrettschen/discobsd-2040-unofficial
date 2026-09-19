# NetBSD 11 micro-backports for a C17 DiscoBSD process window

The first adopted NetBSD 11 backport repairs `strtol()` and `strtoul()` as
one bounded mechanism. Five small historical changes compose into a C17
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

## Remaining high-value frontier

| Priority | Surface | NetBSD evidence | Local finding | Required proof before adoption |
| --- | --- | --- | --- | --- |
| 1 | `lib/libc/gen/syslog.c` | `e0ecee6373cfb4b1f55d2590d28c23960d9d3488` bounds fixed buffers | Local `sprintf()`, `%m` expansion, `vsprintf()`, and `strcat()` write into 512- and 640-byte arrays. Six shipped programs link `syslog.o`. | A formatter shim must exercise long tag, PID, format, `%m`, and CRLF truncation independently, then an a.out link must measure the code and stack costs. |
| 2 | `bin/cat/cat.c` | `75b95f389418367440fe5a480bdc00ebbc79060b`, `9efe55fa450c8b18848289880417188c41e05758`, `54817e25fc331082c64c35ce80695b65f7c15a82` | Local `fastcat()` narrows `st_blksize` into signed `int` and accepts zero as a `malloc()` and `read()` size. | An `fstat()`/`malloc()`/`read()`/`write()` shim must prove zero, negative, oversized, short-write, empty-file, and read-error paths before a C17 refactor. |
| 3 | `rmdir -p` | NetBSD has component-wise removal with defined diagnostics | Local `rmdir` has no `-p`; the change adds a user interface rather than repairing an existing contract. | Choose the feature explicitly, then add filesystem integration cases for partial removal and preserved failure paths. |
| 4 | bounded `vis(3)` | Modern NetBSD separates buffer-length contracts from historical `vis()` | DiscoBSD carries the older API surveyed in `bsd44-backport.md`. | Define whether compatibility or a bounded new interface owns the ABI before copying implementation details. |

Two tempting searches produced no applicable repair. NetBSD's `LIST_MOVE`
hardening cannot backport as a line because DiscoBSD's queue header has no
`LIST_MOVE` macro or caller. NetBSD's tty `INT_MIN` process-group correction
protects a negation absent from DiscoBSD's process-group path. Source similarity
without the triggering operation is not a backport candidate.

The following commands reproduce the remaining frontier:

```sh
git -C ../netbsd-src show --stat 75b95f389418 9efe55fa450c 54817e25fc33
git -C ../netbsd-src show --stat e0ecee6373cf
rg -n 'st_blksize|malloc\(|read\(|write\(' bin/cat/cat.c
rg -n 'sprintf|vsprintf|strcat|tbuf|fmt_cpy' lib/libc/gen/syslog.c
rg -n 'LIST_MOVE|pgrp|INT_MIN|rmdir|vis\(' sys bin lib include
```

The `syslog()` overflow surface has the highest consequence and the larger
proof burden. The `cat` repair has the smaller implementation surface. Each
belongs on a separate branch so its gate can be calibrated against its own
known-bad source and its target footprint can be judged independently.
