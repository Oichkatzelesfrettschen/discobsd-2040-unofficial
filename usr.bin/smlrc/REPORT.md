# Thumb-1 code generator for Smaller C -- report

Branch `smlrc-thumb`, worktree `~/worktrees/discobsd/smlrc-thumb`, branched
from `rp2040-port`. No hardware was touched.

## Result

Smaller C compiles C for the Cortex-M0+ and cross-builds for the RP2040. All
sixteen test programs compile, assemble under `-mcpu=cortex-m0plus`, link
against the tree's `crt0.o`, `libc.a` and `elf32-arm.ld` with no undefined
symbols, convert with `elf2aout`, and execute under `qemu-arm` producing
output identical to a 32-bit host-compiled oracle. A further 200 generated
programs match that oracle.

## Deliverables

`usr.bin/smlrc/cgthumb.c`, 1884 lines, selected by `-DTHUMB`, paralleling
`cgmips.c`'s entry points. Clean under `gcc -Wall -Wextra` and
`clang -Wall -Wextra`; the compiler is clean under valgrind on every test
input. Smaller C's BSD-2-Clause header is preserved verbatim, and no new file
carries a copyright line.

`usr.bin/smlrc/Makefile` selects the back end by `MACHINE`. Other machines are
byte-for-byte unchanged in behavior: `-DMIPS`, `-mips16`, and `lb.c` as
before. `smlrc.c` gains only the `#include` selection.

`usr.bin/smlrc/tests/`, run by `bmake -C usr.bin/smlrc test`, plus
`bmake -C usr.bin/smlrc fuzz` for differential testing.

`usr.bin/smlrc/README.rp2040.md` records support, limitations, footprint and
commands.

## Compiler footprint

    text     data      bss      dec      hex
   51613     1588    26200    79401    13629

77.5 KB of the 96 KB process budget, leaving 18.5 KB of stack. Workable, not
generous. The bss is nearly all parser tables: at `SYNTAX_STACK_MAX=3200`,
`SyntaxStack1` is 12800 bytes, `SyntaxStack0` 3200, `IdentTable` 5632.
Halving `SYNTAX_STACK_MAX` returns 8 KB of stack and rejects the largest
translation units. This is the tuning knob if the stack proves too small on
the device; that has not been measured, because the board was not touched.

## What the architecture forced

Four decisions are not stylistic. Each is recorded in `cgthumb.c` beside the
code it governs.

**Every jump is a BL.** ARMv6-M `B<cond>` reaches 256 bytes, `B` reaches 2048,
and GNU as relaxes neither -- verified directly, it reports "branch out of
range" rather than widening. A compiler cannot bound its output below those
limits, so both are unusable. `BL` reaches 16 MB, and `lr` is dead in the
body because the prolog saves it and the epilog returns through `r3`. `t14`
contains a loop whose closing jump spans **-13204 bytes**, six times the `B`
reach, and produces correct output.

**The frame size is a forward `.equ`.** `cgmips.c` seeks back over the output
with `fgetpos`/`fsetpos` to patch the frame size once the body is parsed. The
tree's libc has neither function. Naming the size with a symbol the epilog
defines removes the dependency, which is what lets the rp2040 build link
against the tree's libc at all, and leaves the frame size unbounded.

**No argument fast path.** The MIPS generator evaluates the leftmost argument
directly into the first argument register while the working register is a
distinct `V0`. On ARM both are `r0`, so a callee expression producing a
function pointer would overwrite argument one. Every argument goes to the
stack, which also frees `r1`-`r3` as scratch throughout.

**Division is a call.** This one was found by testing, not by reading -- see
below.

## The defect the differential testing found

ARMv6-M has no divide instruction, so every division and modulo is a `BL` to
an `__aeabi` helper taking and returning operands in `r0` and `r1`. The
generator inherited from `cgmips.c` the rule that an expression containing no
function call may hold subexpressions in registers. A division is invisible
to that rule, so a statement such as

    r = (a + 5) + (a / b);

kept `(a + 5)` in `r0` and then emitted `mov r0, r5` to set up the helper
call, destroying it. Expected 1147, produced 284. `cgmips.c` never faces this
because its division writes only `HI` and `LO`.

All sixteen hand-written tests missed it, and the reason is instructive:
every one of them divided inside a `printf` argument, and the real call in
that statement had already forced temporaries onto the stack. The bug only
appears when the *only* call in a statement is the hidden one.

It was found by `tests/fuzz.sh`, which compiles generated programs with both
smlrc and the host compiler at `-m32 -fwrapv` and compares output.
`gen_diff.py` keeps every generated program free of undefined behavior --
divisors forced non-zero, shift counts masked, signed overflow wrapping on
both sides -- so any difference is a code generator defect. Before the fix,
0 of 60 seeds matched; after, 200 of 200.

The fix counts division and modulo as calls when deciding register use.
`tests/t15_divreg.c` is the regression test, and it was confirmed to catch
the defect: built against a compiler with the fix removed, it segfaults.

## Floating point

Smaller C names the generic libgcc helpers, `__addsf3` and so on. An
`arm-none-eabi` libgcc for v6-m publishes those operations under their AEABI
names and defines no `__addsf3`, so `float` assembled and then failed to
link. Rather than reject float, the back end prints the AEABI name at the
call site -- `__aeabi_fadd`, `__aeabi_fmul`, `__aeabi_f2iz` and five more.
Each pair is operand-compatible, so the substitution is a rename. The
comparison helpers `__lesf2` and `__gesf2` keep their generic names, which
libgcc defines and whose sign-of-difference result the AEABI `__aeabi_fcmp`
family does not share. `t16_float` covers it.

## Limitations, named

- **K&R function definitions are rejected**, so `bin/echo/echo.c` and
  `bin/cat/cat.c` do not compile as they stand. This is the front end: the
  MIPS back end rejects them at the same token. `t12` and `t13` are those
  programs with prototype definitions and nothing else changed.
- **The `interrupt` attribute** raises an internal error. A user process
  installs no handler and the ARMv6-M exception frame is not the one the
  prolog builds.
- **`sp` is 4-byte aligned at calls, not always 8.** The MIPS back end is the
  same. ARMv6-M has no `LDRD`, `STRD` or FPU, so nothing in the instruction
  set requires the stronger alignment; a `double` passed to a gcc-compiled
  variadic function is the case that would notice, and the rp2040 link line
  omits floating-point printf by default.
- **Local variable access costs three instructions.** Thumb-1 load and store
  offsets are unsigned, so a local's negative offset from `r7` never encodes
  and the address is computed. Correctness over speed, as scoped.

## Review follow-ups

A review after the first six commits raised four points, all acted on.
Compound division whose left side is a dereference -- `*p /= b`,
`sp->x %= b` -- takes a different path from the local and global cases and
holds the address in a register across the helper call, exactly the hazard
that produced the defect above; the fuzzer cannot reach it because
`gen_diff.py` generates no pointers. `t15_divreg.c` now covers it and passes.
`GenNumLabel` did not flush a literal pool although two comments said it did,
so it now does, guarded on the text section because the same emitter also
names static initializers and string literals -- an unguarded flush would put
a branch and a `.ltorg` into `.data`. Every generated `.s` was checked for
that and none has one. `bmake -C usr.bin/smlrc test`, the interface as
stated, was run from the worktree root rather than only from the compiler's
own directory. And `run.sh` now checks that `crt0.o`, `libc.a`, the linker
script and `elf2aout` exist before linking, and that `libc.a` carries the
v6-M build attribute: one `lib/libc.a` serves every ARM machine, the linker
accepts an object built for a wider architecture without complaint, and a
Cortex-M4 libc runs under a full-ARM emulator while faulting on the board.

## What was not run

- **Nothing executed on the board.** Every a.out is left in `tests/out/` with
  its expected output beside the test source. `qemu-arm` cannot run them --
  DiscoBSD uses an SVC immediate and the carry flag, which a Linux user
  emulator does not implement -- which is why the harness has two tiers.
- **The MIPS build was compile-checked only**, since no MIPS toolchain is
  installed here. `cgmips.c` and its Makefile path are unchanged in behavior.
- **The compiler was not run on the device**, so the 18.5 KB stack headroom is
  arithmetic, not measurement.
- **Self-hosting was not attempted.** `cgthumb.c` is written in the same
  subset as `cgmips.c`, but nothing verifies that smlrc compiles itself.

## Tooling

The catalog at `~/Documents/AI/Notes/1_TOOLS.md` was audited; the corrected
path given later, `~/Documents/Notes/AI/1_TOOLS.md`, does not exist and the
file at the original path is the only copy. From it: `qemu-arm` executes the
generated code, `binutils` verified encodings and measured branch
displacements, `valgrind` cleared the compiler on every test input, and
`clang` provided a second warning front end. The differential harness was
built rather than taken from the catalog, which lists no C program generator.
