# Captured PIC replay and linked storage envelope

Implementation `5bb9f09da5cb6f4b4a233b3000af332af76eb571` adds a reusable
compiler-replay instrument on owned main
`a706c0b851daaa25b019639e8217dda5ac17095d`. The implementation changes host
tooling, its calibration and test registration; libc source and the production
flash layout retain their main definitions. Clean static-library capture binds
each archive member to its actual compilation rather than a reconstructed
filename-to-compiler rule.

## Ownership and calibrated replay

`tools/libc_pic_replay.py` selects every actual ARM archive member by its name
and byte hash. Each selection has one unambiguous successful compile witness,
matching source/dependency/tool identities, and the same tracked-source snapshot.
Generated syscall assembly retains its standard-input bytes. Arguments pass
an allow-list; source operands match the captured source set, output operands
have one owner, and response inputs reject before compiler execution.

Replay writes to a fresh evidence directory, adds the shared-library define
and four r9 PIC flags, and validates every resulting ARM relocatable object.
The final PIC archive has the same member denominator and matching object
payloads. Source, capture, retained input, verifier, compiler, archiver and
static-archive identities remain unchanged across the operation. The
[port testing contract](../../../../sys/arch/rp2040/doc/TESTING.md)
owns the command interface and gate registration.

Twenty-four calibrated tests cover real C and generated assembly acceptance,
a real GOT relocation, stale inputs, missing/ambiguous/failed captures,
unsupported capture versions, compiler/dependency/snapshot identities, operand
and path containment, compiler failure, and capture/snapshot/archive/retained-input
changes during execution. The receipt-drift controls fail before the corresponding
guards and pass with them. Existing provider-closure calibration passes 33 tests.
The logged build passes in 18.645 seconds; clean static-library compilation
passes in 4.749 seconds, and all 449 PIC members replay in 9.478 seconds.
These durations describe this host, not target performance.

The first full replay rejects duplicated `-c` options in generated syscall
commands. Both options select the same compiler mode. A generated-assembly
fixture reproduces that valid command form, and the parser preserves it while
checking exactly one output. An earlier fixture expects an undefined
`_GLOBAL_OFFSET_TABLE_` symbol, but actual ARM output uses a GOT relocation
without that undefined symbol. GNU readelf supplies the relocation witness.
The original failures remain retained; the corrected controls qualify the
actual compiler and generated-input mechanisms.

The warning policies selected by `lib/libc/gen/Makefile` for `err.o` and by
`lib/libc/wchar/Makefile` for its C members carry `-Wstrict-prototypes` and
`-Wold-style-definition` into replay. The calibrated ARM compile fixture includes
both options with `-Werror`. Replay passes them through unchanged.

## Current archive and flash reservation

At source `6719f9d7a`, 58 captured ARM archive members use both warning
options. The replay compiles all 531 members with their original warning
policy and the process-local PIC flags. GCC 16.2.0 and GNU binutils
2.47.20260726 produce the following linked-image measurements before
function slots.

| Link roots | Roots | Flash bytes | Writable bytes | Initialized bytes |
| --- | ---: | ---: | ---: | ---: |
| All archive globals | 738 | 73,036 | 5,872 | 1,984 |
| Matching libc linkages in tracked root-header witnesses | 491 | 67,336 | 5,736 | 1,884 |
| Matching libc linkages in installed standalone witnesses | 578 | 69,872 | 5,768 | 1,892 |

The two declaration-derived links use the same source and static archive
identity as the [census](../../libc-declaration-census.md). Each takes the union
across all eight profiles and resolves every named root. They retain kernel
and user declaration ambiguity and leave the reviewed export set open. Each
experiment exceeds the 49,152-byte reservation before slots. Omitting
`memmem` from the all-global control saves 432 bytes and fails required-root
retention. This source revision needs a reviewed export contract and further
size work before image admission.

## Complete-link measurement

The [member receipt](pic-replay-envelope-members.json) binds compiler GCC 16.2.0,
source, replay implementation and all 449 static/PIC member pairs. The
[link receipt](pic-replay-envelope-link.json) retains all 647 global roots,
linked artifact identities, allocated sections and load segments. The
[qualification receipt](pic-replay-envelope-qualification.json) binds the
completed logged jobs and their raw logs. The initial main-based exploration
and clean implementation replay produce byte-identical experimental ELFs;
they share their method and establish reproducibility rather than independent
semantic corroboration.

| Experimental linked surface | Bytes |
| --- | ---: |
| Flash including the initialization image | 62,604 |
| Per-process writable state including initialized storage | 5,648 |
| Initialized writable state | 1,836 |
| Fixed flash reservation | 49,152 |
| Excess before ABI function slots | 13,452 |

The initialized bytes occupy a flash initialization image and a writable runtime
copy. Each resource surface counts its own storage. The linker uses the existing
object-ABI fixture placement and supplies exactly the startup-owned `environ`
and `__progname` storage for measurement. It supplies no executable startup,
state relocation, stable slots or exec admission.

All roots remain defined and the image has no unresolved import. The calibrated
omission link removes only the `memmem` root and loses that definition. Its
flash extent decreases by 456 bytes to 62,148, with unchanged state. The root
check identifies `memmem` as missing. This smaller image fails the original
root contract, so its saving supplies no interface-preserving optimization.

The 647 names are an all-global screening superset, including the factored
shared syscall helper. They remain unreviewed for public ABI eligibility.
The earlier calendar and LTO records retain their original source, flags and
root sets; this measurement supplies no isolated attribution of a size change
to any one later libc mechanism.

## Rejected register optimization

A writable-state leaf function generates an r9 save/restore sequence under the
selected PIC flags. The candidate addition `-ffixed-r9` attempts to remove that
register-management cost. GCC rejects the combination with
`unable to use 'r9' for PIC register`. The
[register control](pic-replay-register-control.json) retains the source hash,
ordinary 28-byte text section and exact rejected-configuration diagnostic.
No candidate size or execution result exists for that flag combination.

The deciding result leaves compiler-register policy separate from libc source
optimization. A future compiler change needs its own calling-convention,
callback, interrupt and two-state witnesses. The existing object ABI and flash
reservation remain unchanged.

## Replay and remaining work

Capture a clean static libc with the existing command recorder, an unchanged
tracked-source snapshot and caller-selected `PYTHON`. The complete capture
procedure lives in the port testing contract. Then replay and measure into
fresh evidence directories:

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
"$PYTHON" tools/libc_pic_replay.py --root . \
    --records .build-logs/pic-capture \
    --snapshot .build-logs/pic-source.json --archive lib/libc.a \
    --output .build-logs/pic-replay
"$PYTHON" docs/research/xip-shared-libc/export-closure/link-pic-envelope.py \
    --root . --replay .build-logs/pic-replay --output .build-logs/pic-link
```

The link collector is an exploratory instrument with an explicit missing-root
control. It measures this experimental layout; it is not an executable-admission
gate. The replay gate participates in the firmware cross tiers. Focused gates,
Ruff and the unchanged production archive identify this host-tooling scope;
the local aggregate, ILP32 runtime inventory and Renode suites remain unrun for
this host-tooling scope. Hosted publication CI runs its configured emulator
stages separately. Physical-board execution remains unrun.

The full-export image still fails the fixed flash limit. The next deciding
work selects reviewed API roots from declarations and the compatibility
profile, computes their complete provider closure, and measures concrete
implementation reductions while retaining every selected root. Full signatures,
mutable-object layouts, versioned admission and process lifecycle retain their
separate obligations. The delivered instrument closes reproducible compiler
replay; it closes none of those architectural requirements.
