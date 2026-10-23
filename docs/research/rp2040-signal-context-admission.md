# RP2040 signal-context admission

The RP2040 `sigreturn` path formerly probed the first byte of the user signal
context and the byte immediately after it, then restored every process field
through direct typed loads from mutable user storage. The second probe rejected
an otherwise valid context ending exactly at the process-window boundary. The
two probes did not establish that the 72-byte object was readable, and direct
loads could observe multiple versions if user storage changed while the
kernel restored it. A misaligned context also made those word loads unsuitable
for ARMv6-M.

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
snapshot and restores direct user loads; the fixture must reject it with exit
status 1.

The inherited implementation fails the calibrated fixture on exact-span
admission, complete-copy use, injected-copy failure and wrapped address
construction. The candidate fixture passes, and the direct-user mutant fails
with the required status. `check-test-execution` validates the run/skip
inventory, and `check-kernel-ilp32` owns the executable gate.

The local snapshot introduces a 72-byte C object on the kernel stack. Static
compiler stack-usage output and matched PICO/PICO_UART distribution receipts
measure the candidate against its clean parent before integration. Static
frame size does not establish runtime peak stack. The resource receipt keeps
runtime stack, heap and storage traffic unmeasured until their own authorized
workloads run.

The exact-source fixture does not execute the signal trampoline, exception
entry or return, MPU enforcement, interrupt behavior or silicon. The retained
board signal-frame record predates the candidate and establishes only its
identified image and workload. Physical-board execution for the candidate
remains a separately authorized evidence class.
