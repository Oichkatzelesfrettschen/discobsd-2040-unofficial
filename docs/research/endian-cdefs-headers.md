# Byte-order and cdefs headers: audit, falsification and remediation

An audit of the tree at eb5b3081 claimed that the port lacks
`<sys/endian.h>`, `<machine/endian.h>`, every BSD byte-order constant and
every `<sys/cdefs.h>` annotation except `__unused`, that the gap silently
miscompiles portable `#if BYTE_ORDER == BIG_ENDIAN` code on the
little-endian Cortex-M0+, and that the function-call `htonl(3)` family costs
measurable cycles. This note records which claims the tree bore out, which
it refuted, and what the remediation changed.

## Claims the tree bore out

- No `endian.h` existed anywhere outside `.git` and `legacy/`.
- `sys/sys/cdefs.h` defined `__unused` alone.
- Byte order lived only in the rp2040 and stm32 `machparam.h` as the
  un-prefixed `LITTLE`, `BIG`, `PDP` and `ENDIAN` names.
- The `__packed` exclusion comment in `cdefs.h` was over-cautious:
  `sys/arch/arm/include/core_cm4.h` defines `__packed` empty only under
  `__CSMC__`, and `sys/arch/stm32/hal/stm32f4xx_hal_def.h` guards its
  definition with `#ifndef __packed`, so a guarded definition in `cdefs.h`
  conflicts with neither under GCC or clang.
- `usr.bin/dc/compat.h`, `usr.bin/textbox/compat.h` and
  `usr.sbin/cron/compat.h` each carried a private shim for one missing
  `cdefs.h` macro.
- `include/arpa/inet.h` declared `htonl`, `htons`, `ntohl` and `ntohs` as
  `unsigned` functions, implemented in `lib/libc/arm/gen/htonl.S` and
  `htons.S`.

## Claims the tree refuted

- The silent-miscompile risk had no instance. Every `BYTE_ORDER` test
  outside `legacy/` sits in the NetBSD v7fs import, and those sources
  compile only with `-include usr.sbin/v7fs/netbsd_compat.h`, which defines
  the constants from the compiler's `__BYTE_ORDER__` predefines;
  `sys/fs/v7fs/v7fs_endian.c` also stops with `#error` when `BYTE_ORDER` is
  undefined. The risk is a class, and `-Wundef`, absent from every warning
  set in the tree, is the diagnostic that names the class.
- The conversion-overhead argument carried no consequence: `htonl` is
  `rev; bx lr`, `htons` is `rev16; uxth; bx lr`, and the only caller outside
  libc is `games/hunt`, which the distribution does not build.
- The audit's remediation sketch failed the build as written. Its unguarded
  `__P` and attribute-based `__dead` were redefinitions of cron's
  `#define __P(x) x` and dc's `#define __dead _Noreturn`, which C treats as
  errors when the parameter names or bodies differ. Its feature-macro guard
  on the un-prefixed names was dead code, since the tree's only
  feature-macro consumer is `include/math.h` testing `_POSIX_SOURCE`. Its
  defect table listed the 64-bit conversions and its header omitted them.
- The audit counted three shims and missed `usr.sbin/v7fs/netbsd_compat.h`,
  the largest one and the only one supplying byte-order constants. That
  shim is the requirements list: the kernel-side v7fs sources under
  `sys/fs/v7fs` use `__packed`, `__BEGIN_DECLS`, `__predict_false`,
  `__KERNEL_RCSID` and `BYTE_ORDER`, appear in no kernel config file, and
  will need every one of those from the kernel's own `cdefs.h` and
  `endian.h` on the first kernel build that lists them.

## What changed

- `sys/sys/endian.h` defines `_LITTLE_ENDIAN`, `_BIG_ENDIAN` and
  `_PDP_ENDIAN` with the BSD and compiler-predefine values, takes
  `_BYTE_ORDER` from `<machine/endian.h>`, exports the un-prefixed names,
  and defines the 16-, 32- and 64-bit `htobe`/`htole`/`be*toh`/`le*toh`
  conversions, their OpenBSD `betoh`/`letoh` spellings and the NetBSD
  `bswap` spellings as macros over `__builtin_bswap*`. The macros are
  spelled with `unsigned short`, `unsigned int` and `unsigned long long`
  rather than the `stdint` typedefs, so the header declares no type: an
  assembler source reaches it through `<machine/machparam.h>`, and the host
  harnesses under `tests/` (`tests/rp2040/clock_catchup` stages it beside
  glibc's `<stdlib.h>`) meet no second set of typedefs. The four public
  byte-order names, the twelve conversions and the alias families are each
  guarded with `#ifndef`, because glibc's `<endian.h>` defines the names
  and the conversions with the same values and its own tokens, and a
  redefinition with different tokens is an error under `-Werror`.
- `sys/arch/rp2040/include/endian.h` and `sys/arch/stm32/include/endian.h`
  select `_BYTE_ORDER _LITTLE_ENDIAN`; both `machparam.h` files include
  `<machine/endian.h>` after the legacy names, which stay.
- `sys/sys/cdefs.h` defines `__BEGIN_DECLS`, `__END_DECLS`, `__P`,
  `__CONCAT`, `__STRING`, `__GNUC_PREREQ__`, `__dead` (as `_Noreturn` under
  C11 and later), `__dead2`, `__unused`, `__used`, `__pure`, `__pure2`,
  `__constfunc`, `__packed`, `__aligned`, `__section`, `__printflike`,
  `__scanflike`, `__format_arg`, `__nonnull`, `__predict_true`,
  `__predict_false`, `__RCSID`, `__KERNEL_RCSID` and `__COPYRIGHT`, every
  one guarded by `#ifndef`.
- `usr.sbin/cron/compat.h` and `usr.bin/dc/compat.h` include
  `<sys/cdefs.h>` in place of their private `__P` and `__dead`.
  `usr.bin/m4/stdd.h` carried a fourth shim the audit also missed,
  `#define __P(protos) ()`, which emptied every prototype in m4's
  `extern.h`; with real prototypes in force the K&R definition of
  `xstrdup` in `usr.bin/m4/misc.c` had to take `const char *` as its
  prototype declares.
  `usr.sbin/v7fs/netbsd_compat.h` stays, because `tests/v7fs_core` builds
  the same sources against glibc and macOS headers; its `#ifndef` guards
  yield to the tree's definitions.
- `include/arpa/inet.h` includes `<sys/endian.h>` and maps the four
  conversion names onto its macros after their declarations, so a call
  folds or compiles to one instruction and the libc functions remain for a
  caller that takes their address.
- `-Wundef` joins `-Wall -Wextra -Werror` in `share/mk/warnings.mk` at the
  full level and in the RP2040 kernel template and both tracked kernel
  Makefiles. `tests/warning_policy/check.py` requires every full cross
  route and both kernels to reject an unshimmed `BYTE_ORDER == BIG_ENDIAN`
  with the compiler's "is not defined, evaluates to" diagnostic and to
  compile the same comparison to an object with `<sys/endian.h>` in scope
  and `#error` in the big-endian branch; `sys/arch/rp2040/doc/TESTING.md`
  carries the gate's claim.

## What -Wundef found in the tree

A non-fatal sweep of the whole RP2040 build (`WARNERR` and `CWARNFLAGS`
with `-Wundef -Wno-error`, GCC 16.2.0) raised the diagnostic in 24 files,
none of them a byte-order test. The `#if` sites and their disposition:

| Site | Identifier | Disposition |
| --- | --- | --- |
| `sys/arch/rp2040/rp2040/sysctl.c` | `NTMSCP` | source now tests `defined(NTMSCP) && NTMSCP > 0` |
| `sys/fs/v7fs/*.c`, `sbin/newfs_v7fs/main.c` | `HAVE_NBTOOL_CONFIG_H` | hash-pinned NetBSD imports; `usr.sbin/v7fs/Makefile` defines it 0 |
| `games/pzcommon/tatham/puzzles.h` | `HAVE_STDINT_H` | vendored; every puzzle Makefile defines it 1, which is the fact |
| `usr.bin/unixbench/src/big.c`, `spawn.c` | `debug` | vendored benchmark; `-Wno-undef` in its Makefile |
| `benchmarks/coremark/core_*.c` | `CORE_DEBUG` and three others | vendored benchmark; `-Wno-undef` in its Makefile |
| `usr.bin/tip/acutab.c` | five dialer names | legacy level, `-Werror` alone, so not fatal; open |
| `usr.bin/fstat/fstat.c` | `HAVE_NETWORK` | legacy level; open |
| `usr.bin/hostid/hostid.c` | `HAVE_NET` | legacy level; open |

Every open site is on a route `share/mk/warnings.mk` holds to `-Werror`
alone, and each is a feature switch that is correctly absent, so the
undefined-is-zero reading matches the intent there. They close when those
directories move to the full level.
