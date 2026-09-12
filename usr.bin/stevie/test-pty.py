"""Drive the host build of stevie through a pty: open a file, insert text,
leave insert mode, exercise a normal-mode motion and delete, write and
quit, then diff the saved file against what the key sequence should have
produced. A second run takes the :q! path and proves it discards.

This exercises everything the port changed except the board's own tty
driver: raw mode entry and restore (window.c's HOSTBUILD half differs
from the target's sgtty half only in which ioctl it calls), the CUP
off-by-one, the tty restore on exit, windrefresh's fflush, Enter inserting one newline rather
than CR and LF, and the write path in cmdline.c's writeit.

Usage: test-pty.py PATH_TO_STEVIE_HOST
Run with ${PYTHON:-python3} after `bmake stevie.host` in this directory.
"""
import fcntl
import os
import pty
import select
import struct
import sys
import tempfile
import termios
import time

STEVIE = sys.argv[1] if len(sys.argv) > 1 else "./stevie.host"
ESC = "\x1b"

# The editor puts its status and command line on the last of 24 rows.
# Upstream emitted "\033[23;0H" for it, one row high, because CUP counts
# from 1 and ECMA-48 clamps a 0 parameter up to 1 -- which also collided
# screen rows 0 and 1. The fixed windgoto emits row 24, column 1.
STATUS_CUP = b"\x1b[24;1H"


def set_winsize(fd, rows=24, cols=80):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def read_available(fd, timeout=0.4):
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
            end = time.time() + 0.15
    return out


def send(fd, s):
    os.write(fd, s.encode("latin-1"))


def spawn(path):
    pid, master = pty.fork()
    if pid == 0:
        os.execv(STEVIE, [STEVIE, path])
        os._exit(127)
    set_winsize(master)
    time.sleep(0.25)
    return pid, master


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def run_write_test():
    path = os.path.join(tempfile.gettempdir(),
                        "stevie-test-pty-%d.txt" % os.getpid())
    if os.path.exists(path):
        os.unlink(path)

    pid, master = spawn(path)

    # The child has already called windinit, so the pty is raw now.
    # Restoring it is windrestore's job, and nothing but this check
    # would notice if it stopped running: the board's console would
    # come back from :q with no echo and no line editing.
    raw = termios.tcgetattr(master)
    if raw[3] & (termios.ICANON | termios.ECHO):
        fail("windinit did not put the tty in raw mode")

    # The opening paint reaches the terminal only if windrefresh flushes
    # stdout: nothing stevie writes ends in a newline, so a buffered
    # stdout would hold all of it until exit.
    opening = read_available(master)
    if not opening:
        fail("no output before any key was sent; windrefresh is not "
             "flushing stdout")
    if STATUS_CUP not in opening:
        fail("the status line never addressed row 24 (expected %r); "
             "windgoto's CUP row is off by one" % STATUS_CUP)

    # Insert four lines. Enter must insert exactly one '\n' -- upstream
    # inserted '\n' and then fell through and inserted '\r' as well.
    send(master, "i")
    read_available(master)
    send(master, "alpha\rbravo\rcharlie\rdelta")
    read_available(master)
    send(master, ESC)
    read_available(master)

    # Normal mode: 1G to the first line, x deletes 'a' from "alpha",
    # then j j dd removes the "charlie" line. Nothing here works unless
    # the editor actually left insert mode on ESC.
    send(master, "1G")
    read_available(master)
    send(master, "x")
    read_available(master)
    send(master, "jjdd")
    read_available(master)

    send(master, ":wq\r")
    read_available(master)
    _, status = os.waitpid(pid, 0)
    restored = termios.tcgetattr(master)
    os.close(master)

    if not (restored[3] & termios.ICANON) or not (restored[3] & termios.ECHO):
        fail(":q left the tty raw and unechoed; windrestore did not run")

    if not os.path.exists(path):
        fail("%s was never written" % path)
    with open(path) as f:
        content = f.read()
    os.unlink(path)

    expected = "lpha\nbravo\ndelta\n"
    if content != expected:
        fail("content mismatch\nexpected: %r\nactual:   %r"
             % (expected, content))
    print("exit status: %d" % status)
    print("PASS: i/type/ESC/1G/x/jj/dd/:wq wrote %r" % content)


def run_discard_test():
    """:q! leaves the file as it was, and :q on a changed file refuses."""
    path = os.path.join(tempfile.gettempdir(),
                        "stevie-test-pty-q-%d.txt" % os.getpid())
    with open(path, "w") as f:
        f.write("original\n")

    pid, master = spawn(path)
    read_available(master)
    send(master, "ichanged")
    read_available(master)
    send(master, ESC)
    read_available(master)

    # A plain :q on a modified buffer must refuse and say so.
    send(master, ":q\r")
    screen = read_available(master)
    if b"q!" not in screen:
        fail(":q on a modified file did not refuse with the 'use q!' "
             "message")

    send(master, ":q!\r")
    read_available(master)
    os.waitpid(pid, 0)
    os.close(master)

    with open(path) as f:
        content = f.read()
    os.unlink(path)
    if content != "original\n":
        fail(":q! wrote the buffer out; file is now %r" % content)
    print("PASS: :q refuses a modified buffer and :q! discards it")


if __name__ == "__main__":
    run_write_test()
    run_discard_test()
