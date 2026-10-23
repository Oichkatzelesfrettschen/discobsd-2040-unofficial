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
The candidate static comparison remains open until a clean committed source
identity produces its matched distribution, closure and resource receipts.

The exact-source fixture executes argument admission and handler selection. It
does not execute the naked exception-entry assembly, longjmp recovery, syscall
handlers, signal-context copying, MPU enforcement or silicon. Runtime stack,
heap and storage traffic remain unmeasured. Physical-board execution remains a
separate authorization-gated evidence class.
