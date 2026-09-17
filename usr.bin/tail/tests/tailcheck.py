#!/usr/bin/env python3
"""Check a tail binary against a model of its output.

usage: PYTHON tailcheck.py TAIL [--small]

The model is the behavior of the 2.11BSD tail: -n prints the bytes after
the (n+1)-th newline from the end, -nc the last n bytes, -nb the last
n*512 bytes, +n the bytes after the (n-1)-th newline, +nc from byte n,
-r the last lines last first with a newline supplied to a final line
that lacks one and one newline for an empty input, and -0 nothing.
Inputs run from empty to several hundred kilobytes with lines longer
than any buffer, through a file argument and through a pipe. --small
keeps every input under 30000 bytes, the capacity of the old buffer.
"""
import os
import random
import subprocess
import sys
import tempfile


def model(data, opt):
    fromend = not opt.startswith("+")
    body = opt[1:] if opt else "10l"
    digits = ""
    while body and body[0].isdigit():
        digits += body[0]
        body = body[1:]
    n = int(digits) if digits else -1
    if not fromend and n > 0:
        n -= 1
    bylines = None
    bkwds = False
    for c in body:
        if c == "b":
            if n == -1:
                n = 1
            n <<= 9
            bylines = False
        elif c == "c":
            bylines = False
        elif c == "l":
            bylines = True
        elif c == "r":
            bkwds = True
            fromend = True
            bylines = True
    if n == -1:
        n = -1 if bkwds else 10
    if bylines is None:
        bylines = True
    if not fromend:
        if bylines:
            pos = 0
            for _ in range(n):
                i = data.find(b"\n", pos)
                if i < 0:
                    return b""
                pos = i + 1
            return data[pos:]
        return data[n:]
    if bkwds:
        if n == 0:
            return b""
        if not data:
            return b"\n"
        lines = data.split(b"\n")
        if lines[-1] == b"":
            lines.pop()
        lines = lines[::-1]
        if n >= 0:
            lines = lines[:n]
        return b"".join(line + b"\n" for line in lines)
    if n <= 0:
        return b""
    if not bylines:
        return data[-n:] if n < len(data) else data
    pos = len(data)
    for _ in range(n + 1):
        i = data.rfind(b"\n", 0, pos)
        if i < 0:
            return data
        pos = i
    return data[pos + 1:]

def corpus(small):
    random.seed(11)
    yield b""
    yield b"x"
    yield b"\n"
    yield b"a\nb\nc\n"
    yield b"a\nb\nc"
    yield b"\n\n\n"
    sizes = [0, 1, 5, 40, 200, 1023, 1024, 1025, 2500, 5000]
    if not small:
        sizes += [40000, 70000]
    for _ in range(60 if small else 30):
        lines = [bytes(random.choice(b"abcdefgh ") for _ in range(random.choice(sizes)))
                 for _ in range(random.randint(1, 80))]
        data = b"\n".join(lines) + (b"\n" if random.random() < 0.8 else b"")
        if small and len(data) >= 30000:
            continue
        yield data

def main():
    tail = sys.argv[1]
    small = "--small" in sys.argv
    opts = ["", "-1", "-3", "-10", "-100", "-0", "-1000", "+1", "+2", "+5", "+100", "-5c", "+5c",
            "+1c", "-1b", "+1b", "-r", "-3r", "-1r", "-0r", "-1c", "-0c", "-2l", "+3l", "-1000c",
            "+30000c", "-50000c", "-40"]
    cases = bad = 0
    with tempfile.NamedTemporaryFile(delete=False) as f:
        path = f.name
    try:
        for data in corpus(small):
            for opt in opts:
                want = model(data, opt)
                args = [opt] if opt else []
                with open(path, "wb") as f:
                    f.write(data)
                got = [subprocess.run([tail] + args + [path], capture_output=True, timeout=30),
                       subprocess.run([tail] + args, input=data, capture_output=True, timeout=30)]
                for how, p in zip(("file", "pipe"), got):
                    cases += 1
                    if p.returncode != 0 or p.stdout != want:
                        bad += 1
                        if bad <= 10:
                            print("FAIL opt=%r via %s len=%d rc=%d got %d bytes, want %d: %r vs %r"
                                  % (opt, how, len(data), p.returncode, len(p.stdout), len(want),
                                     p.stdout[:40], want[:40]))
    finally:
        os.unlink(path)
    print("tail: %d cases, %d failures" % (cases, bad))
    sys.exit(1 if bad else 0)

main()
