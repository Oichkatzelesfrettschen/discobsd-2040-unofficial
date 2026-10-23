# RP2040 signal-context admission

The RP2040 `sigreturn` path formerly probed the first byte of the user signal
context and the byte immediately after it, then restored every process field
through direct typed loads from mutable user storage. The second probe rejected
an otherwise valid context ending exactly at the process-window boundary. The
one-past probe was an indirect and overstrict span check instead of a complete
copy. Direct loads could observe multiple versions if user storage changed
while the kernel restored it. A misaligned context also made those word loads
unsuitable for ARMv6-M.

The signal trampoline enters syscall 103 with `tf_sp` at the base of the
eight-word hardware frame and the saved `struct sigcontext` 32 bytes above it.
The repaired path checks the 32-byte addition before constructing the user
address. One `copyin` call then admits and copies the complete 72-byte object
into an aligned local `struct sigcontext`. `copyin` already rejects a span that
leaves the admitted process window or wraps its end. `sigreturn` publishes
`EJUSTRETURN`, alternate-stack state, the signal mask and trapframe registers
only after that copy succeeds. A rejected address, truncated span or injected
copy failure returns `EFAULT` with those states unchanged.

The change preserves syscall 103, `struct sigcontext` size and offsets, the
trampoline's 32-byte frame convention, `sigcantmask` filtering and every
handler-visible register value. The snapshot does not authenticate provenance:
a handler can modify its own saved context before return. The change also
preserves the existing unfiltered `sc_psr` restoration. Defining the allowed
xPSR bits requires a separate contract covering BSD handler semantics,
ARMv6-M exception-return rules and a deliberate hardware test.

`tests/kernel/sigreturn_context_extract.py` extracts the exact production
function and binds the generated source to the complete production-file hash.
`sigreturn_context_test.c` compiles the function at ILP32, asserts the signal
context and trapframe ABI, and separates host storage from logical user bounds.
The executable covers an exactly bounded context, a misaligned source, an
invalid first byte, a truncated final byte, injected `copyin` failure, wrapped
`tf_sp + 32` arithmetic and a successful restoration of every saved register,
mask and alternate-stack flag. Each rejection must preserve the trapframe,
mask, stack flags and unpublished return state. A generated mutant removes the
snapshot and restores direct user loads. A second generated mutant performs
the complete copy and then points restoration back at user storage. The copy
seam changes the admitted user bytes after copying while the oracle retains an
independent expected context, so the fixture must reject both mutants with exit
status 1.

The inherited implementation fails the calibrated fixture on exact-span
admission, complete-copy use, injected-copy failure and wrapped address
construction. The candidate fixture passes, and the direct-user mutant fails
with the required status. The copy-then-user-reread mutant also fails with the
required status. `check-test-execution` validates the run/skip inventory, and
`check-kernel-ilp32` owns the executable gate.

The clean parent at `cdc0e4987156e7a980a84bb2bec823828e1bbf6a` and the
committed candidate at `80ecf12b926913ab8480e5bcc426fb9e5b07d270` use
Arm GCC 16.2.0, GNU ld 2.47.20260726 and bmake 20260912. Parent clean and
distribution stages completed in 10.1 and 308.7 seconds; candidate stages
completed in 10.2 and 210.4 seconds. Both closure receipts account for 36
shipped artifact groups, 173 PICO C/header paths, 165 PICO_UART C/header
paths and the same 151-member, 43,010-byte native board `libc.a`.

The parent source, closure and resource receipts have SHA-256 values
`5ab948a4617e21db6ae3a1598c841550cbd2f64a204a586e4d36d22e71c73b57`,
`a823965ec1471b822129a621ac7fc4c33d8d936517af878bd07620b8f051cda5`
and `4813cc1487d3885eaa7f930a2ab105469b970826ed24c6a55e6828e245bb5267`.
The candidate source, closure and resource receipts have SHA-256 values
`ac0840aad431356fd009e547e13dd81d89d1bafcbaa925a5b13bacbc4ad89912`,
`675c11a5236eadc219627f812e976c34712def865e28798be478a3d2652f4048`
and `663086b0ae639f9a0922fff398d40648aecf61947c1adf2daf57aff7d76502c6`.
The delta receipt has SHA-256
`5db9a2fb4ef2fb07eae27d5ee701998b379e6c30fd9b2739a482fb8b51e425e2`.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO loaded flash span | 104,808 | 104,808 | 0 | 26,264 bytes |
| PICO SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| PICO_UART loaded flash span | 92,332 | 92,324 | -8 | 38,748 bytes |
| PICO_UART SRAM allocations and extents | measured baseline | unchanged | 0 | unchanged |
| Native board `libc.a` | 43,010 | 43,010 | 0 | unchanged |
| Packed-root data blocks | 692 | 692 | 0 | 287 free blocks |
| Every shipped a.out size and packed occupancy | measured baseline | unchanged | 0 | unchanged |
| Selected archive-member sets | measured baseline | unchanged | 0 | unchanged |

The local snapshot introduces a 72-byte C object on the kernel stack. Exact
recorded compiler commands with `-fstack-usage` report the following static
frames; the companion object-size and disassembly records bind each result to
the source and command capture.

| Configuration | Parent | Candidate | Delta | `sigreturn` code |
| --- | ---: | ---: | ---: | ---: |
| PICO with SYSTRACE | 32 | 112 | +80 | unchanged at 248 bytes |
| PICO_UART | 16 | 80 | +64 | 180 to 176 bytes |

The parent and candidate stack receipts have SHA-256 values
`00aaaabe90c01bc4fa50a2cc1fce84e8f2433c69028b949996d5720d43d6ab95`
and `245bbdc053a09c5cd5e69cdfbd19736993b886f2a468c71fb5218ffdb28efbf3`.
The static increase is a measured exception that requires explicit review.
The complete local snapshot is the mechanism that prevents partial admission,
misaligned typed user loads and field-by-field rereads; removing the storage
would restore those defects. Static frame size does not establish runtime peak
stack. The retained 220-byte stack-paint remainder belongs to an older image
and workload and supplies no budget for this change. Resource receipts keep
runtime stack, heap and storage traffic unmeasured until their own authorized
workloads run.

The exact-source fixture does not execute the signal trampoline, exception
entry or return, MPU enforcement, interrupt behavior or silicon. The retained
board signal-frame record predates the candidate and establishes only its
identified image and workload. Physical-board execution for the candidate
remains a separately authorized evidence class.
