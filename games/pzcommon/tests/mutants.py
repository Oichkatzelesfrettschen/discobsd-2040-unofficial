#!/usr/bin/env python3
# Mutation calibration of the puzzlebox host gate.
#
# usage: mutants.py [--play] [pzcommon-dir]
#
# Without --play, every mutant of an imported puzzle source (a temporary copy;
# the tracked file is never touched) must fail that puzzle's rule test
# (tests/logic_<name>.c), which proves the test can see a broken generator,
# solver, move parser or scoring rule. With --play, every mutant of the text
# front end or of a puzzle wrapper must fail the named tests/play.py cases.
#
# A mutant is one textual substitution that must match exactly once.

import os
import shutil
import subprocess
import sys
import tempfile

CC = os.environ.get("HOSTCC", "cc")
HERE = os.path.dirname(os.path.abspath(__file__))
STRICT = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-g", "-O1"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]

# (puzzle, name, old text, new text) for tatham/<puzzle>.c
LOGIC = [
    (
        "flood",
        "completed-ignores-the-last-cell",
        "for (i = 1; i < wh; i++)\n        if (grid[i] != grid[0])",
        "for (i = 1; i < wh - 1; i++)\n        if (grid[i] != grid[0])",
    ),
    (
        "flood",
        "fill-skips-the-east-neighbor",
        "                if (grid[pos1] == oldcolour) {",
        "                if (grid[pos1] == oldcolour && dir != 0) {",
    ),
    (
        "flood",
        "win-needs-a-spare-move",
        "if (state->complete && state->moves <= state->movelimit) {\n        return +1;",
        "if (state->complete && state->moves < state->movelimit) {\n        return +1;",
    ),
    ("flood", "two-colors-allowed", "params->colours < 3 ||", "params->colours < 2 ||"),
    (
        "flood",
        "same-color-fill-accepted",
        "        c != state->grid[FILLY * state->w + FILLX] &&\n",
        "",
    ),
    (
        "unruly",
        "three-in-a-row-needs-two",
        "if (state->grid[i1] == check && state->grid[i2] == check\n"
        "                && state->grid[i3] == check) {",
        "if (state->grid[i1] == check && state->grid[i2] == check) {",
    ),
    (
        "unruly",
        "column-count-off-by-one",
        "if (scratch->ones_cols[i] > h) {",
        "if (scratch->ones_cols[i] >= h) {",
    ),
    (
        "unruly",
        "clue-cells-writable",
        "        if (state->common->immutable[i]) {\n"
        "            free_game(ret);\n            return NULL;\n        }\n",
        "",
    ),
    (
        "unruly",
        "solve-move-not-marked-cheated",
        "ret->completed = ret->cheated = true;",
        "ret->completed = true;",
    ),
    (
        "lightup",
        "dark-cells-allowed",
        "if (GRID(state,lights,x,y) == 0)\n                return false;",
        "if (GRID(state,lights,x,y) == 0)\n                continue;",
    ),
    ("lightup", "lamp-on-black-accepted", "if (flags & F_BLACK) goto badmove;", ""),
    (
        "lightup",
        "every-move-completes",
        "if (grid_correct(ret)) ret->completed = true;",
        "ret->completed = true;",
    ),
    (
        "lightup",
        "one-wide-grid-accepted",
        "params->w < 2 || params->h < 2",
        "params->w < 1 || params->h < 1",
    ),
    (
        "samegame",
        "removal-scored-linearly",
        "return (sdiff > 0) ? sdiff * sdiff : 0;",
        "return (sdiff > 0) ? sdiff : 0;",
    ),
    (
        "samegame",
        "cleared-board-not-complete",
        "ret->complete = complete;",
        "ret->complete = complete && !complete;",
    ),
    ("samegame", "tiles-do-not-fall", "SWAPTILE(ret,x,y,x,y-1);", "ndone += 0;"),
    ("samegame", "move-index-unchecked", "if (i < 0 || i >= ret->n) {", "if (i < 0) {"),
]

# (puzzle, file relative to games/, name, old, new, cases, extra) for the front end
# and the wrappers; puzzle "" means the change affects every puzzle and is
# tried on flood.
PLAY = [
    (
        "",
        "pzcommon/pzfront.c",
        "wall-one-column-right",
        "gt_goto(CELL_ROW + r, CELL_COL + 2 * cv.w);",
        "gt_goto(CELL_ROW + r, CELL_COL + 2 * cv.w + 1);",
        ["layout"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "cursor-not-highlighted",
        "cv.cell[cv.cy][cv.cx].attr |= PZ_REV;",
        "cv.cell[cv.cy][cv.cx].attr |= 0;",
        ["layout"],
    ),
    ("", "pzcommon/pzfront.c", "undo-ignored", "\t\tif (cur > 0) {", "\t\tif (0) {", ["undo_redo"]),
    (
        "",
        "pzcommon/pzfront.c",
        "redo-ignored",
        "\t\tif (cur + 1 < nhist) {",
        "\t\tif (0) {",
        ["undo_redo"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "solved-not-announced",
        'strcpy(cv.message, "SOLVED  n: new game");',
        'strcpy(cv.message, "");',
        ["solution_moves", "flood_fixture"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "new-game-repeats-the-seed",
        'sprintf(seed, "%s.%d", base_seed, game_no++);',
        'sprintf(seed, "%s.%d", base_seed, game_no++ * 0);',
        ["new_game"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "seed-copied-into-a-fixed-buffer",
        "\t\tseed = snewn(strlen(base_seed) + 13, char);\n"
        '\t\tsprintf(seed, "%s.%d", base_seed, game_no++);\n'
        "\t\trs = random_new(seed, (int)strlen(seed));\n"
        "\t\td = op->new_desc(params, rs, &aux);\n"
        "\t\trandom_free(rs);\n"
        "\t\tsfree(seed);\n",
        "\t\tchar fixed[96];\n"
        "\t\tseed = fixed;\n"
        '\t\tsprintf(seed, "%s.%d", base_seed, game_no++);\n'
        "\t\trs = random_new(seed, (int)strlen(seed));\n"
        "\t\td = op->new_desc(params, rs, &aux);\n"
        "\t\trandom_free(rs);\n",
        ["long_seed"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "bad-id-exits-zero",
        "\tif (err) {\n\t\tcomplain(err);\n\t\treturn 2;\n\t}",
        "\tif (err) {\n\t\tcomplain(err);\n\t\treturn 0;\n\t}",
        ["bad_args", "too_big"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "size-limit-ignored",
        "if (!err && (w > PZ_MAXW || h > PZ_MAXH))",
        "if (0)",
        ["too_big"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "monochrome-flag-ignored",
        '\t\tif (strcmp(argv[ai], "-m") == 0) {\n\t\t\tmono = 1;',
        '\t\tif (strcmp(argv[ai], "-m") == 0) {\n\t\t\tmono = 0;',
        ["monochrome"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "title-not-drawn",
        "c = (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;",
        "c = *p;",
        ["layout"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "test-hook-always-on",
        "\t\tif (testmode) {\n\t\t\tcmd_on = 1;",
        "\t\tif (!testmode) {\n\t\t\tcmd_on = 1;",
        ["solution_moves"],
    ),
    (
        "",
        "pzcommon/pzfront.c",
        "solve-key-ignored",
        "\t\tif (!op->solve)\n\t\t\treturn 0;\n\t\tm = op->solve(orig, hist[cur], aux);",
        "\t\treturn 0;\n\t\tm = op->solve(orig, hist[cur], aux);",
        ["solve_key"],
    ),
    (
        "unruly",
        "pzcommon/pzfront.c",
        "solve-from-the-oldest-kept-state",
        "\t\tm = op->solve(orig, hist[cur], aux);",
        "\t\tm = op->solve(hist[0], hist[cur], aux);",
        ["solve_after_history_eviction"],
    ),
    (
        "lightup",
        "pzcommon/pzfront.c",
        "history-ignores-the-byte-budget",
        "\thist_max = (int)(PZ_HIST_BYTES / op->state_bytes(s)) - 2;",
        "\thist_max = PZ_HIST;",
        ["history_budget"],
    ),
    (
        "lightup",
        "pzcommon/pzfront.c",
        "history-budget-omits-the-dealt-copy",
        "\thist_max = (int)(PZ_HIST_BYTES / op->state_bytes(s)) - 2;",
        "\thist_max = (int)(PZ_HIST_BYTES / op->state_bytes(s));",
        ["history_budget"],
    ),
    (
        "lightup",
        "lightup/lightup.c",
        "lightup-state-cost-omits-the-flags",
        "pz_heap(n * sizeof(int)) +\n\t    pz_heap(n * sizeof(unsigned int));",
        "pz_heap(n * sizeof(int));",
        ["history_budget"],
    ),
    (
        "flood",
        "flood/flood.c",
        "flood-digit-off-by-one",
        "\t\tc = key - '1';",
        "\t\tc = key - '0';",
        ["flood_fixture"],
    ),
    (
        "flood",
        "flood/flood.c",
        "flood-description-colors-unchecked",
        "\t\tif (desc[i] - '0' >= MAXPLAY)",
        "\t\tif (0)",
        ["flood_description_colors"],
    ),
    (
        "flood",
        "flood/flood.c",
        "flood-region-not-bold",
        "cell->attr = reach[y * s->w + x] ? PZ_BOLD : 0;",
        "cell->attr = 0;",
        ["flood_fixture"],
    ),
    (
        "flood",
        "flood/flood.c",
        "flood-script-drops-the-move-letter",
        "if (sol[-1] == 'S' || sol[-1] == ',')",
        "if (sol[-1] == 'S')",
        ["solution_moves"],
    ),
    (
        "flood",
        "flood/flood.c",
        "flood-status-swapped",
        'sprintf(cv->status, "moves %d/%d", s->moves, s->movelimit);',
        'sprintf(cv->status, "moves %d/%d", s->movelimit, s->moves);',
        ["flood_fixture"],
    ),
    (
        "samegame",
        "samegame/samegame.c",
        "samegame-selection-glyph",
        "c->c[0] = '(';",
        "c->c[0] = '[';",
        ["samegame_fixture"],
    ),
    (
        "samegame",
        "samegame/samegame.c",
        "samegame-clear-key-unmapped",
        "int c = key == 'c' ? CURSOR_SELECT2 : pz_button(key);",
        "int c = pz_button(key);",
        ["samegame_fixture"],
    ),
    (
        "samegame",
        "samegame/samegame.c",
        "samegame-score-not-shown",
        'sprintf(cv->status, "score %d", s->score);',
        'sprintf(cv->status, "score %d", 0);',
        ["samegame_fixture"],
    ),
    (
        "unruly",
        "unruly/unruly.c",
        "unruly-digit-keys-unmapped",
        "\tif (key == '0' || key == '1')\n\t\tc = key;\n\telse if",
        "\tif (0)\n\t\tc = key;\n\telse if",
        ["unruly_keys"],
    ),
    (
        "unruly",
        "unruly/unruly.c",
        "unruly-clues-not-bold",
        "c->attr = s->common->immutable[i] ? PZ_BOLD : 0;",
        "c->attr = 0;",
        ["unruly_keys"],
    ),
    (
        "unruly",
        "unruly/unruly.c",
        "unruly-script-wrong-cell",
        'o += sprintf(o, "P%c,%d,%d;", sol[1 + i], i % s->w2, i / s->w2);',
        'o += sprintf(o, "P%c,%d,%d;", sol[1 + i], i / s->w2, i % s->w2);',
        ["solution_moves"],
    ),
    (
        "lightup",
        "lightup/lightup.c",
        "lightup-lamp-glyph",
        "c->c[0] = '<';",
        "c->c[0] = '[';",
        ["lightup_keys"],
    ),
    (
        "lightup",
        "lightup/lightup.c",
        "lightup-wall-drawn-blank",
        "\t\t\t\tc->c[0] = c->c[1] = '#';",
        "\t\t\t\tc->c[0] = c->c[1] = ' ';",
        ["lightup_walls_without_color"],
    ),
    (
        "lightup",
        "lightup/lightup.c",
        "lightup-lit-cells-plain",
        "\t\t\tif (n > 0) {\n\t\t\t\tc->bg = PZ_YELLOW;",
        "\t\t\tif (n > 12345) {\n\t\t\t\tc->bg = PZ_YELLOW;",
        ["lightup_keys"],
    ),
    (
        "lightup",
        "lightup/lightup.c",
        "lightup-script-keeps-the-solve-letter",
        "return dupstr(sol[0] == 'S' && sol[1] == ';' ? sol + 2 : sol);",
        "return dupstr(sol);",
        ["solution_moves", "undo_redo"],
    ),
]


def sanitizers():
    probe = os.path.join(HERE, "..", "..", "..", "tools", "compiler-probe.sh")
    r = subprocess.run(["sh", probe, "native", CC] + SAN_FLAGS, capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip() == "yes":
        return SAN_FLAGS
    print("SKIP sanitizers: runtime unavailable; mutants run unsanitized")
    return []


def copy_tree(src_games, work):
    """A scratch copy of the parts of games/ the puzzles are built from."""
    dst = os.path.join(work, "games")
    os.makedirs(dst)
    for h in ("gametty.h", "gameterm.h"):
        shutil.copy(os.path.join(src_games, h), dst)
    shutil.copytree(
        os.path.join(src_games, "pzcommon"),
        os.path.join(dst, "pzcommon"),
        ignore=shutil.ignore_patterns("build", "*.o"),
    )
    for p in ("flood", "samegame", "unruly", "lightup"):
        os.makedirs(os.path.join(dst, p))
        shutil.copy(os.path.join(src_games, p, p + ".c"), os.path.join(dst, p))
    return dst


def replace_once(path, old, new):
    with open(path) as f:
        text = f.read()
    if text.count(old) != 1:
        return text.count(old)
    with open(path, "w") as f:
        f.write(text.replace(old, new))
    return 1


def build_support(games, san, out):
    os.makedirs(out, exist_ok=True)
    for src, obj, extra in (
        ("support.c", "support.o", ["-ffunction-sections"]),
        ("tests/pztest_fatal.c", "fatal.o", []),
    ):
        r = subprocess.run(
            [CC]
            + STRICT
            + san
            + extra
            + ["-c", "-o", os.path.join(out, obj), os.path.join(games, "pzcommon", src)],
            capture_output=True,
            text=True,
        )
        if r.returncode:
            raise SystemExit("cannot build %s:\n%s" % (src, r.stderr))


def run_logic(games, puzzle, san, out):
    harness = os.path.join(games, "pzcommon", "tests", "logic_%s.c" % puzzle)
    obj = os.path.join(out, "logic_%s.o" % puzzle)
    exe = os.path.join(out, "logic_" + puzzle)
    r = subprocess.run(
        [CC] + STRICT + san + ["-fwhole-program", "-c", "-o", obj, harness],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        return None, "compile error:\n" + r.stderr
    r = subprocess.run(
        [CC]
        + san
        + [
            "-Wl,--gc-sections",
            "-o",
            exe,
            obj,
            os.path.join(out, "support.o"),
            os.path.join(out, "fatal.o"),
            "-lm",
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        return None, "link error:\n" + r.stderr
    env = dict(os.environ)
    if puzzle == "unruly":
        env["ASAN_OPTIONS"] = "detect_leaks=0"
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=30, env=env)
    except subprocess.TimeoutExpired:
        return 124, "timeout"
    return r.returncode, r.stdout + r.stderr


def logic_mutants(src_games):
    san = sanitizers()
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        games = copy_tree(src_games, work)
        out = os.path.join(work, "out")
        build_support(games, san, out)
        for puzzle in ("flood", "samegame", "unruly", "lightup"):
            rc, text = run_logic(games, puzzle, san, out)
            if rc != 0:
                print("baseline %s: the real sources do not pass:\n%s" % (puzzle, text))
                return 1
        print("baseline: the four rule tests pass on the real sources")
        for puzzle, name, old, new in LOGIC:
            target = os.path.join(games, "pzcommon", "tatham", puzzle + ".c")
            shutil.copy(os.path.join(src_games, "pzcommon", "tatham", puzzle + ".c"), target)
            n = replace_once(target, old, new)
            if n != 1:
                print("mutant %-36s ERROR: anchor occurs %d times" % (puzzle + ":" + name, n))
                failures += 1
                continue
            rc, text = run_logic(games, puzzle, san, out)
            if rc is None:
                print("mutant %-36s ERROR: %s" % (puzzle + ":" + name, text.strip()[:200]))
                failures += 1
            elif rc == 0:
                print("mutant %-36s SURVIVED (the test cannot see it)" % (puzzle + ":" + name))
                failures += 1
            else:
                first = next(
                    (
                        ln
                        for ln in text.splitlines()
                        if ln.startswith("FAIL") or "ERROR" in ln or "Assertion" in ln
                    ),
                    "exit %d" % rc,
                )
                print("mutant %-36s rejected: %s" % (puzzle + ":" + name, first[:80]))
    print("logic mutants: %d tried, %d failures" % (len(LOGIC), failures))
    return 1 if failures else 0


def build_host(games, puzzle, exe):
    flags = [
        "-std=c17",
        "-pedantic-errors",
        "-D_DEFAULT_SOURCE",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O1",
    ]
    pz = os.path.join(games, "pzcommon")
    objs = os.path.join(os.path.dirname(exe), "obj-" + puzzle)
    os.makedirs(objs, exist_ok=True)
    steps = (
        (["-fwhole-program"], os.path.join(games, puzzle, puzzle + ".c"), "wrap.o"),
        (["-DHOSTBUILD", "-ffunction-sections"], os.path.join(pz, "pzfront.c"), "front.o"),
        (["-ffunction-sections"], os.path.join(pz, "support.c"), "support.o"),
    )
    for extra, src, obj in steps:
        r = subprocess.run(
            [CC] + flags + extra + ["-c", "-o", os.path.join(objs, obj), src],
            capture_output=True,
            text=True,
        )
        if r.returncode:
            return r.stderr
    r = subprocess.run(
        [CC, "-Wl,--gc-sections", "-o", exe]
        + [os.path.join(objs, o) for o in ("wrap.o", "front.o", "support.o")]
        + ["-lm"],
        capture_output=True,
        text=True,
    )
    return r.stderr if r.returncode else None


def play_mutants(src_games):
    failures = 0
    play = os.path.join(src_games, "pzcommon", "tests", "play.py")
    with tempfile.TemporaryDirectory() as work:
        games = copy_tree(src_games, work)
        for puzzle, rel, name, old, new, cases in PLAY:
            puzzle = puzzle or "flood"
            for p in ("flood", "samegame", "unruly", "lightup"):
                shutil.copy(os.path.join(src_games, p, p + ".c"), os.path.join(games, p, p + ".c"))
            shutil.copy(
                os.path.join(src_games, "pzcommon", "pzfront.c"),
                os.path.join(games, "pzcommon", "pzfront.c"),
            )
            n = replace_once(os.path.join(games, rel), old, new)
            if n != 1:
                print("play mutant %-40s ERROR: anchor occurs %d times" % (name, n))
                failures += 1
                continue
            os.makedirs(os.path.join(games, puzzle), exist_ok=True)
            err = build_host(games, puzzle, os.path.join(games, puzzle, puzzle + "-host"))
            if err:
                print("play mutant %-40s ERROR: compile\n%s" % (name, err))
                failures += 1
                continue
            env = dict(os.environ, PZ_ONLY=puzzle)
            try:
                r = subprocess.run(
                    [sys.executable, play, games] + cases,
                    capture_output=True,
                    text=True,
                    timeout=180,
                    env=env,
                )
            except subprocess.TimeoutExpired:
                print("play mutant %-40s rejected: the pty test timed out" % name)
                continue
            if r.returncode == 0:
                print("play mutant %-40s SURVIVED" % name)
                failures += 1
                continue
            last = (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1]
            print("play mutant %-40s rejected: %s" % (name, last[:80]))
    print("play mutants: %d tried, %d failures" % (len(PLAY), failures))
    return 1 if failures else 0


def main():
    args = [a for a in sys.argv[1:] if a != "--play"]
    root = os.path.abspath(args[0] if args else os.path.join(HERE, ".."))
    games = os.path.dirname(root)
    if "--play" in sys.argv[1:]:
        return play_mutants(games)
    return logic_mutants(games)


if __name__ == "__main__":
    sys.exit(main())
