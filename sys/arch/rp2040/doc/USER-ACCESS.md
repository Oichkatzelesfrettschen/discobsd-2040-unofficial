# Connecting to the DiscoBSD RP2040 console

The board lacks a network interface, so telnet and ssh are absent: the
console is a USB CDC-ACM serial line. Connecting means running a serial
terminal emulator on the host, the same way one talks to any serial
console. The line is 115200 baud, 8 data bits, no parity, one stop bit, no
flow control, and the device presents a stable USB serial so the host can
find it by identity.

## Finding the board

The kernel reports a USB serial made of its flash chip's 64-bit unique ID
in hex and the suffix `-rp2040` (`flash_serial_string()` in
`sys/arch/rp2040/dev/flash.h`), so udev exposes a name that is the board's
own and does not change when the device re-enumerates on reboot:

    /dev/serial/by-id/usb-GizmoBSD_GizmoBSD_RP2040_console_<16 hex digits>-rp2040-if00

Two boards on one host get two names; the pattern
`/dev/serial/by-id/*GizmoBSD*rp2040-if00` matches each of them (a kernel
built before the rename has DiscoBSD in both places). A kernel
that cannot read the chip's ID reports the serial "rp2040" and the name
ends in `console_rp2040-if00`, as kernels before the unique serial did.
The udev rule and `discobsd-term` match the serial's `rp2040` ending, so a
host package older than the unique serial finds such a board by its
product string alone and its udev rule does not make `/dev/discobsd`.

Use that path, not /dev/ttyACMn: the number increments on every
re-enumeration and a stale node from a prior connection returns EIO.
Installing distrib/rp2040/host/71-discobsd-rp2040.rules (the Arch and Debian
packages do) adds the shorter symlink /dev/discobsd and turns off USB
autosuspend for the port.

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

Log in as `operator` with no password, then `su` to root: operator is in the
wheel group, so `su` skips the password prompt. Direct root login on the console is
refused.

## Terminal type

The console entry in /etc/ttys sets the login's TERM to xterm, and
/etc/termcap on the root carries xterm, vt100, ansi and linux entries, so
the screen editor stevie (vi) and the pager more drive the terminal
correctly. The board's own full-screen programs -- stevie, the menu shell,
the games -- emit plain ANSI escapes that every xterm-family and vt100
terminal renders. Leave the emulator at its default type; only the board's
TERM matters over a serial line, since a serial line lacks terminal-type negotiation.

## Terminal size

A serial line lacks window size and resize events, so the kernel
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

## The discobsd-host tools: terminal and web console on any host

distrib/rp2040/host is the `discobsd-host` package (Python 3.9 or later
and pyserial; see its README for the wheel, Arch, Ubuntu, Windows, and
macOS installs). It finds the board by USB identity -- vendor 2e8a,
product 000a, serial `rp2040` -- so the same commands work on Linux,
Windows, and macOS, and `DISCOBSD_PORT` overrides discovery.

- discobsd-term attaches a terminal in the current shell and reattaches on
  its own when the board reboots and re-enumerates. Ctrl-] quits.
  `discobsd-term --probe` connects, pokes the line, and reports whether the
  board answers; `--list` prints the attached boards. On Windows it runs
  in a console with virtual-terminal processing and translates the arrow
  keys to VT100 sequences.

- discobsd-web serves the console as a web terminal, so any device on the
  network -- a phone, a tablet, a laptop of any operating system -- opens it
  in a browser with no client to install. The board lacks a network of its own, so the host it plugs into is the gateway: discobsd-web bridges the
  serial line to xterm.js in the browser over a WebSocket. It binds
  loopback by default; a non-loopback --bind requires --token SECRET, and
  a WebSocket upgrade must carry the Origin of the page the server served.
  One session holds the console at a time.

- discobsd-link redirects a short URL on port 42069 to the tokenized
  console URL; anyone who can reach it learns the token, so open the port
  to the LAN only.

- discobsd-console up starts both servers, generates and stores the token
  in a per-user web.env, and prints the URLs; down stops them; status
  reports. On Linux with the packaged systemd user units it drives the
  units, elsewhere it runs the servers detached with pid files.

For a zero-code alternative, ttyd (<https://github.com/tsl0922/ttyd>, on
Debian and Arch) serves any command as a web terminal:
`ttyd -p 7681 discobsd-term`.

## Getting to the bootloader

To reflash, put the board in BOOTSEL: from the board's own shell, as root,
`bootloader -y` (or `reboot -B -y`) syncs the disks and enters the boot ROM's
USB loader (`boot()` in `rp2040/machdep.c`, `RB_BOOTLOADER`); the board
reappears as the RPI-RP2 drive. Without `-y` the program refuses with exit
64, and a non-root caller gets exit 77, in both cases before it logs, syncs
or signals anything. From a host, `picotool reboot -u -f` uses the console's
reset interface (`dev/usb.c`), or hold the BOOTSEL button while plugging the
cable in. The same shell offers `reboot` and `halt` (no confirmation: both
leave the flash as it is), `shutdown`, and, on a PicoCalc kernel,
`sysctl -w machdep.picocalc.poweroff=1`.

`boardinfo` prints the kernel, release, host name, CPU clock, image length
and CRCs, the previous reset's watchdog reason, whether the PicoCalc
registers answer, and the mounts. The flash chip's unique ID is
`machdep.flash_unique_hi` and `machdep.flash_unique_lo` (the first and last
four bytes, read only, signed ints as `sysctl` prints them, EIO when the
chip's identity did not read), which it joins with `printf %08x%08x` into
16 hex digits, the value the USB serial string carries before its
`-rp2040` suffix; `boardinfo` prints it as `unavailable` on a kernel without
the node. The PicoCalc loader's menu starts only from the keyboard: that loader's stage is entered at power-on (PICOCALC.md, Coexisting with the PicoCalc UF2
loader), and the sources read here lack a reboot request it honors.

The bootrom presents a different USB serial, so the by-id console path
above addresses only the running system and never the bootloader.
