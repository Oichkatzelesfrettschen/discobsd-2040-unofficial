"""Preprocessor-independent scan for K&R function interfaces in C source.

The scan reads every conditional branch, because it never evaluates a
preprocessor directive, and reports two kinds of finding:

  definition   a function definition whose parameter list is empty or an
               identifier list (the obsolescent forms of C17 6.9.1)
  declaration  a file-scope function declarator with an empty parameter
               list, or a function-pointer declarator "(*name)()" at any
               depth, neither of which carries a prototype (C17 6.7.6.3)

It is one of two inventory oracles. The compiler run in c17_inventory.py
sees only the active configuration but parses every form the target compiler
accepts; this scan sees inactive branches but recognizes only the shapes its
fixtures calibrate. A K&R definition nested inside a declarator, such as a
function returning a pointer to function, is outside its grammar and is left
to the compiler oracle.
"""

import re
import sys
from dataclasses import dataclass

TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\d[\w.]*|\.\.\.|->|[^\s\w]")
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
TYPE_WORDS = {
    "void",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "signed",
    "unsigned",
    "_Bool",
    "struct",
    "union",
    "enum",
    "const",
    "volatile",
    "register",
    "restrict",
    "static",
    "extern",
    "inline",
}
# Words whose parenthesized operand is not a declarator, skipped whole.
ATTRIBUTE_WORDS = {"__attribute__", "__asm__", "asm", "__declspec"}
NOT_NAMES = {
    "if",
    "while",
    "for",
    "switch",
    "return",
    "sizeof",
    "do",
    "else",
    "case",
    "goto",
    "_Alignof",
    "_Generic",
    "_Static_assert",
    "defined",
    "__attribute__",
    "__asm__",
    "asm",
    "__typeof__",
    "typeof",
}


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    kind: str
    name: str


def strip_source(text):
    """Blank comments, literals and preprocessor lines, keeping line numbers."""
    out = []
    i, n = 0, len(text)
    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start and c in " \t":
            out.append(c)
            i += 1
            continue
        if at_line_start and c == "#":
            # A directive runs to an unescaped newline; its body is blank.
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                    out.append("\n")
                    i += 2
                    continue
                if text.startswith("/*", i):
                    end = text.find("*/", i + 2)
                    end = n if end < 0 else end + 2
                    out.append("".join(ch if ch == "\n" else " " for ch in text[i:end]))
                    i = end
                    continue
                out.append(" ")
                i += 1
            continue
        at_line_start = False
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:end]))
            i = end
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(" " * (j - i))
            i = j
            continue
        if c == "\n":
            at_line_start = True
        out.append(c)
        i += 1
    return "".join(out)


def tokenize(text):
    tokens = []
    for lineno, line in enumerate(strip_source(text).split("\n"), 1):
        for match in TOKEN.finditer(line):
            tokens.append((match.group(0), lineno))
    return tokens


def closing(tokens, start):
    """Index of the parenthesis matching tokens[start], which is '('."""
    depth = 0
    for k in range(start, len(tokens)):
        if tokens[k][0] == "(":
            depth += 1
        elif tokens[k][0] == ")":
            depth -= 1
            if depth == 0:
                return k
    return -1


def identifier_list(params):
    """True for an empty list or comma-separated plain identifiers."""
    if not params:
        return True
    if params == ["void"]:
        return False
    expect_name = True
    for tok in params:
        if expect_name:
            if not IDENT.match(tok) or tok in NOT_NAMES or tok in TYPE_WORDS:
                return False
        elif tok != ",":
            return False
        expect_name = not expect_name
    return not expect_name


def definition_follows(tokens, k):
    """True when a K&R body follows the parameter list closed at tokens[k-1].

    Either the body opens at once, or a declaration list of ';'-terminated
    declarations precedes it. An initializer, a closing brace, or a name
    applied to a parameter list (the next function's declarator, where the
    parenthesized list belonged to a prototype declaration) before the body
    rules the definition out. "(*name)()" stays legal in the list.
    """
    if k < len(tokens) and tokens[k][0] == "{":
        return True
    saw_semicolon = False
    depth = 0
    while k < len(tokens):
        tok = tokens[k][0]
        if (
            depth == 0
            and tok in ATTRIBUTE_WORDS
            and k + 1 < len(tokens)
            and tokens[k + 1][0] == "("
        ):
            k = closing(tokens, k + 1)
            if k < 0:
                return False
            k += 1
            continue
        if tok in "([":
            depth += 1
        elif tok in ")]":
            depth -= 1
        elif depth == 0:
            if tok == "{":
                return saw_semicolon and tokens[k - 1][0] == ";"
            if tok in "=}":
                return False
            if (
                IDENT.match(tok)
                and k + 1 < len(tokens)
                and tokens[k + 1][0] == "("
                and (k + 2 >= len(tokens) or tokens[k + 2][0] != "*")
            ):
                return False
            if tok == ";":
                saw_semicolon = True
        k += 1
    return False


def statement_start(tokens, k):
    """Index of the first token of the statement or declaration holding k."""
    depth = 0
    j = k - 1
    while j >= 0:
        tok = tokens[j][0]
        if tok in ")]":
            depth += 1
        elif tok in "([":
            if depth == 0:
                return j + 1
            depth -= 1
        elif depth == 0 and tok in {";", "{", "}"}:
            return j + 1
        j -= 1
    return 0


def pointer_declarator(tokens, k):
    """True when "(*name)()" at tokens[k] declares rather than calls.

    A declarator follows a type: a specifier or typedef name, or a "*" of a
    pointer declarator. After a comma it continues a declaration only when
    the enclosing statement or parameter list begins like one: a type
    followed by a declarator. "return (*fp)();", a bare "(*fp)();" statement
    and "f(x, (*fp)())" are calls.
    """
    prev = tokens[k - 1][0] if k else ""
    if prev == "*" or (IDENT.match(prev) and prev not in NOT_NAMES):
        return True
    if prev != ",":
        return False
    start = statement_start(tokens, k)
    if start >= k:
        return False
    first = tokens[start][0]
    if not IDENT.match(first) or first in NOT_NAMES:
        return False
    # A declaration's first token, a type, is followed by its declarator: a
    # name, a "*", or "(*". A call argument or an expression statement is
    # followed by an operator, a comma or a parenthesized argument list.
    following = tokens[start + 1][0] if start + 1 < len(tokens) else ""
    if following == "(":
        return start + 2 < len(tokens) and tokens[start + 2][0] == "*"
    return following == "*" or bool(IDENT.match(following))


def scan_text(path, text):
    tokens = tokenize(text)
    findings = []
    brace = 0
    k = 0
    while k < len(tokens):
        tok, line = tokens[k]
        if tok == "{":
            brace += 1
        elif tok == "}":
            brace = max(brace - 1, 0)
        elif tok == "(" and k + 3 < len(tokens) and tokens[k + 1][0] == "*":
            # "(*name)()" declares a pointer to an unprototyped function.
            name_tok = tokens[k + 2][0]
            if (
                IDENT.match(name_tok)
                and tokens[k + 3][0] == ")"
                and k + 5 < len(tokens)
                and tokens[k + 4][0] == "("
                and tokens[k + 5][0] == ")"
            ):
                if pointer_declarator(tokens, k):
                    findings.append(Finding(path, tokens[k + 2][1], "declaration", name_tok))
        elif (
            brace == 0
            and IDENT.match(tok)
            and tok not in NOT_NAMES
            and tok not in TYPE_WORDS
            and k + 2 < len(tokens)
            and tokens[k + 1][0] == "("
            and tokens[k + 2][0] != "*"
        ):
            end = closing(tokens, k + 1)
            if end < 0:
                break
            params = [t for t, _ in tokens[k + 2 : end]]
            prev = tokens[k - 1][0] if k else ""
            nested = prev == "("
            if not nested and identifier_list(params) and definition_follows(tokens, end + 1):
                findings.append(Finding(path, line, "definition", tok))
                k = end + 1
                continue
            following = tokens[end + 1][0] if end + 1 < len(tokens) else ""
            if not params and not nested and following in {";", ","} and prev not in {"=", ","}:
                if IDENT.match(prev) or prev in {"*", ")"}:
                    findings.append(Finding(path, line, "declaration", tok))
            # A prototype's parameter list can itself hold "(*name)()".
            k += 2
            continue
        k += 1
    return findings


def scan_file(path):
    with open(path, encoding="latin-1") as handle:
        return scan_text(path, handle.read())


def main(argv):
    for path in argv:
        for finding in scan_file(path):
            print(f"{finding.path}:{finding.line}:{finding.kind}:{finding.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
