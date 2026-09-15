"""Find and open the DiscoBSD RP2040 console on any host operating system.

The kernel enumerates as USB vendor 0x2e8a, product 0x000a, with the fixed
serial number "rp2040" and the product string "DiscoBSD RP2040 console".
pyserial's port enumeration exposes those on Linux, Windows, and macOS, so
identity, not the drifting device number, selects the board. The
DISCOBSD_PORT environment variable overrides discovery for a board behind
an adapter or a test fixture.
"""

from __future__ import annotations

import glob
import os
import sys

USB_VID = 0x2E8A
USB_PID = 0x000A
USB_SERIAL = "rp2040"
BAUD = 115200
ENV_PORT = "DISCOBSD_PORT"

# Linux keeps a by-id symlink for the console; it is the fallback when the
# enumeration library is unavailable or reports no USB attributes.
LINUX_BY_ID = "/dev/serial/by-id/*DiscoBSD*rp2040-if00"


def _is_board(info) -> bool:
    vid = getattr(info, "vid", None)
    pid = getattr(info, "pid", None)
    serial_number = getattr(info, "serial_number", None) or ""
    product = getattr(info, "product", None) or ""
    if vid == USB_VID and pid == USB_PID:
        return serial_number == USB_SERIAL or "DiscoBSD" in product
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
        found = sorted(glob.glob(LINUX_BY_ID))
    return sorted(set(found))


def find_board(comports=None, environ=None) -> str | None:
    """The console to use: DISCOBSD_PORT when set, else the first board."""
    environ = os.environ if environ is None else environ
    override = environ.get(ENV_PORT)
    if override:
        return override
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
