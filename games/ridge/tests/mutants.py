#!/usr/bin/env python3
# Mutation calibration of the ridge gates (see games/mutlib.py).
#
# usage: mutants.py [--render] [ridge-dir]
#
# Without --render every mutant of the rules half of ridge.c must fail
# tests/logic_test.c; with --render every mutant of the render half must fail
# tests/render_test.c. Each mutant is one textual substitution that occurs
# once in ridge.c.

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
import mutlib  # noqa: E402  (path set above)

TOP = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SHIM = os.path.join(TOP, "tests", "picocalc", "shim")

# (name, old text, new text)
LOGIC = [
    # Physics constants and rules, each fixed by a hand-derived table.
    (
        "gravity-one-unit-heavier",
        "\tp->vy += RG_GRAV;\n\tif (p->vy > RG_VFALL)",
        "\tp->vy += RG_GRAV + 1;\n\tif (p->vy > RG_VFALL)",
    ),
    (
        "fall-cap-removed",
        "\tif (p->vy > RG_VFALL)\n\t\tp->vy = RG_VFALL;\n\trg_move_x(g, p, p->vx);",
        "\trg_move_x(g, p, p->vx);",
    ),
    ("jump-speed-lower", "p->vy = -RG_JUMP;", "p->vy = -(RG_JUMP - 12);"),
    (
        "jump-never-cut",
        "if (p->jumping && !(in & RG_IN_JUMP) && p->vy < -RG_CUT)",
        "if (p->jumping && 0 && p->vy < -RG_CUT)",
    ),
    (
        "cut-applies-without-a-jump",
        "if (p->jumping && !(in & RG_IN_JUMP) && p->vy < -RG_CUT)",
        "if (!(in & RG_IN_JUMP) && p->vy < -RG_CUT)",
    ),
    ("run-speed-faster", "#define RG_VMAX\t\t40", "#define RG_VMAX\t\t48"),
    (
        "air-accelerates-like-ground",
        "acc = p->ground ? RG_ACC_G : RG_ACC_A;",
        "acc = RG_ACC_G;",
    ),
    ("coyote-time-longer", "#define RG_COYOTE\t4", "#define RG_COYOTE\t5"),
    ("coyote-time-shorter", "#define RG_COYOTE\t4", "#define RG_COYOTE\t3"),
    ("jump-buffer-shorter", "#define RG_JBUF\t\t5", "#define RG_JBUF\t\t4"),
    (
        "coyote-not-refilled-on-ground",
        "\tif (p->ground)\n\t\tp->coyote = RG_COYOTE;\n\telse if",
        "\tif (p->ground && p->coyote == 0)\n\t\tp->coyote = RG_COYOTE;\n\telse if",
    ),
    (
        "drop-key-ignored",
        "\tif ((in & RG_IN_DOWN) && p->ground)\n\t\tp->drop = RG_DROP;\n",
        "",
    ),
    (
        "ledge-also-catches-from-inside",
        "\tif (y_old + RG_PH * RG_SUB > row * RG_T)\n\t\treturn 0;\n",
        "\tif (0 && y_old)\n\t\treturn 0;\n",
    ),
    (
        "ledge-ignores-the-drop-timer",
        "if (s > 0 && p->drop == 0 && rg_hit_ledge(g, p->x, p->y, ny)) {",
        "if (s > 0 && rg_hit_ledge(g, p->x, p->y, ny)) {",
    ),
    (
        "right-wall-snaps-one-unit-in",
        "p->x = rg_tdiv(nx + RG_PW * RG_SUB - 1) * RG_T - RG_PW * RG_SUB;",
        "p->x = rg_tdiv(nx + RG_PW * RG_SUB - 1) * RG_T - RG_PW * RG_SUB + 1;",
    ),
    (
        "left-wall-snaps-into-the-wall",
        "p->x = (rg_tdiv(nx) + 1) * RG_T;",
        "p->x = rg_tdiv(nx) * RG_T;",
    ),
    (
        "ceiling-bump-keeps-the-rise",
        "\t\t\t\tp->y = (rg_tdiv(ny) + 1) * RG_T;\n\t\t\t}\n\t\t\tp->vy = 0;",
        "\t\t\t\tp->y = (rg_tdiv(ny) + 1) * RG_T;\n\t\t\t}",
    ),
    (
        "landing-does-not-ground",
        "\t\t\t\tp->ground = 1;\n\t\t\t} else {",
        "\t\t\t} else {",
    ),
    (
        "box-one-unit-too-tall",
        "int ty0 = rg_tdiv(y), ty1 = rg_tdiv(y + RG_PH * RG_SUB - 1);",
        "int ty0 = rg_tdiv(y), ty1 = rg_tdiv(y + RG_PH * RG_SUB);",
    ),
    ("box-narrower", "#define RG_PW\t\t10", "#define RG_PW\t\t8"),
    ("collision-step-too-long", "#define RG_STEP\t\t64", "#define RG_STEP\t\t192"),
    (
        "map-top-is-open",
        "if (tx < 0 || tx >= g->w || ty < 0)\n\t\treturn RG_ROCK;\n\tif (ty >= RG_H)",
        "if (tx < 0 || tx >= g->w)\n\t\treturn RG_ROCK;\n\tif (ty < 0 || ty >= RG_H)",
    ),
    (
        "map-bottom-is-rock",
        "\tif (ty >= RG_H)\n\t\treturn RG_AIR;",
        "\tif (ty >= RG_H)\n\t\treturn RG_ROCK;",
    ),
    (
        "pit-one-row-early",
        "return p->y > (RG_H + 2) * RG_T;",
        "return p->y > (RG_H + 1) * RG_T;",
    ),
    # Hazards, items and scoring.
    (
        "spike-hits-the-whole-cell",
        "p->y + RG_PH * RG_SUB > ty * RG_T + 5 * RG_SUB)",
        "p->y + RG_PH * RG_SUB > ty * RG_T)",
    ),
    (
        "spike-left-edge-not-inset",
        "p->x + RG_PW * RG_SUB > tx * RG_T + 2 * RG_SUB &&",
        "p->x + RG_PW * RG_SUB > tx * RG_T &&",
    ),
    (
        "spike-right-edge-less-inset",
        "if (p->x < (tx + 1) * RG_T - 2 * RG_SUB &&",
        "if (p->x < (tx + 1) * RG_T - 1 * RG_SUB &&",
    ),
    ("shard-worth-five", "#define RG_SCORE_SHARD\t10UL", "#define RG_SCORE_SHARD\t5UL"),
    ("stomp-worth-forty", "#define RG_SCORE_STOMP\t50UL", "#define RG_SCORE_STOMP\t40UL"),
    ("seconds-worth-three", "#define RG_SCORE_SECOND\t2UL", "#define RG_SCORE_SECOND\t3UL"),
    ("all-shards-bonus-always", "\tif (g->shards == g->nshards)\n", "\tif (1)\n"),
    ("all-shards-bonus-never", "\tif (g->shards == g->nshards)\n", "\tif (0)\n"),
    (
        "shard-stays-in-the-map",
        "\t\t\t\trg_put(g, tx, ty, RG_AIR);\n\t\t\t\tg->shards++;",
        "\t\t\t\tg->shards++;",
    ),
    (
        "flag-stays-down",
        "rg_put(g, tx, ty, RG_FLAGUP);",
        "rg_put(g, tx, ty, RG_FLAG);",
    ),
    (
        "flag-sets-no-checkpoint",
        "\t\t\t\tg->spawn_x = tx * RG_T + (RG_T - RG_PW * RG_SUB) / 2;\n",
        "",
    ),
    (
        "respawn-at-the-left-edge",
        "\tg->p.x = g->spawn_x;\n\tg->p.y = g->spawn_y;",
        "\tg->p.x = 0;\n\tg->p.y = g->spawn_y;",
    ),
    ("death-costs-no-life", "\tg->lives--;\n", ""),
    (
        "game-continues-at-zero-lives",
        "\t\t\tif (g->lives > 0)\n\t\t\t\trg_respawn(g);",
        "\t\t\tif (g->lives >= 0)\n\t\t\t\trg_respawn(g);",
    ),
    (
        "stomp-window-too-deep",
        "bottom0 <= e->y + RG_STOMP * RG_SUB",
        "bottom0 <= e->y + 4 * RG_STOMP * RG_SUB",
    ),
    (
        "enemy-overshoots-its-corridor",
        "\t\t\te->x = e->x1;\n\t\t\te->dir = -1;",
        "\t\t\te->dir = -1;",
    ),
    ("enemy-faster", "#define RG_ESPD\t\t12", "#define RG_ESPD\t\t16"),
    (
        "respawn-grants-no-grace",
        "\tg->p.grace = RG_GRACE;\n",
        "",
    ),
    (
        "grace-ends-while-a-crawler-touches",
        "if (p->grace > 0 && (!touching || p->grace > 1))",
        "if (p->grace > 0 && (!touching || p->grace > 0))",
    ),
    (
        "grace-never-runs-down",
        "if (p->grace > 0 && (!touching || p->grace > 1))\n\t\tp->grace--;",
        "if (touching < 0)\n\t\tp->grace--;",
    ),
    (
        "grace-also-spares-stomped-crawlers",
        "\t\tif (fell && bottom0 <= e->y + RG_STOMP * RG_SUB) {",
        "\t\tif (p->grace == 0 && fell && bottom0 <= e->y + RG_STOMP * RG_SUB) {",
    ),
    (
        "stomp-rebuilt-from-the-landed-velocity",
        "\t\tif (fell && bottom0 <= e->y + RG_STOMP * RG_SUB) {",
        "\t\tif ((void)bottom0, (void)fell, p->vy > 0 &&\n"
        "\t\t    p->y + RG_PH * RG_SUB - p->vy <= e->y + RG_STOMP * RG_SUB) {",
    ),
    (
        "stomp-without-a-fall",
        "\t\tif (fell && bottom0 <= e->y + RG_STOMP * RG_SUB) {",
        "\t\tif ((void)fell, bottom0 <= e->y + RG_STOMP * RG_SUB) {",
    ),
    (
        "stomp-measured-after-the-move",
        "\tbottom0 = g->p.y + RG_PH * RG_SUB;\n\ty0 = g->p.y;\n\trg_player_step(g, &g->p, in);",
        "\ty0 = g->p.y;\n\trg_player_step(g, &g->p, in);\n\tbottom0 = g->p.y + RG_PH * RG_SUB;",
    ),
    # Level flow.
    (
        "clock-never-runs",
        "\tif (g->time_left > 0)\n\t\tg->time_left--;\n",
        "",
    ),
    (
        "ready-ends-at-once",
        "\t\tif (--g->timer <= 0)\n\t\t\tg->state = RG_PLAY;\n\t\treturn;",
        "\t\tg->state = RG_PLAY;\n\t\treturn;",
    ),
    (
        "clear-reloads-the-level",
        "\t\t\t\trg_load(g, g->level + 1);",
        "\t\t\t\trg_load(g, g->level);",
    ),
    (
        "last-level-ends-in-game-over",
        "\t\t\t\tg->state = RG_WON;",
        "\t\t\t\tg->state = RG_OVER;",
    ),
    (
        "new-game-keeps-the-score",
        "\tg->score = 0;\n\tg->lives = RG_LIVES;",
        "\tg->lives = RG_LIVES;",
    ),
    (
        "camera-snap-not-clamped-right",
        "\tif (g->cam > g->w - RG_VIEW_W)\n\t\tg->cam = g->w - RG_VIEW_W;\n\tif (g->cam < 0)",
        "\tif (g->cam < 0)",
    ),
    (
        "camera-never-scrolls-back",
        "\telse if (in_view < RG_DZ_LEFT * RG_TW && g->cam > 0)\n\t\tg->cam--;",
        "",
    ),
    (
        "camera-runs-past-the-level",
        "if (in_view > RG_DZ_RIGHT * RG_TW && g->cam < g->w - RG_VIEW_W)",
        "if (in_view > RG_DZ_RIGHT * RG_TW)",
    ),
    ("camera-moves-two-columns", "\t\tg->cam++;\n\telse if", "\t\tg->cam += 2;\n\telse if"),
    # Key events.
    (
        "release-keeps-the-key-down",
        "\t\tk->down &= ~(1U << slot);",
        "\t\t(void)slot;",
    ),
    (
        "hold-counts-as-a-press",
        "first = state == LCD_KEY_PRESSED && !(k->down >> slot & 1);",
        "first = state != LCD_KEY_RELEASED && !(k->down >> slot & 1);",
    ),
    (
        "repeated-press-is-a-new-edge",
        "first = state == LCD_KEY_PRESSED && !(k->down >> slot & 1);",
        "first = state == LCD_KEY_PRESSED;",
    ),
    (
        "jump-edge-retriggers-on-repeat",
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
        "\tif (state == LCD_KEY_PRESSED)\n\t\tk->edge |= rg_slot_bit[slot];\n"
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
    ),
    (
        "pause-retriggers-on-repeat",
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
        "\tif (state == LCD_KEY_PRESSED && slot == RG_S_P)\n\t\tk->pause = 1;\n"
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
    ),
    (
        "quit-retriggers-on-repeat",
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
        "\tif (state == LCD_KEY_PRESSED && (slot == RG_S_Q || slot == RG_S_ESC))\n"
        "\t\tk->quit = 1;\n"
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
    ),
    (
        "confirm-retriggers-on-repeat",
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
        "\tif (state == LCD_KEY_PRESSED && (slot == RG_S_ENTER || slot == RG_S_SPACE))\n"
        "\t\tk->restart = 1;\n"
        "\tif (!first)\n\t\treturn;\n\tk->edge |= rg_slot_bit[slot];",
    ),
    (
        "releasing-one-alias-cuts-the-action",
        "\tk->held = 0;\n\tfor (i = 0; i < RG_NSLOT; i++)\n\t\tif (k->down >> i & 1)\n"
        "\t\t\tk->held |= rg_slot_bit[i];\n",
        "\ti = 0;\n\t(void)i;\n\tif (state == LCD_KEY_RELEASED)\n"
        "\t\tk->held &= ~rg_slot_bit[slot];\n"
        "\telse\n\t\tk->held |= rg_slot_bit[slot];\n",
    ),
    (
        "lost-events-keep-the-keys-down",
        "\tk->held = 0;\n\tk->down = 0;\n",
        "\tk->held = 0;\n",
    ),
    (
        "tap-is-lost",
        "unsigned both = k->held | k->edge, in = 0;",
        "unsigned both = k->held, in = 0;",
    ),
    ("edges-never-consumed", "\tk->edge = 0;\n\treturn in;", "\treturn in;"),
    (
        "lost-events-keep-the-held-keys",
        "rg_keys_lost(struct rg_keys *k)\n{\n\tk->held = 0;",
        "rg_keys_lost(struct rg_keys *k)\n{\n\t(void)k;",
    ),
    (
        "capital-letters-unmapped",
        "\tif (key >= 'A' && key <= 'Z')\n\t\tkey += 'a' - 'A';\n",
        "",
    ),
    (
        "space-does-not-jump",
        "\tRG_K_JUMP, RG_K_DOWN, RG_K_DOWN, 0, 0, 0, 0",
        "\t0, RG_K_DOWN, RG_K_DOWN, 0, 0, 0, 0",
    ),
    (
        "escape-does-not-quit",
        "else if (slot == RG_S_Q || slot == RG_S_ESC)",
        "else if (slot == RG_S_Q)",
    ),
    (
        "jump-press-edge-dropped",
        "\tif (k->edge & RG_K_JUMP)\n\t\tin |= RG_IN_JPRESS;\n",
        "",
    ),
    (
        "drop-reported-while-held",
        "\tif (k->edge & RG_K_DOWN)\n\t\tin |= RG_IN_DOWN;",
        "\tif (k->held & RG_K_DOWN)\n\t\tin |= RG_IN_DOWN;",
    ),
    # Level data: each edit must be caught by the level rules or the search.
    (
        "level1-wall-too-high",
        "\tFILL(56, 21, 3, 3),",
        "\tFILL(56, 15, 3, 9),",
    ),
    (
        "level1-shard-buried",
        "\tSHARDS(8, 22, 2, 2),\n",
        "\tSHARDS(8, 22, 2, 2),\n\tFILL(8, 22, 1, 1),\n",
    ),
    (
        "level1-crawler-walks-off-the-floor",
        "\tCRAWL(32, 23, 36),",
        "\tCRAWL(32, 23, 50),",
    ),
    (
        "level2-start-inside-rock",
        "\tSTART(2, 23),\n\tGND(0, 24, 14),",
        "\tSTART(2, 26),\n\tGND(0, 24, 14),",
    ),
    ("level3-ledge-missing", "\tLEDGE(32, 18, 8),\n", ""),
    ("level4-gap-too-wide", "\tGND(105, 20, 3),\n", ""),
    (
        "level4-patrol-reaches-the-checkpoint",
        "\tCRAWL(39, 23, 44),\n\tFLAG(37, 23),",
        "\tCRAWL(38, 23, 43),\n\tFLAG(37, 23),",
    ),
    (
        "level4-spike-under-the-goal-approach",
        "\tGND(146, 18, 14),",
        "\tGND(146, 18, 14),\n\tSPIKE(147, 17, 8),",
    ),
]

# (name, old text, new text)
RENDER = [
    (
        "view-ignores-the-camera",
        "rg_base_tile(g, g->cam + x, y);",
        "rg_base_tile(g, x, y);",
    ),
    (
        "world-drawn-over-the-hud",
        "v->fin[(y + RG_HUD_ROWS) * GTILE_COLS + x] =",
        "v->fin[y * GTILE_COLS + x] =",
    ),
    (
        "rock-never-capped",
        "return wy > 0 && rg_cell(g, wx, wy - 1) == RG_ROCK ? T_ROCK : T_CAP;",
        "return T_ROCK;",
    ),
    ("abyss-missing", "\tif (wy >= RG_H)\n\t\treturn T_ABYSS;\n", ""),
    (
        "sprite-signature-ignores-motion",
        "if (!sg->live || memcmp(sg, &now, sizeof now) != 0) {",
        "if (!sg->live) {",
    ),
    ("sprite-signature-ignores-the-camera", "now.cam = g->cam;", "now.cam = 0;"),
    ("sprite-signature-ignores-the-twinkle", "now.anim = anim;", "now.anim = 0 * anim;"),
    ("sprite-signature-ignores-map-changes", "now.gen = g->gen;", "now.gen = 0;"),
    (
        "sprite-not-mirrored",
        "rg_art[s->frame][y][s->flip ? 15 - x : x]",
        "rg_art[s->frame][y][x]",
    ),
    (
        "sprite-box-ignores-the-camera",
        "bxs[i] = rg_fdiv8(spr[i].x - g->cam * RG_TW);",
        "bxs[i] = rg_fdiv8(spr[i].x);",
    ),
    (
        "shared-tiles-take-only-the-owners-sprite",
        "\t\tif (k == i || (rg_near(bxs[k], bx) && rg_near(bys[k], by)))",
        "\t\tif (k == i)",
    ),
    (
        "shared-tiles-replaced-by-the-later-sprite",
        [
            "\t\tif (k == i || (rg_near(bxs[k], bx) && rg_near(bys[k], by)))",
            "\tfor (i = n - 1; i >= 0; i--)\n\t\tfor (cy = 0; cy < RG_BOX; cy++)",
        ],
        [
            "\t\tif (k == i)",
            "\tfor (i = 0; i < n; i++)\n\t\tfor (cy = 0; cy < RG_BOX; cy++)",
        ],
    ),
    (
        "z-order-puts-the-climber-below-the-crawlers",
        "\tfor (k = n - 1; k >= 0; k--)\n\t\tif (k == i ||",
        "\tfor (k = 0; k < n; k++)\n\t\tif (k == i ||",
    ),
    (
        "signature-forgets-the-neighbors",
        "\t\t\t\tnow.ov[now.nov].s = spr[k];\n",
        "",
    ),
    (
        "signature-forgets-the-neighbor-order",
        "\t\t\t\tnow.ov[now.nov].rel = k - i;\n",
        "",
    ),
    (
        "crawler-boxes-never-meet",
        "return a - b < RG_BOX && b - a < RG_BOX;",
        "return a == b;",
    ),
    (
        "grace-never-blinks",
        "&& !(p->grace > 0 && (g->ticks >> 2 & 1))",
        "",
    ),
    (
        "sprites-share-slots",
        "gtile_load(&v->gt, RG_DYN + i * RG_BOX * RG_BOX, RG_BOX * RG_BOX,",
        "gtile_load(&v->gt, RG_DYN, RG_BOX * RG_BOX,",
    ),
    (
        "composite-drops-the-world-under-the-sprite",
        "rg_tile_px(rg_base_tile(g, g->cam + vx, vy), px);",
        "rg_fill_px(px, P_SKY);",
    ),
    (
        "crawler-faces-the-wrong-way",
        "out[n].flip = e->dir < 0;",
        "out[n].flip = e->dir > 0;",
    ),
    (
        "climber-frame-inverted",
        "out[n].frame = !p->ground ? RG_F_JUMP :",
        "out[n].frame = p->ground ? RG_F_JUMP :",
    ),
    (
        "run-frames-never-alternate",
        "p->vx != 0 ? RG_F_RUN + ((p->anim >> 2) & 1) : RG_F_STAND;",
        "p->vx != 0 ? RG_F_RUN : RG_F_STAND;",
    ),
    ("shard-never-twinkles", "return T_SHARD0 + (int)((g->ticks >> 3) & 1);", "return T_SHARD0;"),
    ("banner-on-the-wrong-row", 'rg_banner(v, 14, "PAUSED");', 'rg_banner(v, 13, "PAUSED");'),
    (
        "score-digits-collide",
        "for (i = n - 1; i >= 0; i--, val /= 10)",
        "for (i = n - 1; i >= 0; i--, val /= 100)",
    ),
    (
        "palette-skips-an-entry",
        "\tfor (i = 0; i < LCDG_NPAL; i++) {",
        "\tfor (i = 0; i < LCDG_NPAL - 1; i++) {",
    ),
    (
        "last-static-tile-not-loaded",
        "\tfor (t = 0; t < RG_NSTATIC; t++) {",
        "\tfor (t = 0; t < RG_NSTATIC - 1; t++) {",
    ),
    (
        "ridge-climbs-two-rows",
        "\t\t\trow += t == 'D' ? 0 : -1;",
        "\t\t\trow += t == 'D' ? 0 : -2;",
    ),
    (
        "ridge-fall-forgets-the-row",
        "\t\t\trow += t == 'D' ? 1 : 0;",
        "\t\t\trow += 0;",
    ),
    ("sky-tile-painted-as-hud", "\tcase T_SKY:\n\t\tbreak;\n", ""),
    (
        "view-skips-the-last-column",
        "for (x = 0; x < RG_VIEW_W; x++)\n\t\t\tv->fin[(y + RG_HUD_ROWS)",
        "for (x = 0; x < RG_VIEW_W - 1; x++)\n\t\t\tv->fin[(y + RG_HUD_ROWS)",
    ),
]


def render(src):
    """Compile tests/render_test.c against each mutant of ridge.c; every one must fail."""
    text = open(os.path.join(src, "ridge.c")).read()
    san = mutlib.sanitizers(TOP)
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        # ridge.c includes ../gametile.h, so mirror games/ridge and games/gametile.h.
        games = os.path.join(work, "games")
        tests = os.path.join(games, "ridge", "tests")
        os.makedirs(tests)
        shutil.copy(os.path.join(src, "..", "gametile.h"), games)
        shutil.copy(os.path.join(src, "tests", "render_test.c"), tests)
        exe = os.path.join(work, "render")
        mutated = os.path.join(games, "ridge", "ridge.c")

        def build_run():
            cmd = (
                [mutlib.CC]
                + mutlib.STRICT
                + san
                + [
                    "-D_DEFAULT_SOURCE",
                    "-I" + SHIM,
                    "-o",
                    exe,
                    os.path.join(tests, "render_test.c"),
                ]
            )
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                return None, "compile error:\n" + r.stderr
            try:
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
            except subprocess.TimeoutExpired:
                return 124, "timeout"
            return r.returncode, r.stdout + r.stderr

        with open(mutated, "w") as f:
            f.write(text)
        rc, out = build_run()
        if rc != 0:
            print("baseline: the real sources do not pass:\n" + out)
            return 1
        print("baseline: real sources pass")
        for name, old, new in RENDER:
            olds = old if isinstance(old, list) else [old]
            news = new if isinstance(new, list) else [new]
            if not all(mutlib._anchor_ok(name, text, o) for o in olds):
                failures += 1
                continue
            mutant = text
            for o, n in zip(olds, news):
                mutant = mutant.replace(o, n)
            with open(mutated, "w") as f:
                f.write(mutant)
            rc, out = build_run()
            if rc is None:
                print("mutant %-44s ERROR: %s" % (name, out.strip()))
                failures += 1
            elif rc == 0:
                print("mutant %-44s SURVIVED (the test cannot see it)" % name)
                failures += 1
            else:
                first = next(
                    (ln for ln in out.splitlines() if ln.startswith("FAIL") or "Sanitizer" in ln),
                    "exit %d" % rc,
                )
                print("mutant %-44s rejected: %s" % (name, first[:80]))
    n = len(RENDER)
    print("render mutants: %d tried, %d rejected, %d failures" % (n, n - failures, failures))
    return 1 if failures else 0


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    src = os.path.abspath(args[0]) if args else os.path.join(HERE, "..")
    if "--render" in argv[1:]:
        return render(src)
    return mutlib.logic(
        src,
        "ridge.c",
        "logic_test.c",
        os.devnull,
        LOGIC,
        TOP,
        extra=["-D_DEFAULT_SOURCE", "-I" + SHIM],
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv))
