# Static analysis of the DiscoBSD RP2040 port

Worktree: a worktree, branch `static-analysis`, forked from
`rp2040-port` at `a3c7d3e`. Board not touched; every result below comes from the host
cross-build, host-side analyzers, and one host-executable reproduction harness.

## Tool versions

| Tool | Version |
|---|---|
| arm-none-eabi-gcc | 16.2.0 (Arch Repository) |
| cppcheck | 2.21.1 |
| clang / clang-tidy | 22.1.8 |
| semgrep | 1.164.0 |
| weggli | 0.2.4 |
| sparse | v0.6.5-rc1 |
| smatch | 0.6.4 |
| lizard | 1.24.0 |
| bmake | base (Arch `bmake` package) |

## Setup

```sh
git worktree add \
    ../static-analysis -b static-analysis rp2040-port
cd ../static-analysis
```

`tools/config`'s man page install calls `mandoc`, absent on this host; BOOT-MAP.md notes
groff as this host's stand-in, so a `mandoc` shim (`exec groff -mandoc "$@"`) was placed
first in `PATH` for the tool build only. It touches no tracked file.

```sh
bmake -C tools MACHINE=rp2040 install
bmake MACHINE=rp2040 kernel
```

Baseline kernel build is clean: `90900` text, `39608` bss, zero warnings except the
one pre-existing `exec_subr.c:90: 'nc' set but not used`, unrelated to this port and
unchanged by anything below. `bmake -n` gave the exact compiler invocation, reused for
every analyzer:

```
arm-none-eabi-gcc -g -Wall -std=gnu17 -ffreestanding -fno-builtin -fcommon
  -mfloat-abi=soft -mthumb -mabi=aapcs -mlittle-endian -mcpu=cortex-m0plus -O
  -nostdinc -I. -I<tree>/sys -I<tree>/sys/arch -I<tree>/sys/arch/arm/include
  -I<tree>/sys/arch/rp2040/dhara/compat
  -DPICO -DRP2040 -DUARTUSB_ENABLED -DUART_ENABLED -DUART_BAUD=115200
  -DCONS_MINOR=0 -DCONS_MAJOR=UARTUSB_MAJOR -DCORE_DEFAULT=0 -DNFL=2
  -DHALTREBOOT -DBUS_KHZ=125000 -DCPU_KHZ=125000 -DKERNEL
```

clang/clang-tidy used the equivalent `--target=arm-none-eabi -mcpu=cortex-m0plus
-mthumb -mfloat-abi=soft -mabi=aapcs -mlittle-endian` plus the same `-I`/`-D` set, so
`sizeof`/alignment match the target.

## Commands run per tool

```sh
# cppcheck, whole priority scope
cppcheck --enable=warning,portability,performance --inconclusive --std=c17 \
  -DKERNEL -DPICO -DRP2040 --suppress=missingIncludeSystem \
  -I include -I sys -I sys/arch -I sys/arch/arm/include \
  -I sys/arch/rp2040/dhara/compat -I sys/arch/rp2040/compile/PICO \
  sys/arch/rp2040 sys/kern lib/libc/arm lib/libc/gen/modf.c lib/libc/gen/modff.c \
  lib/libc/gen/setjmp.S lib/libc/stdio tools/elf2aout tools/flashimg tools/fsutil \
  usr.bin/smlrc/cgthumb.c

# clang --analyze, per file, rp2040 arch tree + named kern/ files
clang --analyze --target=arm-none-eabi -mcpu=cortex-m0plus -mthumb \
  -mfloat-abi=soft -mabi=aapcs -mlittle-endian <CROSS_INC> <DEFS> \
  -ffreestanding -fno-builtin -fcommon -std=gnu17 <file>.c -o /dev/null

# gcc -fanalyzer, same file set
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -mabi=aapcs \
  -mlittle-endian <CROSS_INC> <DEFS> -ffreestanding -fno-builtin -fcommon \
  -std=gnu17 -O -Wall -Wextra -fanalyzer -c <file>.c -o /dev/null

# sparse, rp2040 dev/ and rp2040/ (arch tree only -- see "not run")
sparse <same target/include/define flags as clang> -c <file>.c

# semgrep, custom rule for cast-then-deref through a wider pointer type
semgrep --config unaligned.yml sys/arch/rp2040 sys/kern lib/libc/arm lib/libc/gen \
  lib/libc/stdio tools/elf2aout tools/flashimg tools/fsutil usr.bin/smlrc/cgthumb.c

# weggli, structural search for pointer-cast dereferences
weggli '{*($t*)$p;}' sys/arch/rp2040
```

## Findings

| File:line | Tool | Class | Status | Commit |
|---|---|---|---|---|
| `tools/elf2aout/elf2aout.c:688` | cppcheck (`sizeofCalculation`) + read | `sizeof` of a pointer-arithmetic expression used as a copy bound | **CONFIRMED** | `e2ad7dd` |
| `sys/arch/rp2040/rp2040/syscall.c:183` | manual read, prompted by the audit's "cast then deref" class | `*(int *)u.u_code` reads the SVC halfword as an `int`; `u.u_code` is not guaranteed 4-byte aligned (icode1's `svc #11` sits at offset 0xa from a 4-byte-aligned base, address 2 mod 4 once copied to `__user_data_start`) | **PLAUSIBLE, disproven at current -O** | not fixed |
| `sys/arch/rp2040/rp2040/syscall.c:185` | gcc `-Wclobbered` | `callp` local crosses a `setjmp`/`longjmp` pair without `volatile` | **PLAUSIBLE, disproven by control flow** | not fixed |
| `sys/arch/rp2040/dev/usb.c`, `dev/uart.c`, `dev/flash.c` spl/splx pairs | manual read (all `spl*`/`splx` call sites) | spl raised without matching splx on some return path | checked, clean | -- |
| `sys/arch/rp2040/dev/flash.c` `flash_erase`/`flash_program`/`flash_enter_xip` | manual read + disassembly | `__ramfunc` calling a non-ramfunc while XIP is down | checked, clean (see below) | -- |
| `sys/arch/rp2040/dev/flash.c:591-628` `b_bcount` handling | manual read | buffer length rounded up past what was requested | checked, clean, already fixed with an explaining comment | -- |
| `lib/libc/arm/gen/{_,}setjmp.S` vs `sys/arch/rp2040/include/setjmp.h` | manual read | jmp_buf smaller than what the arch's setjmp saves | checked, clean: `_JBLEN 12` = 2 header words + 10 saved registers (r4-r11, ip, lr) |
| `lib/libc/gen/modf.c` / `modff.c` | manual read | function alias with a differing prototype | checked, clean, already fixed (comment documents the original `modf`-as-alias-of-`modff` bug) |
| `sys/kern/kern_mman.c:31-33` (`brk`) / `lib/libc/arm/sys/sbrk.c` | manual read | `brk` growing into the stack | checked, clean, already fixed: kernel `brk()` rejects `p_daddr + newsize > p_saddr` |
| `lib/libc/stdio/doprnt_float.c`, `doprnt.c` | manual read | `cvt` buffer overrun (the historical ASCII-zeros-in-r6 bug) | checked, clean: `MAXNBUF` sizes for `DBL_MAX_10_EXP`/`DBL_DIG` and `dwidth` is clamped to `DBL_DIG` before use |
| `sys/kern/kern_fork.c` (many lines) | cppcheck (`nullPointerRedundantCheck`), clang --analyze (`core.NullDereference`) | null deref after `if (child == NULL) panic(...)` | noise: `panic()` is not annotated `noreturn`, both tools assume it returns | -- |
| `sys/arch/rp2040/rp2040/machdep.c`, `usb.c`, `syscall.c`, `flash.c`, `clock.c`, `fault.c`, `uart.c` (23 sites) | clang --analyze `core.FixedAddressDereference` | MMIO register access through a fixed address | noise: every RP2040 register access looks like this by construction | -- |
| `sys/arch/rp2040/rp2040/usb.c:368`, `machdep.c:571` | gcc -fanalyzer `-Wanalyzer-infinite-loop` | infinite loop | noise: `usb_reset_to_flash` and `boot(..., !RB_HALT)` are deliberate spin loops after handing control to the reset interface / before a hardware reset | -- |
| `tools/fsutil/manifest.c`, `superblock.c`, `usb.c:899` | cppcheck `invalidPrintfArgType_*` | `%u`/`%ld` vs actual argument signedness | noise: host debug/host-tool prints, cosmetic, no memory-safety impact | -- |
| `sys/arch/rp2040/dev/*.c`, `rp2040/*.c` | sparse | "non size-preserving integer/pointer cast", "should be static" | rejected en masse, see "Not run" | -- |

### CONFIRMED: `tools/elf2aout/elf2aout.c:688`, truncated a.out symbol names

```c
strlcpy(nsp + 1, oldstrings + inbuf[i].st_name, sizeof(nsp - 1));
```

`nsp` is `char *`; `nsp - 1` is a pointer-arithmetic expression whose *type* is also
`char *`, so `sizeof(nsp - 1)` is `sizeof(char *)` -- 8 on this x86-64 host -- not the
number of bytes actually left in `newstrings`. Every symbol name longer than 7
characters is silently truncated by `strlcpy`'s own bound.

Host reproduction (the exact call, extracted into a standalone harness):

```
buggy   sizeof(nsp-1)=8 -> copied symbol: "_a_very_" (expected "_a_very_long_symbol_name_over_seven_chars")
```

Fix: bound the copy by the space actually left in `newstrings`
(`newstrings + newstringsize - (nsp + 1)`), which the surrounding allocation
(`newstringsize = strsize + remaining`) already guarantees is large enough for every
symbol. Same harness with the corrected bound copies the name intact. `elf2aout`
converts the kernel's and userland's ELF output to the a.out `unix`/program images
`nm` and `addr2line` read for BOOT-MAP.md section 10's fault-report workflow;
`exec_aout.c` never reads symbol names, so the kernel and userland this tool produces
run identically before and after the fix. Rebuilt (`bmake -C tools/elf2aout MACHINE=rp2040
install`) and the full `bmake MACHINE=rp2040 kernel` re-verified with zero new
warnings. Commit `e2ad7dd`.

### PLAUSIBLE, disproven at current optimization: `syscall.c:183` unaligned syscall-index read

`code = *(int *)u.u_code & 0377;` reads the trapping instruction as a 32-bit `int` to
extract the low byte (the SVC immediate). `u.u_code = tf_pc - INSN_SZ` is the address
of the `svc` instruction itself, and Thumb instructions are guaranteed only 2-byte
alignment, not 4-byte. Disassembly of `sys/arch/rp2040/rp2040/locore.S`'s `icode1`
(process 1's exec-to-init trampoline, copied byte for byte to the word-aligned
`__user_data_start`) confirms a concrete instance:

```
10000ed8 <icode1>:
...
10000ee2:	df0b      	svc	11
```

`0x10000ed8 mod 4 == 0` (icode1's `.align 2` entry) but `0x10000ee2 mod 4 == 2`: this
`svc`'s address is not 4-byte aligned. ARMv6-M (Cortex-M0+) has no unaligned-transfer
support, so a genuine 4-byte `ldr` at a 2-mod-4 address is architecturally a HardFault.

This looked CONFIRMED by disassembly and the architecture manual alone, so it was
implemented (`u_short` in place of `int`) and checked against the disassembly -- which
falsified it. At `-O`, GCC already narrows `*(int *)p & 0377` to a single-byte load
because only the low byte is ever read and the abstract machine gives it no reason to
assume a trap on the wider access:

```
10010618:	781b      	ldrb	r3, [r3, #0]
```

identical before and after the type change. `ldrb` has no alignment requirement, so
the actual code the compiler emits was never at risk; only the *source* mismatches
the instruction's real width. The change was reverted rather than committed, per
instructions not to fix PLAUSIBLE findings. It is still worth naming: a `-O0` debug
build, a different compiler, or a future edit that removes the `& 0377` mask would
remove the narrowing and reintroduce a real fault. Recorded here as the discriminator
future maintenance needs.

### PLAUSIBLE, disproven by control flow: `syscall.c:185` `-Wclobbered`

`const struct sysent *callp` is declared before `if (setjmp(&u.u_qsave) == 0) { (*callp->sy_call)(); }` and gcc -fanalyzer's `-Wclobbered` flags it as possibly clobbered by
the matching `longjmp`. `callp` is read only inside that `if`'s true branch (the
direct, non-longjmp return from `setjmp`); nothing after the following `switch`
reads it, and the `longjmp` path takes the `false` branch, skipping the read
entirely. GCC's warning is the standard conservative one for any local that is live
across a `setjmp` call, regardless of whether it is used on the resume path. No fix
applied.

## ramfunc / XIP check (confirmed clean by disassembly)

`sys/arch/rp2040/dev/flash.c`'s `flash_erase`/`flash_program` are `__ramfunc`
(`noinline, section(".ramfunc")`) and must not call into flash while XIP is disabled.
The `spl*`/`splx` pair around each is `static inline arm_intr_disable`/`arm_intr_restore`
(`sys/arch/rp2040/include/intr.h`), not itself `__ramfunc`. Disassembly of the linked
kernel confirms full inlining -- no `arm_intr_disable`/`arm_intr_restore` symbol exists
in the image, and every outbound call from the two ramfuncs is either a `blx` through
a boot-ROM function pointer (`flrom.*`, resolved once via `rom_func_lookup` and never
in flash) or a `bl` to `flash_enter_xip`, itself in `.ramfunc` at `0x20018160`:

```
2001816c <flash_erase>:
	mrs	r5, PRIMASK        ; arm_get_primask(), inlined
	cpsid	i
	isb	sy
	...
	blx	r3                 ; flrom.connect -- boot ROM, not flash
	...
	bl	20018160 <flash_enter_xip>   ; RAM to RAM
	msr	PRIMASK, r5
```

No defect. This class is checked and clean, evidenced by the disassembly rather than
inferred from source.

## Noise counts (rejected, not investigated further)

| Tool | Rejected findings | Why |
|---|---|---|
| cppcheck | 663 lines of output; ~12 real categories after dedup, of which 1 became the confirmed elf2aout fix, ~10 are `%u`/`%ld` signedness cosmetics in host tools, 1 is the `panic()`-not-`noreturn` null-deref chain in `kern_fork.c` | signedness warnings never touch memory safety; the null-deref chain requires annotating `panic` `noreturn`, out of scope for a one-defect-per-commit pass since it touches every machine's shared `sys/kern` tree, not just rp2040 |
| clang --analyze | 25 warnings across 10 files | 23 are `core.FixedAddressDereference` (every MMIO register access), 2 are `kern_fork.c` dead-store/null-deref noise from the same `panic()` gap |
| gcc -fanalyzer | 35 warnings | 9 unused-parameter (cdevsw entry point conventions), 2 intentional infinite loops (reset/halt spin), 1 `-Wclobbered` (described above), rest are host-tool signedness/type-limits cosmetics |
| sparse | ~300 lines across the rp2040 `dev/`+`rp2040/` subtree, 0 after filtering | every extern global declared in a shared BSD header ("should be static") plus every `u_int`-to-pointer cast to a fixed MMIO or boot-ROM address ("non size-preserving... cast") -- sparse defaults to a host pointer model it was not given a `-m32`/target flag to override, so it treats ARM's 32-bit `u_int == void *` as a width mismatch |
| semgrep custom rule (`unaligned-word-deref`) | 0 matches | the C grammar for a cast-plus-offset dereference pattern did not fire on this tree's style (`*(T *)(p + n)` is written here mostly through named macros like `MREG32`, not literal casts at the call site); superseded by the manual/weggli sweep below |
| weggli (`{*($t*)$p;}`) | 5 raw hits across `syscall.c` and `flash.c`, all read and triaged above (0 new confirmed, 1 already-covered PLAUSIBLE) | -- |

## What was not run, and why

- **smatch**: whole-program cross-function analysis needs its own build-capture
  (`smatch_scripts/build_kernel_data.sh` against the actual make invocation) that,
  for a ~130-file kernel with a nonstandard `bmake` build, costs more setup than the
  remaining scope justifies; sparse's noise on the same tree (host pointer-width
  mismatch, no target flag) predicts smatch would inherit the same false positives
  since it shares sparse's C frontend.
- **sparse/smatch on `sys/kern`, `lib/libc`**: not run beyond the rp2040 arch tree
  sample above, once that sample showed the tool needs a 32-bit/target-aware
  invocation this build setup does not have; extending the noisy run would not add
  signal.
- **codeql / infer**: both want a captured build database (`bear`-wrapped
  `bmake MACHINE=rp2040 kernel`) and their own project setup; skipped as
  disproportionate to the remaining time budget once clang --analyze, gcc
  -fanalyzer, and cppcheck had already covered the same interprocedural dataflow
  questions (null derefs, use-after-free, format mismatches) for this tree's size.
- **cgthumb.c (usr.bin/smlrc)**: read for syscall-emission concerns (it does not
  emit `svc`); not full-depth audited as a code generator, since BOOT-MAP.md section
  11 states the Thumb-1 backend is unmerged and not part of the running system.
- **UART receive path** (`dev/uart.c`): BOOT-MAP.md section 11 states this path has
  never been exercised on hardware. Static reading found the receive loop
  structurally parallel to the working USB receive loop, with one stale `#if 0`
  block referencing PIC32 register names as dead-code carryover; no static defect
  found, but this is read-only confirmation, not a hardware test, and the board was
  not touched to close the gap.
- **valgrind / qemu-arm harness**: only used for the elf2aout host reproduction
  (a native x86-64 harness, since the bug is in a host tool). No target-side
  qemu-arm harness was built; the `ramfunc`/syscall findings above were resolved by
  reading the linked kernel's actual disassembly instead, which is strictly stronger
  evidence for an alignment question than an emulator that may not model ARMv6-M's
  unaligned-access fault behavior faithfully.

## Summary

One CONFIRMED defect, fixed and committed: `tools/elf2aout/elf2aout.c`'s
`translate_syms` truncated every a.out symbol name past 7 characters because
`sizeof(nsp - 1)` names a pointer type, not a buffer size (commit `e2ad7dd`). Host
harness reproduced the truncation and confirmed the fix. Rebuilding the kernel and
`elf2aout` shows zero new warnings.

Two PLAUSIBLE findings, described but not fixed because disassembly disproves the
practical risk under the tree's actual build flags: a type-mismatched (but
compiler-narrowed-to-byte, therefore alignment-safe) read of the syscall index in
`syscall.c`, and a `-Wclobbered` local that is never read on the `longjmp` path.

Every other bug class named in the assignment -- setjmp/jmp_buf sizing, function
aliasing, spl/splx balance, `b_bcount` rounding, ramfunc-calling-flash -- was checked
against the current tree and found already fixed or already correct, most with an
in-source comment documenting the earlier fix; the ramfunc/XIP class is backed by
disassembly showing full inlining of the spl macros with no flash-resident functions
called while XIP is down.
