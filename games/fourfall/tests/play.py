#!/usr/bin/env python3
# pty smoke tests of fourfall-host. Each case runs the game in a pty, feeds
# keys, and reads the result from a small 53x32 terminal model that applies
# the escape sequences the game writes (cursor position, erase, SGR, text),
# so the checks see the screen and not just a byte stream. GAMEBOX_TEST=<letters>
# turns gravity and the lock timer off and fixes the piece queue; the timed
# cases run without it. The well is 12 columns by 22 visible rows; terminal
# rows 2..23 and columns 3..26 hold its cells, two columns per cell.
#
# usage: play.py [./fourfall-host [case ...]]
#   a case is a test function name without its test_ prefix; none runs all

import fcntl
import os
import re
import select
import signal
import struct
import sys
import termios
import time

ROWS, COLS = 32, 53
ESC = b"\x1b"
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


BLANK = (" ", 0, 0, False)

# glyph texture and -c foreground color of each type, in IOTSZJL order
GLYPHS = {"I": "[]", "O": "##", "T": "<>", "S": "//", "Z": "\\\\", "J": "()", "L": "{}"}
HUES = {"I": 37, "O": 34, "T": 32, "S": 35, "Z": 36, "J": 31, "L": 33}
BOARD_ROWS = range(2, 24)


class Term:
    """Cursor, erase, SGR background/foreground/bold and text; nothing else."""

    def __init__(self):
        self.grid = [[BLANK] * (COLS + 1) for _ in range(ROWS + 1)]
        self.r = self.c = 1
        self.bg = self.fg = 0
        self.bold = False
        self.sgr_seen = set()
        self.chunks = []
        self.max_col_written = 0
        self.row32_written = False
        self.scrolled = False
        self.raw = b""
        self.rest = b""

    def feed(self, data, now=0.0):
        self.chunks.append((now, data))
        self.raw += data
        data = self.rest + data
        self.rest = b""
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b == 0x1B:
                m = re.match(rb"\x1b\[([0-9;]*)([A-Za-z])", data[i:])
                if not m:
                    if i + 1 >= n or re.match(rb"\x1b\[[0-9;]*$", data[i:]):
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
                if self.r >= ROWS:
                    self.scrolled = True
                else:
                    self.r += 1
            elif 0x20 <= b < 0x7F:
                if self.r == ROWS:
                    self.row32_written = True
                if self.c > COLS:
                    raise AssertionError("text written past column %d" % COLS)
                self.grid[self.r][self.c] = (chr(b), self.bg, self.fg, self.bold)
                self.max_col_written = max(self.max_col_written, self.c)
                self.c += 1

    def csi(self, params, final):
        p = [int(x) if x else 0 for x in params.split(";")] if params else []
        if final == "H":
            self.r = min(max(p[0] if p and p[0] else 1, 1), ROWS)
            self.c = min(max(p[1] if len(p) > 1 and p[1] else 1, 1), COLS + 1)
        elif final == "J" and p == [2]:
            self.grid = [[BLANK] * (COLS + 1) for _ in range(ROWS + 1)]
        elif final == "K":
            for c in range(self.c, COLS + 1):
                self.grid[self.r][c] = BLANK
        elif final == "m":
            for v in p or [0]:
                self.sgr_seen.add(v)
                if v == 0:
                    self.bg = self.fg = 0
                    self.bold = False
                elif v == 1:
                    self.bold = True
                elif 40 <= v <= 47:
                    self.bg = v
                elif 30 <= v <= 37 or 90 <= v <= 97:
                    self.fg = v

    def text(self, row, c0, c1):
        return "".join(self.grid[row][c][0] for c in range(c0, c1 + 1))

    def bgs(self, row, c0, c1):
        return {self.grid[row][c][1] for c in range(c0, c1 + 1)}

    def line(self, row):
        return self.text(row, 1, COLS).rstrip()

    def lit(self):
        """Terminal cells in the board area that hold a visible character."""
        return [(r, c) for r in BOARD_ROWS for c in range(3, 27) if self.grid[r][c][0] != " "]

    def fgs(self, row, c0, c1):
        return {self.grid[row][c][2] for c in range(c0, c1 + 1)}

    def bolds(self, row, c0, c1):
        return {self.grid[row][c][3] for c in range(c0, c1 + 1)}


def run_in_own_group(prog, args, info_w):
    """Runs in the pty child: fork the game into a new foreground process
    group, report its pid on info_w, wait for it and exit with its status.
    The waiting parent sits in the session's first group, so the game's
    group has a parent outside it and is not orphaned."""
    go_r, go_w = os.pipe()
    gp = os.fork()
    if gp == 0:
        os.close(go_w)
        os.setpgid(0, 0)
        os.read(go_r, 1)  # until the group owns the terminal
        os.close(go_r)
        return
    os.setpgid(gp, gp)
    os.tcsetpgrp(0, gp)
    os.write(info_w, str(gp).encode())
    os.write(go_w, b"g")
    status = 0
    while True:
        _, st = os.waitpid(gp, os.WUNTRACED)
        if os.WIFSTOPPED(st):
            continue
        status = os.waitstatus_to_exitcode(st)
        break
    os._exit(status if status >= 0 else 128 - status)


def proc_state(pid):
    """The scheduler state letter of pid from /proc, or None."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().rsplit(")", 1)[1].split()[0]
    except (OSError, IndexError):
        return None


def wait_state(pid, stopped, limit=3.0):
    """Wait until pid is stopped (T) or no longer stopped."""
    end = time.time() + limit
    while time.time() < end:
        if (proc_state(pid) in ("T", "t")) == stopped:
            return True
        time.sleep(0.01)
    return False


class Game:
    def __init__(self, prog, args=(), test=None, env_extra=None, jobctl=False, close_stdin=False):
        """jobctl runs the game in its own process group, led by a child of
        a waiting parent, so the group is not orphaned and a SIGTSTP stops
        it; self.gpid is the game's pid. close_stdin closes descriptor 0
        before exec so select on it fails."""
        import pty

        self.term = Term()
        self.gpid = None
        info_r, info_w = os.pipe()
        env = dict(os.environ)
        env.pop("GAMEBOX_TEST", None)
        if test is not None:
            env["GAMEBOX_TEST"] = test
        if env_extra:
            env.update(env_extra)
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.clear()
            os.environ.update(env)
            os.close(info_r)
            if jobctl:
                run_in_own_group(prog, args, info_w)
            os.close(info_w)
            if close_stdin:
                os.close(0)
            os.execvp(prog, [prog] + list(args))
        os.close(info_w)
        if jobctl:
            self.gpid = int(os.read(info_r, 32))
        os.close(info_r)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        self.status = None
        self.settle(0.4)

    def settle(self, quiet=0.15, limit=3.0):
        end = time.time() + limit
        last = time.time()
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError:
                    return
                if not chunk:
                    return
                last = time.time()
                self.term.feed(chunk, last)
            elif time.time() - last >= quiet:
                return

    def send(self, data, pause=0.01):
        if isinstance(data, str):
            data = data.encode()
        os.write(self.fd, data)
        time.sleep(pause)

    def keys(self, data, settle=0.2):
        if isinstance(data, bytes):
            self.send(data, 0.05)
        else:
            for ch in data:
                self.send(ch, 0.01)
        self.settle(settle)

    def quit(self):
        try:
            os.write(self.fd, b"q")
        except OSError:
            pass
        self.settle(0.3)
        _, st = os.waitpid(self.pid, 0)
        self.status = os.waitstatus_to_exitcode(st)
        return self.status

    def quit_within(self, limit):
        """Send q and require the exit within limit seconds; returns the
        status. A game that does not leave is killed and the check fails."""
        os.write(self.fd, b"q")
        end = time.time() + limit
        while time.time() < end:
            self.settle(0.02, 0.05)
            done, st = os.waitpid(self.pid, os.WNOHANG)
            if done:
                self.status = os.waitstatus_to_exitcode(st)
                return self.status
        self.kill()
        raise AssertionError("q did not quit within %.2f s" % limit)

    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except OSError:
            pass


STATUS_ROW = 26


def status_line(g):
    return g.term.line(STATUS_ROW)


def cell(g, r, c):
    """The two terminal characters of board cell (r, c)."""
    return g.term.text(r, 3 + 2 * c, 4 + 2 * c)


def piece_cells(g, rmax):
    """Visible board characters in rows 2..rmax as (row, col, char, fg, bold)."""
    t = g.term
    return [
        (r, c, t.grid[r][c][0], t.grid[r][c][2], t.grid[r][c][3]) for r, c in t.lit() if r <= rmax
    ]


def test_replay_one(prog):
    g = Game(prog, test="O")
    check("score 0 lines 0 level 1" in status_line(g), "initial status line: %r" % status_line(g))
    g.keys("hhhhh ")
    g.keys("hhh ")
    g.keys("h ")
    g.keys("l ")
    g.keys("lll ")
    check(b"~~" not in g.term.raw, "a flash drew before any row was cleared")
    g.keys("lllll ")
    text = g.term.raw.decode(errors="replace")
    plain = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
    check("score 564 lines 2 level 1" in plain, "REPLAY-1 status missing in stream")
    check(status_line(g) == "score 564 lines 2 level 1", "REPLAY-1 screen: %r" % status_line(g))
    check(not g.term.lit(), "cleared rows left cells on the board")
    check(g.quit() == 0, "REPLAY-1 exit status %r" % g.status)


def test_replay_two(prog):
    g = Game(prog, test="O")
    g.keys(" " * 12)
    plain = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", g.term.raw.decode(errors="replace"))
    check("score 264" in plain, "REPLAY-2 score 264 missing")
    check("GAME OVER" in plain, "REPLAY-2 GAME OVER missing")
    check(g.term.text(12, 10, 18) == "GAME OVER", "GAME OVER position: %r" % g.term.line(12))
    check(g.term.text(14, 6, 22) == "Enter  play again", "play-again line: %r" % g.term.line(14))
    check(g.term.text(15, 11, 17) == "q  quit", "quit line: %r" % g.term.line(15))
    check(
        status_line(g) == "score 264 lines 0 level 1", "status after game over: %r" % status_line(g)
    )
    # moves after game over are ignored; Enter starts a new game
    g.keys("h")
    check(status_line(g) == "score 264 lines 0 level 1", "key after game over changed the status")
    g.keys("\r")
    check(status_line(g) == "score 0 lines 0 level 1", "new game status: %r" % status_line(g))
    check("GAME OVER" not in g.term.line(12), "overlay left after a new game")
    check(g.term.text(2, 30, 34) == "SCORE", "panel label lost after a new game")
    g.keys(" " * 12)
    check("GAME OVER" in g.term.line(12), "second game did not end")
    check(g.quit() == 0, "REPLAY-2 exit status %r" % g.status)


def test_rotation_arrows(prog):
    g = Game(prog, test="I")
    g.keys(ESC + b"[A" + b" ")  # Up arrow rotates clockwise, space drops
    check(status_line(g) == "score 40 lines 0 level 1", "REPLAY-4: %r" % status_line(g))
    for r in (20, 21, 22, 23):
        check(cell(g, r, 6) == "[]", "REPLAY-4 cell row %d missing" % r)
    check(g.quit() == 0, "exit status %r" % g.status)
    g = Game(prog, test="I")
    g.keys("xz ")  # x clockwise, z back: flat again
    check(status_line(g) == "score 44 lines 0 level 1", "x then z: %r" % status_line(g))
    check(g.quit() == 0, "exit status %r" % g.status)
    g = Game(prog, test="I")
    g.keys(ESC + b"[C" + ESC + b"[D" + ESC + b"[D" + ESC + b"[B")  # right, left, left, soft drop
    g.keys(" ")
    # soft drop 1, hard drop from row 1 covers 21 rows: 1 + 42
    check(status_line(g) == "score 43 lines 0 level 1", "arrows: %r" % status_line(g))
    check(
        cell(g, 23, 3) == "[]"
        and cell(g, 23, 6) == "[]"
        and cell(g, 23, 2) == "  "
        and cell(g, 23, 7) == "  ",
        "arrow moves put the I at the wrong columns",
    )
    check(g.quit() == 0, "exit status %r" % g.status)


def test_enter_keys(prog):
    g = Game(prog, test="I")
    g.keys("\r")  # Enter hard-drops (CR or LF arrives)
    check(status_line(g) == "score 44 lines 0 level 1", "CR: %r" % status_line(g))
    g.keys("\n")
    check(status_line(g) == "score 86 lines 0 level 1", "LF: %r" % status_line(g))
    g.keys("p")
    check(g.term.text(13, 12, 17) == "PAUSED", "pause")
    g.keys("\r")
    check(g.term.text(13, 12, 17) != "PAUSED", "Enter did not resume")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_layout(prog):
    g = Game(prog, test="OT")
    t = g.term
    check(t.text(1, 11, 18) == "FOURFALL", "title: %r" % t.line(1))
    check(t.line(1).strip() == "FOURFALL", "title row holds more than the title")
    for r in BOARD_ROWS:
        check(t.text(r, 2, 2) == "!" and t.text(r, 27, 27) == "!", "wall row %d" % r)
    check(t.text(24, 2, 27) == "=" * 26, "floor: %r" % t.line(24))
    check(t.text(1, 2, 2) == " " and t.text(25, 2, 27) == " " * 26, "frame overdrawn")
    for r, label in ((2, "SCORE"), (3, "LEVEL"), (4, "LINES")):
        check(t.text(r, 30, 34) == label, "label %s" % label)
    check(
        t.text(2, 37, 37) == "0" and t.text(3, 37, 37) == "1" and t.text(4, 37, 37) == "0",
        "panel values",
    )
    check(t.text(7, 30, 33) == "NEXT", "NEXT label")
    for r in range(16, 23):
        check(t.line(r)[29:].strip() != "", "help line %d empty" % r)
    check(status_line(g) == "score 0 lines 0 level 1", "status line")
    # Queue O T O T ...: the T is first in the panel, the O second, each as
    # its glyph texture in its spawn orientation, two preview rows apiece.
    check(t.text(8, 30, 37) == " " * 8, "something between NEXT and the first preview")
    check(t.text(9, 30, 37) == "  <>    ", "preview T top row: %r" % t.text(9, 30, 37))
    check(t.text(10, 30, 37) == "<><><>  ", "preview T bottom row: %r" % t.text(10, 30, 37))
    check(t.text(11, 30, 37) == " " * 8, "gap between the previews")
    check(t.text(12, 30, 37) == "####    ", "second preview O top: %r" % t.text(12, 30, 37))
    check(t.text(13, 30, 37) == "####    ", "second preview O bottom: %r" % t.text(13, 30, 37))
    check(t.fgs(10, 30, 35) == {90} and t.bolds(10, 30, 35) == {False}, "preview not dim")
    check(not t.lit(), "a hidden-row piece is drawn")
    check(t.r == 31 and t.c == 1, "cursor not parked at row 31 col 1: %d,%d" % (t.r, t.c))
    g.keys("jj")  # O to rows 2-3 at board columns 5-6
    check(t.text(2, 13, 16) == "####" and t.text(3, 13, 16) == "####", "falling O not drawn")
    check(t.text(4, 13, 16) == "    ", "falling O drew too far")
    g.keys("hhhhh")
    check(t.text(2, 3, 6) == "####", "O did not reach the left wall")
    g.keys("h")
    check(
        t.text(2, 3, 6) == "####" and t.text(2, 7, 26) == " " * 20,
        "O passed the left wall or left a trail",
    )
    g.keys("llllllllll")
    check(t.text(2, 23, 26) == "####", "O did not reach the right wall")
    g.keys("l")
    check(t.text(2, 23, 26) == "####" and t.text(2, 3, 22) == " " * 20, "O passed the right wall")
    g.keys("jj")
    check(not t.row32_written and not t.scrolled, "the game wrote row 32 or scrolled")
    check(t.max_col_written <= 52, "text reached column %d" % t.max_col_written)
    g.keys(" ")
    check(t.text(23, 23, 26) == "####" and t.text(22, 23, 26) == "####", "hard drop landing")
    # after the lock the O is first in the panel and the T second
    check(t.text(9, 30, 37) == "####    " and t.text(10, 30, 37) == "####    ", "preview not O")
    check(
        t.text(12, 30, 37) == "  <>    " and t.text(13, 30, 37) == "<><><>  ",
        "second preview not T",
    )
    check(g.quit() == 0, "exit status %r" % g.status)
    check(not t.row32_written and not t.scrolled, "the game wrote row 32 or scrolled")


def drop_each_type(g):
    """Soft-drop twice so a fresh piece is visible, yield it, then hard-drop it."""
    for letter in "IOTSZJL":
        g.keys("jj")
        yield letter, piece_cells(g, 5)
        g.keys(" ")


def test_default_look(prog):
    # No hue, no landing aid: glyph textures, bright when falling, dim when settled.
    g = Game(prog, test="IOTSZJL")
    t = g.term
    for letter, cells in drop_each_type(g):
        check(len(cells) == 8, "%s: %d visible characters" % (letter, len(cells)))
        check(
            {ch for _, _, ch, _, _ in cells} == set(GLYPHS[letter]),
            "%s: glyph %r" % (letter, cells),
        )
        check(all(fg == 37 and b for _, _, _, fg, b in cells), "%s falling not bold white" % letter)
    settled = piece_cells(g, 23)
    check(len(settled) == 56, "settled stack holds %d characters, want 56" % len(settled))
    check(all(fg == 90 and not b for _, _, _, fg, b in settled), "settled cells not dim")
    check(all(c[1] == 0 for row in t.grid for c in row), "a background color is set")
    check(t.sgr_seen <= {0, 1, 37, 90}, "default look wrote SGR codes %r" % sorted(t.sgr_seen))
    check(not any("." in t.text(r, 3, 26) for r in BOARD_ROWS), "plumb dots drawn without -g")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_color_mode(prog):
    g = Game(prog, args=["-c"], test="IOTSZJL")
    for letter, cells in drop_each_type(g):
        check({ch for _, _, ch, _, _ in cells} == set(GLYPHS[letter]), "%s: glyph" % letter)
        check(
            all(fg == HUES[letter] and b for _, _, _, fg, b in cells),
            "%s falling hue %r" % (letter, {fg for _, _, _, fg, _ in cells}),
        )
    settled = piece_cells(g, 23)
    check({fg for _, _, _, fg, _ in settled} == set(HUES.values()), "settled hues differ")
    check(not any(b for _, _, _, _, b in settled), "settled cells bold in color mode")
    check(g.quit() == 0, "exit status %r" % g.status)
    g = Game(prog, args=["-c"], test="OT")
    t = g.term
    check(t.fgs(10, 30, 35) == {32} and t.text(10, 30, 35) == "<><><>", "T preview hue")
    check(t.fgs(12, 30, 33) == {34}, "O preview hue")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_plumb_line(prog):
    g = Game(prog, args=["-g"], test="O")
    t = g.term
    # The O waits in the hidden rows over board columns 5 and 6, whose dots
    # sit at terminal columns 13 and 15; every visible row below it has both.
    for r in BOARD_ROWS:
        check(t.text(r, 13, 16) == ". . ", "plumb row %d: %r" % (r, t.text(r, 13, 16)))
        check(t.fgs(r, 13, 13) == {90}, "plumb dot row %d not dim" % r)
    check(
        t.text(2, 3, 12) == " " * 10 and t.text(2, 17, 26) == " " * 10, "plumb outside the columns"
    )
    g.keys("jj")  # O at rows 2-3: dots from row 4 down
    check(t.text(2, 13, 16) == "####" and t.text(3, 13, 16) == "####", "falling O")
    for r in BOARD_ROWS:
        want = ". . " if r >= 4 else "####"
        check(
            t.text(r, 13, 16) == want, "plumb after two rows, row %d: %r" % (r, t.text(r, 13, 16))
        )
    g.keys("g")
    check(
        not any("." in t.text(r, 3, 26) for r in BOARD_ROWS), "g did not switch the plumb line off"
    )
    g.keys("g")
    check(t.text(4, 13, 16) == ". . ", "g did not switch the plumb line on")
    g.keys(" ")  # O lands at rows 22-23; the next O spawns above it
    g.keys("jj")
    for r in range(4, 22):
        check(t.text(r, 13, 16) == ". . ", "plumb above the stack, row %d" % r)
    check(t.text(22, 13, 16) == "####" and t.text(23, 13, 16) == "####", "settled O")
    check(t.fgs(22, 13, 16) == {90}, "settled O not dim")
    g.keys("hhh")  # off the stack: the dots run to the floor again
    check(t.text(23, 7, 10) == ". . ", "plumb beside the stack: %r" % t.text(23, 7, 10))
    check(g.quit() == 0, "exit status %r" % g.status)
    # A flat I over a flat I: the dots of every column stop on the stack.
    g = Game(prog, args=["-g"], test="I")
    t = g.term
    g.keys("jj")
    check(
        all(t.text(r, 11, 18) == ". . . . " for r in range(4, 24)),
        "plumb under a flat I",
    )
    g.keys(" ")
    g.keys("jj")
    check(t.text(23, 11, 18) == "[][][][]", "first I settled")
    check(
        all(t.text(r, 11, 18) == ". . . . " for r in range(4, 23)),
        "plumb does not stop on the stack",
    )
    check(g.quit() == 0, "exit status %r" % g.status)


def test_mono(prog):
    g = Game(prog, args=["-m"], test="O")
    g.keys("jj")
    t = g.term
    check(
        t.text(2, 13, 16) == "OOOO" and t.bgs(2, 13, 16) == {0},
        "monochrome cells: %r" % t.text(2, 13, 16),
    )
    g.keys(" ")
    check(t.text(23, 13, 16) == "OOOO", "monochrome locked cells")
    check(t.sgr_seen <= {0}, "monochrome mode wrote attributes %r" % sorted(t.sgr_seen))
    check(g.quit() == 0, "exit status %r" % g.status)
    g = Game(prog, args=["-m", "-c", "-g"], test="O")
    t = g.term
    g.keys("jj")
    check(t.text(2, 13, 16) == "OOOO" and t.sgr_seen <= {0}, "-m did not win over -c")
    check(t.text(4, 13, 16) == ". . ", "plumb dots in monochrome")
    check(g.quit() == 0, "exit status %r" % g.status)
    g = Game(prog, args=["-cg"], test="O")
    g.keys("jj")
    check(g.term.text(4, 13, 16) == ". . " and g.term.fgs(2, 13, 16) == {34}, "-cg combined flags")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_flash(prog):
    # Three flat I pieces complete the bottom row: its cells show "~~" for
    # about 150 ms, then the board collapses.
    g = Game(prog, test="I")
    t = g.term
    g.keys((ESC + b"[C") * 4 + b" ")
    g.keys((ESC + b"[D") * 4 + b" ")
    check(b"~~" not in t.raw, "flash before the row was complete")
    t.chunks.clear()
    g.keys(" ")
    check(status_line(g) == "score 232 lines 1 level 1", "flash replay status: %r" % status_line(g))
    marks = t.raw.count(b"\x1b[1m~~\x1b[0m")
    check(marks == 12, "%d flash cells drawn, want 12" % marks)
    idx = [i for i, (_, d) in enumerate(t.chunks) if b"~~" in d]
    check(len(idx) == 1 and idx[0] + 1 < len(t.chunks), "flash frame count %r" % idx)
    check(t.chunks[idx[0]][1].count(b"~~") == 12, "the flash frame lacks a cell of the row")
    gap = t.chunks[idx[0] + 1][0] - t.chunks[idx[0]][0]
    check(0.12 <= gap <= 0.6, "flash lasted %.3f s, want about 0.15" % gap)
    check(not t.lit(), "the cleared row is still drawn after the flash")
    check(t.text(23, 3, 26) == " " * 24, "collapsed row not blank")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_usage(prog):
    import subprocess

    for args in (["-x"], ["-cx"], ["1", "2", "3"]):
        r = subprocess.run([prog] + args, capture_output=True, timeout=10, stdin=subprocess.DEVNULL)
        check(r.returncode == 2, "%r: exit %d" % (args, r.returncode))
        check(
            r.stderr == b"usage: fourfall [-cgm] [start-level [seed]]\n",
            "%r: %r" % (args, r.stderr),
        )


def paused_only(t):
    """True when the PAUSED banner is the only text left in the well."""
    return set(t.lit()) == {(13, c) for c in range(12, 18)}


def test_pause_and_keys(prog):
    for name, seq in (
        ("p", b"p"),
        ("F1 SS3", ESC + b"OP"),
        ("F1 CSI", ESC + b"[11~"),
        ("Esc", ESC),
    ):
        g = Game(prog, test="O")
        t = g.term
        g.keys("jj")
        check(len(t.lit()) == 8, "%s: piece not drawn before pause" % name)
        g.send(seq)
        g.settle(0.4)
        check(t.text(13, 12, 17) == "PAUSED", "%s: no PAUSED banner: %r" % (name, t.line(13)))
        check(paused_only(t), "%s: board not blanked while paused" % name)
        check(
            t.text(9, 30, 37) == " " * 8 and t.text(12, 30, 37) == " " * 8,
            "%s: preview kept while paused" % name,
        )
        g.keys("hh ")  # ignored by pause except Enter/Space resumes below
        check(
            len(t.lit()) == 8 and t.text(13, 12, 17) != "PAUSED", "%s: space did not resume" % name
        )
        check(t.text(2, 13, 16) == "####", "%s: resume moved the piece (h while paused)" % name)
        g.send(seq)
        g.settle(0.3)
        check(t.text(13, 12, 17) == "PAUSED", "%s: second pause" % name)
        g.send(seq)
        g.settle(0.3)
        check(t.text(13, 12, 17) != "PAUSED" and len(t.lit()) == 8, "%s: key did not resume" % name)
        # unknown sequences are consumed and ignored
        g.keys(ESC + b"[15~" + ESC + b"[Z" + ESC + b"OQ")
        check(t.text(2, 13, 16) == "####", "%s: unknown sequence acted" % name)
        g.keys("h")
        check(t.text(2, 11, 14) == "####", "%s: key after unknown sequences ignored" % name)
        check(g.quit() == 0, "%s: exit status %r" % (name, g.status))


def test_repaint(prog):
    g = Game(prog, test="O")
    t = g.term
    g.keys("jj")
    before = [row[:] for row in t.grid]
    g.keys("\x0c")  # Ctrl-L
    # The status line (row 26) refreshes on a lock or a repaint only, so it
    # may legitimately show the newer score after Ctrl-L; every other row matches.
    check(
        all(t.grid[r] == before[r] for r in range(1, ROWS + 1) if r != STATUS_ROW),
        "Ctrl-L repaint differs from the screen before it",
    )
    check(status_line(g) == "score 2 lines 0 level 1", "repaint status: %r" % status_line(g))
    g.keys("p")
    g.keys("\x0c")
    check(t.text(13, 12, 17) == "PAUSED" and paused_only(t), "repaint while paused")
    g.keys("p")
    check(len(t.lit()) == 8, "resume after repaint")
    check(g.quit() == 0, "exit status %r" % g.status)


# What every exit writes last: SGR reset, clear, cursor home.
LEAVE = b"\x1b[0m\x1b[2J\x1b[H"


def cooked(fd):
    """True when ICANON and ECHO are both on, as after the restore."""
    lflag = termios.tcgetattr(fd)[3]
    return bool(lflag & termios.ICANON) and bool(lflag & termios.ECHO)


def test_signals(prog):
    for sig, want in (
        (signal.SIGINT, 130),
        (signal.SIGTERM, 143),
        (signal.SIGHUP, 129),
        (signal.SIGQUIT, 131),
    ):
        g = Game(prog, test="O")
        check(not cooked(g.fd), "signal %d: raw mode not entered" % sig)
        os.kill(g.pid, sig)
        g.settle(0.3)
        _, st = os.waitpid(g.pid, 0)
        check(
            os.waitstatus_to_exitcode(st) == want,
            "signal %d: exit %r" % (sig, os.waitstatus_to_exitcode(st)),
        )
        # the terminal mode is restored: echo and canonical mode are back on
        check(cooked(g.fd), "signal %d: terminal left in raw mode" % sig)
        check(g.term.raw.endswith(LEAVE), "signal %d: no reset and home at exit" % sig)


def test_select_failure(prog):
    # Descriptor 0 closed: select fails with EBADF. The exit runs the same
    # restore path as q and says why on stderr, with status 1.
    g = Game(prog, test="O", close_stdin=True)
    g.settle(0.5)
    _, st = os.waitpid(g.pid, 0)
    check(os.waitstatus_to_exitcode(st) == 1, "select failure: exit %r" % st)
    check(b"fourfall: select failed" in g.term.raw, "no select failure message")
    check(LEAVE in g.term.raw, "select failure left no reset and home")


def test_job_control(prog):
    g = Game(prog, test="O", jobctl=True)
    gp = g.gpid
    g.keys("jj")
    check(not cooked(g.fd), "raw mode not entered")
    os.kill(gp, signal.SIGTSTP)
    check(wait_state(gp, True), "SIGTSTP did not stop the game (state %r)" % proc_state(gp))
    check(cooked(g.fd), "stopped with the terminal still raw")
    g.settle(0.2)
    g.term = Term()  # a blank screen shows whether the resume repaints
    os.kill(gp, signal.SIGCONT)
    check(wait_state(gp, False), "SIGCONT did not resume the game")
    g.settle(0.5)
    check(not cooked(g.fd), "raw mode not re-entered after SIGCONT")
    check(len(g.term.lit()) == 8, "no repaint after SIGCONT: %d lit cells" % len(g.term.lit()))
    g.keys("h")
    check(
        g.term.text(2, 11, 14) == "####" or len(g.term.lit()) == 8,
        "game ignores keys after SIGCONT",
    )
    # a SIGCONT without a stop repaints and leaves the saved mode alone
    g.term = Term()
    os.kill(gp, signal.SIGCONT)
    g.settle(0.5)
    check(len(g.term.lit()) == 8, "no repaint after a plain SIGCONT")
    check(not cooked(g.fd), "plain SIGCONT changed the terminal mode")
    os.kill(gp, signal.SIGTERM)
    g.settle(0.3)
    _, st = os.waitpid(g.pid, 0)
    check(os.waitstatus_to_exitcode(st) == 143, "exit %r after SIGTERM" % st)
    check(cooked(g.fd), "terminal not restored to the mode before the first stop")


def test_escape_deadline(prog):
    # A truncated sequence is abandoned at the deadline; the next q quits.
    g = Game(prog, test="O")
    g.send(ESC + b"[")
    time.sleep(0.2)
    check(g.quit_within(0.5) == 0, "ESC [ then q: exit %r" % g.status)
    check(cooked(g.fd), "terminal not restored")
    # The same for SS3 and a partial numeric CSI.
    for frag in (ESC + b"O", ESC + b"[1", ESC + b"[11"):
        g = Game(prog, test="O")
        g.send(frag)
        time.sleep(0.2)
        check(g.quit_within(0.5) == 0, "%r then q: exit %r" % (frag, g.status))
    # A byte inside the sequence arrives within the deadline: ESC, a 10 ms
    # gap, then [A rotates.
    g = Game(prog, test="I")
    g.send(ESC, 0.01)
    g.send(b"[A", 0.01)
    g.keys(" ")
    check(status_line(g) == "score 40 lines 0 level 1", "ESC gap [A: %r" % status_line(g))
    for r in (20, 21, 22, 23):
        check(cell(g, r, 6) == "[]", "ESC gap [A: rotated cell row %d missing" % r)
    check(g.quit() == 0, "exit status %r" % g.status)
    # Every byte of one CSI split by 10 ms gaps still parses as one key.
    g = Game(prog, test="I")
    for ch in (b"\x1b", b"[", b"A"):
        g.send(ch, 0.01)
    g.keys(" ")
    check(status_line(g) == "score 40 lines 0 level 1", "split CSI: %r" % status_line(g))
    check(g.quit() == 0, "exit status %r" % g.status)
    # A bare Esc still pauses, after the deadline and not before a key follows.
    g = Game(prog, test="O")
    g.send(ESC)
    g.settle(0.4)
    check(g.term.text(13, 12, 17) == "PAUSED", "bare Esc did not pause")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_stalled_gravity(prog):
    # Gravity keeps running while a sequence is stalled: the piece falls and
    # the game keeps drawing.
    g = Game(prog, args=["9", "7"])
    t = g.term
    g.settle(0.3, 0.3)
    g.send(ESC + b"[")
    before = len(t.raw)
    rows0 = [r for r in BOARD_ROWS if t.text(r, 3, 26).strip()]
    g.settle(0.6, 0.6)
    rows1 = [r for r in BOARD_ROWS if t.text(r, 3, 26).strip()]
    check(len(t.raw) > before, "no output while the sequence was stalled")
    check(
        rows0 and rows1 and max(rows1) > max(rows0), "piece did not fall during a stalled sequence"
    )
    check(g.quit_within(0.5) == 0, "exit status %r" % g.status)


def test_exit_restores(prog):
    g = Game(prog, test="O")
    check(not (termios.tcgetattr(g.fd)[3] & termios.ICANON), "raw mode not entered")
    check(g.quit() == 0, "exit status %r" % g.status)


def test_timed(prog):
    # Level 9: 210 ms a row. Without GAMEBOX_TEST the piece falls by itself,
    # locks 500 ms after it lands, and the status line is redrawn on the lock.
    g = Game(prog, args=["9", "7"])
    t = g.term
    base = t.raw.count(b"score 0 lines 0 level 9")
    check(base >= 1, "initial status line")
    g.settle(1.0, 1.0)
    rows = [r for r in BOARD_ROWS if t.text(r, 3, 26).strip()]
    check(rows, "no piece after 1.4 s at level 9")
    check(max(rows) >= 4, "piece did not fall (rows %r)" % rows)
    check(
        not any("." in t.text(r, 3, 26) for r in BOARD_ROWS),
        "the plumb line is on by default",
    )
    g.settle(5.0, 5.0)
    check(
        t.raw.count(b"score 0 lines 0 level 9") >= base + 1, "no status line redraw after the lock"
    )
    check(g.quit() == 0, "exit status %r" % g.status)
    # Pause keeps the clock still: nothing locks while paused.
    g = Game(prog, args=["9", "7"])
    t = g.term
    g.send("p")
    g.settle(0.3)
    base = t.raw.count(b"score 0 lines 0 level 9")
    time.sleep(5.0)
    g.settle(0.2)
    check(t.raw.count(b"score 0 lines 0 level 9") == base, "a piece locked while paused")
    check(t.text(13, 12, 17) == "PAUSED", "pause banner lost")
    g.send("p")
    g.settle(5.6, 5.6)
    check(t.raw.count(b"score 0 lines 0 level 9") > base, "no lock after resume")
    check(g.quit() == 0, "exit status %r" % g.status)


CASES = [
    test_replay_one,
    test_replay_two,
    test_rotation_arrows,
    test_enter_keys,
    test_layout,
    test_default_look,
    test_color_mode,
    test_plumb_line,
    test_mono,
    test_flash,
    test_usage,
    test_pause_and_keys,
    test_repaint,
    test_signals,
    test_select_failure,
    test_job_control,
    test_escape_deadline,
    test_stalled_gravity,
    test_exit_restores,
    test_timed,
]


def main():
    prog = sys.argv[1] if len(sys.argv) > 1 else "./fourfall-host"
    want = sys.argv[2:]
    cases = [c for c in CASES if not want or c.__name__[5:] in want]
    if want and len(cases) != len(want):
        print("fourfall: unknown case in %r" % want)
        return 2
    for case in cases:
        try:
            case(prog)
        except AssertionError as e:
            print("fourfall: FAIL in %s: %s" % (case.__name__, e))
            return 1
        print("fourfall: %s ok" % case.__name__)
    print("fourfall: OK (%d cases, %d checks)" % (len(cases), checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
