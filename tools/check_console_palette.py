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

"""Check the console palette against the bounds its comment states.

The PicoCalc LCD console draws sixteen colors from lcd_palette in
sys/arch/rp2040/dev/st7365p.c, and the web console gives the same sixteen to
xterm.js. The panel takes each entry through the truncating RGB565 conversion
of pal565 and widens it again, so rules CP005 to CP009 run on the widened
colors and CP004 compares them with the listed ones.

- CP001: the LCD table, the web theme or a test fixture is unreadable, is
  defined other than once, sits inside a preprocessor conditional or a comment,
  holds a token other than its sixteen colors, or lacks an ANSI color key.
- CP002: the web theme differs from the LCD table at some index.
- CP003: a test fixture copy of the table differs from the LCD table.
- CP004: an entry is not a fixed point of the RGB565 conversion, so the panel
  shows a color other than the one the table names.
- CP005: two of the colors 1-8 are closer than 0.09 in Oklab under normal
  vision, protanopia, deuteranopia or tritanopia simulation (Machado 2009,
  severity 1.0, on linear sRGB), or two of the colors 9-15 are closer than
  0.10.
- CP006: a color other than black has under 5:1 contrast against black.
- CP007: a bold variant (9-14) is not 0.05 to 0.09 lighter in Oklab L than
  its base (1-6).
- CP008: entry 0 is not black, entry 15 is not white, or entry 8 is not
  neutral (Oklab chroma above 0.01), so SGR 90 stops reading as gray.
- CP009: green is under 2.5 times as luminous as red.

--selftest passes the accepted table and requires, for each known-bad table
and mutant, the exact set of rules named in its case. The live run exits 1 on
a finding and 2 when a source cannot be read or decoded, so an empty
population never passes.
"""

import argparse
import itertools
import math
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LCD_SOURCE = "sys/arch/rp2040/dev/st7365p.c"
WEB_SOURCE = "distrib/rp2040/host/src/discobsd_host/web.py"
FIXTURES = ("tests/picocalc/lcdgfx_test.c", "tests/picocalc/lcdgfx_ext_test.c")
WEB_KEYS = (
    "black",
    "red",
    "green",
    "yellow",
    "blue",
    "magenta",
    "cyan",
    "white",
    "brightBlack",
    "brightRed",
    "brightGreen",
    "brightYellow",
    "brightBlue",
    "brightMagenta",
    "brightCyan",
    "brightWhite",
)
WEB_EXTRA_KEYS = ("background", "foreground", "cursor", "selectionBackground")
VIEWS = ("normal", "protan", "deutan", "tritan")
MACHADO = {
    "protan": (
        (0.152286, 1.052583, -0.204868),
        (0.114503, 0.786281, 0.099216),
        (-0.003882, -0.048116, 1.051998),
    ),
    "deutan": (
        (0.367322, 0.860646, -0.227968),
        (0.280085, 0.672501, 0.047413),
        (-0.011820, 0.042940, 0.968881),
    ),
    "tritan": (
        (1.255528, -0.076749, -0.178779),
        (-0.078411, 0.930809, 0.147602),
        (0.004733, 0.691367, 0.303900),
    ),
}
BASE_SEPARATION = 0.09
BRIGHT_SEPARATION = 0.10
MIN_CONTRAST = 5.0
LIFT_RANGE = (0.05, 0.09)
GRAY_CHROMA = 0.01
RED_GREEN_RATIO = 2.5

Color = tuple[int, int, int]
Table = tuple[Color, ...]

GOOD: Table = (
    (0, 0, 0),
    (206, 93, 0),
    (33, 231, 165),
    (214, 190, 33),
    (99, 109, 255),
    (189, 117, 206),
    (140, 186, 214),
    (247, 247, 239),
    (132, 134, 132),
    (231, 113, 66),
    (82, 251, 165),
    (247, 207, 33),
    (132, 130, 255),
    (198, 146, 206),
    (181, 207, 231),
    (255, 255, 255),
)

# The first branch revision: the 8-bit optimum, which the truncating
# conversion moves below its stated bounds.
FIRST: Table = (
    (0, 0, 0),
    (188, 102, 47),
    (32, 230, 166),
    (212, 192, 33),
    (93, 111, 245),
    (180, 120, 190),
    (135, 186, 206),
    (248, 248, 242),
    (123, 134, 184),
    (211, 123, 70),
    (75, 254, 188),
    (235, 215, 68),
    (112, 133, 255),
    (203, 141, 213),
    (157, 209, 229),
    (255, 255, 255),
)

# The table before the color-vision palette.
PREVIOUS: Table = (
    (0, 0, 0),
    (205, 0, 0),
    (0, 205, 0),
    (205, 205, 0),
    (60, 90, 238),
    (205, 0, 205),
    (0, 205, 205),
    (200, 200, 200),
    (127, 127, 127),
    (255, 60, 60),
    (60, 255, 60),
    (255, 255, 60),
    (100, 120, 255),
    (255, 60, 255),
    (60, 255, 255),
    (255, 255, 255),
)


def linear(value: int) -> float:
    c = value / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def simulate(color: Color, view: str) -> tuple[float, float, float]:
    lin = [linear(v) for v in color]
    if view != "normal":
        m = MACHADO[view]
        lin = [sum(m[i][j] * lin[j] for j in range(3)) for i in range(3)]
    r, g, b = (min(max(c, 0.0), 1.0) for c in lin)
    lms = (
        0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b,
        0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b,
        0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b,
    )
    l_, m_, s_ = (math.copysign(abs(x) ** (1 / 3), x) for x in lms)
    return (
        0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_,
    )


def distance(a: Color, b: Color, view: str) -> float:
    return math.dist(simulate(a, view), simulate(b, view))


def luminance(color: Color) -> float:
    r, g, b = (linear(v) for v in color)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast_on_black(color: Color) -> float:
    return (luminance(color) + 0.05) / 0.05


def widen(color: Color) -> Color:
    """The color the panel shows: pal565 truncates, the controller widens."""
    r5, g6, b5 = color[0] >> 3, color[1] >> 2, color[2] >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def min_separation(table: Table, indexes: range) -> tuple[float, int, int, str]:
    worst = (9.0, 0, 0, "normal")
    for i, j in itertools.combinations(indexes, 2):
        for view in VIEWS:
            d = distance(table[i], table[j], view)
            if d < worst[0]:
                worst = (d, i, j, view)
    return worst


def check_table(table: Table) -> list[str]:
    """Return the findings of CP004 to CP009 for a sixteen-entry table."""
    found: list[str] = []
    panel = tuple(widen(c) for c in table)
    for i, (color, shown) in enumerate(zip(table, panel)):
        if shown != color:
            found.append(f"CP004 entry {i} {color} is shown as {shown}")
    d, i, j, view = min_separation(panel, range(1, 9))
    if d < BASE_SEPARATION:
        found.append(f"CP005 entries {i} and {j} are {d:.3f} apart under {view}")
    d, i, j, view = min_separation(panel, range(9, 16))
    if d < BRIGHT_SEPARATION:
        found.append(f"CP005 entries {i} and {j} are {d:.3f} apart under {view}")
    for i in range(1, 16):
        if contrast_on_black(panel[i]) < MIN_CONTRAST:
            found.append(f"CP006 entry {i} has {contrast_on_black(panel[i]):.2f}:1")
    for i in range(1, 7):
        lift = simulate(panel[i + 8], "normal")[0] - simulate(panel[i], "normal")[0]
        if not LIFT_RANGE[0] <= lift <= LIFT_RANGE[1]:
            found.append(f"CP007 entry {i + 8} is {lift:.3f} lighter than entry {i}")
    _, a, b = simulate(panel[8], "normal")
    if panel[0] != (0, 0, 0) or panel[15] != (255, 255, 255):
        found.append("CP008 entry 0 is not black or entry 15 is not white")
    if math.hypot(a, b) > GRAY_CHROMA:
        found.append(f"CP008 entry 8 has chroma {math.hypot(a, b):.4f}")
    ratio = luminance(panel[2]) / max(luminance(panel[1]), 1e-9)
    if ratio < RED_GREEN_RATIO:
        found.append(f"CP009 green is {ratio:.2f} times red in luminance")
    return found


def blank(match: re.Match[str]) -> str:
    return re.sub(r"[^\n]", " ", match.group())


def strip_c_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", blank, text, flags=re.S)


def strip_web_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/|<!--.*?-->", blank, text, flags=re.S)
    return re.sub(r"(?<![:\w\"'])//[^\n]*", blank, text)


def conditional_depth(text: str, end: int) -> int:
    depth = 0
    for line in text[:end].splitlines():
        word = line.lstrip().removeprefix("#").lstrip().split(" ")[0]
        if line.lstrip().startswith("#"):
            if word.startswith(("if", "ifdef", "ifndef")):
                depth += 1
            elif word.startswith("endif"):
                depth -= 1
    return depth


C_TRIPLE = r"\s*\{\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\}\s*,?"


def parse_c_table(text: str) -> Table:
    code = strip_c_comments(text)
    starts = list(re.finditer(r"\blcd_palette\s*\[\s*16\s*\]\s*\[\s*3\s*\]\s*=\s*\{", code))
    if len(starts) != 1:
        raise ValueError(f"{len(starts)} definitions of lcd_palette[16][3]")
    if conditional_depth(code, starts[0].start()) != 0:
        raise ValueError("lcd_palette sits inside a preprocessor conditional")
    body = re.compile(rf"((?:{C_TRIPLE})+)\s*\}}\s*;").match(code, starts[0].end())
    if body is None:
        raise ValueError("the initializer holds a token other than RGB triples")
    triples = re.findall(r"\d+", body.group(1))
    table = tuple(
        (int(triples[i]), int(triples[i + 1]), int(triples[i + 2]))
        for i in range(0, len(triples), 3)
    )
    if len(table) != 16 or any(max(t) > 255 for t in table):
        raise ValueError(f"lcd_palette holds {len(table)} entries")
    return table


def parse_web_theme(text: str) -> Table:
    code = strip_web_comments(text)
    starts = list(re.finditer(r"\btheme\s*:\s*\{\{", code))
    if len(starts) != 1:
        raise ValueError(f"{len(starts)} xterm.js theme objects")
    item = r'\s*\w+\s*:\s*"#[0-9a-fA-F]{6}"\s*,?'
    body = re.compile(rf"((?:{item})+)\s*\}}\}}").match(code, starts[0].end())
    if body is None:
        raise ValueError("the theme holds a token other than quoted hex colors")
    pairs = re.findall(r'(\w+)\s*:\s*"#([0-9a-fA-F]{6})"', body.group(1))
    keys = [k for k, _ in pairs]
    if len(set(keys)) != len(keys) or any(k not in WEB_KEYS + WEB_EXTRA_KEYS for k in keys):
        raise ValueError("the theme repeats or adds a key")
    colors = dict(pairs)
    if any(k not in colors for k in WEB_KEYS):
        raise ValueError("the theme lacks an ANSI color key")
    return tuple(
        (int(colors[k][0:2], 16), int(colors[k][2:4], 16), int(colors[k][4:6], 16))
        for k in WEB_KEYS
    )


def check_sources(lcd: str, web: str, fixtures: dict[str, str]) -> list[str]:
    found: list[str] = []
    try:
        table = parse_c_table(lcd)
    except ValueError as error:
        return [f"CP001 {LCD_SOURCE}: {error}"]
    try:
        theme = parse_web_theme(web)
    except ValueError as error:
        found.append(f"CP001 {WEB_SOURCE}: {error}")
    else:
        for i, (a, b) in enumerate(zip(table, theme)):
            if a != b:
                found.append(f"CP002 index {i}: LCD {a}, web {b}")
    for name, text in fixtures.items():
        try:
            copy = parse_c_table(text)
        except ValueError as error:
            found.append(f"CP001 {name}: {error}")
            continue
        for i, (a, b) in enumerate(zip(table, copy)):
            if a != b:
                found.append(f"CP003 {name} index {i}: LCD {a}, fixture {b}")
    return found + check_table(table)


def c_text(table: Table) -> str:
    rows = ",\n".join("\t{ %d, %d, %d }" % c for c in table)
    return f"const u_char lcd_palette[16][3] = {{\n{rows},\n}};\n"


def web_text(table: Table) -> str:
    items = ",".join(f'{k}:"#{c[0]:02x}{c[1]:02x}{c[2]:02x}"' for k, c in zip(WEB_KEYS, table))
    return f'theme:{{{{background:"#000000",{items}}}}}'


def with_entries(**changes: Color) -> Table:
    table = list(GOOD)
    for index, color in changes.items():
        table[int(index.removeprefix("e"))] = color
    return tuple(table)


def load_sources(root: Path) -> tuple[str, str, dict[str, str]]:
    def read(name: str) -> str:
        return (root / name).read_bytes().decode("utf-8")

    return read(LCD_SOURCE), read(WEB_SOURCE), {f: read(f) for f in FIXTURES}


def rule_set(found: list[str]) -> set[str]:
    return {f.split()[0] for f in found}


def selftest() -> list[str]:
    errors: list[str] = []
    cases = 0

    def expect(label: str, rules: set[str], table: Table = GOOD, **sources: str) -> None:
        nonlocal cases
        cases += 1
        found = check_sources(
            sources.get("lcd", c_text(table)),
            sources.get("web", web_text(table)),
            {"fixture": sources.get("fixture", c_text(table))},
        )
        if rule_set(found) != rules:
            errors.append(f"{label}: expected {sorted(rules)}, got {found}")

    expect("accepted table", set())
    expect("first branch table", {"CP004", "CP005", "CP006", "CP008"}, FIRST)
    expect("table before the palette", {"CP004", "CP005", "CP006", "CP007"}, PREVIOUS)
    expect("off-grid entry", {"CP004"}, with_entries(e1=(205, 93, 0)))
    expect("two colors too close", {"CP005"}, with_entries(e1=(165, 130, 0)))
    expect("low contrast", {"CP006"}, with_entries(e1=(198, 89, 16)))
    expect("bold lift too small", {"CP007"}, with_entries(e1=(231, 81, 41)))
    expect("tinted entry 8", {"CP008"}, with_entries(e8=(132, 121, 140)))
    expect(
        "red nearly as luminous as green",
        {"CP009"},
        with_entries(e1=(222, 105, 0), e9=(231, 125, 90)),
    )
    expect(
        "web theme with a swapped pair",
        {"CP002"},
        web=web_text(GOOD[:1] + (GOOD[2], GOOD[1]) + GOOD[3:]),
    )
    expect("stale fixture", {"CP003"}, fixture=c_text(FIRST))
    expect("no table", {"CP001"}, lcd="int x;\n")
    expect("web theme without keys", {"CP001"}, web="theme:{{}}")
    expect(
        "token inside the initializer",
        {"CP001"},
        lcd=c_text(GOOD).replace("{ 0, 0, 0 }", "BROKEN, { 0, 0, 0 }", 1),
    )
    expect(
        "table inside a disabled branch",
        {"CP001"},
        lcd="#if 0\n" + c_text(GOOD) + "#endif\n",
    )
    expect(
        "good table disabled, old table active",
        {"CP001"},
        lcd="#if 0\n" + c_text(GOOD) + "#endif\n" + c_text(PREVIOUS),
    )
    expect(
        "good table commented, old table active",
        {"CP004", "CP005", "CP006", "CP007"},
        lcd="/*\n" + c_text(GOOD) + "*/\n" + c_text(PREVIOUS),
        web=web_text(PREVIOUS),
        fixture=c_text(PREVIOUS),
    )
    expect(
        "token inside the web theme",
        {"CP001"},
        web=web_text(GOOD).replace("black:", "oops!,black:", 1),
    )
    expect(
        "web theme commented, old theme active",
        {"CP002"},
        web="/* " + web_text(GOOD) + " */ " + web_text(PREVIOUS),
    )
    expect("two web themes", {"CP001"}, web=web_text(GOOD) + web_text(GOOD))
    for label, data in (("missing source", None), ("invalid UTF-8", b"\xff\xfe")):
        cases += 1
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            if data is not None:
                for name in (LCD_SOURCE, WEB_SOURCE, *FIXTURES):
                    (root / name).parent.mkdir(parents=True, exist_ok=True)
                    (root / name).write_bytes(data)
            try:
                load_sources(root)
            except (OSError, UnicodeDecodeError):
                continue
            errors.append(f"{label}: load_sources did not raise")
    print(f"check-console-palette selftest: {cases} cases")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        errors = selftest()
        for error in errors:
            print(f"check-console-palette selftest: {error}", file=sys.stderr)
        return 1 if errors else 0
    try:
        lcd, web, fixtures = load_sources(args.root)
    except (OSError, UnicodeDecodeError) as error:
        print(f"check-console-palette: {error}", file=sys.stderr)
        return 2
    found = check_sources(lcd, web, fixtures)
    for finding in found:
        print(f"check-console-palette: {finding}", file=sys.stderr)
    if found:
        return 1
    print(
        "check-console-palette: LCD table, web theme and fixtures agree and "
        "meet CP004-CP009 on the widened colors"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
