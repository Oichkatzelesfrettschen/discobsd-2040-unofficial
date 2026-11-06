#!/usr/bin/env python3
# pty tests of calc-host. Each case runs the program on a pseudo-terminal sized
# 32 rows by 53 columns, sends keys, and reads the result from a terminal model
# that applies the escape sequences the program writes (cursor addressing, erase,
# scroll region, save and restore cursor, SGR reverse and bold), so the checks see the
# screen and its attributes rather than a byte stream. The F-key byte sequences of
# the PicoCalc come from sys/arch/rp2040/dev/picokbd_map.c, the source of the
# keyboard driver, not from a copy.
#
# usage: play.py [./calc-host [case ...]]
#   a case is a test function name without its test_ prefix; none runs all

import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
from pathlib import Path

ROWS, COLS = 32, 53
ESC = "\x1b"
ROOT = Path(__file__).resolve().parents[3]
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


BLANK = (" ", False, False)


class Term:
    """Cursor movement, erase, scroll region, save and restore cursor, SGR 0 1 4 7."""

    def __init__(self):
        self.grid = [[BLANK] * COLS for _ in range(ROWS)]
        self.r = self.c = 0
        self.top, self.bot = 0, ROWS - 1
        self.rev = self.bold = False
        self.pend = False
        self.saved = (0, 0, False, False)
        self.wraps = 0
        self.raw = b""
        self.rest = b""

    def feed(self, data):
        self.raw += data
        data = self.rest + data
        self.rest = b""
        i, n = 0, len(data)
        while i < n:
            b = data[i]
            if b == 0x1B:
                m = re.match(rb"\x1b\[([0-9;?]*)([A-Za-z@])", data[i:])
                if m:
                    self.csi(m.group(1).decode(), m.group(2).decode())
                    i += m.end()
                    continue
                if i + 1 < n and data[i + 1] in b"78":
                    self.esc(chr(data[i + 1]))
                    i += 2
                    continue
                if i + 1 >= n or re.match(rb"\x1b\[[0-9;?]*$", data[i:]):
                    self.rest = data[i:]
                    return
                i += 1
                continue
            i += 1
            if b == 0x0D:
                self.c, self.pend = 0, False
            elif b == 0x0A:
                self.index()
            elif b == 0x08:
                self.c, self.pend = max(0, self.c - 1), False
            elif 0x20 <= b < 0x7F:
                self.put(chr(b))

    def index(self):
        self.pend = False
        if self.r == self.bot:
            del self.grid[self.top]
            self.grid.insert(self.bot, [BLANK] * COLS)
        elif self.r < ROWS - 1:
            self.r += 1

    def put(self, ch):
        if self.pend:
            self.wraps += 1
            self.c = 0
            self.index()
        self.grid[self.r][self.c] = (ch, self.rev, self.bold)
        if self.c == COLS - 1:
            self.pend = True
        else:
            self.c += 1

    def esc(self, ch):
        if ch == "7":
            self.saved = (self.r, self.c, self.rev, self.bold)
        else:
            self.r, self.c, self.rev, self.bold = self.saved
            self.pend = False

    def csi(self, params, final):
        p = [int(x) if x else 0 for x in params.lstrip("?").split(";")] if params else []
        first = p[0] if p and p[0] else 1
        if final in "Hf":
            self.r = min(max(first, 1), ROWS) - 1
            self.c = min(max(p[1] if len(p) > 1 and p[1] else 1, 1), COLS) - 1
            self.pend = False
        elif final == "J":
            which = p[0] if p else 0
            if which == 2:
                self.grid = [[BLANK] * COLS for _ in range(ROWS)]
            elif which == 0:
                self.erase_line(self.c, COLS)
                for r in range(self.r + 1, ROWS):
                    self.grid[r] = [BLANK] * COLS
        elif final == "K":
            which = p[0] if p else 0
            if which == 0:
                self.erase_line(self.c, COLS)
            elif which == 2:
                self.erase_line(0, COLS)
        elif final == "m":
            for v in p or [0]:
                if v == 0:
                    self.rev = self.bold = False
                elif v == 1:
                    self.bold = True
                elif v == 7:
                    self.rev = True
                elif v == 27:
                    self.rev = False
                elif v == 22:
                    self.bold = False
        elif final == "r":
            if not p:
                self.top, self.bot = 0, ROWS - 1
            else:
                self.top, self.bot = p[0] - 1, (p[1] if len(p) > 1 else ROWS) - 1
            self.r = self.c = 0
            self.pend = False
        elif final == "A":
            self.r = max(0, self.r - first)
        elif final == "B":
            self.r = min(ROWS - 1, self.r + first)
        elif final == "C":
            self.c = min(COLS - 1, self.c + first)
        elif final == "D":
            self.c = max(0, self.c - first)

    def erase_line(self, a, b):
        for c in range(a, b):
            self.grid[self.r][c] = BLANK

    def line(self, row):
        """Text of a 1-based row without trailing blanks."""
        return "".join(ch for ch, _, _ in self.grid[row - 1]).rstrip()

    def text(self):
        return "\n".join(self.line(r) for r in range(1, ROWS + 1))

    def find(self, needle):
        return [r for r in range(1, ROWS + 1) if needle in self.line(r)]

    def attrs(self, row, c0, c1):
        """Reverse-video flags of 1-based columns c0..c1 on a 1-based row."""
        return [self.grid[row - 1][c - 1][1] for c in range(c0, c1 + 1)]


def fkey_sequences():
    """The ten F-key strings of the keyboard driver's fkeys[] table."""
    text = (ROOT / "sys/arch/rp2040/dev/picokbd_map.c").read_text()
    body = text.split("fkeys[", 1)[1].split("};", 1)[0]
    seqs = [s.encode().decode("unicode_escape") for s in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]
    check(len(seqs) == 10, "picokbd_map.c fkeys[] holds %d strings" % len(seqs))
    return seqs


BAR_ITEMS = ["Help", "Alg", "RPN", "Prog", "Rad", "Unit", "Stat", "Cnst", "Clr", "Quit"]


def bar_text(deg=False):
    parts = []
    for i, label in enumerate(BAR_ITEMS):
        if i == 4 and deg:
            label = "Deg"
        parts.append("%d%-4s" % (i + 1, label))
    return "".join(parts)


class Calc:
    def __init__(self, prog, args=()):
        self.term = Term()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            env = {"TERM": "xterm", "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
            os.execvpe(prog, [prog] + list(args), env)
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
                self.term.feed(chunk)
            elif time.time() - last >= quiet:
                return

    def send(self, data, settle=0.15):
        if isinstance(data, str):
            data = data.encode()
        os.write(self.fd, data)
        self.settle(settle)

    def type(self, text):
        for ch in text:
            os.write(self.fd, ch.encode())
            time.sleep(0.004)
        self.settle(0.15)

    def reap(self, limit=3.0):
        end = time.time() + limit
        while time.time() < end:
            pid, st = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.status = os.waitstatus_to_exitcode(st)
                self.settle(0.1, 0.5)
                return self.status
            self.settle(0.05, 0.1)
        return None

    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except OSError:
            pass

    def cursor_row(self):
        return self.term.r + 1

    def find(self, needle):
        return self.term.find(needle)


def restored(g):
    attrs = termios.tcgetattr(g.fd)
    return bool(attrs[3] & termios.ECHO) and bool(attrs[3] & termios.ICANON)


def test_layout(prog):
    g = Calc(prog)
    t = g.term
    check(t.line(ROWS) == bar_text().rstrip(), "bar row: %r" % t.line(ROWS))
    check(len(bar_text()) == 51, "bar text is %d columns" % len(bar_text()))
    # Each item: its digit in normal video, its label in reverse; the mode in use is normal.
    col = 1
    for i, label in enumerate(BAR_ITEMS):
        width = 6 if i == 9 else 5
        dig = len(str(i + 1))
        check(not any(t.attrs(ROWS, col, col + dig - 1)), "digit of item %d is reversed" % (i + 1))
        want_rev = i != 1
        check(
            all(f == want_rev for f in t.attrs(ROWS, col + dig, col + width - 1)),
            "item %d (%s) reverse flags %r" % (i + 1, label, t.attrs(ROWS, col, col + width - 1)),
        )
        col += width
    check(all(t.attrs(ROWS, 52, 53)), "bar padding is not reversed to column 53")
    check(t.line(1).startswith("calc:"), "banner: %r" % t.line(1))
    check(t.line(2) == ">", "prompt row: %r" % t.line(2))
    check(all(t.line(r) == "" for r in range(3, ROWS)), "rows 3 to 31 are not blank")
    check(g.cursor_row() == 2 and t.c == 2, "cursor at %d,%d" % (t.r + 1, t.c + 1))
    check(t.wraps == 0, "text wrapped past column 53")
    g.send("quit\r")
    check(g.reap() == 0, "exit status %r" % g.status)
    check(restored(g), "terminal modes after quit")


def test_evaluate(prog):
    g = Calc(prog)
    g.type("2+3*4\r")
    rows = g.find("> 2+3*4")
    check(rows == [2], "echoed input rows %r" % rows)
    check(g.term.line(3) == "14", "result row: %r" % g.term.line(3))
    check(g.term.line(4) == ">", "next prompt: %r" % g.term.line(4))
    g.type("1/0\r")
    check(g.term.line(5) == "error: division by zero", "error row: %r" % g.term.line(5))
    g.type("ans+1\r")
    check(g.term.line(7) == "15", "session after an error: %r" % g.term.line(7))
    g.type("x=2^10\r")
    g.type("x/4\r")
    check(g.term.line(11) == "256", "variable: %r" % g.term.line(11))
    check(g.term.line(ROWS) == bar_text().rstrip(), "bar disturbed by output")
    check(g.cursor_row() == 12, "cursor row %d" % g.cursor_row())
    g.type("rpn\r")
    g.type("3 4 +\r")
    check(g.find("rpn> 3 4 +") and "7" in g.term.line(g.find("rpn> 3 4 +")[0] + 1), "rpn result")
    g.type("prog\r")
    g.type("0xF0 | 0x0F\r")
    check(any(line == "hex  0xFF" for line in g.term.text().split("\n")), "programmer result")
    check(any(line == "dec  255" for line in g.term.text().split("\n")), "programmer decimal")
    check(g.term.wraps == 0, "text wrapped past column 53")
    # typed commands move the bar as the F keys do
    g.type("alg\r")
    g.type("deg\r")
    check(g.term.line(ROWS) == bar_text(True).rstrip(), "bar after deg: %r" % g.term.line(ROWS))
    g.type("rpn\r")
    check(not any(g.term.attrs(ROWS, 12, 15)) and all(g.term.attrs(ROWS, 7, 10)), "bar after rpn")
    g.send("\x04")
    check(g.reap() == 0, "^D exit status %r" % g.status)


def test_fkeys(prog):
    seqs = fkey_sequences()
    check(seqs[:4] == [ESC + "OP", ESC + "OQ", ESC + "OR", ESC + "OS"], "F1-F4 strings %r" % seqs)
    check(
        seqs[4:] == [ESC + "[%d~" % n for n in (15, 17, 18, 19, 20, 21)],
        "F5-F10 strings %r" % seqs,
    )
    g = Calc(prog)
    t = g.term

    def active(item):
        """Mode item drawn in normal video while the other mode items are reversed."""
        flags = []
        for i in (1, 2, 3):
            col = 5 * i + 1 + 1
            flags.append(not any(t.attrs(ROWS, col, col + 3)))
        return flags == [i == item for i in (1, 2, 3)]

    g.send(seqs[0])
    check("calc: scientific calculator" in t.text() and "F10 quit" in t.text(), "F1 help")
    check(t.find("F6 units F7 stat F8 const F9 clr F10 quit"), "F1 help text")
    check(g.term.line(g.cursor_row()) == ">", "prompt after F1: %r" % t.line(g.cursor_row()))
    g.send(seqs[1])
    check(t.find("mode alg") and active(1), "F2: alg")
    g.send(seqs[2])
    check(t.find("mode rpn"), "F3 acknowledgement")
    prompt = t.line(g.cursor_row())
    check(prompt == "rpn>" and active(2), "F3: rpn prompt %r" % prompt)
    g.send(seqs[3])
    check(t.find("mode prog") and t.line(g.cursor_row()) == "prog>" and active(3), "F4: prog")
    g.send(seqs[1])
    check(t.line(g.cursor_row()) == ">" and active(1), "F2 again: alg prompt")
    g.send(seqs[4])
    check(t.find("angle degrees") and t.line(ROWS) == bar_text(True).rstrip(), "F5: degrees")
    g.type("sin(30)\r")
    check(any(line == "0.5" for line in t.text().split("\n")), "degrees in effect")
    g.send(seqs[4])
    check(t.find("angle radians") and t.line(ROWS) == bar_text().rstrip(), "F5 again: radians")
    g.send(seqs[5])
    check(t.find("length:") and t.find("temperature:"), "F6: units")
    g.send(seqs[6])
    check(t.find("usage: stat v1 v2 ..."), "F7: stat usage")
    g.send(seqs[7])
    check(t.find("pi 3.14159265358979"), "F8: constants")
    g.send(seqs[8])
    check(t.find("cleared"), "F9: clear")
    g.send(seqs[2])
    g.type("1 2\r")
    g.send(seqs[8])
    check(t.find("stack cleared"), "F9 in rpn clears the stack")
    check(t.line(ROWS) == bar_text().rstrip(), "bar after the keys")
    check(t.wraps == 0, "text wrapped past column 53")
    g.send(seqs[9])
    check(g.reap() == 0, "F10 exit status %r" % g.status)
    check(restored(g), "terminal modes after F10")


def test_fkey_variants(prog):
    seqs = fkey_sequences()
    # vt220 F1-F4, shifted xterm F1, Linux console F1, split delivery, ignored sequences
    cases = [
        (ESC + "[11~", "calc: scientific calculator"),
        (ESC + "[12~", "mode alg"),
        (ESC + "[13~", "mode rpn"),
        (ESC + "[14~", "mode prog"),
        (ESC + "[1;2P", "calc: scientific calculator"),
        (ESC + "[[A", "calc: scientific calculator"),
        (ESC + "[[B", "mode alg"),
        (ESC + "[15~", "angle degrees"),
    ]
    for seq, want in cases:
        g = Calc(prog)
        g.send(seq)
        check(g.find(want), "%r did not select %r\n%s" % (seq, want, g.term.text()))
        g.kill()
    g = Calc(prog)
    # One byte at a time with 10 ms gaps, a fifth of the 50 ms lone-Esc wait,
    # so a loaded host's late wakeup still lands inside the window.
    for ch in seqs[0]:
        os.write(g.fd, ch.encode())
        time.sleep(0.01)
    g.settle()
    check(g.find("calc: scientific calculator"), "F1 delivered one byte at a time")
    before = g.term.text()
    g.send(ESC + "[Z")
    g.send(ESC + "[3~")
    g.send(ESC + "[99~")
    g.send(ESC + "[5~")
    check(g.term.text() == before, "an unassigned sequence changed the screen")
    g.type("2+2\r")
    check(g.find("> 2+2") and g.term.line(g.find("> 2+2")[0] + 1) == "4", "input after sequences")
    g.kill()


def test_fkey_midline(prog):
    seqs = fkey_sequences()
    g = Calc(prog)
    g.type("1+")
    g.send(seqs[4])  # F5 while a line is being typed
    row = g.term.line(g.cursor_row())
    check(row == "> 1+", "typed text after F5: %r" % row)
    g.type("2\r")
    rows = g.find("> 1+2")
    check(rows and g.term.line(rows[0] + 1) == "3", "line completed after F5")
    g.send(seqs[1])
    g.kill()


def test_editing(prog):
    g = Calc(prog)
    g.type("12")
    g.send("\x7f")
    g.type("3\r")
    check(g.term.line(3) == "13", "backspace: %r" % g.term.line(3))
    g.type("99")
    g.send("\x04")  # ^D inside a line does not quit
    check(g.reap(0.3) is None, "^D mid-line ended the program")
    g.send("\x15")
    check(g.term.line(g.cursor_row()) == ">", "^U: %r" % g.term.line(g.cursor_row()))
    g.type("7\r")
    check(g.find("> 7") and "7" == g.term.line(g.find("> 7")[-1] + 1), "line after ^U")
    g.type("45")
    g.send(ESC)
    time.sleep(0.1)
    g.settle()
    check(g.term.line(g.cursor_row()) == ">", "bare ESC: %r" % g.term.line(g.cursor_row()))
    g.type("2+2\r")
    g.send(ESC + "[A")
    check(g.term.line(g.cursor_row()) == "> 2+2", "up arrow: %r" % g.term.line(g.cursor_row()))
    g.send(ESC + "[A")
    check(g.term.line(g.cursor_row()) == "> 7", "second up arrow: %r" % g.term.line(g.cursor_row()))
    g.send(ESC + "[B")
    g.send(ESC + "[B")
    check(g.term.line(g.cursor_row()) == ">", "down past the newest line")
    g.send(ESC + "[A")
    g.send("\r")
    rows = g.find("> 2+2")
    check(len(rows) == 2, "recalled line runs again %r" % rows)
    g.send("\x0c")
    check(g.term.line(ROWS) == bar_text().rstrip() and g.term.line(1).startswith("calc:"), "^L")
    g.kill()


def test_long_line(prog):
    g = Calc(prog)
    expr = "1" + "+1" * 40  # 81 characters, wider than the 53 columns
    g.type(expr)
    row = g.term.line(g.cursor_row())
    check(row.startswith("> ") and len(row) <= 52 and row.endswith("+1"), "tail shown: %r" % row)
    g.send("\r")
    check(g.find("41"), "long line result")
    g.type("1" * 200)
    check(g.term.wraps == 0, "typing past the line limit wrapped")
    g.send("\x15")
    g.kill()


def test_scroll(prog):
    g = Calc(prog)
    for _ in range(4):
        g.type("units\r")
    t = g.term
    check(t.line(ROWS) == bar_text().rstrip(), "bar after scrolling: %r" % t.line(ROWS))
    check(t.line(ROWS - 1).strip() != bar_text().rstrip(), "the bar scrolled into row 31")
    check(g.cursor_row() <= ROWS - 1, "cursor entered the bar row: %d" % g.cursor_row())
    check(t.wraps == 0, "text wrapped past column 53")
    g.type("2+2\r")
    check(t.line(ROWS) == bar_text().rstrip(), "bar after a result")
    g.kill()


def test_signals(prog):
    reset = b"\x1b[r\x1b[0m\x1b[32;1H\x1b[2K"
    cases = [
        ("^C byte", "\x03", 130),
        ("SIGINT", signal.SIGINT, 130),
        ("SIGTERM", signal.SIGTERM, 143),
        ("SIGHUP", signal.SIGHUP, 129),
        ("SIGQUIT", signal.SIGQUIT, 131),
    ]
    for name, how, want in cases:
        g = Calc(prog)
        check(not (termios.tcgetattr(g.fd)[3] & termios.ICANON), "raw mode not entered")
        g.type("1+")
        if isinstance(how, str):
            os.write(g.fd, how.encode())
        else:
            os.kill(g.pid, how)
        status = g.reap()
        check(status == want, "%s: exit status %r, expected %d" % (name, status, want))
        check(restored(g), "%s: terminal left in raw mode" % name)
        check(
            g.term.raw.endswith(reset),
            "%s: reset sequence missing: %r" % (name, g.term.raw[-40:]),
        )
        check(
            (g.term.top, g.term.bot) == (0, ROWS - 1) and g.term.line(ROWS) == "",
            "%s: scroll region or bar left behind" % name,
        )
        g.kill()


def test_stop_continue(prog):
    # The pty child leads its own session, so its process group is orphaned
    # and Linux discards the stop the handler re-raises. The case therefore
    # checks the two halves around the stop: the terminal is restored when
    # SIGTSTP arrives, and cbreak mode, the screen and the line come back.
    reset = b"\x1b[r\x1b[0m\x1b[32;1H\x1b[2K"
    g = Calc(prog)
    check(not (termios.tcgetattr(g.fd)[3] & termios.ICANON), "raw mode not entered")
    g.type("1+")
    before = len(g.term.raw)
    os.kill(g.pid, signal.SIGTSTP)
    g.settle(0.3)
    check(reset in g.term.raw[before:], "SIGTSTP: reset sequence missing")
    os.kill(g.pid, signal.SIGCONT)
    g.settle(0.3)
    check(not (termios.tcgetattr(g.fd)[3] & termios.ICANON), "SIGCONT: cbreak mode not re-entered")
    check(g.reap(0.2) is None, "SIGTSTP ended the program")
    g.type("2")
    check(g.term.line(ROWS) == bar_text().rstrip(), "SIGCONT: item bar not redrawn")
    g.send("\r", 0.3)
    rows = g.find("> 1+2")
    check(rows and g.term.line(rows[-1] + 1) == "3", "SIGCONT: the edited line was lost")
    g.send("quit\r")
    check(g.reap() == 0, "quit after continue: exit status %r" % g.status)
    check(restored(g), "quit after continue: terminal left in raw mode")
    g.kill()


def test_exit_paths(prog):
    reset = b"\x1b[r\x1b[0m\x1b[32;1H\x1b[2K"
    for how in ("quit\r", "exit\r", "\x04", "F10"):
        g = Calc(prog)
        check(not (termios.tcgetattr(g.fd)[3] & termios.ICANON), "raw mode not entered")
        g.send(fkey_sequences()[9] if how == "F10" else how)
        check(g.reap() == 0, "%r: exit status %r" % (how, g.status))
        check(restored(g), "%r: terminal left in raw mode" % how)
        check(g.term.raw.endswith(reset), "%r: reset sequence missing" % how)
        g.kill()
    # end of input: the master closes while the program waits for a key
    g = Calc(prog)
    os.close(g.fd)
    end = time.time() + 3
    status = None
    while time.time() < end and status is None:
        pid, st = os.waitpid(g.pid, os.WNOHANG)
        if pid:
            status = os.waitstatus_to_exitcode(st)
        time.sleep(0.05)
    check(status in (0, 129), "hang-up exit status %r" % status)


def test_not_a_terminal(prog):
    # Standard input is a terminal and standard output is a pipe: line mode, no escapes.
    master, slave = pty.openpty()
    proc = subprocess.Popen([prog], stdin=slave, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    os.close(slave)
    os.write(master, b"2+2\n")
    time.sleep(0.3)
    attrs = termios.tcgetattr(master)
    check(bool(attrs[3] & termios.ICANON), "line mode changed the terminal modes")
    os.write(master, b"quit\n")
    out, _ = proc.communicate(timeout=5)
    check(out == b"4\n", "stdout pipe output %r" % out)
    check(proc.returncode == 0, "status %r" % proc.returncode)
    os.close(master)
    # -l keeps line mode on a terminal
    g = Calc(prog, ["-l"])
    g.send("2+2\r")
    check(b"\x1b" not in g.term.raw, "-l wrote an escape: %r" % g.term.raw)
    check(g.find("4"), "-l result")
    g.send("quit\r")
    check(g.reap() == 0, "-l exit status")
    # an expression argument runs without a prompt, bar or raw mode
    g = Calc(prog, ["2+3"])
    check(g.reap() == 0 and b"\x1b" not in g.term.raw and g.find("5"), "argument on a terminal")


def test_options(prog):
    g = Calc(prog, ["-r"])
    g.type("3 4 +\r")
    check(g.term.line(g.cursor_row()) == "rpn>" and g.find("rpn> 3 4 +"), "-r prompt")
    g.kill()
    g = Calc(prog, ["-p"])
    check(g.term.line(g.cursor_row()) == "prog>" and not any(g.term.attrs(ROWS, 1, 1)), "-p")
    g.kill()
    g = Calc(prog, ["-d"])
    check(g.term.line(ROWS) == bar_text(True).rstrip(), "-d bar: %r" % g.term.line(ROWS))
    g.kill()
    g = Calc(prog, ["-q"])
    check(g.reap() == 2, "unknown option status %r" % g.status)


CASES = [
    test_layout,
    test_evaluate,
    test_fkeys,
    test_fkey_variants,
    test_fkey_midline,
    test_editing,
    test_long_line,
    test_scroll,
    test_signals,
    test_stop_continue,
    test_exit_paths,
    test_not_a_terminal,
    test_options,
]


def main():
    prog = sys.argv[1] if len(sys.argv) > 1 else "./calc-host"
    want = sys.argv[2:]
    cases = [c for c in CASES if not want or c.__name__[5:] in want]
    if want and len(cases) != len(want):
        print("calc: unknown case in %r" % want)
        return 2
    for case in cases:
        try:
            case(prog)
        except AssertionError as e:
            print("calc: FAIL in %s: %s" % (case.__name__, e))
            return 1
        print("calc: %s ok" % case.__name__)
    print("calc: OK (%d cases, %d checks)" % (len(cases), checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
