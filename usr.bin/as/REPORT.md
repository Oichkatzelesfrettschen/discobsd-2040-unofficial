# Thumb-1 assembler and linker support for the RP2040 port

Branch `thumb-as-ld`, worktree `~/worktrees/discobsd/thumb-as-ld`, on top of
`rp2040-port`.

## What passes

Every test below runs from `bmake -C usr.bin/as MACHINE=rp2040 test` unless
noted otherwise, on the build host against `arm-none-eabi-*` 16.2.0.

**Encoding, against `arm-none-eabi-as`.** Seven inputs naming every 16-bit
ARMv6-M encoding plus the 32-bit `bl`, `msr`, `mrs`, `dmb`, `dsb` and `isb`,
together with the directives, local labels, PC-relative forms and literal
pool, agree halfword for halfword.

**The native compiler's output.** All 14 programs of
`usr.bin/smlrc/tests`, compiled by smlrc's Thumb-1 back end -- the compiler
this assembler exists to serve -- assemble and agree with
`arm-none-eabi-as`. That back end landed on `rp2040-port` after this
branch's base, and testing against it found three defects that neither the
cross compiler's output nor the hand-written inputs reach, because GCC never
emits a literal pool of its own:

- `MUL` in UAL is `MULS Rdm, Rn, Rdm`, its destination repeating in the
  third operand rather than the second as every other data-processing
  instruction does. The three-operand form was rejected.
- A pool was flushed when the section changed. smlrc opens `.rodata` in the
  middle of a function to place a string, so the pool landed inside that
  function and the processor would have executed it. GNU as keeps a pending
  pool across a section change and emits it at `.ltorg`; so does this now.
- A pool was also flushed on its own once a load neared the 1020-byte reach
  of a PC-relative load. GNU as never does that -- it reports
  `invalid offset, value too big` and leaves placement to the source -- and
  an automatic pool has the same hazard of landing in the instruction
  stream. It is gone, and an unreachable pool is an error naming the
  distance.
- The pool's slot-sharing key was the resolved value, which is not the same
  on both source passes: a forward label is zero on the first and its
  address on the second, so two slots could merge into one and shift every
  later address. The key is now the symbol and addend the expression named,
  which both passes agree on. The two-pass size check caught this on
  `t14_stress.c` before the comparison did.

One representative smlrc output is checked in as `tests/thumb-native.s`, so
the coverage survives without the test needing a second compiler.

**Compiler output.** `arm-none-eabi-gcc -S` for `bin/cat`, `bin/echo` and
`usr.bin/wc` assembles and agrees: 648, 106 and 232 text halfwords, of which
71, 8 and 29 lie in relocated fields and the rest match exactly, and 117, 0
and 54 bytes of data.

**The whole of libc.** All 329 translation units of `lib/libc_aout/libc`,
plus `crt0`, assemble with this assembler and agree with `arm-none-eabi-as`
on every byte outside a relocated field, in both the text and the data
segment -- 43 of them carry data, 5115 bytes of it. This is the widest of
the tests: real compiler output plus the hand-written `.S` files across the
entire library.

**Link and run, the acceptance test.** `crt0.o`, a program, and `libc.a`
archived by the tree's own `ar` link with the tree's own `ld` into a
1616-byte-text `OMAGIC` executable marked `MID_ARM6`, entry `0x7f008001`,
with no relocation left over. Executed under Unicorn against a stub kernel
it prints `Hello, World!` and its own `argv[0]`, which exercises `crt0`
computing `__progname`, the string relocation in `main`, libc's `strlen`,
libc's `write` syscall stub, and `exit`.

**The MIPS target is unchanged.** `as.c` assembles `tests/test1.s` through
`test5.s` to objects byte-identical with those from the assembler at
`rp2040-port`, and `bmake -C usr.bin/as test` for any other machine keeps
the disassembly comparison that was there before.

**Error paths.** A branch out of reach is reported by the assembler naming
the form (`conditional branch out of range`); a `bl` that only becomes out
of reach once `ld` has laid the segments out is reported by `ld`
(`Thumb BL out of range`), verified across a 5 Mbyte object.

**Warnings.** `as-thumb.c` compiles with zero warnings under both
`cc -Wall -Wextra -std=gnu89` and `clang -Wall -Wextra -std=gnu89`, and is
clean under `cppcheck --enable=warning,performance`. The three shell scripts
are clean under `shellcheck -S error`. `ld.c` carries three warnings, all
pre-existing in code this work does not touch (`delexit`'s unused parameter
and two signedness comparisons).

**Independent decode.** capstone, which shares no code with binutils,
disassembles the emitted text back to the source instructions.

**Two-pass stability.** The assembler compares the segment sizes its two
source passes compute and fails with a named error if they differ, so the
soundness condition for the second pass is checked rather than asserted.
Both constructs that can break it were built and confirmed to trigger it: a
literal pool slot that two loads come to share once their forward targets
resolve, and a `.space` whose count the first pass cannot evaluate. Neither
occurs in the 338 real inputs.

## What does not pass, and why

**The acceptance test as originally specified is not achievable.** The task
asked to "compare the resulting a.out's text bytes with the elf2aout-converted
GCC build of the same program where they should match after relocation".
They cannot match, for three independent reasons, each sufficient on its own
and none of them a defect in this work:

1. Literal pools live inside `.text` and hold absolute addresses of rodata,
   data and bss symbols. GNU ld orders `.text | .rodata | .data`; the tree's
   `ld` orders `text | data | rodata` -- `adbase = dbase + count[SDATA]` in
   `as`, and `ld` concatenates all text, then all data. Any program that
   names a string or a global therefore has different values relocated into
   its text.
2. The two linkers assign `.rodata` to different a.out segments. Measured:
   `arm-none-eabi-gcc -N` puts `.text`, `.rodata`, `.data` and `.bss` in one
   `PT_LOAD`, and `elf2aout` reports `text: 0x30, data: 0` for a program
   whose rodata the tree's `ld` would have counted as data.
3. `lib/elf32-arm.ld` hoists `.text.unlikely`, `.text.exit`, `.text.startup`
   and `.text.hot` ahead of `.text`, and at `-Os` GCC puts `main` in
   `.text.startup`; a.out `ld` concatenates in object order.

Nor can a purpose-written linker script rescue it: within a single object the
a.out image is text, then data, then rodata, so across objects rodata is
interleaved per object rather than grouped, which a GNU linker script cannot
express.

The test therefore keeps the link and every relocation under test and
replaces the oracle. It links with this toolchain, asserts the executable has
no relocation left and an odd entry, and then *runs* it -- a stronger check
on the linked bytes than comparing them to a second linker's
different-but-also-correct layout. The per-object comparison against
`arm-none-eabi-as` is retained and widened from one program to all 329 libc
units, so the assembler itself is still held to an external oracle.

**A printf program does not complete under the emulator**, and the cause is
the harness, not this toolchain. It stops silently in stdio's flush path.
The same program built entirely by `arm-none-eabi-gcc`, linked by GNU `ld`
and converted by `elf2aout` -- a path containing none of this work -- stops
at the same point with the same empty output under the same runner, while
the `write`-based program built by this toolchain runs to completion and
prints. The stub kernel's zeroed `fstat` and `ioctl` leave stdio without
usable terminal state; implementing enough of the DiscoBSD kernel ABI to
carry buffered stdio was out of scope.

**The board was not touched**, as instructed. Unicorn is the closest this
goes; it is not a substitute for running on an RP2040.

**Debug information is dropped.** CFI and unwind directives are parsed and
discarded, so programs assembled here carry no DWARF.

**Thumb-2 is rejected, not narrowed.** ARMv6-M does not have it, and `.arm`
and `.code 32` are errors rather than silent no-ops.

**`ld` overstates `a_syms` by four bytes** when the symbol table is already
word aligned: `while (ssize++ % W)` increments even when the test fails, and
`ALIGN(ssize, W)` then rounds past the end. This is pre-existing and
identical at `rp2040-port`, it affects MIPS the same way, and fixing it
would change every MIPS executable, so it is reported here rather than
changed as a side effect of Thumb work. Nothing in the tree reads past
`a_syms`; it was found because a dump script written for this work did.

**One class of difference with `arm-none-eabi-as` is by design and is
excluded from every comparison**: the contents of a relocated field. An
a.out `RTCALL` record stores a plain displacement addend and `ld` computes
`target - (origin + addr + 4)`; ELF `R_ARM_THM_CALL` stores what its own
convention prescribes, which for an unresolved call is `0xf7ff 0xfffe`.
Likewise an a.out `RTABS32` holds a segment-relative address where ELF holds
zero and an addend in the section symbol. The comparison marks these fields
from both objects' own relocation tables and requires every other halfword
to match, so the exclusion is derived from the data rather than assumed.

## Sizes

`arm-none-eabi-size`, built with `bmake MACHINE=rp2040`:

| program | text  | data | bss   | total  |
|---------|-------|------|-------|--------|
| `as`    | 31528 | 761  | 29824 | 62113  |
| `ld`    | 21964 | 745  | 41412 | 64121  |

A program gets 96 kbytes on the device for text, data, bss and stack, so
`as` leaves about 34 kbytes of stack and `ld` about 32. Neither holds an
input segment in memory: both stream through scratch files, as the MIPS
assembler does. `as`'s bss is almost entirely the fixed symbol table, its
string area and the two hash tables.

`as-thumb.c` is 3293 lines. The changes to `ld.c` and `a.out.h` are 271
lines added and 4 removed.

## Design

**The relocation stream had to change.** The MIPS stream is positional:
`pass2` in `as` and `relocate` in `ld` walk a segment four bytes at a time
and consume exactly one record per word, so a record can only name a word. A
Thumb `bl` occupies two halfwords and straddles a word boundary whenever it
sits at an odd halfword -- which it does in the linked hello program -- and
no record in that stream can address it. Objects marked `MID_ARM6` in
`a_midmag` therefore carry a sparse stream: each record names the segment
offset it patches, five bytes, or eight when a symbol index follows.
`relocate_thumb` copies the segment through unchanged and then seeks to each
offset, so straddling and arbitrary alignment need no lookahead and the pass
stays O(1) in memory. MIPS objects are read by the old reader, unchanged;
`ld` refuses to mix the two.

**The assembler reads the source twice.** GCC's `-Os` switch tables read
`(.Lfwd - .Lhere)/2` into a `.byte`, a constant difference whose forward
label is undefined when the cursor reaches it. The first pass learns every
label's address and tolerates that subtraction; `rescan` then discards the
emitted bytes, keeps the symbol table, and the second pass assembles against
known addresses. This is sound because Thumb-1 has no relaxation and this
assembler narrows nothing, so an instruction occupies the same space on both
passes.

**References are resolved late.** Every branch or literal load whose target
is not yet known becomes a fixup record; `resolvefix` walks them once pass1
is done. A target in the same segment is patched into the segment directly,
because the segment bases cancel in a PC-relative displacement, and only a
cross-segment or external target survives as a relocation.

**Seven encoding and layout choices follow GNU as rather than the architecture's
preferred form**, because the two assemblers have to agree byte for byte and
GNU as is the oracle. `nop` is `mov r8, r8`, not the `0xbf00` hint.
`ldr rN, =imm` takes a pool slot rather than narrowing to `movs`. An add or
subtract whose destination repeats its source takes the two-operand
eight-bit form. Padding from an `.align` directive in code is `nop` while
padding inserted before a literal pool is zero. And `mov rd, rm` between two
low registers is `adds rd, rm, #0` in the divided pre-UAL syntax that a file
declaring no `.syntax` assembles in -- which is how every hand-written `.S`
under `lib/libc/arm` is written -- and the high-register encoding once
`.syntax unified` is declared. Finally `.word` and `.hword` align nothing,
so `.byte 1` followed by `.word x` leaves the word at an odd offset; the
sparse stream can address it. That last one was invisible until the
comparison was extended from text to data.

**The Thumb bit is applied in three places and nowhere else**: the `nlist`
record `fputsym` writes, an absolute word that stores a function address, and
the `a_entry` `ld` writes. A label keeps its even value inside the assembler,
so a branch resolved internally computes the right displacement. `getterm`
sets `expr_thumb` when an expression names an already-defined Thumb function,
because `getexpr` folds a defined symbol down to a number and would otherwise
lose the identity that carries the bit.

## Reproducing

    git worktree add ~/worktrees/discobsd/thumb-as-ld thumb-as-ld
    cd ~/worktrees/discobsd/thumb-as-ld

    bmake symlinks MACHINE=rp2040
    bmake -C tools MACHINE=rp2040 install
    bmake -C tools/aoututils MACHINE=rp2040 install

    bmake -C usr.bin/as MACHINE=rp2040 test        # the three tests

    bmake -C lib MACHINE=rp2040                    # for the size figures
    bmake -C usr.bin/as MACHINE=rp2040             # prints as's size
    bmake -C usr.bin/ld MACHINE=rp2040             # prints ld's size

The MIPS target is checked by building the assembler from `rp2040-port` and
comparing objects:

    git show rp2040-port:usr.bin/as/as.c > /tmp/as-old.c
    cc -Os -w -std=gnu89 -DCROSS -Itools/aoututils/include \
       -idirafter include /tmp/as-old.c -o /tmp/as-old
    bmake -C tools/aoututils/as                    # the MIPS default
    for t in 1 2 3 4 5; do \
        /tmp/as-old usr.bin/as/tests/test$t.s -o /tmp/a.$t; \
        ./tools/aoututils/as/as usr.bin/as/tests/test$t.s -o /tmp/b.$t; \
        cmp /tmp/a.$t /tmp/b.$t; done

Warnings:

    cc    -Os -Wall -Wextra -std=gnu89 -DCROSS -Itools/aoututils/include \
          -idirafter include -c usr.bin/as/as-thumb.c -o /dev/null
    clang -Os -Wall -Wextra -std=gnu89 -DCROSS -Itools/aoututils/include \
          -idirafter include -c usr.bin/as/as-thumb.c -o /dev/null
    cppcheck --enable=warning,performance --std=c89 usr.bin/as/as-thumb.c
    shellcheck -S error usr.bin/as/tests/thumb-*.sh

## Tooling

From the user's catalog at `~/Documents/AI/Notes/1_TOOLS.md` (the corrected
path `~/Documents/Notes/AI/1_TOOLS.md` does not exist; the catalog audited is
the one that does): `capstone` decodes emitted text independently of
binutils, `unicorn` runs the linked executable, `cppcheck` and `clang-tidy`
check the new C, and `shellcheck` the new shell. `keystone`, `radiff2` and
`diffoscope` were available and not needed once the binutils comparison was
in place. `cppcheck` found one real defect, an unchecked symbol index in
`resolvefix`, which is now bounds-checked.
