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
xterm.js. The gate evaluates CP005 to CP010 on the panel model of shown() and
of shown_rounded(). pal565 truncates an entry to RGB565, and the controller
stores six bits per channel with the low bit of red and blue clear (measured:
PICOCALC.md reads white back as f8 fc f8). Two elements are assumptions, not
measurements: the full-scale mapping, which takes code 63 as full scale so that
the eight-bit value shown is the code scaled by 255/63 (white shows as 251,
255, 251), and the transfer from six-bit code to light, which the gate takes as
the sRGB curve. shown() computes the scaling by replicating the top bits,
(v << 2) | (v >> 4); shown_rounded() computes round(v * 255 / 63). The two
differ on six entries of the listed table (1, 3, 9, 11, 13, 14), and the gate
requires CP005 to CP010 to hold under both. Zero-fill arithmetic (code << 2)
is not a model of this panel; it reads the tritan minimum as 0.0798 against
the 0.08 bound and the minimum contrast as 4.98:1. The tritan minimum of colors
1-8 is 0.0813 under replication and 0.0800 under rounding, so the margin is
thin. CP004 compares each entry with its RGB565 fixed point; CP008 black and
white identity stays on the listed entries.

- CP001: the LCD table, the web theme or a test fixture is unreadable, is
  defined other than once, sits inside a comment, a string, a macro, a
  preprocessor conditional, a function or an unused object, holds a token other
  than its sixteen colors, or lacks an ANSI color key. The readers are lexical:
  they take decimal triples and unquoted JavaScript keys, and leave syntax to
  the compiler and the browser.
- CP002: the web theme differs from the LCD table at some index, or its
  background is not entry 0 or its foreground is not entry 7.
- CP003: a test fixture copy of the table differs from the LCD table.
- CP004: an entry is not a fixed point of the RGB565 conversion, so the panel
  shows a color other than the one the table names.
- CP005: two of the colors 1-8 are closer than 0.08 in Oklab under normal
  vision, protanopia, deuteranopia or tritanopia simulation (Machado 2009,
  severity 1.0, on linear sRGB), or two of the colors 9-15 are closer than
  0.10.
- CP006: a color other than black has under 5:1 contrast against black.
- CP007: a bold variant (9-14) is not 0.05 to 0.09 lighter in Oklab L than
  its base (1-6).
- CP008: entry 0 is not black, entry 15 is not white, or entry 8 is not
  neutral (Oklab chroma above 0.01), so SGR 90 stops reading as gray.
- CP009: green is under 2.5 times as luminous as red.
- CP010: entry 15 is under 0.08 lighter in Oklab L than entry 7, so bold default
  text (SGR 1 maps color 7 to 15) stops standing out.

--selftest passes the accepted table and requires, for each known-bad table
and mutant, the exact set of rules named in its case. The bounds each case
pins are enumerated by tools/check_console_palette_mutants.py, which applies
single edits to a copy of this file and requires --selftest to reject each. The live run exits 1 on
a finding and 2 when a source cannot be read or decoded, so an empty
population never passes.
"""

import argparse
import ast
import itertools
import math
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LCD_SOURCE = "sys/arch/rp2040/dev/st7365p.c"
WEB_SOURCE = "distrib/rp2040/host/src/gizmobsd_host/web.py"
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
BASE_SEPARATION = 0.08
BRIGHT_SEPARATION = 0.10
MIN_CONTRAST = 5.0
LIFT_RANGE = (0.05, 0.09)
GRAY_CHROMA = 0.01
RED_GREEN_RATIO = 2.5
BOLD_WHITE_LIFT = 0.08

Color = tuple[int, int, int]
Table = tuple[Color, ...]

GOOD: Table = (
    (0, 0, 0),
    (206, 97, 24),
    (132, 227, 140),
    (214, 182, 8),
    (99, 113, 239),
    (189, 121, 189),
    (74, 186, 239),
    (189, 190, 189),
    (132, 130, 132),
    (206, 130, 66),
    (181, 251, 156),
    (247, 195, 16),
    (115, 138, 239),
    (189, 154, 198),
    (132, 211, 255),
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
    """The color an RGB565 word names: pal565 truncates, replication widens."""
    r5, g6, b5 = color[0] >> 3, color[1] >> 2, color[2] >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def stored_codes(color: Color) -> Color:
    """The six-bit codes the controller keeps for the pixel pal565 sends.

    A sixteen-bit pixel leaves the low bit of red and blue clear, so red and
    blue hold the five-bit value shifted left once and green holds six bits
    (PICOCALC.md: white reads f8 fc f8, the codes 62, 63, 62).
    """
    return ((color[0] >> 3) << 1, color[1] >> 2, (color[2] >> 3) << 1)


def shown(color: Color) -> Color:
    """The color the panel shows for the pixel pal565 sends, replicated.

    The eight-bit value of a six-bit code s is taken as s * 255 / 63 (code 63
    is full scale; an assumption), computed by replicating the top bits,
    (s << 2) | (s >> 4). White shows as (251, 255, 251). Appending zero bits
    would instead take code 63.75 as full scale. The transfer from code to
    light is a second assumption: the gate treats it as the sRGB curve.
    """
    return tuple((v << 2) | (v >> 4) for v in stored_codes(color))  # type: ignore[return-value]


def shown_rounded(color: Color) -> Color:
    """The color the panel shows, with round(s * 255 / 63) as the scaling.

    Rounding to the nearest integer differs from replication on the table
    entries 1, 3, 9, 11, 13 and 14 by one unit in one channel.
    """
    return tuple((v * 510 + 63) // 126 for v in stored_codes(color))  # type: ignore[return-value]


def min_separation(table: Table, indexes: range) -> tuple[float, int, int, str]:
    worst = (9.0, 0, 0, "normal")
    for i, j in itertools.combinations(indexes, 2):
        for view in VIEWS:
            d = distance(table[i], table[j], view)
            if d < worst[0]:
                worst = (d, i, j, view)
    return worst


def check_table(table: Table) -> list[str]:
    """Return the findings of CP004 to CP010 for a sixteen-entry table."""
    found: list[str] = []
    for i, color in enumerate(table):
        if widen(color) != color:
            found.append(f"CP004 entry {i} {color} is an RGB565 word widened as {widen(color)}")
    if table[0] != (0, 0, 0) or table[15] != (255, 255, 255):
        found.append("CP008 entry 0 is not black or entry 15 is not white")
    found += check_shown(tuple(shown(c) for c in table))
    for finding in check_shown(tuple(shown_rounded(c) for c in table)):
        if finding not in found:
            found.append(f"{finding} (rounded scaling)")
    return found


def check_shown(panel: Table) -> list[str]:
    """Return the findings of CP005 to CP010 for the colors the panel shows."""
    found: list[str] = []
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
    if math.hypot(a, b) > GRAY_CHROMA:
        found.append(f"CP008 entry 8 has chroma {math.hypot(a, b):.4f}")
    white_lift = simulate(panel[15], "normal")[0] - simulate(panel[7], "normal")[0]
    if white_lift < BOLD_WHITE_LIFT:
        found.append(f"CP010 entry 15 is {white_lift:.3f} lighter than entry 7")
    ratio = luminance(panel[2]) / max(luminance(panel[1]), 1e-9)
    if ratio < RED_GREEN_RATIO:
        found.append(f"CP009 green is {ratio:.2f} times red in luminance")
    return found


TRIGRAPH = re.compile(r"\?\?[=(/)'<!>-]")
DIGRAPHS = (("<%", "{ "), ("%>", "} "), ("<:", "[ "), (":>", "] "), ("%:", "# "))


def mask_c(text: str) -> str:
    """Splice lines, then blank comments and the contents of string and
    character literals and spell digraphs as their tokens, so that markers and
    braces found in the result are code."""
    text = text.replace("\\\r\n", "").replace("\\\n", "")
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text[i] in "\"'":
            quote, j = text[i], i + 1
            while j < n and text[j] not in (quote, "\n"):
                j += 2 if text[j] == "\\" else 1
            out.append(quote + " " * (j - i - 1) + (text[j] if j < n else ""))
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    masked = "".join(out)
    for digraph, token in DIGRAPHS:
        masked = masked.replace(digraph, token)
    return masked


def c_context(code: str, end: int) -> tuple[int, int]:
    """Return the preprocessor-conditional depth and the brace depth at end."""
    conditional = braces = 0
    for line in code[:end].split("\n"):
        stripped = line.lstrip()
        if stripped.startswith("#"):
            word = stripped[1:].lstrip().split(" ")[0]
            if word.startswith(("if", "ifdef", "ifndef")):
                conditional += 1
            elif word.startswith("endif"):
                conditional -= 1
        else:
            braces += line.count("{") - line.count("}")
    return conditional, braces


C_TRIPLE = r"\s*\{\s*[0-9]+\s*,\s*[0-9]+\s*,\s*[0-9]+\s*\}\s*,?"
JS_QUOTES = "\"'" + chr(96)


def parse_c_table(text: str) -> Table:
    if TRIGRAPH.search(text):
        raise ValueError("the source holds a trigraph, which gnu17 ignores and c17 reads")
    code = mask_c(text)
    if re.search(r"^\s*#\s*(define|undef)\s+lcd_palette\b", code, re.M | re.A):
        raise ValueError("a preprocessor line defines or undefines lcd_palette")
    starts = list(re.finditer(r"\blcd_palette\s*\[\s*16\s*\]\s*\[\s*3\s*\]\s*=\s*\{", code, re.A))
    if len(starts) != 1:
        raise ValueError(f"{len(starts)} definitions of lcd_palette[16][3]")
    line_start = code.rfind("\n", 0, starts[0].start()) + 1
    if code[line_start : starts[0].start()].lstrip().startswith("#"):
        raise ValueError("lcd_palette is defined inside a preprocessor directive")
    conditional, braces = c_context(code, starts[0].start())
    if conditional != 0 or braces != 0:
        raise ValueError("lcd_palette is not an unconditional file-scope definition")
    body = re.compile(rf"((?:{C_TRIPLE})+)\s*\}}\s*;", re.A).match(code, starts[0].end())
    if body is None:
        raise ValueError("the initializer holds a token other than decimal RGB triples")
    numbers = [int(v) for v in re.findall(r"[0-9]+", body.group(1), re.A)]
    if len(numbers) != 48:
        raise ValueError(f"lcd_palette holds {len(numbers) // 3} entries")
    for index, value in enumerate(numbers):
        if value > 255:
            raise ValueError(f"entry {index // 3} holds the component {value}")
    return tuple((numbers[i], numbers[i + 1], numbers[i + 2]) for i in range(0, 48, 3))


def page_javascript(source: str) -> str:
    """Return the JavaScript text of the PAGE f-string in web.py: its literal
    parts, with every interpolation replaced by a placeholder token."""
    try:
        tree = ast.parse(source)
    except SyntaxError as error:
        raise ValueError(f"web.py does not parse: {error.msg}") from error
    pages = [
        node.value
        for node in tree.body
        if isinstance(node, ast.Assign)
        and any(isinstance(t, ast.Name) and t.id == "PAGE" for t in node.targets)
    ]
    if len(pages) != 1:
        raise ValueError(f"{len(pages)} PAGE assignments")
    value = pages[0]
    parts: list[str] = []
    for piece in value.values if isinstance(value, ast.JoinedStr) else [value]:
        if isinstance(piece, ast.Constant) and isinstance(piece.value, str):
            parts.append(piece.value)
        else:
            parts.append("0")
    return "".join(parts)


def mask_js(text: str) -> str:
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i) or text.startswith("<!--", i):
            end = "*/" if text.startswith("/*", i) else "-->"
            j = text.find(end, i + 2)
            j = n if j < 0 else j + len(end)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i) and (i == 0 or text[i - 1] not in ":\"'"):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text[i] in JS_QUOTES:
            quote, j = text[i], i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == "\\" else 1
            out.append(quote + " " * (j - i - 1) + (text[j] if j < n else ""))
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def matching_brace(code: str, start: int) -> int:
    depth = 0
    for i in range(start, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced braces")


def parse_web_theme(source: str) -> tuple[Table, dict[str, Color]]:
    """Return the sixteen ANSI colors and every color key of the theme object
    that the single new Terminal(...) call receives."""
    js = page_javascript(source)
    code = mask_js(js)
    calls = list(re.finditer(r"\bnew\s+Terminal\s*\(\s*\{", code))
    if len(calls) != 1:
        raise ValueError(f"{len(calls)} new Terminal calls")
    options_start = calls[0].end() - 1
    options_end = matching_brace(code, options_start)
    options = code[options_start : options_end + 1]
    themes = [
        m
        for m in re.finditer(r"\btheme\s*:\s*\{", options)
        if options[: m.start()].count("{") - options[: m.start()].count("}") == 1
    ]
    if len(themes) != 1:
        raise ValueError(f"{len(themes)} theme properties in the Terminal options")
    start = options_start + themes[0].end() - 1
    end = matching_brace(code, start)
    body = js[start + 1 : end]
    item = r'\s*[A-Za-z]+\s*:\s*"#[0-9a-fA-F]{6}"\s*,?'
    if re.fullmatch(rf"(?:{item})+\s*", body, re.A) is None:
        raise ValueError("the theme holds a token other than quoted hex colors")
    pairs = re.findall(r'([A-Za-z]+)\s*:\s*"#([0-9a-fA-F]{6})"', body)
    keys = [k for k, _ in pairs]
    if len(set(keys)) != len(keys) or any(k not in WEB_KEYS + WEB_EXTRA_KEYS for k in keys):
        raise ValueError("the theme repeats or adds a key")
    colors = {k: (int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16)) for k, v in pairs}
    if any(k not in colors for k in WEB_KEYS + ("background", "foreground")):
        raise ValueError("the theme lacks an ANSI color, background or foreground key")
    return tuple(colors[k] for k in WEB_KEYS), colors


def check_sources(lcd: str, web: str, fixtures: dict[str, str]) -> list[str]:
    found: list[str] = []
    try:
        table = parse_c_table(lcd)
    except ValueError as error:
        return [f"CP001 {LCD_SOURCE}: {error}"]
    try:
        theme, colors = parse_web_theme(web)
    except ValueError as error:
        found.append(f"CP001 {WEB_SOURCE}: {error}")
    else:
        for i, (a, b) in enumerate(zip(table, theme)):
            if a != b:
                found.append(f"CP002 index {i}: LCD {a}, web {b}")
        if colors["background"] != table[0]:
            found.append(f"CP002 background {colors['background']} is not entry 0")
        if colors["foreground"] != table[7]:
            found.append(f"CP002 foreground {colors['foreground']} is not entry 7")
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


def theme_object(
    table: Table, foreground: Color | None = None, background: Color | None = None
) -> str:
    def hexcolor(c: Color) -> str:
        return f'"#{c[0]:02x}{c[1]:02x}{c[2]:02x}"'

    bg, fg = background or table[0], foreground or table[7]
    head = f"background:{hexcolor(bg)},foreground:{hexcolor(fg)},"
    return "{{" + head + ",".join(f"{k}:{hexcolor(c)}" for k, c in zip(WEB_KEYS, table)) + "}}"


def web_text(table: Table, theme: str | None = None, extra: str = "") -> str:
    theme = theme or theme_object(table)
    return (
        'PAGE = f"""<script src="{XTERM}"></script><script>\n'
        f"{extra}var term=new Terminal({{{{cols:80,rows:24,theme:{theme},"
        'minimumContrastRatio:4.5}});\n</script>"""\n'
    )


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
    good_c = c_text(GOOD)
    compact = (
        "const u_char lcd_palette[16][3]={" + "".join("{%d,%d,%d}," % c for c in GOOD) + "};\n"
    )

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
    expect("accepted table without whitespace", set(), lcd=compact)
    digraphs = good_c.replace("[16][3]", "<:16:><:3:>").replace("{", "<%").replace("}", "%>")
    expect("accepted table spelled with digraphs", set(), lcd=digraphs)
    expect("first branch table", {"CP004", "CP005", "CP006", "CP008", "CP010"}, FIRST)
    expect("table before the palette", {"CP004", "CP005", "CP006", "CP007"}, PREVIOUS)
    expect("off-grid entry", {"CP004"}, with_entries(e1=(205, 93, 0)))
    expect("two colors too close", {"CP005"}, with_entries(e7=GOOD[3]))
    # Each pair below sits between the 0.05 that a relaxed bound would admit and
    # the stated bound, and fails under one color-vision view alone, so the
    # case pins that bound and that view. Removing the normal view cannot be
    # pinned: no entry changed alone on the grid fails only under normal vision.
    expect("protan pair in 1-8", {"CP005"}, with_entries(e6=(99, 186, 231)))
    expect("deutan pair in 1-8", {"CP005"}, with_entries(e2=(148, 227, 115)))
    expect("tritan pair in 1-8", {"CP005"}, with_entries(e6=(49, 190, 255)))
    expect("pair short only with clear low bits", {"CP005"}, with_entries(e6=(99, 190, 247)))
    expect("entry 8 near entry 5", {"CP005"}, with_entries(e8=(165, 162, 165)))
    expect("bright pair in 9-15", {"CP005"}, with_entries(e14=(156, 199, 255)))
    expect("bright pair with entry 15", {"CP005"}, with_entries(e10=(206, 239, 165)))
    expect("bright pair just under 0.10", {"CP005"}, with_entries(e14=(66, 223, 239)))
    expect("pair at entry 1 in 1-8", {"CP005"}, with_entries(e1=(189, 113, 66)))
    expect("pair at entry 9 in 9-15", {"CP005", "CP007"}, with_entries(e9=(189, 255, 165)))
    expect(
        "entry 8 near entry 5 under the clear blue bit", {"CP005"}, with_entries(e8=(165, 154, 156))
    )
    expect("entry 8 faint tint", {"CP005", "CP008"}, with_entries(e8=(198, 186, 189)))
    expect("bold lift just under 0.05", {"CP007"}, with_entries(e9=(198, 130, 49)))
    expect("bold lift just over 0.09", {"CP007"}, with_entries(e12=(140, 142, 255)))
    expect("bold variant darker than its base", {"CP007"}, with_entries(e13=(140, 117, 148)))
    expect("bold lift short only under rounded scaling", {"CP007"}, with_entries(e1=(222, 81, 82)))
    expect("pair short only under replicated scaling", {"CP005"}, with_entries(e1=(214, 89, 82)))
    expect("entry 15 below 5:1", {"CP006", "CP008", "CP010"}, with_entries(e15=(115, 117, 115)))
    expect("bold white just under 0.08", {"CP010"}, with_entries(e7=(231, 231, 206)))
    expect(
        "bold white lift is lightness, not luminance", {"CP010"}, with_entries(e7=(239, 251, 74))
    )
    expect(
        "bold white darker than default text", {"CP008", "CP010"}, with_entries(e15=(231, 48, 148))
    )
    expect("entry 1 just under 5:1", {"CP006"}, with_entries(e1=(222, 73, 41)))
    expect("low contrast", {"CP006"}, with_entries(e1=(198, 89, 16)))
    expect("bold lift too small", {"CP007"}, with_entries(e1=(231, 81, 41)))
    expect("bold lift slightly small", {"CP007"}, with_entries(e13=(173, 158, 173)))
    expect("bold lift slightly large", {"CP007"}, with_entries(e9=(239, 130, 24)))
    expect("bold lift large at entry 14", {"CP007"}, with_entries(e14=(107, 227, 214)))
    expect("entry 0 not black", {"CP008"}, with_entries(e0=(8, 4, 8)))
    expect("entry 15 not white", {"CP008"}, with_entries(e15=(255, 255, 247)))
    expect("tinted entry 8", {"CP008"}, with_entries(e8=(123, 130, 123)))
    expect("bold white barely lighter", {"CP010"}, with_entries(e7=(247, 247, 239)))
    expect(
        "red nearly as luminous as green",
        {"CP009"},
        with_entries(e1=(206, 117, 0), e9=(214, 142, 41)),
    )
    swapped = GOOD[:1] + (GOOD[2], GOOD[1]) + GOOD[3:]
    expect(
        "web theme with a swapped pair",
        {"CP002"},
        web=web_text(GOOD, theme_object(swapped, GOOD[7])),
    )
    expect("web foreground black", {"CP002"}, web=web_text(GOOD, theme_object(GOOD, (0, 0, 0))))
    expect(
        "web background differs from entry 0",
        {"CP002"},
        web=web_text(GOOD, theme_object(GOOD, background=(8, 4, 8))),
    )
    expect("stale fixture", {"CP003"}, fixture=c_text(FIRST))
    bypass_c = {
        "no table": "int x;\n",
        "token inside the initializer": good_c.replace("{ 0, 0, 0 }", "BROKEN, { 0, 0, 0 }", 1),
        "table inside #if 0": "#if 0\n" + good_c + "#endif\n",
        "table in an inactive #else": "#if 1\nint a;\n#else\n" + good_c + "#endif\n",
        "#if spelled with a line splice": "#i\\\nf 0\n" + good_c + "#endif\n",
        "good table disabled, old table active": "#if 0\n" + good_c + "#endif\n" + c_text(PREVIOUS),
        "table only in a macro": "#define PALETTE " + good_c.replace("\n", " \\\n"),
        "symbol renamed by a macro": "#define lcd_palette hidden_palette\n" + good_c,
        "duplicate split by a line splice": good_c
        + good_c.replace("lcd_palette", "lcd_pal\\\nette", 1),
        "slashes in a string hide a duplicate": good_c + 'const char *u = "http://x"; ' + good_c,
        "comment markers in strings hide a duplicate": good_c
        + 'const char *a = "/*"; '
        + good_c
        + ' const char *b = "*/";\n',
        "table with function scope": "void f(void) {\n" + good_c + "}\n",
        "table in a macro, digraph hash": "%:define PALETTE " + good_c.replace("\n", " \\\n"),
        "table disabled, digraph hash": "%:if 0\n" + good_c + "%:endif\n",
        "function scope in digraph braces": "void f(void) <%\n" + good_c + "%>\n",
        "function scope in trigraph braces": "void f(void) ??<\n" + good_c + "??>\n",
        "non-ASCII digits": good_c.replace("{ 0, 0, 0 }", "{ ٠, ٠, ٠ }", 1),
        "component over 255": good_c.replace("{ 0, 0, 0 }", "{ 256, 0, 0 }", 1),
        "good table commented, none active": "/*\n" + good_c + "*/\n",
    }
    for label, text in bypass_c.items():
        expect(label, {"CP001"}, lcd=text)
    expect(
        "good table commented, old table active",
        {"CP004", "CP005", "CP006", "CP007"},
        lcd="/*\n" + good_c + "*/\n" + c_text(PREVIOUS),
        web=web_text(PREVIOUS),
        fixture=c_text(PREVIOUS),
    )
    obj = theme_object(GOOD)
    unquoted = obj.replace('"#', "#")
    bypass_web = {
        "no theme": "PAGE = f'''<script>var term=new Terminal({{{{cols:80}}}});</script>'''\n",
        "theme without keys": web_text(GOOD, "{{}}"),
        "token inside the theme": web_text(GOOD, obj.replace("black:", "oops!,black:", 1)),
        "unquoted colors": web_text(GOOD, unquoted),
        "theme only in a JavaScript string": web_text(
            GOOD, "{{}}", "var decoy='theme:" + obj + "';\n"
        ),
        "theme in an unused object": web_text(GOOD, "{{}}", "var unused={{theme:" + obj + "}};\n"),
        "theme in a block comment": web_text(GOOD, "{{}}", "/* theme:" + obj + " */\n"),
        "comment markers in strings hide a theme": web_text(
            GOOD, "{{}}", 'var a="/*"; var b={{theme:' + obj + '}}; var c="*/";\n'
        ),
        "two Terminal calls": web_text(
            GOOD, extra="var other=new Terminal({{theme:" + obj + "}});\n"
        ),
        "repeated key": web_text(GOOD, obj.replace("}}", ',red:"#ce5d00"}}')),
    }
    for label, text in bypass_web.items():
        expect(label, {"CP001"}, web=text)
    expect(
        "theme in a Python comment",
        {"CP001"},
        web="# " + web_text(GOOD).replace("\n", "\n# ") + "PAGE = ''\n",
    )
    expect("web.py with a syntax error", {"CP001"}, web="PAGE = f'''\n")
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
    # No table on the grid isolates the normal view, so the use site is pinned
    # by a pair whose normal-view distance is the smallest of its four views.
    cases += 1
    pair = ((93, 28, 128), (94, 26, 122))
    if min_separation(pair, range(2))[3] != "normal":
        errors.append(f"normal view is the worst view of a pair: expected normal, got {pair}")
    cases += 1
    if VIEWS != ("normal", "protan", "deutan", "tritan") or set(MACHADO) != set(VIEWS[1:]):
        errors.append(f"VIEWS {VIEWS} is not normal, protan, deutan and tritan")
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
        "meet CP004-CP010 on the colors the panel shows"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
