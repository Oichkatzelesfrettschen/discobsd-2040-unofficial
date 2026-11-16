# Asynchronous system traps on the RP2040: SysTick never reaches userret

This record states the model, prediction and falsifier written before the
kernel change of branch `rp2040-systick-ast`; the measured results follow
in their own section, so a deviation stays visible beside the prediction.

## Observation

A benchmark session reported that CPU-bound programs that make no system
call (dhry2reg, hanoi, arithoh) are never stopped by their `alarm()`. The
report is unverified on a board; the Renode gate below is its first
reproduction.

## Governing invariant

In 2.11BSD every trap and interrupt that interrupted user mode returns
through the asynchronous-system-trap (AST) check: `hardclock` and the
callouts post signals (`psignal`) and set `runrun` (`schedcpu` does so
once a second, `setrun` and `wakeup` when a better-priority process
becomes runnable), and the return to user mode runs `issignal`/`postsig`
and calls `swtch` while `runrun` is set. Here that check is `userret`
(sys/kern/kern_sig.c).

## Mechanism in source (origin/main 4b3eaf6c)

- `SysTick_Handler` (sys/arch/rp2040/rp2040/systick.c) selects the
  stack holding the exception frame, tail-branches into `systick`, which
  calls `hardclock` and returns to the interrupted instruction. Nothing
  on that path calls `userret` or pends an exception that does.
- `userret` has two callers: `syscall` (PendSV_Handler, after an SVC)
  and `arm_fault` (sys/arch/rp2040/rp2040/syscall.c, fault.c).
- Device interrupts (UART, USB) return directly as well, so a `psignal`
  from the terminal's interrupt character waits for the next system call.
- Profiling ticks are not affected: `hardclock` charges the tick and
  `softclock` calls `addupc` from the stacked xPSR (`USERMODE`), which
  needs no return through `userret`.

So a process that makes no system call receives no signal (SIGALRM,
SIGINT, SIGXCPU, SIGVTALRM, SIGPROF) and is never preempted, and every
other process, including one woken by a timeout, waits until it exits.

## Hypothesis

Pending PendSV from `systick` when the tick interrupted thread mode on
the process stack (EXC_RETURN 0xfffffffd) and the process has work
waiting (`runrun` or an unmasked pending signal) gives the AST a
lowest-priority handler that runs only when no other exception is active,
on the kernel stack and in the trapframe layout the system-call path
already builds. PendSV must then tell an SVC from an AST, which a flag
written by `SVC_Handler` decides: a SysTick taken while the SVC's PendSV
is pending sees thread mode too.

An asynchronous entry differs from an SVC in three ways the system-call
path never had to honor, and each is part of the change:

1. r12 is live at an arbitrary instruction (`__aeabi_idiv` keeps a sign
   mask in ip), but PendSV_Handler stores PSP into the stacked r12 slot on
   the way out and `struct sigcontext` has no r12 field (`sc_sp` aliases
   `sc_ip`).
2. The SIO divider is per-core state with no owner but the running
   process (sys/arch/rp2040/doc/MPU.md, lib/libc/arm/gen/aeabi_div.S). A
   switch or a signal handler between a divisor write and the quotient
   read hands the sequence another process's or the handler's results.
3. The stack bookkeeping `syscall` performs at entry (`p_ssize`,
   `p_saddr`, which swap-out reads) must also run at an asynchronous entry,
   where the stack can be deeper than at the last system call; and the
   hardware frame carries the STKALIGN pad whenever sp is 4 modulo 8.

## Falsifier and deciding capture

`check-renode-preempt` (tools/renode/check-preempt.py) boots PICO_UART
under Renode with tests/rp2040/preempt staged under /tmp and runs five
cases, each in its own boot so that one wedged case does not hide the
others:

| case | program | pass condition |
|---|---|---|
| alarm | `preempt alarm`: `alarm(1)`, then a loop with no system call | the shell reports status 142 (SIGALRM) |
| share | `preempt share`: two looping children, the parent sleeps 3 s and signals both | both children counted iterations before the signal and the parent woke |
| intr | `preempt spin` and the interrupt character typed on UART0 | the shell reports status 130 (SIGINT) |
| regs | `preempt regs`: two processes hold sentinels in r1-r12 and lr, one at sp 0 and one at sp 4 modulo 8, across 5 ms SIGALRM handlers that clobber r8-r12 | every register still holds its sentinel after 200 handler runs |
| divide | `preempt divide`: two processes divide distinct operands on the SIO divider while their own 3 ms handlers divide, and a third process wakes every 2 ms to force switches | every quotient and remainder equals a software division |

The report is false if all five pass on origin/main.

## Prediction

- origin/main: alarm, share and intr time out (no AST); regs and divide
  end on their iteration cap with zero handler runs and fail.
- Branch: all five pass; check-renode, check-renode-union and
  check-renode-capacity pass unchanged.
- check-divider rejects the kernel until its allow-list admits the
  divider save and restore, the only kernel divider accesses.
- Kernel text grows by 250 to 400 bytes per configuration and kernel RAM by
  4 bytes (the SVC flag); each entry from user mode uses 24 more bytes of
  kernel stack.
