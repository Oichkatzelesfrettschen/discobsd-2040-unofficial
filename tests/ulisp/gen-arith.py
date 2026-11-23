#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Writes the integer-edge corpus for the uLisp interpreter to standard output.

usage: gen-arith.py [BUILTINS.DEF]

Every builtin that takes an integer, or a number that becomes one, is
applied to the edge values of a 32-bit integer: all of them for one and
two arguments, and a fixed pseudo-random sample for three or more. Each form
prints itself, then its value or its error, so a transcript line names the
call it answers. The pass criterion is that the interpreter built with the
address, undefined-behavior and float-cast sanitizers neither reports nor
dies on any line.

Given the builtin table, the script also fails when a builtin is neither
covered here nor listed in NO_NUMBER_ARGUMENT, so a builtin added later must
be classified before the gate passes.
"""

import re
import sys

INT_MIN = -2147483648
INT_MAX = 2147483647
EDGES = [INT_MIN, INT_MIN + 1, -1, 0, 1, 31, 32, 33, INT_MAX - 1, INT_MAX]
# Large positive values wait the milliseconds they name, so the timing
# builtins take the edges that return promptly.
SHORT = [v for v in EDGES if v < 100]
# sleep counts seconds, so only the counts that do not wait
NOWAIT = [v for v in EDGES if v <= 0]
# Floats that sit at or past the integer range, and the halves that round.
FLOATS = [
    "2147483648.0",
    "-2147483904.0",
    "1.0e10",
    "-1.0e10",
    "3.4e38",
    "-3.4e38",
    "0.5",
    "-0.5",
    "1.5",
    "-1.5",
]
FEW_FLOATS = ["2147483648.0", "-1.0e10", "3.4e38", "0.5"]
SAMPLE = 120
LIST = "'(1 2 3)"
CODES = "(list " + " ".join("(char-code (char s %d))" % i for i in range(6)) + ")"
STRING = '"abcdefghijklmnopqrstuvwxyz0123456789"'

# name, arguments of the form, and which values each may take
UNARY = [
    "1+",
    "1-",
    "abs",
    "plusp",
    "minusp",
    "zerop",
    "oddp",
    "evenp",
    "integerp",
    "numberp",
    "float",
    "-",
    "/",
    "lognot",
    "string",
    "princ-to-string",
    "prin1-to-string",
    "code-char",
    "char-code",
    "not",
    "ceiling",
    "floor",
    "truncate",
    "round",
    "digitalread",
    "analogread",
    "set-text-size",
    "set-text-wrap",
    "fill-screen",
    "set-text-color",
    "length",
    "reverse",
    "copy-list",
    "eval",
    "list",
]
# Transcendental functions: the value is libm's, so the transcript keeps its type
FLOATFNS = [
    "sin",
    "cos",
    "tan",
    "asin",
    "acos",
    "atan",
    "sinh",
    "cosh",
    "tanh",
    "exp",
    "sqrt",
    "log",
]
# Variadic arithmetic and comparison, applied to every pair and a sample of triples
VARIADIC = [
    "+",
    "-",
    "*",
    "/",
    "max",
    "min",
    "/=",
    "=",
    "<",
    "<=",
    ">",
    ">=",
    "logand",
    "logior",
    "logxor",
]
BINARY = [
    "mod",
    "rem",
    "ash",
    "logbitp",
    "expt",
    "eq",
    "equal",
    "atan",
    "log",
    "ceiling",
    "floor",
    "truncate",
    "round",
]
# Templates over {a}, {b}, ...; mode "all" crosses the edges, "sample" samples them
CUSTOM = [
    ("nth", "(nth {a} " + LIST + ")", 1, "all", EDGES),
    ("nth", "(nth 1 {a})", 1, "all", EDGES),
    ("aref", "(aref (make-array 3) {a})", 1, "all", EDGES),
    ("aref", "(aref (make-array '(3 3)) {a} {b})", 2, "all", EDGES),
    ("aref", "(aref (make-array 70 :element-type 'bit) {a})", 1, "all", EDGES),
    ("char", "(char " + STRING + " {a})", 1, "all", EDGES),
    ("subseq", "(subseq " + STRING + " {a})", 1, "all", EDGES),
    ("subseq", "(subseq " + STRING + " {a} {b})", 2, "all", EDGES),
    ("subseq", "(subseq " + LIST + " {a} {b})", 2, "all", EDGES),
    ("subseq", "(subseq (make-array 5) {a} {b})", 2, "all", EDGES),
    ("make-array", "(array-dimensions (make-array {a}))", 1, "all", EDGES),
    ("make-array", "(array-dimensions (make-array (list {a} {b})))", 2, "all", EDGES),
    ("make-array", "(array-dimensions (make-array (list {a} {b} {c})))", 3, "sample", EDGES),
    ("make-array", "(aref (make-array {a} :initial-element {b}) 0)", 2, "all", EDGES),
    (
        "make-array",
        "(aref (make-array {a} :element-type 'bit :initial-element {b}) 0)",
        2,
        "all",
        EDGES,
    ),
    ("array-dimensions", "(array-dimensions (make-array (list {a} {b})))", 2, "all", EDGES),
    ("setf", "(let ((s (copy-list '(1 2 3)))) (setf (nth {a} s) {b}) s)", 2, "all", EDGES),
    ("setf", "(let ((s (make-array 5))) (setf (aref s {a}) {b}) s)", 2, "all", EDGES),
    ("setf", "(let ((s (make-array '(2 2)))) (setf (aref s {a} {b}) 1) s)", 2, "all", EDGES),
    (
        "setf",
        "(let ((s (make-array 70 :element-type 'bit))) (setf (aref s {a}) {b}) s)",
        2,
        "all",
        EDGES,
    ),
    (
        "setf",
        '(let ((s (concatenate \'string "abcdef"))) (setf (char s {a}) (code-char {b})) '
        + CODES
        + ")",
        2,
        "all",
        EDGES,
    ),
    (
        "setf",
        '(let ((s (concatenate \'string "abcdef"))) (setf (char s 2) (code-char {a})) '
        + CODES
        + ")",
        1,
        "all",
        EDGES,
    ),
    ("random", "(let ((r (random {a}))) (and (>= r 0) (< r {a})))", 1, "all", EDGES),
    ("random", "(floatp (random {a}))", 1, "floats", FLOATS),
    ("incf", "(let ((v {a})) (incf v) v)", 1, "all", EDGES),
    ("decf", "(let ((v {a})) (decf v) v)", 1, "all", EDGES),
    ("incf", "(let ((v {a})) (incf v {b}) v)", 2, "all", EDGES),
    ("decf", "(let ((v {a})) (decf v {b}) v)", 2, "all", EDGES),
    ("incf", "(let ((v {a})) (incf v {b}) v)", 2, "floats", FEW_FLOATS),
    ("dotimes", "(dotimes (i {a}) (return i))", 1, "all", EDGES),
    ("dotimes", "(dotimes (i {a} i) (return 7))", 1, "all", EDGES),
    (
        "case",
        "(case {a} (1 'one) (2147483647 'max) (-2147483648 'min) (t 'other))",
        1,
        "all",
        EDGES,
    ),
    ("pinmode", "(pinmode {a} {b})", 2, "all", EDGES),
    ("digitalwrite", "(digitalwrite {a} {b})", 2, "all", EDGES),
    ("digitalread", "(digitalread {a})", 1, "all", EDGES),
    ("analogread", "(analogread {a})", 1, "all", EDGES),
    ("analogwrite", "(analogwrite {a} {b})", 2, "all", EDGES),
    ("note", "(note {a})", 1, "all", EDGES),
    ("note", "(note {a} {b})", 2, "all", EDGES),
    ("note", "(note {a} {b} {c})", 3, "sample", EDGES),
    ("delay", "(delay {a})", 1, "all", SHORT),
    ("sleep", "(sleep {a})", 1, "all", NOWAIT),
    (
        "for-millis",
        "(let ((r (for-millis ({a}) 1)) (m (if (< {a} 0) 0 {a}))) (and (>= r m) (< r (+ 1500 m))))",
        1,
        "all",
        SHORT,
    ),
    (
        "write-byte",
        "(char-code (char (with-output-to-string (s) (write-byte {a} s)) 0))",
        1,
        "all",
        EDGES,
    ),
    ("read-byte", "(read-byte {a})", 1, "all", EDGES),
    ("read-line", "(read-line {a})", 1, "all", EDGES),
    ("read", "(read {a})", 1, "all", EDGES),
    ("terpri", "(terpri {a})", 1, "all", EDGES),
    ("prin1", "(prin1 1 {a})", 1, "all", EDGES),
    ("print", "(print 1 {a})", 1, "all", EDGES),
    ("princ", "(princ 1 {a})", 1, "all", EDGES),
    ("write-string", '(write-string "x" {a})', 1, "all", EDGES),
    ("write-line", '(write-line "x" {a})', 1, "all", EDGES),
    ("backtrace", "(backtrace {a})", 1, "all", EDGES),
    ("pprint", "(pprint {a} {b})", 2, "all", EDGES),
    ("set-cursor", "(set-cursor {a} {b})", 2, "all", EDGES),
    ("set-text-color", "(set-text-color {a} {b})", 2, "all", EDGES),
    ("draw-pixel", "(draw-pixel {a} {b})", 2, "all", EDGES),
    ("draw-pixel", "(draw-pixel {a} {b} {c})", 3, "sample", EDGES),
    ("read-pixel", "(read-pixel {a} {b})", 2, "all", EDGES),
    ("draw-line", "(draw-line {a} {b} {c} {d})", 4, "sample", EDGES),
    ("draw-line", "(draw-line {a} {b} {c} {d} {e})", 5, "sample", EDGES),
    ("draw-rect", "(draw-rect {a} {b} {c} {d})", 4, "sample", EDGES),
    ("fill-rect", "(fill-rect {a} {b} {c} {d})", 4, "sample", EDGES),
    ("fill-rect", "(fill-rect {a} {b} {c} {d} {e})", 5, "sample", EDGES),
    ("draw-circle", "(draw-circle {a} {b} {c})", 3, "sample", EDGES),
    ("fill-circle", "(fill-circle {a} {b} {c})", 3, "sample", EDGES),
    ("draw-circle", "(draw-circle {a} {b} {c} {d})", 4, "sample", EDGES),
    ("draw-round-rect", "(draw-round-rect {a} {b} {c} {d} {e})", 5, "sample", EDGES),
    ("fill-round-rect", "(fill-round-rect {a} {b} {c} {d} {e})", 5, "sample", EDGES),
    ("draw-triangle", "(draw-triangle {a} {b} {c} {d} {e} {f})", 6, "sample", EDGES),
    ("fill-triangle", "(fill-triangle {a} {b} {c} {d} {e} {f})", 6, "sample", EDGES),
    ("draw-char", "(draw-char {a} {b} #\\A)", 2, "all", EDGES),
    ("draw-char", "(draw-char {a} {b} {c} {d} {e})", 5, "sample", EDGES),
    ("draw-char", "(draw-char 1 1 #\\A 1 {a})", 1, "all", EDGES),
    ("with-gfx", "(with-gfx (s) (draw-pixel {a} {b}))", 2, "all", EDGES),
    ("format", '(format nil "~{a}d" 5)', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a}a" 5)', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a}s" "x")', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a}x" 255)', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a}b" 5)', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a}o" 8)', 1, "nonneg", EDGES),
    ("format", '(format nil "~d" {a})', 1, "all", EDGES),
    ("format", '(format nil "~a" {a})', 1, "all", EDGES),
    ("format", '(format nil "~s" {a})', 1, "all", EDGES),
    ("format", '(format nil "~x" {a})', 1, "all", EDGES),
    ("format", '(format nil "~o" {a})', 1, "all", EDGES),
    ("format", '(format nil "~b" {a})', 1, "all", EDGES),
    ("format", '(format nil "~c" {a})', 1, "all", EDGES),
    ("format", '(format nil "~10,{a}d" 5)', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a},{b}d" 5)', 2, "nonneg", EDGES),
    ("format", '(format nil "~~{a}%")', 1, "nonneg", EDGES),
    ("format", '(format nil "~{a}&")', 1, "nonneg", EDGES),
    ("format", '(format nil "~d~d" {a} {b})', 2, "all", EDGES),
]
FLOATFORMS = [
    ("float", "(float {a})", 1),
    ("-", "(- {a})", 1),
    ("abs", "(abs {a})", 1),
    ("1+", "(1+ {a})", 1),
    ("1-", "(1- {a})", 1),
    ("integerp", "(integerp {a})", 1),
    ("zerop", "(zerop {a})", 1),
    ("plusp", "(plusp {a})", 1),
    ("minusp", "(minusp {a})", 1),
    ("oddp", "(oddp {a})", 1),
    ("evenp", "(evenp {a})", 1),
    ("lognot", "(lognot {a})", 1),
    ("char", "(char " + STRING + " {a})", 1),
    ("nth", "(nth {a} " + LIST + ")", 1),
    ("ash", "(ash {a} 1)", 1),
    ("ash", "(ash 1 {a})", 1),
    ("code-char", "(code-char {a})", 1),
    ("make-array", "(array-dimensions (make-array {a}))", 1),
    ("subseq", "(subseq " + STRING + " {a})", 1),
    ("dotimes", "(dotimes (i {a}) (return i))", 1),
    ("princ-to-string", "(princ-to-string {a})", 1),
    ("string", "(string {a})", 1),
    ("expt", "(expt {a} 2)", 1),
    ("expt", "(expt 2 {a})", 1),
    ("expt", "(expt {a} {a})", 1),
    ("ceiling", "(ceiling {a})", 1),
    ("floor", "(floor {a})", 1),
    ("truncate", "(truncate {a})", 1),
    ("round", "(round {a})", 1),
    ("ceiling", "(ceiling {a} 1)", 1),
    ("floor", "(floor {a} 1)", 1),
    ("truncate", "(truncate {a} 1)", 1),
    ("round", "(round {a} 1)", 1),
    ("ceiling", "(ceiling 1 {a})", 1),
    ("floor", "(floor 1 {a})", 1),
    ("truncate", "(truncate 1 {a})", 1),
    ("round", "(round 1 {a})", 1),
    ("max", "(max {a} 1)", 1),
    ("min", "(min {a} 1)", 1),
    ("mod", "(mod {a} 7)", 1),
    ("rem", "(rem {a} 7)", 1),
    ("mod", "(mod 7 {a})", 1),
    ("rem", "(rem 7 {a})", 1),
]
# Builtins the corpus leaves out: those that take zero numbers, directly or
# through a conversion, and register, which reads or writes the address it
# is given (a hardware register on the board, any address on the host), and
# gc, whose argument is a flag and whose report carries a time
NO_NUMBER_ARGUMENT = {
    "quote",
    "defun",
    "defvar",
    "defcode",
    "car",
    "first",
    "cdr",
    "rest",
    "setq",
    "or",
    "loop",
    "push",
    "pop",
    "setf",
    "dolist",
    "do",
    "do*",
    "trace",
    "untrace",
    "time",
    "with-output-to-string",
    "with-sd-card",
    "progn",
    "if",
    "cond",
    "when",
    "unless",
    "and",
    "null",
    "cons",
    "atom",
    "listp",
    "consp",
    "symbolp",
    "arrayp",
    "boundp",
    "keywordp",
    "set",
    "streamp",
    "caar",
    "cadr",
    "second",
    "cdar",
    "cddr",
    "caaar",
    "caadr",
    "cadar",
    "caddr",
    "third",
    "cdaar",
    "cdadr",
    "cddar",
    "cdddr",
    "assoc",
    "member",
    "apply",
    "funcall",
    "append",
    "mapc",
    "mapl",
    "mapcar",
    "mapcan",
    "maplist",
    "mapcon",
    "floatp",
    "characterp",
    "stringp",
    "string=",
    "string<",
    "string>",
    "string/=",
    "string<=",
    "string>=",
    "sort",
    "concatenate",
    "search",
    "read-from-string",
    "return",
    "globals",
    "locals",
    "makunbound",
    "break",
    "pprintall",
    "require",
    "list-library",
    "?",
    "documentation",
    "apropos",
    "apropos-list",
    "unwind-protect",
    "ignore-errors",
    "error",
    "directory",
    "get-key",
    "cls",
    "millis",
    "room",
    "save-image",
    "load-image",
    "edit",
    "save-bmp",
    "register",
    "gc",
    "bit",
    "closure",
    "lambda",
    "let",
    "let*",
}


def lcg(seed):
    state = seed
    while True:
        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        yield state >> 33


def tuples(count, values, mode, seed):
    if mode == "all" or mode == "floats" or mode == "nonneg" or (count <= 2 and mode != "sample"):
        pool = [v for v in values if not (mode == "nonneg" and str(v).startswith("-"))]
        out = [()]
        for _ in range(count):
            out = [t + (v,) for t in out for v in pool]
        return out
    out = []
    gen = lcg(seed)
    seen = set()
    while len(out) < SAMPLE:
        t = tuple(values[next(gen) % len(values)] for _ in range(count))
        if t not in seen:
            seen.add(t)
            out.append(t)
    return out


SEEN = set()


def line(form):
    if form not in SEEN:
        SEEN.add(form)
        print('(progn (princ \'%s) (princ " => ") (prin1 %s) (terpri) nothing)' % (form, form))


def emit(covered, name, template, count, values, mode, seed, wrap=None):
    covered.add(name)
    names = "abcdef"
    for t in tuples(count, values, mode, seed):
        form = template.format(**{names[i]: str(v) for i, v in enumerate(t)})
        if wrap:
            form = wrap % form
        line(form)


def main():
    covered = set()
    seed = 1
    for name in UNARY:
        emit(covered, name, "(%s {a})" % name, 1, EDGES, "all", seed)
        emit(covered, name, "(%s {a})" % name, 1, FLOATS, "all", seed)
    for name in FLOATFNS:
        emit(covered, name, "(%s {a})" % name, 1, EDGES + FLOATS, "all", seed, "(floatp %s)")
    for name in VARIADIC:
        for count in (2, 3):
            seed += 1
            emit(
                covered,
                name,
                "(%s %s)" % (name, " ".join("{%s}" % "abc"[i] for i in range(count))),
                count,
                EDGES,
                "all" if count == 2 else "sample",
                seed,
            )
        seed += 1
        emit(
            covered,
            name,
            "(%s {a} {b})" % name,
            2,
            FEW_FLOATS + [str(v) for v in (INT_MIN, -1, 0, 1, INT_MAX)],
            "all",
            seed,
        )
        emit(covered, name, "(%s {a})" % name, 1, EDGES, "all", seed)
    for name in BINARY:
        seed += 1
        emit(covered, name, "(%s {a} {b})" % name, 2, EDGES, "all", seed)
        emit(
            covered,
            name,
            "(%s {a} {b})" % name,
            2,
            FEW_FLOATS + [str(v) for v in (INT_MIN, -1, 0, 1, INT_MAX)],
            "all",
            seed,
        )
    for name, template, count, mode, values in CUSTOM:
        seed += 1
        emit(covered, name, template, count, values, mode, seed)
    for name, template, count in FLOATFORMS:
        seed += 1
        emit(covered, name, template, count, FLOATS, "all", seed)
    read_forms(covered)
    print_forms()
    check_table(covered)


def read_forms(covered):
    """Number text at and past the integer range, in every radix the reader takes."""
    covered.add("read-from-string")
    texts = []
    for v in EDGES + [INT_MAX + 1, INT_MIN - 1, 1 << 32, (1 << 32) - 1, 1 << 63, 1 << 64, 10**20]:
        texts.append(str(v))
        texts.append("+" + str(abs(v)))
    for radix, digits in (("x", "x"), ("b", "b"), ("o", "o")):
        for v in (0, 1, 7, INT_MAX, INT_MAX + 1, (1 << 32) - 1, 1 << 32, 1 << 40):
            texts.append("#" + radix + format(v, digits))
            texts.append("#" + radix + "-" + format(v, digits))
    texts += [
        "#x",
        "#b",
        "#o",
        "#xG",
        "#b2",
        "#o8",
        "-",
        "+",
        "1e",
        "1e+",
        "1e999",
        "-1e999",
        "1e-999",
        "1.",
        ".5",
        "-.5",
        "1.5e10",
        "3.4e38",
        "3.5e38",
        "1e38",
        "1e39",
        "0.0000000000000000000000000000000000000000000001",
        "123456789012345678901234567890",
        "2147483647.5",
        "-2147483648.5",
        "0x10",
        "1/2",
        "--1",
        "1-1",
        "1+1",
    ]
    for text in texts:
        line('(read-from-string "%s")' % text)
        line('(1+ (read-from-string "%s"))' % text)


def print_forms():
    """Floats that print at the limits of the format: the extremes, the
    smallest and the values no float holds."""
    for value in [
        "3.4e38",
        "-3.4e38",
        "1.0e38",
        "1.0e-38",
        "1.17549e-38",
        "1.0e-45",
        "16777216.0",
        "16777217.0",
        "0.1",
        "123456.7",
        "9.99999e-5",
        "(* 3.4e38 10)",
        "(- (* 3.4e38 10))",
        "(sqrt -1)",
        "(/ 1.0e-38 1.0e38)",
        "(float 2147483647)",
        "(float -2147483648)",
        "(- (float 2147483647))",
        "(* 1.0e19 1.0e19)",
        "(float 16777217)",
    ]:
        for call in ("prin1-to-string", "princ-to-string", "string"):
            line("(%s %s)" % (call, value))
        line('(format nil "~a ~d" %s %s)' % (value, value))
    for a in EDGES:
        line("(code-char %d)" % a)
        line("(char-code (code-char %d))" % a)
        line("(length (string (code-char %d)))" % a)
        line('(length (concatenate \'string (string (code-char %d)) "x"))' % a)
        line('(char-code (char (concatenate \'string "ab" (string (code-char %d)) "c") 2))' % a)
        line("(prin1-to-string (code-char %d))" % a)


def check_table(covered):
    if len(sys.argv) < 2:
        return
    names = set()
    with open(sys.argv[1]) as table:
        for line in table:
            m = re.match(r'[NB]\((?:\w+, )?"([^"]+)"', line)
            if m and not m.group(1).startswith((":", "&", "*")):
                names.add(m.group(1))
    names -= {"nil", "t", "nothing"}
    missing = sorted(names - covered - NO_NUMBER_ARGUMENT)
    if missing:
        sys.stderr.write("gen-arith: not classified: %s\n" % " ".join(missing))
        sys.exit(1)


main()
