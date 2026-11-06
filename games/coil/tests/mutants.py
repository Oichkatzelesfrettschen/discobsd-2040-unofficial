#!/usr/bin/env python3
# Mutation calibration of the coil gate (see games/mutlib.py).
#
# usage: mutants.py [--play] [coil-dir]
#
# Without --play every mutant of the rules half of coil.c must fail
# tests/logic_test.c; with --play every mutant of the whole program must
# fail the named tests/play.py cases.

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
import mutlib  # noqa: E402  (path set above)

# (name, old text, new text); each old text occurs once in coil.c
LOGIC = [
    ("tie-goes-ahead",
     "return cl_dist2(p + 1, px, py) < cl_dist2(p - 1, px, py) ? k : k + 1;",
     "return cl_dist2(p + 1, px, py) <= cl_dist2(p - 1, px, py) ? k : k + 1;"),
    ("slot-picks-the-farther-side",
     "return cl_dist2(p + 1, px, py) < cl_dist2(p - 1, px, py) ? k : k + 1;",
     "return cl_dist2(p + 1, px, py) < cl_dist2(p - 1, px, py) ? k + 1 : k;"),
    ("slot-ignores-the-path-end",
     "\tif (p + 1 > CL_PATHLEN - 1)\n\t\treturn k + 1;\n", ""),
    ("slot-ignores-the-path-start",
     "\tif (p - 1 < 0)\n\t\treturn k;\n", ""),
    ("insert-behind-always-shoves",
     "} else if (g->pos[k] > 0 && (k + 1 == g->n || g->pos[k + 1] < g->pos[k] - 1)) {",
     "} else if (0) {"),
    ("insert-ahead-takes-own-cell",
     "\t\ts = g->pos[k] + 1;\n\t} else if", "\t\ts = g->pos[k];\n\t} else if"),
    ("insert-never-pushes",
     "for (j = m - 1; j >= 0 && g->pos[j] <= prev; j--)", "for (j = m - 1; j >= 0 && 0; j--)"),
    ("insert-pushes-through-gaps",
     "for (j = m - 1; j >= 0 && g->pos[j] <= prev; j--)", "for (j = m - 1; j >= 0; j--)"),
    ("run-needs-four",
     "\t\tif (b - a + 1 >= 3) {\n\t\t\tg->combo++;", "\t\tif (b - a + 1 >= 4) {\n\t\t\tg->combo++;"),
    ("run-spans-gaps",
     "while (lo > 0 && g->pos[lo - 1] - g->pos[lo] == 1 && g->col[lo - 1] == g->col[k])",
     "while (lo > 0 && g->col[lo - 1] == g->col[k])"),
    ("run-ignores-color-ahead",
     "while (lo > 0 && g->pos[lo - 1] - g->pos[lo] == 1 && g->col[lo - 1] == g->col[k])",
     "while (lo > 0 && g->pos[lo - 1] - g->pos[lo] == 1)"),
    ("score-five-a-ball",
     "10UL * (unsigned long)cnt", "5UL * (unsigned long)cnt"),
    ("combo-not-multiplied",
     "(unsigned long)cnt * (unsigned long)g->combo", "(unsigned long)cnt"),
    ("sweep-forgets-the-combo-step",
     "\t\t\tg->combo++;\n\t\t\tcl_remove(g, a, b);\n\t\t\tcleared++;",
     "\t\t\tg->combo = 1;\n\t\t\tcl_remove(g, a, b);\n\t\t\tcleared++;"),
    ("front-part-rolls-over-a-gap",
     "\t\tmoves[0] = 0;", "\t\tmoves[0] = 1;"),
    ("only-first-gap-rolls",
     "moves[k] = before[k] > 1 ? 1 : moves[k - 1];", "moves[k] = before[k] > 1 && !moves[k - 1];"),
    ("rejoin-skips-the-sweep",
     "\t\t\t\tif (!cl_sweep(g, g->pos[k], g->pos[k - 1]))\n\t\t\t\t\tg->combo = 0;",
     "\t\t\t\tg->combo = 0;"),
    ("combo-survives-an-idle-joint",
     "\t\t\t\tif (!cl_sweep(g, g->pos[k], g->pos[k - 1]))\n\t\t\t\t\tg->combo = 0;",
     "\t\t\t\tcl_sweep(g, g->pos[k], g->pos[k - 1]);"),
    ("combo-survives-an-advance",
     "\tg->combo = 0;\n\tif (++g->clock < g->adv_ticks)", "\tif (++g->clock < g->adv_ticks)"),
    ("clock-not-restarted",
     "\tg->clock = 0;\n\tfor (k = 0; k < g->n; k++)\n\t\tg->pos[k]++;",
     "\tfor (k = 0; k < g->n; k++)\n\t\tg->pos[k]++;"),
    ("loss-one-cell-late",
     "if (g->n > 0 && g->pos[0] >= CL_PATHLEN - 1)", "if (g->n > 0 && g->pos[0] >= CL_PATHLEN)"),
    ("win-ignores-the-supply",
     "else if (g->n == 0 && g->supply == 0)", "else if (g->n == 0)"),
    ("spawn-run-guard-removed",
     "\tif (g->n >= 2 && g->col[g->n - 1] == c && g->col[g->n - 2] == c)\n\t\tc = c % g->ncol + 1;\n",
     ""),
    ("ray-rounds-down",
     "int ox = (2 * t * ax + m) / (2 * m), oy = (2 * t * ay + m) / (2 * m);",
     "int ox = (2 * t * ax) / (2 * m), oy = (2 * t * ay) / (2 * m);"),
    ("direction-table-entry-swapped",
     "{ 1, 0 }, { 5, 2 }, { 1, 1 }, { 2, 5 },", "{ 1, 0 }, { 5, 2 }, { 1, 1 }, { 5, 2 },"),
    ("spiral-lane-check-removed",
     "\treturn mx >= 0 && mx < CL_W && my >= 0 && my < CL_H && cl_idx[my][mx] >= 0;",
     "\treturn mx >= 0 && mx < CL_W && my >= 0 && my < CL_H && 0;"),
    ("spiral-turns-left",
     "\t\t\tdx = -dy;\n\t\t\tdy = t;", "\t\t\tdx = dy;\n\t\t\tdy = -t;"),
    ("turn-without-wrap",
     "g->dir = (g->dir + steps) & (CL_NDIR - 1);", "g->dir = (g->dir + steps) % CL_NDIR;"),
    ("shot-cycle-stuck",
     "\t\tif (g->shot_script[g->shot_at] == 0)\n\t\t\tg->shot_at = 0;\n",
     ""),
    ("miss-still-shoots-a-ball",
     "\tg->shot = g->next;\n\tg->next = cl_pick_shot(g);\n\treturn hit >= 0;",
     "\treturn hit >= 0;"),
    ("fire-after-the-end",
     "\tif (g->state != CL_PLAY)\n\t\treturn 0;\n\tc = g->shot;", "\tc = g->shot;"),
    ("tick-after-the-end",
     "\tif (g->state != CL_PLAY)\n\t\treturn;\n\tif (cl_first_gap(g)) {",
     "\tif (cl_first_gap(g)) {"),
]

# (name, old text, new text, play.py cases that must fail)
PLAY = [
    ("wall-drawn-one-column-left",
     "#define WALL_R\t\t(CELL_COL + 2 * CL_W)", "#define WALL_R\t\t(CELL_COL + 2 * CL_W - 1)",
     ["layout"]),
    ("hole-not-drawn",
     "\tview[cl_path[CL_PATHLEN - 1][1]][cl_path[CL_PATHLEN - 1][0]] = VC_HOLE;\n", "",
     ["layout", "loss_and_restart"]),
    ("aim-line-hidden",
     "\t\t\tview[y][x] = VC_AIM;", "\t\t\tview[y][x] = view[y][x];",
     ["layout", "turn"]),
    ("aim-line-through-balls",
     "\t\t\tif (view[y][x] > VC_PATH && view[y][x] != VC_HOLE)\n\t\t\t\tbreak;\n", "",
     ["clear_and_reaction"]),
    ("launcher-shows-next-color",
     "(unsigned char)(VC_LAUNCH + game.shot)", "(unsigned char)(VC_LAUNCH + game.next)",
     ["layout", "miss"]),
    ("dot-key-ignored",
     "\t\tif (testmode)\n\t\t\tcl_tick(&game);", "\t\tbreak;",
     ["spawn", "clear_and_reaction", "loss_and_restart"]),
    ("down-turns-a-quarter",
     "cl_turn(&game, CL_NDIR / 2);", "cl_turn(&game, CL_NDIR / 4);",
     ["turn"]),
    ("left-turns-right",
     "\tcase GK_LEFT:\n\t\tcl_turn(&game, -1);", "\tcase GK_LEFT:\n\t\tcl_turn(&game, 1);",
     ["turn"]),
    ("pause-key-ignored",
     "\tif (k == 'p' || k == GT_ESC) {", "\tif (k == 'q' + 1) {",
     ["pause"]),
    ("pause-lets-keys-through",
     "\tif (paused)\n\t\treturn 0;\n\tswitch (k) {", "\tswitch (k) {",
     ["pause"]),
    ("restart-keeps-the-score",
     "\t\t\tif (game.state == CL_LOST)\n\t\t\t\tgame.score = 0;\n", "",
     ["restart_resets_score"]),
    ("restart-key-ignored",
     "\t\tif (k == ' ' || k == '\\r' || k == '\\n') {", "\t\tif (k == '~') {",
     ["clear_and_reaction", "loss_and_restart"]),
    ("level-clear-message-missing",
     "\t\tgt_str(\"LEVEL CLEAR  Enter: next level\");", "\t\tgt_str(\"\");",
     ["clear_and_reaction"]),
    ("status-counts-chain-only",
     "long left = game.supply + game.n;", "long left = game.n;",
     ["layout", "spawn"]),
    ("monochrome-flag-ignored",
     "\t\tmono = 1;\n", "",
     ["color_and_mono"]),
    ("clock-never-ticks",
     "\t\tfor (acc += (unsigned long)dt; acc >= CL_TICK_MS; acc -= CL_TICK_MS)\n\t\t\tcl_tick(&game);",
     "\t\tacc = 0;",
     ["clock"]),
    ("usage-exits-zero",
     "\tgt_put_fd(2, msg, sizeof msg - 1);\n\treturn 2;", "\tgt_put_fd(2, msg, sizeof msg - 1);\n\treturn 0;",
     ["usage"]),
    ("level-ignored",
     "\t\tstart_level = atoi(argv[ai++]);", "\t\tstart_level = 1 + 0 * atoi(argv[ai++]);",
     ["clock"]),
]


def main():
    args = [a for a in sys.argv[1:] if a != "--play"]
    src = os.path.abspath(args[0] if args else os.path.join(HERE, ".."))
    top = os.path.join(src, "..", "..")
    if "--play" in sys.argv[1:]:
        return mutlib.play(src, "coil.c", "coil-host", PLAY)
    return mutlib.logic(src, "coil.c", "logic_test.c",
                        os.path.join(src, "tests", "vectors.txt"), LOGIC, top)


if __name__ == "__main__":
    sys.exit(main())
