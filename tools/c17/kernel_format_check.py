"""Check kernel printf formats against the conversions prf() and GCC share.

sys/sys/systm.h gives printf(), uprintf(), tprintf() and log() the
format(printf) attribute, so GCC checks argument types against ISO C
conversions. The two readers of a format still differ. GCC accepts
conversions sys/kern/subr_prf.c's prf() does not implement: %i, the
floating conversions, and the hh, h, ll, j, z and t length modifiers. GCC
reads %n as a store through a pointer where prf() prints a number, and it
rejects prf()'s %b (bit-field decode) and %D (hex dump of a byte buffer)
only by luck of their letters. This check reads every literal format passed
to those four functions in the sources a kernel configuration builds and
accepts only

    %[-+#0]*(digits|*)?(.(digits|*))?l?[cdopsuxX] and %%

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

FUNCTIONS = {"printf": 0, "uprintf": 0, "tprintf": 1, "log": 1}
CALL = re.compile(r"(?<![\w.>])(printf|uprintf|tprintf|log)\s*\(")
CONVERSION = re.compile(
    r"%(?P<spec>[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*)?)?(?:hh|ll|[hljztLq])?[a-zA-Z%]?)"
)
ALLOWED = re.compile(r"[-+#0]*(\d+|\*)?(\.(\d+|\*))?l?[cdopsuxX]\Z")
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def strip_comments(text):
    """Blank comments and preprocessor lines so they are not read as calls."""
    out = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    out = re.sub(r"//[^\n]*", "", out)
    return re.sub(r"(?m)^[ \t]*#.*$", "", out)


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


def literal_format(argument):
    """The concatenated string literals forming argument, or None."""
    literals = LITERAL.findall(argument)
    if not literals or LITERAL.sub("", argument).strip():
        return None
    return "".join(literals).replace("\\\\", "")


def check_text(path, text):
    """Return (problems, checked) for one source."""
    text = strip_comments(text)
    problems = []
    checked = 0
    for call in CALL.finditer(text):
        name = call.group(1)
        line = text.count("\n", 0, call.start()) + 1
        args = argument_text(text, call.end())
        index = FUNCTIONS[name]
        if len(args) <= index:
            continue
        if any(arg.strip() == "..." for arg in args) or re.search(r"\bchar\s*\*", args[index]):
            continue  # a declaration or definition, not a call
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
