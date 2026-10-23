# RP2040 syscall I/O span admission

The `read`, `write`, `readv`, and `writev` syscall path previously bounded the
sum of vector lengths but passed each `iov_base` to the selected file operation
without admitting its complete span. Inode I/O later reaches `uiomove()` and
raw `bcopy`; tty readers can write through `ureadc`. A nonempty vector outside
the process window could therefore reach transfer code without passing the
complete-span checks already used by `copyin()` and `copyout()`.

Implementation commit `838270f097111657b60d263b35c5bf91f08b28ff` validates
every nonempty vector in the shared `rwuio()` path before `setjmp()`, file
operation dispatch, offset movement, or transfer publication. The kernel
service `baduaddr_span(caddr_t, u_int)` accepts a complete nonwrapping span
inside the active user window. RP2040 keeps its conditional `USER_TOP`
extension, and STM32 provides the same service for its fixed user window.

## Contract and caller closure

`read`, `write`, `readv`, and `writev` construct the existing `struct uio` and
call `rwuio()`. The shared function preserves the established order:

1. Resolve the descriptor and required access mode.
2. Sum every vector length and reject a total above `SSIZE_MAX` with `EINVAL`.
3. Admit the complete span of every nonempty vector and reject a failed span
   with `EFAULT`.
4. Establish the interrupt return point and invoke the selected file operation.
5. Publish the transferred byte count from the saved total and final residual.

The second pass makes admission transactional across the complete vector set.
A bad final vector following valid vectors reaches no file operation. Rejection
preserves the file offset, syscall return value, destination sentinel, and file
operation call count. Empty vectors retain their prior behavior and do not
inspect an unused base address.

Trusted kernel `rdwri()` callers remain outside this syscall-origin boundary.
The change preserves syscall numbers, argument counts, dispatch declarations,
the `iovec` and `uio` layouts, the sixteen-vector limit, descriptor and access
checks, `SSIZE_MAX` error precedence, short-transfer behavior, and interrupted
transfer behavior. The change adds no allocation or transfer buffer.

## Calibrated behavior evidence

The inherited fixture passed 210 assertions. The new refusal assertions ran
against the inherited production source before implementation and reported 10
failures among 225 assertions. Invalid scalar buffers and invalid final vectors
reached the file-operation stub and advanced the modeled offset.

The implemented fixture passes 226 assertions at native width and ILP32. It
covers read and write directions, scalar and vector entry points, the first and
last process-window byte, a full window, below-window, ceiling-crossing and
wrapped spans, empty vectors, the conditional window extension, descriptor and
access errors, sum overflow, copy failure, short transfer, interruption, and
complete refusal-state preservation.

Two independent source mutations calibrate the deciding behavior. One removes
the admission pass. The other reduces admission to a one-byte address check.
Each mutant must exit with assertion status 1 and fail the named refusal
condition. The existing endpoint-wrap mutation continues to calibrate the
RP2040 span service. The exact service type is checked with `_Generic`.

`check-kernel` runs the rwuio fixture under native address and
undefined-behavior sanitizers, including alignment checks, when the compiler
probe admits the runtime. `check-kernel-ilp32` executes the target-width
fixture. Exact PICO and PICO_UART production commands compile the changed
kernel objects as GNU17 for Cortex-M0+. The maintained STM32 source compiles
under its production warning profile, and `sys_generic.c` also compiles for
STM32 with `-Wall -Wextra -Werror`.

The source denominator records the caller-complete read/write seam as accepted
inside migration-open `sys_generic.c`. `ioctl`, select, socket stubs, and the
remaining translation-unit semantics retain separate migration units. The
kernel inventory removes the five old-style-definition findings for `rwuio`,
`read`, `readv`, `write`, and `writev`.

## Measured target resources and exception

Clean schema-2 distributions compare parent
`6f7cce1a135f4049b4f834490e9f07757aeae3a1` with implementation commit
`838270f097111657b60d263b35c5bf91f08b28ff`. Both captures used bmake
20260912, arm-none-eabi-gcc 16.2.0, GNU ld 2.47.20260726, and Python 3.14.8.
The closure receipts account for 36 shipped artifact groups, 173 PICO C/header
paths, 165 PICO_UART C/header paths, every installed a.out, the native startup
object and libc archive, and the packed root.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO flash bytes | 104,918 | 104,958 | +40 | 26,114 bytes below 128 KiB |
| PICO `.bss` / SwapRAM pool / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 | unchanged |
| PICO_UART flash / `.bss` bytes | 92,428 / 14,832 | 92,484 / 14,832 | +56 / 0 | 38,588 flash bytes below 128 KiB |
| Native `libc.a` bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 | unchanged |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 | unchanged |
| Packed-root free inodes | 33 | 33 | 0 | unchanged |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Every selected archive/member set | measured baseline | unchanged | 0 | unchanged |

Exact recorded compiler commands replayed with `-fstack-usage` attribute the
growth to the admission boundary. `sys_generic.o` gains 44 text bytes in each
configuration, all in `rwuio()`. RP2040 `machdep.o` is unchanged in PICO and
gains eight text bytes in PICO_UART because the previously translation-unit
local predicate becomes callable. `rwuio()` retains its 32-byte static frame.
PICO retains the 16-byte `baduaddr_span()` frame and the 24-byte `copyin()` and
`copyout()` frames. PICO_UART emits a new eight-byte-frame span service;
`copyin()` and `copyout()` each move from an eight-byte frame with an inlined
predicate to a 24-byte frame that calls the shared service. Per-function
compiler frames do not establish a complete call-chain or runtime high-water
mark.

The resource comparer correctly rejects the +40-byte PICO and +56-byte
PICO_UART flash deltas under the default nonpositive-growth rule. The bounded
exception accepts those two linked flash deltas and the PICO_UART static-frame
changes for integration. The added code closes a reproduced syscall
trust-boundary defect for all four read/write entry points; removing the pass or
checking only the first byte restores the calibrated failure. The change adds
zero static SRAM allocation, process-window, packed-root, native-library, or
archive-selection consumption. Runtime kernel stack high-water remains open.

The receipt hashes are:

| Receipt | SHA-256 |
| --- | --- |
| Parent source snapshot | `d43b7ee01143ef334a0ef694838bc75c4bc9edd45bff089a4f4c1d8cab8d1e05` |
| Parent dependency closure | `2cc3f0bc139a35ee0d7d669ac09893aa1aa2379390177ce44e13a53247a89fb7` |
| Parent schema-2 resources | `7ae15d0cf1828aca15374769046e0c49fcaf35e93e69eb0dbd3dbff0e7028504` |
| Candidate source snapshot | `77d57ac3cb5c4933802b6dd4ba9d83cbbd40040f0c012b3e38517834ca08fbeb` |
| Candidate dependency closure | `6a3b9720221d103cf61c6b5b6944e6c68863d0b7020d0e836f2670f36e453f83` |
| Candidate schema-2 resources | `655db70c05124914d059cd73781c32b8e680a31f56981f09adc651bb8adc1a55` |
| Static-resource delta | `f509a4b0ccb343964682b74ebedac0edc0a1a9e7e67540d61e802ff5f3062888` |
| Parent exact-command object/frame receipt | `d80722581e48438280d5a4d9c608634801e4bfba157162bd36f46612f60ef26e` |
| Candidate exact-command object/frame receipt | `9084dd3f266a5a42a2a3a75e3d38effe06a13fc70faf2f3510862820da63db7d` |
| Object/frame comparison | `ab3df94f300e90a10373f6af5a41796512f18be0955f38149b8f8e01d775033e` |

## Evidence states and residuals

The host fixtures establish the exercised syscall admission, error precedence,
and refusal-preservation contracts. The exact target builds establish GNU17
Cortex-M0+ compilation, linkage, and static resource measurements. The checks
do not execute a transfer on RP2040 silicon or establish MPU enforcement.
Runtime kernel stack and heap peaks remain unmeasured. Read and write do not
add persistent writes, so this unit makes no flash-wear claim.

Direct-address consumers in `ioctl`, `/dev/mem`, and `ptrace` retain separate
trust-boundary obligations. `ptrace` needs an ARM writable-register and PSR
policy before bounding `PT_WRITE_U` can establish a complete contract. Select
and socket semantics in `sys_generic.c`, wider `uiomove` origin tracking, and
the broad `sys/systm.h` declaration boundary remain separate migration units.
Physical-board execution requires separate authorization for the exact image
and workload.
