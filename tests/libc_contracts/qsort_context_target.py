"""Calibrate context-sort visibility, target types and Smaller C declarations."""

import argparse
import shlex
import subprocess
import tempfile
from pathlib import Path

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-ffreestanding",
         "-fno-builtin", "-fno-common", "-Os", "-nostdinc"]
SIGNATURE = """void (*sort_context)(void *, size_t, size_t,
    int (*)(const void *, const void *, void *), void *) = qsort_r;
"""
MODES = [
    ("default", {}, True),
    ("iso", {"_ANSI_SOURCE": 1}, False),
    ("issue7", {"_POSIX_C_SOURCE": 200809}, False),
    ("issue8", {"_POSIX_C_SOURCE": 202405}, True),
    ("xsi7", {"_XOPEN_SOURCE": 700}, False),
    ("xsi8", {"_XOPEN_SOURCE": 800}, True),
    ("bsd", {"_BSD_SOURCE": 1, "_ANSI_SOURCE": 1}, True),
    ("default-additive", {"_DEFAULT_SOURCE": 1, "_POSIX_C_SOURCE": 200809}, True),
]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(arguments, *, cwd=None, reject=None):
    result = subprocess.run(arguments, cwd=cwd, capture_output=True, text=True,
                            timeout=90, check=False)
    if reject is None:
        require(result.returncode == 0 and not result.stderr,
                "context-sort target command fails: " + repr(arguments) + "\n" +
                result.stdout + result.stderr)
    else:
        require(result.returncode != 0 and reject in result.stdout + result.stderr,
                "context-sort control lacks named rejection: " + repr(result))
    return result.stdout


def fixture(selectors, visible, order):
    text = "".join(f"#define {name} {value}L\n" for name, value in selectors.items())
    if order == "peers-before":
        text += "#include <stddef.h>\n#include <string.h>\n"
    text += "#include <stdlib.h>\n"
    if order == "repeated":
        text += "#define qsort 123\n#include <stdlib.h>\n#undef qsort\n"
    if order == "peers-after":
        text += "#include <string.h>\n#include <stddef.h>\n"
    text += SIGNATURE if visible else "int qsort_r;\n"
    text += "void (*plain_sort)(void *, size_t, size_t,\n"
    text += "    int (*)(const void *, const void *)) = qsort;\n"
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("--host", default="cc")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = args.compiler
    require(compiler.endswith("gcc"), "target compiler must name the GCC driver")
    prefix = compiler[:-3]
    with tempfile.TemporaryDirectory(prefix="qsort-context-target-") as temporary:
        work = Path(temporary)
        flags = FLAGS + ["-I" + str(root / "include")]
        source = work / "fixture.c"
        for label, selectors, visible in MODES:
            for order in ("standalone", "repeated", "peers-before", "peers-after"):
                source.write_text(fixture(selectors, visible, order))
                command([compiler, *flags, "-c", str(source), "-o", str(work / "fixture.o")])
            source.write_text(fixture(selectors, not visible, "standalone"))
            command([compiler, *flags, "-c", str(source), "-o", str(work / "bad.o")],
                    reject="qsort_r")
            print("context-sort target: " + label + " visibility and opposite control pass")
        source.write_text(fixture({}, True, "standalone").replace(
            "const void *, const void *, void *", "void *, const void *, const void *"))
        command([compiler, *flags, "-c", str(source), "-o", str(work / "bad.o")],
                reject="incompatible-pointer-types")
        for name in ("qsort", "qsort_r"):
            command([compiler, *flags, "-fstack-usage", "-c",
                    str(root / "lib/libc/gen" / (name + ".c")),
                    "-o", str(work / (name + ".o"))])
        print(command([prefix + "size", str(work / "qsort.o"), str(work / "qsort_r.o")]), end="")
        for name in ("qsort", "qsort_r"):
            print((work / (name + ".su")).read_text(), end="")

        smaller = work / "smlrc"
        command([*shlex.split(args.host), "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                "-DSYNTAX_STACK_MAX=3200", "-o", str(smaller),
                str(root / "usr.bin/smlrc/smlrc.c")])
        for label, selectors, visible in MODES:
            source.write_text(fixture(selectors, visible, "repeated"))
            command([compiler, "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
                    "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
                    "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
                    "-D__DISCOBSD__", str(source), "-o", str(work / "input.i")])
            command([str(smaller), "input.i", "output.s"], cwd=work)
            command([prefix + "as", "-mcpu=cortex-m0plus", "output.s", "-o", "native.o"], cwd=work)
            print("context-sort native declarations: " + label + " passes")
        (work / "invalid.i").write_text("int broken( {\n")
        command([str(smaller), "invalid.i", "invalid.s"], cwd=work, reject="Error")
    print("context-sort target: signatures, visibility and native input controls pass")


if __name__ == "__main__":
    main()
