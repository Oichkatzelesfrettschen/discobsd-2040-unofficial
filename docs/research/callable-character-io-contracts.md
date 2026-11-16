# Callable character I/O contracts

## Requirement and source boundary

Owned main `a706c0b851daaa25b019639e8217dda5ac17095d` supplies getc/putc
macros, callable fgetc/fputc, and callable getchar/putchar without their public
declarations. Standalone strict ARM C17 probes accept macro calls and existing
fgetc/fputc addresses, while getc/putc address-taking rejects. The archive has
no getc/putc definitions. Source lookup uses
`rg -n 'getc|putc|getchar|putchar' include/stdio.h lib/libc/stdio`;
the compiled archive inspection supplies the separate provider observation.

ISO C17 7.1.4 requires an actual function where a library function also has a
macro form. Parenthesizing its name suppresses macro invocation; taking its
address also requires a declaration and a callable provider. The character
I/O contracts in C17 7.21.7 and POSIX getc/putc/getchar/putchar require the
same byte operations as fgetc/fputc, with stdin/stdout selecting the standard
streams. Byte input returns unsigned-char values converted to int; byte output
converts its int input to unsigned char. EOF remains distinct from every byte.

The implementation unit supplies declarations for all six callable entries,
adds getc/putc providers in the existing fgetc/fputc archive members, and gives
the existing four definitions complete prototypes. Portable typed wrappers
delegate to fgetc/fputc. Public macro expressions, FILE layout, backend state
transitions and standard-stream identity retain their existing definitions.
This archive boundary avoids additional member headers and indexes for two
equivalent operations; actual code, archive and filesystem costs remain measured.

Compiler aliases offer another representation, but they add compiler/object
format and function-address obligations. Separate duplicate byte-operation
bodies add another implementation owner. Typed delegation preserves the existing
mechanism through a portable C17 call and gives archive conversion an ordinary
symbol. No claim depends on compiler inlining or tail-call behavior.

DragonFlyBSD revision `e0f962ddd750a94049c962632074b1b240ceb40e` owns separate
getc/fgetc and putc implementations around its internal byte primitives and
stream locks. FreeBSD `6871285f194d3f83fd270b4b5d5783503c2b86d6` adds
cancellation-safe lock handling; NetBSD
`43604aca965597330ebc7c4e39c288acc3a0431e` likewise serializes its byte read.
These related sources share ancestry. DiscoBSD retains its own byte backend
and single-threaded state contract; this unit imports no lock or cancellation
implementation and establishes no whole-interface concurrency claim.

The five DragonFlyBSD files `getc.c`, `fgetc.c`, `putc.c`, `getchar.c` and
`putchar.c` match the retained donor-ledger revision
`805ca132aededa6ae4c8b3499a2d5c934d78fe56` byte-for-byte. The donor ledger
records their individual SHA-256 identities and BSD 3-Clause notices.
The lookup uses `git show REV:lib/libc/stdio/NAME.c` at both revisions.
The mechanism has established prior art. The port-specific contribution is
portable provider closure within existing native members and a calibrated
macro/function correspondence over DiscoBSD's existing FILE backend.

## Prediction and deciding witnesses

Before production edits, parenthesized getc/putc and getchar/putchar addresses
must reject through missing declarations, and explicitly declared getc/putc
imports must lack providers. After the change, exact function-pointer types,
ordinary macro calls, parenthesized calls and address-taking compile in default,
ISO and POSIX selector modes. Smaller C retains its own header-input witnesses.

Behavior witnesses execute the actual character entry files and real
filbuf/flsbuf backend over controlled read/write dependencies. They compare
every byte with independent expected values, test output int conversion,
buffered/unbuffered/line-buffered transitions, EOF, read/write errors and
short writes, standard-stream selection, return values and indicator state.
Known-bad wrappers alter a byte, lose an error return or select the wrong
standard stream; each must fail its named witness. Runtime input arguments
are valid FILE objects in admitted states. Invalid stream pointers retain the
standard's undefined-behavior domain rather than receiving a new error policy.

The header/compiler matrix and behavior/backend matrix supply different
evidence. Native input, native archives, ELF linkage, host execution, ILP32,
ARM QEMU, Renode and physical execution retain separate claims. Matched clean
resource captures decide every constrained cost; a positive delta requires the
compatibility profile's exact owner decision. Complete stdio visibility,
concurrency and remaining error facets stay open.

## Gate ownership and exercised boundary

`check-character-io-headers` joins the cross tier under firmware CI ownership.
Its matrix covers GNU17, GCC strict C17 and Clang strict ARM C17 under four
namespace selectors and both time views. Five inclusion/macro forms produce
120 valid syntax cases; 24 object checks require exactly the six callable
imports. Removing each added declaration or changing its result type must
reject in every compiler profile, producing 24 negative cases. Forty Smaller C
inputs compile and assemble. FILE size/alignment remain 24/4 on ARM.

`check-libc-character-io` and `tests/libc_contracts:check-host` execute the
actual six source files against controlled read/write dependencies. Native,
ILP32 and sanitizer variants enter the required execution inventory. Each
accepted executable performs 3,963 character operations. Separate compilations
reject six mutations: changed input byte, narrowed EOF, suppressed output
error, wrong getchar stream, wrong putchar stream and missing EOF counter
normalization. The QEMU owner runs the same driver with the Cortex-M0
instruction model. Compiler acceptance and runtime execution retain separate
receipts.

The `getc` macro decrements `_cnt` before `_filbuf` handles an established EOF.
The `_IOEOF` return resets `_cnt` to zero, so repeated calls traverse the fixed
point `(EOF, 0) -> (EOF, -1) -> (EOF, 0)` instead of eventually overflowing a
signed counter. The fixture checks the restored counter before another backend
read, and the `eof-counter` mutant removes only that normalization. `clearerr`
continues to control when a later read may observe appended input.

The fixture isolates production public names from host/newlib stdio through
private assembler symbol names in a generated test-only header. Exact public
signatures are checked before those declarations acquire private linkage names.
The production source and public macros remain unchanged by the namespace
mechanism. Target read/write/fstat/allocation dependencies receive controlled
test implementations; the actual `_filbuf`, `_flsbuf`, flush and close code
supplies the stream state transitions.

The native a.out contract takes initialized addresses of all six functions
and links against both full and reduced archives. Removing any of the four
provider members must reject with the corresponding missing symbol. The full
archive already selects all four members; the reduced archive initially selects
only `fgetc.o` and `fputc.o`. At `4b1b0fc1` the new gate rejects the reduced
archive for missing `getchar.o`. Source lookup in `boardlibc-members` and
`mkboardlibc.py` distinguishes the selected-member list from the source-family
registry: the registry already recognizes both missing members, while the list
omits them. The deciding repair adds `getchar` and `putchar` to the selected
list and adds macro-suppressed calls to the closure reference program.
The archive target also depends on the four provider sources and `stdio.h`,
so changes to these inputs rebuild the reduced archive.
The prediction is that both native archives link all six initialized addresses
and reject all four provider omissions. A matched clean distribution after the
repair supplies the resource vector; the earlier 48-byte archive-only increase
describes the incomplete candidate and supplies no admission limit.

Complete candidate `51dcf8e1` passes both archive links and all 36 removed-member
controls. The [resource record](callable-character-io-resource-exception.md)
binds the matched clean distributions, the 500-byte native archive increase,
unchanged 161 remaining shipped measures and separate native caller probes.
Whole-member native linkage and final text alignment introduce a caller cost
that cross-link garbage collection removes from existing shipped programs.
The owner-approved callable-I/O vector and its complete non-hardware
qualification close at `358efb11`. The
[EOF fixed-point resource record](stdio-eof-fixed-point-resource-exception.md)
owns the later normalization cost and its separate owner-approved vector.

The bounded fixture omits timestamp effects, the complete POSIX errno set,
signal-interruption schedules, thread synchronization and runtime stack peaks.
The six standards rows therefore remain partial/open with host witnesses.
New declarations supply no full-stdio namespace claim. Physical-board execution
retains its separate authorization boundary.
