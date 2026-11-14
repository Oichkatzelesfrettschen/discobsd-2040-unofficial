# Native nm archive and symbol boundaries

## Baseline and deciding experiment

The implementation baseline is remote main `09b7845c8ffacaf1b53d5221241205a33907efd3`.
The qualification branch advances to remote main
`b74c522b449142ae3a06cb989abbe29ed15305f2`, incorporating calendar archive
closure and test fixes. The reader patch reapplies without unresolved conflicts.
`usr.bin/nm/nm.c` supplies both the native program and the host shim in
`tools/aoututils/nm/nm.c`. Source inspection of `nextel`, `nm_get_arobj`,
`fgetword`, `fgetsym`, and `namelist` identifies coupled traversal, extent,
record decoding, and error propagation defects.

`tools/check_nm_reader.py` encodes minimal relocatable objects directly from
the eight-word header in `sys/sys/exec_aout.h` and compact symbol records
described by `fgetsym`. Expected symbol values and names come from the fixture
generator, independently of reader output. The high-bit value `0xf1234567`
also exercises the fourth byte of word decoding.

The GNU17 host baseline passes 12 of 104 calls. Twelve valid archive layouts
cross short, 17-byte and 18-byte names with both payload parities and the
presence of a symbol index. Empty and non-object archives add two valid cases.
Twelve malformed cases cover truncated headers, trailers, names, bodies,
padding, numeric sizes, symbol records, and section extents. Four CLI modes
exercise each case: default, `-g`, `-o`, and `-go`.

The expected repair changes all 104 verdicts to pass. A positive malformed
exit requires a diagnostic; a signal or successful silent exit fails the
oracle. Valid output requires exact symbol order, value, and name after
removing the optional member prefix. Prefix identity, additional CLI flags,
native execution, allocation failures, I/O failures, and mutation controls
remain required additions before the fixture becomes a registered gate.

Replay the current baseline with a caller-selected interpreter:

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
cc -std=gnu17 -Wall -Wextra -Werror -DCROSS \
    -Itools/aoututils/include -idirafter include \
    usr.bin/nm/nm.c -o /tmp/discobsd-nm-reader-baseline
"$PYTHON" tools/check_nm_reader.py /tmp/discobsd-nm-reader-baseline
```

The failing verdict establishes the repair boundary. It does not qualify a
production change. Ruff checking and formatting pass for the fixture.

## Reader invariants

Archive traversal uses the stored size S, including any extended name L.
For a header at H with a 60-byte header, the next header is
`H + 60 + S + (S & 1)`. The payload starts at `H + 60 + L` and has size
`S - L`. Admission establishes `L <= S` and containment in the file before
forming either position. Every admitted member advances the cursor.

The header parser distinguishes exact EOF from a partial header or read
failure. Fixed-width numeric fields accept only the format's digit and
padding grammar, with overflow checked during accumulation. Conversion
precedes narrowing to internal fields. Seek and position errors propagate
to the process status. Empty archives terminate successfully.

Object admission distinguishes an ordinary non-object member from a
recognized object whose header or sections are incomplete. Symbol indexes
and ordinary non-object members skip deliberately. A recognized object
keeps every file-backed section and its symbol table inside the member.
Executable and relocatable objects use their respective `N_SYMOFF`
semantics; checked accumulation prevents the macro's unchecked arithmetic
from deciding admission.

A compact symbol has one length byte, one type byte, four little-endian
value bytes, and the declared name bytes. The reader validates the remaining
symbol-table extent before consuming the record. Each byte read distinguishes
EOF from a byte value. Unsigned shifts construct the value. A name length
of zero begins the zero-filled symbol-table tail. Every remaining byte must
be zero. `usr.bin/as/as-thumb.c` computes `stalign` in `middle` and emits
those bytes in `makesymtab`; `usr.bin/ld/ld.c:fgetsym_bounded` validates that
same tail. The original fixture assumption that every zero length is malformed
is refuted by the freshly built native archive and these two producers/readers.

The two-pass symbol selection requires bounded record counts, checked
allocation sizes, checked rewinds, and complete cleanup after partial name
allocation. A failed member emits a diagnostic and sets the final exit
status. Continuation to a later member requires a separately validated next
header position. The repair preserves option sorting and filtering behavior.

## Qualification still required

The option oracle adds eleven standalone invocations over local, absolute,
undefined, and common symbols with tied and high-bit values. The ARM baseline
fails reverse, numeric-reverse, and external-numeric-reverse ordering: plain
`char` makes `rflg` unsigned on ARM, so assigning -1 produces 255. Explicit
`signed char` preserves its one-byte representation and restores comparator
direction. The unsigned-direction mutation fails the named reverse-order
control. All 171 reader calls pass on Cortex-M0 QEMU; the host suite passes
207 reader/allocation/stream calls and rejects all six compiled mutations.
The production build including the stream-error and signed-direction repairs
passes in 26.491 seconds on the refreshed main baseline. Its receipt is
`.build-logs/rp2040-build-local-20261114T204304Z-60-f5164abd/status.json`.
The generated C17 source ledger includes all three new C fixtures and passes
validation; its unreviewed disposition remains distinct from runtime evidence.

The working candidate passes all 104 fixture calls with GNU17,
`-Wall -Wextra -Werror`, and undefined-behavior sanitization with recovery
disabled. Checked symbol reads distinguish EOF from data and assemble values
with unsigned shifts. Archive admission includes extended-name bytes and
stored-size padding. Object section accumulation checks the remaining member
extent before each addition. The candidate preserves the unsigned archive
date limit through `AR_DATE_MAX` independently of the signed seek limit.

The expanded reader matrix contains 140 calls. Short recognized object headers
at four lengths appear both alone and inside archives; an index whose bytes
resemble object magic remains a deliberate skip. The candidate before the
header repair passes 112/140; the repaired candidate passes 140/140 with the
same undefined-behavior instrumentation.

The selected-symbol count checks both `INT_MAX` and the allocation-size limit
before incrementing. The second pass decodes into local values before checking
capacity and writing a selected record. Partial name allocation releases all
completed names and the list before returning an error. The allocation fixture
in `tests/nm_reader/allocation.c` injects failure at each of three allocation
sites for a two-symbol object and checks allocation counts and zero outstanding
allocations. Its successful control and three failure controls pass, giving
144/144 calls when supplied through `--allocation-binary`.

A warning-clean `-m32` host build passes all 140 reader cases outside the
execution sandbox. The identical binary receives SIGSYS on every invocation
inside that sandbox; those 140 signals count as failed execution, not reader
validation. The outside-sandbox replay isolates the environment difference.
ILP32 host execution remains separate from ARM target execution.

Exact archive/member prefixes now participate in the output oracle. An
embedded NUL followed by garbage in a numeric field fails all four CLI modes
before the parser requires consumption of the entire fixed-width field; the
corrected parser rejects all four. Four zero-padding lengths add sixteen
valid reader calls. The candidate passes 160/160 reader cases under UBSan
and reads the freshly built native libc archive with zero status and no
diagnostic, producing 2,184 output lines. This archive check establishes
successful enumeration, not independent verification of every emitted symbol.

`logged-build` completes with status zero in 39.143 seconds at the
pre-padding candidate. Its immutable receipt is
`.build-logs/rp2040-build-local-20261114T203145Z-46-7ea1d22e/status.json`.
That build establishes production compilation, including the ARM executable;
the subsequent padding correction requires a refreshed production build.

The refreshed production build completes with status zero in 26.993 seconds;
its receipt is
`.build-logs/rp2040-build-local-20261114T203355Z-45-b2ae8724/status.json`.
`check-native-nm` belongs to the host tier and passes its production host
reader cases, UBSan reader cases, and allocation ownership controls. Five
warning-clean mutations each reach a named rejection: omitted archive
padding, embedded-NUL numeric acceptance, suppressed error status, nonzero
symbol padding acceptance, and incorrect member prefixes. Calibration retains
the generated source, build log, run log, and source hash for each invocation.

The stream fixture in `tests/nm_reader/io.c` counts every executed seek,
position, buffered read, byte read, close, and flush operation on an archive
with an extended member name and symbol padding. Injection at the input close
initially preserves successful status, exposing an unchecked close result.
The repair diagnoses failed input close and failed final output flush.
The successful control and all 31 injected failures pass, with zero outstanding
allocations and a diagnostic for each failed operation. Calibration passes
196 combined reader, allocation, and stream cases plus its five mutations.
`check-native-nm-qemu` passes all 160 reader cases with code compiled for
Cortex-M0+ and executed under QEMU's Cortex-M0 model. Its newlib runtime uses
the existing Linux syscall support plus a seek implementation. The execution
does not exercise DiscoBSD stdio or kernel services. These schedules establish
the exercised failure paths; they do not simulate every possible concurrent
file mutation.

Additional fault coverage, caller closure, native execution, matched resource receipts, review, and
integration remain open. The host fixture supplies no board observation.
Resource approval follows the compatibility profile after exact deltas exist.

## Resource receipts and merged-main qualification

The retained resource pair binds baseline `b74c522b449142ae3a06cb989abbe29ed15305f2`
to candidate `ef4962cee53ba6e665b9ca33ba065b4d07a227f9`.
`native-nm-resource-baseline.json` and `native-nm-resource-candidate.json`
contain the clean-distribution receipts; `native-nm-resource-comparison.json`
records zero change across all 162 shipped numeric surfaces and an empty
positive-delta list. The candidate root has 233 free blocks. The shipped
manifest excludes `nm`, so this comparison establishes shipped-image cost
only. Runtime stack and heap peaks remain unmeasured.

Merge commit `63f40147` incorporates remote main `a30ed5f6`, including its
calendar conversion changes. The resource pair above remains evidence for
its named revisions; resource qualification against this newer main remains
open. The merged branch passes `logged-build` in 39.912 seconds, with receipt
`.build-logs/rp2040-build-local-20261114T205550Z-3528080-5ed7a346/status.json`.
The registered `check-native-nm` gate passes 171 production-host cases,
207 UBSan reader/allocation/stream cases, and all six mutation controls.
`check-native-nm-qemu` passes 171 Cortex-M0 cases. An independent invocation
of `tests/nm_reader/calibrate.py` with `CC='cc -m32'` passes all 207 cases
and six mutation controls outside the execution sandbox. These checks qualify
the exercised reader paths on the merged source; physical-board execution
remains unrun.

Hosted run `43639338429` completes successfully for candidate `ef4962ce`.
That result precedes the merge of newer main and does not qualify the merged
head. Review, final-head CI, and matched current-main resource qualification
remain open before PR integration.

## Matched main resource pair and section-offset witness

Main `f602046341801b77feff61a2c3549c119ffb8d37` and candidate
`785395bce94ad5a61f8832cf5a079e9d96ce1031` each complete a clean,
instrumented distribution and shipped-closure check. Their retained receipts
are `native-nm-resources-f6020463.json` and
`native-nm-resources-785395bc.json`; the comparison is
`native-nm-resources-f6020463-785395bc.json`. All 162 shipped numeric surfaces
remain equal, with zero positive deltas. The distribution commands complete
in 193.748 and 195.968 seconds respectively. Those elapsed times describe
concurrent host builds, not a target-performance comparison. The older
resource pair remains immutable historical evidence.

The section-offset probe uses text, data, text relocation, and data relocation
sizes of 3, 5, 7, and 9 bytes. `N_SYMOFF` in `sys/sys/exec_aout.h` includes
relocation extents for RMAGIC and omits them for OMAGIC and NMAGIC. The
registered fixture encodes all three cases independently and requires the
same symbol value and name in four CLI modes. The prediction is twelve
additional passing reader cases; a mutation replacing the text relocation
extent with zero must fail `section-offset-406`. Host and Cortex-M0 execution
confirm that prediction. The calibrated host and ILP32 suites pass 219 cases
and seven mutation controls; Cortex-M0 QEMU passes 183 reader cases.

These fixture additions change tests and documentation only. Production
`nm.c` remains byte-identical to the measured candidate. Final-head CI and
review remain required before integration; stack, heap, and physical-board
measurements remain separate open evidence classes.

## Reader review and optional executable cost

The parent source review follows `nm_get_arobj`, `nextel`, `fgetsym`,
`compare`, and `namelist` in `usr.bin/nm/nm.c`, and checks their layout
assumptions against `sys/sys/exec_aout.h` and `usr.bin/ar/archive.h`.
`usr.bin/nm/Makefile` supplies the native archive-header include directory;
the cross build selects the same archive definitions through its explicit
include. The review finds no additional actionable defect in these paths.
This is a parent review, not an independent model review.

| Claim | Source invariant and falsifier | Evidence |
| --- | --- | --- |
| Archive position stays in the input | `nextel` checks stored bytes and parity against the remaining file extent before adding them; truncated storage falsifies admission | Short/extended names, both parities, malformed header/body/padding cases and archive-padding mutation |
| Symbol offset follows the object format | Each section consumes only remaining member extent; relocation sizes participate only for RMAGIC | Three nonzero-section fixtures and relocation-offset mutation |
| Partial decoding preserves ownership | The first pass bounds count and allocation multiplication; the second pass checks capacity before storing, and failure frees completed names | Allocation and stream failure schedules with zero outstanding allocation checks |
| ARM reverse ordering preserves direction | `rflg` is explicitly signed and each numeric branch returns its sign | Option oracle on host, ILP32, and Cortex-M0; unsigned-direction mutation |
| Output failure reaches process status | Failed final flush or stream error calls the common error path | Injected stream-failure schedule and nonzero-status oracle |

The C17 source-denominator gate passes its 21 rejection controls and reports
153 accepted, 543 excluded, 62 migration-open, and 2,261 unreviewed paths.
Those counts establish accounting, not complete C17 semantic acceptance.

The optional native executable's positive deltas are recorded separately in
[native-nm-resource-exception.md](native-nm-resource-exception.md). Shipped
resource equality does not absorb that executable's 1,140-byte loaded growth.
The owner approves its exact exception on 2026-11-14; final CI and integration
remain open.
