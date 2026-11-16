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

## A silent console: read the kernel's RAM before anything resets it

A forced reboot into the boot ROM keeps the kernel's SRAM, so the state
behind a console that has gone silent can be saved and read after the fact
(issue 569 asks for exactly that capture at the next occurrence). The
sequence, on a host with picotool and the board still attached:

1. Leave the board alone. A power cycle, a reflash or a plain `picotool
   reboot` discards the RAM that explains the silence.
2. Find the board's USB port (`ls /sys/bus/usb/devices`, the `2e8a` entry;
   with two boards attached the capture needs the port named whatever the
   count) and run `discobsd-sramdump capture --usb-port PORT --force-reset
   --out silent-1`. This ends the session, then writes `silent-1/sram.bin`
   and `silent-1/flash.bin` and leaves the board in the boot ROM.
3. Decode with the `unix.elf` of the build that was flashed:
   `discobsd-sramdump decode UNIX.ELF silent-1/sram.bin --flash
   silent-1/flash.bin > silent-1/decode.txt`. A mismatch between the flash
   and the ELF stops it, which is the signal to find the right ELF.
4. Attach `sram.bin`, `flash.bin` and `decode.txt` to the issue, with the
   ELF's SHA-256.
5. Restart the board: `picotool reboot --bus B --address A` with the numbers
   the capture printed, or replug.

What to read in `decode.txt`: the `FLAG:` line (tty `TS_BUSY` with
`usbd.tx_busy` 0 is the hypothesis state), `usbd.configured` and `dtr`, the
`machdep.usb_*` counters, and each process's wait channel (a login shell
asleep in `usbttys+0x...` is waiting for input; one asleep elsewhere names
what it waits for). `distrib/rp2040/host/DEVELOPMENT.md` describes the tool.

## Open: a device-side bulk-OUT wedge

After heavy repeated open/write/close cycling the device can reach a state
where the host's bulk-OUT write returns EIO while the control endpoint
still enumerates and reads. A USB port reset does not clear it; a reflash
does. The toggle and tx_busy paths are ruled out from the datasheet and
source (doc/research/usb-reopen.md), so the OUT endpoint's armed state is
the suspect. Pinning it down needs a live register read over SWD
(usbd.data_out_pid and the bulk-OUT buffer control at 0x50100084) or a
usbmon capture at the moment the write first errors.
