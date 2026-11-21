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
"""The panel display of i86 against a model of /dev/lcdgfx.

check.py runs test() with the emulator built around tests/panel.c. Each
stage of tests/panel.asm draws through display memory, the firmware's
video services or the adapter's ports and stops; the model writes the
frame it holds when the emulator closes it. The frame each stage must
leave is computed here from the stage's own description: what the guest
stores, the layout of the adapter's memory, the adapter's colors and the
font tables font/mkfont.py generates. Nothing of gfx.c is read.
"""

from __future__ import annotations

import importlib.util
import os
import pty
import re
import select
import shutil
import subprocess
import time

WIDTH = HEIGHT = 320
TOP = 60  # the panel line of the adapter's first
FIELD = 8000


# Faults in the sources, each of which test() must detect.
MUTANTS = [
    ("gfx.c", "(reg_color & 0x20) ? 0x753 :", "(reg_color & 0x20) ? 0x642 :"),
    ("gfx.c", "if (irgb == 6)\n\t\tp.g = 0x55;", "if (irgb == 16)\n\t\tp.g = 0x55;"),
    ("gfx.c", "pal_set(1, reg_color & 15, 1);", "pal_set(1, reg_color & 15, 0);"),
    ("gfx.c", "(b >> 7) + (b >> 6 & 1)", "(b >> 7) * 2"),
    (
        "gfx.c",
        "unsigned pages = mem_rearm(), i, r, y, n;",
        "unsigned pages = mem_watched, i, r, y, n;",
    ),
    ("mem.c", "mem_wmap[(watch_base >> MEM_PGSHIFT) + i] = 0;", "mem_stats.soft += 0;"),
    ("mem.c", "mem_watched |= 1u << i;", "mem_watched |= 1u;"),
    ("mem.c", "else if (!wr || !watch_fault(a))", "else"),
    ("mem.c", "if (lent || nframes < n + 4)", "if (lent)"),
    ("mem.c", "nframes += lent;\n\tlent = 0;", "nframes += lent;"),
    ("gfx.c", "if (first + k == cursor && y >= 6)", "if (first + k == cursor && y >= 7)"),
    ("gfx.c", "attr & 15, attr >> 4 & 7) == 0)", "attr & 15, attr >> 4 & 15) == 0)"),
    ("gfx.c", "shown[2 * i] = (uint8_t)~cell[2 * i];", "shown[2 * i] = cell[2 * i];"),
    ("gfx.c", "cell[2 * (i + n) + 1] == cell[2 * i + 1] &&", "1 &&"),
    ("gfx.c", "((y & 1) ? 0 : 4) & 15;", "((y & 1) ? 4 : 0) & 15;"),
    ("gfx.c", "(void)DEV_IOCTL(fd, LCDG_FILL, &all);", "(void)all;"),
    ("gfx.c", "#define GFX_Y0\t\t60", "#define GFX_Y0\t\t59"),
    ("gfx.c", "cm.fmt = LCDG_COLMOD_RGB565;", "cm.fmt = LCDG_COLMOD_RGB666;"),
    (
        "gfx.c",
        "\t\tmemset(bank[0], 0, BANK_PAGES << MEM_PGSHIFT);",
        "\t\tmemset(bank[0], 0, FIELD);",
    ),
    (
        "gfx.c",
        "\t\tmemset(bank[1], 0, BANK_PAGES << MEM_PGSHIFT);\n\t}\n\treturn (k != K_TEXT);",
        "\t\tmemset(bank[1], 0, FIELD / 2);\n\t}\n\treturn (k != K_TEXT);",
    ),
    (
        "gfx.c",
        "memset(bank[0] + 0x1000, 0, 0x1000);\n\t\tmemset(bank[1]",
        "memset(bank[0] + 0x1000, 0, 0x10);\n\t\tmemset(bank[1]",
    ),
    (
        "gfx.c",
        "\t\tmemset(bank[1], 0, BANK_PAGES << MEM_PGSHIFT);\n\t}\n\tif (k == K_TEXT && bank[1]",
        "\t}\n\tif (k == K_TEXT && bank[1]",
    ),
    ("gfx.c", "(reg_mode & 4) ? 0x743 :", "(reg_mode & 8) ? 0x743 :"),
    ("gfx.c", "mem_rb(line_addr(down ? y - n : y + n) + x)", "mem_rb(line_addr(y + n) + x)"),
    ("gfx.c", "k >= h - n ? fill :", "k >= h - n ? fill ^ 0xff :"),
    ("gfx.c", "if (color & 0x80) {\n\t\t\told", "if (color & 0x40) {\n\t\t\told"),
    ("gfx.c", "v |= (color & 3) << (14 - 2 * x);", "v |= (color & 3) << (2 * x);"),
    ("gfx.c", "(v >> shift & mask));", "(v & mask));"),
    (
        "gfx.c",
        "if (al & 0x80)\n\t\t\tv ^= (al & mask) << shift;",
        "if (al & 0x40)\n\t\t\tv ^= (al & mask) << shift;",
    ),
    ("gfx.c", "(reg_color & 0x1f) | (bl & 1) << 5", "(reg_color & 0x1f) | (bl & 1) << 4"),
    ("gfx.c", "k = !(v & 2) ? K_TEXT : (v & 0x10) ? K_CGA6 : K_CGA4;", "k = K_TEXT;"),
    ("gfx.c", "\tif (port == 0x3d9) {\n\t\treg_color = (uint8_t)v;", "\tif (port == 0x3d9) {"),
    ("pc.c", "gfx_close();\n\t/* One line", "/* One line"),
    ("pc.c", "gfx_close();\n\t\treturn;\n\t}\n\toputs(", "return;\n\t}\n\toputs("),
    ("pc.c", "mem_watched |= (1u << (VRAM_SIZE >> MEM_PGSHIFT)) - 1;", "mem_watched |= 0;"),
    (
        "pc.c",
        "if (!gfx_graphic && (ah == 0x00 || ah == 0x06",
        "if ((ah == 0x00 || ah == 0x02 || ah == 0x06",
    ),
    ("pc.c", "mem_rom(ROM_FONT, gfx_font(), 128 * 8);", "(void)gfx_font();"),
    ("pc.c", "graphic ? ((mode & 0x7f) == 6 ? 80 : 40) :", "graphic ? 40 :"),
    (
        "pc.c",
        "gfx_putc(row, col, ch, attr);\n\t\t} else {",
        "gfx_putc(row, col, ch, 3);\n\t\t} else {",
    ),
    ("pc.c", "gfx_putc(row, col++, al, bl);", "gfx_putc(row, col++, al, bl & 0x7f);"),
    (
        "pc.c",
        "\t\tif (graphic)\n\t\t\tset_mode(cols == 40 ? 1 : 3);",
        "\t\tif (!graphic)\n\t\t\tset_mode(cols == 40 ? 1 : 3);",
    ),
    (
        "pc.c",
        "if (gfx_on) {\n\t\tgfx_refresh(cols, bda8(0x51) * cols + bda8(0x50));\n\t\treturn;\n\t}",
        "if (gfx_on)\n\t\tgfx_refresh(cols, bda8(0x51) * cols + bda8(0x50));",
    ),
    (
        "gfx.c",
        "if (fd < 0) {\n\t\t(void)kind_set(K_TEXT);",
        "if (fd < 0) {\n\t\tif (bank[1] == NULL)\n\t\t\t(void)kind_set(K_TEXT);",
    ),
    ("gfx.c", "\t\tif (pointer)\n\t\t\tpointer_draw();", "\t\t(void)pointer;"),
    (
        "gfx.c",
        "\tr.color = 0;\n\t(void)DEV_IOCTL(fd, LCDG_FILL, &r);\n\tif (!pointer)",
        "\tr.color = 0;\n\tif (!pointer)",
    ),
    ("pc.c", "\t\t\tgfx_pointer(mouse_keys);", "\t\t\tgfx_pointer(1);"),
]


class Failure(Exception):
    pass


def load_fonts(src):
    """The 8x8 and 4x8 cells of the 256 characters, from the generator."""
    path = os.path.join(src, "font", "mkfont.py")
    spec = importlib.util.spec_from_file_location("mkfont", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if module.main(["mkfont", "--check", os.path.join(src, "font"), os.path.join(src, "font.h")]):
        raise Failure("font.h is not what font/mkfont.py generates")
    return module.tables(module.Path(src) / "font")


def rgb(irgb, half=False):
    """An adapter color as the panel shows it: 5, 6 and 5 bits kept."""
    low = 0x55 if irgb & 8 else 0
    r = (0xAA if irgb & 4 else 0) + low
    g = (0xAA if irgb & 2 else 0) + low
    b = (0xAA if irgb & 1 else 0) + low
    if irgb == 6:
        g = 0x55
    if half:
        r, g, b = r >> 1, g >> 1, b >> 1
    r, g, b = r >> 3, g >> 2, b >> 3
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def blank():
    return [[(0, 0, 0)] * WIDTH for _ in range(HEIGHT)]


def text_frame(fonts, cells, cols, cursor):
    """Cells of (character, attribute); the cursor underlines its cell."""
    wide, narrow = fonts
    width = 4 if cols == 80 else 8
    table = narrow if cols == 80 else wide
    frame = blank()
    for index, (ch, attr) in enumerate(cells):
        fg, bg = rgb(attr & 15), rgb(attr >> 4 & 7)
        for y in range(8):
            bits = (1 << width) - 1 if index == cursor and y >= 6 else table[ch][y]
            row = frame[TOP + index // cols * 8 + y]
            for x in range(width):
                row[index % cols * width + x] = fg if bits >> (width - 1 - x) & 1 else bg
    return frame


def cga4_frame(fields, color, burst_off=False):
    """320 by 200 in four colors: two bits a pixel, the first one highest."""
    bright = 8 if color & 0x10 else 0
    three = (3, 4, 7) if burst_off else (3, 5, 7) if color & 0x20 else (2, 4, 6)
    colors = [rgb(color & 15)] + [rgb(c | bright) for c in three]
    frame = blank()
    for y in range(200):
        line = fields[y & 1][(y >> 1) * 80 :][:80]
        for x in range(320):
            frame[TOP + y][x] = colors[line[x >> 2] >> (6 - 2 * (x & 3)) & 3]
    return frame


def cga6_frame(fields, color):
    """640 by 200 in two colors, each pair of pixels one of three levels."""
    levels = [rgb(0), rgb(color & 15, half=True), rgb(color & 15)]
    frame = blank()
    for y in range(200):
        line = fields[y & 1][(y >> 1) * 80 :][:80]
        for x in range(320):
            pair = line[x >> 2] >> (6 - 2 * (x & 3)) & 3
            frame[TOP + y][x] = levels[(pair >> 1) + (pair & 1)]
    return frame


class Cga:
    """Display memory of a graphics mode and the firmware's services."""

    def __init__(self, wide, hires=False, fill=True):
        self.font = wide
        self.hires = hires
        self.fields = [bytearray(8192), bytearray(8192)]
        if fill:
            for i in range(FIELD):
                self.fields[0][i] = (5 * i + 1) & 255
                self.fields[1][i] = (11 * i + 7) & 255

    def line(self, y):
        return self.fields[y & 1], (y >> 1) * 80

    def pixel(self, x, y, value=None):
        field, at = self.line(y)
        if self.hires:
            at, shift, mask = at + (x >> 3), 7 - (x & 7), 1
        else:
            at, shift, mask = at + (x >> 2), 6 - 2 * (x & 3), 3
        if value is None:
            return field[at] >> shift & mask
        if value & 0x80:
            field[at] ^= (value & mask) << shift
        else:
            field[at] = field[at] & ~(mask << shift) & 255 | (value & mask) << shift
        return None

    def putc(self, row, col, ch, color):
        for y in range(8):
            bits = self.font[ch][y]
            field, at = self.line(row * 8 + y)
            if self.hires:
                value = bits if color & 1 else 0
                field[at + col] = (field[at + col] ^ value) if color & 0x80 else value
                continue
            value = 0
            for x in range(8):
                if bits >> (7 - x) & 1:
                    value |= (color & 3) << (14 - 2 * x)
            if color & 0x80:
                value ^= field[at + 2 * col] << 8 | field[at + 2 * col + 1]
            field[at + 2 * col] = value >> 8
            field[at + 2 * col + 1] = value & 255

    def scroll(self, top, left, bot, right, n, fill, down):
        per = 1 if self.hires else 2
        snapshot = [bytes(f) for f in self.fields]
        for y in range(top * 8, (bot + 1) * 8):
            source = y - n * 8 if down else y + n * 8
            field, at = self.line(y)
            for x in range(left * per, (right + 1) * per):
                if top * 8 <= source < (bot + 1) * 8:
                    field[at + x] = snapshot[source & 1][(source >> 1) * 80 + x]
                else:
                    field[at + x] = fill


def expected(stage, fonts, boot):
    """The frame stage leaves, and the pixels sent after the first drawing
    where the stage draws twice."""
    wide = fonts[0]
    ramp = [((0x4567 + 0x0123 * i) & 255, (0x4567 + 0x0123 * i) >> 8 & 255) for i in range(2000)]
    if stage == 0:
        # The text mode cleared its 2,000 cells; what the first graphics
        # mode left in the rest of their four kilobytes is still there.
        cga = Cga(wide, fill=False)
        cga.fields[0][0:4000] = bytes([0x20, 0x07]) * 2000
        cga.fields[0][4000:4096] = bytes((5 * i + 1) & 255 for i in range(4000, 4096))
        cga.fields[0][0x100:0x300] = boot[:512]
        return cga4_frame(cga.fields, 0x30)
    if stage == 1:
        for k in range(8):
            ramp[k] = (wide[ord("A")][k], ramp[k][1])
        return text_frame(fonts, ramp, 80, 5 * 80 + 10)
    if stage == 2:
        cells = [
            ((0x89AB + 0x0357 * i) & 255, (0x89AB + 0x0357 * i) >> 8 & 255) for i in range(1000)
        ]
        return text_frame(fonts, cells, 40, 24 * 40 + 39)
    if stage == 3:
        cga = Cga(wide)
        cga.pixel(17, 33, 2)
        cga.pixel(300, 199, 0x83)
        cga.pixel(1, 1, cga.pixel(17, 33))
        cga.pixel(2, 2, 0)
        cga.pixel(3, 2, 0)
        cga.putc(3, 4, ord("A"), 2)
        cga.putc(3, 5, ord("b"), 3)
        for col in (37, 38, 39):
            cga.putc(10, col, ord("Z"), 0x81)
        return cga4_frame(cga.fields, 0x11)
    if stage == 4:
        cga = Cga(wide, hires=True)
        cga.pixel(639, 0, 1)
        cga.pixel(5, 100, 0x81)
        cga.putc(24, 79, ord("W"), 1)
        cga.scroll(0, 0, 24, 79, 1, 0, False)
        cga.putc(24, 0, ord("x"), 1)
        return cga6_frame(cga.fields, 0x3F)
    if stage == 5:
        cga = Cga(wide)
        cga.scroll(2, 3, 10, 20, 2, 0x55, False)
        cga.scroll(12, 0, 24, 39, 1, 0xAA, True)
        return cga4_frame(cga.fields, 0x2A, burst_off=True)
    if stage == 6:
        cga = Cga(wide)
        cga.fields[0][4000] = 0x1B
        cga.fields[1][81] = 0xE4
        return cga4_frame(cga.fields, 0x30)
    if stage == 7:
        ramp[1000] = (ord("X"), 0x1E)
        ramp[14 * 80 : 15 * 80] = ramp[15 * 80 : 16 * 80]
        ramp[15 * 80 : 16 * 80] = [(32, 0x2F)] * 80
        return text_frame(fonts, ramp, 80, 20 * 80 + 5)
    if stage == 8:
        return cga4_frame(Cga(wide).fields, 0x30)
    cells = [(32, 7)] * 2000
    for k, ch in enumerate(b"TEXT4"):
        cells[k] = (ch, 7)
    return text_frame(fonts, cells, 80, 5)


def read_frame(path):
    with open(path, "rb") as handle:
        data = handle.read()
    head = b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT)
    if not data.startswith(head) or len(data) != len(head) + 3 * WIDTH * HEIGHT:
        raise Failure("%s is not a frame" % path)
    body = data[len(head) :]
    return [[tuple(body[3 * (y * WIDTH + x) :][:3]) for x in range(WIDTH)] for y in range(HEIGHT)]


def read_counts(path):
    with open(path) as handle:
        return {k: int(v, 16 if k == "colmod" else 10) for k, v in (ln.split() for ln in handle)}


def compare(got, want, what):
    for y in range(HEIGHT):
        if got[y] != want[y]:
            x = next(x for x in range(WIDTH) if got[y][x] != want[y][x])
            raise Failure(
                "%s: pixel (%d, %d) is %r, expected %r" % (what, x, y, got[y][x], want[y][x])
            )


def run(binary, work, keys, extra=(), env=None):
    """One scripted session; the frames and counters of its closes."""
    base = os.path.join(work, "frame")
    for name in os.listdir(work):
        if name.startswith("frame."):
            os.unlink(os.path.join(work, name))
    image = os.path.join(work, "panel-run.img")
    shutil.copyfile(os.path.join(work, "panel.img"), image)
    cmd = [binary, "-T", "-g", "-s", os.path.join(work, "page"), "-a", image, "-k", keys]
    environ = dict(os.environ, I86_PANEL=base, **(env or {}))
    try:
        done = subprocess.run(
            cmd + list(extra), capture_output=True, timeout=20, check=False, env=environ
        )
    except subprocess.TimeoutExpired:
        raise Failure("panel stage %s: timeout" % keys) from None
    if done.returncode != 0:
        raise Failure("panel stage %s: exit %d" % (keys, done.returncode))
    closes = []
    while os.path.exists("%s.%d.ppm" % (base, len(closes) + 1)):
        n = len(closes) + 1
        closes.append((read_frame("%s.%d.ppm" % (base, n)), read_counts("%s.%d.txt" % (base, n))))
    return done, closes


def test_stages(binary, work, fonts, boot):
    for stage in range(10):
        keys = "%d" % stage + ("x" if stage in (6, 7, 9) else "")
        done, closes = run(binary, work, keys)
        what = "panel stage %d" % stage
        if len(closes) != 1:
            raise Failure("%s: the panel was closed %d times" % (what, len(closes)))
        frame, counts = closes[0]
        if counts["errors"]:
            raise Failure("%s: the device rejected %d operations" % (what, counts["errors"]))
        if counts["colmod"] != 0x55:
            raise Failure("%s: three bytes a pixel on the wire" % what)
        compare(frame, expected(stage, fonts, boot), what)
        if done.stdout:
            raise Failure("%s: %d bytes went to the terminal" % (what, len(done.stdout)))
        if stage in (6, 7):
            # The same session ended at the stage's key wait has drawn
            # everything but the change. The change may cost the scan
            # lines of the two pages written, 27 of them, or the cells
            # that differ: the one stored, the two rows the firmware
            # scrolled and the cursor's old and new places.
            _, before = run(binary, work, "%d" % stage)
            more = counts["pixels"] - before[0][1]["pixels"]
            if more != (27 * 320 if stage == 6 else 163 * 32):
                raise Failure("%s: the change sent %d pixels" % (what, more))


def test_fallbacks(binary, work, fonts, boot):
    """No device, and too few page frames to lend eight."""
    done, closes = run(binary, work, "9x", env={"I86_PANEL_FAIL": "open"})
    if closes or b"no panel device" not in done.stderr:
        raise Failure("panel absent: no report")
    plain = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", done.stdout)
    if b"TEXT" not in plain:
        raise Failure("panel absent: the terminal is not the display")
    # Eleven frames leave fewer than four after a loan of eight: the mode
    # stays text and the session goes on. Twelve suffice, and every page
    # the guest touches then goes through the four that are left.
    done, closes = run(binary, work, "9x", extra=["-f", "11"])
    if len(closes) != 1 or closes[0][1]["errors"]:
        raise Failure("panel with 11 frames: the session failed")
    compare(closes[0][0], expected(9, fonts, boot), "panel with 11 frames")
    done, closes = run(binary, work, "3", extra=["-f", "11"])
    if len(closes) != 1 or closes[0][1]["errors"]:
        raise Failure("panel with 11 frames: the session failed")
    if closes[0][0] == expected(3, fonts, boot):
        raise Failure("panel with 11 frames: a graphics mode with three frames left")
    done, closes = run(binary, work, "3", extra=["-f", "12"])
    if len(closes) != 1 or closes[0][1]["errors"]:
        raise Failure("panel with 12 frames: the session failed")
    compare(closes[0][0], expected(3, fonts, boot), "panel with 12 frames")


def load_ptywait():
    """tests/ptywait.py, beside this file; the interpreter runs isolated,
    without the script's directory on its path."""
    spec = importlib.util.spec_from_file_location(
        "ptywait", os.path.join(os.path.dirname(os.path.abspath(__file__)), "ptywait.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ptywait = load_ptywait()


def session(binary, work, steps, env=None):
    """A session typed at a pseudo-terminal: each step waits for a text
    there or for a close of the panel, then types."""
    base = os.path.join(work, "frame")
    for name in os.listdir(work):
        if name.startswith("frame."):
            os.unlink(os.path.join(work, name))
    image = os.path.join(work, "panel-run.img")
    shutil.copyfile(os.path.join(work, "panel.img"), image)
    master, slave = pty.openpty()
    child = subprocess.Popen(
        [binary, "-T", "-g", "-s", os.path.join(work, "page"), "-a", image],
        stdin=slave,
        stdout=slave,
        stderr=subprocess.DEVNULL,
        env=dict(os.environ, I86_PANEL=base, **(env or {})),
    )
    seen = b""
    at = step = 0
    deadline = time.time() + 30
    try:
        while time.time() < deadline and child.poll() is None and step < len(steps):
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                seen += os.read(master, 4096)
            wait, keys = steps[step]
            if isinstance(wait, int):
                if not os.path.exists("%s.%d.txt" % (base, wait)):
                    continue
            elif wait:
                plain = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", seen)
                found = plain.find(wait, at)
                if found < 0:
                    continue
                at = found + len(wait)
            time.sleep(0.3)
            os.write(master, keys)
            step += 1
        status = None
        if step == len(steps):
            more, status = ptywait.wait_draining(child, master, 5)
            seen += more
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        ready, _, _ = select.select([master], [], [], 0.2)
        if ready:
            seen += os.read(master, 65536)
        os.close(master)
        os.close(slave)
    if step != len(steps) or status != 0:
        raise Failure("panel session: stopped at step %d, exit %r" % (step, status))
    closes = []
    while os.path.exists("%s.%d.ppm" % (base, len(closes) + 1)):
        n = len(closes) + 1
        closes.append((read_frame("%s.%d.ppm" % (base, n)), read_counts("%s.%d.txt" % (base, n))))
    return re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", seen), closes


def test_menu(binary, work, fonts, boot):
    """Control-] gives the panel back for the menu, whose text goes to
    the terminal, and takes it again; a panel that cannot be taken again
    leaves the terminal as the display."""
    plain, closes = session(
        binary, work, [(b"", b"3"), (b"", b"\x1d"), (b"quit", b"x"), (b"", b"\x1d"), (2, b"q")]
    )
    if len(closes) != 3:
        raise Failure("panel menu: %d closes" % len(closes))
    for number, (frame, counts) in enumerate(closes, 1):
        if counts["errors"]:
            raise Failure("panel menu: the device rejected an operation")
        compare(frame, expected(3, fonts, boot), "panel menu, close %d" % number)
    if plain.count(b"quit") != 2:
        raise Failure("panel menu: the menu is not on the terminal")
    plain, closes = session(
        binary,
        work,
        [(b"", b"3"), (b"", b"\x1d"), (b"quit", b"x"), (b"", b"\x1d"), (b"quit", b"q")],
        env={"I86_PANEL_FAIL": "reopen"},
    )
    if len(closes) != 1:
        raise Failure("panel lost: %d closes" % len(closes))
    # Control-] while stage 9 waits for its key in mode 4: the one close
    # shows that mode's fields, so the menu came in the graphics mode. The
    # panel is not taken again, the firmware reports the 40 columns of
    # mode 1 after the key, and the guest's text reaches the terminal.
    plain, closes = session(
        binary,
        work,
        [
            (b"", b"9"),
            (b"", b"\x1d"),
            (b"quit", b"x"),
            (b"", b"x"),
            (b"TEXT1", b"\x1d"),
            (b"quit", b"q"),
        ],
        env={"I86_PANEL_FAIL": "reopen"},
    )
    if len(closes) != 1:
        raise Failure("panel lost in a graphics mode: %d closes" % len(closes))
    compare(closes[0][0], cga4_frame(Cga(fonts[0]).fields, 0x30), "panel lost in a graphics mode")


def pointer(frame):
    """The mark of mouse keys: a white arrow at panel column 308, line 26."""
    rows = (0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xD8, 0x8C, 0x06)
    for y, bits in enumerate(rows):
        for x in range(8):
            if bits >> (7 - x) & 1:
                frame[26 + y][308 + x] = (255, 255, 255)
    return frame


def test_pointer(binary, work, fonts, boot):
    """Control-\\ shows the mark, the menu's return draws it again, and a
    second Control-\\ removes it."""
    _, closes = session(
        binary,
        work,
        [
            (b"", b"1"),
            (b"", b"\x1c"),
            (b"", b"\x1d"),
            (b"quit", b"x"),
            (b"", b"\x1d"),
            (b"quit", b"x"),
            (b"", b"\x1c"),
            (b"", b"\x1d"),
            (b"quit", b"q"),
        ],
    )
    if len(closes) != 4:
        raise Failure("panel pointer: %d closes" % len(closes))
    for number, (frame, counts) in enumerate(closes, 1):
        if counts["errors"]:
            raise Failure("panel pointer: the device rejected an operation")
        want = expected(1, fonts, boot)
        if number <= 2:
            want = pointer(want)
        compare(frame, want, "panel pointer, close %d" % number)


def test(binary, src, work):
    with open(os.path.join(src, "tests", "panel.hexdump")) as handle:
        boot = bytes.fromhex("".join(handle.read().split()))
    if len(boot) % 512 or boot[510:512] != b"\x55\xaa":
        raise Failure("panel.hexdump is not a boot program")
    with open(os.path.join(work, "panel.img"), "wb") as handle:
        handle.write(boot)
        handle.truncate(360 * 1024)
    fonts = load_fonts(src)
    test_stages(binary, work, fonts, boot)
    test_fallbacks(binary, work, fonts, boot)
    test_menu(binary, work, fonts, boot)
    test_pointer(binary, work, fonts, boot)
