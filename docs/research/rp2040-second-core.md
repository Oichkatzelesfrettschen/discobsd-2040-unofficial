Drawn from the two companion reports below; no board run backs any figure except step 4's.

# RP2040 core 1 for DiscoBSD: what survives, in what order

This note decides how the port uses the RP2040's second Cortex-M0+ core. It
merges two investigations and keeps their evidence where it lives:

- `rp2040-second-core-bsd-survey.md`: DragonFly BSD's LWKT and MINIX 3's
  Reincarnation Server decomposed to first principles, the BSD trees under
  BSD_Workspace surveyed for multiprocessor and offload precedent, and a
  three-tier architecture with a falsifier per claim (its section 5.8).
- `rp2040-second-core-hardware.md`: the datasheet facts for core 1 (launch,
  FIFOs, spinlocks, per-core divider and interpolators, reset), the
  resources the port already owns, an instruction-level emulation of the
  packed-executable decoder, and candidates in the same three tiers.

Every figure below is a source reading, an emulation or an inference; none
is a board measurement. Each recommendation names the measurement that
would overturn it.

## What is ruled out, and why

A big-lock SMP kernel. The port keeps one process resident in one 192 KB
window (`sys/arch/rp2040/rp2040/mpu.c`) and exchanges a global `u` at a
fixed address in `resume` (`locore.S`, `sys/sys/user.h`), so a second CPU
has no second runnable image and every kernel path assumes one `u`.
DragonFly's own big lock is a token over per-CPU state the port does not
have, and NetBSD's big-lock bring-up took over a year per architecture. The
survey's section 6 carries the citations. A change to this conclusion needs
a second resident process, which the memory layout excludes.

A codec worker that runs while the caller waits. With one resident process,
the core-0 caller of a swap or exec decode has nothing else to do, so moving
the same serial work to core 1 hides nothing. Only splitting one stream
across both cores can gain, and only after the single-core decoder is fixed.

Hiding flash writes. Erase and program take XIP down for both cores; core 1
can keep running only from SRAM and ROM, and it cannot make a flash write
shorter.

## What DragonFly and MINIX contribute

From DragonFly LWKT, the mechanisms that need no atomic instructions survive
on two M0+ cores that share SRAM and have no cache: one owner per mutable
object; work shipped to the owner as a function and arguments; per-direction
single-writer rings (only the sender advances the write index, only the
receiver the read index); a doorbell sent only when the receiver is not
already pending; and `EASYNC` for "reply later". Tokens, serializers,
critical-section counters and the per-CPU scheduler are discarded: the M0+
has no atomics beyond the 32 SIO spinlocks, and core 1 runs no blocking
threads.

From the MINIX 3 Reincarnation Server, supervision survives: a heartbeat
answered from the service's own message loop, restart with backoff, state
held outside the service, and client retry. A restart cannot deliver
exactly-once semantics, so every operation a core-1 service accepts is a pure,
re-executable function of its request slot, and state that changes
incrementally stays with a core-0 owner. The replica swap and live update are
discarded (no second image).
One rule inverts: in MINIX a core-service death is fatal, here every core-1
service keeps a core-0 inline path, so a failed or absent core 1 degrades
the system to tier A rather than stopping it.

## The three tiers

- **Tier A, core 0 alone.** The bare Pico, the default build and the fallback
  of every other tier. All single-core optimizations land here first.
- **Tier B, core 1 as a service processor.** Two single-writer rings in SRAM
  (32 entries of 8 bytes each way) plus a SIO FIFO doorbell; core 1 runs only
  from SRAM and ROM, parks itself around flash writes, answers a heartbeat,
  and is restarted through `PSM.FRCE_OFF` bit 16. Core 0 takes a service back
  only after `FRCE_OFF` reads back set and the DMA channels and SPI1 core 1
  drove are aborted and reset; a timeout alone never moves ownership. Request
  slots carry a sequence number and an epoch, which reject stale replies and
  leave deduplication to the re-execution rule. Core 1's own MPU acts as the
  service's privilege list. Kernel `boot()`, the panic path and startup
  force core 1 off, because `AIRCR.SYSRESETREQ` resets only the requesting
  core and a running core 1 would otherwise survive a reboot.
- **Tier C, both cores on the PicoCalc.** Tier B plus a console service that
  owns SPI1 and the panel and executes absolute drawing commands. The
  terminal parser and the `lcdterm.c` cell store stay on core 0: the store is
  rewritten in several steps (`copy_row()`, `scroll_up()`, the full-screen
  scroll) and has core-0 writers at interrupt level and in the panic path, so
  a core-1 reset mid-scroll would leave a half-shifted screen that no redraw
  can repair, and a snapshot copy (3,392 bytes plus parser state) would not
  remove the second writer. After a core-1 restart core 0 repaints all 1,696
  cells from the intact store (about 207 ms).

## Order of work

The order puts cheap, reversible single-core wins before any core-1 code,
and lets each step's measurement decide whether the next one is worth it.

1. **Board baseline (tier A).** The emulation predicts a packed 40 KB
   program starts about 250 ms later than its raw form, because the decoder
   costs about 387 cycles per expanded byte (54% in the bit reader, 20% in a
   bit-serial CRC) and exec expands each stream twice. That contradicts the
   "tens of ms" in `sys/arch/rp2040/doc/STORAGE.md`. The 387 figure is an
   emulated count that `docs/research/rp2040-second-core-emulation/build.sh`
   reproduces from tree sources; no board run backs it. The shipped `time`
   prints tenths of a second (`usr.bin/time/time.c`), too coarse for a 50 ms
   falsifier, so the step has two measurements: a loop of 20 execs of each
   variant under `time` (no kernel change, 5 ms resolution per exec), and a
   diagnostic kernel that brackets `hsx_expand()` with `TIMERAWL` (a kernel
   change, reverted before any commit, 1 microsecond resolution). Falsified by
   a per-exec difference under 50 ms for a 40 KB text. Needs the board and the
   owner's flash opt-in.
2. **Single-core decoder and arithmetic (tier A).** A table-driven CRC32
   (about 79 to 25 cycles per byte), a word-buffered bit reader for the HSX
   streams (inferred about 2x), the SIO hardware divider behind
   `__aeabi_idiv`/`__aeabi_uidiv` (56-458 cycles to about 22), and a timer
   catch-up after flash operations so masked erases stop losing ticks. Each
   lands behind its own host gate and mutant. The hardware divider takes
   32-bit operands: it serves `/` and `%` on 32-bit integers, and a Q16.16
   divide gets its 8 cycles only on a range-checked 32/32 fast path (raw
   numerator within 16 bits signed), not in general. Owner decision: whether the
   port may carry its own faster decoder for HSX streams rather than the
   upstream heatshrink code byte for byte.
3. **SPI1 DMA for the PicoCalc panel (tier A).** The render path polls about
   115 us per cell; DMA should take core-0 time per cell to a few
   microseconds. This is the falsifier for the tier C console service: if
   DMA on core 0 recovers more than 90% of the render time, the console
   service is not worth building.
4. **Core 1 skeleton and observer (tier B).** Launch, FRCE_OFF in boot and
   panic, the rings and doorbell, the heartbeat, and a first service that
   only observes: it samples core 0's state and captures a record when core 0
   stops answering, aimed at the unexplained USB console wedge
   (`rp2040-usb-console` notes). Built as `sys/arch/rp2040/rp2040/core1.c`
   over `corering.c`, selected by `options "CORE1"` and carried by the
   PICO and PICO_UART kernels; `sys/arch/rp2040/doc/CORE1.md` is its
   authority and records the measured cost (1,828 bytes of flash, 544 of
   them the SRAM image), the host, linked-image, emulated and Renode
   evidence, and one board run in which an injected 300 ms `splhigh()`
   spin was reported with its PC, a 6 s one reset the board with that
   record, and flash writes raised no report. The estimate made before the code
   existed was: about 300-600 bytes of code that runs from
   SRAM and is stored in kernel flash as its load image, plus 300-500 bytes
   of launch and reset text on core 0: 600 to 1,100 bytes of flash.
   Falsified if an injected `splhigh()` spin goes unreported within two
   heartbeat periods, or a legitimate 400 ms erase is reported as a hang.
5. **Console service (tier C)**, only if step 3 leaves the render share above
   the threshold, and **split-stream decode across both cores**, only if
   step 2 leaves exec above 100 ms for a 40 KB program; that one needs a new
   `HSX_VERSION` with independent segments and must beat the step 2 decoder
   by 1.5x with under 3% packed-size growth.

Interpolators do not speed bit extraction (Thumb-1 shifts and masks are
single-cycle); they are kept for blend (INTERP0), clamp (INTERP1) and affine
work in a future pixel console. Fixed-point Qm.n arithmetic is a calculator and game-physics
question, not a second-core one, and stays with those programs.

## Constraints every core-1 change must respect

- The divider-ownership gate (`tools/verify_rp2040_divider_ownership.py`)
  rejects kernel code that uses the SIO divider outside its owner; core-1
  code either avoids division or gets a calibrated exception.
- SRAM4 and SRAM5 hold the USB transmit ring on PICO and PICOCALC; core-1
  code and rings need their own reservation in `RP2040.ld`.
- USB DPSRAM is execute-never; it can hold data only.
- Kernel flash text: PICO has about 22-26 KB free (the two reports disagree
  by build), PICOCALC under 7 KB after the open driver PRs. Core-1 code
  copied to SRAM at boot is stored in flash as its load image
  (`.ramfunc` in `kern.ldscript`), so the observer costs its 300-600 bytes
  and the launch code its own, 600 to 1,100 bytes in all: 9 to 17 percent of
  a 6.5 KB PICOCALC headroom. A console service adds the panel draw path and font
  the same way, unmeasured.

## Checks not run

Every board measurement above (the emulated cycle counts reproduce on the
host and say nothing about silicon); whether `FRCE_OFF` leaves a DMA channel
or SPI1 mid-transfer, and the size of the panel draw path built for SRAM; the
W25Q16JV erase and program timings (no chip datasheet in the tree); write
ordering between SRAM banks and the clear-on-read behavior of the status
registers the observer would read; the DMA sniffer CRC configuration against
a known vector.
