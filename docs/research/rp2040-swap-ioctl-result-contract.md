# RP2040 temporary-swap ioctl result contract

## Contract

The temporary-swap provider preserves `TFALLOC`, `DIOCGETMEDIASIZE`, their
public payload representations, read-only descriptor acceptance, invalid-minor
rejection, erase-aligned allocation, logical extent sizes, append cursor
publication and unknown-command rejection.

The inherited `swcioctl()` completed two valid operations and then fell through
to `EINVAL`. A zero-valued `TFALLOC` released the prior extent before reporting
failure. `DIOCGETMEDIASIZE` wrote the logical size before reporting failure.
Callers that honored the ioctl result therefore observed failure after the
provider had completed and published the requested operation.

The repaired provider applies these result rules:

- A positive `TFALLOC` returns zero after allocation. Resource shortage still
  returns zero with an output value of zero.
- A zero-valued `TFALLOC` releases the prior extent and returns zero.
- A negative `TFALLOC` releases the prior extent, preserves the negative input
  and returns `EINVAL`.
- `DIOCGETMEDIASIZE` stores the logical block count and returns zero.
- An unknown command returns `EINVAL`; minor 3 and the raw-swap minor 64 return
  `ENODEV` before payload access.

The general syscall layer still passes `TFALLOC` input and bidirectional
payloads through the caller-selected address after an endpoint check. Typed
staging and complete-span admission for that path remain a separate migration
unit. The provider contract establishes only behavior after dispatch with an
admitted, aligned payload.

## Calibrated behavior

`tests/kernel/swap_ioctl_test.c` compiles the exact extracted production
`swsize()` and `swcioctl()` definitions against bounded kernel-header shims and
links the production resource-map allocator. The 56 assertions cover positive
allocation, zero release, media-size queries, negative requests, initial and
replacement shortage, erase-rounded free space, logical size, append cursor
publication, unknown commands, invalid minors and read-only descriptor use.

The inherited implementation fails exactly the zero-release status and
media-size status assertions. The repaired native, address/undefined-behavior
sanitizer and ILP32 fixtures each pass all 56 assertions under GCC. The ILP32
fixture pins four-byte `off_t` and `int` representations plus the exact
`TFALLOC` and `DIOCGETMEDIASIZE` command values. Clang compiles and runs the
positive fixture and sanitizer form under the same warning contract.

Five warning-clean mutations calibrate the deciding behavior:

1. `release-fallthrough` restores the zero-release failure result.
2. `media-size-fallthrough` restores the successful-query failure result.
3. `shortage-output-suppression` withholds the zero shortage payload.
4. `unrounded-free` returns only the logical extent to the erase-owned map.
5. `stale-released-size` leaves a released extent visible through `swsize()`.

Each mutation exits with assertion status 1 at its named falsifier. Exact
GNU17 Cortex-M0+ compiler-profile builds compile and link PICO and PICO_UART
with warnings treated as errors.

## Static resource state

Clean matched distributions compare parent
`c4f802d6b750e9ac83d4c7cf920b9c5369517ffc` with candidate
`ca4f06f9cace75e67a137ca5031ceeea5d897f26`. The same ARM GNU toolchain,
configuration inputs and 36 shipped artifact groups produced both receipts.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,144 bytes | 105,152 bytes | +8 bytes |
| PICO_UART flash | 92,668 bytes | 92,676 bytes | +8 bytes |
| PICO `.bss` | 15,272 bytes | 15,272 bytes | 0 |
| PICO SwapRAM pool | 16,384 bytes | 16,384 bytes | 0 |
| PICO scratch region | 8,192 bytes | 8,192 bytes | 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement and archive-member selection is unchanged.
Exact-command replay attributes the linked growth to `swap.o` and
`swcioctl()`: object text grows from 1,838 to 1,846 bytes and the function
symbol from 280 to 288 bytes in both configurations. Object data remains zero,
object BSS remains 18 bytes, the `swcioctl()` frame remains 56 bytes, and the
`swsize()` frame remains eight bytes.

The default resource comparison correctly rejects both positive flash deltas.
The unit accepts a bounded eight-byte exception per kernel because the added
returns make completed release and query operations observable as successful
without changing allocation ownership or caller-visible ABI. The candidate
retains 25,920 bytes in the PICO kernel region and 38,396 bytes in the
PICO_UART kernel region. Runtime stack high-water remains open.

The evidence records have these SHA-256 identities:

- parent source: `ef1ad70c5b1e8f078b14e8feb1dd6ffb19dd0c6784fe34183f5e6b0104560812`
- parent closure: `10d4779fff19619f873c2e8e2857eeff99d346fb16a0a1241f095587afbd0006`
- parent resources: `06fe2c2430d784a379d65d1f53df5392e7d455784dc96598e4392f14d48a075a`
- parent object and frame: `d8c71832031c7777fbed2f8f9aea9a1d833a5d039dc2d99963d1b630346eaa99`
- candidate source: `5d25bd00982c74099228158f6f2190505655c8ab3c3e246a92a6d00ac15d52bd`
- candidate closure: `312b5960db7f649cda1178329d26e2720ff96e255edb65d180f877f02ff2353e`
- candidate resources: `fa700da5ff5e6f4ada6af2967b83e02dceb47dbaa1a68ed6d341d0a83f6f1f70`
- resource comparison: `6e9f9d04a93f1c9d455e63a0e1345b37057b36c71f96cb9b765a5ddb2d2ccf1e`
- candidate object and frame: `ea33e6121df1bf295bf91a99253266dd5496c6c3d8ddffe7cce24c4fe2247e57`
- object and frame comparison: `8296cb404f7f0f8c71754814603ecb7ab132edb3b388b058c2a86d0a9aa6259c`

## Evidence states and residuals

The host fixtures establish the exercised provider-result, allocation,
release, shortage and media-size contracts. The exact target build establishes
compilation and linkage for both shipped configurations. Neither evidence
class executes temporary-swap operations on RP2040 silicon.

The unit changes no syscall number, command value, public layout, static
storage or packed-root artifact. The matched static comparison accepts only
the recorded eight-byte flash exception. Physical-board execution, runtime
stack and heap peaks, storage traffic, general input and bidirectional ioctl
marshalling, malformed command geometry, remaining temporary-swap operations,
MPU behavior and persistence recovery retain separate evidence states or
migration units.
