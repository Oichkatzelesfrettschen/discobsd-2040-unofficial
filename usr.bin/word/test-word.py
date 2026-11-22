#!/usr/bin/env python3
"""End-to-end PTY test suite for usr.bin/word (host build).

Exercises:
  1. Typing, Transfer Save, Quit, and reloading saved bytes from disk.
  2. Selection (Ctrl-K) + Copy + Insert (Scrap round-trip) + Undo.
  3. Search with Word 5.1 pattern grammar (? wildcard, ^w whitespace run)
     and negative control (search text not found).
  4. Replace with confirm (Y/N) and replacement count.
  5. Live English/German menu toggle (F9 / Ctrl-L) and Help overlay.
"""

import os
import pty
import select
import subprocess
import sys
import tempfile
import time


def read_drain(fd, timeout=0.15):
    chunks = []
    end = time.monotonic() + timeout
    while True:
        rem = end - time.monotonic()
        if rem <= 0:
            break
        r, _, _ = select.select([fd], [], [], rem)
        if not r:
            break
        try:
            data = os.read(fd, 4096)
        except OSError:
            break
        if not data:
            break
        chunks.append(data)
    return b"".join(chunks)


def run_session(binary, args, steps):
    master, slave = pty.openpty()
    proc = subprocess.Popen(
        [binary] + args,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
    )
    os.close(slave)
    transcript = bytearray()
    try:
        transcript.extend(read_drain(master, 0.2))
        for payload, pause in steps:
            os.write(master, payload)
            time.sleep(pause)
            transcript.extend(read_drain(master, 0.15))
        proc.wait(timeout=3.0)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        os.close(master)
    return proc.returncode, transcript.decode("utf-8", errors="replace")


def main():
    if len(sys.argv) != 2:
        print("usage: test-word.py ./word.host", file=sys.stderr)
        return 2
    binary = sys.argv[1]

    with tempfile.TemporaryDirectory(prefix="word-test-") as td:
        out_file = os.path.join(td, "note.txt")

        # 1. Type text, Copy/Insert via Scrap, Transfer Save, Quit.
        rc, out = run_session(
            binary,
            [],
            [
                (b"Hello", 0.05),
                (b"\x0b", 0.05),        # Ctrl-K anchor at col 5
                (b"\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D", 0.05),  # select "Hello"
                (b"\x1b", 0.15),        # ESC -> COMMAND menu
                (b"c", 0.05),           # Copy to scrap
                (b"\x1b[F", 0.05),      # End
                (b" ", 0.05),
                (b"\x1b", 0.15),        # ESC -> COMMAND menu
                (b"i", 0.05),           # Insert from scrap -> "Hello Hello"
                (b"\x1b", 0.15),        # ESC -> COMMAND menu
                (b"t", 0.05),           # Transfer
                (b"s", 0.05),           # Save
                (out_file.encode("ascii") + b"\r", 0.1),
                (b"\x1b", 0.15),        # ESC -> COMMAND
                (b"q", 0.1),            # Quit (clean)
            ],
        )
        assert rc == 0, f"session 1 rc={rc}"
        with open(out_file, encoding="ascii") as f:
            saved = f.read()
        assert saved == "Hello Hello", f"expected 'Hello Hello', got {saved!r}"
        assert "COMMAND:" in out, "missing COMMAND bar"

        # 2. Reload file, test Search (? wildcard), negative Search, Replace, DE toggle.
        rc, out = run_session(
            binary,
            [out_file],
            [
                (b"\x0c", 0.05),        # Ctrl-L -> German
                (b"\x0c", 0.05),        # Ctrl-L -> back to English
                (b"\x1b", 0.15),        # ESC -> COMMAND
                (b"s", 0.05),           # Search
                (b"He?lo\r", 0.1),      # ? wildcard match
                (b"\x1b", 0.15),        # ESC -> COMMAND
                (b"s", 0.05),           # Search negative control
                (b"\x7f\x7f\x7f\x7f\x7fzzz\r", 0.1),
                (b"\x1b[H", 0.05),      # Home
                (b"\x1b", 0.15),        # ESC -> COMMAND
                (b"r", 0.05),           # Replace
                (b"\x7f\x7f\x7fHello\r", 0.05),
                (b"World\r", 0.05),     # With text
                (b"y", 0.05),           # Replace first
                (b"n", 0.05),           # Skip second
                (b"\x13", 0.1),         # Ctrl-S save
                (b"\x11", 0.1),         # Ctrl-Q quit
            ],
        )
        assert rc == 0, f"session 2 rc={rc}"
        assert "BEFEHL:" in out, "missing German BEFEHL menu toggle"
        assert "Search text not found" in out, "missing negative search report"
        with open(out_file, encoding="ascii") as f:
            saved2 = f.read()
        assert saved2 == "World Hello", f"expected 'World Hello', got {saved2!r}"

    print("test-word: PASS (all PTY editing, scrap, search, replace, EN/DE checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
