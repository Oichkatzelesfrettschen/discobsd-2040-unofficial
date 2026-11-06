#!/usr/bin/env python3
# pty smoke tests of bubble-host. Each case runs the game in a pty, feeds
# keys, and reads the last drawn frame (the text after the final cursor-home)
# with the escape sequences stripped. GAMEBOX_TEST fixes the shot colors and
# empties the board: a single digit cycles 1,1,1,2,2,2,..., a longer digit
# string is the repeated color script.
#
# usage: play.py [./bubble-host [case ...]]
#   a case is a test function name without its test_ prefix; none runs all
import os
import pty
import re
import select
import sys
import time

SEED = "20260911"
RIGHT = b"\x1b[C"
LEFT = b"\x1b[D"
GLYPH = {1: "@", 2: "#", 3: "O", 4: "*"}
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


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


def strip_ansi(t):
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", t)


def last_frame(raw):
    """Lines of the final frame: the text after the last cursor-home."""
    i = raw.rfind(b"\x1b[H")
    text = strip_ansi(raw[i + 3 :].decode(errors="replace"))
    return [ln.rstrip("\r") for ln in text.split("\n")]


class Game:
    def __init__(self, prog, args, test):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.pop("GAMEBOX_TEST", None)
            if test is not None:
                os.environ["GAMEBOX_TEST"] = test
            os.execvp(prog, [prog] + list(args))
        self.raw = b""
        self.pull()

    def pull(self, timeout=0.5):
        self.raw += read_all(self.fd, timeout)
        return self.raw

    def keys(self, data, repeat=1):
        for _ in range(repeat):
            os.write(self.fd, data)
            time.sleep(0.01)
        return self.pull()

    def text(self):
        return strip_ansi(self.raw.decode(errors="replace"))

    def frame(self):
        return last_frame(self.raw)

    def finish(self):
        """Quit and return the exit status."""
        try:
            os.write(self.fd, b"q")
        except OSError:
            pass
        read_all(self.fd, 0.5)
        _, status = os.waitpid(self.pid, 0)
        return os.waitstatus_to_exitcode(status)

    def wait(self):
        read_all(self.fd, 0.5)
        _, status = os.waitpid(self.pid, 0)
        return os.waitstatus_to_exitcode(status)


def cell_col(row_line, c, shifted):
    return 4 + (1 if shifted else 0) + 2 * c


def test_square_trio(prog):
    g = Game(prog, [SEED], "1")
    check("score 0" in g.text(), "expected an empty starting board")
    g.keys(b" ", 3)
    check("score 30" in g.text(), "three matching shots did not pop (no score 30)")
    check("hex" not in g.text() and "drop in" not in g.text(), "square mode shows hexagonal status")
    check(g.finish() == 0, "the game did not exit cleanly")


def test_hex_stagger(prog):
    g = Game(prog, ["-x", SEED], "1")
    check("bubble hex" in g.text(), "hex title missing")
    check("drop in 8" in g.text(), "descent counter not at 8")
    g.keys(b" ", 2)
    f = g.frame()
    row0, row1 = f[3], f[4]
    check(row0[cell_col(row0, 4, False)] == "@", "first bubble not at row 0 column 4: %r" % row0)
    check(
        row1[cell_col(row1, 3, True)] == "@",
        "second bubble not at row 1 column 3 (shifted): %r" % row1,
    )
    check(
        cell_col(row1, 3, True) == cell_col(row0, 4, False) - 1,
        "the shifted row does not sit one terminal column left of its parent",
    )
    check("drop in 6" in "\n".join(f), "counter did not count two shots: %r" % f[0])
    check(g.finish() == 0, "the game did not exit cleanly")


def test_hex_pop(prog):
    g = Game(prog, ["-x", SEED], "1")
    g.keys(b" ", 3)
    t = g.text()
    check("score 30" in t, "the zigzag trio did not pop")
    check("cleared!" in t, "the emptied board did not end the game")
    check(g.wait() == 0, "the game did not exit cleanly")


def test_hex_orphan(prog):
    g = Game(prog, ["-x", SEED], "1211")
    g.keys(b" ", 2)
    f = g.frame()
    check(f[4][cell_col(f[4], 3, True)] == "#", "color 2 not hanging at row 1 column 3: %r" % f[4])
    g.keys(RIGHT, 3)
    g.keys(b" ")
    g.keys(RIGHT, 2)
    g.keys(b" ")
    t = g.text()
    check("score 50" in t, "trio plus one orphan did not score 50")
    check("cleared!" in t, "the dropped bubble was not removed")
    check(g.wait() == 0, "the game did not exit cleanly")


def test_hex_descent(prog):
    g = Game(prog, ["-x", SEED], "12")
    g.keys(b" ", 7)
    check("drop in 1" in g.text(), "counter did not reach 1 after seven shots")
    g.keys(b" ")
    f = g.frame()
    check("drop in 8" in "\n".join(f), "counter did not reset after the descent: %r" % f[0])
    row0 = f[3]
    cells = [row0[cell_col(row0, c, True)] for c in range(7)]
    check("".join(cells) == "@#O*@#O", "new ceiling row wrong: %r" % row0)
    check(row0[cell_col(row0, 0, True) - 1] == " ", "shifted ceiling row not indented")
    row1 = f[4]
    check(
        row1[cell_col(row1, 4, False)] == "@", "old first bubble not at row 1 column 4: %r" % row1
    )
    check(g.finish() == 0, "the game did not exit cleanly")


def test_hex_floor(prog):
    g = Game(prog, ["-x", SEED], "12")
    g.keys(b" ", 9)
    check("game over" in g.text(), "the pile at the floor did not end the game")
    check(g.wait() == 0, "the game did not exit cleanly")


def test_frame_fits(prog):
    for args in ([SEED], ["-x", SEED]):
        g = Game(prog, args, "1")
        f = g.frame()
        check(len(f) <= 24, "frame taller than 24 rows: %d" % len(f))
        check(all(len(line) <= 53 for line in f), "a frame line is wider than 53 columns: %r" % f)
        check(
            all(len(line) <= 21 for line in f[2:15]), "the board is wider than 21 columns: %r" % f
        )
        g.finish()


def test_usage(prog):
    for args in (["-z"], ["-x", "1", "2"], ["--"]):
        g = Game(prog, args, None)
        check(g.wait() == 2, "bad arguments %r did not exit 2" % (args,))


CASES = [(n[5:], f) for n, f in sorted(globals().items()) if n.startswith("test_")]


def main():
    prog = sys.argv[1] if len(sys.argv) > 1 else "./bubble-host"
    want = sys.argv[2:]
    ran = 0
    for name, fn in CASES:
        if want and name not in want:
            continue
        fn(prog)
        ran += 1
    check(ran > 0, "no case ran")
    print("bubble: OK (%d cases, %d checks)" % (ran, checks))


if __name__ == "__main__":
    main()
