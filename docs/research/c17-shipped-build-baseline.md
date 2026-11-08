# Shipped dependency and static-resource baseline

Public-header and libc refactoring need a configuration-specific caller set and
comparable artifact measurements. The retained baseline measures production
source at `3626ccf3` (main `6941397d` plus the puzzlebox generated-input
providers) with the opt-in capture adapter, from a clean distribution
of the `full` profile (`distrib/rp2040/sdcard.img`) built with GCC 16.2.0. The
source companion identifies 604 tracked C/header paths by SHA-256 and records
their membership in 37 artifact groups: 33 packed programs, native libc, native
crt0.o, PICO and PICO_UART. The collector selects the `full` image and the PICO
and PICO_UART kernels only. The `picocalc` profile (`bc`, `dc`, `calc`, the SD
device nodes) and the per-board `sdcard-BOARD.img` images fall outside it. The
kernel sizes come from the local compiler and carry no flash-budget authority;
`check-kernel-headroom` owns that figure. The complete command graph remains
bound by its SHA-256 in that companion. Generated-input providers remain
explicit in the closure output.

`c17-shipped-resource-baseline.json` retains the collector's complete static
receipt. `c17-shipped-source-baseline.json` retains source membership and source
identity. The records describe the measured revision. The source denominator
still owns each path's language state, migration owner and risk tier; a shipped
path with an unreviewed tier remains unreviewed. The baseline closes neither
shipped C17 language acceptance nor whole-tree acceptance.

| Surface | Observation |
| --- | ---: |
| PICO tracked C/header paths | 173 |
| PICO_UART tracked C/header paths | 162 |
| PICO loaded flash extent | 95,076 bytes |
| PICO_UART loaded flash extent | 85,264 bytes |
| PICO allocated kernel-region ELF sections | 32,208 bytes |
| PICO_UART allocated kernel-region ELF sections | 15,328 bytes |
| PICO allocated scratch-region ELF sections | 8,192 bytes |
| Root allocated data blocks | 738 |
| Root free blocks | 241 |
| Root free inodes | 32 |
| Largest shipped loaded text/data/BSS | 75,324 bytes, smlrc |

Allocated ELF sections measure the named regions; fixed reservations and
runtime use retain their own accounts. Loaded program occupancy excludes
runtime heap, stack and simultaneous scratch ownership. The retained two-slot
inode reserve and stack-paint remainder provide observations for their original
workloads, rather than an optimization budget.

At `e7040aa1`, two instrumented clean distributions produced identical static
resource vectors and archive-member sets under the same measurement-tool
hashes. That comparison checks the capture/collector integration. A subsequent implementation
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

The retained receipt is schema 2: `shipped-archive-selection-comparison.md`
defines the format, which adds every kernel and non-libc archive selection and
requires matching collector digests. A comparison against it needs a candidate
captured by the same collector and toolchain. The earlier schema-1 receipt at
`e7040aa1` (580 paths, 36 groups, 32 packed programs) remains in Git history;
the validator still labels a schema-1 receipt as historical.

The source companion projects the closure: `sources` maps each tracked path in
any group's `tracked_sources` to its SHA-256, `artifact_sources` lists each
group's sorted `tracked_sources`, and `closure_sha256` is the digest of the
closure file the resource receipt binds as `execution.closure_sha256`;
`tools/c17/project_source_baseline.py` performs that projection and reproduces
the committed companion from its closure byte for byte.

Runtime stack, heap and storage traffic remain unmeasured in the receipt.
Physical-board execution remains outside the capture operation. The twelve-stage
queue in `c17-semantic-migration-plan.md` continues with source ownership review,
header contracts, compiler coverage and subsystem migration; baseline tooling
establishes the prerequisite evidence surfaces.
