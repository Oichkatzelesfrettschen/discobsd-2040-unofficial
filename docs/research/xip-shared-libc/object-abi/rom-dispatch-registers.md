# Shared ROM dispatch register ownership

The double-precision hot-call candidate resolves its target through the
process GOT while preserving `r0` through `r11`, the caller's return address,
condition flags and stack arguments. The dispatch uses caller-saved `ip`
(`r12`) for its branch target. GNU assembly for Cortex-M0+ accepts the sequence;
Unicorn 2.1.4 executes three distinct GOT placements successfully.

```asm
push {r2, r3}
ldr r3, offset
mov r2, r9
ldr r3, [r2, r3]
ldr r3, [r3]
mov ip, r3
pop {r2, r3}
bx ip
.p2align 2
offset: .word 12
```

The literal value 12 belongs to the synthetic GOT fixture. Production assembly
requires a linker-generated `R_ARM_GOT_BREL` displacement. The first load through
the GOT obtains the process-local cache-cell address; the second obtains the
cached ROM function. Saving `r2` and `r3` permits both loads without destroying
double-precision operands in `r0` through `r3`. Moving the target to `ip` permits
restoring those operands before the tail branch. The sequence occupies 20 bytes
including its aligned literal and uses eight temporary stack bytes. Those costs
exclude its cold descriptor, resolver, GOT cell and any link padding.

## Executed controls

[The receipt](rom-dispatch-registers.json) retains the exercised placements and
verdicts. The experiment maps instruction bytes at the proposed library flash
address and places the GOT at three offsets in simulated process RAM. A target
hook checks registers, stack pointer, stack arguments, return address and flags;
a return hook establishes completion within 32 instructions.

Three candidate cases pass. Eight mutation cases reject: replacing the register
restore with stack adjustment loses arguments; replacing `bx ip` with `blx ip`
loses the caller's return address; and substituting one fixed GOT address fails
at the two other placements. That last mutation passes at its matching placement,
which demonstrates why a single application layout cannot establish process-local
ownership. Its matching case is a positive control, not another candidate result.

GNU `as -mcpu=cortex-m0plus` establishes accepted instruction encodings.
Unicorn establishes the exercised register behavior; its Cortex-M0 selection
alone does not establish instruction legality. Neither observation establishes
ROM arithmetic, peripheral behavior or physical-board performance. Local source,
objects and execution inputs remain in `.build-logs/shared-rom-dispatch`.

## Cold resolver obligations

`ROM_FLOAT_TARGET_DESCRIPTOR` in `lib/libc/arm/gen/rom_float.inc` embeds an
absolute writable-cell address after a `bl`. The resolver in
`lib/libc/arm/gen/rom_float_resolver.S` reads that address twice: once for its
user-window/alignment check and once when publishing the resolved ROM target.
Both reads need the same shared-state interpretation. Replacing only the hot
load leaves a flash descriptor tied to one executable's state placement.

A shared descriptor can carry the cell's GOT displacement while retaining the
eight-byte descriptor shape and four-byte descriptor alignment. Its resolver
then loads the cell address through `r9` before applying the existing writable
window checks and before publication. The static descriptor and resolver remain
paired under their existing interpretation. Integration requires a test that
rejects mixed descriptor/resolver modes and malformed GOT displacements before
any out-of-range load or write.

The cold trampoline stores the caller return address in `ip`, and the resolver
saves that address before ROM lookup. That lifetime remains distinct from the
hot candidate's temporary target in `ip`. Single-precision dispatch and the
four integer-conversion callers of the CLZ cell need their own live-register
checks. ROM-version rejection, target validation, cache publication and a second
hot call require execution witnesses before this candidate enters shared libc.
