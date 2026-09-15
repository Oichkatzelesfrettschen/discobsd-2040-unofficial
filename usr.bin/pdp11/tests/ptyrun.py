#!/usr/bin/env python3
"""Drive a program on a pseudo-terminal through an expect/send script.

usage: ptyrun.py [-t SECONDS] -- COMMAND [ARGS...] < SCRIPT

Each SCRIPT line is `expect TEXT` or `send TEXT` (TEXT with Python escapes),
applied in order; `expect` waits up to the timeout for TEXT to appear in the
program's output. Everything the program printed goes to stdout at the end,
and the exit status is 0 when every expect matched and 1 otherwise, so a
test can gate on it.
"""
import os
import pty
import select
import sys
import time


def main():
    argv = sys.argv[1:]
    timeout = 30.0
    if argv and argv[0] == "-t":
        timeout = float(argv[1])
        argv = argv[2:]
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
