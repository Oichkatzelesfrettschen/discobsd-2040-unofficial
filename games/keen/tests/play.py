#!/usr/bin/env python3
# Plays keen to completion over a pty with a fixed seed and checks
# for the "solved!" message. GAMEBOX_TEST makes the program print
# its generated solution grid, row-major digits, before play starts;
# the test enters those digits at the cursor's natural left-to-right,
# top-to-bottom path, so it needs no puzzle solver of its own.
import os
import pty
import sys
import time
import select

SEED = "20260911"
SIZE = "5"


def read_all(fd, timeout=2.0):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if fd in r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
        elif buf:
            break
    return buf


def main():
    prog = sys.argv[1] if len(sys.argv) > 1 else "./keen-host"
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["GAMEBOX_TEST"] = "1"
        os.execvp(prog, [prog, SIZE, SEED])

    out = read_all(fd, 1.0)
    text = out.decode(errors="replace")
    firstline = text.splitlines()[0].strip()
    n = int(SIZE)
    assert len(firstline) == n * n and firstline.isdigit(), \
        "expected an %d-digit solution line, got %r" % (n * n, firstline)

    # Cursor starts at (0,0); entering a digit does not move it, so
    # move right after each entry, and down+home at each row end.
    for i, ch in enumerate(firstline):
        os.write(fd, ch.encode())
        col = i % n
        if col < n - 1:
            os.write(fd, b"\x1b[C")
        elif i // n < n - 1:
            os.write(fd, b"\x1b[B" + b"\x1b[D" * (n - 1))
        time.sleep(0.005)

    out = read_all(fd, 2.0)
    text += out.decode(errors="replace")
    os.waitpid(pid, 0)

    assert "solved!" in text, "keen: did not reach solved state"
    print("keen: OK (%dx%d grid, solved)" % (n, n))


if __name__ == "__main__":
    main()
