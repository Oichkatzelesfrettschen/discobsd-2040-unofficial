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

"""Writes the wrong-type corpus for the uLisp interpreter to standard output.

usage: gen-types.py BUILTINS.DEF

Every builtin in the table is applied with one argument position at a time
replaced by each kind of object a caller can pass: nil, t, a symbol, the
symbol of a builtin, a keyword, a string, the empty string, a character, a
float, a proper list, a dotted pair, a list whose first element is nil, an
array of one and of two dimensions, an output string stream, a graphics
stream and a lambda form. The other positions hold a small integer or a
list. Two-argument builtins also take every pair of kinds, and three-argument
builtins a fixed pseudo-random sample of triples. Each form prints itself,
then its value or its error, so a transcript line names the call it answers.

The pass criterion is that the interpreter built with the address and
undefined-behavior sanitizers neither reports nor dies on any line, and that
no line reports a memory fault, which is the interpreter's own handler for a
wild pointer (check.sh holds the second half). The script fails when a
builtin of the table is neither swept nor listed in EXCLUDED, so a builtin
added later must be classified before the gate passes.
"""

import re
import sys

# A kind is a form that evaluates to one object of that kind. The stream
# kinds name a variable that the wrapper in line() binds.
KINDS = [
    "nil",
    "t",
    "'foo",
    "'car",
    ":low",
    '"abc"',
    '""',
    "#\\a",
    "1.5",
    "'(1 2 3)",
    "'(1 . 2)",
    "'(nil 2)",
    "(make-array 3)",
    "(make-array '(2 2))",
    "s",
    "g",
    "(lambda (x) x)",
]
# The kinds crossed with each other for two arguments; the list filler serves one and two arguments
PAIRKINDS = ["nil", "'foo", '"abc"', "1.5", "'(1 2 3)", "'(nil 2)", "s", "(lambda (x) x)"]
FILLERS = ["1", "'(1 2 3)"]
# The widest arity applied to a builtin that takes any number of arguments.
WIDEST = 4
TRIPLES = 40

# Builtins the sweep does not apply, each with the reason.
EXCLUDED = {
    "loop": "never returns without a return form",
    "return": "at top level it leaves the return flag set, which short-circuits every later form",
    "do": "a test form that stays false never ends the loop",
    "do*": "a test form that stays false never ends the loop",
    "mapcon": "splices the results destructively, so the identity function makes the list circular",
    "edit": "reads the editor's keys from standard input",
    "break": "stops for the console",
    "save-image": "rewrites the image file; the images and race corpora hold the loader and saver",
    "load-image": "replaces the workspace; the images and race corpora hold it",
    "register": "reads and writes a hardware address; the host build maps none",
    "defcode": "assembles machine code for the target; the code corpus runs it under qemu",
    "room": "prints the free cells, which move with every change to the interpreter",
    "gc": "prints the free cells, which move with every change to the interpreter",
    "millis": "prints the clock",
    "random": "prints a pseudo-random value that differs between runs",
    "directory": "lists the work directory, whose file order differs between runs",
    "time": "prints the elapsed time",
    "globals": "prints the user globals, which the sweep itself defines",
    "locals": "prints the local variables of the caller",
    "pprintall": "prints the user globals, which the sweep itself defines",
    "get-key": "waits for the console keyboard",
}


def entries(path):
    """The (name, minimum, maximum) of each builtin that takes arguments."""
    out = []
    with open(path) as table:
        for text in table:
            m = re.match(r'[NB]\((?:\w+, )?"([^"]+)", (\w+), 0(\d)(\d)(\d)', text)
            if not m or m.group(1).startswith((":", "&", "*")):
                continue
            name, function, kind, low, high = m.groups()
            if function == "NULL" and kind == "0" and low == "0" and high == "0":
                continue
            out.append((name, int(low), int(high)))
    return out


def lcg(seed):
    state = seed
    while True:
        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        yield state >> 33


SEEN = set()


def line(form, *kinds):
    """One transcript line. A form that uses a stream kind is wrapped in the
    construct that binds the stream: a string stream collects the printed
    value, a graphics stream draws it."""
    if form in SEEN:
        return
    SEEN.add(form)
    show = "(prin1 %s)" % form
    if "s" in kinds:
        show = "(prin1 (with-output-to-string (s) (prin1 %s s)))" % form
    elif "g" in kinds:
        show = "(with-gfx (g) (prin1 %s))" % form
    print('(progn (princ \'%s) (princ " => ") %s (terpri) nothing)' % (form, show))


def call(name, args):
    return "(%s%s)" % (name, "".join(" " + a for a in args))


def sweep(name, low, high):
    top = min(high, max(low, WIDEST)) if high == 7 else high
    arities = sorted(set([low] + list(range(max(low, 1), top + 1))))
    gen = lcg(len(name) * 7919 + sum(map(ord, name)))
    for n in arities:
        if n == 0:
            line(call(name, []))
            continue
        for filler in FILLERS[: 2 if n <= 2 else 1]:
            for pos in range(n):
                for kind in KINDS:
                    args = [filler] * n
                    args[pos] = kind
                    line(call(name, args), *args)
        if n == 2:
            for a in PAIRKINDS:
                for b in PAIRKINDS:
                    line(call(name, [a, b]), a, b)
        elif n == 3:
            for _ in range(TRIPLES):
                args = [KINDS[next(gen) % len(KINDS)] for _ in range(3)]
                line(call(name, args), *args)
    # One argument beyond the most the builtin takes
    if high < 7:
        line(call(name, ["1"] * (high + 1)))


def extras():
    """Forms whose operand is not an argument position."""
    for kind in KINDS:
        if kind in ("s", "g"):
            continue
        # documentation and help read a variable's value for a lambda form
        line("(let ((x %s)) (documentation 'x))" % kind)
        line("(let ((x %s)) (? x))" % kind)
        line("(progn (defvar y) (setq y %s) (documentation 'y))" % kind)
    for reader in ("read", "read-line", "read-byte", "read-from-string"):
        for kind in ("s", "g"):
            line("(%s %s)" % (reader, kind), kind)
    line("(let ((x '(nil))) (documentation 'x))")


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: gen-types.py BUILTINS.DEF\n")
        sys.exit(2)
    names = {name for name, _, _ in entries(sys.argv[1])}
    stale = sorted(set(EXCLUDED) - names)
    if stale:
        sys.stderr.write("gen-types: excluded but not in the table: %s\n" % " ".join(stale))
        sys.exit(1)
    for name, low, high in entries(sys.argv[1]):
        if name not in EXCLUDED:
            sweep(name, low, high)
    extras()


main()
