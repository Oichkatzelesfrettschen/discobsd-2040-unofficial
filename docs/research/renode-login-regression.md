# The Renode login test failed on a context-switch store one field short of p_addr

## Finding

`bmake check-renode`'s second test, "The boot reaches a login prompt and a
shell", timed out waiting for `/etc/rc`'s first line because the kernel had
already panicked with `panic: wakeup` and was sitting in `cngetc()` behind
`press any key to reboot...`. The terminal tester records only the lines it
waits for, so the panic text never reached a log; a diagnostic Robot run that
paused the machine 45 s after `swap size = 380 kbytes` and read the CPU
showed PC in `uart_rx_ready` with PRIMASK set, LR in `cngetc()`, and a
return chain `panic <- wakeup <- tsleep <- select1` on the stack. Reading
`sysbus.uart0 DumpHistoryBuffer` gave the transcript, which ends:

    swap size = 380 kbytes
    panic: wakeup
    syncing disks... done
    halted
    press any key to reboot...

## Mechanism

`wakeup()` (sys/kern/kern_synch.c) panics when a process on a sleep-queue
chain has `p_stat` other than `SSLEEP` or `SSTOP`. The chain was corrupt
because every context switch wrote over `p_link`, the run-queue link.

Each port's `locore.S` stores the u-area address into
`u.u_procp->p_addr` while it exchanges `u.` and `u0.`, as `str r1, [r3, #60]`
on the ARM ports and `sw $a0, 60($v1)` on pic32. The constant is the field's
offset, which the assembler cannot take from `sys/sys/proc.h`. Commit
`2d2261a1` (#129) widened `p_uid` from `short` to `uid_t`; the four-byte
alignment that follows moved every later field down by four, `p_addr` from
60 to 64 and `p_link` from 56 to 60. The three locores kept 60, so each
switch stored the u-area pointer into `p_link`, and the first process to wake
another walked a run-queue link into the u area.

The offsets, from the struct as `sys/sys/proc.h` lays it out at ILP32:

    field      before #129   after #129
    p_uid      14 (short)    16 (uid_t)
    p_stat     24            28
    P_link     56            60
    P_addr     60            64

## Fix

`sys/sys/proc_asm.h` carries `P_ADDR_OFFSET`, the three locores include it
and store through it, and `sys/kern/kern_proc.c` holds the constant to
`__builtin_offsetof(struct proc, p_un.p_alive.P_addr)` with a `_Static_assert`,
so the next field added or widened ahead of `p_addr` fails the kernel build at
the constant. Calibration: with `P_ADDR_OFFSET` set to 60 the PICO_UART build
stops on `static assertion failed`; at 64 it builds, and `objdump -d` of the
rp2040, stm32 and pic32 `locore.o` shows every store at offset 64.

`tools/renode/boot.robot` gains a test teardown that logs
`sysbus.uart0 DumpHistoryBuffer` when a test fails, so the next panic behind a
timeout appears in the run output and the Robot log instead of needing a
second diagnostic run.

## Evidence

| Rank | Source | What it shows |
| --- | --- | --- |
| 5 | `check-renode` on `097ce75d` before the fix | login test times out at the rc banner; probe test passes |
| 5 | diagnostic Robot run, PC/LR/SP/PRIMASK and stack read at the hang | `uart_rx_ready` <- `cngetc` <- `panic` <- `wakeup` <- `tsleep` <- `select1`, PRIMASK 1 |
| 5 | `sysbus.uart0 DumpHistoryBuffer` | the `panic: wakeup` transcript above |
| 3 | `sys/sys/proc.h` at `2d2261a1~1` and `2d2261a1` | `p_uid` widened; `p_addr` 60 to 64 |
| 4 | `sys/arch/{rp2040,stm32,pic32}/*/locore.S` | the literal 60 in all three |
| 5 | `check-renode` after the fix | probe 2.05 s, login test 23.5 s, both pass, warning classes unchanged |

The board (rank 1) has not run this kernel; the panic mechanism is in
machine-independent code with an architecture-neutral cause, and the stm32
and pic32 kernels carry the same defect and the same fix without an emulator
run here (their `locore.o` objects were assembled and inspected only).

## What stays open

`check-renode` stands outside `check` and CI never runs it, so a
kernel-level regression that the host gates cannot see waits for someone to
run the emulator. The `_Static_assert` closes this class, a struct layout
constant copied into assembly; it does not close the general gap. A CI job
that installs the Renode portable package and runs `check-renode` on the
Linux runner is the next step.

The window named before the bisect, #117..`0b35cbcc`, was correct; the
bisect was replaced by reading the CPU at the hang, one emulator run instead
of five.
