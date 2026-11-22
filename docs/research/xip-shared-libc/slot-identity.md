# Shared-libc callable identity

The shared ABI requires one public function address across application imports,
initialized pointers and pointers returned by libc. Each slot also owns its
Thumb function type and exact ELF extent. The fixed flash reservation remains
49,152 bytes; a successful identity fixture supplies no image-fit claim.

## Mechanism and deciding experiment

GNU ld assignment of a public function symbol to a slot preserves the original
provider's function size. The assignment rejection control reproduces that
extent mismatch for a 12-byte slot. The fixture separates those owners:
the provider has a private implementation alias, its public definition becomes
an unresolved import, and the slot supplies the public definition and extent.
The rewrite changes symbol metadata while preserving section bytes and relocation
indexes. A weak public alias shares its canonical slot with its owning function.

GCC's `-fno-semantic-interposition` can preserve a direct local implementation
call while public address expressions retain their public symbol. The fixture
checks observable pointer and call behavior with and without that option and
after LTO lowers to native relocatable ELF. ELF inspection rejects unsupported
objects and conflicting implementation ownership before writing the separated
object. Every declared alias group has one implementation section, Thumb entry
and extent; individually consistent public/private pairs cannot override that
group contract.

The experiment lives in `tests/libc_xip_slots`. Its symbol separation remains a
test mechanism until full API selection and production build closure establish
all provider and alias ownership. The generated production ABI, native imports,
kernel admission, lifecycle, signal/fork/swap qualification and workload timing
retain their independent requirements.

For flash origin F, slot index i and stride 12, the public Thumb address is
`F + 12*i + 1`. Every name in an alias group has that address. The implementation
alias retains the provider's separate Thumb entry. Public address expressions
resolve through the slot symbol; compiler-local implementation references can
retain their direct call. Only symbol-table entries change during separation.
Section contents, string tables and relocation indexes retain their identities.

Each 12-byte slot saves r3 and LR, loads its implementation address, replaces
the saved return-address word with that target, and restores r3 while branching
through PC. LR retains the original caller return address. Eight temporary
stack bytes expire before the implementation starts. Each eight-byte slot saves r3 and LR and calls a common dispatcher. The
linker writes the Thumb implementation offset in the halfword immediately
after the call. The dispatcher reads that offset through LR, adds the flash
base, restores the caller's LR and transfers control with the original SP.
The compact path uses 16 temporary stack bytes.

## Replay and evidence boundaries

The root gate supplies eight compiled images and eighteen required rejection
controls. A selected Python needs `pyelftools` and Unicorn 2 or later; ARM GNU
GCC, binutils and its LTO plugin supply compilation and linking. Run:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-libc-xip-slots
```

Each successful image executes 222 calls. The 1,776-call matrix covers two
independent library-state areas, three aligned stack placements, zero values,
ordinary values and unsigned overflow boundaries. An application linked at a
separate SRAM address imports a symbol-only ELF retaining the public function
type, Thumb entry and extent. Direct calls, pointers, weak aliases and callbacks
exercise the same slots. The linker supplies any application far-call veneers;
native-linker veneer generation remains a separate work package.
Every imported public address also equals its inspected library definition.
The C-locale linker collision oracle requires the intended public symbol; an
unrelated private-symbol error supplies no public-interposition evidence.

Fresh UUID evidence directories under `.build-logs/xip-callable-identity-*`
retain commands, diagnostics, native/LTO objects, separated objects, both linked
ELFs, source hashes and a terminal receipt. The separation receipt records the
original and rewritten object hashes and each reviewed implementation owner.
The source hash set includes every direct fixture input. The Git revision and
installed compiler/module versions identify the environment; a dirty worktree
requires direct source-hash comparison before using its receipt.

The [testing specification](../../../sys/arch/rp2040/doc/TESTING.md) records
the root gate and Linux `check-qemu` owner. Production source and executable
formats stay unchanged. The fixtures use explicit GNU alias attributes under
strict C17 compiler diagnostics; they supply no portable alias-language or
Smaller C claim. Unicorn executes ARM instructions with modeled memory. Kernel
admission, process scheduling, swap, MPU protection, actual runtime resource
peaks and physical-board timing remain unmeasured.

## Implementation plan

The approved C17/POSIX roadmap owns the program. This plan expands its callable
identity prerequisite into executable behavior and rejection controls.

- [x] Define C and assembly providers, weak aliases and cross-object consumers.
- [x] Calibrate the public symbol/type/extent oracle against the retained failing
  linker-assignment mechanism and a separately named implementation control.
- [x] Implement checked symbol separation with unchanged section/relocation bytes.
- [x] Exercise canonical pointers and six-argument calls across two independent
  process-state areas, stack placements, compiler visibility choices and LTO.
- [x] Reject omitted separation, mismatched provider ownership, public-definition
  collisions, split aliases, incorrect extents, bypassed pointers and wrong calls.
- [x] Register the gate with the root Makefile, testing specification and Linux
  `check-qemu` CI owner; run focused qualification and existing adjacent gates.
- [x] Record exact source/tool identities and bounded review before PR integration.

The [source review record](slot-identity-review.md) binds the approved immutable
review and the parent's executed counterexamples and repairs. The initial
fourteen-control result leaves three validation gaps. Their reproductions and
added controls establish the seventeen-control qualification boundary.

The [qualification receipt](slot-identity-qualification.json) binds clean source
`16d9a0fb` on main `d9ed7d75` to four images, 888 successful calls and seventeen
required rejections. Each finite image occupies 224 text/read-only bytes plus
a 40-byte initialization image, totaling 264 flash bytes, and 40 state bytes.
Those fixture costs supply no full-library capacity estimate. The merged-source
logged build passes at `4c5b8145`; the later source-set correction changes only
the ledger. Adjacent object-ABI, ROM-dispatch, native-veneer and 33-test archive
closure checks, denominator calibration and changed-comment/text checks pass.

The integrated-main denominator initially rejects `bin/sh/tests/board_break.c`,
which main tracks without a ledger row. The explicit source-set generator adds
that single row, retains its unreviewed state and restores complete path
accounting. The failed invocation remains in the qualification receipt.
The full aggregate, dedicated ILP32 inventory and local Renode gates remain
unrun for this test-only change. Hosted CI retains its own head and execution
scope; physical-board execution remains unrun.

An unexpected pointer or extent falsifies the proposed ABI mechanism. An emulator
setup error remains a failed execution; it cannot count as mutation rejection.
Instruction execution establishes functional behavior for the exercised fixtures,
with no kernel, cycle, MPU or physical-board claim. The mechanism uses established
ELF aliases, relocation and Thumb calls; novelty stays limited to their verified
composition under the DiscoBSD ABI.

## Import symbols for the 33-program member envelope

The diagnostic image in `shipped-member-envelope.json` assigns 368 function
names to 350 eight-byte slots. Its public ELF symbols point to implementation
entries. A separately linked application obtains its callable addresses from
`imports.elf`. Each exported name has ELF type STT_FUNC, an eight-byte extent,
and the Thumb address of its slot. Aliases share that address. The import file
contains only the recorded callable names. The 27 object names still require
the process-state ABI.

Run the following commands from the repository root. Set `REPLAY` to the
qualified PIC replay directory and `LINK` to a fresh path under `.build-logs`.
The first command produces the startup fixture and linker template. The second
prints the directory containing the compact image, link map, and import file.

```sh
export PYTHON=${PYTHON:-python3}
"$PYTHON" docs/research/xip-shared-libc/export-closure/link-pic-envelope.py \
    --replay "$REPLAY" --output "$LINK"
"$PYTHON" tests/libc_xip_slots/relink_envelope.py "$REPLAY/libc-pic.a" "$LINK"
```

The replay checks the input archive, startup fixture, linker template and final
image digests. It decodes each slot's target halfword, compares it with every
implementation alias, and verifies the exact import symbol set after stripping.
Pass the resulting file to the application linker with `--just-symbols=imports.elf`.
A Cortex-M0 model executed four direct and indirect SRAM caller invocations of
`rand` across two process states. Importing the image's implementation address
instead caused the application pointer check to fail. This experiment covers
application-side identity for that caller. Production slot numbering, internal
library references and native a.out loading require separate verification.

### Signal trampoline at the user-to-kernel boundary

The diagnostic envelope's `sigaction` wrapper loads the linked Thumb address
of `sigtramp` into r3. A Cortex-M0 instruction probe stopped immediately before
SVC 31 and read 0x1002878d from r3. The corresponding application import is
0x10020899. The wrapper's literal pool thus exposes the implementation address
at the syscall boundary. In the same image, allocated R_ARM_GOT_BREL references
in text reach three other callable implementation addresses: `__doprnt_cvt`,
`fclose` and `fflush`. A relocation inventory identifies these sites; each
consumer still needs a binding decision and a behavior check.

The kernel sets handler LR from the registered trampoline. On handler return,
the sigreturn SVC stacks an eight-word hardware frame; the kernel reads the
saved context at the stacked SP plus 32 bytes
(`sys/arch/rp2040/rp2040/sig_machdep.c`). In a user-instruction probe a
handler returned with `bx lr` to each candidate entry. At the first sigreturn
instruction, both the implementation and the compact dispatcher preserved the
handler's SP, r0 through r12, incoming LR and the 96 sentinel bytes starting
at SP. Only the compact dispatcher wrote the 16 bytes immediately below SP.
These bytes fall inside the subsequent exception frame. The probe stopped
before exception entry.

A diagnostic substitution placed the implementation's two halfwords, SVC 103
and SVC 1, directly in the eight-byte slot. The handler reached SVC 103 with
the bytes below SP intact. This candidate depends on the complete trampoline
being a position-independent four-byte sequence. A link-time extent and
instruction check, binding the registration literal to the same slot, and
target OS signal delivery and return remain open.

A separate diagnostic link isolates the original trampoline instructions in
an executable input section and places that section at slot 275. The four-byte
function and four bytes of padding occupy the slot. The linker's ordinary
ABS32 relocation binds the wrapper's registration literal to the function.
At SVC 31 the modeled r3, linked Thumb symbol and application import all equal
0x10020899. A handler returning through that address reaches SVC 103 with
its register and stack checks passing. The image occupies 48,488 flash bytes
and 5,040 process-state bytes, eight fewer flash bytes than the dispatcher
image. The 136 interleaved state calls also pass on the relinked image.
The experiment uses a diagnostic assembly-section change and linker script;
the shipped static archive and native signal path have separate qualification.

The combined diagnostic link also separates the three stdio implementations
from their public slots. Each GOT cell holds its public slot address, and
each slot's target halfword resolves to its retained private implementation.
In two modeled process states, the argument to `_fwalk` from `fflush(NULL)`
equals the `fflush` slot (0x10020391), and the argument from `_cleanup`
equals the `fclose` slot (0x10020379). The `__doprnt_cvt` GOT cell holds
0x10020109; this check covers the linked provider, leaving weak-provider
absence and floating formatting behavior for separate execution tests.
The combined image measures 48,488 flash bytes and 5,040 process-state bytes.

A linker-script SHORT expression can encode an offset even when section
garbage collection discards the referenced implementation. A control link
rooting only the public names lost the internal `fflush` GOT reference.
Adding all three private names as explicit roots restored their definitions.
An import identity check therefore needs both the public address and a live
executable implementation whose address matches the slot target.

A finite formatting check exercises both bindings through the public
`snprintf` slot with `%a` and binary64 value 1.5. With the provider linked,
each of two process states enters the converter slot once, produces
`0x1.8p+0`, and returns 8. A separate link omits the provider member, its
public and private roots, and its slot symbol while reserving the same eight
bytes. Each process then produces `?`, returns 1, and bypasses that slot.
The provider-present run requires an unsigned quotient/remainder register
model for the formatter's exponent conversion. The original CPU-only probe
stopped on the dividend write to the unmapped SIO address. The model covers
the exercised arithmetic and leaves divider timing and silicon unmeasured.

## Native a.out diagnostic caller

After the RP2040 host tools build, the following command links application
objects and archives in their supplied order and prints the resulting OMAGIC
image path. The inputs must be native Thumb a.out files. The resulting image
requires the measured compact slot layout and an initialized per-process r9
on entry.

```sh
export PYTHON=${PYTHON:-python3}
"$PYTHON" tests/native_linker/xip_imports.py main.o companion.a
```

A zero-origin relocatable link resolves application definitions before import
selection. An unresolved direct Thumb call to an envelope function selects a
local veneer; its data relocation still resolves to the odd flash address.
Aliases share a veneer when the caller references both names. A pointer-only
import consumes zero veneer bytes. With an archive supplying its own
implementation of `rand`, the measured application had the same 132 loaded
bytes as the ordinary single-pass link. With the provider omitted, the main
object and companion archive produced a 112-byte text and four-byte data
image. Direct and indirect calls returned the expected random sequence across
two modeled process states. Default-origin partial linking applied the SRAM
base twice. The diagnostic prelink uses `-T0 -r` to give symbols a
zero origin. The final link allocates common storage and defines segment ends.

This host experiment uses the diagnostic member envelope. Kernel admission,
process initialization and production import numbering remain open.

The optional state reservation binds 29 object names from the compact ELF
identified by the slot record's SHA-256. The link keeps the read-only character
table at its flash address and reserves 5,040 bytes at the eight-byte-aligned
tail of process BSS. Application data, common storage and BSS precede that
reservation. The first heap address is its end. The link reserves storage;
startup must still populate its initialized bytes, rebase pointers and set r9.

With the compact ELF from the envelope replay, run:

```sh
"$PYTHON" tests/native_linker/xip_imports.py --state-image slot.elf main.o companion.a
"$PYTHON" tests/native_linker/xip_imports_test.py --state-image slot.elf
```

The second command reads ELF symbols with GNU readelf and checks relocated
pointer words in two native images requiring zero and four padding bytes.
A changed ELF digest and an application definition of `environ` exercise
the two rejection paths. These checks measure host linker output.

For the same compact ELF, generate and exercise its Thumb cold-start routine:

```sh
"$PYTHON" tests/libc_xip_slots/state_entry.py slot.elf > state_entry.s
"$PYTHON" tests/libc_xip_slots/state_entry_test.py slot.elf
```

The caller passes the reserved state address in r0 and installs the returned
GOT address in r9. The routine copies the 1,600-byte flash template, clears
the remaining 3,440 bytes and adds the process base to 77 pointer cells.
The assembled routine, literals, alignment and halfword offsets occupy 240
bytes after the 48,496-byte diagnostic image, leaving 416 bytes in the
reservation. Unicorn executes the routine at that flash address for two
poisoned SRAM regions. The replay checks their bytes, guard words, return
address, stack pointer and saved registers. Duplicating one relocation
offset changes the resulting state bytes. Kernel exec admission and the
startup call from a shipped application remain open.

### Native executable requirement

A native link with `--state-image` emits `shared.aout`. Its OMAGIC header
uses flag 0x02 in the six-bit flag field. A 56-byte requirement follows
the 32-byte a.out header and precedes the original text, data and symbols.
The record occupies file space only. The shipped kernel rejects the shared flag. The plain diagnostic link still emits its original
host measurement fixture.

| Byte offset in requirement | Width | Little-endian field |
|---:|---:|---|
| 0 | 4 | ASCII XREQ |
| 4 | 4 | Version 1 |
| 8 | 4 | Record length, 56 |
| 12 | 32 | Exact compact ELF SHA-256 bytes in digest order |
| 44 | 4 | Required slot prefix length, zero for an empty set of imported functions |
| 48 | 4 | Absolute SRAM address of the aligned state reservation |
| 52 | 4 | State extent in bytes |

The prefix includes the highest referenced slot and every preceding slot.
Both direct calls and initialized function pointers contribute; application
definitions resolved during the prelink do not. The image digest pins the
diagnostic implementation and its state layout. Production ABI identity and
kernel admission require separate qualification.

The host packer retains the requirement between the a.out header and the
48-byte heatshrink header, and sets both the packed and shared flags. Its
header CRC covers all three headers in file order. Expansion preserves the
shared flag, requirement and loaded text and data. The native startup caller
with 80 text bytes occupies 204 packed bytes and expands to 168 bytes with
its two headers. Pack and unpack check the state geometry against the
RP2040 window. The provider check compares all 32 identity bytes, the slot
prefix and the state extent against an independently supplied library
description. The caller of the layout check supplies the space available
after accounting for the process stack. The shipped kernel's format dispatch
continues to select static executables.

### Shared libc provider members

In the shared compilation of `flsbuf.c` and `doprnt_float.c`, hidden
implementation aliases retain the code for `fflush`, `fclose` and
`__doprnt_cvt` when symbol separation gives the public names to their
slots. The shared ARM assembly emits `sigtramp` into an independently
placeable four-byte section. Static compilation places those instructions
in the wrapper's text section and omits the three implementation aliases.

A diagnostic relink with 64 bytes reserved before the first callable slot
and the cold-start routine after the template occupied 48,792 bytes. Its
360-byte margin applies to the 33-program member envelope. In two modeled
process windows, startup returned the GOT, `sbrk(0)` returned the end of
state and the first `rand` returned 16838. At the entry to `_fwalk`, the
callback argument matched the public slot for each of `fflush` and
`fclose`. At the registration SVC, r3 held 0x100208d9, the shifted
public signal slot. This experiment exercised user instructions through
registration; kernel signal delivery requires its own execution evidence.
