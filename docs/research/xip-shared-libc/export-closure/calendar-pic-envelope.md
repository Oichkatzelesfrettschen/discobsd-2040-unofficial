# Shared libc sizing with calendar and process-local assembly

Source `dcc2580c3e36f19b759188879dbd881e857b33b3` includes calendar main
`9561fd1cf05d823d1b93b9236448a65ff1c99abc` and shared syscall and ROM
state addressing. All 449 archive members compile without diagnostics using
GCC 16.2.0, the captured compilation arguments, the r9 PIC flags and
`DISCOBSD_SHARED_LIBC=1`. Replay validates source, dependency, compiler and
captured standard-input hashes and redirects outputs to isolated files.

The [input-section receipt](calendar-pic-input-sections.json) finds zero
absolute read-only references to its known mutable providers. That bounded
scan covers the earlier 165 syscall and ROM references; it does not establish
complete relocation safety or startup correctness.

## Linked storage

The [normal link](calendar-pic-link.json) retains all 646 global definitions.
Its linker script derives from `tests/libc_xip_object_abi/library.ld`, supplies
`__libc_got_size`, and uses an `environ`/`__progname` startup storage fixture.
Target libgcc supplies compiler helpers. Neither stable function slots nor
executable admission exists in this experimental image.

| Surface | Normal PIC bytes | Hidden visibility bytes |
| --- | ---: | ---: |
| Flash including initialization image | 62,396 | 62,396 |
| Per-process writable state | 5,648 | 5,656 |
| Initialized state | 1,836 | 1,844 |
| GOT | 948 | 956 |

The [hidden-visibility link](calendar-hidden-link.json) retains the same
global definitions. Its eight-byte text reduction coincides with eight more
GOT bytes, leaving flash unchanged and increasing writable state. This
experiment rejects hidden visibility as a storage optimization for these
inputs.

The normal image exceeds the fixed 49,152-byte reservation by 13,244 bytes
before stable slots. The reservation remains fixed. An explicit public ABI
closure and measured implementation reductions determine the next feasible
image; installed callers alone cannot authorize removal of callable APIs.

## LTO diagnostic boundary

An exploratory replay adds `-flto -ffat-lto-objects`; all 449 compilation
commands complete without diagnostics. The full link fails with unresolved
libgcc shifts and Thumb switch helpers. No LTO size result qualifies.

A minimal translation unit increments a global `int state` and returns
`value << count` for an unsigned long long value. Its link succeeds with
`-flto=1`, the same r9 PIC flags and `-lgcc`, both with the default linker
script and with the full experiment's script. Both links report
`'-mpic-register=' is useless without '-fpic'`. Disassembly from
`arm-none-eabi-objdump -dr` nevertheless shows `shift` copying r9 to r4,
loading the state pointer through `[r4, r3]`, and calling the linked
`__aeabi_llsl`. This finite case refutes the inference that the diagnostic
alone proves loss of PIC addressing. It also excludes the experimental
linker script alone as the cause of missing helper extraction.

The full archive's helper extraction and the warning's compiler origin remain
unresolved. The next discriminating experiment compares retained LTRANS
objects and plugin resolution records with the successful minimal link.
The warning-free acceptance boundary remains closed for LTO.

Local evidence lives under `.build-logs/shared-libc-calendar-pic/`,
`.build-logs/shared-libc-calendar-hidden/` and
`.build-logs/shared-libc-calendar-lto/`. Their replay scripts, receipts, maps
and binaries are exploratory evidence, not calibrated acceptance gates.
These results establish compilation, inspected addressing and linked storage;
runtime lifecycle and physical-board qualification remain separate.
