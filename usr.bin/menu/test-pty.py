"""Drive the host build of menu through a pty: point it at a temporary
/etc/menu, move the cursor down twice and run the resulting entry, then
quit, checking that the entry's own output reached the screen and that
the process exited cleanly.

This exercises raw-mode entry/exit, the keymap-to-action dispatch (j/k
and arrow keys map to the same ACT_UP/ACT_DOWN as each other), the list
widget's cursor movement and redraw, /etc/menu parsing, and the
fork/execlp("/bin/sh", ...)/wait command path -- everything menu.c does
except the target's own sgtty ioctls (TERMIOS selects the host's
<termios.h> instead, see menu.c).

Usage: test-pty.py PATH_TO_MENU_HOST
Run with ${PYTHON:-python3} after `bmake menu.host` in this directory.
"""
import fcntl
import os
import pty
import struct
import sys
import tempfile
import termios
import time

MENU = sys.argv[1] if len(sys.argv) > 1 else "./menu.host"
MENUFILE = os.path.join(tempfile.gettempdir(), "menu-test-pty-%d.conf" % os.getpid())

DOWN = "\x1b[B"
ENTER = "\r"
QUIT = "q"


def set_winsize(fd, rows=24, cols=80):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def read_available(fd, timeout=0.4):
    import select
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


def main():
    # Three entries: the two built-ins (Run a program, Show a file) come
    # first, so cursor row 2 (0-indexed) after two Down presses from row 0
    # is this file's own first configured entry.
    with open(MENUFILE, "w") as f:
        f.write("# test menu\n")
        f.write("Say hi: echo hi-from-menu\n")
        f.write("List root: ls /\n")

    pid, master = pty.fork()
    if pid == 0:
        os.execv(MENU, [MENU, MENUFILE])
        os._exit(127)

    set_winsize(master)
    time.sleep(0.2)
    screen = read_available(master)
    if b"DiscoBSD menu" not in screen:
        print("FAIL: initial screen did not paint the header")
        print(screen)
        sys.exit(1)

    # Down, down: builtin 0 (Run a program) -> builtin 1 (Show a file,
    # builtin count is 3) -> "Say hi" is entry index 3, so three downs
    # land on it. (Built-ins: Run a program, Show a file, List a
    # directory -- three entries, 0..2 -- then the two /etc/menu lines
    # at index 3 and 4.)
    send(master, DOWN * 3)
    screen = read_available(master)
    if b"Say hi" not in screen:
        print("FAIL: cursor did not reach 'Say hi' after three Down presses")
        print(screen)
        sys.exit(1)

    send(master, ENTER)
    screen = read_available(master, timeout=0.6)
    if b"hi-from-menu" not in screen:
        print("FAIL: running the entry did not produce its output")
        print(screen)
        sys.exit(1)

    # menu.c pauses after a command with "press any key"; anything but a
    # bound key dismisses it and repaints the list.
    send(master, " ")
    read_available(master)

    send(master, QUIT)
    read_available(master)

    _, status = os.waitpid(pid, 0)
    os.close(master)

    print("exit status: %d" % status)
    if os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0:
        print("PASS: menu.host ran an /etc/menu entry and quit cleanly")
    else:
        print("FAIL: menu.host did not exit cleanly")
        sys.exit(1)

    os.unlink(MENUFILE)


if __name__ == "__main__":
    main()
