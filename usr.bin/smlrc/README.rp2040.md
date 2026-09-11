# Smaller C on the RP2040

`cgthumb.c` is a Thumb-1 (ARMv6-M) code generator for Smaller C, selected by
`-DTHUMB` and built by `bmake MACHINE=rp2040`. It emits GNU unified assembly
that `arm-none-eabi-as -mcpu=cortex-m0plus` accepts, using 16-bit Thumb-1
encodings plus `BL`, and follows AAPCS so that code it generates calls and is
called by the tree's `arm-none-eabi-gcc`-compiled libc.

Other machines are untouched: `MACHINE` chooses `-DMIPS` with `cgmips.c` and
`lb.c` as before, and only `rp2040` takes the new path.

## Compiler footprint

`arm-none-eabi-size` of the cross-built compiler:

    text     data      bss      dec      hex   filename
   51561     1588    26200    79349    135f5   smlrc.elf

A process gets 96 KB for text, data, bss and stack together, so the compiler
occupies 77.5 KB and leaves 18.5 KB of stack. That is workable but not
generous. The bss is almost entirely the parser's tables, and
`SYNTAX_STACK_MAX` in the Makefile is the knob: at the current 3200 entries,
`SyntaxStack0` is 3200 bytes and `SyntaxStack1` is 12800, and `IdentTable`
adds 5632. Halving `SYNTAX_STACK_MAX` returns 8 KB of stack at the cost of
rejecting the largest translation units.

The MIPS build halves its text with `-mips16`. The Cortex-M0+ executes only
Thumb, so there is no corresponding option and none is used.

## What is supported

The full Smaller C language subset the MIPS back end supports:

- `int`, `char`, `short`, `long`, their `unsigned` forms, `_Bool`, and
  enumerations. `long` is `int` is 32 bits, and pointers are 32 bits.
- Pointers, arrays, and pointer arithmetic.
- `struct` and `union`, including nesting, assignment of whole structures,
  and passing and returning them by value.
- All arithmetic, bitwise, shift, comparison, logical and compound-assignment
  operators, prefix and postfix increment and decrement, and the conditional
  operator.
- `if`, `while`, `do`, `for`, `switch` with fallthrough and `default`,
  `break`, `continue`, `goto` and labels, and `return`.
- File-scope and function-scope `static`, initializers for scalars, arrays,
  strings and structures, and `extern` declarations.
- Variadic calls, which work because the prolog places parameters one to four
  in a home area contiguous with the stacked parameters, so `va_arg` walking
  up from the last named parameter is correct.
- `float`, lowered to the libgcc soft-float helpers. See the note below.

`char` is **unsigned**, because `arm-none-eabi-gcc` makes it unsigned and
every object the tree's libc contributes was compiled under that rule.
`-signed-char` still overrides it, but a program compiled that way disagrees
with libc about `char` comparisons.

## What is rejected, and what is not supported

- **Old-style (K&R) function definitions.** Smaller C's parser accepts no
  old-style parameter declaration list, so `bin/echo/echo.c` and
  `bin/cat/cat.c` do not compile as they stand. This is a front-end
  limitation, not a back-end one: the MIPS back end rejects them at the same
  token. `tests/t12_echo.c` and `tests/t13_cat.c` are those two programs with
  their definitions rewritten as prototypes and nothing else changed.
- **Function-like macros in smlrc's own preprocessor.** `<stdio.h>` defines
  `getc` and `putc` as function-like macros, which smlrc's built-in
  preprocessor does not expand. On the device `usr.bin/cc` runs `usr.bin/cpp`
  first and smlrc is built with `-DNO_PREPROCESSOR`; the test harness mirrors
  that by preprocessing with the cross compiler.
- **The `interrupt` attribute.** `GenIsrProlog` and `GenIsrEpilog` report an
  internal error. A DiscoBSD user process installs no handler, and the
  ARMv6-M exception entry sequence is not the frame the prolog builds.
- **Double precision.** Smaller C has no `double` beyond treating it as
  `float`.
- **8-byte stack alignment at public interfaces.** The argument area is built
  from 4-byte pushes, so `sp` at a call is 4-byte aligned and only sometimes
  8-byte aligned, which is what the MIPS back end also does. On ARMv6-M this
  has no observable effect: the architecture has no `LDRD` or `STRD` and no
  FPU, so nothing in the instruction set requires the stronger alignment.
  Code that passes a `double` to a gcc-compiled variadic function is the
  case that would notice, and the rp2040 link line omits floating-point
  printf unless `PRINTF_FLOAT=yes`.

### Floating point

Smaller C names the generic libgcc helpers -- `__addsf3`, `__mulsf3`,
`__floatsisf` and so on. An `arm-none-eabi` libgcc built for v6-m publishes
those operations under their AEABI names instead and defines no `__addsf3`,
so a program using `float` would assemble and then fail to link. The back end
substitutes the AEABI name when it prints the call target: `__aeabi_fadd`,
`__aeabi_fsub`, `__aeabi_fmul`, `__aeabi_fdiv`, `__aeabi_fneg`,
`__aeabi_f2iz`, `__aeabi_i2f` and `__aeabi_ui2f`. Each pair takes and returns
its operands in the same registers, so the substitution is a rename.

The comparison helpers `__lesf2` and `__gesf2` keep their generic names,
which libgcc does define and whose sign-of-difference result the AEABI
`__aeabi_fcmp` family does not share.

`printf("%f")` needs `doprnt_float`, which the rp2040 link line omits unless
a program sets `PRINTF_FLOAT=yes`. Integer conversions of float values work
without it.

## Design notes

**Registers.** `r0` is the working register and the AAPCS return register;
`r4` is the one subexpression temporary; `r5` and `r6` are the two scratch
registers the expression evaluator needs across a helper call, and are
callee-saved because `__aeabi_idivmod` preserves only `r4`-`r11`; `r7` is the
frame pointer. `r1`, `r2` and `r3` are a fixed address scratch, constant
scratch and call-target holder, free because arguments travel on the stack
until the loads immediately before the `BL`.

**Calls.** The MIPS generator's fast path, which evaluates the leftmost
argument directly into the first argument register, is not reproduced: on
ARM the working register and the first argument register are both `r0`, so a
callee expression producing a function pointer would overwrite argument one.
Every argument is evaluated onto the stack, the first four words are loaded
into `r0`-`r3` and dropped, and the callee rebuilds them into a home area.

**Branches.** ARMv6-M `B<cond>` reaches 256 bytes and `B` reaches 2048, and
GNU as relaxes neither -- it reports "branch out of range". Every jump is
therefore a `BL`, which reaches 16 MB, and every conditional jump is the
inverted condition branching over a `BL`. `BL` clobbers `lr`, which is
harmless because the prolog saves it and the epilog returns through `r3`, so
no function is treated as a leaf.

**Frame size.** The prolog names the frame size with a symbol that the epilog
defines with a forward `.equ`, rather than seeking back over the output as
the MIPS generator does. That is what lets the rp2040 build use the tree's
libc, which has neither `fgetpos` nor `fsetpos`, and it leaves the frame size
unbounded.

**Literal pools.** `LDR` (literal) reaches 1020 bytes forward. Every emitter
accounts the bytes it writes, and a pool is flushed as a `B` over a `.ltorg`
whenever the distance passes 850, checked only at the top of the token loop,
at a numeric label, and at the end of a function -- the points where no
instruction sequence is open.

**Division.** ARMv6-M has no divide instruction, so every division and
modulo is a `BL` to an `__aeabi` helper that takes and returns its operands
in `r0` and `r1`. These are counted as calls when deciding whether
subexpressions may live in registers, which keeps the working register at
`r0` and every temporary on the stack, out of reach of the helper's
caller-saved clobbers. The MIPS generator never has to consider this,
because its division writes only `HI` and `LO`.

## Tests

    bmake -C usr.bin/smlrc test        # build and run the suite
    bmake -C usr.bin/smlrc fuzz        # additionally, differential testing

`tests/run.sh` builds smlrc for the build host with `-DTHUMB` so that it runs
natively, then for each program preprocesses it, compiles it with that
compiler, and checks the result two ways.

The **device tier** links exactly as `share/mk/sys.mk` links a user program --
`lib/crt0.o`, `lib/libc.a` and `lib/elf32-arm.ld` -- requires the link to
resolve every symbol, and converts the result with `tools/bin/elf2aout`. The
a.out it leaves in `tests/out/` is what runs on the board.

The **execution tier** links the same object against `tests/qemusys.c`, a
Linux EABI syscall layer, and runs it under `qemu-arm`. DiscoBSD reaches the
kernel through an SVC immediate and reports failure in the carry flag, which
a Linux user emulator does not implement, so the DiscoBSD a.out cannot run
under qemu. Only the syscall layer is replaced; because `libc.a` is an
archive, a member is pulled in only for a symbol still undefined, so these
definitions displace the DiscoBSD ones and everything above them -- printf,
malloc, the string functions -- is the tree's own libc. Output is compared
against a `.expected` file generated by the host compiler in 32-bit mode.

If `qemu-arm` is absent the harness still links every test both ways and
leaves the a.outs in `tests/out/` for the maintainer to run on the device,
with the expected output beside each test as `tests/<name>.expected`.

`tests/fuzz.sh` compiles generated programs both with smlrc and with the host
compiler at `-m32 -fwrapv`, runs both, and compares. `gen_diff.py` keeps every
generated program free of undefined behavior -- divisors forced non-zero,
shift counts masked, signed overflow wrapping on both sides -- so a difference
is a code generator defect. This found the division-clobbers-`r0` defect that
every hand-written test missed, because each had a real call in the same
statement, which already forced temporaries onto the stack.
