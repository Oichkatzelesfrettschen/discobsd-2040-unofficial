# Full-library state, sizing and callable-slot experiments

Source `8181737031bc0b6ff52c247f65f728840c313303` preserves the implementation
at `66ce7c5b57a691fed604f26d14449996f0887743` and adds qualification records.
The experiments below retain their own ELF identities and compiler inputs.
They supply prerequisites for the shared ABI; production integration remains
open. The fixed 49,152-byte reservation stays unchanged.

## Linked image and compiler options

The normal 449-member PIC archive retains all 646 global definitions and
occupies 62,396 flash bytes including its initialization image, with 5,648
bytes of per-process state. Hidden visibility leaves flash unchanged and adds
eight state bytes. The earlier calendar PIC receipts own those measurements.

A whole-archive LTO link fails to extract compiler helpers. Four minimal cases
isolate archive input: direct objects succeed with and without section
collection, while whole-archive input fails in both cases. The direct full
link resolves the helpers. This observation bounds a toolchain interaction;
it does not establish the compiler plugin's internal cause.

The installed GCC ARM Options manual specifies r9 as the single PIC base for
EABI targets. Omitting the redundant explicit `-mpic-register=r9` option
removes the LTO diagnostic in the minimal probe; its disassembly retains the
same r9 addressing. Replaying all 449 members with the documented default,
then linking direct objects with `-flto=1 -flto-partition=one`, completes
without diagnostics and retains all 646 globals. Compilation also uses
`-flto -ffat-lto-objects -DDISCOBSD_SHARED_LIBC=1 -fPIC -msingle-pic-base
-mno-pic-data-is-text-relative` in addition to the captured target arguments.

The resulting image occupies 62,220 flash bytes and 5,344 state bytes, reducing
the normal image by 176 and 304 bytes respectively. Its initialized image is
1,536 bytes and its GOT is 648 bytes. The image still exceeds the reservation
by 13,068 bytes before stable slots. The compiler choice supplies no accepted
resource margin or complete relocation proof.

A separate non-LTO probe adds `-ffixed-r9` to otherwise identical single-base
PIC compilation. Both 32-byte text sections match byte-for-byte and retain
the r9 save/restore sequence. Explicit register reservation therefore supplies
no demonstrated reduction in that case; the production flags remain unchanged.

## State relocation and runtime witnesses

The exploratory ELF inspector classifies every referenced GOT cell and
allocated relocation under its supported forms. The normal image contains
97 pointer slots whose values require the process-state base and 242 pointer
slots whose values remain fixed. The LTO image contains 63 and 201. The
inspector rejects absolute mutable-state references in read-only sections,
misaligned or out-of-range GOT offsets, inconsistent GOT values and uncovered
GOT cells. Complete public-object type and alignment contracts remain open.

Calibration exposes an initialization-boundary defect in the first inspector:
a descriptor whose initialized image stops before the GOT is accepted.
Explicit checks require the whole GOT and every pointer slot to lie inside
the initialized image, and require that image to lie inside aligned state.
Both valid layouts remain identical and six malformed layouts reject. The
original generator and its failing control remain retained.

Unicorn executes each full linked image against two independent state
reservations. Each image passes 522 calls: six tokenizer calls, two overflowing
numeric conversions and 514 character classifications. Tokenizer continuation
belongs to its selected state; overflow returns the target long maximum,
advances the end pointer and sets the selected errno to ERANGE. Character
classification covers EOF and all 256 byte values against the C-locale ASCII
oracle. Every call checks the stack, callee-saved registers, PIC base and
unchanged inactive state. Three controls reject omitted state relocation,
reuse of the first process base and relocation of fixed flash pointers.

The first overflow call faults at SIO address `0xd0000060` because the model
lacks the hardware divider used by `lib/libc/arm/gen/aeabi_div.S`. A functional
register model computes unsigned quotient and remainder after the divisor
write, allowing the unchanged helper to execute. That model establishes
functional execution for these cases, without divider timing, interrupts,
kernel behavior or physical-board evidence. The mapped flash is read-only and
64 KiB wide to contain the oversized experimental image; it establishes no
48 KiB image admission.

## Installed-header discovery and provider bounds

The installed top-level and arpa denominator contains 54 headers. Standalone
Clang ARM C17 parsing runs in default, ISO-selected, Issue 7 and Issue 8 modes.
Each mode has 51 clean headers. `grp.h` lacks gid_t and size_t prerequisites,
`kmem.h` lacks dev_t, and `lastlog.h` lacks time_t. Including sys/types.h first
resolves those failures; repeated inclusion still fails for grp.h and lastlog.h.
System, netinet and machine subtrees remain outside this discovery denominator.

The default-mode successful headers declare 571 distinct external names; 385
have providers in the captured PIC libc archive. The other names need owner
classification across other libraries, startup and compiler facilities before
any absent-provider claim. A link rooted in those 385 names occupies 54,384
flash bytes and 5,480 state bytes. It exceeds the reservation by 5,232 bytes
before slots despite its incomplete public denominator. It is neither an
approved export contract nor permission to remove compatibility interfaces.

## Compact callable slots

An ARMv6-M fixture uses an eight-byte slot: push r3 and LR, BL a shared
dispatcher, then a 16-bit implementation offset. The dispatcher recovers the
offset through the slot return address, restores the original argument and
return registers, and tail-transfers to the implementation. GNU ld emits the
offset with SHORT after final symbol placement; assembler-time subtraction of
Thumb symbols requests an unsupported 16-bit PC-relative relocation.

The dispatcher occupies 28 bytes. Nine modeled calls across three slot/target
positions and three stack placements preserve r0-r12, original LR, target SP
and stack arguments. Temporary stack use is 16 bytes. Two runtime controls
reject an even target and a wrong destination. Link assertions accept valid
slots and reject an implementation outside the fixed 48 KiB reservation.
APSR flags remain call-clobbered, consistent with the function-call boundary.

Compared with a 12-byte transparent jump for every fixed slot, this encoding
saves `4*N - 28` flash bytes for N slots and uses eight additional temporary
stack bytes. This formula compares slot encodings only; image padding, native
application veneers and callee stack costs require separate accounting.
Canonical public function-pointer identity, internal implementation aliases,
removed slots, descriptor versioning, admission and workload timing remain
unimplemented. The encoding is a candidate mechanism, not a selected ABI.

## Integration boundary

Production acceptance still requires one authoritative ABI description,
complete export ownership, public types and alignment, stable callable
identity, executable-tail reservation, startup, native linking, kernel
admission and matched release images. Signal, fork and swap behavior, exact
flash fit and authorized board performance remain separate qualifications.
The experiments retain established PIC and dispatch mechanisms; their
contribution is target-specific composition and falsifiable evidence.

## Evidence identities

The [evidence index](full-library-state-evidence.json) binds nine retained
local receipts by SHA-256. Their `.build-logs` paths identify local evidence,
not files distributed with the repository. These exploratory fixtures remain
outside the production acceptance gates.
