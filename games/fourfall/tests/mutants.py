#!/usr/bin/env python3
# Mutation calibration of tests/logic_test.c. The real sources must pass
# first; then each mutant of fourfall.c (one textual substitution that
# must match exactly once) is compiled with the same flags and the
# harness must exit nonzero on it. A mutant that compiles and passes
# means the harness cannot see that fault, and the run fails.
#
# With --play it instead mutates the terminal front end, builds the host
# program and runs the named tests/play.py cases against it; the pty
# tests must reject each mutant.
#
# usage: mutants.py [--play] [fourfall-dir]

import os
import shutil
import subprocess
import sys
import tempfile

CC = os.environ.get("HOSTCC", "cc")
FLAGS = [
    "-std=c17",
    "-pedantic",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-g",
    "-O1",
    "-fsanitize=address,undefined",
    "-fno-sanitize-recover=undefined",
]

SKIP_ROW = """\t\t\tn++;
\t\t\tcontinue;"""
SKIP_ROW_MUT = """\t\t\tn++;
\t\t\tif (r > 0) {
\t\t\t\tr--;
\t\t\t\tmemcpy(b[w--], b[r], FF_COLS);
\t\t\t}
\t\t\tcontinue;"""

# (name, spec fault it models, old text, new text)
MUTANTS = [
    ("rotation-direction-swapped", "spec", "(dir > 0 ? 1 : 3)", "(dir > 0 ? 3 : 1)"),
    ("right-wall-last-column-blocked", "spec", "c >= FF_COLS ||", "c >= FF_COLS - 1 ||"),
    (
        "floor-check-removed",
        "spec",
        "r < 0 || r >= FF_ROWS || b[r][c])",
        "r < 0 || (r < FF_ROWS && b[r][c]))",
    ),
    ("no-kicks-T-S-Z-J-L", "spec", "\t\tnk = 4;", "\t\tnk = 1;"),
    ("no-kicks-I", "spec", "\t\tnk = 7;", "\t\tnk = 1;"),
    (
        "line-score-100-n-level",
        "spec",
        "(unsigned long)level * 100 * n * (n + 1) / 2",
        "(unsigned long)level * 100 * n",
    ),
    ("clear-skips-row-after-removed", "spec", SKIP_ROW, SKIP_ROW_MUT),
    ("level-step-every-9-lines", "spec", "start + lines / 10", "start + lines / 9"),
    (
        "O-spawns-one-column-left",
        "spec",
        "p.col = (FF_COLS - ff_box[type]) / 2;",
        "p.col = (FF_COLS - ff_box[type]) / 2 - (type == FF_O);",
    ),
    (
        "spawn-centered-for-width-10",
        "spec",
        "p.col = (FF_COLS - ff_box[type]) / 2;",
        "p.col = (10 - ff_box[type]) / 2;",
    ),
    ("well-width-10", "spec", "#define FF_COLS\t\t12", "#define FF_COLS\t\t10"),
    ("well-rows-22", "spec", "#define FF_ROWS\t\t24", "#define FF_ROWS\t\t22"),
    (
        "plain-rand-per-piece",
        "spec",
        "ff_bag(bag, g->draw);",
        "for (i = 0; i < 7; i++) bag[i] = 1 + g->draw(7);",
    ),
    ("lock-restarts-eleven", "extra", "#define FF_LOCK_RESETS\t10", "#define FF_LOCK_RESETS\t11"),
    ("lock-delay-400-ms", "extra", "#define FF_LOCK_MS\t500", "#define FF_LOCK_MS\t400"),
    ("gravity-level-2-is-710", "extra", "800, 700, 610", "800, 710, 610"),
    ("hard-drop-one-point-a-row", "extra", "2UL * d", "1UL * d"),
    ("soft-drop-two-points", "extra", "ff_add_score(g, 1);", "ff_add_score(g, 2);"),
    (
        "lock-out-needs-row-3",
        "extra",
        "\t\tif (r >= FF_HIDDEN)\n\t\t\tvisible = 1;",
        "\t\tif (r >= FF_HIDDEN + 1)\n\t\t\tvisible = 1;",
    ),
    ("score-not-saturated", "extra", "if (g->score > FF_SCORE_MAX)", "if (0)"),
    ("queue-keeps-one-piece", "extra", "while (g->qlen < 2)", "while (g->qlen < 1)"),
    ("O-rotation-keeps-old-rot", "extra", "\t\tp->rot = t.rot;\n\t\treturn 1;", "\t\treturn 1;"),
    (
        "O-cells-rotated",
        "extra",
        "\tif (type == FF_O)\n\t\trot = 0;\t/* the square maps onto itself: fixed enumeration */\n",
        "",
    ),
    (
        "plumb-starts-in-piece-cell",
        "extra",
        "for (r = low + 1; r < FF_ROWS && !b[r][col]; r++)",
        "for (r = low; r < FF_ROWS && !b[r][col]; r++)",
    ),
    (
        "plumb-ignores-stack",
        "extra",
        "for (r = low + 1; r < FF_ROWS && !b[r][col]; r++)",
        "for (r = low + 1; r < FF_ROWS && !(b[r][col] & 0); r++)",
    ),
    ("glyph-L-equals-J", "extra", '"()", "{}"', '"()", "()"'),
    ("glyph-Z-equals-S", "extra", r'"//", "\\\\"', '"//", "//"'),
    ("preview-boxes-overlap", "extra", "#define FF_NEXT_PITCH\t3", "#define FF_NEXT_PITCH\t1"),
    ("single-preview-slot", "extra", "#define FF_NEXT_SLOTS\t2", "#define FF_NEXT_SLOTS\t1"),
    ("flash-record-dropped", "extra", "g->flash_mask = full;", "g->flash_mask = 0;"),
    (
        "flash-board-not-saved",
        "extra",
        "memcpy(g->flash_board, g->board, sizeof g->board);",
        "(void)g->flash_board;",
    ),
    (
        "hidden-row-drawn",
        "extra",
        "if (r < FF_HIDDEN || r >= FF_ROWS",
        "if (r < FF_HIDDEN - 1 || r >= FF_ROWS",
    ),
]


# (name, old text, new text, play.py cases that must reject it)
PLAY_FAST = [
    "replay_one",
    "replay_two",
    "rotation_arrows",
    "enter_keys",
    "layout",
    "default_look",
    "color_mode",
    "plumb_line",
    "mono",
    "flash",
    "usage",
    "pause_and_keys",
    "repaint",
    "signals",
]
# Cases that show a front-end fault without running the whole list.
LOOK = ["layout", "default_look", "color_mode", "plumb_line", "mono"]
PLAY_MUTANTS = [
    ("right-wall-column-28", "ob_goto(r, WALL_R);", "ob_goto(r, WALL_R + 1);", PLAY_FAST),
    ("status-line-wording", 'ob_str(" level ");', 'ob_str(" lvl ");', PLAY_FAST),
    (
        "pause-keeps-board",
        "\t\t\tblank_board();\n\t\t\tdraw_next(0, 0);",
        "\t\t\tif (0)\n\t\t\t\tblank_board();\n\t\t\tdraw_next(0, 0);",
        PLAY_FAST,
    ),
    (
        "bare-esc-ignored",
        "if (!pending())\n\t\treturn 'p';",
        "if (!pending())\n\t\treturn K_NONE;",
        PLAY_FAST,
    ),
    ("f1-csi-code", "num == 11 ? 'p'", "num == 12 ? 'p'", PLAY_FAST),
    ("sigterm-exit-status", "_exit(128 + sig);", "(void)sig;\n\t_exit(0);", PLAY_FAST),
    (
        "resume-skips-repaint",
        "\t\t\tmemset(shadow, UNKNOWN, sizeof shadow);\n\t\t\tp_next[0] = p_next[1] = -1;",
        "\t\t\tp_next[0] = p_next[1] = -1;",
        PLAY_FAST,
    ),
    ("T-color-yellow", "40, 47, 44, 42, 45", "40, 47, 44, 43, 45", ["color_mode"]),
    (
        "cursor-parks-row-32",
        "\tdrawn = state;\n\tob_goto(31, 1);",
        "\tdrawn = state;\n\tob_goto(32, 1);",
        PLAY_FAST,
    ),
    (
        "ctrl-l-ignored",
        "case 014:\n\t\tframe(1);\n\t\treturn 0;",
        "case 014:\n\t\treturn 0;",
        PLAY_FAST,
    ),
    (
        "hard-drop-on-ctrl-key-only",
        "\tcase ' ':\n\tcase '\\r':\n\tcase '\\n':\n\t\tff_hard_drop(&game);",
        "\tcase ' ':\n\t\tff_hard_drop(&game);",
        PLAY_FAST,
    ),
    ("gravity-never-advances", "ff_advance(&game, dt);", "ff_advance(&game, 0);", ["timed"]),
    (
        "plumb-line-on-by-default",
        "static int plumb_on;",
        "static int plumb_on = 1;",
        ["default_look"],
    ),
    ("color-on-by-default", "static int color;", "static int color = 1;", ["default_look"]),
    (
        "solid-background-cells",
        'ob_str(falling ? "\\033[1;37m" : "\\033[90m");',
        'ob_str("\\033[47m");',
        ["default_look"],
    ),
    (
        "settled-cells-bright",
        '"\\033[1;37m" : "\\033[90m"',
        '"\\033[1;37m" : "\\033[1;37m"',
        ["default_look"],
    ),
    ("well-width-10-front-end", "#define FF_COLS\t\t12", "#define FF_COLS\t\t10", PLAY_FAST),
    ("well-rows-22-front-end", "#define FF_ROWS\t\t24", "#define FF_ROWS\t\t22", PLAY_FAST),
    (
        "single-preview",
        "for (slot = 0; slot < FF_NEXT_SLOTS; slot++)\n\t\tif (p_next[slot]",
        "for (slot = 0; slot < 1; slot++)\n\t\tif (p_next[slot]",
        ["layout"],
    ),
    (
        "wall-drawn-as-pipe",
        'ob_goto(r, WALL_L);\n\t\tob_put("!", 1);',
        'ob_goto(r, WALL_L);\n\t\tob_put("|", 1);',
        ["layout"],
    ),
    ("floor-drawn-as-dash", 'ob_put("=", 1);', 'ob_put("-", 1);', ["layout"]),
    (
        "title-left-aligned",
        "ob_goto(1, FF_WELL_COL + (WELL_W - 8) / 2);",
        "ob_goto(1, FF_WELL_COL);",
        ["layout"],
    ),
    ("flash-skipped", "\t\tif (game.flash_mask) {", "\t\tif (0) {", ["flash", "replay_one"]),
    ("flash-too-short", "#define FLASH_MS\t150", "#define FLASH_MS\t20", ["flash"]),
    ("plumb-option-ignored", "\t\t\t\tplumb_on = 1;", "\t\t\t\t(void)plumb_on;", ["plumb_line"]),
    ("color-option-ignored", "\t\t\t\tcolor = 1;", "\t\t\t\t(void)color;", ["color_mode"]),
    (
        "unknown-option-accepted",
        "\t\t\t\treturn usage();\n\t\t}",
        "\t\t\t\tcontinue;\n\t\t}",
        ["usage"],
    ),
]


def play_mutants(src):
    text = open(os.path.join(src, "fourfall.c")).read()
    gametty = os.path.join(src, "..", "gametty.h")
    play = os.path.join(src, "tests", "play.py")
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        os.mkdir(os.path.join(work, "x"))
        shutil.copy(gametty, os.path.join(work, "gametty.h"))
        exe = os.path.join(work, "x", "fourfall-host")
        for name, old, new, cases in PLAY_MUTANTS:
            if text.count(old) != 1:
                print("play mutant %-28s ERROR: anchor occurs %d times" % (name, text.count(old)))
                failures += 1
                continue
            with open(os.path.join(work, "x", "fourfall.c"), "w") as f:
                f.write(text.replace(old, new))
            r = subprocess.run(
                [
                    CC,
                    "-std=c17",
                    "-D_DEFAULT_SOURCE",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DHOSTBUILD",
                    "-o",
                    exe,
                    os.path.join(work, "x", "fourfall.c"),
                ],
                capture_output=True,
                text=True,
            )
            if r.returncode != 0:
                print("play mutant %-28s ERROR: compile\n%s" % (name, r.stderr))
                failures += 1
                continue
            r = subprocess.run(
                [sys.executable, play, exe] + cases, capture_output=True, text=True, timeout=300
            )
            if r.returncode == 0:
                print("play mutant %-28s SURVIVED" % name)
                failures += 1
                continue
            fail = next(
                (ln for ln in r.stdout.splitlines() if "FAIL" in ln), "exit %d" % r.returncode
            )
            print("play mutant %-28s rejected: %s" % (name, fail.split("FAIL in ", 1)[-1][:90]))
    print("play mutants: %d tried, %d failures" % (len(PLAY_MUTANTS), failures))
    return 1 if failures else 0


def build_run(srcdir, work, vectors):
    exe = os.path.join(work, "logic")
    cmd = [CC] + FLAGS + ["-o", exe, os.path.join(work, "tests", "logic_test.c")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return None, "compile error:\n" + r.stderr
    try:
        r = subprocess.run([exe, vectors], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return 124, "timeout"
    return r.returncode, r.stdout + r.stderr


def main():
    args = sys.argv[1:]
    play = "--play" in args
    args = [a for a in args if a != "--play"]
    src = os.path.abspath(args[0] if args else ".")
    if play:
        return play_mutants(src)
    vectors = os.path.join(src, "tests", "vectors.txt")
    text = open(os.path.join(src, "fourfall.c")).read()
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        os.mkdir(os.path.join(work, "tests"))
        shutil.copy(
            os.path.join(src, "tests", "logic_test.c"), os.path.join(work, "tests", "logic_test.c")
        )
        shutil.copy(os.path.join(src, "fourfall.c"), os.path.join(work, "fourfall.c"))
        rc, out = build_run(src, work, vectors)
        if rc != 0:
            print("baseline: the real sources do not pass:\n" + out)
            return 1
        print("baseline: real sources pass")
        spec = 0
        for name, kind, old, new in MUTANTS:
            if text.count(old) != 1:
                print("mutant %-32s ERROR: anchor occurs %d times" % (name, text.count(old)))
                failures += 1
                continue
            with open(os.path.join(work, "fourfall.c"), "w") as f:
                f.write(text.replace(old, new))
            rc, out = build_run(src, work, vectors)
            if rc is None:
                print("mutant %-32s ERROR: %s" % (name, out.strip()))
                failures += 1
                continue
            if rc == 0:
                print("mutant %-32s SURVIVED (harness cannot see it)" % name)
                failures += 1
                continue
            hits = [
                ln.split(":")[0].replace("FAIL vector ", "")
                for ln in out.splitlines()
                if ln.startswith("FAIL vector")
            ]
            first = next(
                (
                    ln
                    for ln in out.splitlines()
                    if ("logic_test.c:" in ln and ln.startswith("FAIL")) or "Sanitizer" in ln
                ),
                "exit %d" % rc,
            )
            how = (
                "vectors " + " ".join(hits[:6]) + (" ..." if len(hits) > 6 else "")
                if hits
                else "check: " + first.split(": ", 1)[-1][:70]
            )
            print("mutant %-32s rejected [%s] %s" % (name, kind, how))
            if kind == "spec":
                spec += 1
    n = len(MUTANTS)
    print(
        "mutants: %d tried, %d rejected, %d spec-listed rejected, %d failures"
        % (n, n - failures, spec, failures)
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
