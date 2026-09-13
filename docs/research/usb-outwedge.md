# USB CDC-ACM console bulk-OUT wedge: mechanism and self-recovery

## Symptom

After heavy repeated open/write/close cycling from the host, the device reaches
a state where the host's bulk-OUT write returns EIO (errno 5) while EP0 still
enumerates and the device stays readable: firmware runs, EP0 works, bulk IN
delivers, only bulk OUT stops accepting data. A USB port reset (usbreset:
`USB_INTS_BUS_RESET` -> `usb_configure(0)` -> host re-enumeration) does not
clear it. Only a full chip reset -- reflash or BOOTSEL -- does.

## The un-armed bulk-OUT buffer is the state that produces EIO

Datasheet 4.1.2.8.3, OUT token phase: the controller enters the data phase only
when the buffer's AVAILABLE bit is set and FULL is clear. A bulk-OUT buffer with
AVAILABLE clear is owned by the processor, so the controller has no buffer to
receive into and NAKs every OUT token. The host retries a NAKed bulk write until
it times out, which `cdc_acm` surfaces as EIO. EP0 and bulk IN have their own
buffers and keep running. So "bulk-OUT EIO, EP0 and IN alive" is exactly the
signature of the bulk-OUT buffer left with AVAILABLE clear.

## Which un-arm mechanism the source supports: none, and why each candidate fails

The bulk-OUT buffer is armed at `usb_configure(1)` (arms EP_DATA OUT buffer 0
DATA0, sets `data_out_pid=1`) and re-armed in `usb_rx_done` after every received
packet (flipping `data_out_pid`); `REQ_CLEAR_FEATURE` and `REQ_SET_INTERFACE`
also re-arm it. Each of the three enumerated un-arm mechanisms is eliminated from
the source:

- **A BUFF_STATUS bit cleared without processing.** `usb_service` reads
  `done = USBREG(USB_BUFF_STATUS)` once, then `USBCLR(USB_BUFF_STATUS) = done`.
  `USBCLR` is the RP2040 atomic-clear register alias (offset 0x3000), so it
  clears exactly the bits present in `done`; a completion the controller raises
  after the read is not in `done` and survives. Every bit in `done` is handled
  in the same block, so the EP_DATA OUT bit is never cleared without calling
  `usb_rx_done`. `USB_INTS.BUFF_STATUS` is level-sourced -- datasheet register
  listing: "Raised when any bit in BUFF_STATUS is set. Clear by clearing all
  bits in BUFF_STATUS" -- so a bit set late re-raises the interrupt. No OUT
  completion is lost this way.

- **A buffer-status race between the polled `usb_service` at spltty and the
  interrupt.** ARMv6-M (Cortex-M0+) carries only PRIMASK, which masks every
  interrupt or none. `machine/intr.h` maps `spltty()`, `splclock()`, and
  `splhigh()` all onto `arm_intr_disable()` (a `cpsid i`), and its comment
  states priorities "only order preemption among interrupts that are already
  enabled." A polled `usb_service()` runs at spltty with the USB interrupt and
  the clock both masked, and core 1 is never launched (the SIO PROC handlers in
  locore0.S are default stubs, machdep.c never starts a second core). The polled
  path and the interrupt cannot interleave, so no race un-arms the buffer.

- **`usb_configure(1)` not re-arming after the reset usbreset triggers.** A bus
  reset drives `usb_service` -> `usb_configure(0)`, which sets `configured=0`
  and leaves the OUT buffer un-armed. If the host then omits `SET_CONFIGURATION`,
  `usb_configure(1)` never runs and the OUT buffer stays un-armed -- but
  `configured` stays 0, `usb_tx_kick` returns early on `!configured`, and bulk
  IN goes dead too. That contradicts the observed "device is readable," so the
  bus-reset-without-reconfigure path does not produce the reported wedge. It may
  explain why usbreset does not recover the console, which is a separate fact.

No source path leaves `configured==1` with bulk IN alive and the bulk-OUT buffer
un-armed. The device-controller errata do not supply one either: RP2040-E2
(NAK-forever) requires setting an `EP_ABORT` bit, which the driver never touches
(no `EP_ABORT` reference in usb.c or usb.h), and it NAKs all endpoints, not OUT
alone; RP2040-E5 (fails to exit RESET on a busy bus) manifests as no enumeration,
while the wedge still enumerates EP0. Both are excluded by the symptom.

## The shipped fix: re-arm an idle un-armed bulk-OUT buffer, proven safe

`usb_rx_rearm_if_idle()`, called at the tail of `usb_service`, re-arms the bulk
OUT buffer when it is stuck un-armed, so the console heals on the next host
control request (a reopen sends `SET_LINE_CODING` and `SET_CONTROL_LINE_STATE`)
or console I/O rather than needing a chip reset. It re-arms only when all three
hold:

    (bc & (AVAILABLE | FULL)) == 0  &&  BUFF_STATUS EP_DATA-OUT bit == 0

gated on `configured`, and re-arms with the current `data_out_pid` without
flipping it, since no packet was received.

**Safety against the mid-DMA race, from the datasheet alone.** 4.1.2.7.4 makes
AVAILABLE the ownership bit: the processor sets it to hand the controller the
buffer, and "the controller sets to 0 when it has used the buffer ... has filled
the buffer with data from the host for an OUT transaction." 4.1.2.8.3 puts the
AVAILABLE-clear, FULL-set, and BUFF_STATUS-set events together in the OUT status
phase. Therefore `AVAILABLE==0 && FULL==0 && BUFF_STATUS bit==0` means the
controller is not mid-transaction on the buffer (it would hold AVAILABLE set),
has not just filled it (that sets FULL and the BUFF_STATUS bit), and no received
packet is waiting to be drained. The processor owns an idle empty buffer;
re-arming it cannot race a DMA in flight and drops no received packet.

At the `usb_service` tail call site the BUFF_STATUS term is already clear,
because `usb_service` cleared the read bits with its own `USBCLR(USB_BUFF_STATUS)
= done` before `usb_rx_done` ran, so FULL is the term that excludes the
undrained-packet window there: FULL is set from the OUT completion until
`usb_rx_done`'s re-arm clears it, and `usb_rx_done` runs earlier in the same
`usb_service` call, so the window never spans a guard evaluation. The BUFF_STATUS
term keeps the function safe against a future caller that has not yet cleared the
bit -- a reset path or `usbputc` -- where a completed-but-unread packet would
still show its BUFF_STATUS bit set.

**No double-arm.** Every other OUT arm site -- `usb_configure`, `usb_rx_done`,
`REQ_CLEAR_FEATURE`, `REQ_SET_INTERFACE` -- leaves AVAILABLE set, so the guard
excludes each and fires only on a buffer that has lost its armed state. In
normal operation AVAILABLE is clear only in the brief window between an OUT
completion and `usb_rx_done`'s re-arm within the same `usb_service` call, and
FULL is set across that window, so the guard never fires on the working path.

**The port-reset case.** The `usb_service` check fires on the `SET_LINE_CODING`
SETUP interrupt, which the host sends only after configuring, so a reopen after
a port reset re-arms the OUT buffer as soon as the host touches the port,
provided `configured==1`. Arming the OUT buffer in the bus-reset path itself is
refused: USB 2.0 puts the bulk endpoints in the Configured state only, so a
device at address 0 must not accept bulk OUT before `SET_CONFIGURATION`. If a
SWD read shows the wedge is `configured==0` (usbreset left the device
deconfigured), the correct fix targets the reconfigure or a SUSPEND/RESUME
handler, not an illegal OUT arm; see the blind spot below.

Existing invariants are untouched: `usb_buf_arm`'s write-then-nops-then-AVAILABLE
sequence (4.1.2.7.1), the DATA0/DATA1 toggle values, and the `tx_busy` meaning
all keep their prior semantics; the recovery reads state and re-arms only the OUT
buffer.

## Host simulation of the handshake

`usb-outwedge-sim.py` models the buffer-control word (AVAILABLE bit 10, FULL bit
15), the BUFF_STATUS bit, and the controller and driver halves, then drives the
steady state, the wedge, the recovery, and the two refusal cases. Run it with
`PYTHON=${PYTHON:-python3}; $PYTHON usb-outwedge-sim.py`:

    after configure:                AVAIL=1 FULL=0 BUFF_STATUS=0 pid=1
    after 5 OUT packets:            AVAIL=1 FULL=0 BUFF_STATUS=0 pid=0 guard fired 0
    wedged (un-armed, idle):        AVAIL=0 FULL=0 BUFF_STATUS=0 pid=0
    after rearm_if_idle:            AVAIL=1 FULL=0 BUFF_STATUS=0 pid=0
    after recovered OUT + rx_done:  AVAIL=1 FULL=0 BUFF_STATUS=0 pid=1
    full buffer, guard refuses:     AVAIL=0 FULL=1 BUFF_STATUS=1 pid=1 (data preserved)
    armed buffer, guard refuses:    AVAIL=1 FULL=0 BUFF_STATUS=0 pid=0

The steady-state guard fires zero times, the wedged buffer NAKs (host EIO), the
guard re-arms it once with no double-arm on a second call, and it refuses to arm
over a full buffer (received data preserved) or an in-flight buffer.

## Blind spot the board test must distinguish

The re-arm heals only the AVAILABLE-clear un-armed state. It does not fire, or
does not help, on three other states that give the same host-visible shape
(EP0 and IN fine, OUT dead, only a chip reset clears it). If the board does not
recover after this fix, that means a wrong wedge state, not a broken fix, and
the SWD read below says which. Read the bulk-OUT state over SWD
(`arm-none-eabi-gdb compile/PICO/unix`, `target extended-remote`):

- `print usbd.data_out_pid` and `x/1xw 0x50100094` (EP2 OUT buffer control,
  `USB_DPRAM_BUF_CTRL(2, 0)` = 0x80 + 2*8 + 4): bit 10 AVAILABLE, bit 15 FULL,
  bit 11 STALL, bit 13 DATA1.
  - AVAILABLE clear, FULL clear, STALL clear: the state this fix heals. Confirm
    the console now recovers on its own.
  - FULL set (bit 15): a received packet was never drained -- the fault is in
    `usb_rx_done` reach, not the arm. Fix: ensure the EP_DATA OUT BUFF_STATUS
    bit is always serviced.
  - STALL set (bit 11): the endpoint is halted. EP0 and IN keep working, and
    `usb_ep0_stall` only ever writes STALL to EP0, so a bulk-OUT STALL comes
    from the controller; a halt usually surfaces to the host as EPIPE (-32)
    rather than EIO (-5), which leans against this. Fix: clear STALL and re-arm
    on the next control request.
- `x/1xw 0x50100014` (EP2 OUT endpoint control, `USB_DPRAM_EP_CTRL(2, 0)`):
  bit 31 ENABLE. ENABLE clear means the endpoint was disabled -- it is written
  once in `usbinit` and never cleared in software, so ENABLE clear points at a
  controller reset of the endpoint control word. Fix: re-write the EP_DATA OUT
  endpoint control word before re-arming.
- `x/1xw 0x50110050` (`USB_SIE_STATUS`): bit 31 `DATA_SEQ_ERROR`. A bulk-OUT
  `DATA_SEQ_ERROR` with `data_out_pid` frozen would be an OUT-toggle deadlock;
  the toggle path is otherwise ruled out (usb-reopen.md).

## Board reproduce-and-recover test (parent runs; USB is unmodeled in Renode)

1. Build and flash: run `bmake MACHINE=rp2040` in
   `sys/arch/rp2040/compile/PICO`, then flash the resulting `unix.uf2`. The
   committed `compile/` binaries are the pre-fix build; rebuild before flashing.
2. Reproduce the wedge: from the host, cycle open/write/close on the console
   many times (open `/dev/discobsd` or the by-id path, write a byte, close;
   loop until a write returns EIO). Confirm with `dmesg`/usbmon that the write
   errors while EP0 still enumerates and reads still work.
3. Confirm self-recovery: without touching the board, open the console again and
   write. With the fix the console recovers on its own -- a subsequent open
   writes and echoes -- where before it needed a reflash. If it does not
   recover, take the SWD read above; a non-AVAILABLE-clear state means the
   wedge is one this fix does not target.

## Build

`bmake MACHINE=rp2040` in `sys/arch/rp2040/compile/PICO` and in `compile/
PICO_UART` both build with zero new warnings. usb.c compiles warning-free under
-Wall; the only warnings are pre-existing and unrelated (the `kern/exec_subr.c`
unused variable in PICO_UART, and the RWX LOAD-segment linker note in both).

A fresh worktree needs the host tools first (`bmake MACHINE=rp2040 symlinks
tools`). Where mandoc is absent that target fails at the config manpage step
after the `config` binary links; install the binary past it with
`tools/bin/binstall -U tools/config/config tools/bin/config`, then the kernel
build finds `config`.
