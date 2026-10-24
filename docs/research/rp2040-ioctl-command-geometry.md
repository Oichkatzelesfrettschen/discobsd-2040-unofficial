# RP2040 ioctl command geometry

## Contract

The public ioctl encoding in `sys/sys/ioctl.h` reserves bits 16 through 23 for
an eight-bit payload length. Direction and void flags occupy bits 29 through
31. Other command bits belong to the command identity and cannot enlarge the
caller-memory span.

The inherited `ioctl()` path removed only the direction and void flags before
shifting the complete remaining command. A reserved bit such as bit 24
therefore inflated a four-byte payload to 260 bytes. The path then probed the
first and last byte separately. An encoded zero length formed
`cmarg + nbytes - 1`, which subtracts one from the caller-selected address.

The repaired syscall applies these rules:

- `(com >> 16) & IOCPARM_MASK` extracts only the public eight-bit size field.
- `baduaddr_span()` admits the complete encoded span with the RP2040 process
  window and conditional extension rules.
- A directional command with encoded size zero owns an empty span and reaches
  the provider without probing caller memory.
- The provider receives the complete command value and retains authority to
  reject unknown or malformed commands.
- The syscall adds no temporary payload buffer. General input and
  bidirectional marshalling retain their separate ownership unit.

The change preserves syscall 54, all public command values and layouts,
descriptor and access precedence, provider dispatch, provider errors, and the
existing `FIONREAD` result-staging contract.

## Calibrated behavior

`tests/kernel/fionread_test.c` compiles the exact extracted production
`ioctl()` and `ino_ioctl()` definitions against bounded kernel-header shims.
Its 67 assertions retain all 51 result-ownership cases and add five command
geometry cases with sixteen assertions:

1. An unknown `IOC_IN` command with encoded size zero reaches the provider as
   an empty span and returns `ENOTTY` for null, below-window, upper-bound and
   above-window argument values.
2. An unknown `IOC_IN` command with reserved bit 24 and an encoded four-byte
   size admits four bytes, preserves the complete command, reaches the
   provider, and returns `ENOTTY`.

The native, ILP32 and address/undefined-behavior/alignment sanitizer fixtures
pass all 67 assertions. Two warning-clean mutations extend the three retained
`FIONREAD` controls:

- `unmasked-size` restores reserved-bit inflation of the payload length.
- `endpoint-span` restores first/last-address probes and zero-length
  subtraction.

Each mutation exits with assertion status 1 at its named falsifier. The root
`check-kernel`, required `check-kernel-ilp32`, `check-test-execution`, Ruff and
diff checks pass. `check-c17-compile-profiles` compiles and links both shipped
kernels with their exact GNU17 Cortex-M0+ commands and warnings as errors.

## Static resource state

Clean schema-2 distributions compare parent
`7f828e2b91f4a4362341ed5e1ed7c2abb8064797` with implementation commit
`997d27d7355fc563d88d5fa545fee46bb07323e7`. Both captures used bmake
20260912, arm-none-eabi-gcc 16.2.0 and GNU ld 2.47.20260726. Their dependency
closures account for 173 PICO C/header paths, 165 PICO_UART paths, all 36
shipped artifact groups, the native startup object and libc archive, generated
inputs, and the packed root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,150 bytes | 105,134 bytes | -16 bytes |
| PICO_UART flash | 92,676 bytes | 92,660 bytes | -16 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement, native artifact, archive selection and
archive-member set is unchanged. Exact recorded compiler commands replayed
with `-fstack-usage` measure `sys_generic.o` text at 2,116 then 2,100 bytes and
`ioctl()` at 452 then 436 bytes in both configurations. Object data remains
zero, object BSS remains eight bytes, and the `ioctl()` static frame remains 24
bytes. Both linked kernels save 16 bytes. The candidate retains 25,938 PICO
flash bytes and 38,412 PICO_UART flash bytes below the 128 KiB
region limit. The default nonpositive-growth rule accepts every constrained
surface. Runtime stack, heap and storage-traffic fields remain unmeasured.

The evidence records have these SHA-256 identities:

- parent source snapshot: `8de67ad512d2e640b528484b4e869199586cfd15d1c9baae8546b3a151b8e74a`
- parent dependency closure: `786f2602ec1ab6b4051106cf37b1b1f79efe4078faac8b92d41beba9301fcd49`
- parent schema-2 resources: `d1ff428d47f387b34a3c1e1ec81baa30789cc0e5322002b89588a4cac60219eb`
- parent object and frame: `6c27085c174dc6136a83e8d49cdf58c7bbcacfe538b93bfe430748fb3e7800db`
- candidate source snapshot: `e1e716c017630d02bcb9cd2eebba1e513913fbde62b99f31c89832e99e6c4450`
- candidate dependency closure: `6e56f136faf539fd02d2af7bf09453333bb265cc4f4abba98b22c9ef75d6f3f4`
- candidate schema-2 resources: `28e6a92f339add574ad5a63fae9b3bfc2ab603a7b6fd289551e15e0f24908aa6`
- static-resource comparison: `99d3740d57950dc55735d7fa3ca9f56b948064be560b786dbf7c3e38bfa70dfa`
- candidate object and frame: `2940489ce1d26bdb6383989d5dd675707cc2d33124c76646549654f3f2793fbe`
- object and frame comparison: `e87cada797088820019a3b85c6f820f18903e2973a1316961991291d393760a7`

## Evidence states and residuals

The host fixtures establish the exercised size extraction, empty-span,
complete-span and command-preservation behavior. Exact target builds establish
compilation, linkage, static placement and shipped dependency closure for PICO
and PICO_UART. The checks do not execute an ioctl on RP2040 silicon or measure
runtime stack and heap peaks.

General input and bidirectional ioctl marshalling still passes a
caller-selected address to the provider after admission. Tty local-mode signed
arithmetic, `/dev/mem`, `ptrace`, MPU fault behavior and physical-board
execution retain separate migration or evidence states.
