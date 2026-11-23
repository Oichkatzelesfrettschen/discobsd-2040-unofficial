#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Drives the host uLisp on a pseudo-terminal and checks what the line editor paints.

usage: hilite-pty.py ULISP.HOST ULISP.DOC

A small terminal model (cursor, wrap with a pending last column, CHA, CUU,
ED and SGR, the sequences the editor uses and lcdterm.c accepts) reads the
bytes the interpreter writes and yields, for every cell, the character and
the class its attributes stand for. The cases check the colors of the real
builtin table, the match highlight, the redraw after backspace, kill and a
wrapped line, and that no escape sequence reaches a terminal that is dumb,
absent, or told with -n, nor a pipe.
"""

import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

SGR_CLASS = {
    "": ".",
    "1": "p",
    "32": "s",
    "90": "c",
    "35": "n",
    "93": "q",
    "95": "k",
    "94": "o",
    "36": "f",
    "33": "x",
    "41": "E",
}


class Screen:
    def __init__(self, cols, rows=40):
        self.cols, self.rows = cols, rows
        self.cells = [[(" ", ".") for _ in range(cols)] for _ in range(rows)]
        self.row = self.col = 0
        self.pending = False
        self.cls = "."
        self.rev = False

    def put(self, ch):
        if self.pending:
            self.col, self.row, self.pending = 0, self.row + 1, False
        self.cells[self.row][self.col] = (ch, self.cls.upper() if self.rev else self.cls)
        if self.col == self.cols - 1:
            self.pending = True
        else:
            self.col += 1

    def feed(self, data):
        text = data.decode("latin1")
        i = 0
        while i < len(text):
            c = text[i]
            if c == "\x1b" and text[i + 1] == "[":
                j = i + 2
                while not (0x40 <= ord(text[j]) <= 0x7E):
                    j += 1
                self.csi(text[i + 2 : j], text[j])
                i = j + 1
                continue
            if c == "\r":
                self.col, self.pending = 0, False
            elif c == "\n":
                self.row += 1
            elif c >= " ":
                self.put(c)
            i += 1

    def csi(self, params, final):
        n = int(params) if params.isdigit() else 1
        if final == "A":
            self.row -= n
            self.pending = False
        elif final == "B":
            self.row += n
            self.pending = False
        elif final == "G":
            self.col, self.pending = n - 1, False
        elif final == "J":
            for c in range(self.col, self.cols):
                self.cells[self.row][c] = (" ", ".")
            for r in range(self.row + 1, self.rows):
                self.cells[r] = [(" ", ".") for _ in range(self.cols)]
        elif final == "m":
            self.cls, self.rev = ".", False
            for p in params.split(";"):
                if p == "7":
                    self.rev = True
                elif p in SGR_CLASS:
                    self.cls = SGR_CLASS[p]
                elif p not in ("0", ""):
                    raise SystemExit("hilite-pty: unknown SGR %r" % p)
        else:
            raise SystemExit("hilite-pty: unknown sequence %r%s" % (params, final))

    def text(self, first_row):
        rows = []
        for r in range(first_row, self.row + 1):
            rows.append("".join(ch for ch, _ in self.cells[r]).rstrip())
        return rows

    def classes(self, row, start, length):
        return "".join(k for _, k in self.cells[row][start : start + length])


def session(binary, doc, keys, term="xterm", args=(), cols=80, gap=0.04):
    """Types keys at the prompt of a fresh interpreter; returns the bytes."""
    pid, fd = pty.fork()
    if pid == 0:
        env = {"ULISPDOC": doc, "PATH": os.environ.get("PATH", "/usr/bin")}
        if term is not None:
            env["TERM"] = term
        os.execve(binary, [binary, "-w", "4000", *args], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, cols, 0, 0))
    out = b""

    def drain(seconds, quiet=None, until=None):
        """Reads for seconds; with quiet, until the output has paused that
        long (and until, when given, has appeared), seconds being the bound.
        A loaded host starts and repaints late, so the first and the last
        read wait for the interpreter instead of for a fixed time."""
        nonlocal out
        now = time.time()
        end = now + seconds
        last = now
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.02)
            if ready:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    return
                if not chunk:
                    return
                out += chunk
                last = time.time()
            elif quiet is not None and time.time() - last >= quiet:
                if until is None or until in out:
                    return

    drain(20.0, quiet=0.4, until=b"> ")
    for key in keys:
        os.write(fd, key)
        drain(gap)
    drain(20.0, quiet=0.5)
    os.kill(pid, 9)
    os.waitpid(pid, 0)
    return out


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    binary, doc = argv[1], argv[2]
    failures = []

    def expect(name, got, want):
        if got != want:
            failures.append("%s\n  want %r\n  got  %r" % (name, want, got))

    def painted(keys, cols=80):
        screen = Screen(cols)
        screen.feed(session(binary, doc, keys, cols=cols))
        return screen

    prompt = len("4000> ")
    # The prompt is on row 1, after the banner line. Each case names its typed
    # text, then the classes of its characters, from the real builtin table; a
    # capital is a character in reverse video, and a quote and a character
    # literal share one color.
    cases = [
        (
            "builtin classes",
            [b"(defun foo (x) (car x))"],
            "P" + "ooooo" + "." + "..." + "." + "p.p" + "." + "p" + "fff" + "." + "." + "pp",
        ),
        (
            "constants, keyword, string, number, char, comment",
            [b'nil :k "s;" 12 #\\a ; c'],
            "xxx.kk.ssss.nn.qqq.ccc",
        ),
        ("a symbol turns into a function as it is typed", [b"ca", b"r"], "fff"),
        ("the closer shows its opener", [b"(a (b))"], "P..p.pp"),
        ("the highlight goes with the next key", [b"(a (b))", b" "], "p..p.pp."),
        ("backspace recolors", [b"(car", b"\x7f", b"d"], "p" + "..."),
        ("a closer with nothing open", [b"())"], "ppE"),
        ("kill line", [b"(foo bar", b"\x15", b"1"], "n"),
        (
            "an arrow key is dropped",
            [b"(+ 1", b"\x1b[A", b" 2)"],
            "P" + "f" + "." + "n" + "." + "n" + "p",
        ),
    ]
    for name, keys, classes in cases:
        screen = painted(keys)
        row = 1
        want_len = len(classes)
        expect(name, screen.classes(row, prompt, want_len), classes)
    # Backspace and kill leave the typed text, not what was erased.
    screen = painted([b"(car", b"\x7f", b"d"])
    expect("backspace text", screen.text(1)[0], "4000> (cad")
    screen = painted([b"(foo bar", b"\x15", b"1"])
    expect("kill text", screen.text(1)[0], "4000> 1")
    # A line wider than the terminal repaints across its rows.
    typed = b"(list 'aaaa 'bbbb 'cccc 'dddd 'eeee"
    for cut in (0, 1, 6, 13):
        keys = [typed] + ([b"\x7f" * cut] if cut else [])
        screen = painted(keys, cols=20)
        flat = "".join(row.ljust(20) for row in screen.text(1)).rstrip()
        expect(
            "wrap, %d erased" % cut,
            flat,
            ("4000> " + typed[: len(typed) - cut].decode()).ljust(len(flat)),
        )
    screen = painted([typed, b"\x15"], cols=20)
    expect("wrap then kill", screen.text(1), ["4000>"])
    # Moving inside the line: the text, the colors and where the cursor stands.
    left, right, home, end, delete = b"\x1b[D", b"\x1b[C", b"\x1b[H", b"\x1b[F", b"\x1b[3~"
    inside = [
        ("insert after two left keys", [b"(list 1 3)", left * 2, b"2 "], "(list 1 2 3)", 10),
        ("home, then insert", [b"1 2)", home, b"(+ "], "(+ 1 2)", 3),
        ("end after home", [b"(+ 1 2)", home, end, b"x"], "(+ 1 2)x", 8),
        ("delete under the cursor", [b"(+ 1 22)", left * 2, delete], "(+ 1 2)", 6),
        ("backspace in the middle", [b"(+ 1 22)", left * 2, b"\x7f"], "(+ 1 2)", 5),
        ("right stops at the end", [b"(+ 1 2)", right * 3], "(+ 1 2)", 7),
        ("left stops at the start", [b"ab", left * 5, b"c"], "cab", 1),
        ("home and end by control keys", [b"(a)", b"\x01", b"x", b"\x05", b"y"], "x(a)y", 5),
        ("delete at the end does nothing", [b"(a)", delete], "(a)", 3),
        ("application cursor keys", [b"(a b)", b"\x1bOD" * 2, b"c"], "(a cb)", 4),
    ]
    for name, keys, text, pos in inside:
        screen = painted(keys)
        expect(name, screen.text(1)[0], "4000> " + text)
        expect(name + ", cursor", (screen.row, screen.col), (1, prompt + pos))
    screen = painted([b"(+ 1 22)", left * 2, delete])
    expect(
        "colors after delete", screen.classes(1, prompt, 7), "p" + "f" + "." + "n" + "." + "n" + "p"
    )
    # The same across the rows of a wrapped line: 6 columns of prompt, 20 wide.
    long = b"(list 'aaaa 'bbbb 'cccc 'dddd"
    for name, keys, text, pos in (
        ("wrap: home, then insert", [long, home, b"("], "(" + long.decode(), 1),
        ("wrap: home, end", [long, home, end], long.decode(), len(long)),
        ("wrap: end of a full row", [b"(list 'aaaa 'b", home, end, b"c"], "(list 'aaaa 'bc", 15),
        (
            "wrap: left over a row",
            [long, left * 20, b"X"],
            long[:-20].decode() + "X" + long[-20:].decode(),
            len(long) - 19,
        ),
        (
            "wrap: delete across rows",
            [long, left * 12, delete * 3],
            long[:-12].decode() + long[-9:].decode(),
            len(long) - 12,
        ),
    ):
        screen = painted(keys, cols=20)
        flat = "".join("".join(ch for ch, _ in screen.cells[r]) for r in range(1, 6)).rstrip()
        expect(name, flat, ("4000> " + text).ljust(len(flat)))
        at = 6 + pos
        expect(name + ", cursor", (screen.row, screen.col), (1 + at // 20, at % 20))
    # The value the loop prints is colored by the same classes.
    screen = painted([b'(list 1 "a" \'car nil)\r'])
    expect(
        "value colors",
        screen.classes(3, 0, 15),
        "p" + "n" + "." + "sss" + "." + "fff" + "." + "xxx" + "p",
    )
    # No escape sequence where the terminal cannot take it.
    for name, term, args in (("dumb", "dumb", ()), ("no TERM", None, ()), ("-n", "xterm", ("-n",))):
        out = session(binary, doc, [b"(car '(1 2))\r"], term=term, args=args)
        expect("no escapes with " + name, b"\x1b" in out, False)
        expect("result with " + name, b"\r\n1\r\n" in out, True)
    piped = subprocess.run(
        [binary, "-w", "4000"],
        input=b"(car '(1 2))\n",
        capture_output=True,
        env={"TERM": "xterm", "ULISPDOC": doc},
        check=False,
    ).stdout
    expect("no escapes in a pipe", b"\x1b" in piped, False)
    if failures:
        print("\n".join("hilite-pty: " + f for f in failures), file=sys.stderr)
        return 1
    print("hilite-pty: the editor paints what the table says")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
