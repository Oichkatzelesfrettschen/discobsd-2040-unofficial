"""Drive the host build of kilo through a pty: open a file, type text,
move with arrow and page keys, search, save, quit, then diff the saved
file against what was typed.

This exercises everything kilo.c does except the board's own tty driver:
raw mode entry/exit, arrow-key and page-key cursor movement, incremental
search, and the save path (editorSave -> ftruncate + write). The arrow-key
and PAGE_UP checks matter here specifically because editorReadKey's
FIONREAD poll (replacing upstream's VTIME escape timeout, see kilo.c) is
compiled into this host build too -- only enableRawMode's ioctls differ
between the host and target builds -- so a passing run is real evidence
that multi-byte escape sequences decode correctly, not just that ESC by
itself does. Usage: test-pty.py PATH_TO_KILO_HOST

Run with ${PYTHON:-python3} after `bmake kilo.host` in this directory.
"""
import fcntl
import os
import pty
import re
import struct
import sys
import tempfile
import termios
import time

KILO = sys.argv[1] if len(sys.argv) > 1 else "./kilo.host"
WORKFILE = os.path.join(tempfile.gettempdir(), "kilo-test-pty-%d.txt" % os.getpid())

CTRL_S = "\x13"
CTRL_Q = "\x11"
CTRL_F = "\x06"
ESC = "\x1b"
ARROW_DOWN = "\x1b[B"
PAGE_UP = "\x1b[5~"

# kilo.c's editorSyntaxToColor emits a plain "\x1b[NNm" -- no bold prefix,
# no 256-color form. KEYWORD1 (int, return, ...) is yellow 33, MLCOMMENT
# is cyan 36; any of 31-37 covers the rest of the HL_* palette.
COLOR_ESCAPE_RE = re.compile(rb"\x1b\[3[1-7]m")
KEYWORD_COLOR_RE = re.compile(rb"\x1b\[33m")
COMMENT_COLOR_RE = re.compile(rb"\x1b\[36m")


def set_winsize(fd, rows=24, cols=80):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def read_available(fd, timeout=0.3):
    import select
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
            end = time.time() + 0.1
    return out


def send(fd, s):
    os.write(fd, s.encode("latin-1"))


def test_syntax_highlighting():
    """editorOpen calls editorSelectSyntaxHighlight(E.filename) before the
    first editorRefreshScreen, so opening a .c file paints its keywords
    and comments in color on the very first screen, with nothing typed.
    A .txt file opened with the same content hits HL_NORMAL for every
    character and paints no color at all -- editorSelectSyntaxHighlight
    matches C_HL_extensions (".c", ".h", ...) and nothing else."""
    c_content = "int main(void) {\n    /* a comment */\n    return 0;\n}\n"
    c_path = os.path.join(tempfile.gettempdir(),
                           "kilo-test-pty-%d.c" % os.getpid())
    txt_path = os.path.join(tempfile.gettempdir(),
                             "kilo-test-pty-%d.txt" % os.getpid())
    with open(c_path, "w") as f:
        f.write(c_content)
    with open(txt_path, "w") as f:
        f.write(c_content)

    try:
        for path in (c_path, txt_path):
            pid, master = pty.fork()
            if pid == 0:
                os.execv(KILO, [KILO, path])
                os._exit(127)

            set_winsize(master)
            time.sleep(0.2)
            screen = read_available(master)
            send(master, CTRL_Q)
            read_available(master)
            os.waitpid(pid, 0)
            os.close(master)

            if path == c_path:
                if not KEYWORD_COLOR_RE.search(screen):
                    print("FAIL: %s: no color escape around a C keyword "
                          "(int/return, expected \\x1b[33m)" % path)
                    sys.exit(1)
                if not COMMENT_COLOR_RE.search(screen):
                    print("FAIL: %s: no color escape around the /* */ "
                          "comment (expected \\x1b[36m)" % path)
                    sys.exit(1)
            else:
                if COLOR_ESCAPE_RE.search(screen):
                    print("FAIL: %s: a .txt file painted a syntax color "
                          "escape; HL_NORMAL should leave it plain" % path)
                    sys.exit(1)
    finally:
        os.unlink(c_path)
        os.unlink(txt_path)

    print("PASS: .c file highlights keywords and comments, "
          ".txt file stays plain")


def main():
    if os.path.exists(WORKFILE):
        os.unlink(WORKFILE)

    pid, master = pty.fork()
    if pid == 0:
        os.execv(KILO, [KILO, WORKFILE])
        os._exit(127)

    set_winsize(master)
    time.sleep(0.2)
    read_available(master)  # drain the initial screen paint

    # Four lines, no trailing Enter after the last one: numrows == 4 and
    # the cursor sits at the end of row 3 ("delta"), column 5.
    send(master, "alpha\rbravo\rcharlie\rdelta")
    read_available(master)

    # Page key: PAGE_UP always lands on row 0 regardless of file length
    # (unlike PAGE_DOWN, which upstream lets run past the last row and
    # pad with blank ones -- deliberate upstream behavior, not something
    # this test should fight). Column stays 5, clamped to "alpha"'s own
    # length, so inserting here only lands at the end of "alpha" if
    # PAGE_UP actually moved the cursor there.
    send(master, PAGE_UP)
    read_available(master)
    send(master, "X")
    read_available(master)

    # Arrow keys: walk back down row by row. Column is clamped to each
    # row's length as it goes (6 -> 5 on "bravo", stays 5 through
    # "charlie", clamps back to 5 on "delta"), so this ends exactly at
    # the end of "delta" -- proof ARROW_DOWN, not just PAGE_UP, moves the
    # cursor correctly.
    send(master, ARROW_DOWN * 3)
    read_available(master)

    # Incremental search for "charlie": confirm the match is found, then
    # leave with ESC, which restores the cursor to where it was before
    # the search started (the end of "delta") rather than the match.
    send(master, CTRL_F)
    read_available(master)
    send(master, "charlie")
    screen = read_available(master)
    if b"charlie" not in screen:
        print("FAIL: incremental search did not highlight 'charlie'")
        sys.exit(1)
    send(master, ESC)
    read_available(master)

    # This only appends onto "delta" if the search's cursor restore left
    # the cursor there, rather than at the match or at the end of file.
    send(master, " ZZ")
    read_available(master)

    # A fifth line, to prove Enter still works after all of the above.
    send(master, "\rfifth line new")
    read_available(master)

    # Save, then quit.
    send(master, CTRL_S)
    read_available(master)
    send(master, CTRL_Q)
    read_available(master)

    _, status = os.waitpid(pid, 0)
    os.close(master)

    if not os.path.exists(WORKFILE):
        print("FAIL: %s was never created" % WORKFILE)
        sys.exit(1)

    with open(WORKFILE) as f:
        content = f.read()

    expected_lines = [
        "alphaX",
        "bravo",
        "charlie",
        "delta ZZ",
        "fifth line new",
    ]
    actual_lines = content.split("\n")
    # editorRowsToString appends a trailing '\n' after every row, so the
    # split leaves one empty trailing element.
    if actual_lines and actual_lines[-1] == "":
        actual_lines.pop()

    print("exit status: %d" % status)
    print("saved file content:")
    print(content)

    if actual_lines != expected_lines:
        print("FAIL: content mismatch")
        print("expected: %r" % expected_lines)
        print("actual:   %r" % actual_lines)
        sys.exit(1)

    print("PASS: kilo.host wrote the expected content after "
          "type/page/arrow/search/save/quit")

    test_syntax_highlighting()


if __name__ == "__main__":
    main()
