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
"""Rewrite K&R function definitions as C17 prototyped definitions.

The converter handles one shape and refuses every other, because a K&R
definition and a prototyped definition differ in how a parameter narrower
than int is received (C17 6.5.2.2p6, 6.9.1p10): a K&R "char c" arrives as a
promoted int and is narrowed in the callee, while a prototyped "char c" arrives
as a char. A rewrite of such a parameter changes the object code, so the tool
leaves the definition untouched and reports the reason.

A definition converts when

  - its identifier list names every parameter once and its declaration list
    declares each of them once, or declares none (implicit int);
  - every parameter has a type unchanged by the default argument promotions:
    no char, short, float, _Bool or enumerated type outside a pointer or
    array declarator, and no typedef name outside WIDE_OK, because the tool
    cannot read the width of u_char, u_short or a narrow uid_t;
  - no parameter is a function or a function pointer, and no comment, string
    or directive sits between the identifier list and the opening brace.

An empty identifier list becomes "(void)". The tool scans the whole text of a
file and never evaluates a preprocessor directive, so a definition in each
conditional arm converts on its own merits and a definition a macro generates
stays as written. The output replaces only the identifier list and the
declaration list; the text before and after is byte-identical.

Usage: kr_to_ansi.py [--check] FILE...  rewrites each FILE in place, or with
--check reports the definitions that would convert and exits 1 when any would.
Every finding is one line on stderr: "FILE:LINE: NAME: converted" or
"FILE:LINE: NAME: refused: REASON".
"""

import re
import sys

PROMOTED = {"char", "short", "float", "_Bool", "enum"}
# Words and typedef names whose by-value width is at least that of int in this tree.
WIDE_OK = {
    "int",
    "unsigned",
    "signed",
    "long",
    "double",
    "register",
    "const",
    "volatile",
    "restrict",
    "size_t",
    "ssize_t",
    "ptrdiff_t",
    "u_int",
    "u_long",
    "dev_t",
    "ino_t",
    "daddr_t",
    "off_t",
    "time_t",
}
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
}
IDENT = re.compile(r"[A-Za-z_]\w*\Z")
LINE_LIMIT = 79


def mask(text):
    """Blank comments, literals and directives with spaces, keeping offsets."""
    out = list(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c in " \t":
            i += 1
            continue
        if line_start and c == "#":
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                    out[i] = " "
                    i += 2
                    continue
                if text.startswith("/*", i):
                    end = text.find("*/", i + 2)
                    end = n if end < 0 else end + 2
                    for k in range(i, end):
                        if text[k] != "\n":
                            out[k] = " "
                    i = end
                    continue
                out[i] = " "
                i += 1
            continue
        if text.startswith("/*", i) or text.startswith("//", i):
            if text.startswith("/*", i):
                end = text.find("*/", i + 2)
                end = n if end < 0 else end + 2
            else:
                end = text.find("\n", i)
                end = n if end < 0 else end
            for k in range(i, end):
                if text[k] != "\n":
                    out[k] = " "
            i = end
            continue
        line_start = False
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            for k in range(i + 1, j):
                if text[k] != "\n":
                    out[k] = " "
            i = j
            continue
        if c == "\n":
            line_start = True
        i += 1
    return "".join(out)


def depth_zero(masked):
    """Return a list: True where the offset lies outside every brace pair."""
    flags = []
    depth = 0
    for ch in masked:
        if ch == "}":
            depth -= 1
        flags.append(depth == 0)
        if ch == "{":
            depth += 1
    return flags


def split_top(text, sep):
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return parts


def parse_declaration(piece_list, names):
    """Return (base, {name: declarator}) for one declaration, or a refusal."""
    first = piece_list[0]
    tokens = re.findall(r"[A-Za-z_]\w*|\*|\(|\)|\[|\]|\S", first)
    start = None
    for k, tok in enumerate(tokens):
        if tok == "*" or (
            tok in names and not (k and tokens[k - 1] in ("struct", "union", "enum"))
        ):
            start = k
            break
    if start is None or start == 0:
        return "no declarator or no type"
    base = " ".join(tokens[:start])
    pieces = [" ".join(tokens[start:])] + [p.strip() for p in piece_list[1:]]
    result = {}
    for piece in pieces:
        m = re.search(r"\b([A-Za-z_]\w*)\b", piece)
        if "(" in piece:
            return "function or function-pointer parameter"
        if not m or m.group(1) not in names:
            return "declared name outside the identifier list"
        result[m.group(1)] = (base, re.sub(r"\s+", " ", piece).strip())
    return result


def format_param(base, declarator):
    declarator = re.sub(r"\s*\*\s*", " *", declarator).strip()
    declarator = re.sub(r"\s*\[\s*", "[", declarator)
    declarator = re.sub(r"\s*\]", "]", declarator)
    return f"{base} {declarator}".replace("* *", "**")


def promoted(base, declarator):
    """Return a reason when a by-value parameter of this type changes under promotion.

    A pointer or an array parameter is always as wide as an int. Any other base
    type must consist of keywords and typedef names this tree defines at least
    as wide as int; an unknown typedef (u_char, u_short, a narrow uid_t) is
    refused, because the converter cannot read its width.
    """
    if "*" in declarator or "[" in declarator:
        return None
    words = base.split()
    skip_tag = False
    for word in words:
        if skip_tag:
            skip_tag = False
            continue
        if word in ("struct", "union"):
            skip_tag = True
            continue
        if word in PROMOTED:
            return f"{base}"
        if word not in WIDE_OK:
            return f"{base}: typedef of unknown width"
    return None


CANDIDATE = re.compile(r"\b([A-Za-z_]\w*)\s*\(([^()]*)\)")


def convert(text):
    masked = mask(text)
    flags = depth_zero(masked)
    edits = []
    findings = []
    for m in CANDIDATE.finditer(masked):
        name = m.group(1)
        if not flags[m.start()] or name in ("if", "while", "for", "switch", "return", "sizeof"):
            continue
        # The return type sits between the previous ';', '}' or file start and the name.
        k = m.start() - 1
        while k >= 0 and masked[k] not in ";}{":
            k -= 1
        lead = masked[k + 1 : m.start()]
        if not re.fullmatch(r"[\s\w\*]*", lead) or "=" in lead:
            continue
        after = m.end()
        brace = masked.find("{", after)
        if brace < 0:
            continue
        between = masked[after:brace]
        if "}" in between or between.lstrip().startswith(";"):
            continue
        # Only identifier-only lists, "()" and "(void)" can begin a K&R definition.
        raw = m.group(2).strip()
        params = [p.strip() for p in raw.split(",")] if raw else []
        if raw == "void" or any(not IDENT.match(p) or p in TYPE_WORDS for p in params):
            continue
        if any(p == "..." for p in params):
            continue
        # A declaration list has only ';'-terminated statements: no '=' and no '{'.
        if between.strip() and (not between.strip().endswith(";") or "=" in between):
            continue
        if not between.strip() and masked[after:brace].strip():
            continue
        line = text.count("\n", 0, m.start(1)) + 1
        original_between = text[after:brace]
        if re.search(r"/\*|//|#|\"|'", original_between):
            findings.append(
                (line, name, "refused: comment, literal or directive in the declaration list")
            )
            continue
        if "va_dcl" in between:
            findings.append((line, name, "refused: va_dcl"))
            continue
        names = set(params)
        if len(names) != len(params):
            findings.append((line, name, "refused: duplicate parameter name"))
            continue
        declared = {}
        reason = None
        for stmt in [s for s in between.split(";") if s.strip()]:
            parsed = parse_declaration(split_top(stmt.strip(), ","), names)
            if isinstance(parsed, str):
                reason = parsed
                break
            for pname, value in parsed.items():
                if pname in declared:
                    reason = "parameter declared twice"
                declared[pname] = value
            if reason:
                break
        if reason:
            findings.append((line, name, f"refused: {reason}"))
            continue
        new_params = []
        for p in params:
            base, declarator = declared.get(p, ("int", p))
            narrow = promoted(base, declarator)
            if narrow:
                reason = f"promoted type for {p} ({narrow})"
                break
            new_params.append(format_param(base, declarator))
        if reason:
            findings.append((line, name, f"refused: {reason}"))
            continue
        header = f"{name}({', '.join(new_params) if new_params else 'void'})"
        if len(header) > LINE_LIMIT:
            header = f"{name}(" + ",\n\t".join(new_params) + ")"
        edits.append((m.start(1), brace, header + "\n"))
        findings.append((line, name, "converted"))
    out = []
    pos = 0
    for start, end, replacement in edits:
        out.append(text[pos:start])
        out.append(replacement)
        pos = end
    out.append(text[pos:])
    return "".join(out), findings


def main(argv):
    check = "--check" in argv
    paths = [a for a in argv if a != "--check"]
    if not paths:
        print(__doc__, file=sys.stderr)
        return 2
    would = False
    for path in paths:
        with open(path, encoding="latin-1", newline="") as handle:
            text = handle.read()
        result, findings = convert(text)
        for line, name, status in findings:
            print(f"{path}:{line}: {name}: {status}", file=sys.stderr)
        if any(status == "converted" for _, _, status in findings):
            would = True
            if not check:
                with open(path, "w", encoding="latin-1", newline="") as handle:
                    handle.write(result)
    return 1 if check and would else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
