#!/usr/bin/env python3
"""Read the PicoCalc panel model's PPM dump back as console text.

The ST7365P model (picocalc/st7365p.cs) writes the visible 320x320 image as
binary PPM. The kernel's console draws 6x10 cells (lcdterm.h LT_CELL_W and
LT_CELL_H) from the 6x9 font in sys/arch/rp2040/dev/lcdfont.c, so every cell
of a frame is one font glyph in a foreground and a background color. This
module parses that font table from the C source, matches each cell against
it and returns the 32 lines of 53 characters.

Usage:
    picocalc_fb.py frame.ppm              print the text
    picocalc_fb.py frame.ppm --png out.png  also write a PNG
    picocalc_fb.py --self-test            calibrate on a rendered frame

It is also a Robot Framework library: Screen Text, Screen Should Contain.
"""

import argparse
import os
import re
import struct
import sys
import zlib
from typing import Optional

CELL_W = 6
CELL_H = 10
COLS = 53
ROWS = 32
WIDTH = 320
HEIGHT = 320
GLYPH_ROWS = 9
FIRST_CHAR = 32
NCHARS = 95
UNKNOWN = "\ufffd"

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_SOURCE = os.path.normpath(
    os.path.join(HERE, "..", "..", "sys", "arch", "rp2040", "dev", "lcdfont.c")
)

Pixel = tuple[int, int, int]


def load_font(path: str = FONT_SOURCE) -> dict[tuple[int, ...], str]:
    """Map each glyph's nine row bytes to its character."""
    with open(path, encoding="ascii") as handle:
        text = handle.read()
    rows = re.findall(r"\{\s*((?:0x[0-9a-fA-F]{2},?\s*){9})\}", text)
    if len(rows) != NCHARS:
        raise ValueError(f"{path}: {len(rows)} glyph rows, expected {NCHARS}")
    font: dict[tuple[int, ...], str] = {}
    for index, row in enumerate(rows):
        key = tuple(int(b, 16) for b in re.findall(r"0x[0-9a-fA-F]{2}", row))
        font.setdefault(key, chr(FIRST_CHAR + index))
    return font


def read_ppm(path: str) -> tuple[int, int, bytes]:
    """Parse a binary PPM, skipping header comments."""
    with open(path, "rb") as handle:
        data = handle.read()
    pos = 0
    tokens: list[bytes] = []
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError(f"{path}: not an 8-bit binary PPM")
    width, height = int(tokens[1]), int(tokens[2])
    pixels = data[pos:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: {len(pixels)} pixel bytes for {width}x{height}")
    return width, height, pixels


def _cell_pixels(pixels: bytes, row: int, col: int) -> list[list[Pixel]]:
    out: list[list[Pixel]] = []
    for y in range(CELL_H):
        base = ((row * CELL_H + y) * WIDTH + col * CELL_W) * 3
        out.append([
            (pixels[base + 3 * x], pixels[base + 3 * x + 1], pixels[base + 3 * x + 2])
            for x in range(CELL_W)
        ])
    return out


def _match(cell: list[list[Pixel]], ink: Pixel,
           font: dict[tuple[int, ...], str]) -> Optional[str]:
    """Match the cell's nine glyph rows with ink as the foreground."""
    key = []
    for y in range(GLYPH_ROWS):
        bits = 0
        for x in range(CELL_W):
            bits = (bits << 1) | (1 if cell[y][x] == ink else 0)
        key.append(bits << 2)
    return font.get(tuple(key))


def decode_cell(cell: list[list[Pixel]], font: dict[tuple[int, ...], str]) -> str:
    """Return the glyph a cell draws, a space for a blank one, or UNKNOWN."""
    counts: dict[Pixel, int] = {}
    for line in cell[:GLYPH_ROWS]:
        for pixel in line:
            counts[pixel] = counts.get(pixel, 0) + 1
    if len(counts) == 1:
        return " "
    if len(counts) > 2:
        return UNKNOWN
    # Either color may be the ink: a reverse-video or cursor cell swaps
    # them. Prefer the reading with the fewer ink pixels.
    colors = sorted(counts, key=lambda c: counts[c])
    for ink in colors:
        found = _match(cell, ink, font)
        if found is not None:
            return found
    return UNKNOWN


def decode_screen(pixels: bytes, font: Optional[dict[tuple[int, ...], str]] = None
                  ) -> list[str]:
    """Return the ROWS lines of text in a 320x320 RGB frame."""
    table = font if font is not None else load_font()
    lines = []
    for row in range(ROWS):
        chars = [decode_cell(_cell_pixels(pixels, row, col), table)
                 for col in range(COLS)]
        lines.append("".join(chars).rstrip())
    return lines


def screen_text(path: str) -> str:
    """Robot keyword: the frame at path as newline-joined lines."""
    width, height, pixels = read_ppm(path)
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: {width}x{height}, expected {WIDTH}x{HEIGHT}")
    return "\n".join(decode_screen(pixels))


def screen_should_contain(path: str, text: str) -> None:
    """Robot keyword: fail unless some line of the frame contains text."""
    shown = screen_text(path)
    if text not in shown:
        raise AssertionError(f"{text!r} not on screen:\n{shown}")


def non_blank_pixels(path: str) -> int:
    """Robot keyword: how many pixels of the frame differ from black."""
    _, _, pixels = read_ppm(path)
    return sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] != b"\0\0\0")


def write_png(path: str, width: int, height: int, pixels: bytes) -> None:
    """Write an 8-bit RGB PNG with the standard library."""
    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    raw = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3]
                   for y in range(height))
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(raw)))
        handle.write(chunk(b"IEND", b""))


def render_text(lines: list[str], fg: Pixel = (200, 200, 200),
                bg: Pixel = (0, 0, 0)) -> bytes:
    """Draw lines with the kernel's font; the calibration input."""
    font = load_font()
    glyphs = {ch: key for key, ch in font.items()}
    frame = bytearray(bytes(bg) * (WIDTH * HEIGHT))
    for row, line in enumerate(lines[:ROWS]):
        for col, ch in enumerate(line[:COLS]):
            key = glyphs.get(ch)
            if key is None:
                continue
            for y in range(GLYPH_ROWS):
                for x in range(CELL_W):
                    if key[y] & (0x80 >> x):
                        base = ((row * CELL_H + y) * WIDTH + col * CELL_W + x) * 3
                        frame[base:base + 3] = bytes(fg)
    return bytes(frame)


def self_test() -> int:
    """Known-good and known-bad frames decide the decoder's verdict."""
    lines = ["DiscoBSD 2.7 (PICOCALC)", "login: operator", "$ uname -sr",
             "The quick brown fox, 0123456789 {}[]~#@%&"]
    good = render_text(lines)
    got = decode_screen(good)
    if got[:len(lines)] != lines or any(got[len(lines):]):
        print(f"self-test: known-good frame decoded as {got[:5]!r}", file=sys.stderr)
        return 1
    # Reverse video: swap fg and bg on one row, as the cursor cell does.
    reverse = bytearray(good)
    for y in range(CELL_H):
        base = (y * WIDTH) * 3
        for x in range(WIDTH):
            o = base + x * 3
            reverse[o:o + 3] = bytes(255 - c for c in reverse[o:o + 3])
    swapped = decode_screen(bytes(reverse))
    if swapped[0] != lines[0]:
        print(f"self-test: reverse-video row decoded as {swapped[0]!r}", file=sys.stderr)
        return 1
    # Known-bad: one stray pixel inside a glyph cell must not decode as that glyph.
    bad = bytearray(good)
    o = ((1 * CELL_H + 0) * WIDTH + 0 * CELL_W + 0) * 3
    bad[o:o + 3] = bytes((200, 200, 200))
    broken = decode_screen(bytes(bad))
    if broken[1] == lines[1]:
        print("self-test: a corrupted cell still decoded as the original", file=sys.stderr)
        return 1
    print("picocalc_fb self-test: ok (good frame, reverse video, corrupted cell)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ppm", nargs="?")
    parser.add_argument("--png")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.ppm:
        parser.error("a PPM path or --self-test is required")
    width, height, pixels = read_ppm(args.ppm)
    print(screen_text(args.ppm))
    if args.png:
        write_png(args.png, width, height, pixels)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
