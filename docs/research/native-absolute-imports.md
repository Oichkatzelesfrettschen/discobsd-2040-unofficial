# Native absolute imports and relocation lifetime

The native linker represents both an absolute symbol and an already-applied
branch displacement with RABS. `relthumb()` must inspect the incoming relocation
state before symbol resolution changes that marker. A newly resolved absolute
target requires displacement calculation; an incoming RABS instruction already
contains its final displacement.

## Partial links and symbol capacity

A fixed target remains stationary when a later link moves its caller. Partial
links therefore retain the external symbol index and original branch addend.
Final links calculate the displacement from the final instruction address.
The three Thumb forms are BL, unconditional B, and conditional Bcc.

Absolute definitions remain outside the 500-entry per-input undefined-symbol
table. A relocation that refers to an absolute definition resolves its original
symbol index from the input symbol stream and matches its name, type, and value
against the global table. This preserves the 1,500-entry global capacity while
using bounded temporary state. Each fallback lookup scans preceding symbol
records and the global table; repeated absolute relocations can repeat this
work. Lookup acceleration requires its own measured storage decision.

Word assembly uses unsigned shifts. Word, symbol-type, and symbol-name reads
require complete input before publication. A zero symbol-length byte or EOF at
the next length field retains the existing end-of-symbol-stream convention;
complete container-bound validation remains a separate obligation.

Sparse relocation records require complete flags, address, and external index
fields. The address reader uses the same unsigned word assembly. Before reading
an external index, the relocation loop requires the entire eight-byte record
inside the declared relocation extent. Before applying a patch, it requires
`addr <= len` and `width <= len - addr`, where BL and absolute words occupy four
bytes and B/Bcc occupy two. Subtraction follows the first comparison, avoiding
an overflowing end-address calculation. Absolute-word patch reads also use the
unsigned word reader.

Section copying rejects premature EOF rather than synthesizing zero bytes.
`alignreloc()` in the assembler and `finishout()` in the linker emit zero
padding to a four-byte boundary. The relocation reader accepts at most three
trailing bytes and requires every declared padding byte to exist and equal
zero.

`readhdr()` establishes the containing file's extent and, for an archive input,
requires the declared member size to fit the file. It subtracts the header,
text, data, both relocation tables, and symbol table from that bounded extent.
Each subtraction requires sufficient remaining bytes, so subsequent section
offset sums fit the admitted container without wrapping. BSS occupies no input
file bytes. Both symbol-loading passes use a remaining-byte counter and admit
a symbol only when its length, type, value, and name fit that counter. An empty
table consumes no trailing bytes; a table ending at a complete record boundary
also stops without reading beyond its extent. A zero terminator requires the
remaining declared bytes to be zero padding. Archive directory parsing retains
separate validation obligations.

`finishout()` increments the symbol extent only when it emits a padding byte.
Incrementing the count in the loop condition also increments it on the final
false comparison, causing the next alignment calculation to declare four bytes
that the file does not contain. The partial-output witness independently sums
the header's file-backed extents and compares them with the physical file size.

## Executable evidence

`tests/native_linker/readers.py` compiles the actual linker reader functions
under GNU17 with warnings as errors and undefined-behavior sanitization.
Its 456 inputs include complete words with the sign bit set, symbol names of
1, 3, 63, and 255 bytes, and every truncated prefix of those records after the
length byte. Relocation inputs cover high-bit addresses, every truncated prefix,
last-fitting and crossing patch offsets for all four formats, and an external
record crossing its declared table extent. Section cases cover every short
prefix of an eight-byte section and absent, nonzero, or excessive padding.
Twelve independently compiled mutations
remove short-word, short-type, short-name, relocation-flags, relocation-index,
record-extent, section-span, section-copy, or padding rejection, or restore a
signed high-byte shift.
Every mutation must violate its named input's output or diagnostic contract.

`tests/native_linker/branches.py` assembles real objects and runs 126 tool
commands. BL, B, and BEQ resolve a near absolute target, reject a distant flash
target, retain the target after two partial links and a 16-byte caller shift,
and accept an absolute definition supplied only at final link. Capacity cases
admit 501 and 800 absolute definitions and mixtures with 499 or 500 undefined
symbols; a mixture with 501 undefined symbols retains its overflow rejection.
The unmodified linker fails the first near-target comparison. Additional cases
cover exact signed displacement limits, positive and negative instruction
addends, ordinary text symbols, already-applied relocations, and data words.
A conflicting absolute definition must report the retained symbol name and
exit with an error. The diagnostic uses global symbol storage after the input
symbol name's allocation is released. Ten oversized section declarations and
short/oversized archive-member declarations reject before section use; a matching
archive member links successfully.
Eleven shortened symbol extents reject a crossing record. Empty and exactly
filled tables preserve their boundaries, and a nonzero padding byte rejects.
`tests/native_linker/calibrate.py` compiles three mutations that skip newly
resolved absolute branches, apply absolute branches during partial links, or
charge absolute definitions against the undefined-symbol table. Two additional
mutations disable container admission or inflate the output symbol extent. A
sixth mutation disables the symbol-record bound. Each must fail
its named regression. Successful
receipts retain source/tool hashes and command outputs beneath `.build-logs`.

The conflict-diagnostic witness follows the capacity witnesses. The injected
absolute-symbol-table mutation also bypasses definition-conflict detection;
placing that diagnostic first masks the capacity failure that calibrates the
mutation. The complete positive run still requires both contracts. The
registered gate passes every reader, command, and mutation above with this
ordering.

Run the probes after building the host linker and assembler:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-native-linker
```

The reader probe requires a host C compiler with UBSan and, on Linux,
pkg-config and libbsd development files. The branch probe accepts explicit
`--linker` and `--assembler` paths for negative controls or compiler comparison.
Its source hash identifies the worktree source; an externally supplied linker
has its own independent executable hash.

## Remaining qualification

The candidate remains unmerged. The logged build completes successfully after
the container and symbol-boundary repairs on the branch based on `f8f536b9`.
Ruff accepts the native-linker Python
probes. The relocation reader and section-span repairs compile for Cortex-M0+
under GNU17 with warnings as errors, including the container admission and
output-size repairs. Archive directory parsing remains outside this candidate's
validated input contract. Matched
clean distributions must measure the shipped linker and all constrained
surfaces before any growth exception receives approval. Native callable
veneers for the SRAM-to-flash distance remain a separate shared-library
requirement; range rejection alone supplies no callable flash import.
