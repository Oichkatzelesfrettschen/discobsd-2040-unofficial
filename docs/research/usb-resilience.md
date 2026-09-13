# USB console: analysis tools and resilient connection handling

The console is a USB CDC-ACM device. It presents a stable serial,
"rp2040", from the running kernel and a different serial from the BOOTSEL
bootrom, so a host can always tell the two apart and address the live
console by identity rather than by an enumeration-order number.

## Host tools, in the order they answer a question

- `dmesg` (or `journalctl -k`): the host kernel logs every enumerate,
  disconnect and cdc_acm bind, with the device number and serial. This
  shows whether the board dropped off the bus and came back, and when.
- `/dev/serial/by-id/usb-DiscoBSD_DiscoBSD_RP2040_console_rp2040-if00`:
  the stable path. `/dev/ttyACMn` increments on every re-enumeration and
  a stale node from a prior connection can linger, so opening the highest
  ttyACMn reaches a dead device and returns EIO. Open the by-id path, or
  the /dev/discobsd symlink the udev rule installs, and the open always
  lands on the live console.
- `usbmon` (`modprobe usbmon`, then read `/sys/kernel/debug/usb/usbmon/<bus>u`,
  or capture in wireshark): the packet-level record of control and bulk
  transfers. It shows verbatim what the host sends on open -- a plain
  reopen carries only SET_LINE_CODING and SET_CONTROL_LINE_STATE, no bus
  reset or SET_CONFIGURATION -- and whether a bulk-OUT write completes or
  errors.
- `usbreset <vid>:<pid>` (usbutils): a port reset without a replug. It
  recovers a host-side confusion, but does not clear a device-side bulk
  path that has stopped accepting OUT data; only a full chip reset (a
  reflash, or BOOTSEL) does, which localizes such a wedge to the device.
- `lsusb -v`, `usbtop`, `udevadm monitor --udev`: the descriptor dump,
  live throughput, and the event stream as devices come and go.
- `tio`, `picocom`, `minicom`: terminal front ends that assert DTR and set
  the line coding the way cdc_acm does; `tio /dev/discobsd` reconnects on
  its own when the device reappears, which is the resilient way to hold a
  session across a reboot.

## Resilience the operating system carries

- The USB serial is fixed at "rp2040" so the host can bind by identity.
- On a USB bus reset the driver returns the address to zero, resets both
  data toggles and re-arms the bulk OUT buffer (usb_configure), and
  SET_INTERFACE now resets the toggles per USB 9.4.10.
- The console transmit path bounds every wait: usbputc services the
  controller and spins a bounded number of times when the ring is full and
  the host is present, then drops the byte rather than stalling the kernel,
  and usbdrain bounds the pre-reset flush. A host that stops reading slows
  the console but does not wedge the kernel.

## Open: a device-side bulk-OUT wedge

After heavy repeated open/write/close cycling the device can reach a state
where the host's bulk-OUT write returns EIO while the control endpoint
still enumerates and reads. A USB port reset does not clear it; a reflash
does. The toggle and tx_busy paths are ruled out from the datasheet and
source (doc/research/usb-reopen.md), so the OUT endpoint's armed state is
the suspect. Pinning it down needs a live register read over SWD
(usbd.data_out_pid and the bulk-OUT buffer control at 0x50100084) or a
usbmon capture at the moment the write first errors.
