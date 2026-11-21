#!/usr/bin/env python3
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

"""Reject an allocation result that a C file uses without a NULL test.

An assignment from malloc, calloc, realloc or strdup, through any casts or
parentheses, must be followed by an if statement whose whole condition is
true exactly when one of the assigned objects is NULL (`p == NULL`,
`p == 0`, `!p`), or must sit inside such a condition itself, as in
`if ((p = malloc(n)) == NULL)`. Nothing may follow the call in its own
statement, and the branch that condition controls must leave before any
statement in it names an assigned object: a statement the branch always
runs (the controlled statement, or one at the top level of the controlled
block, outside any nested condition or loop) must be exit, _exit, abort,
longjmp, err, errx, return, goto or awk's error(FATAL, ...), or error() in
ed or overflo() in awk's b.c, which never return. Assigning a new value to
an object is not a use, and a preprocessor line between the allocation
and the end of its branch fails the site, since the scan does not
evaluate conditional compilation. Comments, literals and preprocessor lines are
blanked first and statements split at a semicolon, brace or the end of a
condition, so a call spread over several lines counts as one statement. Every
allocator call in a scanned file must be such an assignment: a call in any
other form (an argument, a return value, a parenthesized lvalue) fails as
unread, so the scan fails closed on syntax it does not model.

usage: check_alloc_results.py FILE...
       check_alloc_results.py --selftest
"""

import re
import sys
from pathlib import Path

ALLOCATORS = r"(?:malloc|calloc|realloc|strdup)"
LVALUE = (
    r"(?:\*\s*)?[A-Za-z_]\w*"
    r"(?:\s*(?:->|\.)\s*[A-Za-z_]\w*|\s*\[[^\]]*\])*"
)
CAST = r"(?:\(\s*[A-Za-z_][\w\s*]*\)\s*|\(\s*)*"
ASSIGNED_CALL = re.compile(rf"((?:{LVALUE}\s*=(?!=)\s*{CAST})+)({ALLOCATORS})\s*\(")
LABELS = r"(?:(?:case\s+[^:;]+|[A-Za-z_]\w*)\s*:(?!:)\s*)*"
ONE_LVALUE = re.compile(rf"({LVALUE})\s*=(?!=)")
# Statements that leave the failure branch. ed's error() and awk's overflo()
# never return (ed longjmps to its command loop, overflo() calls awk's
# error(FATAL)), so they count in those programs alone.
LEAVES = r"\b(?:exit|_exit|abort|longjmp|err|errx)\s*\(|\breturn\b|\bgoto\b|\berror\s*\(\s*FATAL\b"
PROGRAM_LEAVES = {
    "bin/ed/ed.c": r"\berror\s*\(",
    "usr.bin/awk/b.c": r"\boverflo\s*\(",
}
NULL_WORD = r"(?:NULL|0|\(\s*[A-Za-z_][\w\s]*\*\s*\)\s*0)"

# Statements whose result reaches the caller unchanged. Each entry names the
# file and the statement with its whitespace collapsed; an entry that matches
# nothing fails the check, so a stale exemption cannot linger.
EXEMPT = {
    (
        "usr.bin/awk/tran.c",
        "p = malloc(u)",
    ): "ymalloc, a debugging wrapper compiled only under #ifdef malloc, "
    "returns malloc's result to its caller",
}


def blank(text):
    """Return text with comments and string and character literals blanked."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + re.sub(r"[^\n]", " ", text[i + 1 : j - 1]) + c)
            i = j
        elif c == "#" and (i == 0 or text[i - 1] == "\n"):
            end = i
            while True:
                end = text.find("\n", end)
                if end < 0:
                    end = n
                    break
                if text[end - 1] != "\\":
                    break
                end += 1
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        else:
            out.append(c)
            i += 1
    return "".join(out)


def statements(code):
    """Yield (start, text) for each statement and control header."""
    start = 0
    depth = 0
    i = 0
    header = None
    while i < len(code):
        c = code[i]
        if c == "(":
            if depth == 0 and header is None:
                word = re.search(r"\b(if|while|for|switch)\s*$", code[start:i])
                if word and re.fullmatch(r"\s*" + LABELS, code[start : start + word.start()]):
                    header = True
            depth += 1
        elif c == ")":
            depth = max(depth - 1, 0)
            if depth == 0 and header:
                yield start, code[start : i + 1]
                start = i + 1
                header = None
        elif depth == 0 and c in ";{}":
            if code[start:i].strip():
                yield start, code[start : i + 1]
            start = i + 1
            header = None
        i += 1


def condition(text):
    """Return the parenthesized condition of an if or while header, or None."""
    match = re.match(rf"\s*{LABELS}(?:if|while)\s*\(", text)
    if not match:
        return None
    rest = text[match.end() - 1 :]
    depth = 0
    for i, c in enumerate(rest):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return rest[: i + 1]
    return rest


def tests_null(cond, names):
    """True when cond is true exactly when one of the names holds NULL."""
    for name in names:
        lv = r"\s*".join(re.escape(part) for part in re.findall(r"->|\.|\w+|\S", name))
        if re.fullmatch(
            rf"\(\s*(?:{lv}\s*==\s*{NULL_WORD}|{NULL_WORD}\s*==\s*{lv}|!\s*{lv})\s*\)",
            cond.strip(),
        ):
            return True
    return False


def enclosed_test(stmt, chain_start):
    """True when the condition is the parenthesized assignment tested for NULL."""
    cond = condition(stmt)
    if cond is None:
        return False
    depth = 0
    i = chain_start - 1
    while i >= 0:
        if stmt[i] == ")":
            depth += 1
        elif stmt[i] == "(":
            if depth == 0:
                break
            depth -= 1
        i -= 1
    if i < 0:
        return False
    depth = 0
    for j in range(i, len(stmt)):
        if stmt[j] == "(":
            depth += 1
        elif stmt[j] == ")":
            depth -= 1
            if depth == 0:
                group = re.escape(re.sub(r"\s+", " ", stmt[i : j + 1]))
                whole = re.sub(r"\s+", " ", cond.strip())
                return bool(
                    re.fullmatch(
                        rf"\(\s*(?:{group}\s*==\s*{NULL_WORD}|!\s*{group})\s*\)",
                        whole,
                    )
                )
    return False


def branch_span(code, position):
    """Return (start, end) of the statement or block that starts at position."""
    text = branch(code, position)
    start = position
    while start < len(code) and code[start].isspace():
        start += 1
    return start, start + len(text)


def conditional(text, span):
    """True when a preprocessor line lies inside the span of the original text."""
    start, end = span
    return re.search(r"(?m)^[ \t]*#", text[start:end]) is not None


def branch(code, position):
    """Return the statement or block that starts at position."""
    while position < len(code) and code[position].isspace():
        position += 1
    depth = 0
    for i in range(position, len(code)):
        c = code[i]
        if c in "({":
            depth += 1
        elif c in ")}":
            depth -= 1
            if depth == 0 and c == "}" and code[position] == "{":
                return code[position : i + 1]
        elif c == ";" and depth == 0 and code[position] != "{":
            return code[position : i + 1]
    return code[position:]


def top_level(body):
    """Return the statements of a branch that run whenever the branch runs."""
    body = body.strip()
    if not body.startswith("{"):
        return [body]
    inner = body[1:-1]
    statements_found = []
    depth = 0
    start = 0
    for i, c in enumerate(inner):
        if c in "({":
            depth += 1
        elif c in ")}":
            depth -= 1
            if depth == 0 and c == "}":
                statements_found.append(inner[start : i + 1])
                start = i + 1
        elif c == ";" and depth == 0:
            statements_found.append(inner[start : i + 1])
            start = i + 1
    return [s.strip() for s in statements_found if s.strip()]


def lvalue_pattern(name):
    return r"\s*".join(re.escape(part) for part in re.findall(r"->|\.|\w+|\S", name))


def mentions(text, names):
    """True when text reads one of the assigned objects.

    A plain store to the object itself (`p = q`) reads nothing; a store
    through it (`*p = 1`, `p[0] = 1`, `p->f = 1`) reads the pointer.
    """
    for name in names:
        for match in re.finditer(rf"(?<![\w>.]){lvalue_pattern(name)}(?![\w])", text):
            store = re.match(r"\s*=(?!=)", text[match.end() :])
            through = text[: match.start()].rstrip().endswith("*")
            if not store or through:
                return True
    return False


def leaves(body, label, names):
    """True when the branch leaves before any statement uses a NULL object."""
    pattern = LEAVES
    if label in PROGRAM_LEAVES:
        pattern += "|" + PROGRAM_LEAVES[label]
    for statement in top_level(body):
        if mentions(statement, names):
            return False
        if re.match(rf"(?:{pattern})", statement):
            return True
    return False


def call_ends_statement(stmt, open_paren):
    """True when nothing but closing parentheses follows the allocator call."""
    depth = 0
    for i in range(open_paren, len(stmt)):
        if stmt[i] == "(":
            depth += 1
        elif stmt[i] == ")":
            depth -= 1
            if depth == 0:
                return re.fullmatch(r"[\s)]*;?\s*", stmt[i + 1 :]) is not None
    return False


def names_of(chain, chain_start, code):
    names = []
    for match in ONE_LVALUE.finditer(chain):
        name = re.sub(r"\s+", "", match.group(1))
        before = code[: chain_start + match.start(1)].rstrip()
        if name.startswith("*") and before and (before[-1].isalnum() or before[-1] == "_"):
            name = name[1:]
        names.append(name)
    return names


def scan(path, text, root=None):
    """Return (findings, sites, exemptions used) for one file."""
    code = blank(text)
    parts = list(statements(code))
    label = path.as_posix() if root is None else path.relative_to(root).as_posix()
    findings = []
    used = set()
    sites = 0
    recognized = set()
    for index, (start, stmt) in enumerate(parts):
        for match in ASSIGNED_CALL.finditer(stmt):
            sites += 1
            recognized.add(start + match.start(2))
            line = code.count("\n", 0, start + match.start()) + 1
            names = names_of(match.group(1), start + match.start(1), code)
            span = branch_span(code, start + len(stmt))
            if (
                enclosed_test(stmt, match.start(1))
                and not conditional(text, (start + match.start(), span[1]))
                and leaves(code[span[0] : span[1]], label, names)
            ):
                continue
            if call_ends_statement(stmt, match.end() - 1) and index + 1 < len(parts):
                after, following = parts[index + 1]
                cond = condition(following)
                span = branch_span(code, after + len(following))
                if (
                    cond is not None
                    and tests_null(cond, names)
                    and not conditional(text, (start + match.start(), span[1]))
                    and leaves(code[span[0] : span[1]], label, names)
                ):
                    continue
            collapsed = re.sub(r"\s+", " ", stmt[: stmt.find("(", match.end() - 1)]).strip()
            call = collapsed + re.sub(r"\s+", " ", stmt[match.end() - 1 : stmt.rfind(")") + 1])
            key = (label, re.sub(r"\s*;\s*$", "", call).strip())
            if key in EXEMPT:
                used.add(key)
                continue
            findings.append(
                f"{label}:{line}: {', '.join(names)} from an allocation is used "
                "without a NULL test whose branch leaves"
            )
    for call in re.finditer(rf"\b{ALLOCATORS}\s*\(", code):
        if call.start() not in recognized:
            line = code.count("\n", 0, call.start()) + 1
            findings.append(
                f"{label}:{line}: an allocation in a form the scan does not read "
                "(an argument, a return value, an unusual assignment)"
            )
            sites += 1
    return findings, sites, used


KNOWN_BAD = {
    "unchecked": "p = malloc(4);\n*p = 1;\n",
    "cast": "p = (char *)malloc(4);\np[0] = 0;\n",
    "chained": "p = buf = (char *) malloc(8);\nwhile (*s) *p++ = *s++;\n",
    "declaration": "char *cp = strdup(s);\nputs(cp);\n",
    "multiline": "*pp = (struct a *)realloc((char *)*pp,\n    2 * n);\nfp = *pp + n;\n",
    "other variable": "p = calloc(2, 4);\nif (q == NULL)\n\texit(1);\n",
    "test after use": "p = malloc(4);\np[0] = 0;\nif (p == NULL)\n\texit(1);\n",
    "member": "dp->name = strdup(s);\nlen = strlen(dp->name);\n",
    "comparison with one": "p = malloc(4);\nif (p == 1)\n\texit(1);\n",
    "observing test": "p = malloc(4);\nif (p != NULL)\n\tlog_ok();\n*p = 1;\n",
    "logging branch": 'p = malloc(4);\nif (p == NULL)\n\tputs("no memory");\n*p = 1;\n',
    "logging block": 'p = malloc(4);\nif (p == NULL) {\n\tputs("no memory");\n}\n*p = 1;\n',
    "inline observing": "if ((p = malloc(4)) != NULL)\n\tlog_ok();\n*p = 1;\n",
    "inline logging": 'if ((p = malloc(4)) == NULL)\n\tputs("no memory");\n*p = 1;\n',
    "parenthesized call": "p = (malloc(4));\n*p = 1;\n",
    "cast of parenthesized call": "p = (char *)(malloc(4));\n*p = 1;\n",
    "test in a conjunction": "p = malloc(4);\nif (p == NULL && verbose)\n\texit(1);\n*p = 1;\n",
    "error without FATAL": 'p = malloc(4);\nif (p == NULL)\n\terror(WARNING, "x");\n*p = 1;\n',
    "conditional terminator": (
        "p = malloc(4);\nif (p == NULL) {\n\tif (verbose)\n\t\treturn;\n}\n*p = 1;\n"
    ),
    "terminator in a nested block": (
        "p = malloc(4);\nif (p == NULL) {\n\twhile (busy) {\n\t\texit(1);\n\t}\n}\n*p = 1;\n"
    ),
    "terminator in an argument": "p = malloc(4);\nif (p == NULL)\n\tlog(exit_code());\n*p = 1;\n",
    "use in the allocation statement": "p = malloc(4), *p = 1;\nif (!p)\n\texit(1);\n",
    "use before the exit": "p = malloc(4);\nif (!p) {\n\t*p = 1;\n\texit(1);\n}\n",
    "inline use before the exit": "if ((p = malloc(4)) == NULL) {\n\tp[0] = 0;\n\texit(1);\n}\n",
    "terminator under #if": (
        "p = malloc(4);\nif (!p) {\n#if FOO\n\texit(1);\n#endif\n}\n*p = 1;\n"
    ),
    "test under #if": "p = malloc(4);\n#ifdef CHECKED\nif (!p)\n\texit(1);\n#endif\n*p = 1;\n",
    "qualified cast": "p = (char * const)malloc(4);\n*p = 1;\n",
    "unassigned": "return (malloc(n));\n",
    "argument": "use(malloc(n));\n",
    "parenthesized lvalue": "(p) = malloc(4);\n*p = 1;\n",
    "exit reads the pointer": "p = malloc(4);\nif (!p)\n\texit(*p);\n",
    "return reads the pointer": "p = malloc(4);\nif (p == NULL)\n\treturn p->n;\n",
    "overflo outside awk": "if ((p = malloc(n)) == NULL)\n\toverflo();\n*p = 1;\n",
    "error outside ed": "p = malloc(4);\nif (p == NULL)\n\terror(Q);\n*p = 1;\n",
    "commented test": "p = malloc(4);\n/* if (p == NULL) exit(1); */\np[0] = 0;\n",
}
KNOWN_GOOD = {
    "null": "p = malloc(4);\nif (p == NULL)\n\texit(1);\n",
    "zero": "fp = (struct a *)calloc(n, sizeof (struct a));\nif (fp == 0) {\n\texit(1);\n}\n",
    "negation": "p = strdup(s);\nif (!p)\n\treturn;\n",
    "inline": (
        "usr.bin/awk/b.c",
        "if ((p = ptr = (int *) malloc((n+1)*sizeof(int))) == NULL)\n\toverflo();\n",
    ),
    "labeled inline": (
        "usr.bin/awk/b.c",
        "tenter:\n\tif ((pfa = (struct fa *) malloc(n)) == NULL)\n\t\toverflo();\n",
    ),
    "macro above": "#define TWO \\\n\t2\np = malloc(TWO);\nif (p == NULL)\n\texit(1);\n",
    "inline in block": (
        "bin/ed/ed.c",
        "if ((zero = (int *)realloc((char *)zero, n))==NULL) {\n\terror(Q);\n}\n",
    ),
    "declaration": "register char *cp = strdup(str);\n\nif (cp == NULL) {\n\texit(1);\n}\n",
    "chained second": "p = buf = (char *)malloc(8);\nif (buf == NULL)\n\texit(1);\n",
    "dereferenced store": (
        "*pp = (struct a *)realloc((char *)*pp,\n    2 * n);\nif (*pp == 0) {\n\texit(1);\n}\n"
    ),
    "comment between": "p = malloc(4); /* buffer */\nif (p == NULL)\n\texit(1);\n",
    "member": "dp->name = strdup(s);\nif (dp->name == NULL)\n\texit(1);\n",
    "parenthesized call": "p = (malloc(4));\nif (p == NULL)\n\treturn;\n",
    "fatal error": 'p = malloc(4);\nif (p == NULL)\n\terror(FATAL, "out of space");\n',
    "restore before the exit": (
        "if ((zero = (int *)realloc((char *)zero, n)) == NULL) {\n\tzero = ozero;\n\texit(1);\n}\n"
    ),
    "terminator after a message": (
        'p = malloc(4);\nif (p == NULL) {\n\tfputs("out of memory", stderr);\n\texit(1);\n}\n'
    ),
    "qualified cast": "p = (char * const)malloc(4);\nif (p == NULL)\n\texit(1);\n",
    "goto": "p = malloc(4);\nif (!p)\n\tgoto fail;\n",
    "cast null": "p = malloc(4);\nif (p == (char *)0)\n\texit(1);\n",
}


def selftest():
    failures = []
    for name, body in KNOWN_BAD.items():
        found, sites, _ = scan(Path("bad.c"), "void f(void) {\n" + body + "}\n")
        if sites != 1 or len(found) != 1:
            failures.append(f"known-bad {name}: sites={sites} findings={len(found)}")
    for name, body in KNOWN_GOOD.items():
        label, body = body if isinstance(body, tuple) else ("good.c", body)
        found, sites, _ = scan(Path(label), "void f(void) {\n" + body + "}\n")
        if sites != 1 or found:
            failures.append(f"known-good {name}: sites={sites} findings={found}")
    if failures:
        for failure in failures:
            print(f"check-alloc-results selftest: {failure}", file=sys.stderr)
        return 1
    print(
        f"check-alloc-results selftest: {len(KNOWN_BAD)} known-bad rejected, "
        f"{len(KNOWN_GOOD)} known-good accepted"
    )
    return 0


def main(argv):
    if argv == ["--selftest"]:
        return selftest()
    if not argv:
        print(__doc__.strip().splitlines()[-2], file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parent.parent
    findings = []
    used = set()
    sites = 0
    for name in argv:
        path = Path(name).resolve()
        found, count, exempt = scan(path, path.read_text(encoding="latin-1"), root)
        findings += found
        used |= exempt
        sites += count
    for key in EXEMPT.keys() - used:
        scanned = {Path(name).resolve().relative_to(root).as_posix() for name in argv}
        if key[0] in scanned:
            findings.append(f"{key[0]}: exemption for {key[1]!r} matches nothing")
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return 1
    print(f"check-alloc-results: {sites} allocation sites in {len(argv)} files tested")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
