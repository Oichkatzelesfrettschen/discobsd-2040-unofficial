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
"""Generate font.h, the two code page 437 fonts of the panel display.

Usage: mkfont.py DIR OUTPUT          write OUTPUT
       mkfont.py --check DIR OUTPUT  fail unless OUTPUT is what DIR gives

DIR holds 4x6-cp437.bdf and 5x8-cp437.bdf, the glyphs of code page 437
from the public-domain Misc-Fixed fonts of those sizes. A 5x8 glyph goes
into an 8x8 cell one column from the left; a 4x6 glyph into a 4x8 cell one
row from the top. The line and block characters B0h to DFh are computed
for both cells instead, so that lines meet across cell edges. The 4x6 font
has no glyph for sixteen of the pictures below 32 and for 7Fh; DRAWN holds
those, and 08h and 0Ah are the bullet and the circle inverted over the
whole cell, as code page 437 shows them. A character with no glyph stops
the generator.
"""

from __future__ import annotations

import sys
import unicodedata
from pathlib import Path
from typing import NoReturn

# The pictures code page 437 shows for the bytes below 32.
LOW = (
    0x0000,
    0x263A,
    0x263B,
    0x2665,
    0x2666,
    0x2663,
    0x2660,
    0x2022,
    0x25D8,
    0x25CB,
    0x25D9,
    0x2642,
    0x2640,
    0x266A,
    0x266B,
    0x263C,
    0x25BA,
    0x25C4,
    0x2195,
    0x203C,
    0x00B6,
    0x00A7,
    0x25AC,
    0x21A8,
    0x2191,
    0x2193,
    0x2192,
    0x2190,
    0x221F,
    0x2194,
    0x25B2,
    0x25BC,
)
ARMS = {"UP": 0, "DOWN": 1, "LEFT": 2, "RIGHT": 3}

# 4x6 glyphs by code point, in the font's style: three columns wide, the
# picture in the upper five rows.
DRAWN = {
    0x263A: ("....", "#.#.", "....", "#.#.", ".#..", "...."),
    0x263B: ("###.", ".#..", "###.", ".#..", "#.#.", "...."),
    0x25CB: ("....", ".#..", "#.#.", ".#..", "....", "...."),
    0x2642: ("..##", "...#", ".##.", "#.#.", ".#..", "...."),
    0x2640: (".#..", "#.#.", ".#..", "###.", ".#..", "...."),
    0x263C: (".#..", "###.", "#.#.", "###.", ".#..", "...."),
    0x25BA: ("#...", "##..", "###.", "##..", "#...", "...."),
    0x25C4: ("..#.", ".##.", "###.", ".##.", "..#.", "...."),
    0x203C: ("#.#.", "#.#.", "#.#.", "....", "#.#.", "...."),
    0x25AC: ("....", "....", "....", "###.", "###.", "...."),
    0x21A8: (".#..", "###.", ".#..", "###.", ".#..", "###."),
    0x25B2: ("....", "....", ".#..", "###.", "....", "...."),
    0x25BC: ("....", "....", "###.", ".#..", "....", "...."),
    0x2302: ("....", ".#..", "#.#.", "#.#.", "###.", "...."),
}
# Inverse pictures: the character they invert.
INVERSE = {0x08: 0x07, 0x0A: 0x09}


def fail(message: str) -> NoReturn:
    raise SystemExit(f"mkfont: {message}")


def code_point(ch: int) -> int:
    """The Unicode character code page 437 shows for byte ch."""
    if ch < 32:
        return LOW[ch]
    if ch == 127:
        return 0x2302
    return ord(bytes([ch]).decode("cp437"))


def parse(path: Path, width: int, height: int) -> dict[int, list[int]]:
    """Glyph rows by code point, leftmost pixel in bit width - 1."""
    glyphs: dict[int, list[int]] = {}
    fbb: tuple[int, ...] | None = None
    lines = iter(path.read_text(encoding="ascii").splitlines())
    for line in lines:
        words = line.split()
        if not words:
            continue
        if words[0] == "FONTBOUNDINGBOX":
            fbb = tuple(int(w) for w in words[1:5])
            if fbb[:2] != (width, height):
                fail(f"{path}: bounding box {fbb[:2]}, expected {width}x{height}")
        elif words[0] == "ENCODING":
            code = int(words[1])
            bbx: tuple[int, ...] | None = None
            for inner in lines:
                parts = inner.split()
                if parts and parts[0] == "BBX":
                    bbx = tuple(int(w) for w in parts[1:5])
                elif parts and parts[0] == "BITMAP":
                    break
            if fbb is None or bbx is None:
                fail(f"{path}: glyph {code} without a bounding box")
            rows = [0] * height
            top = (fbb[1] + fbb[3]) - (bbx[1] + bbx[3])
            shift = width - bbx[0] - (bbx[2] - fbb[2])
            for r in range(bbx[1]):
                text = next(lines).strip()
                bits = int(text, 16) >> (len(text) * 4 - bbx[0])
                if not 0 <= top + r < height or shift < 0:
                    fail(f"{path}: glyph {code} leaves the cell")
                rows[top + r] = bits << shift
            glyphs[code] = rows
    return glyphs


def arms_of(name: str) -> list[int]:
    """Line weights up, down, left, right of a box-drawing character.

    The names read "LIGHT VERTICAL AND LEFT", "DOUBLE DOWN AND RIGHT" or,
    for mixed weights, "DOWN SINGLE AND RIGHT DOUBLE".
    """
    arms = [0, 0, 0, 0]
    words = name.replace("BOX DRAWINGS ", "").replace(" AND ", " ").split()
    whole = 0
    if words[0] in ("LIGHT", "DOUBLE"):
        whole = 2 if words[0] == "DOUBLE" else 1
        words = words[1:]
    pending: list[int] = []
    for word in words:
        if word in ("SINGLE", "DOUBLE"):
            for arm in pending:
                arms[arm] = 2 if word == "DOUBLE" else 1
            pending = []
        elif word == "VERTICAL":
            pending += [0, 1]
        elif word == "HORIZONTAL":
            pending += [2, 3]
        else:
            pending.append(ARMS[word])
    for arm in pending:
        if not whole:
            fail(f"{name}: no weight")
        arms[arm] = whole
    return arms


def box(code: int, width: int) -> list[int]:
    """A line character in a width x 8 cell: lines through column cx, row 3."""
    up, down, left, right = arms_of(unicodedata.name(chr(code)))
    cx = width // 2 - 1
    cy = 3
    grid = [[0] * width for _ in range(8)]
    xs = [cx] if max(up, down) < 2 else [cx - 1, cx + 1]
    ys = [cy] if max(left, right) < 2 else [cy - 1, cy + 1]
    for weight, rows in ((up, range(ys[-1] + 1)), (down, range(ys[0], 8))):
        for x in [cx] if weight == 1 else [cx - 1, cx + 1] if weight else []:
            for y in rows:
                grid[y][x] = 1
    for weight, cols in ((left, range(xs[-1] + 1)), (right, range(xs[0], width))):
        for y in [cy] if weight == 1 else [cy - 1, cy + 1] if weight else []:
            for x in cols:
                grid[y][x] = 1
    return [int("".join(map(str, row)), 2) for row in grid]


def block(ch: int, width: int) -> list[int]:
    """A shade or block character."""
    full = (1 << width) - 1
    half = width // 2
    shades = {0xB0: (0x88, 0x22), 0xB1: (0xAA, 0x55), 0xB2: (0x77, 0xDD)}
    if ch in shades:
        return [shades[ch][y % 2] >> (8 - width) for y in range(8)]
    if ch == 0xDB:
        return [full] * 8
    if ch == 0xDC:
        return [0] * 4 + [full] * 4
    if ch == 0xDD:
        return [full >> half << half] * 8
    if ch == 0xDE:
        return [full >> half] * 8
    return [full] * 4 + [0] * 4


def cell(ch: int, width: int, glyphs: dict[int, list[int]]) -> list[int]:
    """The eight rows of character ch, leftmost pixel in bit width - 1."""
    if 0xB3 <= ch <= 0xDA:
        return box(code_point(ch), width)
    if 0xB0 <= ch <= 0xDF:
        return block(ch, width)
    if ch in (0, 32, 255):
        return [0] * 8
    if ch in INVERSE and code_point(ch) not in glyphs:
        full = (1 << width) - 1
        return [~r & full for r in cell(INVERSE[ch], width, glyphs)]
    rows = glyphs.get(code_point(ch))
    if rows is None and width == 4 and code_point(ch) in DRAWN:
        rows = [int(row.replace(".", "0").replace("#", "1"), 2) for row in DRAWN[code_point(ch)]]
    if rows is None:
        fail(f"no glyph for character {ch:02X}h in the {width}-pixel cell")
    if width == 8:
        return [r << 2 for r in rows]
    return [0, *rows, 0]


def tables(directory: Path) -> tuple[list[list[int]], list[list[int]]]:
    """The 8x8 and the 4x8 cells of all 256 characters."""
    small = parse(directory / "4x6-cp437.bdf", 4, 6)
    large = parse(directory / "5x8-cp437.bdf", 5, 8)
    return (
        [cell(ch, 8, large) for ch in range(256)],
        [cell(ch, 4, small) for ch in range(256)],
    )


def generate(directory: Path) -> str:
    wide, narrow = tables(directory)
    out = [
        "/*",
        " * Generated by font/mkfont.py from the code page 437 subsets of the",
        " * public-domain Misc-Fixed 4x6 and 5x8 fonts in font/ and the 4x6",
        " * pictures the generator draws. Do not edit; regenerate.",
        " *",
        " * font8[c][row] is one row of character c in an 8x8 cell, leftmost",
        " * pixel in bit 7. font4[c][row / 2] holds two rows of the 4x8 cell,",
        " * the upper one in the high four bits, leftmost pixel in bit 3 of",
        " * each.",
        " */",
        "#ifndef I86_FONT_H",
        "#define I86_FONT_H",
        "",
        "static const unsigned char font8[256][8] = {",
    ]
    for rows in wide:
        out.append("\t{ " + ", ".join(f"0x{r:02x}" for r in rows) + " },")
    out += ["};", "", "static const unsigned char font4[256][4] = {"]
    for rows in narrow:
        pairs = [rows[i] << 4 | rows[i + 1] for i in range(0, 8, 2)]
        out.append("\t{ " + ", ".join(f"0x{p:02x}" for p in pairs) + " },")
    out += ["};", "", "#endif", ""]
    return "\n".join(out)


def main(argv: list[str]) -> int:
    check = argv[1:2] == ["--check"]
    args = argv[2:] if check else argv[1:]
    if len(args) != 2:
        fail("usage: mkfont.py [--check] DIR OUTPUT")
    text = generate(Path(args[0]))
    target = Path(args[1])
    if not check:
        target.write_text(text, encoding="ascii")
        return 0
    if not target.exists() or target.read_text(encoding="ascii") != text:
        print(f"mkfont: {target} is not what {args[0]} generates", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
