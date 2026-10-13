# Typed kernel transfer interfaces

`kern_subr.c` defines uiomove, ureadc, uwritec and uiofmove with parameter
types inside their function signatures. The definitions match the existing
KERNEL declarations in `sys/sys/uio.h`. The migration preserves function
bodies, caller interfaces, object layouts and error/accounting behavior.

| Function | Preserved parameter contract | Preserved result |
| --- | --- | --- |
| uiomove | caddr_t, unsigned int count, struct uio pointer | copy/accounting result |
| ureadc | int byte, struct uio pointer | zero after one byte |
| uwritec | struct uio pointer | unsigned byte 0..255 or -1 |
| uiofmove | caddr_t, signed int count, struct uio and iovec pointers | zero after copy; caller owns accounting |

Each parameter is a pointer, int or unsigned int. Moving these types from
old-style parameter declarations into prototypes introduces no narrow-argument
promotion transition. ureadc retains int input and narrows when storing the
byte. uiomove retains unsigned count while uiofmove retains signed count.
The Pico SDK register/ROM conventions and target attributes stay outside the
changed interface family.

## Caller closure and inherited preconditions

`rg -n '\b(uiomove|ureadc|uwritec|uiofmove)\s*\(' sys` enumerates source
uses. uiomove serves inode I/O, terminal/pty copying and log reads; ureadc
serves terminal and pty byte delivery. The sys tree contains definitions and
public declarations for uwritec and uiofmove, with their behavior exercised by
the exact-source host fixture. Graft reports ambiguous call edges for uiomove
because the header and a test shim share its name; the raw source search
supplies the bounded caller inventory.

uiomove assumes a valid segment list, representable lengths, and residual
accounting consistent with remaining segments. It neither caps each copy by
residual nor validates an exhausted segment count. ureadc requires available
residual and segments; exhausted input reaches the existing panic path.
uiofmove requires a nonnegative count and sufficient source/destination
storage, and leaves accounting to the caller. Offset addition requires a
representable resulting off_t. These inherited preconditions remain explicit;
definition typing does not claim to repair invalid caller state.

## Verification

The existing uio_test links the exact kernel source and exercises both copy
directions, segment traversal, empty segments, partial transfers, zero
count/residual, unsigned byte widening, panic paths and uiofmove accounting
preservation. Its source compile adds `-Wold-style-definition` under Werror.
A baseline compile rejects all four original definitions; the typed source
passes with identical flags. The broader kernel ILP32 tier exercises rwuio
and its interfaces, while the exact uio_test fixture executes at native host
width. Cross kernel builds establish the target-width compilation boundary.

Select and export PYTHON, then run sequentially:

```
bmake MACHINE=rp2040 check-kernel check-kernel-ilp32
bmake -j4 MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check-c17-kernel-inventory check-kernel-printf-formats
bmake MACHINE=rp2040 check
```

The two-oracle inventory combines lexical scanning and compiler diagnostics
from PICO/PICO_UART. Its calibration runs before comparison with the retained
ledger. The ledger removes only the four kern_subr.c definition rows; the
source bodies and public declarations retain their contents. The unit keeps
host behavior, cross compilation, linked footprint and board behavior as
separate evidence classes.

The comparison base is `048c31f5`. Baseline and candidate use arm-none-eabi
GCC 16.2.0. After `arm-none-eabi-objcopy --strip-debug`, both configurations'
kern_subr.o files share SHA-256
`8c94dacfcdf83ea66ee9fca602631b674bc7dd201b444ec63955b42df854d41e`.
The object includes 340 text bytes and zero data/BSS. Debug information changes
with the source declarations; executable instructions and retained object
metadata compare byte-identically after the stated removal.

| Linked section surface | Before | After |
| --- | ---: | ---: |
| PICO text/data/BSS | 104646/248/39848 | 104646/248/39848 |
| PICO_UART text/data/BSS | 92220/192/14832 | 92220/192/14832 |

The stale ledger fails on the exact four removed rows before regeneration.
Scanner calibration covers 52 marked findings and rejects known-bad scanner
and reconciliation variants. The regenerated inventory contains 191
definitions and four declarations across 28 files, with compiler coverage
of 51 of 120 requested sources. Remaining conditionals retain lexical
coverage; those counts describe the selected kernel inventory, not the full
repository's C17 completion. The format gate accepts 166 literal formats in
171 sources after its independent negative-control calibration.

All 275 installed executable text/data/BSS/file-size records match the
baseline; 274 executable hashes match. pdc differs in its compiled-time
string from `usr.bin/pdc/pdc.y`, which embeds __TIME__ and __DATE__. Full
and selected a.out archive hashes also differ: ordered header/payload
comparison confines every changed byte to member timestamp fields and
preserves all 370 full-archive and 152 selected-archive payloads, including
symbol indexes. Packed payload sizes and 281 free root blocks match. These
observations bound build nondeterminism rather than assigning it to the
kernel definition changes.

The fresh distribution and serialized full non-hardware aggregate pass.
The exact kern_subr fixture reports 99 passing checks. All 18 Smaller C
programs pass after the scanner, directory, case-fold, formatter, scratch and
kernel units; the copied filbuf archive mutation fails and the restored
archive passes. Public Smaller C input retains its header constructs and
layouts. That final native-consumer receipt closes roadmap item 11's hosted
compile/link/execution boundary; on-device preprocessing and physical-board
execution remain open. Existing host shell/libtcl link diagnostics name
tmpnam, getwd and mktemp.
