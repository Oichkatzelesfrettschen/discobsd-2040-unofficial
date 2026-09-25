# discobsd-host

Host tools for the DiscoBSD RP2040 console on Linux, Windows, and macOS.

The Raspberry Pi Pico running DiscoBSD is a USB CDC-ACM serial device with
no network stack. Every current operating system carries the CDC-ACM
driver, so nothing kernel-side is installed: these tools find the board by
its USB identity (vendor 2e8a, product 000a, serial `rp2040`), open the
console at 115200 8N1, and either attach a terminal to it or serve it to a
browser on the local network.

| command | what it does |
| --- | --- |
| `discobsd-term` | attach the current terminal; reattaches across board reboots; Ctrl-] is the escape (`q` quits); `--probe` checks the board answers; `--list` prints attached boards |
| `discobsd-web` | serve the console as a browser terminal on port 7681; a non-loopback bind requires a token, from `DISCOBSD_WEB_TOKEN`, a systemd credential named `web-token`, or the deprecated `--token` |
| `discobsd-link` | redirect a short URL (port 42069) to the tokenized console URL, built from `--host` and the same token, or the deprecated `--to` |
| `discobsd-console` | `up`, `down`, `status`: run web and link detached, or through the systemd user units when installed |
| `discobsd-connect` | POSIX shell wrapper that launches tio, picocom, minicom, or cu on the board |
| `discobsd-flash` | reflash with picotool: `FILE.uf2 ...`, `--bootsel`, `--eject`; unmounts the RPI-RP2 volume before every reboot out of BOOTSEL and waits for the boot ROM itself |

Log in as `operator` with no password, then `su` to root. The console
serves one session at a time; leave it cleanly as described under "Keys
and exits" so the next session can attach.

## Install

Every release on the GitHub Releases page carries a wheel, an sdist, an
Arch package, an Ubuntu 24.04 `.deb`, a Windows zip of standalone `.exe`
files, and a macOS zip. CI builds and smoke-installs each on its own
operating system.

| platform | install |
| --- | --- |
| Arch, CachyOS | `sudo pacman -U discobsd-host-*.pkg.tar.zst` (or `makepkg -si` in `packaging/arch`) |
| Ubuntu 24.04, Debian | `sudo apt install ./discobsd-host_*.deb` |
| any OS with Python 3.9+ | `pipx install discobsd_host-*.whl` (or `pip install discobsd-host`) |
| Windows | unzip `discobsd-host-windows.zip`; run the `.exe` files from a terminal (no Python needed); the board is a `USB Serial Device (COMn)` under the inbox usbser driver, and the `Reset` interface it also exposes stays without a driver, which only picotool needs |
| macOS | unzip `discobsd-host-macos.zip`, or `pipx install` the wheel |

The Arch and Debian packages also install the udev rule that names the
board `/dev/discobsd` and the systemd user units for the web console. The
rule tags the node `uaccess`, so the user logged in at the machine's own
seat gets access the moment the board is plugged in, with no group and no
re-login. A user who is not at the seat (an ssh session, a service) joins
the `discobsd` group the packages create, then logs in again:

    sudo usermod -aG discobsd "$USER"

From a checkout of the port:

    cd distrib/rp2040/host
    python3 -m pip install --user .          # or: pipx install .

## macOS

Nothing kernel-side is installed and nothing needs signing: macOS binds
its own CDC-ACM driver to the board and creates `/dev/cu.usbmodemrp20401`
(and a `tty.` twin, which the tools skip because it blocks on carrier),
world-writable, the moment it is plugged in. `discobsd-term --list`
prints that node; `--probe` prints the login banner.

Apple's command line tools ship Python 3.9, the oldest release the
package accepts, without pyserial. Use Homebrew's pipx, which brings a
current Python and keeps the tools out of the system interpreter:

    brew install pipx picotool     # picotool only to reflash
    pipx ensurepath                # once; ~/.local/bin on PATH, new shell
    pipx install discobsd-host     # or, from a checkout: pipx install distrib/rp2040/host
    discobsd-term

The same four commands install with `uv tool install discobsd-host`;
both put their links in `~/.local/bin`, so choose one.

picotool talks to the board through libusb and needs no driver either.
Reflash with `discobsd-flash flash.uf2` (or the kernel's `unix.uf2`,
or both in order): it reboots the kernel into BOOTSEL, waits for the
boot ROM, unmounts the `RPI-RP2` volume so macOS does not complain that
a disk was not ejected properly, loads each image, and reboots. The
one-shot `picotool info -f` form is not reliable on macOS, whose
enumeration outlasts picotool's wait; `picotool reboot -u -f` and then
the command always is, and the script does that. For a copy by hand,
`discobsd-flash --bootsel` leaves the volume mounted and
`discobsd-flash --eject` unmounts it and reboots.

The release zip holds PyInstaller executables built on Apple silicon and
signed ad hoc, not notarized. Run them from a terminal: an Intel Mac and
a Finder double-click (which Gatekeeper refuses for an unnotarized
download) both want the pipx install instead. `discobsd-console up`
keeps its state under `~/Library/Application Support/discobsd`.
`discobsd-connect` finds `cu` on every Mac and `tio` or `picocom` from
Homebrew.

## Web console on the LAN

    discobsd-console up

prints a short URL such as `http://10.0.0.5:42069/` that redirects to the
tokenized console at `http://10.0.0.5:7681/?token=...`. The token and the
advertised address live in `web.env` under the per-user configuration
directory (`~/.config/discobsd` on Linux, `~/Library/Application Support/
discobsd` on macOS, `%APPDATA%\discobsd` on Windows), created with
owner-only permissions on the first run. `discobsd-console down` stops
both servers; `status` reports; `--detached` on any of the three bypasses
the systemd units.

On Linux with the packaged systemd user units, `discobsd-console up`
restarts the units instead of spawning detached processes. Enabling them
ties them to the board: they start when `/dev/discobsd` appears, at boot
or on hotplug, and stop when it goes away. A board already attached made
its appearance before the link existed, so start the units once by hand:

    systemctl --user enable discobsd-web discobsd-link
    systemctl --user start discobsd-web discobsd-link    # board attached now
    loginctl enable-linger "$USER"

An install that enabled the units under `default.target` moves them to the
device with `systemctl --user reenable discobsd-web discobsd-link`.

Firewall: open ports 7681 and 42069 to the LAN only, never to the
Internet, because the short link hands out the token. On a ufw host:

    sudo ufw allow from 10.0.0.0/24 to any port 7681 proto tcp
    sudo ufw allow from 10.0.0.0/24 to any port 42069 proto tcp

Set `DISCOBSD_PORT` to a device path to bypass discovery.

The console serves one viewer at a time. Leaving the page releases it at
once; a viewer that vanishes without closing (a tab the browser kept in
its cache, a device that left the LAN) is pinged after 15 seconds of
silence and dropped 15 seconds later, and the next viewer gets in.

## Keys and exits

Every key below is a byte on the DiscoBSD serial line; `discobsd-term` and
the web console deliver the same bytes.

| key | byte | action |
| --- | --- | --- |
| Ctrl-C | 003 | interrupt the running program |
| DEL, Backspace on most terminals | 177 | erase a character |
| Ctrl-U | 025 | erase the line |
| Ctrl-D | 004 | end input; log out at a prompt |
| Ctrl-\ | 034 | quit with a core dump |
| Ctrl-Z | 032 | suspend through job control |
| Ctrl-L | 014 | redraw the shell line |
| Ctrl-R | 022 | search shell history |
| Esc, Tab, arrows | | operate `vi`, `stevie`, and the shell line editor |

`discobsd-term` keeps Ctrl-] for itself as the escape, the way telnet
does:

| type | effect |
| --- | --- |
| Ctrl-] q, or Ctrl-] Ctrl-] | quit discobsd-term |
| Ctrl-] ] | send a literal Ctrl-] |
| Ctrl-] ? | print this list |

The web console is built for more than a mouse and good eyes. The status
line at the top right is a live region that names the connection state and
narrates Sync & leave, so a screen reader hears each change without
moving focus. Every key-bar button carries an accessible name that says
what the key does (for example, "Control C, interrupt in DiscoBSD"), the
Ctrl modifier reports its
armed state, and Tab moves through the bar with a visible focus ring.
The `reader` button turns on xterm.js's screen reader mode, which builds
an accessibility tree of the screen and announces output as it arrives;
the choice is remembered in the browser. Colors are rendered at a
minimum contrast of 4.5:1 against the black background (WCAG AA). `hide`
folds the bar down to a
single `keys` tab so a phone gets the screen back and keeps a way to the
keys and to leaving.

The web console's key bar sends the same bytes from buttons because browsers
keep Ctrl-C, Ctrl-D, Ctrl-W, and Ctrl-minus for themselves. Ctrl arms
a one-shot modifier for the next typed key. The `keys` button shows this
reference in the page. `Sync & leave` types `sync`, then `exit`, reports
each action in the terminal, and closes the socket. It assumes a shell
prompt, so finish `ed` or `vi` first.

Leave cleanly, in this order:

1. In DiscoBSD, type `exit` (or Ctrl-D) until the `login:` prompt is
   back, so the next person starts at login. `sync` first if you wrote
   files.
2. Leave the console: `discobsd-term` with Ctrl-] q; the web console
   with `Sync & leave`, then close the
   tab. Closing the tab alone also frees the console, a few seconds
   later, when the server notices the dead socket, but syncs nothing.
3. Unplug the board only after `sync` or `halt` in DiscoBSD.

## Development

    python3 -m pip install -e .[test]
    ruff check .
    pytest

The tests run without a board: they exercise the WebSocket framing, the
token and origin checks, USB identity matching, the redirector, the
Windows key translation, and the configuration file, and drive the web
server end to end against a fake serial line.

The tree under `packaging/` holds the Arch `PKGBUILD`, the `debian/`
directory, and the PyInstaller spec that the release workflow in
`.github/workflows/host.yml` uses.
