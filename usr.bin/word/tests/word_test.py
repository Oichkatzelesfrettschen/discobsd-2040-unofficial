#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# PTY tests of usr.bin/word (SPEC.md) on the host build. Each session runs
# the program on a 53x32 pseudo-terminal, the PicoCalc console's size, and
# reads its output through a small ECMA-48 screen model: cursor addressing,
# erase, SGR and the DEC line-drawing switch, which are all the program
# sends. A check reads the modeled screen or the bytes the program saved,
# and every feature has a case that must succeed and one that must fail.
#
# usage: word_test.py ./word.host

import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time

ROWS, COLS = 32, 53
ESC = b"\x1b"
SHIFT, CTRLP = b"\x1a", b"\x03"
F1, F4, F6 = b"\x1bOP", b"\x1bOS", b"\x1b[17~"
DEL, INS = b"\x1b[3~", b"\x1b[2~"
LEFT, RIGHT, UP, DOWN = b"\x1b[D", b"\x1b[C", b"\x1b[A", b"\x1b[B"


class Fail(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Fail(what)


class Screen:
    def __init__(self):
        self.cells = [[" "] * COLS for _ in range(ROWS)]
        self.attrs = [[0] * COLS for _ in range(ROWS)]
        self.r = self.c = 0
        self.acs = False
        self.sgr = 0
        self.raw = bytearray()

    def feed(self, data):
        self.raw.extend(data)
        i = 0
        while i < len(data):
            b = data[i]
            if b == 0x1B:
                m = re.compile(rb"\x1b(\[[0-9;?]*[A-Za-z@]|[()][0B]|.)").match(data, i)
                if m is None:
                    break
                self.esc(m.group(1))
                i = m.end()
                continue
            if b == 13:
                self.c = 0
            elif b == 10:
                self.r = min(self.r + 1, ROWS - 1)
            elif 32 <= b < 127:
                ch = chr(b)
                if self.acs:
                    ch = {"`": "*", "q": "-", "x": "|"}.get(ch, "+")
                if self.r < ROWS and self.c < COLS:
                    self.cells[self.r][self.c] = ch
                    self.attrs[self.r][self.c] = self.sgr
                self.c += 1
            i += 1

    def esc(self, s):
        if s == b"(0":
            self.acs = True
        elif s == b"(B":
            self.acs = False
        elif s.startswith(b"[") and s.endswith(b"H") and b"?" not in s:
            p = s[1:-1].split(b";")
            self.r = int(p[0]) - 1 if p[0] else 0
            self.c = int(p[1]) - 1 if len(p) > 1 and p[1] else 0
        elif s == b"[2J":
            self.cells = [[" "] * COLS for _ in range(ROWS)]
        elif s.startswith(b"[") and s.endswith(b"m"):
            # Bit 0 bold, bit 1 underline, bit 2 reverse; color kept above bit 8.
            for p in s[1:-1].split(b";") if s[1:-1] else [b"0"]:
                v = int(p) if p else 0
                if v == 0:
                    self.sgr = 0
                elif v == 1:
                    self.sgr |= 1
                elif v == 4:
                    self.sgr |= 2
                elif v == 7:
                    self.sgr |= 4
                elif 30 <= v <= 37 or 90 <= v <= 97:
                    self.sgr = (self.sgr & 0xFF) | (v << 8)

    def row(self, n):
        return "".join(self.cells[n])

    def text(self):
        return "\n".join(self.row(n) for n in range(ROWS))


class Session:
    def __init__(self, binary, args=(), cwd=None, env=None):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        e = dict(os.environ)
        e.pop("WORDKEYS", None)
        e.update(env or {})
        self.proc = subprocess.Popen(
            [binary] + list(args),
            stdin=slave,
            stdout=slave,
            stderr=slave,
            close_fds=True,
            cwd=cwd,
            env=e,
        )
        os.close(slave)
        self.fd = master
        self.scr = Screen()
        self.pump(1.0)

    def pump(self, limit=2.0, quiet=0.12):
        end = time.monotonic() + limit
        while time.monotonic() < end:
            r, _, _ = select.select([self.fd], [], [], quiet)
            if not r:
                return
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                return
            if not data:
                return
            self.scr.feed(data)

    def send(self, *parts):
        for p in parts:
            before = bytes(self.scr.raw)
            os.write(self.fd, p)
            self.pump()
            if p == ESC:
                # A lone ESC is decided after a 60 ms wait for a sequence;
                # the next key goes only once the menu has been drawn.
                self.wait_change(before)

    def wait_change(self, before, limit=3.0):
        end = time.monotonic() + limit
        while bytes(self.scr.raw) == before and time.monotonic() < end:
            self.pump(0.5, 0.05)

    def has(self, s):
        return s in self.scr.text()

    def close(self, expect_exit=True):
        try:
            rc = self.proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
            rc = None
        os.close(self.fd)
        if expect_exit:
            check(rc == 0, "program exited with %r" % (rc,))
        return rc


def read(path):
    with open(path, "rb") as f:
        return f.read()


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def menu(s, *keys):
    s.send(ESC)
    s.send(*keys)


def t_save_menu(b, td):
    out = os.path.join(td, "note.txt")
    s = Session(b, [out])
    check(s.has("COMMAND: Copy Delete Format"), "COMMAND menu missing")
    check(s.has("Transfer Undo Window"), "COMMAND menu does not wrap at 53 columns")
    s.send(b"Hello world")
    check("{}" in s.scr.text(), "empty scrap not shown as {}")
    menu(s, b"t", b"s")
    check(s.has("TRANSFER SAVE"), "Transfer Save form missing")
    s.send(b"\r")
    check(read(out) == b"Hello world", "saved bytes %r" % read(out))
    check(not os.path.exists(out + ".wtmp"), "temporary file left beside the target")
    # Known-bad: a save into a missing directory reports and writes nothing.
    menu(s, b"t", b"s")
    s.send(os.path.join(td, "nodir", "x.txt").encode(), b"\r")
    check(s.has("No such file or directory"), "failed save not reported")
    check(not os.path.exists(os.path.join(td, "nodir")), "failed save created a path")
    menu(s, b"q")
    s.close()
    # Known-bad: loading a directory is refused and keeps the document.
    s = Session(b, [out])
    menu(s, b"t", b"l")
    s.send(td.encode(), b"\r")
    check(s.has("Invalid argument"), "directory load not refused")
    check(s.has("Hello world"), "document lost after a refused load")
    menu(s, b"q")
    s.close()


def t_wrap(b, td):
    words = ["w%02d" % i + "x" * (i % 5) for i in range(60)]
    para = " ".join(words)
    path = os.path.join(td, "wrap.txt")
    write(path, (para + "\nsecond paragraph\n").encode())
    s = Session(b, [path])
    rows = [s.scr.row(n).rstrip() for n in range(12)]
    joined = []
    for r in rows:
        if r.startswith("second"):
            break
        check(len(r) <= COLS, "row wider than the screen")
        joined.extend(r.split())
    check(joined == words, "wrapped rows split or lose a word")
    check(len([r for r in rows if r and not r.startswith("second")]) >= 5, "paragraph did not wrap")
    # Wrap off: the first row is cut at the edge and the second paragraph follows.
    menu(s, b"o")
    s.send(b"\t", b"n", b"\r")
    check(s.scr.row(0) == para[:COLS], "wrap off does not cut the line")
    check(s.scr.row(1).startswith("second"), "wrap off kept the wrapped rows")
    menu(s, b"q")
    s.close()


def t_search_replace(b, td):
    path = os.path.join(td, "sr.txt")
    write(path, b"one two one two one")
    s = Session(b, [path])
    menu(s, b"s")
    s.send(b"t?o\r")
    check(not s.has("not found"), "wildcard search missed")
    check(s.has("Pg1 Li1 Co8"), "search did not move to the match end")
    menu(s, b"s")
    s.send(b"zzz\r")
    check(s.has("Search text not found"), "missing search not reported")
    # Case Yes: ONE is not in the text.
    menu(s, b"s")
    s.send(b"ONE", b"\t", b"\t", b"y", b"\r")
    check(s.has("Search text not found"), "case-sensitive search ignored case")
    s.send(b"\x1b[1;5H")
    s.send(CTRLP, b"\x15")
    menu(s, b"r")
    check(s.has("REPLACE"), "Replace form missing")
    s.send(b"one", b"\t", b"1", b"\t", b"\t", b"n", b"\r")
    s.send(b"y", b"n", b"a")
    check(s.has("2 replacements"), "replacement count wrong")
    menu(s, b"t", b"s")
    s.send(b"\r")
    check(read(path) == b"1 two one two 1", "replace result %r" % read(path))
    menu(s, b"q")
    s.close()


def t_undo_scrap(b, td):
    path = os.path.join(td, "u.txt")
    s = Session(b, [path])
    s.send(b"abc", LEFT, b"X")
    s.send(SHIFT, F1)
    s.send(SHIFT, F1)
    check(not s.has("abc"), "undo did not remove the typed text")
    s.send(b"\x19")
    s.send(b"\x19")
    s.send(F6, LEFT, LEFT, DEL)
    check(s.has("{bX}"), "cut text not shown in the scrap")
    s.send(CTRLP, b"\x04", INS)
    s.send(CTRLP, b"\x15", SHIFT, b"\x18")
    check(s.has("{bX}"), "Shift+Del changed the scrap")
    menu(s, b"t", b"s")
    s.send(b"\r")
    check(read(path) == b"cbX", "undo/scrap result %r" % read(path))
    menu(s, b"q")
    s.close()


def t_vi(b, td):
    path = os.path.join(td, "v.txt")
    s = Session(b, ["-k", "vi", path])
    s.send(b"iHello", ESC, b"o", b"world", ESC)
    check(s.has("-- INSERT --") is False, "insert mode did not end")
    s.send(b"yy", b"p")
    s.send(b":q\r")
    check(s.has("No write since last change"), ":q left a modified document")
    s.send(b"gg", b"dd", b"x")
    s.send(b":wq\r")
    s.close()
    check(read(path) == b"orld\nworld", "vi result %r" % read(path))
    s = Session(b, ["-k", "vi", path])
    s.send(b"l", b"j")
    check(s.scr.row(31).find("Li2 Co2") >= 0, "vi j lost the column")
    s.send(b"k", b"cwX", ESC)
    s.send(b":menu\r")
    check(s.has("COMMAND: Copy Delete Format"), ":menu did not show the COMMAND menu")
    s.send(ESC)
    s.send(b"vlld", b"u", b"\x12", b":x\r")
    s.close()
    check(read(path) == b"o\nworld", "vi cw/visual/redo result %r" % read(path))


def t_nano(b, td):
    path = os.path.join(td, "n.txt")
    write(path, b"a\nb\nc\n")
    s = Session(b, ["-k", "nano", path])
    check(s.has("^G Help") and s.has("^X Exit"), "nano hints missing")
    s.send(b"\x0b", b"\x0b")
    s.send(DOWN, b"\x15")
    s.send(b"\x0f", b"\r")
    s.send(b"\x18")
    s.close()
    check(read(path) == b"c\na\nb\n", "nano cut/paste result %r" % read(path))
    # Known-bad: ^X on a modified buffer asks before leaving.
    s = Session(b, ["-k", "nano", path])
    s.send(b"z", b"\x18")
    check(s.has("Save changes?"), "nano exit did not ask")
    s.send(b"n")
    s.close()
    check(read(path) == b"c\na\nb\n", "discarded edit reached the file")


def t_picker(b, td):
    d = os.path.join(td, "pick")
    os.mkdir(d)
    os.mkdir(os.path.join(d, "sub"))
    write(os.path.join(d, "alpha.txt"), b"alpha text")
    write(os.path.join(d, "beta.txt"), b"beta text")
    s = Session(b, [], cwd=d)
    menu(s, b"t", b"l")
    s.send(F1)
    rows = [s.scr.row(n).strip() for n in range(6)]
    check(rows[1:5] == ["../", "sub/", "alpha.txt", "beta.txt"], "picker order %r" % rows)
    s.send(ESC)
    check(s.has("TRANSFER LOAD"), "Esc in the picker did not return to the form")
    s.send(F1, b"b", b"\r")
    check(s.has("beta text") and s.has("beta.txt"), "picked file not loaded")
    menu(s, b"q")
    s.close()


def t_large(b, td):
    big = os.path.join(td, "big.txt")
    data = b"".join(b"line %06d of the spill test\n" % i for i in range(12000))
    write(big, data)
    out = os.path.join(td, "big-out.txt")
    s = Session(b, [out])
    menu(s, b"t", b"m")
    s.send(big.encode(), b"\r")
    check(s.has("Merged %d bytes" % len(data)), "merge not reported")
    s.send(CTRLP, b"\x04", b"END")
    s.send(CTRLP, b"\x15", b"START")
    menu(s, b"j", b"p")
    s.send(b"50\r", b"MID")
    s.send(SHIFT, F1)
    menu(s, b"t", b"s")
    s.send(b"\r")
    check(read(out) == b"START" + data + b"END", "large document bytes differ")
    # Edit the saved file in place: the save renames over the loaded file.
    menu(s, b"t", b"l")
    s.send(out.encode(), b"\r")
    s.send(DEL, DEL, DEL, DEL, DEL)
    menu(s, b"t", b"s")
    s.send(b"\r")
    check(read(out) == data + b"END", "save over the loaded file differs")
    menu(s, b"q")
    s.close()


def t_ansi(b, td):
    s = Session(b, [os.path.join(td, "a.txt")])
    check(b"\x1b(0`" in s.scr.raw, "end mark not drawn with the line-drawing set")
    check(b";44;97m" in s.scr.raw, "status line not colored")
    menu(s, b"q")
    s.close()
    s = Session(b, ["-c", os.path.join(td, "a.txt")])
    check(b";44;97m" not in s.scr.raw, "color off still sends colors")
    menu(s, b"q")
    s.close()
    # Known-bad: a language option no longer exists and is refused.
    s = Session(b, ["-l", "de", os.path.join(td, "a.txt")])
    check(s.close(expect_exit=False) == 2, "-l was accepted")


SHIFT_LEFT = b"\x1b[1;2D"
ALT = b"\x01"


def t_format(b, td):
    s = Session(b, [os.path.join(td, "f.txt")])
    s.send(b"plain bold")
    for _ in range(4):
        s.send(SHIFT_LEFT)
    s.send(ALT, b"b")
    s.send(b"\x1b[F")
    row = s.scr.row(0)
    check(row.startswith("plain bold"), "text missing: %r" % row)
    check(all(s.scr.attrs[0][c] & 1 for c in range(6, 10)), "selection not bold")
    check(not any(s.scr.attrs[0][c] & 1 for c in range(0, 5)), "bold leaked")
    # Typing after a speed key with no selection uses the typing format.
    s.send(ALT, b"u", b"X")
    check(s.scr.attrs[0][10] & 2 and s.scr.row(0)[10] == "X", "typing format not underlined")
    # Undo removes the typed X, then the bold.
    s.send(SHIFT, F1, SHIFT, F1)
    check(not any(s.scr.attrs[0][c] & 1 for c in range(6, 10)), "undo kept the bold")
    s.send(b"\x19")
    check(all(s.scr.attrs[0][c] & 1 for c in range(6, 10)), "redo lost the bold")
    # Format Paragraph: centered, then a bad measurement is refused.
    menu(s, b"f", b"p")
    check(s.has("FORMAT PARAGRAPH") and s.has("alignment:"), "Format Paragraph form missing")
    s.send(b"c", b"\r")
    check(s.scr.row(0).index("plain") == (COLS - 10) // 2, "paragraph not centered")
    menu(s, b"f", b"p")
    s.send(b"\t", b"abc", b"\r")
    check(s.has("Not a valid measurement"), "bad indent accepted")
    s.send(ALT, b"l")
    check(s.scr.row(0).startswith("plain"), "Alt+L did not align left")
    # Left indent 1" wraps the paragraph ten columns in.
    s.send(b"\x1b[F", b" " + b"word " * 20)
    menu(s, b"f", b"p")
    s.send(b"\t", b"1", b"\r")
    rows = [s.scr.row(n) for n in range(4)]
    check(all(r.startswith(" " * 10) and r[10] != " " for r in rows[:3]), "indent rows %r" % rows)
    words = [w for w in " ".join(r.strip() for r in rows).split() if w != "*"]
    check(words == ["plain", "bold"] + ["word"] * 20, "indented wrap lost words %r" % words)
    # Format Character fields reflect the selection and set italic.
    s.send(CTRLP, b"\x15", b"\x1b[1;2C" * 5)
    menu(s, b"f", b"c")
    check(s.has("FORMAT CHARACTER") and s.has("italic:"), "Format Character form missing")
    s.send(b"\t", b"y", b"\r", RIGHT)
    check(all((s.scr.attrs[0][c + 10] >> 8) == 32 for c in range(5)), "italic not shown")
    check(not s.scr.attrs[0][16] >> 8, "italic past the selection")
    menu(s, b"q")
    s.send(b"n")
    s.close()


TESTS = [
    t_format,
    t_save_menu,
    t_wrap,
    t_search_replace,
    t_undo_scrap,
    t_vi,
    t_nano,
    t_picker,
    t_large,
    t_ansi,
]


def main():
    if len(sys.argv) != 2:
        print("usage: word_test.py ./word.host", file=sys.stderr)
        return 2
    binary = os.path.abspath(sys.argv[1])
    failed = 0
    for t in TESTS:
        with tempfile.TemporaryDirectory(prefix="word-test-") as td:
            try:
                t(binary, td)
                print("PASS", t.__name__)
            except Fail as e:
                failed += 1
                print("FAIL", t.__name__ + ":", e)
            except (OSError, subprocess.SubprocessError, ValueError) as e:
                failed += 1
                print("FAIL", t.__name__ + ": " + type(e).__name__, e)
    if failed:
        print("word_test: %d of %d failed" % (failed, len(TESTS)))
        return 1
    print("word_test: all %d passed" % len(TESTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
