# Connecting to the DiscoBSD RP2040 console

The board has no network interface, so there is no telnet or ssh: the
console is a USB CDC-ACM serial line. Connecting means running a serial
terminal emulator on the host, the same way one talks to any serial
console. The line is 115200 baud, 8 data bits, no parity, one stop bit, no
flow control, and the device presents a stable USB serial so the host can
find it by identity.

## Finding the board

The kernel reports the USB serial "rp2040", so udev exposes a name that
does not change when the device re-enumerates on reboot:

    /dev/serial/by-id/usb-DiscoBSD_DiscoBSD_RP2040_console_rp2040-if00

Use that path, not /dev/ttyACMn: the number increments on every
re-enumeration and a stale node from a prior connection returns EIO.
Installing distrib/rp2040/host/71-discobsd-pico.rules adds the shorter
symlink /dev/discobsd and turns off USB autosuspend for the port.

## Terminals, by platform

All of these speak the serial standard and ship on both Debian and
CachyOS/Arch. Any one works; tio is the most forgiving because it
reconnects on its own when the board reboots.

| terminal | Debian | CachyOS/Arch | connect |
|---|---|---|---|
| tio | apt install tio | pacman -S tio | tio /dev/discobsd |
| picocom | apt install picocom | pacman -S picocom | picocom -b 115200 /dev/discobsd |
| minicom | apt install minicom | pacman -S minicom | minicom -b 115200 -D /dev/discobsd |
| GNU screen | apt install screen | pacman -S screen | screen /dev/discobsd 115200 |
| cu | apt install cu | pacman -S uucp | cu -l /dev/discobsd -s 115200 |
| PuTTY | apt install putty | pacman -S putty | putty -serial /dev/discobsd -sercfg 115200,8,n,1,N |

distrib/rp2040/host/discobsd-connect wraps this: it finds the board by its
by-id path and launches the first of tio, picocom, minicom or cu that is
installed, at the right line settings.

Log in as root with an empty password.

## Terminal type

The console entry in /etc/ttys sets the login's TERM to xterm, and
/etc/termcap on the root carries xterm, vt100, vt102 and ansi entries, so
the screen editors re and kilo and the pager more drive the terminal
correctly. The board's own full-screen programs -- kilo, the menu shell,
the games -- emit plain ANSI escapes that every xterm-family and vt100
terminal renders. Leave the emulator at its default type; only the board's
TERM matters over a serial line, since the line carries no terminal-type
negotiation.

## Terminal size

A serial line carries no window size and no resize event, so the kernel
opens the console at the VT100 standard 80x24 (dev/usb.c, dev/uart.c) and
every program reads that through TIOCGWINSZ. Size the emulator window to 80
columns by 24 rows for an exact fit.

To use a larger window, run

    resize

once after connecting. It asks the terminal for its real size with the
xterm cursor-report protocol -- park the cursor past the corner, request
its position with ESC [ 6 n, read the reply -- and writes the reported rows
and columns back with TIOCSWINSZ, which the kernel then reports to programs
and announces with SIGWINCH. A terminal that does not answer leaves the
80x24 default in place. Resizing the emulator window later does not reach
the board on its own; run resize again.

## A Python terminal, and a web terminal for any device

Two host programs in distrib/rp2040/host make connecting friendly, and both
need only Python and pyserial (Debian "apt install python3-serial", Arch
"pacman -S python-pyserial"):

- discobsd-term attaches a terminal in the current shell. It finds the board
  by its by-id path, opens the line, and bridges the local terminal to it,
  reattaching on its own when the board reboots and re-enumerates. Ctrl-]
  quits. `discobsd-term --probe` connects, pokes the line, and reports
  whether the board is reachable without taking over the terminal.

- discobsd-web serves the console as a web terminal, so any device on the
  network -- a phone, a tablet, a laptop of any operating system -- opens it
  in a browser with no client to install. The board has no network of its
  own, so the host it plugs into is the gateway: discobsd-web bridges the
  serial line to xterm.js in the browser over a WebSocket. Run it and open
  the printed http://<host>:7681/ URL. It binds every interface by default;
  --bind 127.0.0.1 keeps it local, --port changes the port, and a trailing
  device path overrides the by-id default.

For a zero-code alternative, ttyd (https://github.com/tsl0922/ttyd, on
Debian and Arch) serves any command as a web terminal:
`ttyd -p 7681 discobsd-term`. discobsd-web is the self-contained option that
needs only Python.

## Getting to the bootloader

To reflash, put the board in BOOTSEL: from a running kernel,
`picotool reboot -u -f` uses the console's reset interface, or hold the
BOOTSEL button while plugging the cable in. The bootrom presents a
different USB serial, so the by-id console path above addresses only the
running system and never the bootloader.
