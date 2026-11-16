#!/usr/bin/env python3
"""Drive a program on a pseudo-terminal through an expect/send script.

usage: ptyrun.py [-t SECONDS] [--wait-exit] -- COMMAND [ARGS...] < SCRIPT

Each SCRIPT line is `expect TEXT` or `send TEXT` (TEXT with Python escapes),
applied in order; `expect` waits up to the timeout for TEXT to appear in the
program's output. Everything the program printed goes to stdout at the end,
and the exit status is 0 when every expect matched and 1 otherwise, so a
test can gate on it.

--wait-exit requires command completion within the timeout after the script,
drains its output and preserves its exit status. The default mode terminates
an interactive program after the script completes.
"""
import errno
import os
import pty
import select
import signal
import sys
import time


def wait_for_exit(pid, fd, out, timeout):
    """Drain the terminal until its child exits; bound and reap timed-out children."""
    deadline = time.monotonic() + timeout
    terminal_open = True
    while True:
        child, status = os.waitpid(pid, os.WNOHANG)
        if child:
            while terminal_open and select.select([fd], [], [], 0)[0]:
                if time.monotonic() >= deadline:
                    print("ptyrun: timeout draining terminal output", file=sys.stderr)
                    return 1
                terminal_open = read_terminal(fd, out)
            code = os.waitstatus_to_exitcode(status)
            return code if code >= 0 else 128 - code
        left = deadline - time.monotonic()
        if left <= 0:
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            print("ptyrun: timeout waiting for child exit", file=sys.stderr)
            return 1
        if terminal_open:
            if select.select([fd], [], [], min(left, 0.1))[0]:
                terminal_open = read_terminal(fd, out)
        else:
            time.sleep(min(left, 0.1))


def read_terminal(fd, out):
    """Treat PTY EIO and an empty read as end of stream; propagate other errors."""
    try:
        data = os.read(fd, 4096)
    except OSError as error:
        if error.errno != errno.EIO:
            raise
        return False
    out += data
    return bool(data)


def main():
    argv = sys.argv[1:]
    timeout = 30.0
    if argv and argv[0] == "-t":
        timeout = float(argv[1])
        argv = argv[2:]
    wait_exit = bool(argv and argv[0] == "--wait-exit")
    if wait_exit:
        argv = argv[1:]
    if argv and argv[0] == "--":
        argv = argv[1:]
    script = []
    for line in sys.stdin.read().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        verb, _, text = line.partition(" ")
        script.append((verb, text.encode().decode("unicode_escape").encode("latin1")))

    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(argv[0], argv)
    out = bytearray()
    ok = True
    pos = 0
    for verb, text in script:
        if verb == "send":
            os.write(fd, text)
            continue
        deadline = time.time() + timeout
        while out.find(text, pos) < 0:
            left = deadline - time.time()
            if left <= 0:
                ok = False
                break
            r, _, _ = select.select([fd], [], [], min(left, 0.5))
            if fd in r:
                try:
                    data = os.read(fd, 4096)
                except OSError:
                    data = b""
                if not data:
                    ok = False
                    break
                out += data
        if not ok:
            sys.stdout.write("ptyrun: timeout waiting for %r\n" % text)
            break
        pos = out.find(text, pos) + len(text)
        time.sleep(0.3)     # let the program finish setting the tty modes
    if wait_exit:
        status = wait_for_exit(pid, fd, out, timeout)
        os.close(fd)
        sys.stdout.write(out.decode("latin1").replace("\r", ""))
        return status if ok else 1
    time.sleep(0.3)
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.3)
            if fd not in r:
                break
            data = os.read(fd, 4096)
            if not data:
                break
            out += data
    except OSError:
        pass
    try:
        os.kill(pid, 15)
    except OSError:
        pass
    os.waitpid(pid, 0)
    sys.stdout.write(out.decode("latin1").replace("\r", ""))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
