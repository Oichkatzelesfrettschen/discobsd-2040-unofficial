"""discobsd-term: attach a terminal to the DiscoBSD RP2040 console.

The board is a USB CDC-ACM serial device with no network, so this is a
serial terminal, not telnet or ssh. It finds the board by its USB identity,
opens the line at 115200 8N1, and bridges the local terminal to it. When
the board reboots and its USB device re-enumerates, the terminal waits and
reattaches on its own rather than dying with a stale-node error.

Ctrl-] is the escape (as in telnet): Ctrl-] q quits, Ctrl-] _ leaves the V6
emulator, Ctrl-] ? lists the rest. Log in as operator with no password;
su for root.

POSIX hosts put the terminal in raw mode through termios and multiplex
with select. Windows has no termios and select refuses console handles, so
the console runs through msvcrt with a reader thread, the Windows key codes
for the arrow keys become their ANSI sequences, and the console output mode
gets virtual-terminal processing so the board's escapes render.
"""

from __future__ import annotations

import argparse
import sys
import threading
import time

from . import __version__, ports

ESCAPE = 0x1D  # Ctrl-]

# Ctrl-] is the escape, as in telnet: the next key says what to do. Every
# terminal on every operating system delivers Ctrl-] and a plain letter,
# where Ctrl-_ and Ctrl-\ are zoom or quit shortcuts in many of them.
ESCAPES = {
    b"q": ("quit", b""),
    b"\x1d": ("quit", b""),
    b"_": ("send", b"\x1f"),  # leave the V6 emulator (pdp11)
    b"d": ("send", b"\x7f"),  # DEL: V6 interrupt
    b"\\": ("send", b"\x1c"),  # Ctrl-\: V6 quit
    b"]": ("send", b"\x1d"),  # a literal Ctrl-]
}
HELP = "Ctrl-] then: q quit, _ leave V6, d DEL, \\ Ctrl-\\, ] literal Ctrl-]"


class Escape:
    """Split keyboard bytes into what goes to the board and what the escape
    asks for. feed() returns (bytes to send, action) where action is None,
    "quit", or "help"."""

    def __init__(self) -> None:
        self.armed = False

    def feed(self, keys: bytes):
        out = bytearray()
        action = None
        for b in keys:
            ch = bytes([b])
            if self.armed:
                self.armed = False
                kind, data = ESCAPES.get(ch, ("help", b""))
                if kind == "quit":
                    return bytes(out), "quit"
                if kind == "help":
                    action = "help"
                else:
                    out += data
            elif b == ESCAPE:
                self.armed = True
            else:
                out.append(b)
        return bytes(out), action

# Windows msvcrt reports an extended key as a 0x00 or 0xE0 prefix byte and
# a scan code; the board expects the VT100 sequences a real terminal sends.
WIN_KEYS = {
    0x48: b"\x1b[A",  # up
    0x50: b"\x1b[B",  # down
    0x4D: b"\x1b[C",  # right
    0x4B: b"\x1b[D",  # left
    0x47: b"\x1b[H",  # home
    0x4F: b"\x1b[F",  # end
    0x53: b"\x1b[3~",  # delete
    0x49: b"\x1b[5~",  # page up
    0x51: b"\x1b[6~",  # page down
}


def note(msg: str) -> None:
    sys.stderr.write(f"\r\n[discobsd-term] {msg}\r\n")
    sys.stderr.flush()


def wait_for_board(first: bool) -> str:
    device = ports.find_board()
    if device:
        return device
    if first:
        note("waiting for the board -- plug it in, or it is rebooting")
    while True:
        time.sleep(0.5)
        device = ports.find_board()
        if device:
            return device


def translate_windows_key(prefix: int, code: int) -> bytes:
    """Map an msvcrt extended key to the bytes a VT100 keyboard sends."""
    del prefix
    return WIN_KEYS.get(code, b"")


def _session_posix(open_line) -> None:
    import os
    import select
    import termios
    import tty

    fd_in = sys.stdin.fileno()
    saved = termios.tcgetattr(fd_in)
    tty.setraw(fd_in)
    esc = Escape()
    try:
        first = True
        while True:
            device = wait_for_board(first)
            first = False
            try:
                line = open_line(device)
            except Exception as exc:
                note(f"cannot open {device}: {exc}")
                time.sleep(0.5)
                continue
            note(f"attached to {device}")
            try:
                line.write(b"\r")
            except Exception:
                pass
            fd_line = line.fileno()
            try:
                while True:
                    ready, _, _ = select.select([fd_line, fd_in], [], [], 0.2)
                    if fd_line in ready:
                        data = line.read(4096)
                        if data:
                            os.write(1, data)
                    if fd_in in ready:
                        keys, action = esc.feed(os.read(fd_in, 1024))
                        if action == "quit":
                            note("bye")
                            return
                        if action == "help":
                            note(HELP)
                        if keys:
                            try:
                                line.write(keys)
                            except Exception as exc:
                                raise OSError("write failed") from exc
            except OSError:
                note("board went away -- waiting for it to come back")
                try:
                    line.close()
                except Exception:
                    pass
                continue
    finally:
        termios.tcsetattr(fd_in, termios.TCSADRAIN, saved)


def _enable_windows_vt() -> None:
    import ctypes

    kernel32 = ctypes.windll.kernel32
    handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
    mode = ctypes.c_uint32()
    if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
        kernel32.SetConsoleMode(handle, mode.value | 0x0004)  # ENABLE_VIRTUAL_TERMINAL_PROCESSING


def _session_windows(open_line) -> None:
    import msvcrt

    _enable_windows_vt()
    out = sys.stdout.buffer
    quit_flag = threading.Event()
    keys = []
    keys_lock = threading.Lock()
    esc = Escape()

    def keyboard():
        while not quit_flag.is_set():
            if not msvcrt.kbhit():
                time.sleep(0.01)
                continue
            ch = msvcrt.getch()
            if ch in (b"\x00", b"\xe0"):
                code = msvcrt.getch()
                data = translate_windows_key(ch[0], code[0])
            else:
                data = ch
            data, action = esc.feed(data)
            if action == "quit":
                quit_flag.set()
                return
            if action == "help":
                note(HELP)
            if data:
                with keys_lock:
                    keys.append(data)

    threading.Thread(target=keyboard, daemon=True).start()
    first = True
    while not quit_flag.is_set():
        device = wait_for_board(first)
        first = False
        try:
            line = open_line(device)
        except Exception as exc:
            note(f"cannot open {device}: {exc}")
            time.sleep(0.5)
            continue
        note(f"attached to {device}")
        try:
            line.write(b"\r")
        except Exception:
            pass
        try:
            while not quit_flag.is_set():
                data = line.read(4096)
                if data:
                    out.write(data)
                    out.flush()
                with keys_lock:
                    pending = b"".join(keys)
                    keys.clear()
                if pending:
                    line.write(pending)
                elif not data:
                    time.sleep(0.02)
        except Exception:
            note("board went away -- waiting for it to come back")
            try:
                line.close()
            except Exception:
                pass
            continue
        line.close()
    note("bye")


def run(open_line=ports.open_serial) -> int:
    if not sys.stdin.isatty():
        note("stdin is not a terminal; run this from an interactive shell")
        return 1
    note("Ctrl-] q quits; Ctrl-] ? lists the escapes. Log in as operator, no password.")
    if sys.platform == "win32":
        _session_windows(open_line)
    else:
        _session_posix(open_line)
    return 0


def probe(open_line=ports.open_serial, seconds: float = 3.0) -> int:
    """Connect, poke, read for a few seconds, and report, with no raw terminal.
    Answers "is the board reachable?" and is how a host install is tested."""
    device = ports.find_board()
    if not device:
        note("no board found")
        return 1
    try:
        line = open_line(device)
    except Exception as exc:
        note(f"cannot open {device}: {exc}")
        return 1
    try:
        line.write(b"\n")
    except Exception as exc:
        note(f"write to {device} failed: {exc} (board may be wedged)")
        return 1
    end = time.time() + seconds
    got = b""
    while time.time() < end:
        d = line.read(4096)
        if d:
            got += d
        else:
            time.sleep(0.05)
    line.close()
    if got:
        note("reachable at %s -- %d bytes, tail: %r" % (device, len(got), got[-60:]))
        return 0
    note(f"attached to {device} but the board sent nothing")
    return 1


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="discobsd-term", description="Attach a terminal to the DiscoBSD RP2040 console."
    )
    p.add_argument("--probe", action="store_true", help="report whether the board answers")
    p.add_argument("--list", action="store_true", help="list attached boards and exit")
    p.add_argument("--version", action="version", version="discobsd-term " + __version__)
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        import serial  # noqa: F401
    except ImportError:
        note(ports.pyserial_hint())
        return 1
    if args.list:
        boards = ports.list_boards()
        for b in boards:
            print(b)
        return 0 if boards else 1
    if args.probe:
        return probe()
    return run()


if __name__ == "__main__":
    sys.exit(main())
