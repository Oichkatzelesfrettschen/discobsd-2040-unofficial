#!/usr/bin/env python3
# pty smoke tests of coil-host. Each case runs the game in a pty, feeds keys,
# and reads the result from a small 53x32 terminal model that applies the
# escape sequences the game writes (cursor position, erase, SGR, text), so
# the checks see the screen and not just a byte stream.
#
# GAMEBOX_TEST=<spawn>[/<shots>] stops the clock: a '.' key is one tick and
# the chain advances on every tick. The spawn string is the whole supply and
# the shot string repeats. Field cell (x, y) is terminal row 3 + y and
# column 3 + 2 x; the path starts at field cell (0, 0) and the launcher
# stands at (11, 8).
#
# usage: play.py [./coil-host [case ...]]
#   a case is a test function name without its test_ prefix; none runs all
import os
import pty
import re
import select
import signal
import sys
import time

ROWS, COLS = 32, 53
GLYPH = {1: "@@", 2: "##", 3: "OO", 4: "XX", 5: "%%"}
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


def cell_rc(x, y):
    return 3 + y, 3 + 2 * x


class Term:
    """Cursor, erase, SGR background and text; nothing else."""

    def __init__(self):
        self.grid = [[(" ", 0)] * (COLS + 2) for _ in range(ROWS + 1)]
        self.r = self.c = 1
        self.bg = 0
        self.max_row = 0
        self.max_col = 0
        self.rest = b""
        self.sgr = set()
        self.raw = b""

    def feed(self, data):
        self.raw += data
        data = self.rest + data
        self.rest = b""
        i, n = 0, len(data)
        while i < n:
            b = data[i]
            if b == 0x1B:
                m = re.match(rb"\x1b\[([0-9;]*)([A-Za-z])", data[i:])
                if not m:
                    if re.match(rb"\x1b(\[[0-9;]*)?$", data[i:]):
                        self.rest = data[i:]
                        return
                    i += 1
                    continue
                self.csi(m.group(1).decode(), m.group(2).decode())
                i += m.end()
                continue
            i += 1
            if b == 0x0D:
                self.c = 1
            elif b == 0x0A:
                self.r = min(self.r + 1, ROWS)
            elif 0x20 <= b < 0x7F:
                if self.c > COLS:
                    raise AssertionError("text written past column %d" % COLS)
                self.grid[self.r][self.c] = (chr(b), self.bg)
                self.max_row = max(self.max_row, self.r)
                self.max_col = max(self.max_col, self.c)
                self.c += 1

    def csi(self, params, final):
        p = [int(x) if x else 0 for x in params.split(";")] if params else []
        if final == "H":
            self.r = min(max(p[0] if p and p[0] else 1, 1), ROWS)
            self.c = min(max(p[1] if len(p) > 1 and p[1] else 1, 1), COLS + 1)
        elif final == "J" and p == [2]:
            self.grid = [[(" ", 0)] * (COLS + 2) for _ in range(ROWS + 1)]
        elif final == "K":
            for c in range(self.c, COLS + 1):
                self.grid[self.r][c] = (" ", 0)
        elif final == "m":
            for v in p or [0]:
                self.sgr.add(v)
                if v == 0:
                    self.bg = 0
                elif 40 <= v <= 47:
                    self.bg = v

    def text(self, row, c0, c1):
        return "".join(self.grid[row][c][0] for c in range(c0, c1 + 1))

    def line(self, row):
        return self.text(row, 1, COLS).rstrip()

    def cell(self, x, y):
        r, c = cell_rc(x, y)
        return self.text(r, c, c + 1)

    def cell_bg(self, x, y):
        r, c = cell_rc(x, y)
        return self.grid[r][c][1]


class Game:
    def __init__(self, prog, args=(), test=None):
        self.term = Term()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.pop("GAMEBOX_TEST", None)
            if test is not None:
                os.environ["GAMEBOX_TEST"] = test
            os.execvp(prog, [prog] + list(args))
        self.pull()

    def pull(self, timeout=0.4):
        end = time.time() + timeout
        got = False
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if self.fd in r:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                self.term.feed(chunk)
                got = True
            elif got:
                break

    def send(self, data, repeat=1, pause=0.0):
        for _ in range(repeat):
            os.write(self.fd, data)
            if pause:
                time.sleep(pause)
        self.pull()

    def tick(self, n=1):
        for _ in range(n):
            os.write(self.fd, b".")
            time.sleep(0.004)
        self.pull()

    def status(self):
        return self.term.line(1)

    def finish(self):
        try:
            os.write(self.fd, b"q")
        except OSError:
            pass
        self.pull(0.3)
        return self.wait()

    def wait(self):
        self.pull(0.3)
        _, st = os.waitpid(self.pid, 0)
        return os.waitstatus_to_exitcode(st)


def test_layout(prog):
    g = Game(prog, [], "1221/2")
    t = g.term
    check(
        t.line(1).startswith("COIL  score 0  level 1  left 4  next "), "status line: %r" % t.line(1)
    )
    check(t.text(2, 2, 2 + 2 * 23 + 1) == "." + "-" * 46 + ".", "top border: %r" % t.line(2))
    check(t.text(20, 2, 2 + 2 * 23 + 1) == "'" + "-" * 46 + "'", "bottom border: %r" % t.line(20))
    for y in range(17):
        r, _ = cell_rc(0, y)
        check(t.text(r, 2, 2) == "|" and t.text(r, 49, 49) == "|", "wall on row %d" % r)
    check(t.cell(0, 0) == ". " and t.cell(22, 0) == ". ", "path cells on the top row")
    check(t.cell(0, 1) == "  ", "the lane under the top row is blank, got %r" % t.cell(0, 1))
    check(t.cell(17, 4) == "()", "the hole at the path end: %r" % t.cell(17, 4))
    check(t.cell(11, 8) == GLYPH[2], "launcher shows the loaded color: %r" % t.cell(11, 8))
    for y in range(7, -1, -1):
        check(t.cell(11, y)[0] == ":", "aim line cell (11, %d): %r" % (y, t.cell(11, y)))
    check(t.max_row <= 24, "wrote below row 24: %d" % t.max_row)
    check(t.max_col <= 53, "wrote past column 53")
    check(t.line(22).startswith("h/l turn"), "help line: %r" % t.line(22))
    check(g.finish() == 0, "the game did not exit cleanly")


def test_spawn(prog):
    g = Game(prog, [], "123/1")
    t = g.term
    g.tick()
    check(t.cell(0, 0) == GLYPH[1], "first ball at path cell 0: %r" % t.cell(0, 0))
    check("left 3" in g.status(), "left counts the supply and the chain")
    g.tick()
    check(t.cell(1, 0) == GLYPH[1] and t.cell(0, 0) == GLYPH[2], "chain after two ticks")
    g.tick()
    check(
        [t.cell(x, 0) for x in range(3)] == [GLYPH[3], GLYPH[2], GLYPH[1]],
        "chain after three ticks",
    )
    g.tick(2)
    check(
        [t.cell(x, 0) for x in range(2, 5)] == [GLYPH[3], GLYPH[2], GLYPH[1]],
        "chain after five ticks",
    )
    check(t.cell(0, 0) == ". ", "the vacated path cell shows the path again")
    check(g.finish() == 0, "the game did not exit cleanly")


def test_color_and_mono(prog):
    g = Game(prog, [], "12/1")
    g.tick()
    r, c = cell_rc(0, 0)
    check(g.term.cell_bg(0, 0) == 41, "color mode: background %d" % g.term.cell_bg(0, 0))
    check(g.finish() == 0, "exit")
    g = Game(prog, ["-m"], "12/1")
    g.tick()
    check(
        g.term.cell_bg(0, 0) == 0 and not (g.term.sgr & set(range(40, 48))),
        "monochrome mode set a background",
    )
    check(g.term.cell(0, 0) == GLYPH[1], "monochrome glyph pair")
    check(g.finish() == 0, "exit")


def test_turn(prog):
    g = Game(prog, [], "123/1")
    t = g.term
    g.send(b"\x1b[C")
    check(
        t.cell(11, 7)[0] == ":" and t.cell(12, 6)[0] == ":" and t.cell(14, 0)[0] == ":",
        "direction 1 aim line",
    )
    check(t.cell(11, 0) == ". ", "the old aim line is erased: %r" % t.cell(11, 0))
    g.send(b"\x1b[D", 2)
    check(t.cell(10, 6)[0] == ":" and t.cell(8, 0)[0] == ":", "direction 15 aim line")
    g.send(b"l")
    g.send(b"j")
    check(t.cell(11, 9)[0] == ":" and t.cell(11, 16)[0] == ":", "reversed aim line")
    g.send(b"l", 8)
    check(t.cell(11, 7)[0] == ":", "direction 0 again after eight steps")
    check(g.finish() == 0, "the game did not exit cleanly")


def run_to(g, ticks):
    g.tick(ticks)


def test_clear_and_reaction(prog):
    # Spawn 1,1,2,2,1; after 14 ticks the chain is 13:1 12:1 11:2 10:2 9:1.
    g = Game(prog, [], "11221/2")
    t = g.term
    run_to(g, 14)
    check(
        [t.cell(x, 0) for x in range(9, 14)] == [GLYPH[1], GLYPH[2], GLYPH[2], GLYPH[1], GLYPH[1]],
        "chain before the shot",
    )
    g.send(b" ")
    check(t.cell(14, 0) == GLYPH[1] and t.cell(13, 0) == GLYPH[1], "front pair pushed ahead")
    check(
        t.cell(10, 0) == ". " and t.cell(12, 0) == ". " and t.cell(11, 0) == ": ",
        "the three twos vanished (cell 11 shows the aim line now): %r" % t.line(3),
    )
    check("score 30" in g.status(), "the clear scored 30: %r" % g.status())
    g.tick()
    check(t.cell(10, 0) == GLYPH[1], "the rear part rolls forward one cell a tick")
    g.tick(2)
    check("score 90" in g.status(), "the rejoin cleared the ones with combo 2: %r" % g.status())
    check(t.line(23).startswith("LEVEL CLEAR"), "level clear message: %r" % t.line(23))
    check("left 0" in g.status(), "nothing left")
    g.send(b"\r")
    check(
        "level 2" in g.status() and "score 90" in g.status(),
        "Enter starts level 2: %r" % g.status(),
    )
    check(t.line(23) == "", "the message is erased")
    check(g.finish() == 0, "the game did not exit cleanly")


def test_miss(prog):
    g = Game(prog, [], "1/12")
    t = g.term
    g.tick()
    g.send(b"\x1b[C", 4)
    before = t.cell(0, 0)
    check(g.status().endswith("next ##"), "the next preview at the start: %r" % g.status())
    g.send(b" ")
    # A miss changes neither the score, the count left nor the level, only the
    # color queued after the one now loaded; the preview must follow it.
    check(g.status().endswith("next @@"), "the next preview after a miss: %r" % g.status())
    check(
        t.cell(0, 0) == before and "left 1" in g.status(), "a shot into the open changed the chain"
    )
    check(t.cell(11, 8) == GLYPH[2], "the launcher reloads after a miss: %r" % t.cell(11, 8))
    g.tick(3)
    check(g.status().endswith("next @@"), "the next preview after ticks: %r" % g.status())
    check(g.finish() == 0, "the game did not exit cleanly")


def test_loss_and_restart(prog):
    g = Game(prog, [], "1/1")
    t = g.term
    g.tick(149)
    check(t.cell(17, 4) == "()", "the hole is still free after 149 ticks")
    check(t.line(23) == "", "no message while playing")
    g.tick()
    check(t.cell(17, 4) == GLYPH[1], "the ball fills the hole: %r" % t.cell(17, 4))
    check(t.line(23).startswith("GAME OVER"), "game over message: %r" % t.line(23))
    g.send(b"x.")
    check(t.line(23).startswith("GAME OVER"), "keys other than Enter keep the screen")
    g.send(b"\r")
    check(
        "left 1" in g.status() and "level 1" in g.status() and "score 0" in g.status(),
        "restart: %r" % g.status(),
    )
    check(t.cell(17, 4) == "()", "the new game starts with an empty path")
    check(g.finish() == 0, "the game did not exit cleanly")


def test_restart_resets_score(prog):
    # Spawn 1,2,2; after 13 ticks the chain is 12:1 11:2 10:2, and a color-2
    # shot makes three twos. The lone one then crawls to the hole.
    g = Game(prog, [], "122/2")
    t = g.term
    g.tick(13)
    g.send(b" ")
    check("score 30" in g.status(), "the clear scored 30: %r" % g.status())
    check(t.cell(13, 0) == GLYPH[1] and "left 1" in g.status(), "one ball is left")
    g.tick(135)
    check(t.line(23) == "", "still playing one tick before the end")
    g.tick()
    check(
        t.line(23).startswith("GAME OVER") and "score 30" in g.status(),
        "lost with the score kept on screen",
    )
    g.send(b" ")
    check(
        "score 0" in g.status() and "level 1" in g.status(),
        "a lost game restarts at zero: %r" % g.status(),
    )
    check(g.finish() == 0, "the game did not exit cleanly")


def test_pause(prog):
    g = Game(prog, [], "12/1")
    t = g.term
    g.send(b"\x1b")
    time.sleep(0.15)
    g.pull()
    check(t.line(23).startswith("PAUSED"), "bare Esc pauses: %r" % t.line(23))
    g.send(b"\x1b[C")
    check(t.cell(12, 6)[0] != ":", "keys are ignored while paused")
    g.send(b"p")
    check(t.line(23) == "", "p resumes")
    g.send(b"\x1bOP")
    check(t.line(23).startswith("PAUSED"), "F1 pauses")
    g.send(b"p")
    check(g.finish() == 0, "the game did not exit cleanly")


def test_clock(prog):
    # Without GAMEBOX_TEST the chain advances by itself: level 9 enters a
    # ball every 100 ms.
    g = Game(prog, ["-m", "9", "5"])
    t = g.term
    time.sleep(0.9)
    g.pull(0.3)
    balls = sum(1 for x in range(23) if t.cell(x, 0) in GLYPH.values())
    check(balls >= 4, "after a second only %d balls entered" % balls)
    # The ball count has only a lower bound, because a loaded host can
    # oversleep until level 1's 450 ms rate also shows four balls; the status
    # line names the level the argument selected.
    check(" level 9 " in g.status(), "status line: %r" % g.status())
    check(g.finish() == 0, "the game did not exit cleanly")


def test_signals(prog):
    for sig, code in ((signal.SIGTERM, 143), (signal.SIGHUP, 129)):
        g = Game(prog, [], "12/1")
        os.kill(g.pid, sig)
        check(g.wait() == code, "signal %d did not end the game with %d" % (sig, code))
        check(
            g.term.raw.endswith(b"\x1b[0m\x1b[2J\x1b[H"),
            "the screen is not cleared after signal %d" % sig,
        )


def test_exit_restores(prog):
    g = Game(prog, [], "12/1")
    g.finish()
    check(g.term.raw.endswith(b"\x1b[0m\x1b[2J\x1b[H"), "q does not clear the screen")


def test_usage(prog):
    for args in (["-z"], ["1", "2", "3"], ["-m", "-m"]):
        g = Game(prog, args)
        check(g.wait() == 2, "arguments %r did not exit 2" % (args,))


CASES = [(n[5:], f) for n, f in sorted(globals().items()) if n.startswith("test_")]


def main():
    prog = sys.argv[1] if len(sys.argv) > 1 else "./coil-host"
    want = sys.argv[2:]
    ran = 0
    for name, fn in CASES:
        if want and name not in want:
            continue
        fn(prog)
        ran += 1
    check(ran > 0, "no case ran")
    print("coil: OK (%d cases, %d checks)" % (ran, checks))


if __name__ == "__main__":
    main()
