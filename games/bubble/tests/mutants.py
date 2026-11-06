#!/usr/bin/env python3
# Mutation calibration of the bubble gate (see games/mutlib.py).
#
# usage: mutants.py [--play] [bubble-dir]
#
# Without --play every mutant of the rules half of bubble.c must fail
# tests/hex_test.c; with --play every mutant of the whole program must
# fail the named tests/play.py cases.

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
import mutlib  # noqa: E402  (path set above)

# (name, old text, new text); each old text occurs once in bubble.c
LOGIC = [
    ("shifted-row-neighbors-swapped",
     "c0 = shifted(r) ? c : c - 1;", "c0 = shifted(r) ? c - 1 : c;"),
    ("one-cell-per-adjacent-row",
     "c1 = c0 + 1;", "c1 = c0;"),
    ("neighbors-ignore-hex-mode",
     "\t\tif (!hexmode) {\n\t\t\tout[n++] = rr * COLS + c;",
     "\t\tif (1) {\n\t\t\tout[n++] = rr * COLS + c;"),
    ("shifted-row-keeps-all-cells",
     "return shifted(r) ? COLS - 1 : COLS;", "return shifted(r) ? COLS : COLS;"),
    ("shift-on-even-rows",
     "return hexmode && ((r + stagger) & 1);", "return hexmode && !((r + stagger) & 1);"),
    ("stagger-ignored",
     "return hexmode && ((r + stagger) & 1);", "return hexmode && (r & 1);"),
    ("descent-after-seven-shots",
     "#define DESCENT\t\t8", "#define DESCENT\t\t7"),
    ("descent-after-nine-shots",
     "#define DESCENT\t\t8", "#define DESCENT\t\t9"),
    ("descent-keeps-stagger",
     "\tstagger ^= 1;\n", "\n"),
    ("pop-keeps-miss-count",
     "\tif (popped)\n\t\tmisses = 0;", "\tif (popped)\n\t\tmisses += 0;"),
    ("miss-count-never-resets",
     "\t\tmisses = 0;\n\t\tdescend();", "\t\tdescend();"),
    ("pop-needs-four",
     "\tif (n >= 3) {", "\tif (n >= 4) {"),
    ("orphan-bonus-ten",
     "score += dropped * 20;", "score += dropped * 10;"),
    ("pop-bonus-twenty",
     "score += n * 10;", "score += n * 20;"),
    ("anchor-only-column-zero",
     "\tfor (c = 0; c < COLS; c++)\n\t\tif (grid[c] != 0 && !mark2[c]) {",
     "\tfor (c = 0; c < 1; c++)\n\t\tif (grid[c] != 0 && !mark2[c]) {"),
    ("shifted-row-position-ignored",
     "c = (xf - (shifted(r) ? SUB / 2 : 0)) / SUB;", "c = xf / SUB;"),
    ("shot-lag-rule-strict",
     "ax * rise <= ay * run", "ax * rise < ay * run"),
    ("floor-test-one-row-up",
     "grid[(ROWS - 1) * COLS + c]", "grid[(ROWS - 2) * COLS + c]"),
    ("descent-row-color-off-by-one",
     "return testmode ? c % NCOLORS + 1 : rand() % NCOLORS + 1;",
     "return testmode ? c % NCOLORS : rand() % NCOLORS + 1;"),
    ("descent-row-full-width",
     "grid[c] = c < rowlen(0) ? rowcolor(c) : 0;", "grid[c] = rowcolor(c);"),
]

# (name, old text, new text, play.py cases that must fail)
PLAY = [
    ("hex-flag-ignored",
     "\t\thexmode = 1;\n", "\t\thexmode = 0;\n",
     ["hex_stagger", "hex_pop", "hex_orphan", "hex_descent", "hex_floor"]),
    ("shifted-row-not-indented",
     "\t\tif (shifted(r))\n\t\t\tp += sprintf(buf + p, \" \");\n\t\tfor",
     "\t\tfor",
     ["hex_stagger", "hex_descent"]),
    ("descent-counter-hidden",
     "p += sprintf(buf + p, \"  drop in %d\", DESCENT - misses);", "(void)misses;",
     ["hex_stagger", "hex_descent"]),
    ("hex-title-missing",
     "hexmode ? \" hex\" : \"\"", "\"\"",
     ["hex_stagger"]),
    ("usage-exit-zero",
     "\t\tput_str(2, \"usage: bubble [-x] [seed]\\n\");\n\t\treturn 2;",
     "\t\tput_str(2, \"usage: bubble [-x] [seed]\\n\");\n\t\treturn 0;",
     ["usage"]),
    ("script-ignored",
     "\tif (env && script_ok(env))\n\t\tscript = env;",
     "\tif (env && script_ok(env))\n\t\tscript = 0;",
     ["hex_orphan", "hex_descent", "hex_floor"]),
    ("game-over-not-announced",
     "put_str(1, \"\\r\\n\\033[1;31mgame over\\033[0m\\r\\n\");", "",
     ["hex_floor"]),
    ("cleared-not-announced",
     "put_str(1, \"\\r\\n\\033[1;32mcleared!\\033[0m\\r\\n\");", "",
     ["hex_pop", "hex_orphan"]),
    ("board-border-wider",
     "\"  \\033[36m'-----------------'\\033[0m\\r\\n\"",
     "\"  \\033[36m'-------------------------------------------------'\\033[0m\\r\\n\"",
     ["frame_fits"]),
]


def main():
    args = [a for a in sys.argv[1:] if a != "--play"]
    src = os.path.abspath(args[0] if args else os.path.join(HERE, ".."))
    top = os.path.join(src, "..", "..")
    if "--play" in sys.argv[1:]:
        return mutlib.play(src, "bubble.c", "bubble-host", PLAY)
    return mutlib.logic(src, "bubble.c", "hex_test.c",
                        os.path.join(src, "tests", "vectors.txt"), LOGIC, top)


if __name__ == "__main__":
    sys.exit(main())
