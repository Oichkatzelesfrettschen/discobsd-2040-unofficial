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
| `discobsd-term` | attach the current terminal; reattaches across board reboots; `--probe` checks the board answers; `--list` prints attached boards |
| `discobsd-web` | serve the console as a browser terminal on port 7681; a non-loopback bind requires `--token` |
| `discobsd-link` | redirect a short URL (port 42069) to the tokenized console URL |
| `discobsd-console` | `up`, `down`, `status`: run web and link detached, or through the systemd user units when installed |
| `discobsd-connect` | POSIX shell wrapper that launches tio, picocom, minicom, or cu on the board |

Log in as `operator` with no password, then `su` to root. Quit
`discobsd-term` with Ctrl-]. The console serves one session at a time.

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
| Windows | unzip `discobsd-host-windows.zip`; run the `.exe` files from a terminal (no Python needed) |
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
restarts the units instead of spawning detached processes; enable them at
boot with:

    systemctl --user enable --now discobsd-web discobsd-link
    loginctl enable-linger "$USER"

Firewall: open ports 7681 and 42069 to the LAN only, never to the
Internet, because the short link hands out the token. On a ufw host:

    sudo ufw allow from 10.0.0.0/24 to any port 7681 proto tcp
    sudo ufw allow from 10.0.0.0/24 to any port 42069 proto tcp

Set `DISCOBSD_PORT` to a device path to bypass discovery.

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
