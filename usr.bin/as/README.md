# as -- the ARM target assembler

`usr.bin/as` builds `as-thumb.c` for both maintained ARM machines. RP2040
selects Cortex-M0+ and STM32 selects Cortex-M4 through the canonical machine
registry. RetroBSD's former MIPS32 assembler and its fixtures are preserved
under `legacy/non-arm/mips-pic32/` as archival source; the maintained build
has no MIPS assembler selector.

Both write the a.out object that `usr.bin/ld` links. A Thumb object is
marked `MID_ARM6` in `a_midmag`, which is what tells `ld` to read the
sparse relocation stream described below.

## What the Thumb assembler accepts

The input is GNU as unified Thumb syntax as `arm-none-eabi-gcc -S -mcpu=cortex-m0plus -mthumb`
emits it, and the divided pre-UAL syntax that a source file assembles in
when it declares no `.syntax`.

Every 16-bit encoding ARMv6-M defines is accepted, together with the 32-bit
`bl`, `msr`, `mrs`, `dmb`, `dsb` and `isb`. A conditional branch carries its
condition in the mnemonic, so `bne`, `bhi` and the rest resolve to `b` with
the condition attached.

Directives: `.text .data .bss .section .global .globl .local .weak .align
.p2align .word .long .4byte .short .hword .half .2byte .byte .ascii .asciz
.string .space .skip .comm .lcomm .equ .set .thumb_set .size .type .thumb
.code .thumb_func .syntax .cpu .arch .arch_extension .fpu
.eabi_attribute .file .ident .ltorg .pool .previous`. The CFI and unwind
directives are read and discarded, because an a.out object carries no place
to put them.

Labels may be named or numeric: `1:` with `1b` and `1f` referring backward
and forward, and the `.L` names GCC generates.

`.word` and `.hword` place their values at the cursor and align nothing, as
GNU as does, so `.byte 1` followed by `.word x` leaves the word at an odd
offset. The sparse relocation stream addresses any byte, so an unaligned
relocated word is expressible; whether the program can load it is the
source's business, as it is with GNU as.

`ldr rN, =expression` places the value in a literal pool. Expressions
naming the same symbol with the same addend share a slot, and so do equal
constants. A pool is emitted at `.ltorg` or `.pool` and at end of input,
and nowhere else: GNU as places one only where the source says to, and a
pool dropped in on its own would land in the middle of a function, where a
compiler that opens `.rodata` mid-function would have the processor execute
it. A load that cannot reach its pool is an error naming the distance, as
it is in GNU as. `adr rN, label` and `ldr rN, label` resolve against the
section they sit in.

### What it does not accept

Thumb-2. ARMv6-M is a 16-bit instruction set plus the five 32-bit
instructions named above, and anything wider is rejected rather than
silently narrowed. ARM (A32) state has no encoding here either: `.arm` and
`.code 32` are errors.

Debug information. `.debug_*` sections, CFI and unwind tables are dropped,
so a program assembled here carries no DWARF.

Macros, `.rept`, `.irp`, `.if` and the other GNU as control directives. The
compiler emits none of them, and neither does anything under `lib/libc/arm`.

## Two passes over the source

GCC's `-Os` switch tables read `(.Lfwd - .Lhere)/2` into a `.byte`, a
constant difference whose forward label is undefined when the cursor
reaches it. The assembler therefore reads the source twice: the first pass
learns every label's address and tolerates that subtraction, and the second
assembles against known addresses.

This is sound only if an instruction occupies the same space on both
passes, which holds because Thumb-1 has no relaxation and nothing here
narrows. Two constructs could still shift a segment -- a literal pool slot
that two loads come to share once their forward targets are known, and a
`.space` whose count the first pass could not evaluate -- so the segment
sizes from the two passes are compared and a mismatch is an error naming
the segment, rather than a silently wrong forward reference.

## Relocations

A Thumb BL occupies two halfwords and straddles a word boundary whenever it
sits at an odd halfword. The MIPS relocation stream cannot name it: that
stream is positional, one record per four bytes of segment, so a record can
only ever address a word. A Thumb object therefore carries a sparse stream,
in which each record names the segment offset it patches -- five bytes, or
eight when the record also carries a symbol index.

`include/a.out.h` adds four formats beside the MIPS ones, in a field
widened to four bits by taking `RGPREL`, which this target never sets:

| format     | patches                                    |
|------------|--------------------------------------------|
| `RTABS32`  | a 32-bit absolute address                  |
| `RTCALL`   | `bl`, a 22-bit immediate split over two halfwords |
| `RTJUMP11` | `b`, an 11-bit PC-relative halfword offset |
| `RTJUMP8`  | `b<cond>`, an 8-bit PC-relative halfword offset |

A branch whose target lies in the same segment is resolved by the assembler
and emits no record, because the segment bases cancel in a PC-relative
displacement. Only a cross-segment or external target survives, and a
target out of reach is an error naming the instruction.

The low bit of a Thumb function's address selects the instruction set. A
label keeps its even value inside the assembler so an internally resolved
branch computes the right displacement, and the bit is applied in exactly
three places: the `nlist` record, an absolute word that stores a function
address, and the `a_entry` that `ld` writes.

## Size

Built for the RP2040 with `bmake MACHINE=rp2040`, measured by
`arm-none-eabi-size`:

| program | text  | data | bss   | total  |
|---------|-------|------|-------|--------|
| `as`    | 31664 | 765  | 30848 | 63277  |
| `ld`    | 21964 | 745  | 41412 | 64121  |

A program on the RP2040 gets 144 kbytes for text, data, bss and stack
together. The bss is nearly all fixed tables -- the symbol table,
its string area and the two hash tables -- and neither program holds an
input segment in memory: both passes stream through scratch files, as the
MIPS assembler does.

## Tests

    bmake -C usr.bin/as MACHINE=rp2040 test

The tests run on the build host. They need `arm-none-eabi-gcc`,
`arm-none-eabi-as`, `arm-none-eabi-objcopy` and `arm-none-eabi-readelf`,
the host build of this tree's own tools from `tools/aoututils`, and the
interpreter named by `PYTHON`. The last step also runs the linked program if
that interpreter can import Unicorn, and says so when it cannot.

Three tests run in order.

Each comparison covers both the text and the data segment. A field covered
by a relocation is excluded, because there the a.out addend convention and
the ELF one differ by design and neither is wrong; the offsets come from
each object's own relocation table, so the exclusion is read from the data
rather than assumed. Every other byte must match.

**Encoding.** Seven inputs name every ARMv6-M instruction form, along with
the directives, local labels, PC-relative forms and literal pool. Each is
assembled by this assembler and by `arm-none-eabi-as` and the results are
compared. The last is assembly written by `usr.bin/smlrc`'s Thumb-1 back
end, the native compiler this assembler exists to serve, whose literal pool
and section use differ from the cross compiler's.

**Compiler output.** `arm-none-eabi-gcc -S` output for `bin/cat`,
`bin/echo` and `usr.bin/wc` is assembled and compared. These
carry what hand-written input does not: switch tables that read the
difference of two labels into a `.byte`, code split across `.text` and
`.text.startup`, and calls to symbols the object does not define.

**Link.** This is the acceptance test. `crt0` and the whole of `libc` are
built with this assembler through `lib/libc_aout`, every one of those
translation units is compared against `arm-none-eabi-as`, the library is
archived with the tree's own `ar`, and a program is linked against it with
the tree's own `ld`. The result must be an `OMAGIC` executable marked
`MID_ARM6` with an odd entry point and no relocation left over, and it is
then executed under Unicorn against a stub kernel and must print what it
should.

The same test entry point runs for each maintained ARM tuple. Archived MIPS
fixtures are excluded from the maintained test graph.
