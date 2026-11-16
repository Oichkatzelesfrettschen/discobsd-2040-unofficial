#!/usr/bin/env python3
# Host tests of picofetch-host and picotop-host over the fixture readings of
# hwinfo_fixture.c. A terminal model applies what the programs write (cursor
# addressing, erase, SGR colors, the DEC line set, bank codes 0xa0-0xff) to a
# grid of cells, and fails on a character written outside the grid, so the
# checks read the screen, its colors and its glyph codes. The glyph shapes are
# read from the upload log the host build writes (PCDRAW_GLYPHLOG) and compared
# with the shapes pcdraw.h documents, stated here independently.
#
# usage: render.py ./picofetch-host [../picotop/picotop-host [--dump DIR]]
#   --dump DIR writes each bank-tier screen as a PPM image, for a reader

import os
import pty
import re
import select
import subprocess
import sys
import tempfile
import termios
import time

ROWS, COLS = 32, 53
checks = 0

# sys/arch/rp2040/dev/st7365p.c lcd_palette is not read here: the dump is a
# reading aid, and these are the usual sixteen ANSI colors.
PALETTE = [
    (0, 0, 0), (205, 0, 0), (0, 205, 0), (205, 205, 0), (0, 0, 238), (205, 0, 205),
    (0, 205, 205), (229, 229, 229), (127, 127, 127), (255, 0, 0), (0, 255, 0),
    (255, 255, 0), (92, 92, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise AssertionError(msg)


class Screen:
    def __init__(self, rows=ROWS, cols=COLS):
        self.rows, self.cols = rows, cols
        self.cell = [[(32, 7, 0, False)] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.fg, self.bg, self.rev, self.dec = 7, 0, False, False
        self.sgr = []

    def put(self, code):
        if self.r >= self.rows:
            raise AssertionError(f"row {self.r} is below the screen")
        if self.c >= self.cols:
            raise AssertionError(f"row {self.r} overflows column {self.cols}")
        if self.dec and code == ord("q"):
            code = 0x100 + code
        self.cell[self.r][self.c] = (code, self.fg, self.bg, self.rev)
        self.c += 1

    def feed(self, data):
        i = 0
        while i < len(data):
            b = data[i]
            if b == 0x1B:
                m = re.match(rb"\x1b\[([0-9;]*)([A-Za-z])", data[i:])
                if m:
                    self.csi(m.group(1).decode(), m.group(2).decode())
                    i += m.end()
                    continue
                m = re.match(rb"\x1b\(([0B])", data[i:])
                check(m is not None, f"unknown escape at {i}: {data[i:i + 8]!r}")
                self.dec = m.group(1) == b"0"
                i += m.end()
                continue
            i += 1
            if b == 0x0A:
                self.r, self.c = self.r + 1, 0
            elif b == 0x0D:
                self.c = 0
            else:
                if not (0x20 <= b <= 0x7E or b >= 0xA0):
                    raise AssertionError(f"byte {b:#x} is no cell code")
                self.put(b)

    def csi(self, args, final):
        n = [int(x) if x else 0 for x in args.split(";")] if args else [0]
        if final == "m":
            for v in n:
                self.sgr.append(v)
                if v == 0:
                    self.fg, self.bg, self.rev = 7, 0, False
                elif v == 7:
                    self.rev = True
                elif 30 <= v <= 37:
                    self.fg = v - 30
                elif 90 <= v <= 97:
                    self.fg = v - 90 + 8
                elif 40 <= v <= 47:
                    self.bg = v - 40
                else:
                    if v != 1:
                        raise AssertionError(f"SGR {v} is outside the console's set")
        elif final == "H":
            self.r = max(n[0], 1) - 1
            self.c = (n[1] if len(n) > 1 else 1) - 1
        elif final == "K":
            # After the last cell is written the cursor stays on it
            # (pending wrap), so the erase starts there.
            for c in range(min(self.c, self.cols - 1), self.cols):
                self.cell[self.r][c] = (32, 7, 0, False)
        elif final == "J":
            check(n[0] == 2, "only erase-display 2 is used")
            self.cell = [[(32, 7, 0, False)] * self.cols for _ in range(self.rows)]
        else:
            check(False, f"unexpected CSI {args}{final}")

    def text(self, r):
        return "".join(chr(c[0]) if c[0] < 0x7F else "~" for c in self.cell[r]).rstrip()

    def find(self, prefix):
        for r in range(self.rows):
            t = self.text(r)
            at = t.find(prefix)
            if at >= 0:
                return r, at
        raise AssertionError(f"no row shows {prefix!r}")


def glyph_spec(code):
    """The nine rows pcdraw.h documents for a bank code."""
    rows = [0] * 9
    if 0xA0 <= code <= 0xA5:
        rows = [sum(0x80 >> k for k in range(code - 0xA0 + 1))] * 9
    elif 0xA6 <= code <= 0xAE:
        n = code - 0xA6 + 1
        rows = [0xFC if r >= 9 - n else 0 for r in range(9)]
    elif code == 0xAF:
        rows = [0, 0, 0, 0xFC, 0xFC, 0xFC, 0, 0, 0]
    else:
        mask = code - 0xB0 + 1
        for r in range(9):
            pair = mask >> (r // 3 * 2)
            rows[r] = (0xE0 if pair & 1 else 0) | (0x1C if pair & 2 else 0)
    return rows


def run(binary, args=(), fixture="picocalc", tty=None, bank=False, env_extra=None):
    """Run a program; return (output bytes, uploaded glyphs by code)."""
    env = {"PATH": os.environ.get("PATH", ""), "USER": "root", "TERM": "xterm-basic",
           "PICO_FIXTURE": fixture}
    env.update(env_extra or {})
    log = None
    if tty:
        env["PCDRAW_TTY"] = tty
    if bank:
        fd, log = tempfile.mkstemp(prefix="glyphlog")
        os.close(fd)
        env["PCDRAW_GLYPHLOG"] = log
    try:
        p = subprocess.run([binary, *args], env=env, stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=30)
        glyphs = {}
        if log:
            with open(log) as f:
                for line in f:
                    v = [int(x, 16) for x in line.split()]
                    glyphs[v[0]] = v[1:]
    finally:
        if log:
            os.unlink(log)
    check(p.returncode == 0, f"{binary} {args} exit {p.returncode}: {p.stderr!r}")
    return p.stdout, glyphs


def screen_of(data, rows=ROWS, cols=COLS):
    s = Screen(rows, cols)
    s.feed(data)
    return s


def bar_cells(s, label, n):
    r, at = s.find(label)
    start = at + len(label)
    return [s.cell[r][start + i] for i in range(n)]


def dump(s, glyphs, path):
    """The screen as a PPM: 6 by 10 pixel cells, bank glyphs from the log."""
    w, h = s.cols * 6, s.rows * 10
    px = bytearray(w * h * 3)
    for r in range(s.rows):
        for c in range(s.cols):
            code, fg, bg, rev = s.cell[r][c]
            if rev:
                fg, bg = bg, fg
            rows = glyphs.get(code)
            if rows is None:
                # No font here: a text cell shows as a small block in its color.
                rows = [0] * 9 if code == 32 else [0, 0, 0x78, 0x78, 0x78, 0x78, 0x78, 0, 0]
                if code == 0x100 + ord("q"):
                    rows = [0, 0, 0, 0, 0xFC, 0, 0, 0, 0]
            for y in range(10):
                bits = rows[min(y, 8)]
                for x in range(6):
                    col = PALETTE[fg if bits & (0x80 >> x) else bg]
                    o = ((r * 10 + y) * w + c * 6 + x) * 3
                    px[o:o + 3] = bytes(col)
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + px)


def logo_spec():
    """The mirror ball of picofetch.c: 28 by 30 squares, True where drawn."""
    glint = {(0, 7), (27, 5), (0, 26), (27, 28)}
    out = set()
    for y in range(30):
        for x in range(28):
            dx, dy = 2 * x - 28, 2 * y - 33
            if dx * dx + dy * dy <= 625:
                if not (x % 4 == 2 or y % 3 == 0):
                    out.add((x, y))
            elif (y < 4 and x == 14) or (x, y) in glint:
                out.add((x, y))
    return out


def test_glyphs(fetch):
    _, glyphs = run(fetch, tty="32x53", bank=True)
    check(sorted(glyphs) == list(range(0xA0, 0xEF)), "79 codes 0xa0-0xee are uploaded")
    for code, rows in glyphs.items():
        check(len(rows) == 9 and all(r & 3 == 0 for r in rows),
              f"glyph {code:#x} keeps bits 1-0 clear in nine rows")
        check(rows == glyph_spec(code), f"glyph {code:#x} has its documented shape")


def test_fetch_plain(fetch):
    out, _ = run(fetch)
    check(b"\x1b" not in out and all(b < 0x80 for b in out), "piped output is plain ASCII")
    text = out.decode().splitlines()
    want = [
        "root@pico",
        "OS       DiscoBSD 2.7 arm",
        "Kernel   PICOCALC_PSRAM#812",
        "Board    ClockworkPi PicoCalc",
        "Uptime   1d 02:03:06",
        "CPU      125.000 MHz, 100 Hz tick",
        "Cores    1 kernel; core1 not in this kernel",
        "SRAM     264K, process window 192K",
        "Memory   ####.... 96K core, 301K all",
        "Swap     ##...... 96K/384K ram 1",
        "Root     ####.... 494K/988K fl0a",
        "/mnt     ##...... 7.4G/29.7G sd0a",
        "Disks    fl0 fl1 sd0",
        "Flash    image 113880 B crc 1234abcd ok",
        "XIP      cache on, hit 98.2%",
        "MPU      on, 4 of 8 regions",
        "Battery  ######.. 75% charging",
        "Light    panel 128, keys 16 of 240",
        "Sensors  not in this kernel",
        "Load     1.12 0.45 0.07 run 1",
        "Tables   proc 7/20 ino 12/24 file 9/20 buf 4/4",
        "Kstack   ###..... peak 812 of 2048 B",
        "I/O      counters not in this kernel",
        "USB      console not in this kernel",
        "Console  dev 10,0 xterm-basic",
        "Reset    power or run pin",
    ]
    for line in want:
        check(line in text, f"plain output holds {line!r}")


def test_fetch_states(fetch):
    def lines(fixture):
        return run(fetch, fixture=fixture)[0].decode().splitlines()

    pico = lines("pico")
    check("Battery  not in this kernel" in pico, "an unserved node says so")
    check("Light    not in this kernel" in pico, "an unserved node says so")
    check("Cores    1 kernel; core1 observer r0 s3 f0" in pico, "core 1 counters")
    check("I/O      root w 612K swap w 12.0M er 47" in pico, "storage counters")
    check("USB      cdc in 5120, recovered 0" in pico, "USB counters")
    check("Disks    fl0 fl1" in pico, "two drives")
    check(not any(x.startswith("/mnt") for x in pico), "no card mount on the Pico")
    check("Battery  read failed" in lines("failed"), "a failed read is not 'absent'")
    check("Sensors  31.4 C, VSYS 4.98 V" in lines("sensors"), "ADC readings")
    full = lines("full")
    check("Flash    image 113880 B crc 1234abcd MISMATCH" in full, "CRC mismatch shown")
    check("Reset    watchdog timeout" in full, "watchdog reason decoded")
    check("Battery  #....... 9%" in full, "9 percent rounds to the first cell")
    check("Memory   ######## 190K core, 301K all" in full, "a nearly full window")


def test_fetch_bank(fetch, dumpdir):
    out, glyphs = run(fetch, tty="32x53", bank=True)
    s = screen_of(out)
    check(s.r <= 30, f"a command line, the report and a prompt fit 32 rows ({s.r} rows)")
    check(all(len(s.text(r)) <= 53 for r in range(s.rows)), "no row passes column 53")
    for r in range(2, 10):
        check(s.cell[r][16][0] != 32 and s.cell[r][15][0] == 32,
              f"row {r} holds a reading beside the logo")
    check(s.text(10).startswith("Cores    1 kernel; core1 not in this kernel"),
          "rows below the logo use the whole width")
    check(b"\n" not in out.replace(b"\r\n", b""), "the bank tier ends each row with CR LF")
    # The logo: decode the mosaic cells of the top ten rows, 14 cells wide.
    drawn = set()
    for r in range(10):
        for c in range(14):
            code = s.cell[r][c][0]
            if code == 32:
                continue
            check(0xB0 <= code <= 0xEE, f"logo cell {r},{c} is a mosaic code")
            mask = code - 0xB0 + 1
            for bit in range(6):
                if mask >> bit & 1:
                    drawn.add((c * 2 + bit % 2, r * 3 + bit // 2))
    check(drawn == logo_spec(), "the logo is the documented mirror ball")
    for r in range(2, 10):
        for c in range(1, 13, 2):
            pair = {s.cell[r][c + k][1] for k in (0, 1) if s.cell[r][c + k][0] != 32}
            check(len(pair) <= 1, f"the facet at row {r}, cells {c}-{c + 1} has one color")
    facets = {s.cell[r][c][1] for r in range(2, 10) for c in range(2, 12)
              if s.cell[r][c][0] != 32}
    check(facets >= {9, 10, 11, 12, 13, 14}, "the facets use the six bright colors")
    # Bars: sixths of a cell. 96K of 192K is 24 sixths of 8 cells.
    mem = bar_cells(s, "Memory   ", 8)
    check([c[0] for c in mem] == [0xA5] * 4 + [0xAF] * 4, "half a bar is four full cells")
    check(all(c[1] == 2 for c in mem[:4]) and all(c[1] == 8 for c in mem[4:]),
          "a half-used level is green on a gray track")
    ks = bar_cells(s, "Kstack   ", 8)
    check([c[0] for c in ks] == [0xA5] * 3 + [0xA0] + [0xAF] * 4,
          "812 of 2048 is 19 sixths: three cells and one column")
    bat = bar_cells(s, "Battery  ", 8)
    check([c[0] for c in bat] == [0xA5] * 6 + [0xAF] * 2 and bat[0][1] == 2,
          "75 percent battery is six green cells")
    r, at = s.find("Battery")
    check(s.cell[r][at][1] == 14, "labels are bright cyan")
    # The palette row: sixteen colors, two full cells each.
    pal = [row for row in range(s.rows)
           if [c[1] for c in s.cell[row][0:32]] == [i // 2 for i in range(32)]
           and all(c[0] == 0xA5 for c in s.cell[row][0:32])]
    check(len(pal) == 1, "one palette row shows the sixteen colors")
    if dumpdir:
        dump(s, glyphs, os.path.join(dumpdir, "picofetch.ppm"))

    out, _ = run(fetch, fixture="full", tty="32x53", bank=True)
    s = screen_of(out)
    check(bar_cells(s, "Root     ", 8)[0][1] == 1, "a nearly full root is red")
    check(bar_cells(s, "Battery  ", 8)[0][0] == 0xA3 and
          bar_cells(s, "Battery  ", 8)[0][1] == 1,
          "9 percent battery is four red columns")

    out, _ = run(fetch, args=["-n"], tty="32x53", bank=True)
    s = screen_of(out)
    check(not any(30 <= v <= 47 or 90 <= v <= 97 for v in s.sgr), "-n writes no color")
    out, _ = run(fetch, tty="24x44")
    s = screen_of(out, 40, 44)
    check(s.text(2).startswith("OS "), "a terminal under 46 columns gets no logo")
    out, _ = run(fetch, args=["-l"], tty="32x53", bank=True)
    check(screen_of(out).text(0).startswith("root@pico"), "-l drops the logo")


def frames(out):
    return [f.splitlines() for f in out.decode().split("\n\n") if f.strip()]


def procs_of(frame):
    at = next(i for i, x in enumerate(frame) if "COMMAND" in x)
    return [(x.split()[-1], x.split()[-3] if x.split()[-2].count(":") else None)
            for x in frame[at + 1:]]


def test_top_plain(top):
    out, _ = run(top, args=["-c", "2", "-d", "1"])
    check(b"\x1b" not in out, "piped frames are plain")
    f = frames(out)
    check(len(f) == 2, "-c 2 draws two frames")
    one, two = f
    check(any(x.startswith("CPU ") and x.endswith(" 40.0%") for x in two),
          "80 busy of 200 ticks is 40.0%")
    check("     user 25.0 nice 0.0 sys 15.0 idle 60.0" in two, "state shares")
    check("Evt  cs 41 sc 230 in 112 /s  run 1 slp 4 swp 2" in two, "event rates")
    check(any("fl0 5/s 10K" in x for x in two), "disk rates per second")
    check(any("fl0 0/s 0K" in x for x in one), "the first frame has no rate")
    order = [(n, p) for n, p in procs_of(two)]
    check(order[:3] == [("picotop", "20.0"), ("swapper", "15.0"), ("cc", "5.0")],
          f"processes by processor share: {order[:3]}")
    check(all(p == "0.0" for _, p in procs_of(one)), "the first frame has no shares")
    check(any(x.startswith("Bat ") and x.endswith("75% charging") for x in two), "battery")
    check("Tmp  not in this kernel" in two, "an unserved sensor says so")

    # Two thousand million ticks since boot: the shares of the first frame and
    # of the next both hold in 32-bit arithmetic (the -m32 build runs this).
    one, two = frames(run(top, args=["-c", "2", "-d", "1"], fixture="longrun")[0])
    for name, frame in (("since boot", one), ("between frames", two)):
        check(any(x.startswith("CPU ") and x.endswith(" 40.0%") for x in frame),
              f"long uptime, {name}: 40.0% busy")
        check("     user 25.0 nice 0.0 sys 15.0 idle 60.0" in frame,
              f"long uptime, {name}: state shares")
    check(procs_of(two)[:3] == [("picotop", "20.0"), ("swapper", "15.0"), ("cc", "5.0")],
          "long uptime: process shares from tick counts above 2^31")

    mem = procs_of(frames(run(top, args=["-c", "1", "-s", "mem"])[0])[0])
    check([n for n, _ in mem][:3] == ["cc", "stevie", "picotop"], "-s mem orders by size")
    pid = frames(run(top, args=["-c", "1", "-s", "pid"])[0])[0]
    at = next(i for i, x in enumerate(pid) if "COMMAND" in x)
    pids = [int(x.split()[0]) for x in pid[at + 1:]]
    check(pids == sorted(pids) and len(pids) == 7, "-s pid orders by process id")
    row = next(x for x in pid if x.split()[-1] == "cc")
    check(row.split()[4] == "147K" and row.split()[-2] == "0:03", "size and time columns")
    two = frames(run(top, args=["-c", "2", "-d", "1"], fixture="pico")[0])[1]
    check("Bat  not in this kernel" in two, "no battery node on the Pico")
    check(any("core1 observer" in x for x in two), "core 1 state")
    for bad in (["-d", "0"], ["-d", "61"], ["-c", "0"], ["-s", "x"], ["extra"]):
        try:
            p = subprocess.run([top, *bad], capture_output=True, stdin=subprocess.DEVNULL,
                               timeout=10)
        except subprocess.TimeoutExpired:
            raise AssertionError(f"{bad} is accepted and runs") from None
        check(p.returncode == 2 and p.stderr.startswith(b"usage:"), f"{bad} is refused")


def test_top_bank(top, dumpdir):
    out, glyphs = run(top, args=["-c", "2", "-d", "1"], tty="32x53", bank=True)
    s = screen_of(out)
    check(all(c[3] for c in s.cell[0]), "the title is a full reverse row")
    # CPU bar: 41 cells, 246 sixths. User 25% ends at 61, system at 98.
    cpu = bar_cells(s, "CPU  ", 41)
    check(all(c[0] == 0xA5 and c[1] == 2 for c in cpu[:10]), "ten full user cells")
    check(cpu[10][:3] == (0xA0, 2, 1), "the user edge: one column on the system color")
    check(all(c[0] == 0xA5 and c[1] == 1 for c in cpu[11:16]), "five full system cells")
    check(cpu[16][:3] == (0xA1, 1, 0), "the system edge: two columns")
    check(all(c[0] == 0xAF and c[1] == 8 for c in cpu[17:]), "then the track")
    # Graph: five rows; 40% of 45 ninths is 18, two full cells, newest at right.
    for col in (51, 52):
        codes = [s.cell[3 + g][col][0] for g in range(5)]
        check(codes == [32, 32, 32, 0xAE, 0xAE], f"graph column {col}: {codes}")
        check(s.cell[7][col][1] == 10, "a share under 50% is green")
    check(all(s.cell[3 + g][50][0] == 32 for g in range(5)), "two frames, two columns")
    r, at = s.find(" 40.0%")
    check(at + 6 == 53, "the busy share ends in the last column, percent sign kept")
    r, _ = s.find("COMMAND")
    check(all(c[3] for c in s.cell[r]), "the process header is a reverse row")
    check(s.text(r + 1).split()[-1] == "picotop" and s.cell[r + 1][0][1] == 10,
          "the running process is green")
    check(b"q quit  c/m/p order" in out, "each frame ends with the key line")
    check(s.text(31) == "" and (s.r, s.c) == (31, 0),
          "exit clears the key line and leaves the cursor there for the prompt")
    bat = bar_cells(s, "Bat  ", 27)
    check(bat[0][:2] == (0xA5, 2) and bat[26][0] == 0xAF,
          "a 75 percent battery is a green level, not a yellow load")
    check([c[0] for c in bar_cells(s, "Mem  ", 27)].count(0xA5) == 13,
          "96K of 192K fills half of 27 cells")
    if dumpdir:
        dump(s, glyphs, os.path.join(dumpdir, "picotop.ppm"))
    s = screen_of(run(top, args=["-c", "1"], tty="20x40")[0], 20, 40)
    check(not any(0xA6 <= c[0] <= 0xAE for row in s.cell for c in row),
          "a 20-row terminal has no graph")


def test_top_keys(top):
    """On a pty: keys change the order and the interval, q restores the tty."""
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    env = {"PATH": os.environ.get("PATH", ""), "PICO_FIXTURE": "picocalc", "TERM": "xterm"}
    p = subprocess.Popen([top, "-d", "60"], stdin=slave, stdout=slave, stderr=slave, env=env)
    seen = b""

    def expect(token, what):
        nonlocal seen
        end = time.time() + 10
        while token not in seen:
            check(time.time() < end, f"{what}: {token!r} did not appear")
            if select.select([master], [], [], 0.2)[0]:
                seen += os.read(master, 4096)
        seen = b""

    try:
        expect(b">CPU%", "first frame")
        check(termios.tcgetattr(slave)[3] & termios.ICANON == 0, "keys are read unbuffered")
        os.write(master, b"m")
        expect(b">SIZE", "m orders by size")
        os.write(master, b"p")
        expect(b">PID", "p orders by pid")
        os.write(master, b"+")
        expect(b" 60s", "+ stops at 60 seconds")
        os.write(master, b"-")
        expect(b" 59s", "- shortens the interval")
        os.write(master, b"q")
        check(p.wait(timeout=10) == 0, "q exits 0")
        check(termios.tcgetattr(slave) == before, "the tty modes are restored")
        p2 = subprocess.Popen([top, "-d", "60"], stdin=slave, stdout=slave, stderr=slave,
                              env=env)
        expect(b">CPU%", "second run")
        p2.terminate()
        check(p2.wait(timeout=10) == 128 + 15, "SIGTERM exits 143")
        check(termios.tcgetattr(slave) == before, "a signal restores the tty modes")
    finally:
        if p.poll() is None:
            p.kill()
        os.close(master)
        os.close(slave)


def main():
    args = sys.argv[1:]
    dumpdir = None
    if "--dump" in args:
        at = args.index("--dump")
        dumpdir = args[at + 1]
        del args[at:at + 2]
    fetch = args[0] if args else "./picofetch-host"
    top = args[1] if len(args) > 1 else "../picotop/picotop-host"
    test_glyphs(fetch)
    test_fetch_plain(fetch)
    test_fetch_states(fetch)
    test_fetch_bank(fetch, dumpdir)
    test_top_plain(top)
    test_top_bank(top, dumpdir)
    test_top_keys(top)
    print(f"picofetch and picotop host tests: {checks} checks passed")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
