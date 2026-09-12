#!/usr/bin/env python3
# Fires three shots into one empty column over a pty and checks that
# the score increases (a pop happened) and the board reports empty.
# GAMEBOX_TEST starts the board empty and forces the shot color to
# cycle 1,1,1,2,2,2,... one color per three shots, independent of
# rand(), so three shots in a row always match and pop.
import os
import pty
import sys
import time
import select

SEED = "20260911"


def read_all(fd, timeout=1.5):
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
    prog = sys.argv[1] if len(sys.argv) > 1 else "./bubble-host"
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["GAMEBOX_TEST"] = "1"
        os.execvp(prog, [prog, SEED])

    text = read_all(fd, 0.5).decode(errors="replace")
    assert "score 0" in text, "expected an empty starting board"

    for _ in range(3):
        os.write(fd, b" ")
        time.sleep(0.01)
    text += read_all(fd, 0.5).decode(errors="replace")

    assert "score 30" in text, \
        "bubble: three matching shots did not pop (no score 30 in output)"

    os.write(fd, b"q")
    read_all(fd, 0.5)
    os.waitpid(pid, 0)
    print("bubble: OK (three shots popped, score 30)")


if __name__ == "__main__":
    main()
