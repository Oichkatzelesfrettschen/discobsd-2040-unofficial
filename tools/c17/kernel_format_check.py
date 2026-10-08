"""Check kernel printf formats against the conversions prf() and GCC share.

sys/sys/systm.h gives printf(), uprintf(), tprintf() and log() the
format(printf) attribute, so GCC checks argument types against ISO C
conversions. The two readers of a format still differ. GCC accepts
conversions sys/kern/subr_prf.c's prf() does not implement: %i, the
floating conversions, and the hh, h, ll, j, z and t length modifiers. GCC
reads %n as a store through a pointer where prf() prints a number, and %b
as C23's one-argument binary conversion where prf() decodes a bit field from
two arguments; a one-argument %b passes the attribute and makes prf() read a
second argument that is not there. GCC rejects prf()'s %D (hex dump of a
byte buffer). This check reads every literal format passed
to those four functions in the sources a kernel configuration builds and
accepts only

    %[-+#0]*(digits|*)?(.(digits|*))?(l?[douxX]|[cps]) and %%

where prf() and GCC agree on the argument type. A call whose format is not
a string literal is reported, since neither check can read it.
"""

import argparse
import os
import re
import shlex
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import c17_inventory  # noqa: E402

# Format argument index per function. DEBUG and DEBUG1 through DEBUG9 in
# sys/arch/rp2040/include/debug.h expand to printf(__VA_ARGS__) under
# GLOBAL_DEBUG, so their formats reach prf() in a debug kernel.
FUNCTIONS = {"printf": 0, "uprintf": 0, "tprintf": 1, "log": 1, "DEBUG": 0}
FUNCTIONS.update({f"DEBUG{level}": 0 for level in range(1, 10)})
# A parenthesized designator, "(printf)(...)", calls the function while
# suppressing a function-like macro of the same name.
CALL = re.compile(
    r"(?<![\w.>])(?P<group>\(\s*)?(?P<name>"
    + "|".join(sorted(FUNCTIONS, key=len, reverse=True))
    + r")(?(group)\s*\))\s*\("
)
CONVERSION = re.compile(
    r"%(?P<spec>[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*)?)?(?:hh|ll|[hljztLq])?[a-zA-Z%]?)"
)
# prf() reads the l modifier only for d, o, u, x and X; it ignores it for c,
# s and p, where ISO C's %lc and %ls take wide characters.
ALLOWED = re.compile(r"[-+#0]*(\d+|\*)?(\.(\d+|\*))?(l?[douxX]|[cps])\Z")
# A name preceded by one of these is being declared or defined, not called.
DECLARATION_PREFIX = re.compile(
    r"(?:\b(?:void|int|char|long|unsigned|static|extern|inline)|\*)\s*\Z"
)
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
ESCAPE = re.compile(r"\\(x[0-9A-Fa-f]+|[0-7]{1,3}|.)")
SIMPLE_ESCAPES = {
    "n": "\n",
    "t": "\t",
    "r": "\r",
    "a": "\a",
    "b": "\b",
    "f": "\f",
    "v": "\v",
}


# Left where a preprocessor directive stood, so a call whose arguments span
# one, such as a format chosen by #if and #else, is recognizable.
DIRECTIVE = "\0"


def strip_comments(text):
    """Blank comments and preprocessor lines, keeping literals and newlines.

    String and character literals are copied through first, so comment
    delimiters inside a format string stay part of the format. A directive
    is blank through its last backslash-continued line, after a DIRECTIVE
    marker.
    """
    out = []
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c in " \t":
            out.append(c)
            i += 1
            continue
        if line_start and c == "#":
            out.append(DIRECTIVE)
            i += 1
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                    out.append("\n")
                    i += 2
                    continue
                out.append(" ")
                i += 1
            continue
        line_start = c == "\n"
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j])
            i = j
            continue
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
        out.append(c)
        i += 1
    return "".join(out)


def argument_text(text, start):
    """Return the arguments of the call whose '(' ends at start, split at depth 0."""
    depth = 1
    args, current = [], []
    k = start
    while k < len(text) and depth:
        c = text[k]
        if c == '"':
            match = LITERAL.match(text, k)
            if match:
                current.append(match.group(0))
                k = match.end()
                continue
        if c == "'":
            end = text.find("'", k + 1)
            while end > 0 and text[end - 1] == "\\" and text[end - 2] != "\\":
                end = text.find("'", end + 1)
            current.append(text[k : end + 1])
            k = end + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                break
        elif c == "," and depth == 1:
            args.append("".join(current))
            current = []
            k += 1
            continue
        current.append(c)
        k += 1
    args.append("".join(current))
    return args


def decode_literal(body):
    """The characters a C string literal body denotes, one literal at a time.

    Octal and hexadecimal escapes are decoded so that "\\045n" is read as the
    %n it is at run time; each literal is decoded separately because an
    escape never spans adjacent literals.
    """

    def replace(match):
        escape = match.group(1)
        if escape[0] == "x":
            return chr(int(escape[1:], 16) & 0xFF)
        if escape[0] in "01234567":
            return chr(int(escape, 8) & 0xFF)
        return SIMPLE_ESCAPES.get(escape, escape)

    return ESCAPE.sub(replace, body)


def literal_format(argument):
    """The concatenated, decoded string literals forming argument, or None."""
    literals = LITERAL.findall(argument)
    if not literals or LITERAL.sub("", argument).strip():
        return None
    return "".join(decode_literal(body) for body in literals)


def check_text(path, text):
    """Return (problems, checked) for one source."""
    text = strip_comments(text)
    problems = []
    checked = 0
    for call in CALL.finditer(text):
        name = call["name"]
        line = text.count("\n", 0, call.start()) + 1
        args = argument_text(text, call.end())
        index = FUNCTIONS[name]
        if len(args) <= index:
            continue
        if DECLARATION_PREFIX.search(text[max(call.start() - 40, 0) : call.start()]):
            continue  # a declaration or definition, not a call
        if any(DIRECTIVE in arg for arg in args):
            problems.append(
                f"{path}:{line}: {name} arguments span a preprocessor directive, "
                "so no single format can be checked"
            )
            continue
        fmt = literal_format(args[index])
        if fmt is None:
            problems.append(f"{path}:{line}: {name} format is not a string literal")
            continue
        checked += 1
        for match in CONVERSION.finditer(fmt):
            spec = match["spec"]
            if spec == "%":
                continue
            if not ALLOWED.match(spec):
                problems.append(
                    f"{path}:{line}: {name} uses %{spec}, "
                    "outside the conversions prf() and GCC share"
                )
    return problems, checked


def sources_from_builds(make, root, builds):
    sources = set()
    for build_dir in builds:
        build = (root / build_dir).resolve()
        for item in shlex.split(c17_inventory.make_value(make, build, "CFILES")):
            sources.add(os.path.relpath((build / item).resolve(), root))
    return sorted(sources)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--make", default=os.environ.get("MAKE", "bmake"))
    parser.add_argument("--build", action="append", default=[])
    parser.add_argument("sources", nargs="*")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    sources = list(args.sources) or sources_from_builds(args.make, root, args.build)
    problems, checked = [], 0
    for source in sources:
        found, count = check_text(source, (root / source).read_text(encoding="latin-1"))
        problems += found
        checked += count
    if problems:
        for problem in problems:
            print("FAIL " + problem, file=sys.stderr)
        return 1
    print(
        f"PASS kernel formats: {checked} literal formats in {len(sources)} sources "
        "use prf() conversions"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
