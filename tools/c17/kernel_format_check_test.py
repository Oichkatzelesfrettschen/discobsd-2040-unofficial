"""Calibrate kernel_format_check.py against marked accepted and rejected calls.

Every line of fixtures/kernel_formats.c marked "expect: reject" must be
reported and no other line may be. Each known-bad checker must fail the
fixture:

  - one accepting every conversion GCC accepts, or l on every conversion;
  - one reading every call as a declaration;
  - one stripping comment delimiters inside literals;
  - one blind to directives inside a call, or to macro bodies;
  - one rejecting a macro that forwards its caller's format;
  - one blind to the DEBUG wrappers, to object-like aliases or to other
    forwarding wrappers;
  - one blind to (printf)(...), to (*printf)(...) and (&printf)(...), or
    reading one grouping layer;
  - one rejecting u8 literals or parenthesized literals;
  - one blind to line splices, or splitting tokens at them;
  - one finding calls inside string literals;
  - one reading escapes undecoded;
  - one dropping the literal-format requirement.

The checker itself must fail when it finds no source or no literal format,
source discovery must report a build whose CFILES is empty and add the headers
a build includes; an alias must apply within a translation unit and not
leak into another.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kernel_format_check  # noqa: E402

FIXTURE = HERE / "fixtures" / "kernel_formats.c"


def reported(text):
    problems, checked = kernel_format_check.check_text("fixture.c", text)
    return {int(p.split(":")[1]) for p in problems}, checked


def main():
    text = FIXTURE.read_text()
    marked = {n for n, line in enumerate(text.splitlines(), 1) if "expect: reject" in line}
    failures = []
    got, checked = reported(text)
    if len(marked) < 10:
        failures.append("fixture lost its expectation markers")
    for line in sorted(marked - got):
        failures.append(f"accepted marked line {line}")
    for line in sorted(got - marked):
        failures.append(f"rejected unmarked line {line}")
    if checked < 10:
        failures.append(f"only {checked} literal formats were checked")

    saved = kernel_format_check.ALLOWED
    kernel_format_check.ALLOWED = re.compile(r".*")
    try:
        if reported(text)[0] == got:
            failures.append("a checker accepting every conversion matched the fixture")
    finally:
        kernel_format_check.ALLOWED = saved
    for label, name, pattern in (
        ("l on every conversion", "ALLOWED", r"[-+#0]*(\d+|\*)?(\.(\d+|\*))?l?[cdopsuxX]\Z"),
        ("every call read as a declaration", "DECLARATION_PREFIX", r""),
    ):
        saved_pattern = getattr(kernel_format_check, name)
        setattr(kernel_format_check, name, re.compile(pattern))
        try:
            if reported(text)[0] == got:
                failures.append(f"a checker with {label} matched the fixture")
        finally:
            setattr(kernel_format_check, name, saved_pattern)

    def regex_strip(source):
        out = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), source, flags=re.S)
        return re.sub(r"//[^\n]*", "", out)

    saved_strip = kernel_format_check.strip_comments
    kernel_format_check.strip_comments = regex_strip
    try:
        if reported(text)[0] == got:
            failures.append("a checker stripping comments inside literals matched the fixture")
    finally:
        kernel_format_check.strip_comments = saved_strip
    kernel_format_check.strip_comments = lambda source: saved_strip(source).replace(
        kernel_format_check.DIRECTIVE, " "
    )
    try:
        if reported(text)[0] == got:
            failures.append("a checker blind to directives inside a call matched the fixture")
    finally:
        kernel_format_check.strip_comments = saved_strip
    saved_call = kernel_format_check.CALL
    names = r"(?P<name>DEBUG[1-9]?|printf|uprintf|tprintf|log)"
    for label, pattern in (
        (
            "blind to the DEBUG wrappers",
            r"(?<![\w.>])(?P<open>)(?P<name>printf|uprintf|tprintf|log)(?P<close>)\s*\(",
        ),
        (
            "blind to parenthesized designators",
            r"(?<![\w.>])(?P<open>)" + names + r"(?P<close>)\s*\(",
        ),
        (
            "blind to dereferenced designators",
            r"(?<![\w.>])(?P<open>\(\s*)?" + names + r"(?P<close>(?(open)\s*\)))\s*\(",
        ),
        (
            "reading one grouping layer",
            r"(?<![\w.>])(?P<open>\(\s*(?:[*&]\s*)*)?" + names + r"(?P<close>(?(open)\s*\)))\s*\(",
        ),
    ):
        kernel_format_check.CALL = re.compile(pattern)
        try:
            if reported(text)[0] == got:
                failures.append(f"a checker {label} matched the fixture")
        finally:
            kernel_format_check.CALL = saved_call
    saved_literal_pattern = kernel_format_check.LITERAL
    kernel_format_check.LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    try:
        if reported(text)[0] == got:
            failures.append("a checker rejecting u8 literals matched the fixture")
    finally:
        kernel_format_check.LITERAL = saved_literal_pattern

    # An empty discovery must fail rather than report every format sound.
    for argv, label in (
        ([], "no sources and no build"),
        ([str(HERE / "fixtures" / "no_formats.c")], "a source with no literal format"),
    ):
        result = subprocess.run(
            [sys.executable, str(HERE / "kernel_format_check.py"), "--root", "/"] + argv,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            failures.append(f"the checker passed with {label}")

    saved_head = kernel_format_check.DEFINE_HEAD
    kernel_format_check.DEFINE_HEAD = re.compile(r"(?!)")
    try:
        if reported(text)[0] == got:
            failures.append("a checker blind to macro bodies matched the fixture")
    finally:
        kernel_format_check.DEFINE_HEAD = saved_head

    saved_forwarded = kernel_format_check.forwarded_formats
    kernel_format_check.forwarded_formats = lambda source: []
    try:
        if reported(text)[0] == got:
            failures.append("a checker rejecting forwarding macros matched the fixture")
    finally:
        kernel_format_check.forwarded_formats = saved_forwarded

    saved_join = kernel_format_check.join_splices
    for label, join in (
        ("blind to line splices", lambda source: source),
        ("splitting tokens at line splices", lambda source: source.replace("\\\n", " \n")),
    ):
        kernel_format_check.join_splices = join
        try:
            if reported(text)[0] == got:
                failures.append(f"a checker {label} matched the fixture")
        finally:
            kernel_format_check.join_splices = saved_join

    saved_mask = kernel_format_check.mask_literals
    kernel_format_check.mask_literals = lambda source: source
    try:
        if reported(text)[0] == got:
            failures.append("a checker finding calls inside literals matched the fixture")
    finally:
        kernel_format_check.mask_literals = saved_mask

    saved_decode = kernel_format_check.decode_literal
    kernel_format_check.decode_literal = lambda body: body
    try:
        if reported(text)[0] == got:
            failures.append("a checker reading escapes undecoded matched the fixture")
    finally:
        kernel_format_check.decode_literal = saved_decode

    saved_unwrap = kernel_format_check.unparenthesize
    kernel_format_check.unparenthesize = lambda argument: argument
    try:
        if reported(text)[0] == got:
            failures.append("a checker rejecting parenthesized literals matched the fixture")
    finally:
        kernel_format_check.unparenthesize = saved_unwrap

    # Each build must list sources; an empty CFILES in one configuration is
    # a discovery failure even when another configuration supplies formats.
    # The headers a build includes join the sources.
    saved_value = kernel_format_check.c17_inventory.make_value
    saved_headers = kernel_format_check.header_dependencies
    listed = {"PICO": "../../fixture.c", "PICO_UART": "../../fixture.c"}
    kernel_format_check.c17_inventory.make_value = lambda make, build, name: listed[build.name]
    kernel_format_check.header_dependencies = lambda make, root, build, items: (
        {"fixture.c": {"include/helper.h"}} if items else {}
    )
    try:
        found, both, scope = kernel_format_check.sources_from_builds(
            "make", Path("/r"), list(listed)
        )
        listed["PICO"] = ""
        _, one, _ = kernel_format_check.sources_from_builds("make", Path("/r"), list(listed))
    finally:
        kernel_format_check.c17_inventory.make_value = saved_value
        kernel_format_check.header_dependencies = saved_headers
    if both:
        failures.append(f"two listing builds reported {both}")
    if one != ["PICO: CFILES is empty"]:
        failures.append(f"a build with empty CFILES reported {one}")
    if "include/helper.h" not in found or scope.get("fixture.c") != {"include/helper.h"}:
        failures.append(f"included headers did not join the sources: {found} {scope}")
    header = (HERE / "fixtures" / "kernel_header.h").read_text()
    if len(kernel_format_check.check_text("kernel_header.h", header)[0]) != 1:
        failures.append("the format in an included header's inline function was not reported")

    # An alias defined in one source applies to calls in another.
    aliases = kernel_format_check.aliases_in(["#define KLOG log\n"])
    if kernel_format_check.check_text("u.c", 'void f(void) { KLOG(1, "%i\\n", 2); }\n', aliases)[
        0
    ] != ["u.c:1: KLOG uses %i, outside the conversions prf() and GCC share"]:
        failures.append("an alias from another source was not applied")
    # Aliases are scoped to a translation unit: one source's alias does not
    # rename another source's unrelated function of the same name.
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "a.c").write_text('#define REPORT printf\nvoid f(void) { REPORT("%d", 1); }\n')
        (Path(tmp) / "b.c").write_text("int REPORT(int);\nvoid g(void) { REPORT(1); }\n")
        script = str(HERE / "kernel_format_check.py")
        scoped = subprocess.run(
            [sys.executable, script, "--root", tmp, "a.c", "b.c"], capture_output=True, text=True
        )
        if scoped.returncode != 0:
            failures.append("an alias leaked into another translation unit:\n" + scoped.stderr)
        saved_aliases_in = kernel_format_check.aliases_in
        everything = [(Path(tmp) / name).read_text() for name in ("a.c", "b.c")]
        try:
            kernel_format_check.aliases_in = lambda texts: saved_aliases_in(everything)
            leaked = kernel_format_check.check_text("b.c", everything[1], None)[0]
        finally:
            kernel_format_check.aliases_in = saved_aliases_in
        if not leaked:
            failures.append("a checker sharing aliases across units accepted b.c")
    saved_wrapper = kernel_format_check.FUNCTION_WRAPPER
    kernel_format_check.FUNCTION_WRAPPER = re.compile(r"(?!)")
    try:
        if reported(text)[0] == got:
            failures.append("a checker blind to forwarding wrappers matched the fixture")
    finally:
        kernel_format_check.FUNCTION_WRAPPER = saved_wrapper
    saved_alias = kernel_format_check.OBJECT_ALIAS
    kernel_format_check.OBJECT_ALIAS = re.compile(r"(?!)")
    try:
        if reported(text)[0] == got:
            failures.append("a checker blind to object-like aliases matched the fixture")
    finally:
        kernel_format_check.OBJECT_ALIAS = saved_alias

    saved_literal = kernel_format_check.literal_format
    kernel_format_check.literal_format = lambda argument: saved_literal(argument) or ""
    try:
        if reported(text)[0] == got:
            failures.append("a checker accepting non-literal formats matched the fixture")
    finally:
        kernel_format_check.literal_format = saved_literal

    if failures:
        for failure in failures:
            print("FAIL " + failure, file=sys.stderr)
        return 1
    print(
        f"PASS kernel format calibration: {len(marked)} rejected calls reported, "
        f"{checked} literal formats read, known-bad checkers rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
