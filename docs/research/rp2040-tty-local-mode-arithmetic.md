# RP2040 tty local-mode unsigned arithmetic

Date: 2026-10-23
Baseline revision: `de585a0df2d9d6f8bae7010f164ebd397a7741d5`

## Finding and contract

`ttioctl()` formerly evaluated each `TIOCLBIS`, `TIOCLBIC`, and `TIOCLSET`
operand as `(long)*(u_int *)data << 16`. On the RP2040 ILP32 target, local-mode
value `0x8000` produces a signed result that `long` cannot represent. An
undefined-behavior sanitizer build of the inherited expression reports
`left shift of 32768 by 16 places cannot be represented in type 'long int'`.
`TIOCLGET` formerly right-shifted the potentially negative signed
`struct tty::t_flags`, so the returned value depended on the implementation's
signed right-shift rule.

The accepted arithmetic contract uses one 32-bit unsigned representation:

- `struct tty::t_flags` stores ordinary tty flags in bits 0 through 15 and
  local-mode flags in bits 16 through 31.
- `TIOCLBIS` sets the selected local-mode bits and preserves every other bit.
- `TIOCLBIC` clears the selected local-mode bits and preserves every other bit.
- `TIOCLSET` replaces bits 16 through 31 and preserves bits 0 through 15.
- `TIOCLGET` returns bits 16 through 31 as a zero-extended 16-bit value.
- Only the low 16 bits of each setter payload select local-mode bits.

Four static helpers in `sys/kern/tty.c` express those operations in
`uint32_t`. `ttioctl()` calls those exact helpers after the syscall boundary
has staged the public four-byte `int` payload in typed, aligned kernel storage.
The provider reads or writes that declared effective type and converts
explicitly at the unsigned arithmetic boundary. The tty input and read
snapshots use the same unsigned representation, and `pstat` converts the field
to `unsigned long` for its existing octal presentation.

## ABI and layout

The type change preserves the RP2040 binary layout. A strict-C17 Cortex-M0+
translation unit asserts these measured values:

| Surface | Accepted value |
|---|---:|
| `sizeof(struct tty)` | 120 bytes |
| `_Alignof(struct tty)` | 4 bytes |
| `offsetof(struct tty, t_flags)` | 60 bytes |
| `sizeof(t_flags)` | 4 bytes |
| `offsetof(struct tty, t_state)` | 64 bytes |
| `offsetof(struct tty, t_pgrp)` | 68 bytes |

The `TIOCL*` command numbers, encoded payload widths, syscall number, provider
dispatch, structure offsets, and `pstat` display format remain unchanged.

## Deciding evidence

`tests/kernel/tty_local_mode_extract.py` requires one exact definition of each
production helper and one exact call from each `ttioctl()` branch. The native
and ILP32 strict-C17 executables check `TIOCLBIS`, `TIOCLBIC`, `TIOCLSET`, and
`TIOCLGET` for all 65,536 local-mode values. Three extra assertions verify
that high payload bits cannot select flags outside the local-mode word. Each
run therefore executes 262,147 assertions.

Six calibrated mutations lose local-mode bit 15, substitute the wrong set or
clear operation, preserve the old high word during `TIOCLSET`, restore signed
right shift, or restore the inherited signed left shift. The five semantic
mutations exit through their named assertion. The signed-left-shift mutation
compiles at ILP32 with undefined-behavior sanitization and must emit the
`left shift of 32768` diagnostic. Its execution inventory row records either
the accepted rejection or an explicit unrun state when the ILP32 sanitizer is
unavailable. The mutation oracle accepts the supported rejection statuses and
the exact diagnostic while rejecting success, status 2, and an unrelated
diagnostic. The unmodified helper set also runs under native address and
undefined-behavior sanitizers.

The Cortex-M0+ layout gate compiles the 120-byte contract as its known-good
control, then substitutes a 121-byte expected size and requires the compiler
to reject the named structure-size assertion. A generated dependency file
keeps every consumed header inside the positive object's freshness boundary.

`check-kernel`, `check-kernel-ilp32`, and `check-tty-layout-cross` pass with the
new gates. Clean PICO and PICO_UART kernel builds compile every `tty.h` kernel
consumer with GNU17 and warnings as errors. A clean target compile of
`usr.sbin/pstat/pstat.c` also passes; each clean distribution also links the
complete program against the target C runtime archive.

## Static resources and dependency closure

Clean schema-2 distributions compare parent
`de585a0df2d9d6f8bae7010f164ebd397a7741d5` with the candidate source tree.
Both captures use bmake 20260912, arm-none-eabi-gcc 16.2.0, and GNU ld
2.47.20260726. Their dependency closures account for 173 PICO C/header paths,
165 PICO_UART paths, all 36 shipped artifact groups, every installed a.out,
the native startup object and libc archive, generated inputs, and the packed
root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,520 bytes | 105,456 bytes | -64 bytes |
| PICO_UART flash | 93,036 bytes | 92,980 bytes | -56 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| `tty.o` text / data / BSS | 6,719 / 16 / 0 bytes | 6,659 / 16 / 0 bytes | -60 / 0 / 0 bytes |
| `ttioctl()` symbol / static frame | 1,332 / 56 bytes | 1,328 / 56 bytes | -4 / 0 bytes |
| `pstat` text / data / BSS | 13,552 / 316 / 96 bytes | 13,552 / 316 / 96 bytes | 0 / 0 / 0 |
| `pstat` raw / packed bytes | 13,900 / 11,548 bytes | 13,900 / 11,548 bytes | 0 / 0 bytes |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement, native-artifact size, archive selection, and
archive-member set is unchanged. The largest shipped loaded text/data/BSS
occupancy remains 75,412 bytes for `/usr/libexec/smlrc`. The candidate retains
25,616 PICO and 38,092 PICO_UART bytes below the 128 KiB kernel limit. The
default resource comparer accepts the two flash reductions and all remaining
zero deltas. Runtime stack, heap, and storage traffic remain unmeasured.

The retained parent evidence records have these SHA-256 identities:

- parent source snapshot: `da818f463fada0dcec494e267a7c0234fb49fdb7ff4bea421f2c83116557dc2f`
- parent dependency closure: `cca6262fc84dc93cdf6fcf86f946ba9df2d971ef918c180e504c5049e93afbf9`
- parent schema-2 resources: `38a1bf20ce5517fa6595c95fc04f26a5fe065525df68ad229588ec0db5c20f46`
- parent object and frame: `a4004d2196a9155020c947ea227256bd4c9dfb87021dfa12d834fe44907f8dbe`

The candidate receipt identities and aggregate `logged-check` state remain open
until a clean capture binds them to the implementation commit.

## Evidence boundaries and residuals

The host gates establish the exhaustive unsigned arithmetic, mutation
rejection, sanitizer, and layout contracts. The two target builds establish
exact Cortex-M0+ compilation and kernel linkage. These checks do not execute
the tty provider on physical silicon, measure a runtime stack or heap peak, or
establish terminal-driver timing. Those evidence classes remain separate.

`sys/kern/tty.c`, `sys/sys/tty.h`, and `usr.sbin/pstat/pstat.c` retain
independent migration units for their remaining old-style definitions,
declarations, casts, and complete translation-unit semantics. The focused
local-mode seam is accepted without claiming those files complete.
