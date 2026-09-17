# Quirk: the console goes silent after a web console session

Seen once, on 2026-09-16, on macOS 26 with host tools 1.0.8 and the
kernel built on that Mac (main after the macOS host pull request).
Recorded as a quirk with its recovery; the cause is not established.

## Symptom

After a browser session on the web console had ended and
`discobsd-console down` had stopped both servers, the console was
silent in both directions:

- `discobsd-term --probe`: "attached to /dev/cu.usbmodemrp20401 but the
  board sent nothing".
- Twelve seconds of listening with a carriage return every few seconds:
  zero bytes.
- The device still enumerated: product string "DiscoBSD RP2040
  console", serial `rp2040`, the `cu.` node present and world-writable
  with a fresh timestamp, so the board had re-enumerated at some point
  after the session.
- No process on the host held the node (`lsof` empty).

## What it was not

The bulk-OUT wedge in usb-outwedge.md: there, the host's write returns
EIO and bulk IN keeps delivering, and only a full chip reset clears it.
Here writes succeeded, nothing came back on IN, and a reboot through the
kernel's reset interface cleared it. So the kernel's USB stack and reset
handling were alive; what had stopped was above them: the console tty or
getty after the session ended.

## Recovery

    picotool reboot -f            # the kernel answers on its reset interface
    discobsd-term --probe         # boot messages, then login:

then log in, `sync`, `exit`. Everything written before the silence was
on a filesystem that fsck found clean at the next boot, so nothing was
lost, but a `sync` had not been issued after the web session, which is
the case the README's "leave cleanly" steps exist for.

## Open questions

- Whether "Sync & leave" was used or the tab closed; the latter frees
  the console after the server notices the dead socket, and the
  server's close of the serial line (DTR drop) is where a getty
  interaction would start.
- Whether the re-enumeration timestamp on the node came from the
  kernel's own reset on session end or from something the host did.
- A reproduction: open the web console, log in, close the tab without
  Sync & leave, wait thirty seconds, `discobsd-console down`, probe.

Related: usb-reopen.md (what the host sends on a reopen) and
usb-resilience.md.
