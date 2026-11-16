# Native shared-call veneer mechanism

The native assembler and linker can construct an SRAM-resident call veneer
that transfers control to the proposed libc flash interval while preserving
arguments, the return address, and the process-local PIC base. This establishes
the instruction mechanism; automatic veneer insertion and a shared executable
ABI remain unimplemented.

## Register and stack invariants

`ROM_FLOAT_DOUBLE_TARGET` in `lib/libc/arm/gen/rom_float.inc` already uses a
saved stack word as a branch destination. A fixed-address import needs one
literal load rather than the ROM trampoline's cell load followed by an
indirect load:

```asm
    push {r3, r4}
    ldr r3, =target
    str r3, [sp, #4]
    pop {r3, pc}
    .pool
```

For entry stack pointer S, the push stores r3 at S-8 and r4 at S-4. The
store replaces the saved r4 word with the odd Thumb destination. The pop
restores r3, loads PC, and restores SP to S. Register r4 itself never changes;
its saved copy provides temporary storage. LR retains the caller's BL return
address. Registers r0-r12, including r9, and condition flags remain unchanged.
Arguments at S and above retain their addresses and values. The transient
stack extent is eight bytes, preserving eight-byte alignment at every step.

The four halfword instructions and one aligned literal occupy 12 bytes. That
encoding count differs from executable growth: `middle()` in `usr.bin/ld/ld.c`
rounds complete text to 16 bytes. The first fixture contains 20 bytes of input
text and 32 linked text bytes. For an already four-byte-aligned aggregate text
extent T and N appended veneers, the final text delta is
`align16(T + 12*N) - align16(T)`. Resource admission must measure complete
images rather than multiply the encoding count alone. The eight-byte stack
cost also remains separate from image storage.

## Function-address identity and insertion boundary

A public wrapper with the imported function's name supplies an SRAM address
to application address-taking, while libc can retain the flash function
address. Those pointers denote the same interface but compare differently.
The native shared linker must therefore retain the canonical absolute flash
symbol for data relocations and function pointers and redirect only eligible
branch relocations through an internal veneer. The fixture's separate data
word retains the flash target while its call uses a local bridge.

`relthumb()` already distinguishes branch formats from `RTABS32` and preserves
absolute external identity during partial links. Shared final-link insertion
must occur after required imports are known and before `middle()` fixes data,
BSS, and end-symbol addresses. Partial links retain the original import and
addend, avoiding publication of a final veneer placement. Static output keeps
its existing range checks. A shared mode must reject unsupported imports and
branch forms explicitly.

The fixed 192 KiB process window fits within the native BL displacement range.
Short unconditional and conditional branches have narrower ranges; an appended
veneer alone does not establish their reachability. Stable slots, canonical
function addresses, nonzero branch addends, alias identity, veneer ownership,
and overflow-checked final layout require integration tests before automatic
insertion ships. The public object reservation and versioned exec admission
retain their separate implementation contracts.

## Calibrated evidence and replay

`tests/native_linker/veneers.py` assembles and links eight fixtures with the
production native tools. Two target addresses exercise the beginning and end
of the proposed 48 KiB flash interval; four caller paddings exercise literal
alignment. Each execution checks register values, stack arguments, LR, flags,
return control, and the temporary stack extent. Each successful veneer must
match bytes assembled independently by GNU assembler for Cortex-M0+.

Three runtime controls replace the veneer with an r3-clobbering jump, overwrite
the wrong saved stack word, or use an even Thumb target. Each requires its
named failure. A GNU assembler control must reject Thumb-2 MOVW under
`-mcpu=cortex-m0plus`.

Unicorn's selected Cortex-M0 model executes MOVW in the measured environment.
The receipt records that observation separately from the passing veneer
behavior. Emulator selection alone therefore supplies no ARMv6-M instruction
exclusion claim. GNU assembler's matching bytes and rejected control supply
the instruction-legality evidence. The experiment exercises neither silicon
timing nor kernel signal delivery during the veneer.

Run after the logged build:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-native-call-veneer
```

The gate requires Unicorn 2 or later, ARM GNU binutils, and the built native
assembler and linker. Linux CI owns it through `check-qemu`. Each invocation
retains assembly, linked images, GNU encoding controls, tool hashes, the probe
hash, Unicorn version, and a receipt under a unique `.build-logs` directory.
The [shared-libc contract](xip-shared-libc/README.md) owns the complete release
and admission obligations. This mechanism reuses an existing trampoline
invariant and establishes no new algorithm or completed shared-link mode.
