"""Preprocessor-independent scan for K&R function interfaces in C source.

The scan reads every conditional branch, because it never evaluates a
preprocessor directive, and reports two kinds of finding:

  definition   a function definition whose parameter list is empty or an
               identifier list (the obsolescent forms of C17 6.9.1)
  declaration  a function declarator with an empty parameter list, at
               file scope or in a declaration-shaped block-scope statement,
               or a grouped declarator such as "(name)()", "(*name)()",
               "(**name)()" or "(*name[2])()" at any depth, none of which
               carries a prototype (C17 6.7.6.3)

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
# Qualifiers of a pointer declarator, as in "(* const name)()".
POINTER_QUALIFIERS = {
    "const",
    "volatile",
    "restrict",
    "__const",
    "__const__",
    "__volatile",
    "__volatile__",
    "__restrict",
    "__restrict__",
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


# Matched at a directive's "#", which strip_source finds at a line start.
CONDITIONAL = re.compile(r"#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b")


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    kind: str
    name: str


def strip_source(text, conditionals=None):
    """Blank comments, literals and preprocessor lines, keeping line numbers.

    When conditionals is a list, each conditional directive outside a
    comment is appended to it as (line number, directive name).
    """
    out = []
    i, n = 0, len(text)
    at_line_start = True
    lineno, counted = 1, 0
    while i < n:
        c = text[i]
        if at_line_start and c in " \t":
            out.append(c)
            i += 1
            continue
        if at_line_start and c == "#":
            if conditionals is not None:
                lineno += text.count("\n", counted, i)
                counted = i
                match = CONDITIONAL.match(text, i)
                if match:
                    conditionals.append((lineno, match.group(1)))
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
            # A backslash-newline inside the literal keeps its newline.
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
            continue
        if c == "\n":
            at_line_start = True
        out.append(c)
        i += 1
    return "".join(out)


def tokenize(text, conditionals=None):
    tokens = []
    for lineno, line in enumerate(strip_source(text, conditionals).split("\n"), 1):
        for match in TOKEN.finditer(line):
            tokens.append((match.group(0), lineno))
    return tokens


def conditional_events(conditionals, tokens):
    """Map a token index to the conditional directives just before it.

    The directive lines are blank in the token stream, so this is how the
    scan learns where an #if group's alternatives begin and end.
    """
    events = {}
    k = 0
    for lineno, kind in conditionals:
        while k < len(tokens) and tokens[k][1] <= lineno:
            k += 1
        events.setdefault(k, []).append(kind)
    return events


def apply_conditionals(kinds, brace, stack):
    """Brace depth after the directives in kinds.

    Each alternative of an #if group starts at the depth the group opened
    at, so a branch that opens a function body does not hide the next
    branch's definition; after #endif the depth is the first alternative's.
    """
    for kind in kinds:
        if kind in ("if", "ifdef", "ifndef"):
            stack.append([brace, None])
        elif kind in ("elif", "else") and stack:
            if stack[-1][1] is None:
                stack[-1][1] = brace
            brace = stack[-1][0]
        elif kind == "endif" and stack:
            opened, first = stack.pop()
            if first is not None:
                brace = first
    return brace


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


def subscripts_end(tokens, j):
    """Index of the first token at or after j that ends a run of [...]."""
    while j < len(tokens) and tokens[j][0] == "[":
        depth = 0
        while j < len(tokens):
            if tokens[j][0] == "[":
                depth += 1
            elif tokens[j][0] == "]":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        j += 1
    return j


def grouped_declarator(tokens, k):
    """Index of the name in "( *... name [...]... ) ( )" at tokens[k], or -1.

    A parenthesized name, optionally behind pointer stars and the
    qualifiers that may follow each star and before array subscripts,
    applied to an empty parameter list: a declarator of an unprototyped
    function, of a pointer or an array of pointers to one, or a call
    through a parenthesized designator. The empty parameter list starts
    at subscripts_end(tokens, name + 1) + 1.
    """
    if k >= len(tokens) or tokens[k][0] != "(":
        return -1
    j = k + 1
    stars = 0
    while j < len(tokens) and (
        tokens[j][0] == "*" or (stars and tokens[j][0] in POINTER_QUALIFIERS)
    ):
        stars += tokens[j][0] == "*"
        j += 1
    if j >= len(tokens) or not IDENT.match(tokens[j][0]) or tokens[j][0] in NOT_NAMES:
        return -1
    m = subscripts_end(tokens, j + 1)
    if (
        m + 2 < len(tokens)
        and tokens[m][0] == ")"
        and tokens[m + 1][0] == "("
        and tokens[m + 2][0] == ")"
    ):
        return j
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
    """True when the grouped declarator at tokens[k] declares rather than calls.

    A declarator follows a type: a specifier or typedef name, or a "*" of a
    pointer declarator. After a comma it continues a declaration only when
    the enclosing statement or parameter list begins like one: a type
    followed by a declarator. "return (*fp)();", a bare "(*fp)();" statement
    and "f(x, (*fp)())" are calls.
    """
    prev = tokens[k - 1][0] if k else ""
    if prev == "*" or (IDENT.match(prev) and prev not in NOT_NAMES):
        return True
    return prev == "," and continues_declaration(tokens, k)


def continues_declaration(tokens, k):
    """True when the comma before tokens[k] separates declarators."""
    start = statement_start(tokens, k)
    if start >= k:
        return False
    first = tokens[start][0]
    if not IDENT.match(first) or first in NOT_NAMES:
        return False
    # A declaration's first token, a type, is followed by its declarator: a
    # name, a "*", "(*" or a grouped "(name)()". A call argument or an
    # expression statement is followed by an operator, a comma or a
    # parenthesized argument list.
    following = tokens[start + 1][0] if start + 1 < len(tokens) else ""
    if following == "(":
        return (start + 2 < len(tokens) and tokens[start + 2][0] == "*") or grouped_declarator(
            tokens, start + 1
        ) >= 0
    return following == "*" or bool(IDENT.match(following))


def declaration_prefix(tokens, k):
    """True when the statement holding tokens[k] opens like a declaration.

    Every token before the name is a specifier, a typedef name, a "*" or
    an attribute group, as in "extern int name();", "char *name();" or
    "extern __attribute__((noreturn)) int name();". A call has an
    operator, a keyword such as return, or nothing before it.
    """
    start = statement_start(tokens, k)
    if start >= k or not IDENT.match(tokens[start][0]):
        return False
    j = start
    while j < k:
        tok = tokens[j][0]
        if tok in ATTRIBUTE_WORDS and j + 1 < k and tokens[j + 1][0] == "(":
            j = closing(tokens, j + 1)
            if j < 0 or j >= k:
                return False
        elif tok != "*" and not (IDENT.match(tok) and tok not in NOT_NAMES):
            return False
        j += 1
    return True


def following_attributes(tokens, after):
    """Index past any attribute groups starting at tokens[after]."""
    while (
        after + 1 < len(tokens)
        and tokens[after][0] in ATTRIBUTE_WORDS
        and tokens[after + 1][0] == "("
    ):
        after = closing(tokens, after + 1) + 1
        if after <= 0:
            return -1
    return after


def scan_text(path, text):
    conditionals = []
    tokens = tokenize(text, conditionals)
    events = conditional_events(conditionals, tokens)
    stack = []
    applied = 0
    findings = []
    brace = 0
    k = 0
    while k < len(tokens):
        while applied <= k:
            brace = apply_conditionals(events.get(applied, ()), brace, stack)
            applied += 1
        tok, line = tokens[k]
        if tok == "{":
            brace += 1
        elif tok == "}":
            brace = max(brace - 1, 0)
        elif tok == "(" and grouped_declarator(tokens, k) >= 0:
            # "(name)()" declares an unprototyped function and "(*name)()" a
            # pointer to one; at file scope a body after "(name)()" defines it.
            j = grouped_declarator(tokens, k)
            after = subscripts_end(tokens, j + 1) + 3
            name_tok, name_line = tokens[j]
            prev = tokens[k - 1][0] if k else ""
            if (
                j == k + 1
                and after == j + 4
                and brace == 0
                and prev != "("
                and definition_follows(tokens, after)
            ):
                findings.append(Finding(path, name_line, "definition", name_tok))
                k = after
                continue
            if pointer_declarator(tokens, k):
                findings.append(Finding(path, name_line, "declaration", name_tok))
            k = after
            continue
        elif (
            brace == 0
            and IDENT.match(tok)
            and tok not in NOT_NAMES
            and tok not in TYPE_WORDS
            and k + 2 < len(tokens)
            and tokens[k + 1][0] == "("
            and tokens[k + 2][0] != "*"
            and grouped_declarator(tokens, k + 1) < 0
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
            after = following_attributes(tokens, end + 1)
            following = tokens[after][0] if 0 < after < len(tokens) else ""
            typed = (IDENT.match(prev) and prev not in NOT_NAMES) or prev == "*"
            if not params and not nested and following in {";", ","}:
                # At file scope "name();" after a statement boundary is an
                # implicit-int declaration; no call can appear there.
                if (
                    typed
                    or prev in {")", "", ";", "}"}
                    or (prev == "," and continues_declaration(tokens, k))
                ):
                    findings.append(Finding(path, line, "declaration", tok))
            elif not params and following == ")" and typed:
                # The last parameter of a prototype, "void f(int cb());",
                # declares an unprototyped function of its own.
                findings.append(Finding(path, line, "declaration", tok))
            # A prototype's parameter list can itself hold "(*name)()".
            k += 2
            continue
        elif (
            brace > 0
            and IDENT.match(tok)
            and tok not in NOT_NAMES
            and tok not in TYPE_WORDS
            and k + 3 < len(tokens)
            and tokens[k + 1][0] == "("
            and tokens[k + 2][0] == ")"
        ):
            # A block-scope declaration, "extern int name();", where a call
            # "name();" is also possible: only a declaration-shaped statement
            # counts.
            after = following_attributes(tokens, k + 3)
            following = tokens[after][0] if 0 < after < len(tokens) else ""
            prev = tokens[k - 1][0] if k else ""
            if following in {";", ","} and (
                declaration_prefix(tokens, k) or (prev == "," and continues_declaration(tokens, k))
            ):
                findings.append(Finding(path, line, "declaration", tok))
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
