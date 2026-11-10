Source readings and host measurements only; no board or Renode run backs any line.

# A DragonFly message-port architecture for the RP2040, audited

A proposal titled "Decomposition of DragonFly BSD to First Principles for
RP2040" asks for five DragonFly "axioms" (per-domain ownership, single-writer
rings with a doorbell, `EASYNC` completion, a universal message port, and
vkernel isolation) to be adopted whole, with core 1 running the LCD, the
Dhara flash translation layer, V7FS and USB behind `lwkt_msg` ports. This
note tests each claim against DragonFly's source, the RP2040 datasheet
readings the tree already holds, and the port itself.

Result: the ring, the doorbell and the completion convention survive, and
they were already the decided design in `rp2040-second-core.md` and section
5.2 of `rp2040-second-core-bsd-survey.md`. The universal port, the vkernel
placement, all four subsystem placements and the proposed C code fail. The
surviving transport is implemented as `sys/arch/rp2040/rp2040/corering.c`
behind the `check-corering` host gate; it is linked into no kernel, and no
kernel launches core 1.

## Evidence base

- DragonFly BSD at release tag v6.4.2 (`54d71181`) and at master
  `94448f62` (2026-11-08), fetched from the project's GitHub mirror.
  `sys/kern/lwkt_ipiq.c`, `sys/kern/lwkt_msgport.c`, `sys/sys/msgport.h` and
  `sys/sys/msgport2.h` are byte-identical between the two, so every line
  number below holds for both. Paths are relative to the DragonFly tree.
- This repository at `eb5b3081`.
- `arm-none-eabi-gcc` 16.2.0 with `-mcpu=cortex-m0plus -mthumb -Os
  -std=gnu17 -ffreestanding` for the size and instruction readings; host
  `cc` GCC 16.2.1 for the gate.

## The five axioms

| Claim | Verdict | Authority |
| --- | --- | --- |
| Every mutable object has one owning CPU; cross-core locks are prohibited | Survives as a design rule here. False as a description of DragonFly | `lwkt_msgport.c` calls `spin_lock()` or `spin_unlock()` on nineteen lines and `lwkt_ipiq.c:313` signals with an atomic exchange; the rule holds on two M0+ cores because ARMv6-M offers nothing to build a lock from (survey section 3.1) |
| Requests cross cores on single-producer rings with publish-then-advance | Survives | `lwkt_ipiq.c:302-307` stores the entry, executes `cpu_sfence()`, then increments `ip_windex`. The ring carries a function and two arguments, not message descriptors |
| The doorbell is elided when one is already pending | Survives with a different mechanism | `lwkt_ipiq.c:313` elides with `atomic_swap_int(&target->gd_npoll, 1)`, an atomic exchange ARMv6-M lacks. The SIO FIFO's `RDY` bit replaces it; the argument that a skipped push strands nothing depends on the consumer emptying the FIFO before its snapshot, which the proposal's code omits because it has no consumer |
| A message completes inline or returns `EASYNC` | Survives | `lwkt_msgport.c:98-110`; the kernel glue that sleeps or polls on a slot belongs with the core-1 skeleton |
| Character devices, file systems and system calls all map to port commands | Fails | `MSG_CMD_CDEV`, `MSG_CMD_VFS` and `MSG_CMD_SYSCALL` are defined at `sys/sys/msgport.h:100-102` and referenced by no other file under `sys/` (`git grep MSG_CMD_ -- sys`). DragonFly dispatches devices through `dev_ops` and file systems through `vop_ops` |
| `lwkt_msgport.c` compiles identically for kernel and userland | Fails | The comment at lines 34-35 says so; the file includes the VM, spinlock and SMP headers, and no file under `lib`, `bin`, `sbin`, `usr.bin`, `usr.sbin` or `libexec` references it |
| Subsystems run as userland programs the way a vkernel does | Fails | A vkernel is a whole kernel in a process built on virtual page tables (`sys/platform/vkernel64/platform/init.c:580-609`, `MAP_VPAGETABLE`, which that file's own comment marks for removal). The RP2040 has no MMU and the port keeps one process resident in one 192 KB window (`sys/arch/rp2040/doc/MPU.md`), so a server process costs two swaps per message. `tiny-rump-components.md` already rejected this placement |
| The result has "zero locks across CPUs" and keeps "100% of DragonFly's invariants" | Fails | The proposal's own silicon table assigns the 32 SIO spinlocks to "ring buffer credit allocation". A credit count has one writer, the requesting core, and needs no lock |

## Hardware and port facts

| Claim | Verdict | Authority |
| --- | --- | --- |
| `FIFO_ST` 0xd0000050, `FIFO_WR` 0x54, `FIFO_RD` 0x58, `RDY` is bit 1, IRQ 15 and 16 | Correct | Datasheet 2.3.1.4 and 2.3.2 as read in `rp2040-second-core-hardware.md` |
| A `dmb` "flushes CPU write buffers" before the index store | Harmless, wrong reason | A Cortex-M0+ has no write buffer and no data cache. GCC already emits `dmb` before a release store and after an acquire load on ARMv6-M, which the disassembly of `corering.o` shows |
| SRAM4 and SRAM5 are free for core-1 stacks, descriptors and DMA queues | Fails | `sys/arch/rp2040/conf/kern.ldscript` places the 8 KB USB transmit ring there on PICO and PICOCALC and asserts it |
| The user window is 144 KB | Stale | 192 KB (`MPU.md`); 144 KB is the former map |
| The LCD blocks core 0 "for 195 ms per frame" | Misread | 195 ms is the bus time of a full 1,696-cell repaint (`sys/arch/rp2040/doc/PICOCALC.md`); one cell costs about 122 us |
| Stock librump costs 229 KB of text and 76 KB of RAM | Text correct | 228,971 bytes of text; data plus bss measure 75,805 bytes (`tiny-rump-components.md`) |
| The message layer adds at most 600 bytes | True of the wrong thing | The three proposed functions compile to 100 bytes because they contain no consumer, no credit and no wait; `lwkt_initport` and `lwkt_domsg` are declared and never defined. The complete transport in `corering.c` is 296 bytes |

## Subsystem placements

Each placement contradicts a ruling in `rp2040-second-core.md`, and the
proposal brings no measurement that would reopen one.

- **Dhara flash translation layer on core 1.** Erase and program take XIP
  down for both cores (`sys/arch/rp2040/dev/flash.c`), the kernel executes
  from XIP, and with one resident process the caller has nothing else to
  run. Moving the translation layer moves the wait and shortens nothing.
- **V7FS on core 1 or as a `usr.sbin/v7fs` server.** `usr.sbin/v7fs` is a
  tool that edits image files. No kernel file system consumes the imported
  core, and the server placement needs the second resident image the layout
  excludes.
- **USB CDC pump on core 1.** `sys/arch/rp2040/dev/usb.c` is interrupt-driven
  and its polled path relies on core 1 never being launched. A core-1 pump
  shares tty state between cores, the option the hardware report rates high
  risk, and compute loops on core 0 already yield to the USB interrupt.
- **LCD on core 1.** Kept as a conditional last step: the SPI1 DMA
  experiment on core 0 runs first, and the console service is built only if
  DMA leaves more than a tenth of the render time on core 0.

## Defects in the proposed code

- The ring stores `lwkt_msg_t *`. A restarted core 1 would follow a pointer
  written before its reset; the decided design carries slot numbers.
- `lwkt_sendmsg()` reads and writes `mp_windex` without masking interrupts,
  so a core-0 interrupt handler that sends corrupts a process-level send.
- `lwkt_replymsg()` rings the doorbell and queues nothing. Core 0 learns
  that some message finished and must scan every message to find which.
- No consumer exists, so the order "empty the FIFO, then snapshot the index"
  that makes doorbell elision safe is unstated and untested.
- A reply path with no credit rule lets the request and reply rings fill
  against each other, the deadlock `lwkt_ipiq.c:208-262` exists to avoid.

## What was built

`corering.c` holds one ring per direction (32 entries of 8 bytes, free-running
indices, one writing core per word), the FIFO doorbell with the `RDY` test,
sticky-error accounting, and a requester-side credit count that guarantees a
free reply entry. It reaches the SIO block through two macros and includes
no kernel header.

`check-corering` compiles it unchanged against a model of the FIFO registers.
A deterministic run places the other core's action at each register access
and inside a handler, and checks after every step that unread entries imply
a pending FIFO interrupt. A two-thread run under the thread sanitizer
exchanges numbered requests and replies with random stalls, services a ring
only while the model's interrupt is pending, and fails when progress stops.
Sixteen mutants must fail, among them each swapped ordering, a doorbell that
ignores `RDY`, an off-by-one full test and three credit errors.

Writing the gate found one defect in the first draft: a credit returned
before the reply handler ran let that handler's next request be answered
into the reply entry still in use. The credit now returns after the
handler, and the `credit-before-reply-handler` mutant keeps it there.

This retires three falsifiers of survey section 5.8 at the host evidence
class: no message lost or duplicated, a full FIFO strands no entry, and
credits prevent ring deadlock. One unsanitized run of 10^8 round trips
(`corering_test stress 100000000`) passed in 99 seconds with 16,983,334
doorbells skipped on a full FIFO.

## Not run, and what stays open

- When this audit was written core 1 was not launched. The launch,
  `PSM.FRCE_OFF` on the reset paths, the SRAM vector table, the heartbeat
  and the observer have since been built as `core1.c`
  (`sys/arch/rp2040/doc/CORE1.md`). That work corrected one statement made
  here: Renode's RP2040 model does run a second CPU and an inter-core FIFO,
  and the kernel launches core 1 through the boot ROM under it. The model
  gap is the power-on state machine and the NMI route, not the FIFO.
  `CORE1.md` records the board run.
- The host gate runs on a strongly ordered x86 host with sanitizer-defined
  ordering. It says nothing about write ordering between SRAM banks on
  silicon; the board stress run of survey section 8 decides that.
- `corering.c` executes from flash as compiled on its own. `core1.c`
  includes it with the producer and consumer placed in `.ramfunc`, and
  `check-core1-image` holds the linked result to SRAM.
- The SPI1 DMA measurement that ranks the console service is not run.
