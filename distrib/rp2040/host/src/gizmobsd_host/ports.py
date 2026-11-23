"""Find and open the DiscoBSD RP2040 console on any host operating system.

The kernel enumerates as USB vendor 0x2e8a, product 0x000a, with a serial
number that ends in "rp2040" (the flash chip's unique ID in hex, a hyphen
and that suffix; "rp2040" alone from a kernel that cannot read the ID or
predates it) and the product string "GizmoBSD RP2040 console" ("DiscoBSD
RP2040 console" from a kernel built before the rename).
pyserial's port enumeration exposes those on Linux, Windows, and macOS, so
identity, not the drifting device number, selects the board. Windows
reports the serial number from the device instance ID, which the PnP
manager upper-cases ("RP2040"), and its inbox usbser driver exposes no
product string, so the serial number's ending is compared without regard
to case and the product string is only a second chance. A Pico SDK program
on the same vendor and product reports the bare hex ID and is not matched. The DISCOBSD_PORT
environment variable overrides discovery for a board behind an adapter
or a test fixture.

Two boards can enumerate alike (2e8a:000a, serial "rp2040"), so identity
alone cannot choose between them. find_board() is the one selection rule
for gizmobsd-term and gizmobsd-web, and mirrors gizmobsd-flash and
gizmobsd-connect: with more than one 2e8a device in the Linux USB sysfs
tree and no board named it raises BoardSelectionError, and a USB port
named by `usb_port` (--usb-port) or DISCOBSD_USB_PORT selects the tty of
that port's interface 0. DISCOBSD_SYSFS_USB and DISCOBSD_DEVFS replace
/sys/bus/usb/devices and /dev for tests, as in the shell tools.
"""

from __future__ import annotations

import glob
import os
import re
import sys

USB_VID = 0x2E8A
USB_PID = 0x000A
USB_SERIAL = "rp2040"
BAUD = 115200
ENV_PORT = "DISCOBSD_PORT"
ENV_USB_PORT = "DISCOBSD_USB_PORT"
ENV_SYSFS = "DISCOBSD_SYSFS_USB"
ENV_DEVFS = "DISCOBSD_DEVFS"
DEFAULT_SYSFS = "/sys/bus/usb/devices"
DEFAULT_DEVFS = "/dev"
# A sysfs device name: bus-port[.port...] and nothing else, because the
# value is joined into a sysfs path.
USB_PORT_RE = re.compile(r"[0-9]+-[0-9]+(\.[0-9]+)*")
# gizmobsd-web and gizmobsd-link share one bearer secret: the unit files'
# EnvironmentFile sets DISCOBSD_WEB_TOKEN, and LoadCredential=web-token:...
# would expose the same secret as a file, never in argv or `ps`.
TOKEN_ENV = "DISCOBSD_WEB_TOKEN"
TOKEN_CREDENTIAL = "web-token"

# Linux keeps a by-id symlink for the console; it is the fallback when the
# enumeration library is unavailable or reports zero USB attributes.
# A kernel built before the system took the name GizmoBSD reports DiscoBSD.
LINUX_BY_ID = (
    "/dev/serial/by-id/*GizmoBSD*rp2040-if00",
    "/dev/serial/by-id/*DiscoBSD*rp2040-if00",
)
PRODUCT_NAMES = ("GizmoBSD", "DiscoBSD")


def _is_board(info) -> bool:
    vid = getattr(info, "vid", None)
    pid = getattr(info, "pid", None)
    serial_number = getattr(info, "serial_number", None) or ""
    product = getattr(info, "product", None) or ""
    if vid == USB_VID and pid == USB_PID:
        return serial_number.lower().endswith(USB_SERIAL) or any(
            name in product for name in PRODUCT_NAMES
        )
    return False


def _preferred(device: str) -> str:
    """macOS lists both tty.* and cu.*; cu.* is the call-out node a terminal
    should open, because tty.* blocks until carrier detect."""
    if sys.platform == "darwin" and "/tty." in device:
        candidate = device.replace("/tty.", "/cu.", 1)
        if os.path.exists(candidate):
            return candidate
    return device


def list_boards(comports=None) -> list[str]:
    """Every attached DiscoBSD console, sorted by device name."""
    if comports is None:
        try:
            from serial.tools import list_ports

            comports = list_ports.comports
        except ImportError:
            comports = None
    found = []
    if comports is not None:
        for info in comports():
            if _is_board(info):
                found.append(_preferred(info.device))
    if not found and sys.platform.startswith("linux"):
        found = sorted(path for pattern in LINUX_BY_ID for path in glob.glob(pattern))
    return sorted(set(found))


class BoardSelectionError(Exception):
    """No single board can be chosen; str() is the operator-facing reason.

    kind is "ambiguous" (several 2e8a devices, none named), "absent" (the
    named USB port lacks a 2e8a device or a console tty), "invalid" (the
    port is not a USB port path) or "unsupported" (no sysfs on this host).
    A caller that waits for a board treats "absent" as not yet attached.
    """

    def __init__(self, kind: str, message: str):
        super().__init__(message)
        self.kind = kind

    @property
    def exit_code(self) -> int:
        return 2 if self.kind == "ambiguous" else 1


def add_usb_port_argument(parser) -> None:
    parser.add_argument(
        "--usb-port",
        metavar="PORT",
        help="use the board at USB port PORT, such as 1-3 (Linux; default "
        "$DISCOBSD_USB_PORT). With several boards attached and none named, "
        "the tool refuses to choose",
    )


def _sysattr(sysfs: str, name: str, attr: str) -> str:
    try:
        with open(os.path.join(sysfs, name, attr), encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return ""


def _check_ambiguous(sysfs: str) -> None:
    """Refuse when more than one 2e8a device (kernel or BOOTSEL) is attached."""
    try:
        names = sorted(os.listdir(sysfs))
    except OSError:
        return  # no sysfs: macOS and Windows keep the unchanged discovery
    devices = [n for n in names if _sysattr(sysfs, n, "idVendor") == "2e8a"]
    if len(devices) < 2:
        return
    lines = [
        "  %s 2e8a:%s %s" % (n, _sysattr(sysfs, n, "idProduct"), _sysattr(sysfs, n, "product"))
        for n in devices
    ]
    raise BoardSelectionError(
        "ambiguous",
        "more than one 2e8a device is attached; name the board with "
        "--usb-port PORT or DISCOBSD_USB_PORT:\n" + "\n".join(lines),
    )


def usb_port_console(usb_port: str, sysfs: str, devfs: str) -> str:
    """The console tty of the board at `usb_port`, from the USB sysfs tree.

    /dev/ttyACMn is renumbered at each re-enumeration, so the udev by-path
    link for the same interface replaces the tty name whenever it points at
    it; the by-path name follows the physical port.
    """
    if not USB_PORT_RE.fullmatch(usb_port):
        raise BoardSelectionError(
            "invalid", f"{usb_port} is not a USB port path such as 1-3 or 1-2.4"
        )
    if not os.path.isdir(sysfs):
        raise BoardSelectionError(
            "unsupported", "a USB port selector reads Linux sysfs; this host has none"
        )
    if _sysattr(sysfs, usb_port, "idVendor") != "2e8a":
        raise BoardSelectionError("absent", f"USB port {usb_port} holds no 2e8a device")
    pattern = os.path.join(sysfs, usb_port, usb_port + ":1.0", "tty", "*")
    for node in sorted(glob.glob(pattern)):
        tty = os.path.join(devfs, os.path.basename(node))
        suffix = usb_port.split("-", 1)[1]
        links = glob.glob(os.path.join(devfs, "serial", "by-path", f"*-usb-*:{suffix}:1.0"))
        for link in sorted(links):
            try:
                if os.path.samefile(link, tty):
                    return link
            except OSError:
                continue
        return tty
    raise BoardSelectionError(
        "absent",
        "port %s holds 2e8a:%s, which has no console tty"
        % (usb_port, _sysattr(sysfs, usb_port, "idProduct")),
    )


def find_board(comports=None, environ=None, usb_port=None, sysfs=None, devfs=None):
    """The console to use, or None when no board is attached.

    Order: an explicit `usb_port` (the --usb-port operand, which must not be
    empty); DISCOBSD_PORT, a device path that names the board itself;
    DISCOBSD_USB_PORT (empty counts as unset); then discovery, which raises
    BoardSelectionError("ambiguous") with several 2e8a devices attached and
    returns the one board's device otherwise.
    """
    environ = os.environ if environ is None else environ
    sysfs = sysfs or environ.get(ENV_SYSFS) or DEFAULT_SYSFS
    devfs = devfs or environ.get(ENV_DEVFS) or DEFAULT_DEVFS
    if usb_port is not None:
        if not usb_port:
            raise BoardSelectionError("invalid", "--usb-port needs a port such as 1-3")
        return usb_port_console(usb_port, sysfs, devfs)
    override = environ.get(ENV_PORT)
    if override:
        return override
    env_usb = environ.get(ENV_USB_PORT)
    if env_usb:
        return usb_port_console(env_usb, sysfs, devfs)
    _check_ambiguous(sysfs)
    boards = list_boards(comports)
    return boards[0] if boards else None


def open_serial(device: str):
    """Open the console at its fixed line settings.

    A zero read timeout keeps every poll loop responsive; the write timeout
    bounds a write to a board that has stopped draining its input queue.
    DTR and RTS are asserted because a CDC-ACM device may hold its output
    until the host signals a terminal is present.
    """
    import serial

    line = serial.Serial(
        device,
        BAUD,
        timeout=0,
        write_timeout=2,
        rtscts=False,
        dsrdtr=False,
    )
    line.dtr = True
    line.rts = True
    return line


def pyserial_hint() -> str:
    return (
        "pyserial is missing: pip install pyserial, apt install python3-serial, "
        "or pacman -S python-pyserial"
    )


def credential(name: str, env: str, environ=None) -> str | None:
    """A secret from a systemd credential, or an environment variable.

    LoadCredential=/SetCredential= in a systemd unit exposes a credential as
    a file named `name` under $CREDENTIALS_DIRECTORY, readable only by the
    service's own user and never visible in argv or `ps`; it is checked
    first. `env` is the fallback for a shell launch or a unit that still
    uses EnvironmentFile, such as this package's own web.env.
    """
    environ = os.environ if environ is None else environ
    directory = environ.get("CREDENTIALS_DIRECTORY")
    if directory:
        try:
            with open(os.path.join(directory, name), encoding="utf-8") as f:
                value = f.read().strip()
            if value:
                return value
        except OSError:
            pass
    value = environ.get(env)
    return value or None
