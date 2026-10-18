# Shipped dependency and static-resource baseline

Public-header and libc refactoring need a configuration-specific caller set and
comparable artifact measurements. The retained baseline measures production
source at `e7040aa1` with the opt-in capture adapter. The source companion
identifies 580 tracked C/header paths by SHA-256 and records their membership in
36 artifact groups: 32 packed programs, native libc, native crt0.o, PICO and
PICO_UART. The complete command graph remains bound by its SHA-256 in that
companion. Generated-input providers remain explicit in the closure output.

`c17-shipped-resource-baseline.json` retains the collector's complete static
receipt. `c17-shipped-source-baseline.json` retains source membership and source
identity. The records describe the measured revision. The source denominator
still owns each path's language state, migration owner and risk tier; a shipped
path with an unreviewed tier remains unreviewed. The baseline closes neither
shipped C17 language acceptance nor whole-tree acceptance.

| Surface | Observation |
| --- | ---: |
| PICO tracked C/header paths | 171 |
| PICO_UART tracked C/header paths | 163 |
| PICO loaded flash extent | 104,768 bytes |
| PICO_UART loaded flash extent | 92,284 bytes |
| PICO allocated kernel-region ELF sections | 32,216 bytes |
| PICO_UART allocated kernel-region ELF sections | 15,336 bytes |
| PICO allocated scratch-region ELF sections | 8,192 bytes |
| Root allocated data blocks | 692 |
| Root free blocks | 287 |
| Root free inodes | 33 |
| Largest shipped loaded text/data/BSS | 75,412 bytes, smlrc |

Allocated ELF sections measure the named regions; fixed reservations and
runtime use retain their own accounts. Loaded program occupancy excludes
runtime heap, stack and simultaneous scratch ownership. The retained two-slot
inode reserve and stack-paint remainder provide observations for their original
workloads, rather than an optimization budget.

Two instrumented clean distributions produced identical static resource
vectors and archive-member sets under the same measurement-tool hashes. That
comparison checks the capture/collector integration. A subsequent implementation
change must compare its own clean parent and candidate receipts. The retained
uninstrumented parent artifacts lack the complete command graph required for
accepted dependency attribution and remain outside that paired claim.

The foundation integration at `eb3d16cf` supplied 54 recorder, closure and
resource controls. The donor gate
adds seven retained-source controls. The controls reject incorrect producer
identity, missing dependencies, changed archives/images, generated-input gaps,
concurrent source changes, omitted indirect blocks, native artifact growth,
added archive dependencies and changed measurement tools.

`sys/arch/rp2040/doc/TESTING.md` provides the complete capture and comparison
commands. A resource receipt can be checked independently with:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
"${PYTHON}" tools/c17/shipped_resources.py validate \
    --candidate docs/research/c17-shipped-resource-baseline.json
```

The retained schema-1 receipt preserves its numeric and libc/native archive
projection scope. `shipped-archive-selection-comparison.md` defines schema 2,
which adds every kernel and non-libc archive selection and requires matching
collector digests. The validator labels the retained record as historical;
complete comparisons require a newly measured schema-2 pair. The historical
JSON and its recorded measurements remain unchanged.

Runtime stack, heap and storage traffic remain unmeasured in the receipt.
Physical-board execution remains outside the capture operation. The twelve-stage
queue in `c17-semantic-migration-plan.md` continues with source ownership review,
header contracts, compiler coverage and subsystem migration; baseline tooling
establishes the prerequisite evidence surfaces.
