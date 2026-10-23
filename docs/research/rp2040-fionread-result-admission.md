# RP2040 FIONREAD result admission

## Contract

The `FIONREAD` syscall path preserves syscall 54, the command value, the
public `long`/`off_t` representation, descriptor and access checks, provider
selection, provider errors, and the existing regular-file, directory, pipe and
terminal result calculations.

The inherited general `ioctl()` path passed the caller-selected output address
directly to the file-operation provider. `ino_ioctl()` and `ttioctl()` then
stored through an `off_t *`. A complete but unaligned user address therefore
reached an undefined typed store on ARMv6-M, and a provider could modify caller
memory before returning an error.

The repaired path applies these rules:

- `ioctl()` initializes one aligned kernel `off_t` and passes its address to
  every shipped `FIONREAD` provider.
- A successful provider publishes exactly one complete `off_t` through
  `copyout()`.
- A provider error suppresses publication even when the provider wrote its
  private result first.
- The existing encoded-span admission still rejects a crossing destination
  before provider dispatch.
- An incorrectly successful provider that writes no result publishes the
  initialized zero rather than kernel stack bytes.

The unit closes `FIONREAD` across regular files, directories, both pipe ends,
the controlling-terminal forwarding path, UART and USB terminal providers.
Other input, output and bidirectional `ioctl` payloads retain separate units.

## Calibrated behavior

`tests/kernel/fionread_test.c` compiles the exact extracted production
`ioctl()` and `ino_ioctl()` definitions against bounded kernel-header shims.
The 51 assertions cover regular files, directories, pipe read and write ends,
an unaligned but admitted destination, byte guards, initialized provider
storage, provider failure after a private write, copyout failure, an
incorrectly successful provider that writes nothing, a crossing span, a
closed descriptor and an invalid descriptor.

The inherited implementation fails 22 assertions in the native fixture. Its
sanitizer form terminates at the production `ino_ioctl()` store with a
misaligned `off_t` diagnostic. The repaired native, address/undefined-behavior
sanitizer and ILP32 fixtures each pass all 51 assertions under GCC. Clang
compiles and runs the native fixture and all mutations with the same warning
contract. The extracted production functions compile in C17 mode with full
prototype and old-style-definition diagnostics; broad kernel headers retain
their separately tracked GNU extension boundary.

Three warning-clean mutations calibrate the deciding behavior:

1. `direct-user-pointer` restores direct provider access to the caller
   destination.
2. `unconditional-publication` publishes private output after provider
   failure.
3. `wrong-width` copies only a `short` result.

Each mutation exits with assertion status 1 at its named falsifier. The logged
GNU17 Cortex-M0+ kernel build compiles and links both PICO and PICO_UART with
warnings treated as errors.

## Static resource state

Clean matched distributions compare parent
`bd0d28c82343d64ee51a4b2aa29e19d82b82c071` with candidate
`549628e8cfc53c9ba835a64a2d3856c4520bb35c`. The same ARM GNU toolchain and
configuration inputs produced both receipts.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,080 bytes | 105,144 bytes | +64 bytes |
| PICO_UART flash | 92,604 bytes | 92,668 bytes | +64 bytes |
| PICO `.bss` | 15,272 bytes | 15,272 bytes | 0 |
| PICO SwapRAM pool | 16,384 bytes | 16,384 bytes | 0 |
| PICO scratch region | 8,192 bytes | 8,192 bytes | 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement and archive-member selection is unchanged.
Exact-command replay attributes the linked growth to `sys_generic.o` and
`ioctl()`: object text grows from 2,084 to 2,148 bytes and the function symbol
from 388 to 452 bytes in both configurations. Object data remains zero,
object BSS remains eight bytes, and the compiler-reported `ioctl()` frame
remains 24 bytes.

The default resource comparison correctly rejects both positive flash deltas.
The unit accepts a bounded 64-byte exception per kernel because private result
staging closes typed alignment, result ownership and successful-publication
requirements across every shipped provider. The candidate retains 25,928
bytes in the PICO kernel region and 38,404 bytes in the PICO_UART kernel
region. Runtime stack high-water remains open.

The evidence records have these SHA-256 identities:

- parent source: `3340be6156e3af10b873e17a9712ff540896d38f157e760f39a0a3e2652e1f9e`
- parent closure: `80fb6d9a63d8a8aadbbfa600c1cb2ff778af237b2bc374f699c9ebc688a5c005`
- parent resources: `764dd1e012a6dac6a8074ae786a5c3fdc4c19e4a194fd57e45176cc46076dbff`
- candidate source: `1ec17fa5a4d0978cdcedaf1cc68f2694df68131f65f8842c6574cab28e57112f`
- candidate closure: `9bbd6ecfa5c8247de57c367f7fd382bc1ffa40192576e357532d01bd87cb747e`
- candidate resources: `216629c441fba167a953d2694a07331ced12b2cd1b0d43386b2e9d6ec4998a40`
- resource comparison: `58a7234d198a0ee9a033d81d1d4a30735b95dc3075d98b68fdb2aa086c0c22ff`
- parent object and frame: `a2917d3b897039bd7fb09a27bbd43d87df4228115f36e9dee61f65cea97e8a8f`
- candidate object and frame: `39a3efd3d38ff839d8ddb8dd5bdeda357532abbf7eebfaf2a687e86e7e700f7b`
- object and frame comparison: `10024e40ed6c500c38f58d1df2fc90cbff48d0881ff65cf5804ecf629a099f4a`

## Evidence states and residuals

The host fixtures establish the exercised alignment, private-result ownership,
success-gated publication, result width and failure-preservation contracts.
The exact target build establishes compilation and linkage for both shipped
configurations. Neither evidence class executes a terminal, file or pipe
`FIONREAD` request on RP2040 silicon.

The unit allocates no static result buffer and changes no syscall or public
layout. The matched static comparison accepts only the recorded 64-byte flash
exception. Physical-board execution, runtime stack and heap peaks, storage
traffic, general input and bidirectional `ioctl` marshalling, malformed
command geometry, swap-device result semantics, tty local-mode arithmetic,
`/dev/mem` and `ptrace` remain separate evidence states or migration units.
