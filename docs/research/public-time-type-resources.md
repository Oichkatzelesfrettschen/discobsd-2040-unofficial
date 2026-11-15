# Public time-type resources and installed closure

## Matched source and resource vector

Owned main `a706c0b851daaa25b019639e8217dda5ac17095d` supplies the clean
baseline; implementation `43e6d7ee7a3f74ce2bf169eb9f29e77e8f761492` supplies
the independently cleaned candidate. Captured compiler/linker inputs, source
snapshots, selected manifests, archive members, completed invocation receipts,
and identical measurement-tool identities bind each distribution.

The [parent](public-time-type-resources-parent.json),
[candidate](public-time-type-resources-candidate.json), and
[paired comparison](public-time-type-resources-comparison.json) retain all
162 shipped resource surfaces. Every delta is zero. Both kernels, each
program's loaded/raw/packed sizes and allocation blocks, installed native
artifacts, archive selections, and root arithmetic retain their measured
values. The root retains 752 data blocks, 9 inode-list blocks, and 227 free
blocks in its 988-block logical partition.

All 34 shipped program images are byte-identical across these captures.
The [complete built-program comparison](public-time-type-built-resources.json)
also preserves loaded/raw/packed sizes and allocation blocks for all 278
ordinary a.out producers. Of these, 277 retain identical bytes; pdc differs
only in four bytes of its eight-character compilation-time string, with
unchanged sizes and blocks.
Archive selection records report no added, removed, or changed selected
member payloads. Type-owner headers increase the captured header dependency
closure by four files without adding a linked provider or mutable object.
The compatibility profile therefore requires no new growth exception for
this measured vector. Runtime peaks and physical performance remain separate
from static resource accounting.

## Native archive identity

The installed native libc archive has a different raw SHA-256 at the same
byte length. The [archive decomposition](public-time-type-native-archive.json)
finds all 173 member payloads identical, including the symbol-index record.
All 692 changed bytes lie inside the archive headers' date fields. Clearing
only those fields gives the same normalized SHA-256 on both sides.

The existing Unix-archive parser independently extracts ordered member
payloads; duplicate member names reject. A separate header traversal checks
magic, header trailers, sizes, containment, and the terminal extent before
classifying changed byte offsets. Controls alter a payload byte, a non-date
header byte, and the global magic; all three reject the corresponding
identity or format claim. The normalized comparison classifies metadata;
it rewrites no installed library and establishes no new runtime claim.

This decomposition keeps raw file identity, member-code identity, and resource
cost separate. A changed archive digest alone establishes neither changed
native instructions nor additional bytes.

## Installed header closure

`include/Makefile` installs private sys headers through its existing wildcard
copy. The staged sysroot's ten public/private time-type header byte identities
match the source. Thirty-two standalone strict-C17 consumers compile using
only the staged include directory: four public entry points, four feature
selectors, and two cross time views. The checks use no fallback source
include path.

These observations qualify the staged SDK. The selected root manifest
contains no header files, so this unit adds no header-storage cost to the
shipped filesystem. They establish neither onboard native preprocessing nor
native runtime execution. The broader native-development package retains
those separate obligations.

## Behavior and remaining qualification

The [mechanism record](public-time-type-ownership.md) owns requirements,
private-owner dependencies, target layouts, donor observations, falsifiers,
and scope. Its compiler matrix contains 816 valid syntax cases, 24 alias
objects, six kernel cases, and 33 expected compiler rejections. All 272
Smaller C inputs compile and assemble with the actual narrow native policy
under either requested time selector. Real malformed-source and incorrect-width
controls reject, alongside fourteen diagnostic-verdict controls. Existing
Cortex-M0 time64 wrapper execution rejects every mutation. Peer header/signal
checks, time binding, selected hosted shim closure, Clang analyzer, C17 source
accounting, donor/profile calibration, and repository lint pass.

The [qualification receipt](public-time-type-qualification.json) binds the
clean implementation to the passing aggregate (720.78 seconds), required
inventory (216/216, 641.9 seconds), hosted CI 43714369946, UART Renode
(380.5 seconds), and union-kernel Renode (302.4 seconds). The inventory's
complete canonical report retains all 216 variant outcomes. The bounded
source review remains open. Five
header standards rows stay partial/open; narrow timeval signedness, clock
interfaces, complete namespace visibility, and full syscall semantics retain
separate work. Physical-board execution stays unrun.

With caller-selected `PYTHON`, replay the static comparison:

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
"$PYTHON" tools/c17/shipped_resources.py compare \
    --base docs/research/public-time-type-resources-parent.json \
    --candidate docs/research/public-time-type-resources-candidate.json \
    --output .build-logs/public-time-type-resource-replay.json
```

The calibrated comparator accepts the complete zero-delta vector without an
exception. Its verdict does not close the remaining behavioral or hardware
leaves.
