# RP2040 syscall stack-argument admission

The RP2040 syscall adapter formerly admitted the fifth and sixth argument with
separate one-byte `baduaddr` probes and then loaded a complete `u_int` from each
accepted address. A rejected probe left the corresponding `u.u_arg` word from
the previous syscall intact, and the adapter still called the selected handler.
The same path left all six words intact for a zero-argument syscall. The
one-byte probe therefore established neither a complete stack word nor the
one- or two-word tail that a handler consumed.

ARMv6-M exception entry pushes eight words on the process stack. The port's
`PendSV_Handler` replaces the stacked `r12` word with the process stack pointer,
so `tf_sp` names the base of that hardware frame. The first stack argument is
32 bytes beyond the base. `XPSR_STKALIGN` records one additional four-byte
alignment word. The adapter preserves those offsets and the established rule
that every nonzero-argument call publishes `r0` through `r3`, including calls
whose metadata declares fewer than four arguments.

`sys/arch/rp2040/rp2040/syscall.c` now clears the fixed six-word `u.u_arg`
array before argument publication. Metadata above that ABI returns `EINVAL`
without copying or dispatching. For five- and six-argument calls, the adapter
checks the frame-offset addition before constructing the user address and asks
`copyin` to admit and copy the complete four- or eight-byte tail. `copyin`
already validates the complete non-wrapping process-window span before its one
`bcopy` publication. An invalid, truncated or otherwise rejected tail produces
`EFAULT`; the adapter reaches neither `setjmp` nor the handler while an
admission error remains. Syscall numbers, handler selection, the public
six-word layout, register argument publication, return values and carry-bit
error reporting retain their existing interfaces.

`tests/kernel/syscall_args_extract.py` copies the exact block from dispatch-slot
selection through handler invocation and records the full production source
hash. `syscall_args_test.c` compiles that block at ILP32, asserts four-byte
pointer, integer and size widths, and gives dereferenceable host storage
separate logical user bounds. The fixture covers five and six arguments,
alignment padding, one-argument register publication, zero-argument clearing,
invalid-number routing, an invalid fifth-argument address, a truncated sixth
argument, forced `copyin` failure, seven-word metadata and wrapped frame
arithmetic. Rejected cases require the declared error, cleared unpublished
words and zero handler and `setjmp` calls. A generated mutant restores handler
dispatch after admission failure; the same executable must reject it with
status 1.

The unmodified parent block failed 23 named assertions. The failures included
stale fifth and sixth words, zero calls to the complete-span copy seam,
successful dispatch after invalid and truncated tails, successful dispatch
after injected copy failure, acceptance of seven-word metadata, and dispatch
after wrapped frame arithmetic. The repaired block and the production
dispatch-after-failure mutant provide the positive and negative controls for
the retained gate.

The clean parent at `40b9247b32c22d0984072256a7961a0faf5ba605` used
`/usr/bin/python` 3.14.8, bmake 20260912 and Arm GCC 16.2.0. Its clean and
distribution stages completed in 4.9 and 146.9 seconds. The capture accounts
for all 36 shipped artifact groups, 173 tracked C/header paths in PICO, 165 in
PICO_UART, and a 43,010-byte native board `libc.a` containing 151 members. The
parent source, closure and resource receipts have SHA-256 values
`a64d3e44d354e1dd4ac2828a58eefbd73d4b384529f9fd943d8c3ee7ce900190`,
`8ffae6c92d919de62495a26c701789076c8ac85961688c6d2d65aa734b4b7a15` and
`768ffab7e821170eb5d07bb08dfa0ee7a37984e413b9875b4568e7edde3b8f07`.
The committed candidate at `d5193996c796fbda4b9681f7c2640775dba8a505`
completed its clean and distribution stages in 10.0 and 207.3 seconds. The
candidate closure retains the same 36 shipped artifact groups, 173 PICO and
165 PICO_UART C/header paths, and 151-member, 43,010-byte native board
`libc.a`. The source snapshot, closure, resource and delta receipts have
SHA-256 values
`f4c7da90841759de47156fa08ec6fba83f50dde65281ffda0620dc0012b4865f`,
`8c89769942338b81a80a301f776e1b381f8293ddca4a93ff5afcf45ea32892fe`,
`58f5d6ec6502b2449b17f5175222c27b6908683e5e1b3ed9413b0c246a2eabb2` and
`2a5308c4bd154d7d30f21f166d98a7b3c43ded03ea10cb944af151d7b17af91b`.

The resource comparison records the following constrained surfaces:

| Surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,758 | 104,806 | +48 | 26,266 bytes |
| PICO SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| PICO_UART loaded flash span | 92,276 | 92,332 | +56 | 38,740 bytes |
| PICO_UART SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| Native board `libc.a` | 43,010 | 43,010 | 0 | unchanged |
| Packed-root data blocks | 692 | 692 | 0 | 287 free blocks |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Selected archive-member sets | measured baseline | unchanged | 0 | unchanged |

The resource comparer rejects the two positive flash deltas by design pending
review. The candidate requests the bounded exception because complete tail
copying, overflow rejection and dispatch suppression are required to enforce
the six-word syscall ABI. Removing those checks would restore the reproduced
stale-argument, partial-admission or post-failure dispatch defects. The change
consumes 48 bytes in PICO and 56 bytes in PICO_UART while leaving kernel SRAM,
every process image, root occupancy and dependency selection unchanged. Review
of the exact receipts and retained margins decides the exception.

The exact-source fixture executes argument admission and handler selection. It
does not execute the naked exception-entry assembly, longjmp recovery, syscall
handlers, signal-context copying, MPU enforcement or silicon. Runtime stack,
heap and storage traffic remain unmeasured. Physical-board execution remains a
separate authorization-gated evidence class.
