#!/usr/bin/env python3
# Plays fifteen to completion over a pty with a fixed seed and checks
# for the "solved!" message. GAMEBOX_TEST makes the program print its
# shuffle move list (u/d/l/r, the direction the blank moved) before
# the board; feeding back the exact reverse sequence undoes the
# shuffle regardless of which rand() the host libc supplies.
import os
import pty
import sys
import time

SEED = "20260911"
KEY = {"u": b"\x1b[A", "d": b"\x1b[B", "l": b"\x1b[D", "r": b"\x1b[C"}
OPPOSITE = {"u": "d", "d": "u", "l": "r", "r": "l"}


def read_all(fd, timeout=2.0):
    import select

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
    prog = sys.argv[1] if len(sys.argv) > 1 else "./fifteen-host"
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["GAMEBOX_TEST"] = "1"
        os.execvp(prog, [prog, SEED])

    out = read_all(fd, 1.0)
    text = out.decode(errors="replace")
    firstline = text.splitlines()[0].strip()
    assert firstline and all(c in "udlr" for c in firstline), \
        "expected a shuffle log line, got %r" % firstline

    solve = "".join(OPPOSITE[c] for c in reversed(firstline))
    for c in solve:
        os.write(fd, KEY[c])
        time.sleep(0.005)

    out = read_all(fd, 2.0)
    text += out.decode(errors="replace")
    os.waitpid(pid, 0)

    assert "solved!" in text, "fifteen: did not reach solved state"
    print("fifteen: OK (%d shuffle moves, board solved)" % len(firstline))


if __name__ == "__main__":
    main()
