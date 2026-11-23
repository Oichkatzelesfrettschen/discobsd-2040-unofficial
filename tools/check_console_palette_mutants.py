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

"""Require tools/check_console_palette.py --selftest to reject each mutant.

Each entry of MUTANTS is a single textual edit of the gate: a name, the exact
text to replace (it must occur once), its replacement, and either the label of
the selftest case that must reject the mutant or EQUIVALENT with a reason. The
run copies the gate into a temporary directory once per mutant, applies the
edit and runs --selftest on the copy. A mutant counts as rejected when the copy
exits nonzero and its stderr names the expected case; an equivalent mutant must
still pass --selftest, so a listed equivalent that a later case begins to
reject is reported and moved to the rejected set by hand. An unmodified copy
must pass first, which calibrates the harness against a known-good input.

The edits cover, for each rule CP004 to CP010, every numeric bound relaxed by
about five percent in the permissive direction, every index range shortened by
one at each end, every color-vision view dropped at its use site, every check
disabled, the measure swapped where a rule names one, and the panel model
replaced (zero-fill, low bit kept, a channel's clear bit skipped). The contrast
bound is also relaxed by 0.2 percent. A bound is pinned to the step its mutant
relaxes, not to the last RGB565 grid color.
"""

import argparse
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

GATE = Path(__file__).resolve().parent / "check_console_palette.py"
EQUIVALENT = "EQUIVALENT"
LABEL_PREFIX = "check-console-palette selftest: "

# (name, old text, new text, rejecting case label or "EQUIVALENT: reason")
MUTANTS: list[tuple[str, str, str, str]] = []


def mutant(name: str, old: str, new: str, case: str) -> None:
    MUTANTS.append((name, old, new, case))


def build_mutants() -> None:
    mutant("CP004 check disabled", "if widen(color) != color:", "if False:", "off-grid entry")
    mutant(
        "CP004 widen without green replication",
        "(g6 << 2) | (g6 >> 4)",
        "(g6 << 2)",
        "accepted table",
    )
    mutant(
        "CP004 widen without red replication",
        "(r5 << 3) | (r5 >> 2)",
        "(r5 << 3)",
        "accepted table",
    )
    mutant(
        "CP004 widen without blue replication",
        "(b5 << 3) | (b5 >> 2)",
        "(b5 << 3)",
        "accepted table",
    )
    # CP005
    mutant(
        "CP005 base bound relaxed 5 percent",
        "BASE_SEPARATION = 0.08",
        "BASE_SEPARATION = 0.076",
        "pair short only with clear low bits",
    )
    mutant(
        "CP005 bright bound relaxed 5 percent",
        "BRIGHT_SEPARATION = 0.10",
        "BRIGHT_SEPARATION = 0.095",
        "bright pair just under 0.10",
    )
    mutant(
        "CP005 base range starts at 2",
        "min_separation(panel, range(1, 9))",
        "min_separation(panel, range(2, 9))",
        "pair at entry 1 in 1-8",
    )
    mutant(
        "CP005 base range ends at 7",
        "min_separation(panel, range(1, 9))",
        "min_separation(panel, range(1, 8))",
        "entry 8 near entry 5",
    )
    mutant(
        "CP005 bright range starts at 10",
        "min_separation(panel, range(9, 16))",
        "min_separation(panel, range(10, 16))",
        "pair at entry 9 in 9-15",
    )
    mutant(
        "CP005 bright range ends at 14",
        "min_separation(panel, range(9, 16))",
        "min_separation(panel, range(9, 15))",
        "bright pair with entry 15",
    )
    mutant(
        "CP005 base check disabled", "if d < BASE_SEPARATION:", "if False:", "two colors too close"
    )
    mutant(
        "CP005 bright check disabled",
        "if d < BRIGHT_SEPARATION:",
        "if False:",
        "bright pair in 9-15",
    )
    mutant(
        "CP005 normal view dropped",
        "        for view in VIEWS:\n            d = distance",
        "        for view in VIEWS[1:]:\n            d = distance",
        "normal view is the worst view of a pair",
    )
    mutant(
        "CP005 protan view dropped",
        "        for view in VIEWS:\n            d = distance",
        "        for view in ('normal', 'deutan', 'tritan'):\n            d = distance",
        "protan pair in 1-8",
    )
    mutant(
        "CP005 deutan view dropped",
        "        for view in VIEWS:\n            d = distance",
        "        for view in ('normal', 'protan', 'tritan'):\n            d = distance",
        "deutan pair in 1-8",
    )
    mutant(
        "CP005 tritan view dropped",
        "        for view in VIEWS:\n            d = distance",
        "        for view in VIEWS[:3]:\n            d = distance",
        "tritan pair in 1-8",
    )
    # CP006
    mutant(
        "CP006 bound relaxed 5 percent",
        "MIN_CONTRAST = 5.0",
        "MIN_CONTRAST = 4.75",
        "first branch table",
    )
    mutant(
        "CP006 range starts at 2",
        "for i in range(1, 16):\n        if contrast",
        "for i in range(2, 16):\n        if contrast",
        "low contrast",
    )
    mutant(
        "CP006 range ends at 14",
        "for i in range(1, 16):\n        if contrast",
        "for i in range(1, 15):\n        if contrast",
        "entry 15 below 5:1",
    )
    mutant(
        "CP006 range covers entry 0",
        "for i in range(1, 16):\n        if contrast",
        "for i in range(0, 16):\n        if contrast",
        "accepted table",
    )
    mutant(
        "CP006 check disabled",
        "if contrast_on_black(panel[i]) < MIN_CONTRAST:",
        "if False:",
        "low contrast",
    )
    mutant(
        "CP006 relative luminance weight of blue dropped",
        "0.7152 * g + 0.0722 * b",
        "0.7152 * g",
        "accepted table",
    )
    # CP007
    mutant(
        "CP007 lower bound relaxed 5 percent",
        "LIFT_RANGE = (0.05, 0.09)",
        "LIFT_RANGE = (0.0475, 0.09)",
        "bold lift just under 0.05",
    )
    mutant(
        "CP007 upper bound relaxed 5 percent",
        "LIFT_RANGE = (0.05, 0.09)",
        "LIFT_RANGE = (0.05, 0.0945)",
        "bold lift just over 0.09",
    )
    mutant(
        "CP007 range starts at 2",
        "for i in range(1, 7):",
        "for i in range(2, 7):",
        "bold lift too small",
    )
    mutant(
        "CP007 range ends at 5",
        "for i in range(1, 7):",
        "for i in range(1, 6):",
        "bold lift large at entry 14",
    )
    mutant(
        "CP007 check disabled",
        "if not LIFT_RANGE[0] <= lift <= LIFT_RANGE[1]:",
        "if False:",
        "bold lift slightly small",
    )
    mutant(
        "CP007 lower bound disabled",
        "if not LIFT_RANGE[0] <= lift <= LIFT_RANGE[1]:",
        "if not lift <= LIFT_RANGE[1]:",
        "bold lift too small",
    )
    mutant(
        "CP007 upper bound disabled",
        "if not LIFT_RANGE[0] <= lift <= LIFT_RANGE[1]:",
        "if not LIFT_RANGE[0] <= lift:",
        "bold lift slightly large",
    )
    mutant(
        "CP007 sign of the lift ignored",
        "if not LIFT_RANGE[0] <= lift <= LIFT_RANGE[1]:",
        "if not LIFT_RANGE[0] <= abs(lift) <= LIFT_RANGE[1]:",
        "bold variant darker than its base",
    )
    mutant(
        "CP007 lift on luminance",
        'lift = simulate(panel[i + 8], "normal")[0] - simulate(panel[i], "normal")[0]',
        "lift = luminance(panel[i + 8]) - luminance(panel[i])",
        "accepted table",
    )
    # CP008
    mutant(
        "CP008 chroma bound relaxed 5 percent",
        "GRAY_CHROMA = 0.01",
        "GRAY_CHROMA = 0.0105",
        "entry 8 faint tint",
    )
    mutant(
        "CP008 chroma check disabled",
        "if math.hypot(a, b) > GRAY_CHROMA:",
        "if False:",
        "tinted entry 8",
    )
    mutant(
        "CP008 chroma ignores a",
        "if math.hypot(a, b) > GRAY_CHROMA:",
        "if abs(b) > GRAY_CHROMA:",
        "tinted entry 8",
    )
    mutant(
        "CP008 chroma ignores b",
        "if math.hypot(a, b) > GRAY_CHROMA:",
        "if abs(a) > GRAY_CHROMA:",
        "first branch table",
    )
    mutant(
        "CP008 chroma read from entry 7",
        'simulate(panel[8], "normal")',
        'simulate(panel[7], "normal")',
        "tinted entry 8",
    )
    mutant(
        "CP008 black not required",
        "table[0] != (0, 0, 0) or table[15] != (255, 255, 255)",
        "table[15] != (255, 255, 255)",
        "entry 0 not black",
    )
    mutant(
        "CP008 white not required",
        "table[0] != (0, 0, 0) or table[15] != (255, 255, 255)",
        "table[0] != (0, 0, 0)",
        "entry 15 not white",
    )
    mutant(
        "CP008 identity both required to fail",
        "table[0] != (0, 0, 0) or table[15] != (255, 255, 255)",
        "table[0] != (0, 0, 0) and table[15] != (255, 255, 255)",
        "entry 0 not black",
    )
    # CP009
    mutant(
        "CP009 bound relaxed 5 percent",
        "RED_GREEN_RATIO = 2.5",
        "RED_GREEN_RATIO = 2.375",
        "red nearly as luminous as green",
    )
    mutant(
        "CP009 check disabled",
        "if ratio < RED_GREEN_RATIO:",
        "if False:",
        "red nearly as luminous as green",
    )
    mutant(
        "CP009 ratio of lightness",
        "ratio = luminance(panel[2]) / max(luminance(panel[1]), 1e-9)",
        'ratio = simulate(panel[2], "normal")[0] / max(simulate(panel[1], "normal")[0], 1e-9)',
        "accepted table",
    )
    mutant(
        "CP009 red against yellow",
        "luminance(panel[2]) / max(luminance(panel[1]), 1e-9)",
        "luminance(panel[2]) / max(luminance(panel[3]), 1e-9)",
        "accepted table",
    )
    # CP010
    mutant(
        "CP010 bound relaxed 5 percent",
        "BOLD_WHITE_LIFT = 0.08",
        "BOLD_WHITE_LIFT = 0.076",
        "bold white just under 0.08",
    )
    mutant(
        "CP010 check disabled",
        "if white_lift < BOLD_WHITE_LIFT:",
        "if False:",
        "bold white barely lighter",
    )
    mutant(
        "CP010 sign of the lift ignored",
        "if white_lift < BOLD_WHITE_LIFT:",
        "if abs(white_lift) < BOLD_WHITE_LIFT:",
        "bold white darker than default text",
    )
    mutant(
        "CP010 lift on luminance",
        'white_lift = simulate(panel[15], "normal")[0] - simulate(panel[7], "normal")[0]',
        "white_lift = luminance(panel[15]) - luminance(panel[7])",
        "bold white lift is lightness, not luminance",
    )
    mutant(
        "CP010 lift against entry 8",
        'simulate(panel[7], "normal")[0]\n    if white_lift',
        'simulate(panel[8], "normal")[0]\n    if white_lift',
        "bold white barely lighter",
    )
    # the contrast bound sits within 0.2 percent of a grid color
    mutant(
        "CP006 bound relaxed 0.2 percent",
        "MIN_CONTRAST = 5.0",
        "MIN_CONTRAST = 4.99",
        "entry 1 just under 5:1",
    )
    # panel model
    mutant(
        "model zero-fill replication",
        "(v << 2) | (v >> 4) for v in stored_codes(color)",
        "v << 2 for v in stored_codes(color)",
        "accepted table",
    )
    mutant(
        "model keeps red low bit",
        "(color[0] >> 3) << 1, color[1] >> 2, (color[2] >> 3) << 1",
        "color[0] >> 2, color[1] >> 2, (color[2] >> 3) << 1",
        "pair short only with clear low bits",
    )
    mutant(
        "model keeps blue low bit",
        "(color[0] >> 3) << 1, color[1] >> 2, (color[2] >> 3) << 1",
        "(color[0] >> 3) << 1, color[1] >> 2, color[2] >> 2",
        "entry 8 near entry 5 under the clear blue bit",
    )
    mutant(
        "model keeps both low bits",
        "(color[0] >> 3) << 1, color[1] >> 2, (color[2] >> 3) << 1",
        "color[0] >> 2, color[1] >> 2, color[2] >> 2",
        "entry 8 near entry 5 under the clear blue bit",
    )
    mutant(
        "model rounded scaling dropped",
        "for finding in check_shown(tuple(shown_rounded(c) for c in table)):",
        "for finding in []:",
        "bold lift short only under rounded scaling",
    )
    mutant(
        "model rounded scaling is replication",
        "(v * 510 + 63) // 126 for v",
        "(v << 2) | (v >> 4) for v",
        "bold lift short only under rounded scaling",
    )
    mutant(
        "model rounded scaling truncates",
        "(v * 510 + 63) // 126 for v",
        "(v * 255) // 63 for v",
        "bold lift short only under rounded scaling",
    )
    mutant(
        "model replication is rounding",
        "return tuple((v << 2) | (v >> 4) for v in stored_codes(color))",
        "return shown_rounded(color)",
        "pair short only under replicated scaling",
    )


def rejecting_cases(stderr: str) -> set[str]:
    cases = set()
    for line in stderr.splitlines():
        if line.startswith(LABEL_PREFIX) and ": expected " in line:
            cases.add(line[len(LABEL_PREFIX) :].split(": expected ")[0])
    return cases


def run_selftest(path: Path) -> tuple[int, set[str], str]:
    done = subprocess.run(
        [sys.executable, "-I", str(path), "--selftest"],
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
    )
    return done.returncode, rejecting_cases(done.stderr), done.stderr


def run_mutant(source: str, entry: tuple[str, str, str, str]) -> tuple[str, int, set[str], str]:
    name, old, new, _ = entry
    if source.count(old) != 1:
        return name, -1, set(), f"old text occurs {source.count(old)} times"
    with tempfile.TemporaryDirectory() as work:
        copy = Path(work) / "check_console_palette.py"
        copy.write_text(source.replace(old, new, 1), encoding="utf-8")
        code, cases, err = run_selftest(copy)
    return name, code, cases, err


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--list", action="store_true", help="print each mutant with its rejecting cases"
    )
    args = parser.parse_args()
    build_mutants()
    source = GATE.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as work:
        good = Path(work) / "check_console_palette.py"
        good.write_text(source, encoding="utf-8")
        code, _, err = run_selftest(good)
    if code != 0:
        print(
            f"check-console-palette-mutants: the unmodified gate fails --selftest:\n{err}",
            file=sys.stderr,
        )
        return 1
    names = [m[0] for m in MUTANTS]
    if len(set(names)) != len(names):
        print("check-console-palette-mutants: duplicate mutant names", file=sys.stderr)
        return 1
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda e: run_mutant(source, e), MUTANTS))
    rejected = equivalent = 0
    failures: list[str] = []
    for entry, (name, code, cases, err) in zip(MUTANTS, results):
        expected = entry[3]
        if args.list:
            print(f"{name}: rc={code} cases={sorted(cases)}")
        if code < 0:
            failures.append(f"{name}: {err}")
        elif expected.startswith(EQUIVALENT):
            if code == 0:
                equivalent += 1
            else:
                failures.append(f"{name}: listed equivalent but rejected by {sorted(cases)}")
        elif code == 0:
            failures.append(f"{name}: survives --selftest (expected {expected!r})")
        elif expected not in cases:
            failures.append(f"{name}: rejected by {sorted(cases)}, not by {expected!r}")
        else:
            rejected += 1
    for failure in failures:
        print(f"check-console-palette-mutants: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(
        f"check-console-palette-mutants: {rejected} mutants rejected, "
        f"{equivalent} listed equivalent, {len(MUTANTS)} total"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
