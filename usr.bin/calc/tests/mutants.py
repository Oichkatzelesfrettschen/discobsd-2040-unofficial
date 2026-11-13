#!/usr/bin/env python3
# Mutation calibration of the calc tests. The unmutated program must pass the
# line-mode table and the pty tests first. Each mutant is one deliberately wrong
# copy of calc.c (textual substitutions that each match exactly once), built with
# the host flags of the Makefile; the suites named for it must reject it. A mutant
# that builds and passes means the tests cannot see that fault, and the run fails.
# The unit factors and the constants are mutated one row at a time by scanning
# the tables of calc.c.
#
# usage: mutants.py [calc-dir]       (HOSTCC selects the compiler)

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import mutant_pool  # noqa: E402  tools/mutant_pool.py, beside this tree's other tools

CC = os.environ.get("HOSTCC", "cc")
FLAGS = [
    "-std=c17",
    "-pedantic-errors",
    "-D_DEFAULT_SOURCE",
    "-DHOSTBUILD",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wshadow",
    "-Wconversion",
    "-O1",
]
SAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]

PLAY_ALL = None  # every pty case

# (name, [(old, new), ...], line suites, pty cases or None for none, sanitized build)
MUTANTS = [
    (
        "multiplication-and-addition-precedence-swapped",
        [
            (
                "\t\tif (c != '*' && c != '/' && c != '%')\n\t\t\treturn v;\n\t\tsrc++;\n"
                "\t\tv = arith(c, v, unary());",
                "\t\tif (c != '+' && c != '-')\n\t\t\treturn v;\n\t\tsrc++;\n"
                "\t\tv = arith(c, v, unary());",
            ),
            (
                "\t\tif (c != '+' && c != '-')\n\t\t\treturn v;\n\t\tsrc++;\n"
                "\t\tv = arith(c, v, term());",
                "\t\tif (c != '*' && c != '/' && c != '%')\n\t\t\treturn v;\n\t\tsrc++;\n"
                "\t\tv = arith(c, v, term());",
            ),
        ],
        ["alg", "rpn"],
        None,
        False,
    ),
    (
        "power-left-associative",
        [
            (
                "\tif (peek() == '^') {\n\t\tsrc++;\n\t\tb = dpow(b, unary());",
                "\twhile (peek() == '^') {\n\t\tsrc++;\n\t\tb = dpow(b, postfix());",
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "nesting-bound-removed",
        [
            (
                "\tdouble v;\n\tint c;\n\n\tif (++depth > MAXDEPTH + 1)\n"
                '\t\tfail("nested too deeply");',
                "\tdouble v;\n\tint c;\n\n\t++depth;",
            )
        ],
        ["nest"],
        None,
        False,
    ),
    (
        "nesting-bound-17",
        [
            (
                "\tdouble v;\n\tint c;\n\n\tif (++depth > MAXDEPTH + 1)",
                "\tdouble v;\n\tint c;\n\n\tif (++depth > MAXDEPTH + 2)",
            )
        ],
        ["nest"],
        None,
        False,
    ),
    (
        "nesting-bound-15",
        [
            (
                "\tdouble v;\n\tint c;\n\n\tif (++depth > MAXDEPTH + 1)",
                "\tdouble v;\n\tint c;\n\n\tif (++depth > MAXDEPTH)",
            )
        ],
        ["nest"],
        None,
        False,
    ),
    (
        "programmer-nesting-bound-removed",
        [
            (
                "\tword v;\n\tint c;\n\n\tif (++depth > MAXDEPTH + 1)\n"
                '\t\tfail("nested too deeply");',
                "\tword v;\n\tint c;\n\n\t++depth;",
            )
        ],
        ["prog"],
        None,
        False,
    ),
    (
        "unary-minus-binds-tighter-than-power",
        [
            (
                "\t\tv = -unary();",
                "\t\tv = -postfix();\n\t\tif (peek() == '^') {\n\t\t\tsrc++;\n"
                "\t\t\tv = dpow(v, unary());\n\t\t}",
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "unary-minus-looser-than-addition",
        [("\t\tv = -unary();", "\t\tv = -expr();")],
        ["alg"],
        None,
        False,
    ),
    (
        "terminal-restore-skipped-on-signal",
        [
            (
                "on_signal(int sig)\n{\n\ttty_leave();",
                "on_signal(int sig)\n{\n\tput_fd(1, reset_seq, (size_t)reset_len);",
            )
        ],
        [],
        ["signals"],
        False,
    ),
    (
        "terminal-restore-skipped-everywhere",
        [("\ttty_raw = 0;\n\ttty_restore();", "\ttty_raw = 0;\n\t(void)tty_restore;")],
        [],
        ["signals", "exit_paths"],
        False,
    ),
    (
        "signal-reset-sequence-skipped",
        [("\ttty_restore();\n\tput_fd(1, reset_seq, (size_t)reset_len);", "\ttty_restore();")],
        [],
        ["signals", "exit_paths"],
        False,
    ),
    (
        "sto-name-unchecked",
        [("\tif (!validname(name))\n", "\tif (0 && !validname(name))\n")],
        ["rpn"],
        None,
        False,
    ),
    (
        "nonfinite-format-unguarded",
        [
            (
                "\tif (!fin(x)) {\n\t\tsnprintf(buf, n, \"%s\", "
                "x != x ? \"nan\" : x < 0.0 ? \"-inf\" : \"inf\");\n"
                "\t\treturn;\n\t}\n",
                "",
            )
        ],
        ["stats"],
        None,
        False,
    ),
    (
        "stop-restore-skipped",
        [("\ttty_leave();\n\tsignal(sig, SIG_DFL);", "\tsignal(sig, SIG_DFL);")],
        [],
        ["stop_continue"],
        False,
    ),
    (
        "continue-cbreak-not-reentered",
        [("\tsignal(sig, on_stop);\n\ttty_enter();", "\tsignal(sig, on_stop);")],
        [],
        ["stop_continue"],
        False,
    ),
    (
        "sigint-handler-not-installed",
        [("\tsignal(SIGINT, on_signal);\n", "")],
        [],
        ["signals"],
        False,
    ),
    (
        "sigterm-handler-not-installed",
        [("\tsignal(SIGTERM, on_signal);\n", "")],
        [],
        ["signals"],
        False,
    ),
    (
        "signal-exit-status-zero",
        [("\t_exit(128 + sig);", "\t(void)sig;\n\t_exit(0);")],
        [],
        ["signals"],
        False,
    ),
    (
        "atexit-restore-removed",
        [("\tatexit(on_exit_restore);\n", "\t(void)on_exit_restore;\n")],
        [],
        ["exit_paths"],
        False,
    ),
    (
        "echo-left-on",
        [("t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);", "t.c_lflag &= ~(tcflag_t)ICANON;")],
        [],
        ["evaluate"],
        False,
    ),
    (
        "scroll-region-not-set",
        [('\\033[1;%dr\\033[1;1H", rows - 1);', '\\033[1;1H", rows - 1);')],
        [],
        ["scroll"],
        False,
    ),
    (
        "bar-restore-cursor-dropped",
        [('\\033[0m\\0338", SCREEN_COLS - 51', '\\033[0m", SCREEN_COLS - 51')],
        [],
        ["layout", "evaluate"],
        False,
    ),
    (
        "bar-wider-than-53-columns",
        [("SCREEN_COLS - 51", "SCREEN_COLS - 49")],
        [],
        ["layout"],
        False,
    ),
    (
        "bar-angle-label-inverted",
        [("if (i == 4 && !degmode)", "if (i == 4 && degmode)")],
        [],
        ["layout", "fkeys"],
        False,
    ),
    (
        "bar-active-item-inverted",
        [('active ? "1" : "7"', 'active ? "7" : "1"')],
        [],
        ["layout"],
        False,
    ),
    (
        "bar-not-redrawn-after-command",
        [("\t\t\t\trun_line(line);\n\t\t\t\tbar_draw();", "\t\t\t\trun_line(line);")],
        [],
        ["evaluate"],
        False,
    ),
    (
        "f5-sequence-code-16",
        [("11, 12, 13, 14, 15, 17, 18, 19, 20, 21", "11, 12, 13, 14, 16, 17, 18, 19, 20, 21")],
        [],
        ["fkeys"],
        False,
    ),
    (
        "f10-sequence-code-22",
        [("11, 12, 13, 14, 15, 17, 18, 19, 20, 21", "11, 12, 13, 14, 15, 17, 18, 19, 20, 22")],
        [],
        ["fkeys"],
        False,
    ),
    (
        "f1-ss3-letter-shifted",
        [
            (
                "return c >= 'P' && c <= 'S' ? K_F1 + (c - 'P') : K_NONE;",
                "return c >= 'Q' && c <= 'S' ? K_F1 + (c - 'Q') : K_NONE;",
            )
        ],
        [],
        ["fkeys"],
        False,
    ),
    (
        "xterm-modified-f1-ignored",
        [("return num[0] == 1 ? K_F1 + (c - 'P') : K_NONE;", "return K_NONE;")],
        [],
        ["fkey_variants"],
        False,
    ),
    (
        "vt220-f1-ignored",
        [
            (
                "static const unsigned char codes[BAR_ITEMS] = {\n\t\t11, 12,",
                "static const unsigned char codes[BAR_ITEMS] = {\n\t\t99, 12,",
            )
        ],
        [],
        ["fkey_variants"],
        False,
    ),
    (
        "linux-console-f-keys-ignored",
        [("return c >= 'A' && c <= 'E' ? K_F1 + (c - 'A') : K_NONE;", "return K_NONE;")],
        [],
        ["fkey_variants"],
        False,
    ),
    (
        "split-sequence-not-awaited",
        [("\tif (wait_input(50) <= 0)\n\t\treturn K_ESC;\n\tc = rdbyte();", "\tc = rdbyte();")],
        [],
        ["fkey_variants", "editing"],
        False,
    ),
    (
        "lone-esc-ignored",
        [
            (
                "\tif (wait_input(50) <= 0)\n\t\treturn K_ESC;",
                "\tif (wait_input(50) <= 0)\n\t\treturn K_NONE;",
            )
        ],
        [],
        ["editing"],
        False,
    ),
    (
        "f2-runs-rpn",
        [('"help", "alg", "rpn", "prog"', '"help", "rpn", "rpn", "prog"')],
        [],
        ["fkeys"],
        False,
    ),
    (
        "f9-runs-quit",
        [('"clr", "quit"', '"quit", "quit"')],
        [],
        ["fkeys"],
        False,
    ),
    (
        "fkey-line-not-restored",
        [
            (
                "\t\t\t\tif (quitting)\n\t\t\t\t\tbreak;\n"
                "\t\t\t\tshow_input(line, len);\n\t\t\t\tcontinue;",
                "\t\t\t\tif (quitting)\n\t\t\t\t\tbreak;\n\t\t\t\tcontinue;",
            )
        ],
        [],
        ["fkey_midline"],
        False,
    ),
    (
        "backspace-ignored",
        [("if (k == 0x7f || k == '\\b')", "if (k == 0x7e)")],
        [],
        ["editing"],
        False,
    ),
    (
        "ctrl-d-quits-mid-line",
        [
            (
                "\t\t\t} else if (k == 0x04) {\n\t\t\t\tif (len == 0) {",
                "\t\t\t} else if (k == 0x04) {\n\t\t\t\tif (1) {",
            )
        ],
        [],
        ["editing"],
        False,
    ),
    (
        "history-up-ignored",
        [("if (k == K_UP && h > 0)", "if (k == K_UP && h > 99)")],
        [],
        ["editing"],
        False,
    ),
    (
        "long-line-not-windowed",
        [("int skip = len > room ? len - room : 0;", "int skip = 0 * room;")],
        [],
        ["long_line"],
        False,
    ),
    (
        "line-mode-writes-escape",
        [("\tline_loop();\n\toflush();", '\toputs("\\033[0m");\n\tline_loop();\n\toflush();')],
        ["process"],
        None,
        False,
    ),
    (
        "tty-test-ignores-stdout-pipe",
        [("isatty(0) && isatty(1)", "isatty(0)")],
        [],
        ["not_a_terminal"],
        False,
    ),
    (
        "dash-l-ignored",
        [("forced_line = 1;", "forced_line = 0;")],
        [],
        ["not_a_terminal"],
        False,
    ),
    (
        "percent-is-division",
        [("return chk(fmod(a, b));", "return chk(a / b);")],
        ["alg"],
        None,
        False,
    ),
    (
        "factorial-limit-171",
        [("x > 170.0", "x > 171.0")],
        ["alg"],
        None,
        False,
    ),
    (
        "factorial-accepts-fractions",
        [
            (
                ' || x != floor(x))\n\t\tfail("domain error");\n\tn = (int)x;',
                ')\n\t\tfail("domain error");\n\tn = (int)x;',
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "degree-quadrants-not-exact",
        [("if (r == 90.0 * (double)q) {", "if (0) {")],
        ["deg"],
        None,
        False,
    ),
    (
        "degree-turn-wrong",
        [("r = fmod(x, 360.0);", "r = fmod(x, 180.0);")],
        ["deg"],
        None,
        False,
    ),
    (
        "degree-mode-default-on",
        [("static int degmode;", "static int degmode = 1;")],
        ["alg", "process"],
        None,
        False,
    ),
    (
        "inverse-trig-stays-in-radians",
        [("return degmode ? calc_angle_from_rad(CALC_DEGREES, r) : r;", "return r;")],
        ["deg"],
        None,
        False,
    ),
    (
        "atan2-arguments-swapped",
        [("return ang_out(atan2(a[0], a[1]));", "return ang_out(atan2(a[1], a[0]));")],
        ["alg", "deg"],
        None,
        False,
    ),
    (
        "acos-sign-lost",
        [
            (
                "2.0 * atan2(sqrt(1.0 - x), sqrt(1.0 + x))",
                "2.0 * atan2(sqrt(1.0 + x), sqrt(1.0 - x))",
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "cbrt-negative-sign-lost",
        [("return x < 0.0 ? -r : r;", "return r;")],
        ["alg"],
        None,
        False,
    ),
    (
        "round-half-up",
        [("return calc_round(x);", "return floor(x + 0.5);")],
        ["alg"],
        None,
        False,
    ),
    (
        "exp-overflow-unchecked",
        [('\t\tif (x > 709.782712893384)\n\t\t\tfail("overflow");\n', "")],
        ["alg"],
        None,
        False,
    ),
    (
        "hypot-overflows",
        [("return chk(hypot(a[0], a[1]));", "return chk(sqrt(a[0] * a[0] + a[1] * a[1]));")],
        ["alg"],
        None,
        False,
    ),
    (
        "division-by-zero-unchecked",
        [
            (
                '\t\tif (b == 0.0)\n\t\t\tfail("division by zero");\n\t\treturn chk(a / b);',
                "\t\treturn chk(a / b);",
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "finiteness-test-always-true",
        [("return x - x == 0.0;", "(void)x;\n\treturn 1;")],
        ["alg"],
        None,
        False,
    ),
    (
        "power-zero-negative-unchecked",
        [
            (
                '\t\tif (a == 0.0)\n\t\t\tfail("division by zero");\n'
                "\t\treturn ipow(chk(1.0 / a), -n);",
                "\t\treturn ipow(chk(1.0 / a), -n);",
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "tan-range-unchecked",
        [('\tif (fabs(x) >= 1.6e9)\n\t\tfail("argument out of range");\n', "")],
        ["alg"],
        None,
        False,
    ),
    (
        "negative-base-parity-dropped",
        [("b - 2.0 * floor(0.5 * b) != 0.0", "0")],
        ["alg"],
        None,
        False,
    ),
    (
        "negative-base-fractional-power-allowed",
        [('\t\tif (b != floor(b))\n\t\t\tfail("domain error");\n', "")],
        ["alg"],
        None,
        False,
    ),
    (
        "reserved-name-assignable",
        [
            (
                '\tif (reserved(name))\n\t\tfailname("cannot assign to", name);',
                '\tif (reserved(name) && 0)\n\t\tfailname("cannot assign to", name);',
            )
        ],
        ["alg"],
        None,
        False,
    ),
    (
        "ans-not-updated",
        [("\t\tans = v;\n\t\tprintnum(v);", "\t\tprintnum(v);")],
        ["alg"],
        None,
        False,
    ),
    (
        "memory-register-off-by-one",
        [("*out = mem[name[1] - '0'];", "*out = mem[(name[1] - '0' + 1) % NMEM];")],
        ["alg"],
        None,
        False,
    ),
    (
        "error-ends-the-session",
        [
            (
                'outf("error: %s\\n", errmsg);\n\t\thad_error = 1;',
                'outf("error: %s\\n", errmsg);\n\t\thad_error = 1;\n\t\tquitting = 1;',
            )
        ],
        ["alg", "process"],
        None,
        False,
    ),
    (
        "exit-status-ignores-errors",
        [("\t\treturn had_error ? 1 : 0;\n\t}\n#ifndef", "\t\treturn 0;\n\t}\n#ifndef")],
        ["process"],
        None,
        False,
    ),
    (
        "digits-ignored",
        [
            (
                'snprintf(raw, sizeof raw, "%.*e", digits - 1, x);',
                'snprintf(raw, sizeof raw, "%.*e", 14, x);',
            )
        ],
        ["digits"],
        None,
        False,
    ),
    (
        "negative-zero-printed",
        [
            (
                '\tif (x == 0.0) {\n\t\tsnprintf(buf, n, "0");',
                '\tif (x == 1.0 / 3.0 * 0.0 - 1.0) {\n\t\tsnprintf(buf, n, "0");',
            )
        ],
        ["digits"],
        None,
        False,
    ),
    (
        "variable-limit-off-by-one",
        [("if (nvars == NVARS)", "if (nvars == NVARS + 1)")],
        ["limits"],
        None,
        True,
    ),
    (
        "argument-limit-off-by-one",
        [
            (
                'if (n == MAXARGS)\n\t\t\t\tfail("too many arguments");',
                'if (n == MAXARGS + 1)\n\t\t\t\tfail("too many arguments");',
            )
        ],
        ["limits"],
        None,
        True,
    ),
    (
        "list-limit-off-by-one",
        [("if (cnt == STATMAX)", "if (cnt == STATMAX + 1)")],
        ["limits"],
        None,
        True,
    ),
    (
        "rpn-stack-limit-off-by-one",
        [("if (sp == STACKMAX)", "if (sp == STACKMAX + 1)")],
        ["limits", "rpn"],
        None,
        True,
    ),
    (
        "line-buffer-off-by-one",
        [
            (
                "if (n < sizeof line - 1)\n\t\t\t\tline[n++] = (char)c;",
                "if (n < sizeof line)\n\t\t\t\tline[n++] = (char)c;",
            )
        ],
        ["process"],
        None,
        True,
    ),
    (
        "rpn-swap-no-op",
        [("stack[sp - 1] = stack[sp - 2];", "stack[sp - 1] = stack[sp - 1];")],
        ["rpn"],
        None,
        False,
    ),
    (
        "rpn-roll-direction-swapped",
        [
            ('strcmp(tok, "roll") == 0', 'strcmp(tok, "@@") == 0'),
            ('strcmp(tok, "rollu") == 0', 'strcmp(tok, "roll") == 0'),
            ('strcmp(tok, "@@") == 0', 'strcmp(tok, "rollu") == 0'),
        ],
        ["rpn"],
        None,
        False,
    ),
    (
        "rpn-over-pushes-top",
        [("push(stack[sp - 2]);", "push(stack[sp - 1]);")],
        ["rpn"],
        None,
        False,
    ),
    (
        "rpn-underflow-unchecked",
        [
            (
                '\tif (sp < n)\n\t\tfail("stack empty");',
                '\tif (sp < n - n)\n\t\tfail("stack empty");',
            )
        ],
        ["rpn"],
        None,
        True,
    ),
    (
        "rpn-binary-operands-reversed",
        [
            (
                "v = arith(tok[0], stack[sp - 2], stack[sp - 1]);",
                "v = arith(tok[0], stack[sp - 1], stack[sp - 2]);",
            )
        ],
        ["rpn"],
        None,
        False,
    ),
    (
        "rpn-variadic-takes-two",
        [
            (
                "k = f->min == f->max ? f->min : sp;",
                "k = f->min == f->max ? f->min : (sp > 2 ? 2 : sp);",
            )
        ],
        ["rpn"],
        None,
        False,
    ),
    (
        "rpn-stack-lines-ascending",
        [('outf("%d: %s\\n", sp - i, buf);', 'outf("%d: %s\\n", i + 1, buf);')],
        ["rpn"],
        None,
        False,
    ),
    (
        "programmer-shift-count-masked",
        [("v = r >= (word)bits ? 0u : (v << r) & wmask();", "v = (v << (r & 31)) & wmask();")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-right-shift-arithmetic",
        [
            (
                "v = r >= (word)bits ? 0u : v >> r;",
                "v = r >= (word)bits ? (v >> 31 ? 0xffffffffu & wmask() : 0u) "
                ": (word)((int32_t)v >> r) & wmask();",
            )
        ],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-word-mask-always-32",
        [("return bits >= 32 ? 0xffffffffu : (((word)1 << bits) - 1u);", "return 0xffffffffu;")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-sign-test-on-bit-31",
        [("if ((w >> (bits - 1)) & 1u)", "if ((w >> 31) & 1u)")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-and-is-or",
        [("v &= pshift();", "v |= pshift();")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-xor-is-or",
        [("v ^= pand();", "v |= pand();")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-shift-binds-tighter-than-addition",
        [("word v = padd(), r;", "word v = pmul(), r;")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-literal-overflow-unchecked",
        [("if (v > (0xffffffffu - (word)d) / (word)base)", "if (0)")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-literal-wider-than-word",
        [('\tif (v > wmask())\n\t\tfail("value too large for word size");\n', "")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-rotate-direction-swapped",
        [("return protate(a, b, name[2] == 'l');", "return protate(a, b, name[2] == 'r');")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-not-unmasked",
        [("v = ~punary() & wmask();", "v = ~punary();")],
        ["prog"],
        None,
        False,
    ),
    (
        "programmer-division-by-zero-unchecked",
        [
            (
                "\t\t\tif (r == 0)\n\t\t\t\tfail(\"division by zero\");\n\t\t\tv = c == '/'",
                "\t\t\tif (r == 0)\n\t\t\t\tr = 1;\n\t\t\tv = c == '/'",
            )
        ],
        ["prog"],
        None,
        False,
    ),
    (
        "bits-accepts-any-size",
        [('\t\tif (n != 8 && n != 16 && n != 32)\n\t\t\tfail("bits must be 8, 16 or 32");\n', "")],
        ["prog"],
        None,
        False,
    ),
    (
        "conv-offset-applied-after-scale",
        [("base = v * uf->f + uf->off;", "base = (v + uf->off) * uf->f;")],
        ["units"],
        None,
        False,
    ),
    (
        "conv-target-factor-inverted",
        [("v = chk((base - ut->off) / ut->f);", "v = chk((base - ut->off) * ut->f);")],
        ["units"],
        None,
        False,
    ),
    (
        "conv-target-offset-dropped",
        [("v = chk((base - ut->off) / ut->f);", "v = chk(base / ut->f);")],
        ["units"],
        None,
        False,
    ),
    (
        "conv-incompatible-units-allowed",
        [("if (uf->dim != ut->dim)", "if (0)")],
        ["units"],
        None,
        False,
    ),
    (
        "conv-first-unit-not-checked",
        [('\tif (uf == NULL)\n\t\tfailname("unknown unit", from);\n', "")],
        ["units"],
        None,
        True,
    ),
    (
        "stat-variance-is-population",
        [
            (
                'cstatus(calc_variance(a, (size_t)n, 1, &r));\n\t\tprintkv("var", r);',
                'cstatus(calc_variance(a, (size_t)n, 0, &r));\n\t\tprintkv("var", r);',
            )
        ],
        ["stats"],
        None,
        False,
    ),
    (
        "stat-stdev-is-population",
        [
            (
                'cstatus(calc_stddev(a, (size_t)n, 1, &r));\n\t\tprintkv("stdev", r);',
                'cstatus(calc_stddev(a, (size_t)n, 0, &r));\n\t\tprintkv("stdev", r);',
            )
        ],
        ["stats"],
        None,
        False,
    ),
    (
        "stat-median-is-mean",
        [
            (
                'cstatus(calc_median(a, (size_t)n, scratch, &r));\n\tprintkv("median", r);',
                '(void)scratch;\n\tcstatus(calc_mean(a, (size_t)n, &r));\n\tprintkv("median", r);',
            )
        ],
        ["stats"],
        None,
        False,
    ),
    (
        "function-stdev-is-population",
        [
            (
                "cstatus(calc_stddev(a, (size_t)n, 1, &r));\n\t\treturn chk(r);",
                "cstatus(calc_stddev(a, (size_t)n, 0, &r));\n\t\treturn chk(r);",
            )
        ],
        ["alg", "stats"],
        None,
        False,
    ),
    (
        "reg-slope-inverted",
        [('printkv("slope", chk(sxy / sxx));', 'printkv("slope", chk(sxx / sxy));')],
        ["stats"],
        None,
        False,
    ),
    (
        "reg-intercept-sign",
        [("chk(my - sxy / sxx * mx)", "chk(my + sxy / sxx * mx)")],
        ["stats"],
        None,
        False,
    ),
    (
        "reg-length-mismatch-allowed",
        [
            (
                '\tif (n != m)\n\t\tfail("lists differ in length");',
                '\tif (n != m && 0)\n\t\tfail("lists differ in length");',
            )
        ],
        ["stats"],
        None,
        True,
    ),
    (
        "format-trailing-zeros-kept",
        [
            (
                "\twhile (nd > 1 && d[nd - 1] == '0')\n\t\tnd--;",
                "\twhile (nd > 1 && d[nd - 1] == 'x')\n\t\tnd--;",
            )
        ],
        ["digits", "alg"],
        None,
        False,
    ),
    (
        "format-exponent-threshold-low",
        [("if (ex < -4 || ex >= digits) {", "if (ex < -5 || ex >= digits) {")],
        ["digits"],
        None,
        False,
    ),
    (
        "format-exponent-threshold-high",
        [("if (ex < -4 || ex >= digits) {", "if (ex < -4 || ex > digits) {")],
        ["digits"],
        None,
        False,
    ),
    (
        "format-leading-zeros-count-as-digits",
        [
            (
                "\t	for (i = 0; i < -ex - 1; i++)\n\t\t\t*o++ = '0';\n"
                "\t\tfor (i = 0; i < nd; i++)\n\t\t\t*o++ = d[i];",
                "\t\tfor (i = 0; i < -ex - 1; i++)\n\t\t\t*o++ = '0';\n"
                "\t\tfor (i = 0; i < nd - (-ex - 1); i++)\n\t\t\t*o++ = d[i];",
            )
        ],
        ["digits"],
        None,
        False,
    ),
    (
        "help-line-wider-than-53",
        [
            (
                'oline("calc: scientific calculator");',
                'oline("calc: scientific calculator for the PicoCalc console and ANSI terminals");',
            )
        ],
        ["process"],
        None,
        False,
    ),
]


def table_mutants(text):
    """One mutant per unit row and per constant, each scaled by 1 + 1e-6."""
    out = []
    unit_rows = text.split("units[] = {", 1)[1].split("};", 1)[0]
    for m in re.finditer(r'\{ "([^"]+)", (D_\w+), ([^,]+), ([^}]+?) \}', unit_rows):
        row, name, dim, factor = m.group(0), m.group(1), m.group(2), m.group(3)
        new = row.replace(", %s, %s," % (dim, factor), ", %s, (%s) * 1.000001," % (dim, factor), 1)
        out.append(("unit-factor-" + name, [(row, new)], ["units"], None, False))
        offset = m.group(4)
        if offset.strip() != "0":
            new = row[: row.rindex(offset)] + "(%s) * 1.000001 }" % offset.strip()
            out.append(("unit-offset-" + name, [(row, new)], ["units"], None, False))
    konst_rows = text.split("konsts[] = {", 1)[1].split("};", 1)[0]
    for m in re.finditer(r'\{ "([^"]+)",\s+([^}]+?) \},', konst_rows):
        row = m.group(0)
        new = row.replace(m.group(2), "(%s) * 1.000001" % m.group(2), 1)
        out.append(("constant-" + m.group(1), [(row, new)], ["alg"], None, False))
    return out


def build(work, source, exe, san):
    cmd = [CC] + FLAGS + (SAN if san else []) + ["-I" + str(work["lib"]), "-o", exe, source]
    cmd += work["objs"] + ["-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    return r.returncode, r.stderr


PLAY_RUNS = 3
# Mutants that only a pty case with a sub-deadline inter-byte gap can see.
# The macOS runner schedules the program later than the gap often enough
# that every byte arrives in one read, so there a survivor is reported as
# unobserved; Linux hosts, including the Linux CI job, must reject them.
TIMING_MUTANTS = {"split-sequence-not-awaited"}


def run_suite(prog, line, play, san):
    env = dict(os.environ)
    if san:
        env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1"
    here = Path(__file__).resolve().parent
    if line:
        r = subprocess.run(
            [sys.executable, "-I", str(here / "line.py"), prog] + line,
            capture_output=True,
            text=True,
            timeout=600,
            env=env,
            check=False,
        )
        if r.returncode != 0:
            msg = next((x for x in r.stdout.splitlines() if x.startswith("FAIL")), "")
            return "line " + (msg or "exit %d" % r.returncode)[:80]
    if play:
        # A timing mutant shows only on a run where the host schedules calc
        # inside the gap a case leaves, so a play suite gets PLAY_RUNS tries
        # to reject the mutant; the unmutated program passes in one.
        for _ in range(PLAY_RUNS):
            r = subprocess.run(
                [sys.executable, "-I", str(here / "play.py"), prog] + play,
                capture_output=True,
                text=True,
                timeout=600,
                env=env,
                check=False,
            )
            if r.returncode != 0:
                break
        if r.returncode != 0:
            msg = next((x for x in r.stdout.splitlines() if "FAIL" in x), "exit %d" % r.returncode)
            return "play " + msg.split("FAIL in ", 1)[-1][:80]
    return None


def main():
    src = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    text = (src / "calc.c").read_text()
    top = src.parent.parent
    lib = top / "lib" / "libcalc"
    failures = 0
    mutants = MUTANTS + table_mutants(text)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        objs = []
        for f in sorted(lib.glob("calc_*.c")):
            o = tmp / (f.stem + ".o")
            cmd = [CC] + FLAGS + ["-I" + str(lib), "-c", str(f), "-o", str(o)]
            subprocess.run(cmd, check=True)
            objs.append(str(o))
        sobjs = []
        for f in sorted(lib.glob("calc_*.c")):
            o = tmp / (f.stem + ".san.o")
            cmd = [CC] + FLAGS + SAN + ["-I" + str(lib), "-c", str(f), "-o", str(o)]
            subprocess.run(cmd, check=True)
            sobjs.append(str(o))
        work = {"lib": lib, "objs": objs}
        swork = {"lib": lib, "objs": sobjs}
        shutil.copy(src / "calc.c", tmp / "calc.c")
        exe = str(tmp / "calc-mutant")
        rc, err = build(work, str(tmp / "calc.c"), exe, False)
        if rc != 0:
            print("baseline does not build:\n" + err)
            return 1
        here = Path(__file__).resolve().parent
        r = subprocess.run([sys.executable, "-I", str(here / "line.py"), exe], check=False)
        if r.returncode != 0:
            print("baseline: the unmutated line tests do not pass")
            return 1
        r = subprocess.run([sys.executable, "-I", str(here / "play.py"), exe], check=False)
        if r.returncode != 0:
            print("baseline: the unmutated pty tests do not pass")
            return 1
        print("baseline: the unmutated program passes both suites")
        def check(mutant):
            """Build one mutant in its own directory and run its suites;
            returns (verdict, rejected, failed)."""
            name, subs, line, play, san = mutant
            mutated = text
            for old, new in subs:
                if mutated.count(old) != 1:
                    return (
                        "ERROR: anchor occurs %d times: %r" % (mutated.count(old), old[:60]),
                        0,
                        1,
                    )
                mutated = mutated.replace(old, new)
            own = Path(tempfile.mkdtemp(dir=tmp))
            (own / "calc.c").write_text(mutated)
            mexe = str(own / "calc-mutant")
            rc, err = build(swork if san else work, str(own / "calc.c"), mexe, san)
            if rc != 0:
                return "ERROR: does not build\n%s" % err[:400], 0, 1
            how = run_suite(mexe, line, play, san)
            if how is None and name in TIMING_MUTANTS and sys.platform == "darwin":
                return "UNOBSERVED: timing mutant on a macOS host", 1, 0
            if how is None:
                return "SURVIVED", 0, 1
            return "rejected: %s" % how, 1, 0

        tried = rejected = 0
        for mutant, (verdict, ok, failed) in mutant_pool.ordered(
            mutants, check, serial=TIMING_MUTANTS
        ):
            tried += 1
            rejected += ok
            failures += failed
            print("mutant %-48s %s" % (mutant[0], verdict))
    print("mutants: %d tried, %d rejected, %d failures" % (tried, rejected, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
