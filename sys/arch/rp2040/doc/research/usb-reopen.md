# USB CDC-ACM console wedge on host reopen: investigation

## Symptom

A fresh boot serves one host session over `/dev/ttyACM0` cleanly. After the
host closes the port and opens it again, the second session produces no
command output and often no echo; the console recovers only on a reflash from
BOOTSEL. A single session per boot never wedges.

## Finding

Under the host behavior confirmed below, the device keeps `usbd.configured`,
`usbd.data_in_pid`, and `usbd.data_out_pid`, so neither a bulk data-toggle
offset nor a stuck `tx_busy` latch is reachable through the enumeration or
control path across a reopen. The code as written cannot produce the reported
wedge through that path. A premise is therefore unverified: either the host
emits a request or bus event on reopen that this analysis has not observed, or
the wedge is a device-side state corruption outside the control path. The
board itself must supply the missing state; the decisive probes are below.

## What the host sends on a reopen

Source: `drivers/usb/class/cdc-acm.c` (torvalds/linux master).

- `acm_port_activate` submits the control URB, calls `acm_tty_set_termios`
  (which sends `SET_LINE_CODING` and `SET_CONTROL_LINE_STATE` when line control
  changes), and re-submits the bulk-IN read URBs. It calls no `usb_clear_halt`,
  no `usb_reset_endpoint`, and no `usb_set_interface`.
- `acm_port_shutdown` kills the read URBs.
- The file calls `usb_set_interface` nowhere. It calls `usb_clear_halt` only in
  `acm_softint` (RX stall recovery) and in `acm_probe` under the
  CLEAR_HALT_CONDITIONS quirk, which the Pico VID:PID 0x2e8a:0x000a does not
  match. It calls `usb_reset_device` and `usb_reset_configuration` nowhere.

So a plain close/reopen of this non-quirky CDC device carries only
`SET_LINE_CODING` and `SET_CONTROL_LINE_STATE`; the host resets no toggle and
sends no `SET_CONFIGURATION`, `SET_INTERFACE`, or `ClearFeature`. The host
controller keeps its bulk-IN and bulk-OUT data toggles across the close, and
the read URBs resume polling on reopen. pyserial reaches the same endpoint
through the same kernel `cdc_acm`, so its close/reopen is identical.

## Mechanisms ruled out

- **SET_CONFIGURATION on reopen.** Not sent (enumeration is once per plug). So
  `usb_configure()` does not run on a reopen; `configured` stays 1 and the
  toggles are not reset there.
- **Toggle offset from a host-side reset.** The host resets neither toggle on a
  reopen (no `ClearFeature`, `SET_INTERFACE`, or `SET_CONFIGURATION`), and the
  device resets neither (those are the only reset sites in `usb.c`). Both sides
  keep their toggles, so an offset cannot be created. A device-side reset on
  DTR would instead create one, so resetting `data_in_pid`/`data_out_pid` on
  DTR is refused.
- **Stuck `tx_busy` (software latch)`.** `tx_busy` clears on any bulk-IN
  buffer-done in `usb_service()`. Datasheet 4.1.2.8.2: the controller sets the
  `BUFF_STATUS` bit at the same STATUS-phase point it clears `AVAILABLE`; that
  bit raises `INTS_BUFF_STATUS`, enabled in `usbinit` and never disabled, and
  the polled console routines read `USB_BUFF_STATUS` directly. So a completion
  is always observed and `tx_busy` always clears; the state "`tx_busy` set,
  `AVAILABLE` clear, no `BUFF_STATUS` pending" is unreachable while interrupts
  run. An IN buffer left armed (`AVAILABLE` set) at close is delivered when the
  host resumes polling on reopen, and a wrong-toggle IN packet is ACKed anyway
  (USB 2.0 8.6), so neither leaves `tx_busy` stuck.

## Remaining candidates

- **A host reset or resume not in the plain-open path.** Autosuspend of the
  idle closed port, then a resume-with-port-reset (USB persist) or a
  ModemManager/udev probe on reopen, would drive a bus reset the device sees as
  `USB_INTS_BUS_RESET`. `usb_service` then calls `usb_configure(0)`:
  `configured` goes to 0 and the bulk-OUT buffer is left un-armed. If the host
  does not follow with the re-address and `SET_CONFIGURATION` a reset requires,
  the device stays `configured==0` with no armed OUT buffer -- no output
  (`usb_tx_kick` returns on `!configured`) and no input (no OUT buffer to
  receive keystrokes), recovering only on re-enumeration. This matches the
  symptom, but whether the reopen actually carries a bus reset is unverified.
- **Device-side reentrancy.** The polled console routines run `usb_service()`
  at `spltty`; the clock interrupt outranks `IPL_TTY` and can drive
  `ttstart -> usbstart -> usb_tx_kick`/`usb_tx_put`, which touch `usbd`. A
  clock preemption of a polled `usb_service()` mid-completion could corrupt
  `tx_busy` or the ring pointers against the hardware. This is timing-dependent
  and not reopen-specific, so it is a weaker match.

## Decisive probes (parent runs these; no fabricated fix is shipped)

1. **Read the device state at the wedge over SWD.** With a debug probe attached
   (`arm-none-eabi-gdb compile/PICO/unix`, `target extended-remote`), after the
   wedge inspect the driver state directly, no code change:
   - `print usbd` -- `configured`, `dtr`, `tx_busy`, `data_in_pid`,
     `data_out_pid`, `tx_head`, `tx_tail`.
   - `x/1xw 0x50100088` (bulk-IN buffer control, EP2 IN) and `x/1xw 0x50100084`
     (bulk-OUT buffer control) -- AVAILABLE bit 10, FULL bit 15, DATA1 bit 13.
   - `x/1xw 0x50110050` (`USB_SIE_STATUS`) -- bit for `DATA_SEQ_ERROR`.
   `configured==0` confirms the bus-reset-without-reconfigure candidate;
   `tx_busy==1` with `AVAILABLE==0` confirms a lost completion; a bulk-OUT
   `DATA_SEQ_ERROR` with `data_out_pid` frozen confirms an OUT-toggle deadlock.
2. **Capture the reopen on the host.** `modprobe usbmon`, then record the Pico's
   bus (`cat /sys/kernel/debug/usb/usbmon/<bus>u`, or Wireshark usbmon) across
   the close and reopen. Every control transfer is verbatim: this shows whether
   a bus reset, `SET_CONFIGURATION`, `SET_INTERFACE`, or `ClearFeature` reaches
   the device on reopen, ending the speculation in one capture. It touches the
   host only, not the board.

## Contingent fixes

- usbmon shows a bus reset on reopen: `usb_configure(0)` must leave the device
  able to recover, or the driver must handle `SUSPEND`/`RESUME` so an
  autosuspend does not force a deconfigure. Re-arm the bulk-OUT buffer on the
  event that actually precedes the reopen.
- usbmon shows `SET_INTERFACE` to the data interface: the `REQ_SET_INTERFACE`
  case (see latent bug below) must reset both bulk toggles, re-arm bulk OUT to
  DATA0, and reconcile `tx_busy`, matching the `REQ_CLEAR_FEATURE` handler.
- gdb shows `configured==1` and both toggles synced with the host, yet wedged:
  the fault is the reentrancy candidate; serialize `usbd` access between the
  polled routines and `usbstart`.

## Latent bug found en route

`REQ_SET_INTERFACE` (line 474) acks with `usb_ep0_ack()` and resets no toggle.
USB 2.0 9.4.10: SetInterface resets the data toggles of every endpoint in that
interface to DATA0, and the host resets its own. This driver's ack-without-reset
is an asymmetric reset whenever a host issues SetInterface to the data
interface, offsetting both bulk toggles. `cdc_acm` never issues it, so it is not
the reopen trigger, but any host that does would desync the console. The
`REQ_CLEAR_FEATURE` handler directly above gets the equivalent reset right for
one endpoint and is the template for a fix.

## Build

`bmake MACHINE=rp2040` in `sys/arch/rp2040/compile/PICO` builds the unchanged
kernel to `compile/PICO/unix`; the only warnings are pre-existing and unrelated
(`kern/exec_subr.c` unused variable; the RWX LOAD-segment linker note). No
driver change is committed, since no shipped fix is justified until a probe
identifies the wrong state.
