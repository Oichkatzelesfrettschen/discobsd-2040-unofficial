#!/usr/bin/env python3
# pty smoke tests of the host builds of flood, samegame, unruly and lightup.
# Each case runs a puzzle in a pty, feeds keys, and reads the screen from a
# small 53x32 terminal model that applies the escape sequences the game
# writes (cursor position, erase, SGR attributes and colors, text), so the
# checks see the screen and not just a byte stream.
#
# GAMEBOX_TEST fixes the seed and makes the front end print the game ID and
# the moves that solve it ("TEST id ..." and "TEST moves ..." lines below the
# help); the key '!' then plays one printed move. Board cell (x, y) is
# terminal row 3 + y and columns 3 + 2 x and 4 + 2 x.
#
# usage: play.py GAMES-DIR [case ...]
#   GAMES-DIR holds <name>/<name>-host for the four puzzles; a case is a
#   test function name without its test_ prefix; none runs all

import os
import pty
import re
import select
import signal
import sys
import time

ROWS, COLS = 32, 53
PUZZLES = tuple(os.environ.get("PZ_ONLY", "flood samegame unruly lightup").split())
SOLVABLE = tuple(p for p in ("flood", "unruly", "lightup") if p in PUZZLES)
checks = 0
BOLD, DIM, REV = 1, 2, 4


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


class Term:
    """Cursor, erase, SGR and text; nothing else."""

    def __init__(self):
        self.reset_grid()
        self.r = self.c = 1
        self.fg = self.bg = self.attr = 0
        self.max_row = self.max_col = 0
        self.rest = b""
        self.raw = b""
        self.sgr = set()

    def reset_grid(self):
        self.grid = [[(" ", 0, 0, 0)] * (COLS + 2) for _ in range(ROWS + 1)]

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
                self.grid[self.r][self.c] = (chr(b), self.fg, self.bg, self.attr)
                self.max_row = max(self.max_row, self.r)
                self.max_col = max(self.max_col, self.c)
                self.c += 1

    def csi(self, params, final):
        p = [int(x) if x else 0 for x in params.split(";")] if params else []
        if final == "H":
            self.r = min(max(p[0] if p and p[0] else 1, 1), ROWS)
            self.c = min(max(p[1] if len(p) > 1 and p[1] else 1, 1), COLS + 1)
        elif final == "J" and p == [2]:
            self.reset_grid()
        elif final == "K":
            for c in range(self.c, COLS + 1):
                self.grid[self.r][c] = (" ", 0, 0, 0)
        elif final == "m":
            for v in p or [0]:
                self.sgr.add(v)
                if v == 0:
                    self.fg = self.bg = self.attr = 0
                elif v == 1:
                    self.attr |= BOLD
                elif v == 2:
                    self.attr |= DIM
                elif v == 7:
                    self.attr |= REV
                elif 30 <= v <= 37:
                    self.fg = v - 29
                elif 40 <= v <= 47:
                    self.bg = v - 39

    def line(self, row):
        return "".join(self.grid[row][c][0] for c in range(1, COLS + 1)).rstrip()

    def cell(self, x, y):
        r, c = 3 + y, 3 + 2 * x
        return self.grid[r][c][0] + self.grid[r][c + 1][0]

    def style(self, x, y):
        r, c = 3 + y, 3 + 2 * x
        _, fg, bg, attr = self.grid[r][c]
        return fg, bg, attr

    def board(self, w, h):
        return [[self.cell(x, y) for x in range(w)] for y in range(h)]

    def test_lines(self):
        ids = ["".join(ln[8:] for ln in (self.line(r) for r in range(1, ROWS + 1))
                       if ln.startswith("TEST id "))]
        mv = "".join(ln[11:] for ln in (self.line(r) for r in range(1, ROWS + 1))
                     if ln.startswith("TEST moves "))
        return (ids[0] or None), [m for m in mv.split(";") if m]


class Game:
    def __init__(self, root, name, args=(), test=None):
        self.name = name
        self.term = Term()
        prog = os.path.join(root, name, name + "-host")
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.pop("GAMEBOX_TEST", None)
            if test is not None:
                os.environ["GAMEBOX_TEST"] = test
            os.execv(prog, [prog] + list(args))
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

    def send(self, data, repeat=1):
        for _ in range(repeat):
            os.write(self.fd, data)
            time.sleep(0.004)
        self.pull()

    def move(self, mv):
        """Play one move string through the test hook."""
        self.send(b"!" + mv.encode() + b"\r")

    def status(self):
        return self.term.line(1)

    def message(self, h):
        return self.term.line(3 + h + 2)

    def finish(self):
        try:
            os.write(self.fd, b"q")
        except OSError:
            pass
        return self.wait()

    def wait(self):
        """Exit status; a game that has not exited after five seconds is
        killed and reported, so a missing exit cannot hang the test."""
        self.pull(0.3)
        end = time.time() + 5
        while time.time() < end:
            pid, st = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                return os.waitstatus_to_exitcode(st)
            self.pull(0.1)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        raise AssertionError("%s did not exit" % self.name)


def size_of(game_id):
    """Board size from a test game ID's parameter text."""
    m = re.match(r"(\d+)x(\d+)", game_id)
    return int(m.group(1)), int(m.group(2))


def start(root, name, test="1", args=()):
    g = Game(root, name, args, test)
    gid, moves = g.term.test_lines()
    check(gid is not None, "%s: no game ID printed" % name)
    w, h = size_of(gid)
    return g, gid, moves, w, h


def move_cursor(g, x0, y0, x, y):
    for _ in range(abs(x - x0)):
        g.send(b"l" if x > x0 else b"h")
    for _ in range(abs(y - y0)):
        g.send(b"j" if y > y0 else b"k")


def cursor_at(g, w, h):
    return [(x, y) for y in range(h) for x in range(w) if g.term.style(x, y)[2] & REV]


def test_layout(root):
    for name in PUZZLES:
        g, gid, moves, w, h = start(root, name)
        t = g.term
        check(t.line(1).startswith(name.upper()), "%s: title line %r" % (name, t.line(1)))
        check(t.line(2) == " ." + "-" * (2 * w) + ".", "%s: top border %r" % (name, t.line(2)))
        check(t.line(3 + h) == " '" + "-" * (2 * w) + "'", "%s: bottom border" % name)
        for y in range(h):
            row = t.line(3 + y)
            check(row[1] == "|" and row[2 + 2 * w] == "|", "%s: walls on row %d: %r" % (name, y, row))
        check("arrows move" in t.line(3 + h + 3), "%s: help line" % name)
        check("q quit" in t.line(3 + h + 5), "%s: quit help: %r" % (name, t.line(3 + h + 5)))
        check(len(cursor_at(g, w, h)) == 1, "%s: expected one cursor cell" % name)
        if name in SOLVABLE:
            check(len(moves) > 0, "%s: no solution moves printed" % name)
        check(g.finish() == 0, "%s: exit" % name)


def test_fits_24_rows(root):
    for name in PUZZLES:
        g = Game(root, name, ["-s", "fit"])
        check(g.term.max_row <= 24, "%s: wrote on row %d" % (name, g.term.max_row))
        check(g.term.max_col <= 52, "%s: wrote on column %d" % (name, g.term.max_col))
        check(g.finish() == 0, "%s: exit" % name)


def test_largest_boards(root):
    cases = (("flood", "24x16c8m0"), ("samegame", "24x16c8s2"), ("unruly", "24x16"),
             ("lightup", "24x8"))
    for name, spec in cases:
        if name not in PUZZLES:
            continue
        g = Game(root, name, ["-s", "big", spec])
        check(g.term.max_row <= 24 and g.term.max_col <= 52,
              "%s: %s drew to row %d column %d" % (name, spec, g.term.max_row, g.term.max_col))
        check(g.term.line(2).startswith(" .-") and len(g.term.line(2)) == 51,
              "%s: top border of %s: %r" % (name, spec, g.term.line(2)))
        check(g.finish() == 0, "%s: exit" % name)


def test_too_big(root):
    for name, spec in (("flood", "25x5"), ("samegame", "5x17"), ("unruly", "26x6"), ("lightup", "10x17")):
        if name not in PUZZLES:
            continue
        g = Game(root, name, [spec])
        check(g.wait() == 2, "%s: %s did not exit 2" % (name, spec))
        out = g.term.raw.decode(errors="replace")
        check("does not fit" in out, "%s: no size message: %r" % (name, out))


def test_solution_moves(root):
    for name in SOLVABLE:
        g, gid, moves, w, h = start(root, name)
        for mv in moves:
            g.move(mv)
        check(g.message(h).startswith("SOLVED"), "%s: not solved: %r" % (name, g.message(h)))
        check(g.finish() == 0, "%s: exit" % name)


def test_undo_redo(root):
    for name in SOLVABLE:
        g, gid, moves, w, h = start(root, name)
        t = g.term
        before = t.board(w, h)
        g.move(moves[0])
        after = t.board(w, h)
        check(after != before, "%s: the first solution move changed nothing" % name)
        g.send(b"u")
        check(t.board(w, h) == before, "%s: undo did not restore the board" % name)
        g.send(b"u")
        check(t.board(w, h) == before, "%s: undo past the start changed the board" % name)
        g.send(b"y")
        check(t.board(w, h) == after, "%s: redo did not repeat the move" % name)
        g.send(b"y")
        check(t.board(w, h) == after, "%s: redo past the end changed the board" % name)
        check(g.finish() == 0, "%s: exit" % name)


def test_new_game(root):
    for name in PUZZLES:
        g, gid, moves, w, h = start(root, name)
        first = g.term.board(w, h)
        g.send(b"n")
        gid2, _ = g.term.test_lines()
        check(gid2 is not None and gid2 != gid, "%s: n kept the game ID %s" % (name, gid))
        check(g.term.board(w, h) != first, "%s: n kept the board" % name)
        check(g.finish() == 0, "%s: exit" % name)


def test_solve_key(root):
    for name in SOLVABLE:
        g, gid, moves, w, h = start(root, name)
        g.send(b"S")
        if name == "flood":
            check(g.message(h) == "", "flood: S alone does not play: %r" % g.message(h))
            for _ in range(len(moves)):
                g.send(b"m")
        check(g.message(h).startswith("SOLVED"), "%s: S did not solve: %r" % (name, g.message(h)))
        g.send(b"u")
        check(g.message(h) == "" or name == "flood", "%s: undo leaves SOLVED showing" % name)
        check(g.finish() == 0, "%s: exit" % name)
    if "samegame" in PUZZLES:
        g, gid, moves, w, h = start(root, "samegame")
        before = g.term.board(w, h)
        g.send(b"S")
        check(g.term.board(w, h) == before, "samegame: S changed the board")
        check(g.finish() == 0, "samegame: exit")


def test_flood_fixture(root):
    if "flood" not in PUZZLES:
        return
    # Colors 0 1 0 / 1 1 2 / 2 0 0 with three moves allowed: 1, 0, 2 wins.
    g = Game(root, "flood", [], "3x3c3m0:010112200,3")
    t = g.term
    check(t.line(1) == "FLOOD  moves 0/3", "status: %r" % t.line(1))
    check(t.board(3, 3) == [["11", "22", "11"], ["22", "22", "33"], ["33", "11", "11"]],
          "board: %r" % t.board(3, 3))
    fg, bg, attr = t.style(0, 0)
    check(bg == 2 and attr & BOLD and attr & REV, "region cell style %r" % ((fg, bg, attr),))
    check(t.style(2, 0)[2] & BOLD == 0, "cell outside the region is bold")
    g.send(b"2")
    check(t.line(1) == "FLOOD  moves 1/3", "digit key fill: %r" % t.line(1))
    check(t.board(3, 3)[0] == ["22", "22", "11"], "after the first fill: %r" % t.board(3, 3))
    g.send(b"2")
    check(t.line(1) == "FLOOD  moves 1/3", "refilling the same color spent a move")
    g.send(b"1")
    g.send(b"3")
    check(g.message(3).startswith("SOLVED"), "message %r" % g.message(3))
    check(t.line(1) == "FLOOD  moves 3/3", "status %r" % t.line(1))
    g.send(b"2")
    check(t.line(1) == "FLOOD  moves 3/3", "a move after the win was accepted")
    g.send(b"uu")
    check(t.line(1) == "FLOOD  moves 1/3", "undo twice: %r" % t.line(1))
    # Spend all three moves without finishing: 0 then 1 leaves two cells of 2.
    g.send(b"1")
    g.send(b"2")
    check(g.message(3).startswith("OUT OF MOVES"), "message %r" % g.message(3))
    g.send(b"u")
    check(g.message(3) == "", "undo leaves the out-of-moves message")
    # Enter fills with the color under the cursor.
    g.send(b"l")
    g.send(b"\r")
    check(t.line(1).startswith("FLOOD  moves "), "status after Enter")
    check(g.finish() == 0, "exit")


def test_samegame_fixture(root):
    if "samegame" not in PUZZLES:
        return
    # Three rows of one color each: three groups of three.
    g = Game(root, "samegame", [], "3x3c3s2:1,1,1,2,2,2,3,3,3")
    t = g.term
    check(t.line(1) == "SAMEGAME  score 0", "status: %r" % t.line(1))
    check(t.board(3, 3) == [["11"] * 3, ["22"] * 3, ["33"] * 3], "board: %r" % t.board(3, 3))
    g.send(b"\r")
    check(t.board(3, 3)[0] == ["()"] * 3, "selected group: %r" % t.board(3, 3)[0])
    check(t.board(3, 3)[1] == ["22"] * 3, "other rows change on select")
    check(t.line(1) == "SAMEGAME  score 0  group 3 (+1)", "group status: %r" % t.line(1))
    g.send(b"m")
    check(t.board(3, 3)[0] == ["11"] * 3 and t.line(1) == "SAMEGAME  score 0", "m clears the selection")
    g.send(b"\r")
    g.send(b"c")
    check(t.board(3, 3)[0] == ["11"] * 3, "c clears the selection")
    g.send(b"\r\r")
    check(t.board(3, 3)[0] == ["  "] * 3 and t.line(1) == "SAMEGAME  score 1", "removal: %r" % t.line(1))
    g.send(b"j\r\r")
    check(t.board(3, 3)[1] == ["  "] * 3 and t.line(1) == "SAMEGAME  score 2", "second removal: %r" % t.line(1))
    g.send(b"u")
    check(t.board(3, 3)[1] == ["22"] * 3 and t.line(1) == "SAMEGAME  score 1", "undo: %r" % t.line(1))
    g.send(b"y")
    g.send(b"j\r\r")
    check(g.message(3).startswith("SOLVED"), "message %r" % g.message(3))
    check(t.line(1) == "SAMEGAME  score 3", "final score %r" % t.line(1))
    check(g.finish() == 0, "exit")
    # Removing the pair leaves no two equal colors touching.
    g = Game(root, "samegame", [], "3x2c3s2:1,1,2,3,2,3")
    g.send(b"\r\r")
    check(g.term.line(3 + 2 + 2).startswith("NO MOVES LEFT"), "no moves message: %r" % g.term.line(7))
    check(g.term.board(3, 2) == [["  ", "  ", "22"], ["33", "22", "33"]], "board %r" % g.term.board(3, 2))
    check(g.finish() == 0, "exit")


def test_unruly_keys(root):
    if "unruly" not in PUZZLES:
        return
    g, gid, moves, w, h = start(root, "unruly")
    t = g.term
    mv = next(m for m in moves if m.startswith("P"))
    c, x, y = re.match(r"P(.),(\d+),(\d+)", mv).groups()
    x, y = int(x), int(y)
    cur = cursor_at(g, w, h)[0]
    move_cursor(g, cur[0], cur[1], x, y)
    check(cursor_at(g, w, h) == [(x, y)], "cursor moved to %r" % (cursor_at(g, w, h),))
    g.send(c.encode())
    check(t.cell(x, y) == c * 2, "digit %s not set: %r" % (c, t.cell(x, y)))
    other = "0" if c == "1" else "1"
    g.send(other.encode())
    check(t.cell(x, y) == other * 2, "digit %s not set over %s" % (other, c))
    g.send(b"x")
    check(t.cell(x, y) == ". ", "x did not clear: %r" % t.cell(x, y))
    g.send(b" ")
    check(t.cell(x, y) == "11", "space did not set one: %r" % t.cell(x, y))
    g.send(b" ")
    check(t.cell(x, y) == "00", "space did not cycle to zero: %r" % t.cell(x, y))
    g.send(b" ")
    check(t.cell(x, y) == ". ", "space did not cycle to empty")
    # A clue cannot be changed: find one.
    clue = [(cx, cy) for cy in range(h) for cx in range(w) if t.style(cx, cy)[2] & BOLD and t.cell(cx, cy) != ". "]
    check(clue, "no clue cells shown")
    cx, cy = clue[0]
    cur = cursor_at(g, w, h)[0]
    move_cursor(g, cur[0], cur[1], cx, cy)
    was = t.cell(cx, cy)
    g.send(b" ")
    check(t.cell(cx, cy) == was, "a clue changed")
    check(g.finish() == 0, "exit")


def test_lightup_keys(root):
    if "lightup" not in PUZZLES:
        return
    g, gid, moves, w, h = start(root, "lightup")
    t = g.term
    mv = next(m for m in moves if m.startswith("L"))
    x, y = (int(v) for v in mv[1:].split(","))
    cur = cursor_at(g, w, h)[0]
    move_cursor(g, cur[0], cur[1], x, y)
    g.send(b"\r")
    check(t.cell(x, y) == "<>", "lamp not placed: %r" % t.cell(x, y))
    check("lamps 1" in t.line(1), "status %r" % t.line(1))
    lit = [(cx, cy) for cy in range(h) for cx in range(w) if t.style(cx, cy)[1] == 4]
    check(len(lit) >= 1, "no lit cells")
    g.send(b"\r")
    check(t.cell(x, y) != "<>" and "lamps 0" in t.line(1), "lamp not removed")
    g.send(b"m")
    check(t.cell(x, y) == "x ", "mark not set: %r" % t.cell(x, y))
    g.send(b"\r")
    check(t.cell(x, y) == "x ", "a lamp went onto a marked cell")
    g.send(b"m")
    check(t.cell(x, y) != "x ", "mark not cleared")
    blacks = [(cx, cy) for cy in range(h) for cx in range(w) if t.style(cx, cy)[1] == 1]
    check(blacks, "no black cells")
    cx, cy = blacks[0]
    cur = cursor_at(g, w, h)[0]
    move_cursor(g, cur[0], cur[1], cx, cy)
    was = t.cell(cx, cy)
    g.send(b"\r")
    check(t.cell(cx, cy) == was, "a lamp went onto a black cell")
    check(g.finish() == 0, "exit")


def test_monochrome(root):
    for name in PUZZLES:
        g = Game(root, name, ["-m", "-s", "mono"])
        used = {v for v in g.term.sgr if 30 <= v <= 47}
        check(not used, "%s: -m used colors %r" % (name, sorted(used)))
        check(g.finish() == 0, "%s: exit" % name)
        g = Game(root, name, ["-s", "mono"])
        check(any(30 <= v <= 47 for v in g.term.sgr), "%s: no colors without -m" % name)
        check(g.finish() == 0, "%s: exit" % name)


def test_seed_repeats(root):
    for name in PUZZLES:
        boards = []
        for seed in ("alpha", "alpha", "beta"):
            g = Game(root, name, ["-s", seed])
            boards.append([g.term.line(r) for r in range(3, 3 + 12)])
            g.finish()
        check(boards[0] == boards[1], "%s: the same seed gave two boards" % name)
        check(boards[0] != boards[2], "%s: two seeds gave one board" % name)


def test_bad_args(root):
    for name in PUZZLES:
        for args in (["-z"], ["-s"], ["a", "b"], ["-m", "-m", "-q"]):
            g = Game(root, name, args)
            check(g.wait() == 2, "%s: %r did not exit 2" % (name, args))
        g = Game(root, name, ["3x3:xyz"])
        out = g.term.raw.decode(errors="replace")
        check(g.wait() == 2, "%s: a bad game ID did not exit 2" % name)
        check(out.startswith(name + ": "), "%s: no complaint: %r" % (name, out))


def test_signals(root):
    for name in PUZZLES:
        for sig, code in ((signal.SIGTERM, 143), (signal.SIGHUP, 129)):
            g = Game(root, name, ["-s", "sig"])
            os.kill(g.pid, sig)
            check(g.wait() == code, "%s: signal %d exit" % (name, sig))
            check(g.term.raw.endswith(b"\x1b[0m\x1b[2J\x1b[H"),
                  "%s: the screen is not cleared after signal %d" % (name, sig))


def test_exit_restores(root):
    for name in PUZZLES:
        g = Game(root, name, ["-s", "exit"])
        g.send(b"q")
        check(g.wait() == 0, "%s: exit" % name)
        check(g.term.raw.endswith(b"\x1b[0m\x1b[2J\x1b[H"), "%s: q does not clear the screen" % name)


def test_escape_keys(root):
    if "lightup" not in PUZZLES:
        return
    g, gid, moves, w, h = start(root, "lightup")
    cur = cursor_at(g, w, h)[0]
    g.send(b"\x1b[C")
    check(cursor_at(g, w, h) == [(min(cur[0] + 1, w - 1), cur[1])], "right arrow")
    g.send(b"\x1b[B")
    check(cursor_at(g, w, h) == [(min(cur[0] + 1, w - 1), min(cur[1] + 1, h - 1))], "down arrow")
    g.send(b"\x1b[D")
    g.send(b"\x1b[A")
    check(cursor_at(g, w, h) == [cur], "left and up arrows")
    g.send(b"\x1bOP")
    check(cursor_at(g, w, h) == [cur], "F1 moved the cursor")
    g.send(b"\x1b")
    time.sleep(0.15)
    g.pull()
    check(cursor_at(g, w, h) == [cur], "a bare Esc moved the cursor")
    check(g.finish() == 0, "exit")


CASES = [(n[5:], f) for n, f in sorted(globals().items()) if n.startswith("test_")]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else ".."
    want = sys.argv[2:]
    ran = 0
    for name, fn in CASES:
        if want and name not in want:
            continue
        fn(root)
        ran += 1
    check(ran > 0, "no case ran")
    print("puzzlebox: OK (%d cases, %d checks)" % (ran, checks))


if __name__ == "__main__":
    main()
